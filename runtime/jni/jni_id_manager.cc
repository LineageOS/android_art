/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "jni_id_manager.h"

#include <algorithm>
#include <cstdint>
#include <type_traits>

#include "android-base/macros.h"
#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/enums.h"
#include "base/globals.h"
#include "base/locks.h"
#include "base/mutex.h"
#include "class_root-inl.h"
#include "gc/allocation_listener.h"
#include "gc/heap.h"
#include "jni/jni_internal.h"
#include "jni_id_type.h"
#include "mirror/array-inl.h"
#include "mirror/array.h"
#include "mirror/class-alloc-inl.h"
#include "mirror/class-inl.h"
#include "mirror/class.h"
#include "mirror/class_ext-inl.h"
#include "mirror/object-inl.h"
#include "obj_ptr-inl.h"
#include "reflective_handle_scope-inl.h"
#include "reflective_handle_scope.h"
#include "reflective_value_visitor.h"
#include "thread-inl.h"
#include "thread.h"

namespace art {
namespace jni {

constexpr bool kTraceIds = false;

// TODO This whole thing could be done lock & wait free (since we never remove anything from the
// ids list). It's not clear this would be worthwile though.

namespace {

static constexpr size_t IdToIndex(uintptr_t id) {
  return id >> 1;
}

static constexpr uintptr_t IndexToId(size_t index) {
  return (index << 1) + 1;
}

template <typename ArtType>
ObjPtr<mirror::PointerArray> GetIds(ObjPtr<mirror::Class> k, ArtType* t)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ObjPtr<mirror::Object> ret;
  if constexpr (std::is_same_v<ArtType, ArtField>) {
    ret = t->IsStatic() ? k->GetStaticFieldIds() : k->GetInstanceFieldIds();
  } else {
    ret = t->IsObsolete() ? nullptr : k->GetMethodIds();
  }
  DCHECK(ret.IsNull() || ret->IsArrayInstance()) << "Should have bailed out early!";
  if (kIsDebugBuild && !ret.IsNull()) {
    if (kRuntimePointerSize == PointerSize::k32) {
      CHECK(ret->IsIntArray());
    } else {
      CHECK(ret->IsLongArray());
    }
  }
  return down_cast<mirror::PointerArray*>(ret.Ptr());
}

template <typename ArtType>
bool ShouldReturnPointer(ObjPtr<mirror::Class> klass, ArtType* t)
    REQUIRES_SHARED(Locks::mutator_lock_);

template <>
bool ShouldReturnPointer(ObjPtr<mirror::Class> klass, ArtMethod* t ATTRIBUTE_UNUSED) {
  ObjPtr<mirror::ClassExt> ext(klass->GetExtData());
  if (ext.IsNull()) {
    return true;
  }
  ObjPtr<mirror::Object> arr = ext->GetJMethodIDs();
  return arr.IsNull() || !arr->IsArrayInstance();
}

template<>
bool ShouldReturnPointer(ObjPtr<mirror::Class> klass, ArtField* t) {
  ObjPtr<mirror::ClassExt> ext(klass->GetExtData());
  if (ext.IsNull()) {
    return true;
  }
  ObjPtr<mirror::Object> arr = t->IsStatic() ? ext->GetStaticJFieldIDs()
                                             : ext->GetInstanceJFieldIDs();
  return arr.IsNull() || !arr->IsArrayInstance();
}


// Forces the appropriate id array to be present if possible. Returns true if allocation was
// attempted but failed.
template <typename ArtType>
bool EnsureIdsArray(Thread* self, ObjPtr<mirror::Class> k, ArtType* t)
    REQUIRES_SHARED(Locks::mutator_lock_);

template <>
bool EnsureIdsArray(Thread* self, ObjPtr<mirror::Class> k, ArtField* field) {
  ScopedExceptionStorage ses(self);
  StackHandleScope<1> hs(self);
  Handle<mirror::Class> h_k(hs.NewHandle(k));
  if (Locks::mutator_lock_->IsExclusiveHeld(self)) {
    return false;
  } else {
    // NB This modifies the class to allocate the ClassExt and the ids array.
    field->IsStatic() ? mirror::Class::EnsureStaticFieldIds(h_k)
                      : mirror::Class::EnsureInstanceFieldIds(h_k);
  }
  if (self->IsExceptionPending()) {
    self->AssertPendingOOMException();
    ses.SuppressOldException("Failed to allocate maps for jmethodIDs. ");
    return true;
  }
  return false;
}

template <>
bool EnsureIdsArray(Thread* self, ObjPtr<mirror::Class> k, ArtMethod* method) {
  if (method->IsObsolete()) {
    if (kTraceIds) {
      LOG(INFO) << "jmethodID for Obsolete method " << method->PrettyMethod() << " requested!";
    }
    // No ids array for obsolete methods. Just do a linear scan.
    return false;
  }
  StackHandleScope<1> hs(self);
  Handle<mirror::Class> h_k(hs.NewHandle(k));
  if (Locks::mutator_lock_->IsExclusiveHeld(self) || !Locks::mutator_lock_->IsSharedHeld(self)) {
    return false;
  } else {
    // NB This modifies the class to allocate the ClassExt and the ids array.
    mirror::Class::EnsureMethodIds(h_k);
  }
  if (self->IsExceptionPending()) {
    self->AssertPendingOOMException();
    return true;
  }
  return false;
}

template <typename ArtType>
size_t GetIdOffset(ObjPtr<mirror::Class> k, ArtType* t, PointerSize pointer_size)
    REQUIRES_SHARED(Locks::mutator_lock_);
template <>
size_t GetIdOffset(ObjPtr<mirror::Class> k, ArtField* f, PointerSize ptr_size ATTRIBUTE_UNUSED) {
  return f->IsStatic() ? k->GetStaticFieldIdOffset(f) : k->GetInstanceFieldIdOffset(f);
}
template <>
size_t GetIdOffset(ObjPtr<mirror::Class> k, ArtMethod* method, PointerSize pointer_size) {
  return method->IsObsolete() ? -1 : k->GetMethodIdOffset(method, pointer_size);
}

// Calls the relevant PrettyMethod/PrettyField on the input.
template <typename ArtType>
std::string PrettyGeneric(ArtType t) REQUIRES_SHARED(Locks::mutator_lock_);
template <>
std::string PrettyGeneric(ArtMethod* f) {
  return f->PrettyMethod();
}
template <>
std::string PrettyGeneric(ReflectiveHandle<ArtMethod> f) {
  return f->PrettyMethod();
}
template <>
std::string PrettyGeneric(ArtField* f) {
  return f->PrettyField();
}
template <>
std::string PrettyGeneric(ReflectiveHandle<ArtField> f) {
  return f->PrettyField();
}

// Checks if the field or method is obsolete.
template <typename ArtType>
bool IsObsolete(ReflectiveHandle<ArtType> t) REQUIRES_SHARED(Locks::mutator_lock_);
template <>
bool IsObsolete(ReflectiveHandle<ArtField> t ATTRIBUTE_UNUSED) {
  return false;
}
template <>
bool IsObsolete(ReflectiveHandle<ArtMethod> t) {
  return t->IsObsolete();
}

// Get the canonical (non-copied) version of the field or method. Only relevant for methods.
template <typename ArtType>
ArtType* Canonicalize(ReflectiveHandle<ArtType> t) REQUIRES_SHARED(Locks::mutator_lock_);
template <>
ArtField* Canonicalize(ReflectiveHandle<ArtField> t) {
  return t.Get();
}
template <>
ArtMethod* Canonicalize(ReflectiveHandle<ArtMethod> t) {
  if (UNLIKELY(t->IsCopied())) {
    return t->GetCanonicalMethod();
  }
  return t.Get();
}

};  // namespace

// We increment the id by 2 each time to allow us to use the LSB as a flag that the ID is an index
// and not a pointer. This gives us 2**31 unique methods that can be addressed on 32-bit art, which
// should be more than enough.
template <>
uintptr_t JniIdManager::GetNextId<ArtField>(JniIdType type) {
  DCHECK_EQ(type, JniIdType::kIndices);
  uintptr_t res = next_field_id_;
  next_field_id_ += 2;
  CHECK_GT(next_field_id_, res) << "jfieldID Overflow";
  return res;
}

template <>
uintptr_t JniIdManager::GetNextId<ArtMethod>(JniIdType type) {
  DCHECK_EQ(type, JniIdType::kIndices);
  uintptr_t res = next_method_id_;
  next_method_id_ += 2;
  CHECK_GT(next_method_id_, res) << "jmethodID Overflow";
  return res;
}
template <>
std::vector<ArtField*>& JniIdManager::GetGenericMap<ArtField>() {
  return field_id_map_;
}

template <>
std::vector<ArtMethod*>& JniIdManager::GetGenericMap<ArtMethod>() {
  return method_id_map_;
}
template <>
size_t JniIdManager::GetLinearSearchStartId<ArtField>(
    ReflectiveHandle<ArtField> t ATTRIBUTE_UNUSED) {
  return deferred_allocation_field_id_start_;
}

template <>
size_t JniIdManager::GetLinearSearchStartId<ArtMethod>(ReflectiveHandle<ArtMethod> m) {
  if (m->IsObsolete()) {
    return 1;
  } else {
    return deferred_allocation_method_id_start_;
  }
}

// TODO need to fix races in here with visitors
template <typename ArtType>
uintptr_t JniIdManager::EncodeGenericId(ReflectiveHandle<ArtType> t) {
  static_assert(std::is_same_v<ArtType, ArtField> || std::is_same_v<ArtType, ArtMethod>,
                "Expected ArtField or ArtMethod");
  Runtime* runtime = Runtime::Current();
  JniIdType id_type = runtime->GetJniIdType();
  if (id_type == JniIdType::kPointer || t == nullptr) {
    return reinterpret_cast<uintptr_t>(t.Get());
  }
  Thread* self = Thread::Current();
  ScopedExceptionStorage ses(self);
  DCHECK(!t->GetDeclaringClass().IsNull()) << "Null declaring class " << PrettyGeneric(t);
  size_t off = GetIdOffset(t->GetDeclaringClass(), Canonicalize(t), kRuntimePointerSize);
  // Here is the earliest point we can suspend.
  bool allocation_failure = EnsureIdsArray(self, t->GetDeclaringClass(), t.Get());
  if (allocation_failure) {
    self->AssertPendingOOMException();
    ses.SuppressOldException("OOM exception while trying to allocate JNI ids.");
    return 0u;
  } else if (ShouldReturnPointer(t->GetDeclaringClass(), t.Get())) {
    return reinterpret_cast<uintptr_t>(t.Get());
  }
  ObjPtr<mirror::Class> klass = t->GetDeclaringClass();
  ObjPtr<mirror::PointerArray> ids(GetIds(klass, t.Get()));
  uintptr_t cur_id = 0;
  if (!ids.IsNull()) {
    DCHECK_GT(ids->GetLength(), static_cast<int32_t>(off)) << " is " << PrettyGeneric(t);
    DCHECK_LE(0, static_cast<int32_t>(off)) << " is " << PrettyGeneric(t);
    cur_id = ids->GetElementPtrSize<uintptr_t>(off, kRuntimePointerSize);
  }
  if (cur_id != 0) {
    return cur_id;
  }
  WriterMutexLock mu(self, *Locks::jni_id_lock_);
  ScopedAssertNoThreadSuspension sants("EncodeJniId critical section.");
  // Check the ids array for a racing id.
  constexpr std::pair<size_t, size_t> counts {
    std::is_same_v<ArtType, ArtField> ? 1 : 0,
    std::is_same_v<ArtType, ArtField> ? 0 : 1,
  };
  StackReflectiveHandleScope<counts.first, counts.second> hs(self);
  t = hs.NewHandle(Canonicalize(t));
  if (!ids.IsNull()) {
    // It's possible we got suspended and structurally redefined during the EnsureIdsArray. We need
    // to get the information again.
    ids = GetIds(klass, t.Get());
    off = GetIdOffset(klass, Canonicalize(t), kRuntimePointerSize);
    CHECK(!ids.IsNull());
    cur_id = ids->GetElementPtrSize<uintptr_t>(off, kRuntimePointerSize);
    if (cur_id != 0) {
      // We were racing some other thread and lost.
      return cur_id;
    }
  } else {
    // We cannot allocate anything here or don't have an ids array (we might be an obsolete method).
    DCHECK(IsObsolete(t) || deferred_allocation_refcount_ > 0u)
        << "deferred_allocation_refcount_: " << deferred_allocation_refcount_
        << " t: " << PrettyGeneric(t);
    // Check to see if we raced and lost to another thread.
    const std::vector<ArtType*>& vec = GetGenericMap<ArtType>();
    bool found = false;
    // simple count-while.
    size_t search_start_index = IdToIndex(GetLinearSearchStartId(t));
    size_t index = std::count_if(vec.cbegin() + search_start_index,
                                 vec.cend(),
                                 [&found, &self, t](const ArtType* candidate) {
                                   Locks::mutator_lock_->AssertSharedHeld(self);
                                   found = found || candidate == t.Get();
                                   return !found;
                                 }) +
                   search_start_index;
    if (found) {
      // We were either racing some other thread and lost or this thread was asked to encode the
      // same method multiple times while holding the mutator lock.
      DCHECK_EQ(vec[index], t.Get())
          << "Expected: " << PrettyGeneric(vec[index]) << " got " << PrettyGeneric(t)
          << " at index " << index << " (id: " << IndexToId(index) << ").";
      return IndexToId(index);
    }
  }
  cur_id = GetNextId<ArtType>(id_type);
  DCHECK_EQ(cur_id % 2, 1u);
  size_t cur_index = IdToIndex(cur_id);
  std::vector<ArtType*>& vec = GetGenericMap<ArtType>();
  vec.reserve(cur_index + 1);
  vec.resize(std::max(vec.size(), cur_index + 1), nullptr);
  vec[cur_index] = t.Get();
  if (ids.IsNull()) {
    if (kIsDebugBuild && !IsObsolete(t)) {
      CHECK_NE(deferred_allocation_refcount_, 0u)
          << "Failed to allocate ids array despite not being forbidden from doing so!";
      Locks::mutator_lock_->AssertExclusiveHeld(self);
    }
  } else {
    ids->SetElementPtrSize(off, reinterpret_cast<void*>(cur_id), kRuntimePointerSize);
  }
  return cur_id;
}

jfieldID JniIdManager::EncodeFieldId(ArtField* field) {
  StackArtFieldHandleScope<1> rhs(Thread::Current());
  return EncodeFieldId(rhs.NewHandle(field));
}

jfieldID JniIdManager::EncodeFieldId(ReflectiveHandle<ArtField> field) {
  auto* res = reinterpret_cast<jfieldID>(EncodeGenericId(field));
  if (kTraceIds && field != nullptr) {
    LOG(INFO) << "Returning " << res << " for field " << field->PrettyField();
  }
  return res;
}

jmethodID JniIdManager::EncodeMethodId(ArtMethod* method) {
  StackArtMethodHandleScope<1> rhs(Thread::Current());
  return EncodeMethodId(rhs.NewHandle(method));
}

jmethodID JniIdManager::EncodeMethodId(ReflectiveHandle<ArtMethod> method) {
  auto* res = reinterpret_cast<jmethodID>(EncodeGenericId(method));
  if (kTraceIds && method != nullptr) {
    LOG(INFO) << "Returning " << res << " for method " << method->PrettyMethod();
  }
  return res;
}

void JniIdManager::VisitRoots(RootVisitor *visitor) {
  pointer_marker_.VisitRootIfNonNull(visitor, RootInfo(kRootVMInternal));
}

void JniIdManager::Init(Thread* self) {
  // When compiling we don't want to have anything to do with any of this, which is fine since JNI
  // ids won't be created during AOT compilation. This also means we don't need to do any
  // complicated stuff with the image-writer.
  if (!Runtime::Current()->IsAotCompiler()) {
    // Allocate the marker
    StackHandleScope<3> hs(self);
    Handle<mirror::Object> marker_obj(
        hs.NewHandle(GetClassRoot<mirror::Object>()->AllocObject(self)));
    CHECK(!marker_obj.IsNull());
    pointer_marker_ = GcRoot<mirror::Object>(marker_obj.Get());
    // Manually mark class-ext as having all pointer-ids to avoid any annoying loops.
    Handle<mirror::Class> class_ext_class(hs.NewHandle(GetClassRoot<mirror::ClassExt>()));
    mirror::Class::EnsureExtDataPresent(class_ext_class, self);
    Handle<mirror::ClassExt> class_ext_ext(hs.NewHandle(class_ext_class->GetExtData()));
    class_ext_ext->SetIdsArraysForClassExtExtData(marker_obj.Get());
  }
}

void JniIdManager::VisitReflectiveTargets(ReflectiveValueVisitor* rvv) {
  art::WriterMutexLock mu(Thread::Current(), *Locks::jni_id_lock_);
  for (auto it = field_id_map_.begin(); it != field_id_map_.end(); ++it) {
    ArtField* old_field = *it;
    uintptr_t id = IndexToId(std::distance(field_id_map_.begin(), it));
    ArtField* new_field =
        rvv->VisitField(old_field, JniIdReflectiveSourceInfo(reinterpret_cast<jfieldID>(id)));
    if (old_field != new_field) {
      *it = new_field;
      ObjPtr<mirror::Class> old_class(old_field->GetDeclaringClass());
      ObjPtr<mirror::Class> new_class(new_field->GetDeclaringClass());
      ObjPtr<mirror::ClassExt> old_ext_data(old_class->GetExtData());
      ObjPtr<mirror::ClassExt> new_ext_data(new_class->GetExtData());
      if (!old_ext_data.IsNull()) {
        CHECK(!old_ext_data->HasInstanceFieldPointerIdMarker() &&
              !old_ext_data->HasStaticFieldPointerIdMarker())
            << old_class->PrettyClass();
        // Clear the old field mapping.
        if (old_field->IsStatic()) {
          size_t old_off = ArraySlice<ArtField>(old_class->GetSFieldsPtr()).OffsetOf(old_field);
          ObjPtr<mirror::PointerArray> old_statics(old_ext_data->GetStaticJFieldIDsPointerArray());
          if (!old_statics.IsNull()) {
            old_statics->SetElementPtrSize(old_off, 0, kRuntimePointerSize);
          }
        } else {
          size_t old_off = ArraySlice<ArtField>(old_class->GetIFieldsPtr()).OffsetOf(old_field);
          ObjPtr<mirror::PointerArray> old_instances(
              old_ext_data->GetInstanceJFieldIDsPointerArray());
          if (!old_instances.IsNull()) {
            old_instances->SetElementPtrSize(old_off, 0, kRuntimePointerSize);
          }
        }
      }
      if (!new_ext_data.IsNull()) {
        CHECK(!new_ext_data->HasInstanceFieldPointerIdMarker() &&
              !new_ext_data->HasStaticFieldPointerIdMarker())
            << new_class->PrettyClass();
        // Set the new field mapping.
        if (new_field->IsStatic()) {
          size_t new_off = ArraySlice<ArtField>(new_class->GetSFieldsPtr()).OffsetOf(new_field);
          ObjPtr<mirror::PointerArray> new_statics(new_ext_data->GetStaticJFieldIDsPointerArray());
          if (!new_statics.IsNull()) {
            new_statics->SetElementPtrSize(new_off, id, kRuntimePointerSize);
          }
        } else {
          size_t new_off = ArraySlice<ArtField>(new_class->GetIFieldsPtr()).OffsetOf(new_field);
          ObjPtr<mirror::PointerArray> new_instances(
              new_ext_data->GetInstanceJFieldIDsPointerArray());
          if (!new_instances.IsNull()) {
            new_instances->SetElementPtrSize(new_off, id, kRuntimePointerSize);
          }
        }
      }
    }
  }
  for (auto it = method_id_map_.begin(); it != method_id_map_.end(); ++it) {
    ArtMethod* old_method = *it;
    uintptr_t id = IndexToId(std::distance(method_id_map_.begin(), it));
    ArtMethod* new_method =
        rvv->VisitMethod(old_method, JniIdReflectiveSourceInfo(reinterpret_cast<jmethodID>(id)));
    if (old_method != new_method) {
      *it = new_method;
      ObjPtr<mirror::Class> old_class(old_method->GetDeclaringClass());
      ObjPtr<mirror::Class> new_class(new_method->GetDeclaringClass());
      ObjPtr<mirror::ClassExt> old_ext_data(old_class->GetExtData());
      ObjPtr<mirror::ClassExt> new_ext_data(new_class->GetExtData());
      if (!old_ext_data.IsNull()) {
        CHECK(!old_ext_data->HasMethodPointerIdMarker()) << old_class->PrettyClass();
        // Clear the old method mapping.
        size_t old_off = ArraySlice<ArtMethod>(old_class->GetMethodsPtr()).OffsetOf(old_method);
        ObjPtr<mirror::PointerArray> old_methods(old_ext_data->GetJMethodIDsPointerArray());
        if (!old_methods.IsNull()) {
          old_methods->SetElementPtrSize(old_off, 0, kRuntimePointerSize);
        }
      }
      if (!new_ext_data.IsNull()) {
        CHECK(!new_ext_data->HasMethodPointerIdMarker()) << new_class->PrettyClass();
        // Set the new method mapping.
        size_t new_off = ArraySlice<ArtMethod>(new_class->GetMethodsPtr()).OffsetOf(new_method);
        ObjPtr<mirror::PointerArray> new_methods(new_ext_data->GetJMethodIDsPointerArray());
        if (!new_methods.IsNull()) {
          new_methods->SetElementPtrSize(new_off, id, kRuntimePointerSize);
        }
      }
    }
  }
}

template <typename ArtType> ArtType* JniIdManager::DecodeGenericId(uintptr_t t) {
  if (Runtime::Current()->GetJniIdType() == JniIdType::kIndices && (t % 2) == 1) {
    ReaderMutexLock mu(Thread::Current(), *Locks::jni_id_lock_);
    size_t index = IdToIndex(t);
    DCHECK_GT(GetGenericMap<ArtType>().size(), index);
    return GetGenericMap<ArtType>().at(index);
  } else {
    DCHECK_EQ((t % 2), 0u) << "id: " << t;
    return reinterpret_cast<ArtType*>(t);
  }
}

ArtMethod* JniIdManager::DecodeMethodId(jmethodID method) {
  return DecodeGenericId<ArtMethod>(reinterpret_cast<uintptr_t>(method));
}

ArtField* JniIdManager::DecodeFieldId(jfieldID field) {
  return DecodeGenericId<ArtField>(reinterpret_cast<uintptr_t>(field));
}

ObjPtr<mirror::Object> JniIdManager::GetPointerMarker() {
  return pointer_marker_.Read();
}

// This whole defer system is an annoying requirement to allow us to generate IDs during heap-walks
// such as those required for instrumentation tooling.
//
// The defer system works with the normal id-assignment routine to ensure that all the class-ext
// data structures are eventually created and filled in. Basically how it works is the id-assignment
// function will check to see if it has a strong mutator-lock. If it does not then it will try to
// allocate the class-ext data structures normally and fail if it is unable to do so. In the case
// where mutator-lock is being held exclusive no attempt to allocate will be made and the thread
// will CHECK that allocations are being deferred (or that the method is obsolete, in which case
// there is no class-ext to store the method->id map in).
//
// Once the thread is done holding the exclusive mutator-lock it will go back and fill-in the
// class-ext data of all the methods that were added. We do this without the exclusive mutator-lock
// on a copy of the maps before we decrement the deferred refcount. This ensures that any other
// threads running at the same time know they need to perform a linear scan of the id-map. Since we
// don't have the mutator-lock anymore other threads can allocate the class-ext data, meaning our
// copy is fine. The only way additional methods could end up on the id-maps after our copy without
// having class-ext data is if another thread picked up the exclusive mutator-lock and added another
// defer, in which case that thread would fix-up the remaining ids. In this way we maintain eventual
// consistency between the class-ext method/field->id maps and the JniIdManager id->method/field
// maps.
//
// TODO It is possible that another thread to gain the mutator-lock and allocate new ids without
// calling StartDefer. This is basically a race that we should try to catch but doing so is
// rather difficult and since this defer system is only used in very rare circumstances unlikely to
// be worth the trouble.
void JniIdManager::StartDefer() {
  Thread* self = Thread::Current();
  WriterMutexLock mu(self, *Locks::jni_id_lock_);
  if (deferred_allocation_refcount_++ == 0) {
    deferred_allocation_field_id_start_ = next_field_id_;
    deferred_allocation_method_id_start_ = next_method_id_;
  }
}

class JniIdDeferStackReflectiveScope : public BaseReflectiveHandleScope {
 public:
  JniIdDeferStackReflectiveScope() REQUIRES_SHARED(art::Locks::mutator_lock_)
      : BaseReflectiveHandleScope(), methods_(), fields_() {
    PushScope(Thread::Current());
  }

  void Initialize(const std::vector<ArtMethod*>& methods, const std::vector<ArtField*>& fields)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Roles::uninterruptible_) {
    methods_ = methods;
    fields_ = fields;
  }

  ~JniIdDeferStackReflectiveScope() REQUIRES_SHARED(Locks::mutator_lock_) {
    PopScope();
  }

  void VisitTargets(ReflectiveValueVisitor* visitor) override
      REQUIRES_SHARED(Locks::mutator_lock_) {
    for (auto it = methods_.begin(); it != methods_.end(); ++it) {
      if (*it == nullptr) {
        continue;
      }
      *it = visitor->VisitMethod(*it, ReflectiveHandleScopeSourceInfo(this));
    }
    for (auto it = fields_.begin(); it != fields_.end(); ++it) {
      if (*it == nullptr) {
        continue;
      }
      *it = visitor->VisitField(*it, ReflectiveHandleScopeSourceInfo(this));
    }
  }

  ArtField** GetFieldPtr(size_t idx) REQUIRES_SHARED(Locks::mutator_lock_) {
    return &fields_[idx];
  }

  ArtMethod** GetMethodPtr(size_t idx) REQUIRES_SHARED(Locks::mutator_lock_) {
    return &methods_[idx];
  }

  size_t NumFields() const {
    return fields_.size();
  }
  size_t NumMethods() const {
    return methods_.size();
  }

 private:
  std::vector<ArtMethod*> methods_;
  std::vector<ArtField*> fields_;
};

void JniIdManager::EndDefer() {
  // Fixup the method->id map.
  Thread* self = Thread::Current();
  auto set_id = [&](auto** t, uintptr_t id) REQUIRES_SHARED(Locks::mutator_lock_) {
    if (t == nullptr) {
      return;
    }
    bool alloc_failure = EnsureIdsArray(self, (*t)->GetDeclaringClass(), *t);
    ObjPtr<mirror::Class> klass((*t)->GetDeclaringClass());
    size_t off = GetIdOffset(klass, (*t), kRuntimePointerSize);
    ObjPtr<mirror::PointerArray> ids = GetIds(klass, (*t));
    CHECK(!alloc_failure) << "Could not allocate jni ids array!";
    if (ids.IsNull()) {
      return;
    }
    if (kIsDebugBuild) {
      uintptr_t old_id = ids->GetElementPtrSize<uintptr_t, kRuntimePointerSize>(off);
      if (old_id != 0) {
        DCHECK_EQ(old_id, id);
      }
    }
    ids->SetElementPtrSize(off, reinterpret_cast<void*>(id), kRuntimePointerSize);
  };
  // To ensure eventual consistency this depends on the fact that the method_id_map_ and
  // field_id_map_ are the ultimate source of truth and no id is ever reused to be valid. It also
  // relies on all threads always getting calling StartDefer if they are going to be allocating jni
  // ids while suspended. If a thread tries to do so while it doesn't have a scope we could miss
  // ids.
  // TODO We should use roles or something to verify that this requirement is not broken.
  //
  // If another thread comes along and adds more methods to the list after
  // copying either (1) the id-maps are already present for the method and everything is fine, (2)
  // the thread is not suspended and so can create the ext-data and id lists or, (3) the thread also
  // suspended everything and incremented the deferred_allocation_refcount_ so it will fix up new
  // ids when it finishes.
  Locks::mutator_lock_->AssertNotExclusiveHeld(self);
  Locks::mutator_lock_->AssertSharedHeld(self);
  JniIdDeferStackReflectiveScope jidsrs;
  uintptr_t method_start_id;
  uintptr_t field_start_id;
  {
    ReaderMutexLock mu(self, *Locks::jni_id_lock_);
    ScopedAssertNoThreadSuspension sants(__FUNCTION__);
    jidsrs.Initialize(method_id_map_, field_id_map_);
    method_start_id = deferred_allocation_method_id_start_;
    field_start_id = deferred_allocation_field_id_start_;
  }

  for (size_t index = kIsDebugBuild ? 0 : IdToIndex(method_start_id); index < jidsrs.NumMethods();
       ++index) {
    set_id(jidsrs.GetMethodPtr(index), IndexToId(index));
  }
  for (size_t index = kIsDebugBuild ? 0 : IdToIndex(field_start_id); index < jidsrs.NumFields();
       ++index) {
    set_id(jidsrs.GetFieldPtr(index), IndexToId(index));
  }
  WriterMutexLock mu(self, *Locks::jni_id_lock_);
  DCHECK_GE(deferred_allocation_refcount_, 1u);
  if (--deferred_allocation_refcount_ == 0) {
    deferred_allocation_field_id_start_ = 0;
    deferred_allocation_method_id_start_ = 0;
  }
}

ScopedEnableSuspendAllJniIdQueries::ScopedEnableSuspendAllJniIdQueries()
    : manager_(Runtime::Current()->GetJniIdManager()) {
  manager_->StartDefer();
}

ScopedEnableSuspendAllJniIdQueries::~ScopedEnableSuspendAllJniIdQueries() {
  manager_->EndDefer();
}

};  // namespace jni
};  // namespace art
