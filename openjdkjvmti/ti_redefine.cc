/* Copyright (C) 2016 The Android Open Source Project
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This file implements interfaces from the file jvmti.h. This implementation
 * is licensed under the same terms as the file jvmti.h.  The
 * copyright and license information for the file jvmti.h follows.
 *
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "ti_redefine.h"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include "alloc_manager.h"
#include "android-base/macros.h"
#include "android-base/thread_annotations.h"
#include "art_field-inl.h"
#include "art_field.h"
#include "art_jvmti.h"
#include "art_method-inl.h"
#include "art_method.h"
#include "base/array_ref.h"
#include "base/casts.h"
#include "base/enums.h"
#include "base/globals.h"
#include "base/iteration_range.h"
#include "base/length_prefixed_array.h"
#include "base/locks.h"
#include "base/stl_util.h"
#include "base/utils.h"
#include "class_linker-inl.h"
#include "class_linker.h"
#include "class_root-inl.h"
#include "class_status.h"
#include "debugger.h"
#include "dex/art_dex_file_loader.h"
#include "dex/class_accessor-inl.h"
#include "dex/class_accessor.h"
#include "dex/dex_file.h"
#include "dex/dex_file_loader.h"
#include "dex/dex_file_types.h"
#include "dex/primitive.h"
#include "dex/signature-inl.h"
#include "dex/signature.h"
#include "events-inl.h"
#include "events.h"
#include "gc/allocation_listener.h"
#include "gc/heap.h"
#include "gc/heap-inl.h"
#include "gc/heap-visit-objects-inl.h"
#include "handle.h"
#include "handle_scope.h"
#include "instrumentation.h"
#include "intern_table.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "jni/jni_env_ext-inl.h"
#include "jni/jni_id_manager.h"
#include "jvmti.h"
#include "jvmti_allocator.h"
#include "linear_alloc.h"
#include "mirror/array-alloc-inl.h"
#include "mirror/array.h"
#include "mirror/class-alloc-inl.h"
#include "mirror/class-inl.h"
#include "mirror/class-refvisitor-inl.h"
#include "mirror/class.h"
#include "mirror/class_ext-inl.h"
#include "mirror/dex_cache-inl.h"
#include "mirror/dex_cache.h"
#include "mirror/executable-inl.h"
#include "mirror/field-inl.h"
#include "mirror/field.h"
#include "mirror/method.h"
#include "mirror/method_handle_impl-inl.h"
#include "mirror/object.h"
#include "mirror/object_array-alloc-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/object_array.h"
#include "mirror/string.h"
#include "mirror/var_handle.h"
#include "nativehelper/scoped_local_ref.h"
#include "non_debuggable_classes.h"
#include "obj_ptr.h"
#include "object_lock.h"
#include "reflective_value_visitor.h"
#include "runtime.h"
#include "runtime_globals.h"
#include "scoped_thread_state_change.h"
#include "stack.h"
#include "thread.h"
#include "thread_list.h"
#include "ti_breakpoint.h"
#include "ti_class_definition.h"
#include "ti_class_loader.h"
#include "ti_heap.h"
#include "ti_logging.h"
#include "ti_thread.h"
#include "transform.h"
#include "verifier/class_verifier.h"
#include "verifier/verifier_enums.h"
#include "well_known_classes.h"
#include "write_barrier.h"

namespace openjdkjvmti {

// Debug check to force us to directly check we saw all methods and fields exactly once directly.
// Normally we don't need to do this since if any are missing the count will be different
constexpr bool kCheckAllMethodsSeenOnce = art::kIsDebugBuild;

using android::base::StringPrintf;

// A helper that fills in a classes obsolete_methods_ and obsolete_dex_caches_ classExt fields as
// they are created. This ensures that we can always call any method of an obsolete ArtMethod object
// almost as soon as they are created since the GetObsoleteDexCache method will succeed.
class ObsoleteMap {
 public:
  art::ArtMethod* FindObsoleteVersion(art::ArtMethod* original) const
      REQUIRES(art::Locks::mutator_lock_, art::Roles::uninterruptible_) {
    auto method_pair = id_map_.find(original);
    if (method_pair != id_map_.end()) {
      art::ArtMethod* res = obsolete_methods_->GetElementPtrSize<art::ArtMethod*>(
          method_pair->second, art::kRuntimePointerSize);
      DCHECK(res != nullptr);
      return res;
    } else {
      return nullptr;
    }
  }

  void RecordObsolete(art::ArtMethod* original, art::ArtMethod* obsolete)
      REQUIRES(art::Locks::mutator_lock_, art::Roles::uninterruptible_) {
    DCHECK(original != nullptr);
    DCHECK(obsolete != nullptr);
    int32_t slot = next_free_slot_++;
    DCHECK_LT(slot, obsolete_methods_->GetLength());
    DCHECK(nullptr ==
           obsolete_methods_->GetElementPtrSize<art::ArtMethod*>(slot, art::kRuntimePointerSize));
    DCHECK(nullptr == obsolete_dex_caches_->Get(slot));
    obsolete_methods_->SetElementPtrSize(slot, obsolete, art::kRuntimePointerSize);
    obsolete_dex_caches_->Set(slot, original_dex_cache_);
    id_map_.insert({original, slot});
  }

  ObsoleteMap(art::ObjPtr<art::mirror::PointerArray> obsolete_methods,
              art::ObjPtr<art::mirror::ObjectArray<art::mirror::DexCache>> obsolete_dex_caches,
              art::ObjPtr<art::mirror::DexCache> original_dex_cache)
      : next_free_slot_(0),
        obsolete_methods_(obsolete_methods),
        obsolete_dex_caches_(obsolete_dex_caches),
        original_dex_cache_(original_dex_cache) {
    // Figure out where the first unused slot in the obsolete_methods_ array is.
    while (obsolete_methods_->GetElementPtrSize<art::ArtMethod*>(
        next_free_slot_, art::kRuntimePointerSize) != nullptr) {
      DCHECK(obsolete_dex_caches_->Get(next_free_slot_) != nullptr);
      next_free_slot_++;
    }
    // Sanity check that the same slot in obsolete_dex_caches_ is free.
    DCHECK(obsolete_dex_caches_->Get(next_free_slot_) == nullptr);
  }

  struct ObsoleteMethodPair {
    art::ArtMethod* old_method;
    art::ArtMethod* obsolete_method;
  };

  class ObsoleteMapIter {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = ObsoleteMethodPair;
    using difference_type = ptrdiff_t;
    using pointer = void;    // Unsupported.
    using reference = void;  // Unsupported.

    ObsoleteMethodPair operator*() const
        REQUIRES(art::Locks::mutator_lock_, art::Roles::uninterruptible_) {
      art::ArtMethod* obsolete = map_->obsolete_methods_->GetElementPtrSize<art::ArtMethod*>(
          iter_->second, art::kRuntimePointerSize);
      DCHECK(obsolete != nullptr);
      return { iter_->first, obsolete };
    }

    bool operator==(ObsoleteMapIter other) const {
      return map_ == other.map_ && iter_ == other.iter_;
    }

    bool operator!=(ObsoleteMapIter other) const {
      return !(*this == other);
    }

    ObsoleteMapIter operator++(int) {
      ObsoleteMapIter retval = *this;
      ++(*this);
      return retval;
    }

    ObsoleteMapIter operator++() {
      ++iter_;
      return *this;
    }

   private:
    ObsoleteMapIter(const ObsoleteMap* map,
                    std::unordered_map<art::ArtMethod*, int32_t>::const_iterator iter)
        : map_(map), iter_(iter) {}

    const ObsoleteMap* map_;
    std::unordered_map<art::ArtMethod*, int32_t>::const_iterator iter_;

    friend class ObsoleteMap;
  };

  ObsoleteMapIter end() const {
    return ObsoleteMapIter(this, id_map_.cend());
  }

  ObsoleteMapIter begin() const {
    return ObsoleteMapIter(this, id_map_.cbegin());
  }

 private:
  int32_t next_free_slot_;
  std::unordered_map<art::ArtMethod*, int32_t> id_map_;
  // Pointers to the fields in mirror::ClassExt. These can be held as ObjPtr since this is only used
  // when we have an exclusive mutator_lock_ (i.e. all threads are suspended).
  art::ObjPtr<art::mirror::PointerArray> obsolete_methods_;
  art::ObjPtr<art::mirror::ObjectArray<art::mirror::DexCache>> obsolete_dex_caches_;
  art::ObjPtr<art::mirror::DexCache> original_dex_cache_;
};

// This visitor walks thread stacks and allocates and sets up the obsolete methods. It also does
// some basic sanity checks that the obsolete method is sane.
class ObsoleteMethodStackVisitor : public art::StackVisitor {
 protected:
  ObsoleteMethodStackVisitor(
      art::Thread* thread,
      art::LinearAlloc* allocator,
      const std::unordered_set<art::ArtMethod*>& obsoleted_methods,
      ObsoleteMap* obsolete_maps)
        : StackVisitor(thread,
                       /*context=*/nullptr,
                       StackVisitor::StackWalkKind::kIncludeInlinedFrames),
          allocator_(allocator),
          obsoleted_methods_(obsoleted_methods),
          obsolete_maps_(obsolete_maps) { }

  ~ObsoleteMethodStackVisitor() override {}

 public:
  // Returns true if we successfully installed obsolete methods on this thread, filling
  // obsolete_maps_ with the translations if needed. Returns false and fills error_msg if we fail.
  // The stack is cleaned up when we fail.
  static void UpdateObsoleteFrames(
      art::Thread* thread,
      art::LinearAlloc* allocator,
      const std::unordered_set<art::ArtMethod*>& obsoleted_methods,
      ObsoleteMap* obsolete_maps)
        REQUIRES(art::Locks::mutator_lock_) {
    ObsoleteMethodStackVisitor visitor(thread,
                                       allocator,
                                       obsoleted_methods,
                                       obsolete_maps);
    visitor.WalkStack();
  }

  bool VisitFrame() override REQUIRES(art::Locks::mutator_lock_) {
    art::ScopedAssertNoThreadSuspension snts("Fixing up the stack for obsolete methods.");
    art::ArtMethod* old_method = GetMethod();
    if (obsoleted_methods_.find(old_method) != obsoleted_methods_.end()) {
      // We cannot ensure that the right dex file is used in inlined frames so we don't support
      // redefining them.
      DCHECK(!IsInInlinedFrame()) << "Inlined frames are not supported when using redefinition: "
                                  << old_method->PrettyMethod() << " is inlined into "
                                  << GetOuterMethod()->PrettyMethod();
      art::ArtMethod* new_obsolete_method = obsolete_maps_->FindObsoleteVersion(old_method);
      if (new_obsolete_method == nullptr) {
        // Create a new Obsolete Method and put it in the list.
        art::Runtime* runtime = art::Runtime::Current();
        art::ClassLinker* cl = runtime->GetClassLinker();
        auto ptr_size = cl->GetImagePointerSize();
        const size_t method_size = art::ArtMethod::Size(ptr_size);
        auto* method_storage = allocator_->Alloc(art::Thread::Current(), method_size);
        CHECK(method_storage != nullptr) << "Unable to allocate storage for obsolete version of '"
                                         << old_method->PrettyMethod() << "'";
        new_obsolete_method = new (method_storage) art::ArtMethod();
        new_obsolete_method->CopyFrom(old_method, ptr_size);
        DCHECK_EQ(new_obsolete_method->GetDeclaringClass(), old_method->GetDeclaringClass());
        new_obsolete_method->SetIsObsolete();
        new_obsolete_method->SetDontCompile();
        cl->SetEntryPointsForObsoleteMethod(new_obsolete_method);
        obsolete_maps_->RecordObsolete(old_method, new_obsolete_method);
      }
      DCHECK(new_obsolete_method != nullptr);
      SetMethod(new_obsolete_method);
    }
    return true;
  }

 private:
  // The linear allocator we should use to make new methods.
  art::LinearAlloc* allocator_;
  // The set of all methods which could be obsoleted.
  const std::unordered_set<art::ArtMethod*>& obsoleted_methods_;
  // A map from the original to the newly allocated obsolete method for frames on this thread. The
  // values in this map are added to the obsolete_methods_ (and obsolete_dex_caches_) fields of
  // the redefined classes ClassExt as it is filled.
  ObsoleteMap* obsolete_maps_;
};

template <RedefinitionType kType>
jvmtiError
Redefiner::IsModifiableClassGeneric(jvmtiEnv* env, jclass klass, jboolean* is_redefinable) {
  if (env == nullptr) {
    return ERR(INVALID_ENVIRONMENT);
  }
  art::Thread* self = art::Thread::Current();
  art::ScopedObjectAccess soa(self);
  art::StackHandleScope<1> hs(self);
  art::ObjPtr<art::mirror::Object> obj(self->DecodeJObject(klass));
  if (obj.IsNull() || !obj->IsClass()) {
    return ERR(INVALID_CLASS);
  }
  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(obj->AsClass()));
  std::string err_unused;
  *is_redefinable =
      Redefiner::GetClassRedefinitionError<kType>(h_klass, &err_unused) != ERR(UNMODIFIABLE_CLASS)
          ? JNI_TRUE
          : JNI_FALSE;
  return OK;
}

jvmtiError
Redefiner::IsStructurallyModifiableClass(jvmtiEnv* env, jclass klass, jboolean* is_redefinable) {
  return Redefiner::IsModifiableClassGeneric<RedefinitionType::kStructural>(
      env, klass, is_redefinable);
}

jvmtiError Redefiner::IsModifiableClass(jvmtiEnv* env, jclass klass, jboolean* is_redefinable) {
  return Redefiner::IsModifiableClassGeneric<RedefinitionType::kNormal>(env, klass, is_redefinable);
}

template <RedefinitionType kType>
jvmtiError Redefiner::GetClassRedefinitionError(jclass klass, /*out*/ std::string* error_msg) {
  art::Thread* self = art::Thread::Current();
  art::ScopedObjectAccess soa(self);
  art::StackHandleScope<1> hs(self);
  art::ObjPtr<art::mirror::Object> obj(self->DecodeJObject(klass));
  if (obj.IsNull() || !obj->IsClass()) {
    return ERR(INVALID_CLASS);
  }
  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(obj->AsClass()));
  return Redefiner::GetClassRedefinitionError<kType>(h_klass, error_msg);
}

template <RedefinitionType kType>
jvmtiError Redefiner::GetClassRedefinitionError(art::Handle<art::mirror::Class> klass,
                                                /*out*/ std::string* error_msg) {
  art::Thread* self = art::Thread::Current();
  if (!klass->IsResolved()) {
    // It's only a problem to try to retransform/redefine a unprepared class if it's happening on
    // the same thread as the class-linking process. If it's on another thread we will be able to
    // wait for the preparation to finish and continue from there.
    if (klass->GetLockOwnerThreadId() == self->GetThreadId()) {
      *error_msg = "Modification of class " + klass->PrettyClass() +
          " from within the classes ClassLoad callback is not supported to prevent deadlocks." +
          " Please use ClassFileLoadHook directly instead.";
      return ERR(INTERNAL);
    } else {
      LOG(WARNING) << klass->PrettyClass() << " is not yet resolved. Attempting to transform "
                   << "it could cause arbitrary length waits as the class is being resolved.";
    }
  }
  if (klass->IsPrimitive()) {
    *error_msg = "Modification of primitive classes is not supported";
    return ERR(UNMODIFIABLE_CLASS);
  } else if (klass->IsInterface()) {
    *error_msg = "Modification of Interface classes is currently not supported";
    return ERR(UNMODIFIABLE_CLASS);
  } else if (klass->IsStringClass()) {
    *error_msg = "Modification of String class is not supported";
    return ERR(UNMODIFIABLE_CLASS);
  } else if (klass->IsArrayClass()) {
    *error_msg = "Modification of Array classes is not supported";
    return ERR(UNMODIFIABLE_CLASS);
  } else if (klass->IsProxyClass()) {
    *error_msg = "Modification of proxy classes is not supported";
    return ERR(UNMODIFIABLE_CLASS);
  }

  for (jclass c : art::NonDebuggableClasses::GetNonDebuggableClasses()) {
    if (klass.Get() == self->DecodeJObject(c)->AsClass()) {
      *error_msg = "Class might have stack frames that cannot be made obsolete";
      return ERR(UNMODIFIABLE_CLASS);
    }
  }

  if (kType == RedefinitionType::kStructural) {
    // Class initialization interacts really badly with structural redefinition since we need to
    // make the old class obsolete. We currently just blanket don't allow it.
    // TODO It might be nice to allow this at some point.
    if (klass->IsInitializing() &&
       !klass->IsInitialized() &&
        klass->GetClinitThreadId() == self->GetTid()) {
      // We are in the class-init running on this thread.
      *error_msg = "Modification of class " + klass->PrettyClass() + " during class" +
                   " initialization is not allowed.";
      return ERR(INTERNAL);
    }
    if (!art::Runtime::Current()->GetClassLinker()->EnsureInitialized(
            self, klass, /*can_init_fields=*/true, /*can_init_parents=*/true)) {
      self->AssertPendingException();
      *error_msg = "Class " + klass->PrettyClass() + " failed initialization. Structural" +
                   " redefinition of erroneous classes is not allowed. Failure was: " +
                   self->GetException()->Dump();
      self->ClearException();
      return ERR(INVALID_CLASS);
    }
    if (klass->IsMirrored()) {
      std::string pc(klass->PrettyClass());
      *error_msg = StringPrintf("Class %s is a mirror class and cannot be structurally redefined.",
                                pc.c_str());
      return ERR(UNMODIFIABLE_CLASS);
    }
    // Check Thread specifically since it's not a root but too many things reach into it with Unsafe
    // too allow structural redefinition.
    if (klass->IsAssignableFrom(
            self->DecodeJObject(art::WellKnownClasses::java_lang_Thread)->AsClass())) {
      *error_msg =
          "java.lang.Thread has fields accessed using sun.misc.unsafe directly. It is not "
          "safe to structurally redefine it.";
      return ERR(UNMODIFIABLE_CLASS);
    }
    auto has_pointer_marker =
        [](art::ObjPtr<art::mirror::Class> k) REQUIRES_SHARED(art::Locks::mutator_lock_) {
          // Check for fields/methods which were returned before moving to index jni id type.
          // TODO We might want to rework how this is done. Once full redefinition is implemented we
          // will need to check any subtypes too.
          art::ObjPtr<art::mirror::ClassExt> ext(k->GetExtData());
          if (!ext.IsNull()) {
            if (ext->HasInstanceFieldPointerIdMarker() || ext->HasMethodPointerIdMarker() ||
                ext->HasStaticFieldPointerIdMarker()) {
              return true;
            }
          }
          return false;
        };
    if (has_pointer_marker(klass.Get())) {
      *error_msg =
          StringPrintf("%s has active pointer jni-ids and cannot be redefined structurally",
                       klass->PrettyClass().c_str());
      return ERR(UNMODIFIABLE_CLASS);
    }
    jvmtiError res = OK;
    art::ClassFuncVisitor cfv(
      [&](art::ObjPtr<art::mirror::Class> k) REQUIRES_SHARED(art::Locks::mutator_lock_) {
        // if there is any class 'K' that is a subtype (i.e. extends) klass and has pointer-jni-ids
        // we cannot structurally redefine the class 'k' since we would structurally redefine the
        // subtype.
        if (k->IsLoaded() && klass->IsAssignableFrom(k) && has_pointer_marker(k)) {
          *error_msg = StringPrintf(
              "%s has active pointer jni-ids from subtype %s and cannot be redefined structurally",
              klass->PrettyClass().c_str(),
              k->PrettyClass().c_str());
          res = ERR(UNMODIFIABLE_CLASS);
          return false;
        }
        return true;
      });
    art::Runtime::Current()->GetClassLinker()->VisitClasses(&cfv);
    return res;
  }
  return OK;
}

template jvmtiError Redefiner::GetClassRedefinitionError<RedefinitionType::kNormal>(
    art::Handle<art::mirror::Class> klass, /*out*/ std::string* error_msg);
template jvmtiError Redefiner::GetClassRedefinitionError<RedefinitionType::kStructural>(
    art::Handle<art::mirror::Class> klass, /*out*/ std::string* error_msg);

// Moves dex data to an anonymous, read-only mmap'd region.
art::MemMap Redefiner::MoveDataToMemMap(const std::string& original_location,
                                        art::ArrayRef<const unsigned char> data,
                                        std::string* error_msg) {
  art::MemMap map = art::MemMap::MapAnonymous(
      StringPrintf("%s-transformed", original_location.c_str()).c_str(),
      data.size(),
      PROT_READ|PROT_WRITE,
      /*low_4gb=*/ false,
      error_msg);
  if (LIKELY(map.IsValid())) {
    memcpy(map.Begin(), data.data(), data.size());
    // Make the dex files mmap read only. This matches how other DexFiles are mmaped and prevents
    // programs from corrupting it.
    map.Protect(PROT_READ);
  }
  return map;
}

Redefiner::ClassRedefinition::ClassRedefinition(
    Redefiner* driver,
    jclass klass,
    const art::DexFile* redefined_dex_file,
    const char* class_sig,
    art::ArrayRef<const unsigned char> orig_dex_file) :
      driver_(driver),
      klass_(klass),
      dex_file_(redefined_dex_file),
      class_sig_(class_sig),
      original_dex_file_(orig_dex_file) {
  GetMirrorClass()->MonitorEnter(driver_->self_);
}

Redefiner::ClassRedefinition::~ClassRedefinition() {
  if (driver_ != nullptr) {
    GetMirrorClass()->MonitorExit(driver_->self_);
  }
}

template<RedefinitionType kType>
jvmtiError Redefiner::RedefineClassesGeneric(jvmtiEnv* jenv,
                                             jint class_count,
                                             const jvmtiClassDefinition* definitions) {
  art::Runtime* runtime = art::Runtime::Current();
  art::Thread* self = art::Thread::Current();
  ArtJvmTiEnv* env = ArtJvmTiEnv::AsArtJvmTiEnv(jenv);
  if (env == nullptr) {
    JVMTI_LOG(WARNING, env) << "FAILURE TO REDEFINE env was null!";
    return ERR(INVALID_ENVIRONMENT);
  } else if (class_count < 0) {
    JVMTI_LOG(WARNING, env) << "FAILURE TO REDEFINE class_count was less then 0";
    return ERR(ILLEGAL_ARGUMENT);
  } else if (class_count == 0) {
    // We don't actually need to do anything. Just return OK.
    return OK;
  } else if (definitions == nullptr) {
    JVMTI_LOG(WARNING, env) << "FAILURE TO REDEFINE null definitions!";
    return ERR(NULL_POINTER);
  }
  std::string error_msg;
  std::vector<ArtClassDefinition> def_vector;
  def_vector.reserve(class_count);
  for (jint i = 0; i < class_count; i++) {
    jvmtiError res = Redefiner::GetClassRedefinitionError<RedefinitionType::kNormal>(
        definitions[i].klass, &error_msg);
    if (res != OK) {
      JVMTI_LOG(WARNING, env) << "FAILURE TO REDEFINE " << error_msg;
      return res;
    }
    ArtClassDefinition def;
    res = def.Init(self, definitions[i]);
    if (res != OK) {
      JVMTI_LOG(WARNING, env) << "FAILURE TO REDEFINE bad definition " << i;
      return res;
    }
    def_vector.push_back(std::move(def));
  }
  // Call all the transformation events.
  Transformer::RetransformClassesDirect<kType>(self, &def_vector);
  if (kType == RedefinitionType::kStructural) {
    Transformer::RetransformClassesDirect<RedefinitionType::kNormal>(self, &def_vector);
  }
  jvmtiError res = RedefineClassesDirect(env, runtime, self, def_vector, kType, &error_msg);
  if (res != OK) {
    JVMTI_LOG(WARNING, env) << "FAILURE TO REDEFINE " << error_msg;
  }
  return res;
}

jvmtiError Redefiner::StructurallyRedefineClasses(jvmtiEnv* jenv,
                                                  jint class_count,
                                                  const jvmtiClassDefinition* definitions) {
  ArtJvmTiEnv* art_env = ArtJvmTiEnv::AsArtJvmTiEnv(jenv);
  if (art_env == nullptr) {
    return ERR(INVALID_ENVIRONMENT);
  } else if (art_env->capabilities.can_redefine_classes != 1) {
    return ERR(MUST_POSSESS_CAPABILITY);
  }
  return RedefineClassesGeneric<RedefinitionType::kStructural>(jenv, class_count, definitions);
}

jvmtiError Redefiner::RedefineClasses(jvmtiEnv* jenv,
                                      jint class_count,
                                      const jvmtiClassDefinition* definitions) {
  return RedefineClassesGeneric<RedefinitionType::kNormal>(jenv, class_count, definitions);
}

jvmtiError Redefiner::StructurallyRedefineClassDirect(jvmtiEnv* env,
                                                      jclass klass,
                                                      const unsigned char* data,
                                                      jint data_size) {
  if (env == nullptr) {
    return ERR(INVALID_ENVIRONMENT);
  } else if (ArtJvmTiEnv::AsArtJvmTiEnv(env)->capabilities.can_redefine_classes != 1) {
    JVMTI_LOG(INFO, env) << "Does not have can_redefine_classes cap!";
    return ERR(MUST_POSSESS_CAPABILITY);
  }
  std::vector<ArtClassDefinition> acds;
  ArtClassDefinition acd;
  jvmtiError err = acd.Init(
      art::Thread::Current(),
      jvmtiClassDefinition{ .klass = klass, .class_byte_count = data_size, .class_bytes = data });
  if (err != OK) {
    return err;
  }
  acds.push_back(std::move(acd));
  std::string err_msg;
  err = RedefineClassesDirect(ArtJvmTiEnv::AsArtJvmTiEnv(env),
                              art::Runtime::Current(),
                              art::Thread::Current(),
                              acds,
                              RedefinitionType::kStructural,
                              &err_msg);
  if (err != OK) {
    JVMTI_LOG(WARNING, env) << "Failed structural redefinition: " << err_msg;
  }
  return err;
}

jvmtiError Redefiner::RedefineClassesDirect(ArtJvmTiEnv* env,
                                            art::Runtime* runtime,
                                            art::Thread* self,
                                            const std::vector<ArtClassDefinition>& definitions,
                                            RedefinitionType type,
                                            std::string* error_msg) {
  DCHECK(env != nullptr);
  if (definitions.size() == 0) {
    // We don't actually need to do anything. Just return OK.
    return OK;
  }
  // We need to fiddle with the verification class flags. To do this we need to make sure there are
  // no concurrent redefinitions of the same class at the same time. For simplicity and because
  // this is not expected to be a common occurrence we will just wrap the whole thing in a TOP-level
  // lock.

  // Stop JIT for the duration of this redefine since the JIT might concurrently compile a method we
  // are going to redefine.
  // TODO We should prevent user-code suspensions to make sure this isn't held for too long.
  art::jit::ScopedJitSuspend suspend_jit;
  // Get shared mutator lock so we can lock all the classes.
  art::ScopedObjectAccess soa(self);
  Redefiner r(env, runtime, self, type, error_msg);
  for (const ArtClassDefinition& def : definitions) {
    // Only try to transform classes that have been modified.
    if (def.IsModified()) {
      jvmtiError res = r.AddRedefinition(env, def);
      if (res != OK) {
        return res;
      }
    }
  }
  return r.Run();
}

jvmtiError Redefiner::AddRedefinition(ArtJvmTiEnv* env, const ArtClassDefinition& def) {
  std::string original_dex_location;
  jvmtiError ret = OK;
  if ((ret = GetClassLocation(env, def.GetClass(), &original_dex_location))) {
    *error_msg_ = "Unable to get original dex file location!";
    return ret;
  }
  char* generic_ptr_unused = nullptr;
  char* signature_ptr = nullptr;
  if ((ret = env->GetClassSignature(def.GetClass(), &signature_ptr, &generic_ptr_unused)) != OK) {
    *error_msg_ = "Unable to get class signature!";
    return ret;
  }
  JvmtiUniquePtr<char> generic_unique_ptr(MakeJvmtiUniquePtr(env, generic_ptr_unused));
  JvmtiUniquePtr<char> signature_unique_ptr(MakeJvmtiUniquePtr(env, signature_ptr));
  art::MemMap map = MoveDataToMemMap(original_dex_location, def.GetDexData(), error_msg_);
  std::ostringstream os;
  if (!map.IsValid()) {
    os << "Failed to create anonymous mmap for modified dex file of class " << def.GetName()
       << "in dex file " << original_dex_location << " because: " << *error_msg_;
    *error_msg_ = os.str();
    return ERR(OUT_OF_MEMORY);
  }
  if (map.Size() < sizeof(art::DexFile::Header)) {
    *error_msg_ = "Could not read dex file header because dex_data was too short";
    return ERR(INVALID_CLASS_FORMAT);
  }
  std::string name = map.GetName();
  uint32_t checksum = reinterpret_cast<const art::DexFile::Header*>(map.Begin())->checksum_;
  const art::ArtDexFileLoader dex_file_loader;
  std::unique_ptr<const art::DexFile> dex_file(dex_file_loader.Open(name,
                                                                    checksum,
                                                                    std::move(map),
                                                                    /*verify=*/true,
                                                                    /*verify_checksum=*/true,
                                                                    error_msg_));
  if (dex_file.get() == nullptr) {
    os << "Unable to load modified dex file for " << def.GetName() << ": " << *error_msg_;
    *error_msg_ = os.str();
    return ERR(INVALID_CLASS_FORMAT);
  }
  redefinitions_.push_back(
      Redefiner::ClassRedefinition(this,
                                   def.GetClass(),
                                   dex_file.release(),
                                   signature_ptr,
                                   def.GetNewOriginalDexFile()));
  return OK;
}

art::ObjPtr<art::mirror::Class> Redefiner::ClassRedefinition::GetMirrorClass() {
  return driver_->self_->DecodeJObject(klass_)->AsClass();
}

art::ObjPtr<art::mirror::ClassLoader> Redefiner::ClassRedefinition::GetClassLoader() {
  return GetMirrorClass()->GetClassLoader();
}

art::mirror::DexCache* Redefiner::ClassRedefinition::CreateNewDexCache(
    art::Handle<art::mirror::ClassLoader> loader) {
  art::StackHandleScope<2> hs(driver_->self_);
  art::ClassLinker* cl = driver_->runtime_->GetClassLinker();
  art::Handle<art::mirror::DexCache> cache(hs.NewHandle(
      art::ObjPtr<art::mirror::DexCache>::DownCast(
          art::GetClassRoot<art::mirror::DexCache>(cl)->AllocObject(driver_->self_))));
  if (cache.IsNull()) {
    driver_->self_->AssertPendingOOMException();
    return nullptr;
  }
  art::Handle<art::mirror::String> location(hs.NewHandle(
      cl->GetInternTable()->InternStrong(dex_file_->GetLocation().c_str())));
  if (location.IsNull()) {
    driver_->self_->AssertPendingOOMException();
    return nullptr;
  }
  art::WriterMutexLock mu(driver_->self_, *art::Locks::dex_lock_);
  art::mirror::DexCache::InitializeDexCache(driver_->self_,
                                            cache.Get(),
                                            location.Get(),
                                            dex_file_.get(),
                                            loader.IsNull() ? driver_->runtime_->GetLinearAlloc()
                                                            : loader->GetAllocator(),
                                            art::kRuntimePointerSize);
  return cache.Get();
}

void Redefiner::RecordFailure(jvmtiError result,
                              const std::string& class_sig,
                              const std::string& error_msg) {
  *error_msg_ = StringPrintf("Unable to perform redefinition of '%s': %s",
                             class_sig.c_str(),
                             error_msg.c_str());
  result_ = result;
}

art::mirror::Object* Redefiner::ClassRedefinition::AllocateOrGetOriginalDexFile() {
  // If we have been specifically given a new set of bytes use that
  if (original_dex_file_.size() != 0) {
    return art::mirror::ByteArray::AllocateAndFill(
        driver_->self_,
        reinterpret_cast<const signed char*>(original_dex_file_.data()),
        original_dex_file_.size()).Ptr();
  }

  // See if we already have one set.
  art::ObjPtr<art::mirror::ClassExt> ext(GetMirrorClass()->GetExtData());
  if (!ext.IsNull()) {
    art::ObjPtr<art::mirror::Object> old_original_dex_file(ext->GetOriginalDexFile());
    if (!old_original_dex_file.IsNull()) {
      // We do. Use it.
      return old_original_dex_file.Ptr();
    }
  }

  // return the current dex_cache which has the dex file in it.
  art::ObjPtr<art::mirror::DexCache> current_dex_cache(GetMirrorClass()->GetDexCache());
  // TODO Handle this or make it so it cannot happen.
  if (current_dex_cache->GetDexFile()->NumClassDefs() != 1) {
    LOG(WARNING) << "Current dex file has more than one class in it. Calling RetransformClasses "
                 << "on this class might fail if no transformations are applied to it!";
  }
  return current_dex_cache.Ptr();
}

struct CallbackCtx {
  ObsoleteMap* obsolete_map;
  art::LinearAlloc* allocator;
  std::unordered_set<art::ArtMethod*> obsolete_methods;

  explicit CallbackCtx(ObsoleteMap* map, art::LinearAlloc* alloc)
      : obsolete_map(map), allocator(alloc) {}
};

void DoAllocateObsoleteMethodsCallback(art::Thread* t, void* vdata) NO_THREAD_SAFETY_ANALYSIS {
  CallbackCtx* data = reinterpret_cast<CallbackCtx*>(vdata);
  ObsoleteMethodStackVisitor::UpdateObsoleteFrames(t,
                                                   data->allocator,
                                                   data->obsolete_methods,
                                                   data->obsolete_map);
}

// This creates any ArtMethod* structures needed for obsolete methods and ensures that the stack is
// updated so they will be run.
// TODO Rewrite so we can do this only once regardless of how many redefinitions there are.
void Redefiner::ClassRedefinition::FindAndAllocateObsoleteMethods(
    art::ObjPtr<art::mirror::Class> art_klass) {
  DCHECK(!IsStructuralRedefinition());
  art::ScopedAssertNoThreadSuspension ns("No thread suspension during thread stack walking");
  art::ObjPtr<art::mirror::ClassExt> ext = art_klass->GetExtData();
  CHECK(ext->GetObsoleteMethods() != nullptr);
  art::ClassLinker* linker = driver_->runtime_->GetClassLinker();
  // This holds pointers to the obsolete methods map fields which are updated as needed.
  ObsoleteMap map(ext->GetObsoleteMethods(), ext->GetObsoleteDexCaches(), art_klass->GetDexCache());
  CallbackCtx ctx(&map, linker->GetAllocatorForClassLoader(art_klass->GetClassLoader()));
  // Add all the declared methods to the map
  for (auto& m : art_klass->GetDeclaredMethods(art::kRuntimePointerSize)) {
    if (m.IsIntrinsic()) {
      LOG(WARNING) << "Redefining intrinsic method " << m.PrettyMethod() << ". This may cause the "
                   << "unexpected use of the original definition of " << m.PrettyMethod() << "in "
                   << "methods that have already been compiled.";
    }
    // It is possible to simply filter out some methods where they cannot really become obsolete,
    // such as native methods and keep their original (possibly optimized) implementations. We don't
    // do this, however, since we would need to mark these functions (still in the classes
    // declared_methods array) as obsolete so we will find the correct dex file to get meta-data
    // from (for example about stack-frame size). Furthermore we would be unable to get some useful
    // error checking from the interpreter which ensure we don't try to start executing obsolete
    // methods.
    ctx.obsolete_methods.insert(&m);
  }
  {
    art::MutexLock mu(driver_->self_, *art::Locks::thread_list_lock_);
    art::ThreadList* list = art::Runtime::Current()->GetThreadList();
    list->ForEach(DoAllocateObsoleteMethodsCallback, static_cast<void*>(&ctx));
    // After we've done walking all threads' stacks and updating method pointers on them,
    // update JIT data structures (used by the stack walk above) to point to the new methods.
    art::jit::Jit* jit = art::Runtime::Current()->GetJit();
    if (jit != nullptr) {
      for (const ObsoleteMap::ObsoleteMethodPair& it : *ctx.obsolete_map) {
        // Notify the JIT we are making this obsolete method. It will update the jit's internal
        // structures to keep track of the new obsolete method.
        jit->GetCodeCache()->MoveObsoleteMethod(it.old_method, it.obsolete_method);
      }
    }
  }
}

namespace {
template <typename T> struct SignatureType {};
template <> struct SignatureType<art::ArtField> { using type = std::string_view; };
template <> struct SignatureType<art::ArtMethod> { using type = art::Signature; };

template <typename T> struct NameAndSignature {
 public:
  using SigType = typename SignatureType<T>::type;

  NameAndSignature(const art::DexFile* dex_file, uint32_t id);

  NameAndSignature(const std::string_view& name, const SigType& sig) : name_(name), sig_(sig) {}

  bool operator==(const NameAndSignature<T>& o) {
    return name_ == o.name_ && sig_ == o.sig_;
  }

  std::ostream& dump(std::ostream& os) const {
    return os << "'" << name_ << "' (sig: " << sig_ << ")";
  }

  std::string ToString() const {
    std::ostringstream os;
    os << *this;
    return os.str();
  }

  std::string_view name_;
  SigType sig_;
};

template <typename T>
std::ostream& operator<<(std::ostream& os, const NameAndSignature<T>& nas) {
  return nas.dump(os);
}

using FieldNameAndSignature = NameAndSignature<art::ArtField>;
template <>
FieldNameAndSignature::NameAndSignature(const art::DexFile* dex_file, uint32_t id)
    : FieldNameAndSignature(dex_file->GetFieldName(dex_file->GetFieldId(id)),
                            dex_file->GetFieldTypeDescriptor(dex_file->GetFieldId(id))) {}

using MethodNameAndSignature = NameAndSignature<art::ArtMethod>;
template <>
MethodNameAndSignature::NameAndSignature(const art::DexFile* dex_file, uint32_t id)
    : MethodNameAndSignature(dex_file->GetMethodName(dex_file->GetMethodId(id)),
                             dex_file->GetMethodSignature(dex_file->GetMethodId(id))) {}

}  // namespace

void Redefiner::ClassRedefinition::RecordNewMethodAdded() {
  DCHECK(driver_->IsStructuralRedefinition());
  added_methods_ = true;
}
void Redefiner::ClassRedefinition::RecordNewFieldAdded() {
  DCHECK(driver_->IsStructuralRedefinition());
  added_fields_ = true;
}

bool Redefiner::ClassRedefinition::CheckMethods() {
  art::StackHandleScope<1> hs(driver_->self_);
  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(GetMirrorClass()));
  DCHECK_EQ(dex_file_->NumClassDefs(), 1u);

  // Make sure we have the same number of methods (or the same or greater if we're structural).
  art::ClassAccessor accessor(*dex_file_, dex_file_->GetClassDef(0));
  uint32_t num_new_method = accessor.NumMethods();
  uint32_t num_old_method = h_klass->GetDeclaredMethodsSlice(art::kRuntimePointerSize).size();
  const bool is_structural = driver_->IsStructuralRedefinition();
  if (!is_structural && num_new_method != num_old_method) {
    bool bigger = num_new_method > num_old_method;
    RecordFailure(bigger ? ERR(UNSUPPORTED_REDEFINITION_METHOD_ADDED)
                         : ERR(UNSUPPORTED_REDEFINITION_METHOD_DELETED),
                  StringPrintf("Total number of declared methods changed from %d to %d",
                               num_old_method,
                               num_new_method));
    return false;
  }

  // Skip all of the fields. We should have already checked this.
  // Check each of the methods. NB we don't need to specifically check for removals since the 2 dex
  // files have the same number of methods, which means there must be an equal amount of additions
  // and removals. We should have already checked the fields.
  const art::DexFile& old_dex_file = h_klass->GetDexFile();
  art::ClassAccessor old_accessor(old_dex_file, *h_klass->GetClassDef());
  // We need this to check for methods going missing in structural cases.
  std::vector<bool> seen_old_methods(
      (kCheckAllMethodsSeenOnce || is_structural) ? old_accessor.NumMethods() : 0, false);
  const auto old_methods = old_accessor.GetMethods();
  for (const art::ClassAccessor::Method& new_method : accessor.GetMethods()) {
    // Get the data on the method we are searching for
    MethodNameAndSignature new_method_id(dex_file_.get(), new_method.GetIndex());
    const auto old_iter =
        std::find_if(old_methods.cbegin(), old_methods.cend(), [&](const auto& current_old_method) {
          MethodNameAndSignature old_method_id(&old_dex_file, current_old_method.GetIndex());
          return old_method_id == new_method_id;
        });

    if (!new_method.IsStaticOrDirect()) {
      RecordHasVirtualMembers();
    }
    if (old_iter == old_methods.cend()) {
      if (is_structural) {
        RecordNewMethodAdded();
      } else {
        RecordFailure(
            ERR(UNSUPPORTED_REDEFINITION_METHOD_ADDED),
            StringPrintf("Unknown virtual method %s was added!", new_method_id.ToString().c_str()));
        return false;
      }
    } else if (new_method.GetAccessFlags() != old_iter->GetAccessFlags()) {
      RecordFailure(
          ERR(UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED),
          StringPrintf("method %s had different access flags", new_method_id.ToString().c_str()));
      return false;
    } else if (kCheckAllMethodsSeenOnce || is_structural) {
      // We only need this if we are structural.
      size_t off = std::distance(old_methods.cbegin(), old_iter);
      DCHECK(!seen_old_methods[off])
          << "field at " << off << "("
          << MethodNameAndSignature(&old_dex_file, old_iter->GetIndex()) << ") already seen?";
      seen_old_methods[off] = true;
    }
  }
  if ((kCheckAllMethodsSeenOnce || is_structural) &&
      !std::all_of(seen_old_methods.cbegin(), seen_old_methods.cend(), [](auto x) { return x; })) {
    DCHECK(is_structural) << "We should have hit an earlier failure before getting here!";
    auto first_fail =
        std::find_if(seen_old_methods.cbegin(), seen_old_methods.cend(), [](auto x) { return !x; });
    auto off = std::distance(seen_old_methods.cbegin(), first_fail);
    auto fail = old_methods.cbegin();
    std::advance(fail, off);
    RecordFailure(
        ERR(UNSUPPORTED_REDEFINITION_METHOD_DELETED),
        StringPrintf("Method %s missing!",
                     MethodNameAndSignature(&old_dex_file, fail->GetIndex()).ToString().c_str()));
    return false;
  }
  return true;
}

bool Redefiner::ClassRedefinition::CheckFields() {
  art::StackHandleScope<1> hs(driver_->self_);
  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(GetMirrorClass()));
  DCHECK_EQ(dex_file_->NumClassDefs(), 1u);
  art::ClassAccessor new_accessor(*dex_file_, dex_file_->GetClassDef(0));

  const art::DexFile& old_dex_file = h_klass->GetDexFile();
  art::ClassAccessor old_accessor(old_dex_file, *h_klass->GetClassDef());
  // Instance and static fields can be differentiated by their flags so no need to check them
  // separately.
  std::vector<bool> seen_old_fields(old_accessor.NumFields(), false);
  const auto old_fields = old_accessor.GetFields();
  for (const art::ClassAccessor::Field& new_field : new_accessor.GetFields()) {
    // Get the data on the method we are searching for
    FieldNameAndSignature new_field_id(dex_file_.get(), new_field.GetIndex());
    const auto old_iter =
        std::find_if(old_fields.cbegin(), old_fields.cend(), [&](const auto& old_iter) {
          FieldNameAndSignature old_field_id(&old_dex_file, old_iter.GetIndex());
          return old_field_id == new_field_id;
        });
    if (!new_field.IsStatic()) {
      RecordHasVirtualMembers();
    }
    if (old_iter == old_fields.cend()) {
      if (driver_->IsStructuralRedefinition()) {
        RecordNewFieldAdded();
      } else {
        RecordFailure(ERR(UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED),
                      StringPrintf("Unknown field %s added!", new_field_id.ToString().c_str()));
        return false;
      }
    } else if (new_field.GetAccessFlags() != old_iter->GetAccessFlags()) {
      RecordFailure(
          ERR(UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED),
          StringPrintf("Field %s had different access flags", new_field_id.ToString().c_str()));
      return false;
    } else {
      size_t off = std::distance(old_fields.cbegin(), old_iter);
      DCHECK(!seen_old_fields[off])
          << "field at " << off << "(" << FieldNameAndSignature(&old_dex_file, old_iter->GetIndex())
          << ") already seen?";
      seen_old_fields[off] = true;
    }
  }
  if (!std::all_of(seen_old_fields.cbegin(), seen_old_fields.cend(), [](auto x) { return x; })) {
    auto first_fail =
        std::find_if(seen_old_fields.cbegin(), seen_old_fields.cend(), [](auto x) { return !x; });
    auto off = std::distance(seen_old_fields.cbegin(), first_fail);
    auto fail = old_fields.cbegin();
    std::advance(fail, off);
    RecordFailure(
        ERR(UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED),
        StringPrintf("Field %s is missing!",
                     FieldNameAndSignature(&old_dex_file, fail->GetIndex()).ToString().c_str()));
    return false;
  }
  return true;
}

bool Redefiner::ClassRedefinition::CheckClass() {
  art::StackHandleScope<1> hs(driver_->self_);
  // Easy check that only 1 class def is present.
  if (dex_file_->NumClassDefs() != 1) {
    RecordFailure(ERR(ILLEGAL_ARGUMENT),
                  StringPrintf("Expected 1 class def in dex file but found %d",
                               dex_file_->NumClassDefs()));
    return false;
  }
  // Get the ClassDef from the new DexFile.
  // Since the dex file has only a single class def the index is always 0.
  const art::dex::ClassDef& def = dex_file_->GetClassDef(0);
  // Get the class as it is now.
  art::Handle<art::mirror::Class> current_class(hs.NewHandle(GetMirrorClass()));

  // Check the access flags didn't change.
  if (def.GetJavaAccessFlags() != (current_class->GetAccessFlags() & art::kAccValidClassFlags)) {
    RecordFailure(ERR(UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED),
                  "Cannot change modifiers of class by redefinition");
    return false;
  }

  // Check class name.
  // These should have been checked by the dexfile verifier on load.
  DCHECK_NE(def.class_idx_, art::dex::TypeIndex::Invalid()) << "Invalid type index";
  const char* descriptor = dex_file_->StringByTypeIdx(def.class_idx_);
  DCHECK(descriptor != nullptr) << "Invalid dex file structure!";
  if (!current_class->DescriptorEquals(descriptor)) {
    std::string storage;
    RecordFailure(ERR(NAMES_DONT_MATCH),
                  StringPrintf("expected file to contain class called '%s' but found '%s'!",
                               current_class->GetDescriptor(&storage),
                               descriptor));
    return false;
  }
  if (current_class->IsObjectClass()) {
    if (def.superclass_idx_ != art::dex::TypeIndex::Invalid()) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Superclass added!");
      return false;
    }
  } else {
    const char* super_descriptor = dex_file_->StringByTypeIdx(def.superclass_idx_);
    DCHECK(descriptor != nullptr) << "Invalid dex file structure!";
    if (!current_class->GetSuperClass()->DescriptorEquals(super_descriptor)) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Superclass changed");
      return false;
    }
  }
  const art::dex::TypeList* interfaces = dex_file_->GetInterfacesList(def);
  if (interfaces == nullptr) {
    if (current_class->NumDirectInterfaces() != 0) {
      // TODO Support this for kStructural.
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Interfaces added");
      return false;
    }
  } else {
    DCHECK(!current_class->IsProxyClass());
    const art::dex::TypeList* current_interfaces = current_class->GetInterfaceTypeList();
    if (current_interfaces == nullptr || current_interfaces->Size() != interfaces->Size()) {
      // TODO Support this for kStructural.
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Interfaces added or removed");
      return false;
    }
    // The order of interfaces is (barely) meaningful so we error if it changes.
    const art::DexFile& orig_dex_file = current_class->GetDexFile();
    for (uint32_t i = 0; i < interfaces->Size(); i++) {
      if (strcmp(
            dex_file_->StringByTypeIdx(interfaces->GetTypeItem(i).type_idx_),
            orig_dex_file.StringByTypeIdx(current_interfaces->GetTypeItem(i).type_idx_)) != 0) {
        RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED),
                      "Interfaces changed or re-ordered");
        return false;
      }
    }
  }
  return true;
}

bool Redefiner::ClassRedefinition::CheckRedefinable() {
  std::string err;
  art::StackHandleScope<1> hs(driver_->self_);

  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(GetMirrorClass()));
  jvmtiError res;
  if (driver_->type_ == RedefinitionType::kStructural && this->IsStructuralRedefinition()) {
    res = Redefiner::GetClassRedefinitionError<RedefinitionType::kStructural>(h_klass, &err);
  } else {
    res = Redefiner::GetClassRedefinitionError<RedefinitionType::kNormal>(h_klass, &err);
  }
  if (res != OK) {
    RecordFailure(res, err);
    return false;
  } else {
    return true;
  }
}

bool Redefiner::ClassRedefinition::CheckRedefinitionIsValid() {
  return CheckClass() && CheckFields() && CheckMethods() && CheckRedefinable();
}

class RedefinitionDataIter;

// A wrapper that lets us hold onto the arbitrary sized data needed for redefinitions in a
// reasonably sane way. This adds no fields to the normal ObjectArray. By doing this we can avoid
// having to deal with the fact that we need to hold an arbitrary number of references live.
class RedefinitionDataHolder {
 public:
  enum DataSlot : int32_t {
    kSlotSourceClassLoader = 0,
    kSlotJavaDexFile = 1,
    kSlotNewDexFileCookie = 2,
    kSlotNewDexCache = 3,
    kSlotMirrorClass = 4,
    kSlotOrigDexFile = 5,
    kSlotOldObsoleteMethods = 6,
    kSlotOldDexCaches = 7,
    kSlotNewClassObject = 8,
    kSlotOldInstanceObjects = 9,
    kSlotNewInstanceObjects = 10,
    kSlotOldClasses = 11,
    kSlotNewClasses = 12,

    // Must be last one.
    kNumSlots = 13,
  };

  // This needs to have a HandleScope passed in that is capable of creating a new Handle without
  // overflowing. Only one handle will be created. This object has a lifetime identical to that of
  // the passed in handle-scope.
  RedefinitionDataHolder(art::StackHandleScope<1>* hs,
                         art::Runtime* runtime,
                         art::Thread* self,
                         std::vector<Redefiner::ClassRedefinition>* redefinitions)
      REQUIRES_SHARED(art::Locks::mutator_lock_) :
    arr_(hs->NewHandle(art::mirror::ObjectArray<art::mirror::Object>::Alloc(
        self,
        art::GetClassRoot<art::mirror::ObjectArray<art::mirror::Object>>(runtime->GetClassLinker()),
        redefinitions->size() * kNumSlots))),
    redefinitions_(redefinitions),
    initialized_(redefinitions_->size(), false),
    actually_structural_(redefinitions_->size(), false),
    initial_structural_(redefinitions_->size(), false) {}

  bool IsNull() const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return arr_.IsNull();
  }

  art::ObjPtr<art::mirror::ClassLoader> GetSourceClassLoader(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::ClassLoader>::DownCast(
        GetSlot(klass_index, kSlotSourceClassLoader));
  }
  art::ObjPtr<art::mirror::Object> GetJavaDexFile(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return GetSlot(klass_index, kSlotJavaDexFile);
  }
  art::ObjPtr<art::mirror::LongArray> GetNewDexFileCookie(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::LongArray>::DownCast(
        GetSlot(klass_index, kSlotNewDexFileCookie));
  }
  art::ObjPtr<art::mirror::DexCache> GetNewDexCache(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::DexCache>::DownCast(GetSlot(klass_index, kSlotNewDexCache));
  }
  art::ObjPtr<art::mirror::Class> GetMirrorClass(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::Class>::DownCast(GetSlot(klass_index, kSlotMirrorClass));
  }

  art::ObjPtr<art::mirror::Object> GetOriginalDexFile(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::Object>::DownCast(GetSlot(klass_index, kSlotOrigDexFile));
  }

  art::ObjPtr<art::mirror::PointerArray> GetOldObsoleteMethods(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::PointerArray>::DownCast(
        GetSlot(klass_index, kSlotOldObsoleteMethods));
  }

  art::ObjPtr<art::mirror::ObjectArray<art::mirror::DexCache>> GetOldDexCaches(
      jint klass_index) const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::ObjectArray<art::mirror::DexCache>>::DownCast(
        GetSlot(klass_index, kSlotOldDexCaches));
  }

  art::ObjPtr<art::mirror::Class> GetNewClassObject(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::Class>::DownCast(GetSlot(klass_index, kSlotNewClassObject));
  }

  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>> GetOldInstanceObjects(
      jint klass_index) const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>>::DownCast(
        GetSlot(klass_index, kSlotOldInstanceObjects));
  }

  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>> GetNewInstanceObjects(
      jint klass_index) const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>>::DownCast(
        GetSlot(klass_index, kSlotNewInstanceObjects));
  }
  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>> GetOldClasses(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>>::DownCast(
        GetSlot(klass_index, kSlotOldClasses));
  }
  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>> GetNewClasses(jint klass_index) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>>::DownCast(
        GetSlot(klass_index, kSlotNewClasses));
  }
  bool IsInitialized(jint klass_index) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return initialized_[klass_index];
  }
  bool IsActuallyStructural(jint klass_index) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return actually_structural_[klass_index];
  }

  bool IsInitialStructural(jint klass_index) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return initial_structural_[klass_index];
  }

  void SetSourceClassLoader(jint klass_index, art::ObjPtr<art::mirror::ClassLoader> loader)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotSourceClassLoader, loader);
  }
  void SetJavaDexFile(jint klass_index, art::ObjPtr<art::mirror::Object> dexfile)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotJavaDexFile, dexfile);
  }
  void SetNewDexFileCookie(jint klass_index, art::ObjPtr<art::mirror::LongArray> cookie)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotNewDexFileCookie, cookie);
  }
  void SetNewDexCache(jint klass_index, art::ObjPtr<art::mirror::DexCache> cache)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotNewDexCache, cache);
  }
  void SetMirrorClass(jint klass_index, art::ObjPtr<art::mirror::Class> klass)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotMirrorClass, klass);
  }
  void SetOriginalDexFile(jint klass_index, art::ObjPtr<art::mirror::Object> bytes)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotOrigDexFile, bytes);
  }
  void SetOldObsoleteMethods(jint klass_index, art::ObjPtr<art::mirror::PointerArray> methods)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotOldObsoleteMethods, methods);
  }
  void SetOldDexCaches(jint klass_index,
                       art::ObjPtr<art::mirror::ObjectArray<art::mirror::DexCache>> caches)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotOldDexCaches, caches);
  }

  void SetNewClassObject(jint klass_index, art::ObjPtr<art::mirror::Class> klass)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotNewClassObject, klass);
  }

  void SetOldInstanceObjects(jint klass_index,
                             art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>> objs)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotOldInstanceObjects, objs);
  }
  void SetNewInstanceObjects(jint klass_index,
                             art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>> objs)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotNewInstanceObjects, objs);
  }
  void SetOldClasses(jint klass_index,
                     art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>> klasses)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotOldClasses, klasses);
  }
  void SetNewClasses(jint klass_index,
                     art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>> klasses)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotNewClasses, klasses);
  }
  void SetInitialized(jint klass_index) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    initialized_[klass_index] = true;
  }
  void SetActuallyStructural(jint klass_index) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    actually_structural_[klass_index] = true;
  }
  void SetInitialStructural(jint klass_index) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    initial_structural_[klass_index] = true;
  }
  int32_t Length() const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return arr_->GetLength() / kNumSlots;
  }

  std::vector<Redefiner::ClassRedefinition>* GetRedefinitions()
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return redefinitions_;
  }

  bool operator==(const RedefinitionDataHolder& other) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return arr_.Get() == other.arr_.Get();
  }

  bool operator!=(const RedefinitionDataHolder& other) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return !(*this == other);
  }

  RedefinitionDataIter begin() REQUIRES_SHARED(art::Locks::mutator_lock_);
  RedefinitionDataIter end() REQUIRES_SHARED(art::Locks::mutator_lock_);

 private:
  mutable art::Handle<art::mirror::ObjectArray<art::mirror::Object>> arr_;
  std::vector<Redefiner::ClassRedefinition>* redefinitions_;
  // Used to mark a particular redefinition as fully initialized.
  std::vector<bool> initialized_;
  // Used to mark a redefinition as 'actually' structural. That is either the redefinition is
  // structural or a superclass is.
  std::vector<bool> actually_structural_;
  // Used to mark a redefinition as the initial structural redefinition. This redefinition will take
  // care of updating all of its subtypes.
  std::vector<bool> initial_structural_;

  art::ObjPtr<art::mirror::Object> GetSlot(jint klass_index, DataSlot slot) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    DCHECK_LT(klass_index, Length());
    return arr_->Get((kNumSlots * klass_index) + slot);
  }

  void SetSlot(jint klass_index,
               DataSlot slot,
               art::ObjPtr<art::mirror::Object> obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    DCHECK(!art::Runtime::Current()->IsActiveTransaction());
    DCHECK_LT(klass_index, Length());
    arr_->Set<false>((kNumSlots * klass_index) + slot, obj);
  }

  DISALLOW_COPY_AND_ASSIGN(RedefinitionDataHolder);
};

class RedefinitionDataIter {
 public:
  RedefinitionDataIter(int32_t idx, RedefinitionDataHolder& holder) : idx_(idx), holder_(holder) {}

  RedefinitionDataIter(const RedefinitionDataIter&) = default;
  RedefinitionDataIter(RedefinitionDataIter&&) = default;
  RedefinitionDataIter& operator=(const RedefinitionDataIter&) = default;
  RedefinitionDataIter& operator=(RedefinitionDataIter&&) = default;

  bool operator==(const RedefinitionDataIter& other) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return idx_ == other.idx_ && holder_ == other.holder_;
  }

  bool operator!=(const RedefinitionDataIter& other) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return !(*this == other);
  }

  RedefinitionDataIter operator++() {  // Value after modification.
    idx_++;
    return *this;
  }

  RedefinitionDataIter operator++(int) {
    RedefinitionDataIter temp = *this;
    idx_++;
    return temp;
  }

  RedefinitionDataIter operator+(ssize_t delta) const {
    RedefinitionDataIter temp = *this;
    temp += delta;
    return temp;
  }

  RedefinitionDataIter& operator+=(ssize_t delta) {
    idx_ += delta;
    return *this;
  }

  // Compat for STL iterators.
  RedefinitionDataIter& operator*() {
    return *this;
  }

  Redefiner::ClassRedefinition& GetRedefinition() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return (*holder_.GetRedefinitions())[idx_];
  }

  RedefinitionDataHolder& GetHolder() {
    return holder_;
  }

  art::ObjPtr<art::mirror::ClassLoader> GetSourceClassLoader() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetSourceClassLoader(idx_);
  }
  art::ObjPtr<art::mirror::Object> GetJavaDexFile() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetJavaDexFile(idx_);
  }
  art::ObjPtr<art::mirror::LongArray> GetNewDexFileCookie() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetNewDexFileCookie(idx_);
  }
  art::ObjPtr<art::mirror::DexCache> GetNewDexCache() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetNewDexCache(idx_);
  }
  art::ObjPtr<art::mirror::Class> GetMirrorClass() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetMirrorClass(idx_);
  }
  art::ObjPtr<art::mirror::Object> GetOriginalDexFile() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetOriginalDexFile(idx_);
  }
  art::ObjPtr<art::mirror::PointerArray> GetOldObsoleteMethods() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetOldObsoleteMethods(idx_);
  }
  art::ObjPtr<art::mirror::ObjectArray<art::mirror::DexCache>> GetOldDexCaches() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetOldDexCaches(idx_);
  }

  art::ObjPtr<art::mirror::Class> GetNewClassObject() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetNewClassObject(idx_);
  }

  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>> GetOldInstanceObjects() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetOldInstanceObjects(idx_);
  }
  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>> GetNewInstanceObjects() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetNewInstanceObjects(idx_);
  }
  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>> GetOldClasses() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetOldClasses(idx_);
  }
  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>> GetNewClasses() const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.GetNewClasses(idx_);
  }
  bool IsInitialized() const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.IsInitialized(idx_);
  }
  bool IsActuallyStructural() const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.IsActuallyStructural(idx_);
  }
  bool IsInitialStructural() const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return holder_.IsInitialStructural(idx_);
  }
  int32_t GetIndex() const {
    return idx_;
  }

  void SetSourceClassLoader(art::mirror::ClassLoader* loader)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetSourceClassLoader(idx_, loader);
  }
  void SetJavaDexFile(art::ObjPtr<art::mirror::Object> dexfile)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetJavaDexFile(idx_, dexfile);
  }
  void SetNewDexFileCookie(art::ObjPtr<art::mirror::LongArray> cookie)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetNewDexFileCookie(idx_, cookie);
  }
  void SetNewDexCache(art::ObjPtr<art::mirror::DexCache> cache)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetNewDexCache(idx_, cache);
  }
  void SetMirrorClass(art::ObjPtr<art::mirror::Class> klass)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetMirrorClass(idx_, klass);
  }
  void SetOriginalDexFile(art::ObjPtr<art::mirror::Object> bytes)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetOriginalDexFile(idx_, bytes);
  }
  void SetOldObsoleteMethods(art::ObjPtr<art::mirror::PointerArray> methods)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetOldObsoleteMethods(idx_, methods);
  }
  void SetOldDexCaches(art::ObjPtr<art::mirror::ObjectArray<art::mirror::DexCache>> caches)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetOldDexCaches(idx_, caches);
  }
  void SetNewClassObject(art::ObjPtr<art::mirror::Class> klass)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetNewClassObject(idx_, klass);
  }
  void SetOldInstanceObjects(art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>> objs)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetOldInstanceObjects(idx_, objs);
  }
  void SetNewInstanceObjects(art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>> objs)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetNewInstanceObjects(idx_, objs);
  }
  void SetOldClasses(art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>> klasses)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetOldClasses(idx_, klasses);
  }
  void SetNewClasses(art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>> klasses)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetNewClasses(idx_, klasses);
  }
  void SetInitialized() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetInitialized(idx_);
  }
  void SetActuallyStructural() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetActuallyStructural(idx_);
  }
  void SetInitialStructural() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    holder_.SetInitialStructural(idx_);
  }

 private:
  int32_t idx_;
  RedefinitionDataHolder& holder_;
};

RedefinitionDataIter RedefinitionDataHolder::begin() {
  return RedefinitionDataIter(0, *this);
}

RedefinitionDataIter RedefinitionDataHolder::end() {
  return RedefinitionDataIter(Length(), *this);
}

bool Redefiner::ClassRedefinition::CheckVerification(const RedefinitionDataIter& iter) {
  DCHECK_EQ(dex_file_->NumClassDefs(), 1u);
  art::StackHandleScope<2> hs(driver_->self_);
  std::string error;
  // TODO Make verification log level lower
  art::verifier::FailureKind failure =
      art::verifier::ClassVerifier::VerifyClass(driver_->self_,
                                                dex_file_.get(),
                                                hs.NewHandle(iter.GetNewDexCache()),
                                                hs.NewHandle(GetClassLoader()),
                                                /*class_def=*/ dex_file_->GetClassDef(0),
                                                /*callbacks=*/ nullptr,
                                                /*allow_soft_failures=*/ true,
                                                /*log_level=*/
                                                art::verifier::HardFailLogMode::kLogWarning,
                                                art::Runtime::Current()->GetTargetSdkVersion(),
                                                &error);
  switch (failure) {
    case art::verifier::FailureKind::kNoFailure:
      // TODO It is possible that by doing redefinition previous NO_COMPILE verification failures
      // were fixed. It would be nice to reflect this in the new implementations.
      return true;
    case art::verifier::FailureKind::kSoftFailure:
    case art::verifier::FailureKind::kAccessChecksFailure:
      // Soft failures might require interpreter on some methods. It won't prevent redefinition but
      // it does mean we need to run the verifier again and potentially update method flags after
      // performing the swap.
      needs_reverify_ = true;
      return true;
    case art::verifier::FailureKind::kHardFailure: {
      RecordFailure(ERR(FAILS_VERIFICATION), "Failed to verify class. Error was: " + error);
      return false;
    }
  }
}

// Looks through the previously allocated cookies to see if we need to update them with another new
// dexfile. This is so that even if multiple classes with the same classloader are redefined at
// once they are all added to the classloader.
bool Redefiner::ClassRedefinition::AllocateAndRememberNewDexFileCookie(
    art::Handle<art::mirror::ClassLoader> source_class_loader,
    art::Handle<art::mirror::Object> dex_file_obj,
    /*out*/RedefinitionDataIter* cur_data) {
  art::StackHandleScope<2> hs(driver_->self_);
  art::MutableHandle<art::mirror::LongArray> old_cookie(
      hs.NewHandle<art::mirror::LongArray>(nullptr));
  bool has_older_cookie = false;
  // See if we already have a cookie that a previous redefinition got from the same classloader.
  for (auto old_data = cur_data->GetHolder().begin(); old_data != *cur_data; ++old_data) {
    if (old_data.GetSourceClassLoader() == source_class_loader.Get()) {
      // Since every instance of this classloader should have the same cookie associated with it we
      // can stop looking here.
      has_older_cookie = true;
      old_cookie.Assign(old_data.GetNewDexFileCookie());
      break;
    }
  }
  if (old_cookie.IsNull()) {
    // No older cookie. Get it directly from the dex_file_obj
    // We should not have seen this classloader elsewhere.
    CHECK(!has_older_cookie);
    old_cookie.Assign(ClassLoaderHelper::GetDexFileCookie(dex_file_obj));
  }
  // Use the old cookie to generate the new one with the new DexFile* added in.
  art::Handle<art::mirror::LongArray>
      new_cookie(hs.NewHandle(ClassLoaderHelper::AllocateNewDexFileCookie(driver_->self_,
                                                                          old_cookie,
                                                                          dex_file_.get())));
  // Make sure the allocation worked.
  if (new_cookie.IsNull()) {
    return false;
  }

  // Save the cookie.
  cur_data->SetNewDexFileCookie(new_cookie.Get());
  // If there are other copies of this same classloader we need to make sure that we all have the
  // same cookie.
  if (has_older_cookie) {
    for (auto old_data = cur_data->GetHolder().begin(); old_data != *cur_data; ++old_data) {
      // We will let the GC take care of the cookie we allocated for this one.
      if (old_data.GetSourceClassLoader() == source_class_loader.Get()) {
        old_data.SetNewDexFileCookie(new_cookie.Get());
      }
    }
  }

  return true;
}

bool CompareClasses(art::ObjPtr<art::mirror::Class> l, art::ObjPtr<art::mirror::Class> r)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  auto parents = [](art::ObjPtr<art::mirror::Class> c) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    uint32_t res = 0;
    while (!c->IsObjectClass()) {
      res++;
      c = c->GetSuperClass();
    }
    return res;
  };
  return parents(l.Ptr()) < parents(r.Ptr());
}

bool Redefiner::ClassRedefinition::CollectAndCreateNewInstances(
    /*out*/ RedefinitionDataIter* cur_data) {
  if (!cur_data->IsInitialStructural()) {
    // An earlier structural redefinition already remade all the instances.
    return true;
  }
  art::gc::Heap* heap = driver_->runtime_->GetHeap();
  art::VariableSizedHandleScope hs(driver_->self_);
  art::Handle<art::mirror::Class> old_klass(hs.NewHandle(cur_data->GetMirrorClass()));
  std::vector<art::Handle<art::mirror::Object>> old_instances;
  auto is_instance = [&](art::mirror::Object* obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return obj->InstanceOf(old_klass.Get());
  };
  heap->VisitObjects([&](art::mirror::Object* obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    if (is_instance(obj)) {
      old_instances.push_back(hs.NewHandle(obj));
    }
  });
  VLOG(plugin) << "Collected " << old_instances.size() << " instances to recreate!";
  art::Handle<art::mirror::ObjectArray<art::mirror::Class>> old_classes_arr(
      hs.NewHandle(cur_data->GetOldClasses()));
  art::Handle<art::mirror::ObjectArray<art::mirror::Class>> new_classes_arr(
      hs.NewHandle(cur_data->GetNewClasses()));
  DCHECK_EQ(old_classes_arr->GetLength(), new_classes_arr->GetLength());
  DCHECK_GT(old_classes_arr->GetLength(), 0);
  art::Handle<art::mirror::Class> obj_array_class(
      hs.NewHandle(art::GetClassRoot<art::mirror::ObjectArray<art::mirror::Object>>(
          driver_->runtime_->GetClassLinker())));
  art::Handle<art::mirror::ObjectArray<art::mirror::Object>> old_instances_arr(
      hs.NewHandle(art::mirror::ObjectArray<art::mirror::Object>::Alloc(
          driver_->self_, obj_array_class.Get(), old_instances.size())));
  if (old_instances_arr.IsNull()) {
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Could not allocate old_instance arrays!");
    return false;
  }
  for (uint32_t i = 0; i < old_instances.size(); ++i) {
    old_instances_arr->Set(i, old_instances[i].Get());
  }
  cur_data->SetOldInstanceObjects(old_instances_arr.Get());

  art::Handle<art::mirror::ObjectArray<art::mirror::Object>> new_instances_arr(
      hs.NewHandle(art::mirror::ObjectArray<art::mirror::Object>::Alloc(
          driver_->self_, obj_array_class.Get(), old_instances.size())));
  if (new_instances_arr.IsNull()) {
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Could not allocate new_instance arrays!");
    return false;
  }
  for (auto pair : art::ZipCount(art::IterationRange(old_instances.begin(), old_instances.end()))) {
    art::Handle<art::mirror::Object> hinstance(pair.first);
    int32_t i = pair.second;
    auto iterator = art::ZipLeft(old_classes_arr.Iterate<art::mirror::Class>(),
                                 new_classes_arr.Iterate<art::mirror::Class>());
    auto it = std::find_if(iterator.begin(),
                           iterator.end(),
                           [&](auto class_pair) REQUIRES_SHARED(art::Locks::mutator_lock_) {
                             return class_pair.first == hinstance->GetClass();
                           });
    DCHECK(it != iterator.end()) << "Unable to find class pair for "
                                 << hinstance->GetClass()->PrettyClass() << " (instance " << i
                                 << ")";
    auto [_, new_type] = *it;
    // Make sure when allocating the new instance we don't add it's finalizer since we will directly
    // replace the old object in the finalizer reference. If we added it here to we would call
    // finalize twice.
    // NB If a type is changed from being non-finalizable to finalizable the finalizers on any
    //    objects created before the redefine will never be called. This is (sort of) allowable by
    //    the spec and greatly simplifies implementation.
    // TODO Make it so we will always call all finalizers, even if the object when it was created
    // wasn't finalizable. To do this we need to be careful of handling failure correctly and making
    // sure that objects aren't finalized multiple times and that instances of failed redefinitions
    // aren't finalized.
    art::ObjPtr<art::mirror::Object> new_instance(
        new_type->Alloc</*kIsInstrumented=*/true,
                        art::mirror::Class::AddFinalizer::kNoAddFinalizer,
                        /*kCheckAddFinalizer=*/false>(
            driver_->self_, driver_->runtime_->GetHeap()->GetCurrentAllocator()));
    if (new_instance.IsNull()) {
      driver_->self_->AssertPendingOOMException();
      driver_->self_->ClearException();
      std::string msg(
          StringPrintf("Could not allocate instance %d of %zu", i, old_instances.size()));
      RecordFailure(ERR(OUT_OF_MEMORY), msg);
      return false;
    }
    new_instances_arr->Set(i, new_instance);
  }
  cur_data->SetNewInstanceObjects(new_instances_arr.Get());
  return true;
}

bool Redefiner::ClassRedefinition::FinishRemainingCommonAllocations(
    /*out*/RedefinitionDataIter* cur_data) {
  art::ScopedObjectAccessUnchecked soa(driver_->self_);
  art::StackHandleScope<2> hs(driver_->self_);
  cur_data->SetMirrorClass(GetMirrorClass());
  // This shouldn't allocate
  art::Handle<art::mirror::ClassLoader> loader(hs.NewHandle(GetClassLoader()));
  // The bootclasspath is handled specially so it doesn't have a j.l.DexFile.
  if (!art::ClassLinker::IsBootClassLoader(soa, loader.Get())) {
    cur_data->SetSourceClassLoader(loader.Get());
    art::Handle<art::mirror::Object> dex_file_obj(hs.NewHandle(
        ClassLoaderHelper::FindSourceDexFileObject(driver_->self_, loader)));
    cur_data->SetJavaDexFile(dex_file_obj.Get());
    if (dex_file_obj == nullptr) {
      RecordFailure(ERR(INTERNAL), "Unable to find dex file!");
      return false;
    }
    // Allocate the new dex file cookie.
    if (!AllocateAndRememberNewDexFileCookie(loader, dex_file_obj, cur_data)) {
      driver_->self_->AssertPendingOOMException();
      driver_->self_->ClearException();
      RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate dex file array for class loader");
      return false;
    }
  }
  cur_data->SetNewDexCache(CreateNewDexCache(loader));
  if (cur_data->GetNewDexCache() == nullptr) {
    driver_->self_->AssertPendingException();
    driver_->self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate DexCache");
    return false;
  }

  // We won't always need to set this field.
  cur_data->SetOriginalDexFile(AllocateOrGetOriginalDexFile());
  if (cur_data->GetOriginalDexFile() == nullptr) {
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate array for original dex file");
    return false;
  }
  return true;
}

bool Redefiner::ClassRedefinition::FinishNewClassAllocations(RedefinitionDataHolder &holder,
                                                             RedefinitionDataIter *cur_data) {
  if (cur_data->IsInitialized() || !cur_data->IsActuallyStructural()) {
    cur_data->SetInitialized();
    return true;
  }

  art::VariableSizedHandleScope hs(driver_->self_);
  // If we weren't the lowest structural redef the superclass would have already initialized us.
  CHECK(IsStructuralRedefinition());
  CHECK(cur_data->IsInitialStructural()) << "Should have already been initialized by supertype";
  auto setup_single_redefinition =
      [this](RedefinitionDataIter* data, art::Handle<art::mirror::Class> super_class)
          REQUIRES_SHARED(art::Locks::mutator_lock_) -> art::ObjPtr<art::mirror::Class> {
    art::StackHandleScope<3> chs(driver_->self_);
    art::Handle<art::mirror::Class> nc(
        chs.NewHandle(AllocateNewClassObject(chs.NewHandle(data->GetMirrorClass()),
                                             super_class,
                                             chs.NewHandle(data->GetNewDexCache()),
                                             /*dex_class_def_index*/ 0)));
    if (nc.IsNull()) {
      return nullptr;
    }

    data->SetNewClassObject(nc.Get());
    // We really want to be able to resolve to the new class-object using this dex-cache for
    // verification work. Since we haven't put it in the class-table yet we wll just manually add it
    // to the dex-cache.
    // TODO: We should maybe do this in a better spot.
    data->GetNewDexCache()->SetResolvedType(nc->GetDexTypeIndex(), nc.Get());
    data->SetInitialized();
    return nc.Get();
  };

  std::vector<art::Handle<art::mirror::Class>> old_types;
  {
    art::gc::Heap* heap = driver_->runtime_->GetHeap();
    art::Handle<art::mirror::Class>
        old_klass(hs.NewHandle(cur_data->GetMirrorClass()));
    if (setup_single_redefinition(cur_data, hs.NewHandle(old_klass->GetSuperClass())).IsNull()) {
      return false;
    }
    auto is_subtype = [&](art::mirror::Object* obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
      // We've already waited for class defines to be finished and paused them. All classes should be
      // either resolved or error. We don't need to do anything with error classes, since they cannot
      // be accessed in any observable way.
      return obj->IsClass() && obj->AsClass()->IsResolved() &&
            old_klass->IsAssignableFrom(obj->AsClass());
    };
    heap->VisitObjects([&](art::mirror::Object* obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
      if (is_subtype(obj)) {
        old_types.push_back(hs.NewHandle(obj->AsClass()));
      }
    });
    DCHECK_GT(old_types.size(), 0u) << "Expected to find at least old_klass!";
    VLOG(plugin) << "Found " << old_types.size() << " types that are/are subtypes of "
                << old_klass->PrettyClass();
  }

  art::Handle<art::mirror::Class> cls_array_class(
      hs.NewHandle(art::GetClassRoot<art::mirror::ObjectArray<art::mirror::Class>>(
          driver_->runtime_->GetClassLinker())));
  art::Handle<art::mirror::ObjectArray<art::mirror::Class>> old_classes_arr(
      hs.NewHandle(art::mirror::ObjectArray<art::mirror::Class>::Alloc(
          driver_->self_, cls_array_class.Get(), old_types.size())));
  if (old_classes_arr.IsNull()) {
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Could not allocate old_classes arrays!");
    return false;
  }
  // Sort the old_types topologically.
  {
    art::ScopedAssertNoThreadSuspension sants("Sort classes");
    // Sort them by the distance to the base-class. This ensures that any class occurs before any of
    // its subtypes.
    std::sort(old_types.begin(),
              old_types.end(),
              [](auto& l, auto& r) REQUIRES_SHARED(art::Locks::mutator_lock_) {
                return CompareClasses(l.Get(), r.Get());
              });
  }
  for (uint32_t i = 0; i < old_types.size(); ++i) {
    DCHECK(!old_types[i].IsNull()) << i;
    old_classes_arr->Set(i, old_types[i].Get());
  }
  cur_data->SetOldClasses(old_classes_arr.Get());
  DCHECK_GT(old_classes_arr->GetLength(), 0);

  art::Handle<art::mirror::ObjectArray<art::mirror::Class>> new_classes_arr(
      hs.NewHandle(art::mirror::ObjectArray<art::mirror::Class>::Alloc(
          driver_->self_, cls_array_class.Get(), old_types.size())));
  if (new_classes_arr.IsNull()) {
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Could not allocate new_classes arrays!");
    return false;
  }

  art::MutableHandle<art::mirror::DexCache> dch(hs.NewHandle<art::mirror::DexCache>(nullptr));
  art::MutableHandle<art::mirror::Class> superclass(hs.NewHandle<art::mirror::Class>(nullptr));
  for (size_t i = 0; i < old_types.size(); i++) {
    art::Handle<art::mirror::Class>& old_type = old_types[i];
    if (old_type.Get() == cur_data->GetMirrorClass()) {
      CHECK_EQ(i, 0u) << "original class not at index 0. Bad sort!";
      new_classes_arr->Set(i, cur_data->GetNewClassObject());
      continue;
    } else {
      auto old_super = std::find_if(old_types.begin(),
                                    old_types.begin() + i,
                                    [&](art::Handle<art::mirror::Class>& v)
                                        REQUIRES_SHARED(art::Locks::mutator_lock_) {
                                          return v.Get() == old_type->GetSuperClass();
                                        });
      // Only the GetMirrorClass should not be in this list.
      CHECK(old_super != old_types.begin() + i)
          << "from first " << i << " could not find super of " << old_type->PrettyClass()
          << " expected to find " << old_type->GetSuperClass()->PrettyClass();
      superclass.Assign(new_classes_arr->Get(std::distance(old_types.begin(), old_super)));
      auto new_redef = std::find_if(
          *cur_data + 1, holder.end(), [&](auto it) REQUIRES_SHARED(art::Locks::mutator_lock_) {
            return it.GetMirrorClass() == old_type.Get();
          });
      art::ObjPtr<art::mirror::Class> new_type;
      if (new_redef == holder.end()) {
        // We aren't also redefining this subclass. Just allocate a new class and continue.
        dch.Assign(old_type->GetDexCache());
        new_type =
            AllocateNewClassObject(old_type, superclass, dch, old_type->GetDexClassDefIndex());
      } else {
        // This subclass is also being redefined. We need to use its new dex-file to load the new
        // class.
        CHECK(new_redef.IsActuallyStructural());
        CHECK(!new_redef.IsInitialStructural());
        new_type = setup_single_redefinition(&new_redef, superclass);
      }
      if (new_type == nullptr) {
        VLOG(plugin) << "Failed to load new version of class " << old_type->PrettyClass()
                     << " for structural redefinition!";
        return false;
      }
      new_classes_arr->Set(i, new_type);
    }
  }
  cur_data->SetNewClasses(new_classes_arr.Get());
  return true;
}

uint32_t Redefiner::ClassRedefinition::GetNewClassSize(art::ClassAccessor& accessor) {
  uint32_t num_8bit_static_fields = 0;
  uint32_t num_16bit_static_fields = 0;
  uint32_t num_32bit_static_fields = 0;
  uint32_t num_64bit_static_fields = 0;
  uint32_t num_ref_static_fields = 0;
  for (const art::ClassAccessor::Field& f : accessor.GetStaticFields()) {
    std::string_view desc(accessor.GetDexFile().GetFieldTypeDescriptor(
        accessor.GetDexFile().GetFieldId(f.GetIndex())));
    if (desc[0] == 'L' || desc[0] == '[') {
      num_ref_static_fields++;
    } else if (desc == "Z" || desc == "B") {
      num_8bit_static_fields++;
    } else if (desc == "C" || desc == "S") {
      num_16bit_static_fields++;
    } else if (desc == "I" || desc == "F") {
      num_32bit_static_fields++;
    } else if (desc == "J" || desc == "D") {
      num_64bit_static_fields++;
    } else {
      LOG(FATAL) << "Unknown type descriptor! " << desc;
    }
  }

  return art::mirror::Class::ComputeClassSize(/*has_embedded_vtable=*/ false,
                                              /*num_vtable_entries=*/ 0,
                                              num_8bit_static_fields,
                                              num_16bit_static_fields,
                                              num_32bit_static_fields,
                                              num_64bit_static_fields,
                                              num_ref_static_fields,
                                              art::kRuntimePointerSize);
}

art::ObjPtr<art::mirror::Class>
Redefiner::ClassRedefinition::AllocateNewClassObject(art::Handle<art::mirror::DexCache> cache) {
  art::StackHandleScope<2> hs(driver_->self_);
  art::Handle<art::mirror::Class> old_class(hs.NewHandle(GetMirrorClass()));
  art::Handle<art::mirror::Class> super_class(hs.NewHandle(old_class->GetSuperClass()));
  return AllocateNewClassObject(old_class, super_class, cache, /*dex_class_def_index*/0);
}

art::ObjPtr<art::mirror::Class> Redefiner::ClassRedefinition::AllocateNewClassObject(
    art::Handle<art::mirror::Class> old_class,
    art::Handle<art::mirror::Class> super_class,
    art::Handle<art::mirror::DexCache> cache,
    uint16_t dex_class_def_index) {
  // This is a stripped down DefineClass. We don't want to use DefineClass directly because it needs
  // to perform a lot of extra steps to tell the ClassTable and the jit and everything about a new
  // class. For now we will need to rely on our tests catching any issues caused by changes in how
  // class_linker sets up classes.
  // TODO Unify/move this into ClassLinker maybe.
  art::StackHandleScope<3> hs(driver_->self_);
  art::ClassLinker* linker = driver_->runtime_->GetClassLinker();
  const art::DexFile* dex_file = cache->GetDexFile();
  art::ClassAccessor accessor(*dex_file, dex_class_def_index);
  art::Handle<art::mirror::Class> new_class(hs.NewHandle(linker->AllocClass(
      driver_->self_, GetNewClassSize(accessor))));
  if (new_class.IsNull()) {
    driver_->self_->AssertPendingOOMException();
    RecordFailure(
        ERR(OUT_OF_MEMORY),
        "Unable to allocate class object for redefinition of " + old_class->PrettyClass());
    driver_->self_->ClearException();
    return nullptr;
  }
  new_class->SetDexCache(cache.Get());
  linker->SetupClass(*dex_file,
                     dex_file->GetClassDef(dex_class_def_index),
                     new_class,
                     old_class->GetClassLoader());

  // Make sure we are ready for linking. The lock isn't really needed since this isn't visible to
  // other threads but the linker expects it.
  art::ObjectLock<art::mirror::Class> lock(driver_->self_, new_class);
  new_class->SetClinitThreadId(driver_->self_->GetTid());
  // Make sure we have a valid empty iftable even if there are errors.
  new_class->SetIfTable(art::GetClassRoot<art::mirror::Object>(linker)->GetIfTable());
  linker->LoadClass(
      driver_->self_, *dex_file, dex_file->GetClassDef(dex_class_def_index), new_class);
  // NB. We know the interfaces and supers didn't change! :)
  art::MutableHandle<art::mirror::Class> linked_class(hs.NewHandle<art::mirror::Class>(nullptr));
  art::Handle<art::mirror::ObjectArray<art::mirror::Class>> proxy_ifaces(
      hs.NewHandle<art::mirror::ObjectArray<art::mirror::Class>>(nullptr));
  // No changing hierarchy so everything is loaded.
  new_class->SetSuperClass(super_class.Get());
  art::mirror::Class::SetStatus(new_class, art::ClassStatus::kLoaded, nullptr);
  if (!linker->LinkClass(driver_->self_, nullptr, new_class, proxy_ifaces, &linked_class)) {
    std::ostringstream oss;
    oss << "failed to link class due to "
        << (driver_->self_->IsExceptionPending() ? driver_->self_->GetException()->Dump()
                                                 : " unknown");
    RecordFailure(ERR(INTERNAL), oss.str());
    driver_->self_->ClearException();
    return nullptr;
  }
  // Everything is already resolved.
  art::ObjectLock<art::mirror::Class> objlock(driver_->self_, linked_class);
  // Mark the class as initialized.
  CHECK(old_class->IsResolved())
      << "Attempting to redefine an unresolved class " << old_class->PrettyClass()
      << " status=" << old_class->GetStatus();
  CHECK(linked_class->IsResolved());
  if (old_class->WasVerificationAttempted()) {
    // Match verification-attempted flag
    linked_class->SetVerificationAttempted();
  }
  if (old_class->ShouldSkipHiddenApiChecks()) {
    // Match skip hiddenapi flag
    linked_class->SetSkipHiddenApiChecks();
  }
  if (old_class->IsInitialized()) {
    // We already verified the class earlier. No need to do it again.
    linker->ForceClassInitialized(driver_->self_, linked_class);
  } else if (old_class->GetStatus() > linked_class->GetStatus()) {
    // We want to match the old status.
    art::mirror::Class::SetStatus(linked_class, old_class->GetStatus(), driver_->self_);
  }
  // Make sure we have ext-data space for method & field ids. We won't know if we need them until
  // it's too late to create them.
  // TODO We might want to remove these arrays if they're not needed.
  if (!art::mirror::Class::EnsureInstanceFieldIds(linked_class) ||
      !art::mirror::Class::EnsureStaticFieldIds(linked_class) ||
      !art::mirror::Class::EnsureMethodIds(linked_class)) {
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
    RecordFailure(
        ERR(OUT_OF_MEMORY),
        "Unable to allocate jni-id arrays for redefinition of " + old_class->PrettyClass());
    return nullptr;
  }
  // Finish setting up methods.
  linked_class->VisitMethods([&](art::ArtMethod* m) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    linker->SetEntryPointsToInterpreter(m);
    m->SetNotIntrinsic();
    DCHECK(m->IsCopied() || m->GetDeclaringClass() == linked_class.Get())
        << m->PrettyMethod()
        << " m->GetDeclaringClass(): " << m->GetDeclaringClass()->PrettyClass()
        << " != linked_class.Get(): " << linked_class->PrettyClass();
  }, art::kRuntimePointerSize);
  if (art::kIsDebugBuild) {
    linked_class->VisitFields([&](art::ArtField* f) REQUIRES_SHARED(art::Locks::mutator_lock_) {
      DCHECK_EQ(f->GetDeclaringClass(), linked_class.Get());
    });
  }
  // Reset ClinitThreadId back to the thread that loaded the old class. This is needed if we are in
  // the middle of initializing a class.
  linked_class->SetClinitThreadId(old_class->GetClinitThreadId());
  return linked_class.Get();
}

void Redefiner::ClassRedefinition::UnregisterJvmtiBreakpoints() {
  BreakpointUtil::RemoveBreakpointsInClass(driver_->env_, GetMirrorClass().Ptr());
}

void Redefiner::UnregisterAllBreakpoints() {
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    redef.UnregisterJvmtiBreakpoints();
  }
}

bool Redefiner::CheckAllRedefinitionAreValid() {
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    if (!redef.CheckRedefinitionIsValid()) {
      return false;
    }
  }
  return true;
}

void Redefiner::RestoreObsoleteMethodMapsIfUnneeded(RedefinitionDataHolder& holder) {
  for (RedefinitionDataIter data = holder.begin(); data != holder.end(); ++data) {
    data.GetRedefinition().RestoreObsoleteMethodMapsIfUnneeded(&data);
  }
}

void Redefiner::MarkStructuralChanges(RedefinitionDataHolder& holder) {
  for (RedefinitionDataIter data = holder.begin(); data != holder.end(); ++data) {
    if (data.IsActuallyStructural()) {
      // A superclass was structural and it marked all subclasses already. No need to do anything.
      CHECK(!data.IsInitialStructural());
    } else if (data.GetRedefinition().IsStructuralRedefinition()) {
      data.SetActuallyStructural();
      data.SetInitialStructural();
      // Go over all potential subtypes and mark any that are actually subclasses as structural.
      for (RedefinitionDataIter sub_data = data + 1; sub_data != holder.end(); ++sub_data) {
        if (sub_data.GetRedefinition().GetMirrorClass()->IsSubClass(
                data.GetRedefinition().GetMirrorClass())) {
          sub_data.SetActuallyStructural();
        }
      }
    }
  }
}

bool Redefiner::EnsureAllClassAllocationsFinished(RedefinitionDataHolder& holder) {
  for (RedefinitionDataIter data = holder.begin(); data != holder.end(); ++data) {
    if (!data.GetRedefinition().EnsureClassAllocationsFinished(&data)) {
      return false;
    }
  }
  return true;
}

bool Redefiner::CollectAndCreateNewInstances(RedefinitionDataHolder& holder) {
  for (RedefinitionDataIter data = holder.begin(); data != holder.end(); ++data) {
    // Allocate the data this redefinition requires.
    if (!data.GetRedefinition().CollectAndCreateNewInstances(&data)) {
      return false;
    }
  }
  return true;
}

bool Redefiner::FinishAllNewClassAllocations(RedefinitionDataHolder& holder) {
  for (RedefinitionDataIter data = holder.begin(); data != holder.end(); ++data) {
    // Allocate the data this redefinition requires.
    if (!data.GetRedefinition().FinishNewClassAllocations(holder, &data)) {
      return false;
    }
  }
  return true;
}

bool Redefiner::FinishAllRemainingCommonAllocations(RedefinitionDataHolder& holder) {
  for (RedefinitionDataIter data = holder.begin(); data != holder.end(); ++data) {
    // Allocate the data this redefinition requires.
    if (!data.GetRedefinition().FinishRemainingCommonAllocations(&data)) {
      return false;
    }
  }
  return true;
}

void Redefiner::ClassRedefinition::ReleaseDexFile() {
  dex_file_.release();  // NOLINT b/117926937
}

void Redefiner::ReleaseAllDexFiles() {
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    redef.ReleaseDexFile();
  }
}

bool Redefiner::CheckAllClassesAreVerified(RedefinitionDataHolder& holder) {
  for (RedefinitionDataIter data = holder.begin(); data != holder.end(); ++data) {
    if (!data.GetRedefinition().CheckVerification(data)) {
      return false;
    }
  }
  return true;
}

class ScopedDisableConcurrentAndMovingGc {
 public:
  ScopedDisableConcurrentAndMovingGc(art::gc::Heap* heap, art::Thread* self)
      : heap_(heap), self_(self) {
    if (heap_->IsGcConcurrentAndMoving()) {
      heap_->IncrementDisableMovingGC(self_);
    }
  }

  ~ScopedDisableConcurrentAndMovingGc() {
    if (heap_->IsGcConcurrentAndMoving()) {
      heap_->DecrementDisableMovingGC(self_);
    }
  }
 private:
  art::gc::Heap* heap_;
  art::Thread* self_;
};

class ClassDefinitionPauser : public art::ClassLoadCallback {
 public:
  explicit ClassDefinitionPauser(art::Thread* self) REQUIRES_SHARED(art::Locks::mutator_lock_)
      : self_(self),
        is_running_(false),
        barrier_(0),
        release_mu_("SuspendClassDefinition lock", art::kGenericBottomLock),
        release_barrier_(0),
        release_cond_("SuspendClassDefinition condvar", release_mu_),
        count_(0),
        release_(false) {
    art::Locks::mutator_lock_->AssertSharedHeld(self_);
  }
  ~ClassDefinitionPauser() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    art::Locks::mutator_lock_->AssertSharedHeld(self_);
    CHECK(release_) << "Must call Release()";
  }
  void Release() REQUIRES(art::Locks::mutator_lock_) {
    if (is_running_) {
      art::Locks::mutator_lock_->AssertExclusiveHeld(self_);
      uint32_t count;
      // Wake up everything.
      {
        art::MutexLock mu(self_, release_mu_);
        release_ = true;
        // We have an exclusive mutator so all threads must be suspended and therefore they've
        // either already incremented this count_ or they are stuck somewhere before it.
        count = count_;
        release_cond_.Broadcast(self_);
      }
      // Wait for all threads to leave this structs code.
      VLOG(plugin) << "Resuming " << count << " threads paused before class-allocation!";
      release_barrier_.Increment</*locks=*/art::Barrier::kAllowHoldingLocks>(self_, count);
    } else {
      release_ = true;
    }
  }
  void BeginDefineClass() override REQUIRES_SHARED(art::Locks::mutator_lock_) {
    art::Thread* this_thread = art::Thread::Current();
    if (this_thread == self_) {
      // Allow the redefining thread to do whatever.
      return;
    }
    if (this_thread->GetDefineClassCount() != 0) {
      // We are in the middle of a recursive define-class. Don't suspend now allow it to finish.
      VLOG(plugin) << "Recursive DefineClass in " << *this_thread
                   << " allowed to proceed despite class-def pause initiated by " << *self_;
      return;
    }
    // If we are suspended (no mutator-lock) then the pausing thread could do everything before the
    // count_++ including destroying this object, causing UAF/deadlock.
    art::Locks::mutator_lock_->AssertSharedHeld(this_thread);
    ++count_;
    art::ScopedThreadSuspension sts(this_thread, art::ThreadState::kSuspended);
    {
      art::MutexLock mu(this_thread, release_mu_);
      VLOG(plugin) << "Suspending " << *this_thread << " due to class definition. class-def pause "
                   << "initiated by " << *self_;
      while (!release_) {
        release_cond_.Wait(this_thread);
      }
    }
    release_barrier_.Pass(this_thread);
  }

  void EndDefineClass() override REQUIRES_SHARED(art::Locks::mutator_lock_) {
    art::Thread* this_thread = art::Thread::Current();
    if (this_thread == self_) {
      // Allow the redefining thread to do whatever.
      return;
    }
    if (this_thread->GetDefineClassCount() == 0) {
      // We are done with defining classes.
      barrier_.Pass(this_thread);
    }
  }

  void ClassLoad(art::Handle<art::mirror::Class> klass ATTRIBUTE_UNUSED) override {}
  void ClassPrepare(art::Handle<art::mirror::Class> klass1 ATTRIBUTE_UNUSED,
                    art::Handle<art::mirror::Class> klass2 ATTRIBUTE_UNUSED) override {}

  void SetRunning() {
    is_running_ = true;
  }
  void WaitFor(uint32_t t) REQUIRES(!art::Locks::mutator_lock_) {
    barrier_.Increment(self_, t);
  }

 private:
  art::Thread* self_;
  bool is_running_;
  art::Barrier barrier_;
  art::Mutex release_mu_;
  art::Barrier release_barrier_;
  art::ConditionVariable release_cond_;
  std::atomic<uint32_t> count_;
  bool release_;
};

class ScopedSuspendClassLoading {
 public:
  ScopedSuspendClassLoading(art::Thread* self, art::Runtime* runtime, RedefinitionDataHolder& h)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      : self_(self), runtime_(runtime), pauser_() {
    if (std::any_of(h.begin(), h.end(), [](auto r) REQUIRES_SHARED(art::Locks::mutator_lock_) {
          return r.GetRedefinition().IsStructuralRedefinition();
        })) {
      VLOG(plugin) << "Pausing Class loading for structural redefinition.";
      pauser_.emplace(self);
      {
        art::ScopedThreadSuspension sts(self_, art::ThreadState::kNative);
        uint32_t in_progress_defines = 0;
        {
          art::ScopedSuspendAll ssa(__FUNCTION__);
          pauser_->SetRunning();
          runtime_->GetRuntimeCallbacks()->AddClassLoadCallback(&pauser_.value());
          art::MutexLock mu(self_, *art::Locks::thread_list_lock_);
          runtime_->GetThreadList()->ForEach([&](art::Thread* t) {
            if (t != self_ && t->GetDefineClassCount() != 0) {
              in_progress_defines++;
            }
          });
          VLOG(plugin) << "Waiting for " << in_progress_defines
                       << " in progress class-loads to finish";
        }
        pauser_->WaitFor(in_progress_defines);
      }
    }
  }
  ~ScopedSuspendClassLoading() {
    if (pauser_.has_value()) {
      art::ScopedThreadSuspension sts(self_, art::ThreadState::kNative);
      art::ScopedSuspendAll ssa(__FUNCTION__);
      pauser_->Release();
      runtime_->GetRuntimeCallbacks()->RemoveClassLoadCallback(&pauser_.value());
    }
  }

 private:
  art::Thread* self_;
  art::Runtime* runtime_;
  std::optional<ClassDefinitionPauser> pauser_;
};

class ScopedSuspendAllocations {
 public:
  ScopedSuspendAllocations(art::Runtime* runtime, RedefinitionDataHolder& h)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      : paused_(false) {
    if (std::any_of(h.begin(),
                    h.end(),
                    [](auto r) REQUIRES_SHARED(art::Locks::mutator_lock_) {
                      return r.GetRedefinition().IsStructuralRedefinition();
                    })) {
      VLOG(plugin) << "Pausing allocations for structural redefinition.";
      paused_ = true;
      AllocationManager::Get()->PauseAllocations(art::Thread::Current());
      // Collect garbage so we don't need to recreate as much.
      runtime->GetHeap()->CollectGarbage(/*clear_soft_references=*/false);
    }
  }

  ~ScopedSuspendAllocations() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    if (paused_) {
      AllocationManager::Get()->ResumeAllocations(art::Thread::Current());
    }
  }

 private:
  bool paused_;

  DISALLOW_COPY_AND_ASSIGN(ScopedSuspendAllocations);
};

jvmtiError Redefiner::Run() {
  art::StackHandleScope<1> hs(self_);
  // Sort the redefinitions_ array topologically by class. This makes later steps easier since we
  // know that every class precedes all of its supertypes.
  std::sort(redefinitions_.begin(),
            redefinitions_.end(),
            [&](auto& l, auto& r) REQUIRES_SHARED(art::Locks::mutator_lock_) {
              return CompareClasses(l.GetMirrorClass(), r.GetMirrorClass());
            });
  // Allocate an array to hold onto all java temporary objects associated with this
  // redefinition. We will let this be collected after the end of this function.
  RedefinitionDataHolder holder(&hs, runtime_, self_, &redefinitions_);
  if (holder.IsNull()) {
    self_->AssertPendingOOMException();
    self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Could not allocate storage for temporaries");
    return result_;
  }

  // First we just allocate the ClassExt and its fields that we need. These can be updated
  // atomically without any issues (since we allocate the map arrays as empty).
  if (!CheckAllRedefinitionAreValid()) {
    return result_;
  }
  // Mark structural changes.
  MarkStructuralChanges(holder);
  // Now we pause class loading. If we are doing a structural redefinition we will need to get an
  // accurate picture of the classes loaded and having loads in the middle would make that
  // impossible. This only pauses class-loading if we actually have at least one structural
  // redefinition.
  ScopedSuspendClassLoading suspend_class_load(self_, runtime_, holder);
  if (!EnsureAllClassAllocationsFinished(holder) ||
      !FinishAllRemainingCommonAllocations(holder) ||
      !FinishAllNewClassAllocations(holder) ||
      !CheckAllClassesAreVerified(holder)) {
    return result_;
  }

  ScopedSuspendAllocations suspend_alloc(runtime_, holder);
  if (!CollectAndCreateNewInstances(holder)) {
    return result_;
  }

  // At this point we can no longer fail without corrupting the runtime state.
  for (RedefinitionDataIter data = holder.begin(); data != holder.end(); ++data) {
    art::ClassLinker* cl = runtime_->GetClassLinker();
    cl->RegisterExistingDexCache(data.GetNewDexCache(), data.GetSourceClassLoader());
    if (data.GetSourceClassLoader() == nullptr) {
      cl->AppendToBootClassPath(self_, &data.GetRedefinition().GetDexFile());
    }
  }
  UnregisterAllBreakpoints();

  {
    // Disable GC and wait for it to be done if we are a moving GC.  This is fine since we are done
    // allocating so no deadlocks.
    ScopedDisableConcurrentAndMovingGc sdcamgc(runtime_->GetHeap(), self_);

    // Do transition to final suspension
    // TODO We might want to give this its own suspended state!
    // TODO This isn't right. We need to change state without any chance of suspend ideally!
    art::ScopedThreadSuspension sts(self_, art::ThreadState::kNative);
    art::ScopedSuspendAll ssa("Final installation of redefined Classes!", /*long_suspend=*/true);
    for (RedefinitionDataIter data = holder.begin(); data != holder.end(); ++data) {
      art::ScopedAssertNoThreadSuspension nts("Updating runtime objects for redefinition");
      ClassRedefinition& redef = data.GetRedefinition();
      if (data.GetSourceClassLoader() != nullptr) {
        ClassLoaderHelper::UpdateJavaDexFile(data.GetJavaDexFile(), data.GetNewDexFileCookie());
      }
      redef.UpdateClass(data);
    }
    RestoreObsoleteMethodMapsIfUnneeded(holder);
    // TODO We should check for if any of the redefined methods are intrinsic methods here and, if
    // any are, force a full-world deoptimization before finishing redefinition. If we don't do this
    // then methods that have been jitted prior to the current redefinition being applied might
    // continue to use the old versions of the intrinsics!
    // TODO Do the dex_file release at a more reasonable place. This works but it muddles who really
    // owns the DexFile and when ownership is transferred.
    ReleaseAllDexFiles();
  }
  // By now the class-linker knows about all the classes so we can safetly retry verification and
  // update method flags.
  ReverifyClasses(holder);
  return OK;
}

void Redefiner::ReverifyClasses(RedefinitionDataHolder& holder) {
  for (RedefinitionDataIter data = holder.begin(); data != holder.end(); ++data) {
    data.GetRedefinition().ReverifyClass(data);
  }
}

void Redefiner::ClassRedefinition::ReverifyClass(const RedefinitionDataIter &cur_data) {
  if (!needs_reverify_) {
    return;
  }
  VLOG(plugin) << "Reverifying " << class_sig_ << " due to soft failures";
  std::string error;
  // TODO Make verification log level lower
  art::verifier::FailureKind failure =
      art::verifier::ClassVerifier::ReverifyClass(driver_->self_,
                                                  cur_data.GetMirrorClass(),
                                                  /*log_level=*/
                                                  art::verifier::HardFailLogMode::kLogWarning,
                                                  /*api_level=*/
                                                  art::Runtime::Current()->GetTargetSdkVersion(),
                                                  &error);
  CHECK_NE(failure, art::verifier::FailureKind::kHardFailure);
}

void Redefiner::ClassRedefinition::UpdateMethods(art::ObjPtr<art::mirror::Class> mclass,
                                                 const art::dex::ClassDef& class_def) {
  art::ClassLinker* linker = driver_->runtime_->GetClassLinker();
  art::PointerSize image_pointer_size = linker->GetImagePointerSize();
  const art::dex::TypeId& declaring_class_id = dex_file_->GetTypeId(class_def.class_idx_);
  const art::DexFile& old_dex_file = mclass->GetDexFile();
  // Update methods.
  for (art::ArtMethod& method : mclass->GetDeclaredMethods(image_pointer_size)) {
    const art::dex::StringId* new_name_id = dex_file_->FindStringId(method.GetName());
    art::dex::TypeIndex method_return_idx =
        dex_file_->GetIndexForTypeId(*dex_file_->FindTypeId(method.GetReturnTypeDescriptor()));
    const auto* old_type_list = method.GetParameterTypeList();
    std::vector<art::dex::TypeIndex> new_type_list;
    for (uint32_t i = 0; old_type_list != nullptr && i < old_type_list->Size(); i++) {
      new_type_list.push_back(
          dex_file_->GetIndexForTypeId(
              *dex_file_->FindTypeId(
                  old_dex_file.GetTypeDescriptor(
                      old_dex_file.GetTypeId(
                          old_type_list->GetTypeItem(i).type_idx_)))));
    }
    const art::dex::ProtoId* proto_id = dex_file_->FindProtoId(method_return_idx, new_type_list);
    CHECK(proto_id != nullptr || old_type_list == nullptr);
    const art::dex::MethodId* method_id = dex_file_->FindMethodId(declaring_class_id,
                                                                  *new_name_id,
                                                                  *proto_id);
    CHECK(method_id != nullptr);
    uint32_t dex_method_idx = dex_file_->GetIndexForMethodId(*method_id);
    method.SetDexMethodIndex(dex_method_idx);
    linker->SetEntryPointsToInterpreter(&method);
    method.SetCodeItemOffset(dex_file_->FindCodeItemOffset(class_def, dex_method_idx));
    // Clear all the intrinsics related flags.
    method.SetNotIntrinsic();
  }
}

void Redefiner::ClassRedefinition::UpdateFields(art::ObjPtr<art::mirror::Class> mclass) {
  // TODO The IFields & SFields pointers should be combined like the methods_ arrays were.
  for (auto fields_iter : {mclass->GetIFields(), mclass->GetSFields()}) {
    for (art::ArtField& field : fields_iter) {
      std::string declaring_class_name;
      const art::dex::TypeId* new_declaring_id =
          dex_file_->FindTypeId(field.GetDeclaringClass()->GetDescriptor(&declaring_class_name));
      const art::dex::StringId* new_name_id = dex_file_->FindStringId(field.GetName());
      const art::dex::TypeId* new_type_id = dex_file_->FindTypeId(field.GetTypeDescriptor());
      CHECK(new_name_id != nullptr && new_type_id != nullptr && new_declaring_id != nullptr);
      const art::dex::FieldId* new_field_id =
          dex_file_->FindFieldId(*new_declaring_id, *new_name_id, *new_type_id);
      CHECK(new_field_id != nullptr);
      uint32_t new_field_index = dex_file_->GetIndexForFieldId(*new_field_id);
      // We only need to update the index since the other data in the ArtField cannot be updated.
      field.SetDexFieldIndex(new_field_index);
    }
  }
}

void Redefiner::ClassRedefinition::CollectNewFieldAndMethodMappings(
    const RedefinitionDataIter& data,
    std::map<art::ArtMethod*, art::ArtMethod*>* method_map,
    std::map<art::ArtField*, art::ArtField*>* field_map) {
  for (auto [new_cls, old_cls] :
       art::ZipLeft(data.GetNewClasses()->Iterate(), data.GetOldClasses()->Iterate())) {
    for (art::ArtField& f : old_cls->GetSFields()) {
      (*field_map)[&f] = new_cls->FindDeclaredStaticField(f.GetName(), f.GetTypeDescriptor());
    }
    for (art::ArtField& f : old_cls->GetIFields()) {
      (*field_map)[&f] = new_cls->FindDeclaredInstanceField(f.GetName(), f.GetTypeDescriptor());
    }
    auto new_methods = new_cls->GetMethods(art::kRuntimePointerSize);
    for (art::ArtMethod& m : old_cls->GetMethods(art::kRuntimePointerSize)) {
      // No support for finding methods in this way since it's generally not needed. Just do it the
      // easy way.
      auto nm_iter = std::find_if(
          new_methods.begin(),
          new_methods.end(),
          [&](art::ArtMethod& cand) REQUIRES_SHARED(art::Locks::mutator_lock_) {
            return cand.GetNameView() == m.GetNameView() && cand.GetSignature() == m.GetSignature();
          });
      CHECK(nm_iter != new_methods.end())
          << "Could not find redefined version of " << m.PrettyMethod();
      (*method_map)[&m] = &(*nm_iter);
    }
  }
}

static void CopyField(art::ObjPtr<art::mirror::Object> target,
                      art::ArtField* new_field,
                      art::ObjPtr<art::mirror::Object> source,
                      art::ArtField& old_field) REQUIRES(art::Locks::mutator_lock_) {
  art::Primitive::Type ftype = old_field.GetTypeAsPrimitiveType();
  CHECK_EQ(ftype, new_field->GetTypeAsPrimitiveType())
      << old_field.PrettyField() << " vs " << new_field->PrettyField();
  if (ftype == art::Primitive::kPrimNot) {
    new_field->SetObject<false>(target, old_field.GetObject(source));
  } else {
    switch (ftype) {
#define UPDATE_FIELD(TYPE)                                            \
  case art::Primitive::kPrim##TYPE:                                   \
    new_field->Set##TYPE<false>(target, old_field.Get##TYPE(source)); \
    break
      UPDATE_FIELD(Int);
      UPDATE_FIELD(Float);
      UPDATE_FIELD(Long);
      UPDATE_FIELD(Double);
      UPDATE_FIELD(Short);
      UPDATE_FIELD(Char);
      UPDATE_FIELD(Byte);
      UPDATE_FIELD(Boolean);
      case art::Primitive::kPrimNot:
      case art::Primitive::kPrimVoid:
        LOG(FATAL) << "Unexpected field with type " << ftype << " found!";
        UNREACHABLE();
#undef UPDATE_FIELD
    }
  }
}

static void CopyFields(bool is_static,
                       art::ObjPtr<art::mirror::Object> target,
                       art::ObjPtr<art::mirror::Class> target_class,
                       art::ObjPtr<art::mirror::Object> source,
                       art::ObjPtr<art::mirror::Class> source_class)
    REQUIRES(art::Locks::mutator_lock_) {
  DCHECK(!source_class->IsObjectClass() && !target_class->IsObjectClass())
      << "Should not be overriding object class fields. Target: " << target_class->PrettyClass()
      << " Source: " << source_class->PrettyClass();
  for (art::ArtField& f : (is_static ? source_class->GetSFields() : source_class->GetIFields())) {
    art::ArtField* new_field =
        (is_static ? target_class->FindDeclaredStaticField(f.GetName(), f.GetTypeDescriptor())
                   : target_class->FindDeclaredInstanceField(f.GetName(), f.GetTypeDescriptor()));
    CHECK(new_field != nullptr) << "could not find new version of " << f.PrettyField();
    CopyField(target, new_field, source, f);
  }
  if (!is_static && !target_class->GetSuperClass()->IsObjectClass()) {
    CopyFields(
        is_static, target, target_class->GetSuperClass(), source, source_class->GetSuperClass());
  }
}

static void ClearField(art::ObjPtr<art::mirror::Object> target, art::ArtField& field)
    REQUIRES(art::Locks::mutator_lock_) {
  art::Primitive::Type ftype = field.GetTypeAsPrimitiveType();
  if (ftype == art::Primitive::kPrimNot) {
    field.SetObject<false>(target, nullptr);
  } else {
    switch (ftype) {
#define UPDATE_FIELD(TYPE)             \
  case art::Primitive::kPrim##TYPE:    \
    field.Set##TYPE<false>(target, 0); \
    break
      UPDATE_FIELD(Int);
      UPDATE_FIELD(Float);
      UPDATE_FIELD(Long);
      UPDATE_FIELD(Double);
      UPDATE_FIELD(Short);
      UPDATE_FIELD(Char);
      UPDATE_FIELD(Byte);
      UPDATE_FIELD(Boolean);
      case art::Primitive::kPrimNot:
      case art::Primitive::kPrimVoid:
        LOG(FATAL) << "Unexpected field with type " << ftype << " found!";
        UNREACHABLE();
#undef UPDATE_FIELD
    }
  }
}

static void ClearFields(bool is_static,
                        art::ObjPtr<art::mirror::Object> target,
                        art::ObjPtr<art::mirror::Class> target_class)
    REQUIRES(art::Locks::mutator_lock_) {
  DCHECK(!target_class->IsObjectClass());
  for (art::ArtField& f : (is_static ? target_class->GetSFields() : target_class->GetIFields())) {
    ClearField(target, f);
  }
  if (!is_static && !target_class->GetSuperClass()->IsObjectClass()) {
    ClearFields(is_static, target, target_class->GetSuperClass());
  }
}

static void CopyAndClearFields(bool is_static,
                               art::ObjPtr<art::mirror::Object> target,
                               art::ObjPtr<art::mirror::Class> target_class,
                               art::ObjPtr<art::mirror::Object> source,
                               art::ObjPtr<art::mirror::Class> source_class)
    REQUIRES(art::Locks::mutator_lock_) {
  // Copy all non-j.l.Object fields
  CopyFields(is_static, target, target_class, source, source_class);
  // Copy the lock-word.
  target->SetLockWord(source->GetLockWord(false), false);
  // Clear (reset) the old one.
  source->SetLockWord(art::LockWord::Default(), false);
  art::WriteBarrier::ForEveryFieldWrite(target);

  // Clear the fields from the old class. We don't need it anymore.
  ClearFields(is_static, source, source_class);
  art::WriteBarrier::ForEveryFieldWrite(source);
}

void Redefiner::ClassRedefinition::UpdateClassStructurally(const RedefinitionDataIter& holder) {
  DCHECK(holder.IsActuallyStructural());
  DCHECK(holder.IsInitialStructural());
  // LETS GO. We've got all new class structures so no need to do all the updating of the stacks.
  // Instead we need to update everything else.
  // Just replace the class and be done with it.
  art::Locks::mutator_lock_->AssertExclusiveHeld(driver_->self_);
  art::ClassLinker* cl = driver_->runtime_->GetClassLinker();
  art::ScopedAssertNoThreadSuspension sants(__FUNCTION__);
  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>> new_classes(holder.GetNewClasses());
  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Class>> old_classes(holder.GetOldClasses());
  // Collect mappings from old to new fields/methods
  std::map<art::ArtMethod*, art::ArtMethod*> method_map;
  std::map<art::ArtField*, art::ArtField*> field_map;
  CollectNewFieldAndMethodMappings(holder, &method_map, &field_map);
  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>> new_instances(
      holder.GetNewInstanceObjects());
  art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>> old_instances(
      holder.GetOldInstanceObjects());
  // Once we do the ReplaceReferences old_classes will have the new_classes in it. We want to keep
  // ahold of the old classes so copy them now.
  std::vector<art::ObjPtr<art::mirror::Class>> old_classes_vec(old_classes->Iterate().begin(),
                                                               old_classes->Iterate().end());
  // Copy over the static fields of the class and all the instance fields.
  for (auto [new_class, old_class] : art::ZipLeft(new_classes->Iterate(), old_classes->Iterate())) {
    CHECK(!new_class.IsNull());
    CHECK(!old_class.IsNull());
    CHECK(!old_class->IsErroneous());
    if (old_class->GetStatus() > new_class->GetStatus()) {
      // Some verification/initialization step happened during interval between
      // creating the new class and now. Just copy the new status.
      new_class->SetStatusLocked(old_class->GetStatus());
    }
    CopyAndClearFields(true, new_class, new_class, old_class, old_class);
  }

  // Copy and clear the fields of the old-instances.
  for (auto [new_instance, old_instance] :
       art::ZipLeft(new_instances->Iterate(), old_instances->Iterate())) {
    CopyAndClearFields(/*is_static=*/false,
                       new_instance,
                       new_instance->GetClass(),
                       old_instance,
                       old_instance->GetClass());
  }
  // Mark old class and methods obsolete. Copy over any native implementation as well.
  for (auto [old_class, new_class] : art::ZipLeft(old_classes->Iterate(), new_classes->Iterate())) {
    old_class->SetObsoleteObject();
    // Mark methods obsolete and copy native implementation. We need to wait
    // until later to actually clear the jit data. We copy the native
    // implementation here since we don't want to race with any threads doing
    // RegisterNatives.
    for (art::ArtMethod& m : old_class->GetMethods(art::kRuntimePointerSize)) {
      if (m.IsNative()) {
        art::ArtMethod* new_method =
            new_class->FindClassMethod(m.GetNameView(), m.GetSignature(), art::kRuntimePointerSize);
        DCHECK(new_class->GetMethodsSlice(art::kRuntimePointerSize).Contains(new_method))
            << "Could not find method " << m.PrettyMethod() << " declared in new class!";
        DCHECK(new_method->IsNative());
        new_method->SetEntryPointFromJni(m.GetEntryPointFromJni());
      }
      m.SetIsObsolete();
      cl->SetEntryPointsForObsoleteMethod(&m);
      if (m.IsInvokable()) {
        m.SetDontCompile();
      }
    }
  }
  // Update live pointers in ART code.
  auto could_change_resolution_of = [&](auto* field_or_method,
                                        const auto& info) REQUIRES(art::Locks::mutator_lock_) {
    constexpr bool is_method = std::is_same_v<art::ArtMethod*, decltype(field_or_method)>;
    static_assert(is_method || std::is_same_v<art::ArtField*, decltype(field_or_method)>,
                  "Input is not field or method!");
    // Only dex-cache is used for resolution
    if (LIKELY(info.GetType() != art::ReflectionSourceType::kSourceDexCacheResolvedField &&
               info.GetType() != art::ReflectionSourceType::kSourceDexCacheResolvedMethod)) {
      return false;
    }
    if constexpr (is_method) {
      // Only direct methods are used without further indirection through a vtable/IFTable.
      // Constructors cannot be shadowed.
      if (LIKELY(!field_or_method->IsDirect() || field_or_method->IsConstructor())) {
        return false;
      }
    } else {
      // Only non-private fields can be shadowed in a manner that's visible.
      if (LIKELY(field_or_method->IsPrivate())) {
        return false;
      }
    }
    // We can only shadow things from our superclasses
    auto orig_classes_iter = old_classes->Iterate();
    auto replacement_classes_iter = new_classes->Iterate();
    art::ObjPtr<art::mirror::Class> f_or_m_class = field_or_method->GetDeclaringClass();
    if (LIKELY(!f_or_m_class->IsAssignableFrom(holder.GetMirrorClass()) &&
               std::find(orig_classes_iter.begin(), orig_classes_iter.end(), f_or_m_class) ==
                   orig_classes_iter.end())) {
      return false;
    }
    if constexpr (is_method) {
      return std::any_of(
          replacement_classes_iter.begin(),
          replacement_classes_iter.end(),
          [&](art::ObjPtr<art::mirror::Class> cand) REQUIRES(art::Locks::mutator_lock_) {
            auto direct_methods = cand->GetDirectMethods(art::kRuntimePointerSize);
            return std::find_if(direct_methods.begin(),
                                direct_methods.end(),
                                [&](art::ArtMethod& m) REQUIRES(art::Locks::mutator_lock_) {
                                  return UNLIKELY(m.HasSameNameAndSignature(field_or_method));
                                }) != direct_methods.end();
          });
    } else {
      auto pred = [&](art::ArtField& f) REQUIRES(art::Locks::mutator_lock_) {
        return std::string_view(f.GetName()) == std::string_view(field_or_method->GetName()) &&
               std::string_view(f.GetTypeDescriptor()) ==
                   std::string_view(field_or_method->GetTypeDescriptor());
      };
      if (field_or_method->IsStatic()) {
        return std::any_of(
            replacement_classes_iter.begin(),
            replacement_classes_iter.end(),
            [&](art::ObjPtr<art::mirror::Class> cand) REQUIRES(art::Locks::mutator_lock_) {
              auto sfields = cand->GetSFields();
              return std::find_if(sfields.begin(), sfields.end(), pred) != sfields.end();
            });
      } else {
        return std::any_of(
            replacement_classes_iter.begin(),
            replacement_classes_iter.end(),
            [&](art::ObjPtr<art::mirror::Class> cand) REQUIRES(art::Locks::mutator_lock_) {
              auto ifields = cand->GetIFields();
              return std::find_if(ifields.begin(), ifields.end(), pred) != ifields.end();
            });
      }
    }
  };
  // TODO Performing 2 stack-walks back to back isn't the greatest. We might want to try to combine
  // it with the one ReplaceReferences does. Doing so would be rather complicated though.
  driver_->runtime_->VisitReflectiveTargets(
      [&](art::ArtField* f, const auto& info) REQUIRES(art::Locks::mutator_lock_) {
        DCHECK(f != nullptr) << info;
        auto it = field_map.find(f);
        if (UNLIKELY(could_change_resolution_of(f, info))) {
          // Dex-cache Resolution might change. Just clear the resolved value.
          VLOG(plugin) << "Clearing resolution " << info << " for (field) " << f->PrettyField();
          return static_cast<art::ArtField*>(nullptr);
        } else if (it != field_map.end()) {
          VLOG(plugin) << "Updating " << info << " object for (field) "
                       << it->second->PrettyField();
          return it->second;
        }
        return f;
      },
      [&](art::ArtMethod* m, const auto& info) REQUIRES(art::Locks::mutator_lock_) {
        DCHECK(m != nullptr) << info;
        auto it = method_map.find(m);
        if (UNLIKELY(could_change_resolution_of(m, info))) {
          // Dex-cache Resolution might change. Just clear the resolved value.
          VLOG(plugin) << "Clearing resolution " << info << " for (method) " << m->PrettyMethod();
          return static_cast<art::ArtMethod*>(nullptr);
        } else if (it != method_map.end()) {
          VLOG(plugin) << "Updating " << info << " object for (method) "
                      << it->second->PrettyMethod();
          return it->second;
        }
        return m;
      });

  // Force every frame of every thread to deoptimize (any frame might have eg offsets compiled in).
  driver_->runtime_->GetInstrumentation()->DeoptimizeAllThreadFrames();

  std::unordered_map<art::ObjPtr<art::mirror::Object>,
                     art::ObjPtr<art::mirror::Object>,
                     art::HashObjPtr> map;
  for (auto [new_class, old_class] : art::ZipLeft(new_classes->Iterate(), old_classes->Iterate())) {
    map.emplace(old_class, new_class);
  }
  for (auto [new_instance, old_instance] :
       art::ZipLeft(new_instances->Iterate(), old_instances->Iterate())) {
    map.emplace(old_instance, new_instance);
    // Bare-bones check that the mapping is correct.
    CHECK(new_instance->GetClass() == map[old_instance->GetClass()]->AsClass())
        << new_instance->GetClass()->PrettyClass() << " vs "
        << map[old_instance->GetClass()]->AsClass()->PrettyClass();
  }

  // Actually perform the general replacement. This doesn't affect ArtMethod/ArtFields. It does
  // affect the declaring_class field of all the obsolete objects, which is unfortunate and needs to
  // be undone. This replaces the mirror::Class in 'holder' as well. It's magic!
  HeapExtensions::ReplaceReferences(driver_->self_, map);

  // Save the old class so that the JIT gc doesn't get confused by it being collected before the
  // jit code. This is also needed to keep the dex-caches of any obsolete methods live.
  for (auto [new_class, old_class] :
       art::ZipLeft(new_classes->Iterate(), art::MakeIterationRange(old_classes_vec))) {
    new_class->GetExtData()->SetObsoleteClass(old_class);
  }

  art::jit::Jit* jit = driver_->runtime_->GetJit();
  if (jit != nullptr) {
    // Clear jit.
    // TODO We might want to have some way to tell the JIT not to wait the kJitSamplesBatchSize
    // invokes to start compiling things again.
    jit->GetCodeCache()->InvalidateAllCompiledCode();
  }

  // Clear thread caches
  {
    // TODO We might be able to avoid doing this but given the rather unstructured nature of the
    // interpreter cache it's probably not worth the effort.
    art::MutexLock mu(driver_->self_, *art::Locks::thread_list_lock_);
    driver_->runtime_->GetThreadList()->ForEach(
        [](art::Thread* t) { t->GetInterpreterCache()->Clear(t); });
  }

  if (art::kIsDebugBuild) {
    // Just make sure we didn't screw up any of the now obsolete methods or fields. We need their
    // declaring-class to still be the obolete class
    std::for_each(
        old_classes_vec.cbegin(),
        old_classes_vec.cend(),
        [](art::ObjPtr<art::mirror::Class> orig) REQUIRES_SHARED(art::Locks::mutator_lock_) {
          orig->VisitMethods(
              [&](art::ArtMethod* method) REQUIRES_SHARED(art::Locks::mutator_lock_) {
                if (method->IsCopied()) {
                  // Copied methods have interfaces as their declaring class.
                  return;
                }
                DCHECK_EQ(method->GetDeclaringClass(), orig)
                    << method->GetDeclaringClass()->PrettyClass() << " vs " << orig->PrettyClass();
              },
              art::kRuntimePointerSize);
          orig->VisitFields([&](art::ArtField* field) REQUIRES_SHARED(art::Locks::mutator_lock_) {
            DCHECK_EQ(field->GetDeclaringClass(), orig)
                << field->GetDeclaringClass()->PrettyClass() << " vs " << orig->PrettyClass();
          });
        });
  }
}

// Redefines the class in place
void Redefiner::ClassRedefinition::UpdateClassInPlace(const RedefinitionDataIter& holder) {
  art::ObjPtr<art::mirror::Class> mclass(holder.GetMirrorClass());
  // TODO Rewrite so we don't do a stack walk for each and every class.
  FindAndAllocateObsoleteMethods(mclass);
  art::ObjPtr<art::mirror::DexCache> new_dex_cache(holder.GetNewDexCache());
  art::ObjPtr<art::mirror::Object> original_dex_file(holder.GetOriginalDexFile());
  DCHECK_EQ(dex_file_->NumClassDefs(), 1u);
  const art::dex::ClassDef& class_def = dex_file_->GetClassDef(0);
  UpdateMethods(mclass, class_def);
  UpdateFields(mclass);

  art::ObjPtr<art::mirror::ClassExt> ext(mclass->GetExtData());
  CHECK(!ext.IsNull());
  ext->SetOriginalDexFile(original_dex_file);

  // If this is the first time the class is being redefined, store
  // the native DexFile pointer and initial ClassDef index in ClassExt.
  // This preserves the pointer for hiddenapi access checks which need
  // to read access flags from the initial DexFile.
  if (ext->GetPreRedefineDexFile() == nullptr) {
    ext->SetPreRedefineDexFile(&mclass->GetDexFile());
    ext->SetPreRedefineClassDefIndex(mclass->GetDexClassDefIndex());
  }

  // Update the class fields.
  // Need to update class last since the ArtMethod gets its DexFile from the class (which is needed
  // to call GetReturnTypeDescriptor and GetParameterTypeList above).
  mclass->SetDexCache(new_dex_cache.Ptr());
  mclass->SetDexClassDefIndex(dex_file_->GetIndexForClassDef(class_def));
  mclass->SetDexTypeIndex(dex_file_->GetIndexForTypeId(*dex_file_->FindTypeId(class_sig_.c_str())));

  // Notify the jit that all the methods in this class were redefined. Need to do this last since
  // the jit relies on the dex_file_ being correct (for native methods at least) to find the method
  // meta-data.
  art::jit::Jit* jit = driver_->runtime_->GetJit();
  if (jit != nullptr) {
    art::PointerSize image_pointer_size =
        driver_->runtime_->GetClassLinker()->GetImagePointerSize();
    auto code_cache = jit->GetCodeCache();
    // Non-invokable methods don't have any JIT data associated with them so we don't need to tell
    // the jit about them.
    for (art::ArtMethod& method : mclass->GetDeclaredMethods(image_pointer_size)) {
      if (method.IsInvokable()) {
        code_cache->NotifyMethodRedefined(&method);
      }
    }
  }
}

// Performs final updates to class for redefinition.
void Redefiner::ClassRedefinition::UpdateClass(const RedefinitionDataIter& holder) {
  CHECK(holder.IsInitialized());
  if (holder.IsInitialStructural()) {
    UpdateClassStructurally(holder);
  } else if (!holder.IsActuallyStructural()) {
    UpdateClassInPlace(holder);
  }
  UpdateClassCommon(holder);
}

void Redefiner::ClassRedefinition::UpdateClassCommon(const RedefinitionDataIter &cur_data) {
  // NB This is after we've already replaced all old-refs with new-refs in the structural case.
  art::ObjPtr<art::mirror::Class> klass(cur_data.GetMirrorClass());
  DCHECK(!IsStructuralRedefinition() || klass == cur_data.GetNewClassObject());
  if (!needs_reverify_) {
    return;
  }
  // Force the most restrictive interpreter environment. We don't know what the final verification
  // will allow. We will clear these after retrying verification once we drop the mutator-lock.
  klass->VisitMethods([](art::ArtMethod* m) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    if (!m->IsNative() && m->IsInvokable() && !m->IsObsolete()) {
      m->ClearSkipAccessChecks();
      m->SetDontCompile();
      m->SetMustCountLocks();
    }
  }, art::kRuntimePointerSize);
}

// Restores the old obsolete methods maps if it turns out they weren't needed (ie there were no new
// obsolete methods).
void Redefiner::ClassRedefinition::RestoreObsoleteMethodMapsIfUnneeded(
    const RedefinitionDataIter* cur_data) {
  if (cur_data->IsActuallyStructural()) {
    // We didn't touch these in this case.
    return;
  }
  art::ObjPtr<art::mirror::Class> klass = GetMirrorClass();
  art::ObjPtr<art::mirror::ClassExt> ext = klass->GetExtData();
  art::ObjPtr<art::mirror::PointerArray> methods = ext->GetObsoleteMethods();
  art::ObjPtr<art::mirror::PointerArray> old_methods = cur_data->GetOldObsoleteMethods();
  int32_t old_length = old_methods == nullptr ? 0 : old_methods->GetLength();
  int32_t expected_length =
      old_length + klass->NumDirectMethods() + klass->NumDeclaredVirtualMethods();
  // Check to make sure we are only undoing this one.
  if (methods.IsNull()) {
    // No new obsolete methods! We can get rid of the maps.
    ext->SetObsoleteArrays(cur_data->GetOldObsoleteMethods(), cur_data->GetOldDexCaches());
  } else if (expected_length == methods->GetLength()) {
    for (int32_t i = 0; i < expected_length; i++) {
      art::ArtMethod* expected = nullptr;
      if (i < old_length) {
        expected = old_methods->GetElementPtrSize<art::ArtMethod*>(i, art::kRuntimePointerSize);
      }
      if (methods->GetElementPtrSize<art::ArtMethod*>(i, art::kRuntimePointerSize) != expected) {
        // We actually have some new obsolete methods. Just abort since we cannot safely shrink the
        // obsolete methods array.
        return;
      }
    }
    // No new obsolete methods! We can get rid of the maps.
    ext->SetObsoleteArrays(cur_data->GetOldObsoleteMethods(), cur_data->GetOldDexCaches());
  }
}

// This function does all (java) allocations we need to do for the Class being redefined.
// TODO Change this name maybe?
bool Redefiner::ClassRedefinition::EnsureClassAllocationsFinished(
    /*out*/RedefinitionDataIter* cur_data) {
  art::StackHandleScope<2> hs(driver_->self_);
  art::Handle<art::mirror::Class> klass(hs.NewHandle(
      driver_->self_->DecodeJObject(klass_)->AsClass()));
  if (klass == nullptr) {
    RecordFailure(ERR(INVALID_CLASS), "Unable to decode class argument!");
    return false;
  }
  // Allocate the classExt
  art::Handle<art::mirror::ClassExt> ext =
      hs.NewHandle(art::mirror::Class::EnsureExtDataPresent(klass, driver_->self_));
  if (ext == nullptr) {
    // No memory. Clear exception (it's not useful) and return error.
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Could not allocate ClassExt");
    return false;
  }
  if (!cur_data->IsActuallyStructural()) {
    CHECK(!IsStructuralRedefinition());
    // First save the old values of the 2 arrays that make up the obsolete methods maps. Then
    // allocate the 2 arrays that make up the obsolete methods map. Since the contents of the arrays
    // are only modified when all threads (other than the modifying one) are suspended we don't need
    // to worry about missing the unsyncronized writes to the array. We do synchronize when setting
    // it however, since that can happen at any time.
    cur_data->SetOldObsoleteMethods(ext->GetObsoleteMethods());
    cur_data->SetOldDexCaches(ext->GetObsoleteDexCaches());
    if (!art::mirror::ClassExt::ExtendObsoleteArrays(
            ext, driver_->self_, klass->GetDeclaredMethodsSlice(art::kRuntimePointerSize).size())) {
      // OOM. Clear exception and return error.
      driver_->self_->AssertPendingOOMException();
      driver_->self_->ClearException();
      RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate/extend obsolete methods map");
      return false;
    }
  }
  return true;
}

}  // namespace openjdkjvmti
