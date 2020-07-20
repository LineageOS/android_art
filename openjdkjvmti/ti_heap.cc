/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "ti_heap.h"

#include <ios>
#include <unordered_map>

#include "android-base/logging.h"
#include "android-base/thread_annotations.h"
#include "arch/context.h"
#include "art_field-inl.h"
#include "art_jvmti.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "base/utils.h"
#include "class_linker.h"
#include "deopt_manager.h"
#include "dex/primitive.h"
#include "events-inl.h"
#include "gc/collector_type.h"
#include "gc/gc_cause.h"
#include "gc/heap-visit-objects-inl.h"
#include "gc/heap-inl.h"
#include "gc/scoped_gc_critical_section.h"
#include "gc_root-inl.h"
#include "handle.h"
#include "handle_scope.h"
#include "java_frame_root_info.h"
#include "jni/jni_env_ext.h"
#include "jni/jni_id_manager.h"
#include "jni/jni_internal.h"
#include "jvmti_weak_table-inl.h"
#include "mirror/array-inl.h"
#include "mirror/array.h"
#include "mirror/class.h"
#include "mirror/object-inl.h"
#include "mirror/object-refvisitor-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/object_array-alloc-inl.h"
#include "mirror/object_reference.h"
#include "obj_ptr-inl.h"
#include "object_callbacks.h"
#include "object_tagging.h"
#include "offsets.h"
#include "read_barrier.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "thread-inl.h"
#include "thread_list.h"
#include "ti_logging.h"
#include "ti_stack.h"
#include "ti_thread.h"
#include "well_known_classes.h"

namespace openjdkjvmti {

EventHandler* HeapExtensions::gEventHandler = nullptr;

namespace {

struct IndexCache {
  // The number of interface fields implemented by the class. This is a prefix to all assigned
  // field indices.
  size_t interface_fields;

  // It would be nice to also cache the following, but it is complicated to wire up into the
  // generic visit:
  // The number of fields in interfaces and superclasses. This is the first index assigned to
  // fields of the class.
  // size_t superclass_fields;
};
using IndexCachingTable = JvmtiWeakTable<IndexCache>;

static IndexCachingTable gIndexCachingTable;

// Report the contents of a string, if a callback is set.
jint ReportString(art::ObjPtr<art::mirror::Object> obj,
                  jvmtiEnv* env,
                  ObjectTagTable* tag_table,
                  const jvmtiHeapCallbacks* cb,
                  const void* user_data) REQUIRES_SHARED(art::Locks::mutator_lock_) {
  if (UNLIKELY(cb->string_primitive_value_callback != nullptr) && obj->IsString()) {
    art::ObjPtr<art::mirror::String> str = obj->AsString();
    int32_t string_length = str->GetLength();
    JvmtiUniquePtr<uint16_t[]> data;

    if (string_length > 0) {
      jvmtiError alloc_error;
      data = AllocJvmtiUniquePtr<uint16_t[]>(env, string_length, &alloc_error);
      if (data == nullptr) {
        // TODO: Not really sure what to do here. Should we abort the iteration and go all the way
        //       back? For now just warn.
        LOG(WARNING) << "Unable to allocate buffer for string reporting! Silently dropping value."
                     << " >" << str->ToModifiedUtf8() << "<";
        return 0;
      }

      if (str->IsCompressed()) {
        uint8_t* compressed_data = str->GetValueCompressed();
        for (int32_t i = 0; i != string_length; ++i) {
          data[i] = compressed_data[i];
        }
      } else {
        // Can copy directly.
        memcpy(data.get(), str->GetValue(), string_length * sizeof(uint16_t));
      }
    }

    const jlong class_tag = tag_table->GetTagOrZero(obj->GetClass());
    jlong string_tag = tag_table->GetTagOrZero(obj.Ptr());
    const jlong saved_string_tag = string_tag;

    jint result = cb->string_primitive_value_callback(class_tag,
                                                      obj->SizeOf(),
                                                      &string_tag,
                                                      data.get(),
                                                      string_length,
                                                      const_cast<void*>(user_data));
    if (string_tag != saved_string_tag) {
      tag_table->Set(obj.Ptr(), string_tag);
    }

    return result;
  }
  return 0;
}

// Report the contents of a primitive array, if a callback is set.
jint ReportPrimitiveArray(art::ObjPtr<art::mirror::Object> obj,
                          jvmtiEnv* env,
                          ObjectTagTable* tag_table,
                          const jvmtiHeapCallbacks* cb,
                          const void* user_data) REQUIRES_SHARED(art::Locks::mutator_lock_) {
  if (UNLIKELY(cb->array_primitive_value_callback != nullptr) &&
      obj->IsArrayInstance() &&
      !obj->IsObjectArray()) {
    art::ObjPtr<art::mirror::Array> array = obj->AsArray();
    int32_t array_length = array->GetLength();
    size_t component_size = array->GetClass()->GetComponentSize();
    art::Primitive::Type art_prim_type = array->GetClass()->GetComponentType()->GetPrimitiveType();
    jvmtiPrimitiveType prim_type =
        static_cast<jvmtiPrimitiveType>(art::Primitive::Descriptor(art_prim_type)[0]);
    DCHECK(prim_type == JVMTI_PRIMITIVE_TYPE_BOOLEAN ||
           prim_type == JVMTI_PRIMITIVE_TYPE_BYTE ||
           prim_type == JVMTI_PRIMITIVE_TYPE_CHAR ||
           prim_type == JVMTI_PRIMITIVE_TYPE_SHORT ||
           prim_type == JVMTI_PRIMITIVE_TYPE_INT ||
           prim_type == JVMTI_PRIMITIVE_TYPE_LONG ||
           prim_type == JVMTI_PRIMITIVE_TYPE_FLOAT ||
           prim_type == JVMTI_PRIMITIVE_TYPE_DOUBLE);

    const jlong class_tag = tag_table->GetTagOrZero(obj->GetClass());
    jlong array_tag = tag_table->GetTagOrZero(obj.Ptr());
    const jlong saved_array_tag = array_tag;

    jint result;
    if (array_length == 0) {
      result = cb->array_primitive_value_callback(class_tag,
                                                  obj->SizeOf(),
                                                  &array_tag,
                                                  0,
                                                  prim_type,
                                                  nullptr,
                                                  const_cast<void*>(user_data));
    } else {
      jvmtiError alloc_error;
      JvmtiUniquePtr<char[]> data = AllocJvmtiUniquePtr<char[]>(env,
                                                                array_length * component_size,
                                                                &alloc_error);
      if (data == nullptr) {
        // TODO: Not really sure what to do here. Should we abort the iteration and go all the way
        //       back? For now just warn.
        LOG(WARNING) << "Unable to allocate buffer for array reporting! Silently dropping value.";
        return 0;
      }

      memcpy(data.get(), array->GetRawData(component_size, 0), array_length * component_size);

      result = cb->array_primitive_value_callback(class_tag,
                                                  obj->SizeOf(),
                                                  &array_tag,
                                                  array_length,
                                                  prim_type,
                                                  data.get(),
                                                  const_cast<void*>(user_data));
    }

    if (array_tag != saved_array_tag) {
      tag_table->Set(obj.Ptr(), array_tag);
    }

    return result;
  }
  return 0;
}

template <typename UserData>
bool VisitorFalse(art::ObjPtr<art::mirror::Object> obj ATTRIBUTE_UNUSED,
                  art::ObjPtr<art::mirror::Class> klass ATTRIBUTE_UNUSED,
                  art::ArtField& field ATTRIBUTE_UNUSED,
                  size_t field_index ATTRIBUTE_UNUSED,
                  UserData* user_data ATTRIBUTE_UNUSED) {
  return false;
}

template <typename UserData, bool kCallVisitorOnRecursion>
class FieldVisitor {
 public:
  // Report the contents of a primitive fields of the given object, if a callback is set.
  template <typename StaticPrimitiveVisitor,
            typename StaticReferenceVisitor,
            typename InstancePrimitiveVisitor,
            typename InstanceReferenceVisitor>
  static bool ReportFields(art::ObjPtr<art::mirror::Object> obj,
                           UserData* user_data,
                           StaticPrimitiveVisitor& static_prim_visitor,
                           StaticReferenceVisitor& static_ref_visitor,
                           InstancePrimitiveVisitor& instance_prim_visitor,
                           InstanceReferenceVisitor& instance_ref_visitor)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    FieldVisitor fv(user_data);

    if (obj->IsClass()) {
      // When visiting a class, we only visit the static fields of the given class. No field of
      // superclasses is visited.
      art::ObjPtr<art::mirror::Class> klass = obj->AsClass();
      // Only report fields on resolved classes. We need valid field data.
      if (!klass->IsResolved()) {
        return false;
      }
      return fv.ReportFieldsImpl(nullptr,
                                 obj->AsClass(),
                                 obj->AsClass()->IsInterface(),
                                 static_prim_visitor,
                                 static_ref_visitor,
                                 instance_prim_visitor,
                                 instance_ref_visitor);
    } else {
      // See comment above. Just double-checking here, but an instance *should* mean the class was
      // resolved.
      DCHECK(obj->GetClass()->IsResolved() || obj->GetClass()->IsErroneousResolved());
      return fv.ReportFieldsImpl(obj,
                                 obj->GetClass(),
                                 false,
                                 static_prim_visitor,
                                 static_ref_visitor,
                                 instance_prim_visitor,
                                 instance_ref_visitor);
    }
  }

 private:
  explicit FieldVisitor(UserData* user_data) : user_data_(user_data) {}

  // Report the contents of fields of the given object. If obj is null, report the static fields,
  // otherwise the instance fields.
  template <typename StaticPrimitiveVisitor,
            typename StaticReferenceVisitor,
            typename InstancePrimitiveVisitor,
            typename InstanceReferenceVisitor>
  bool ReportFieldsImpl(art::ObjPtr<art::mirror::Object> obj,
                        art::ObjPtr<art::mirror::Class> klass,
                        bool skip_java_lang_object,
                        StaticPrimitiveVisitor& static_prim_visitor,
                        StaticReferenceVisitor& static_ref_visitor,
                        InstancePrimitiveVisitor& instance_prim_visitor,
                        InstanceReferenceVisitor& instance_ref_visitor)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    // Compute the offset of field indices.
    size_t interface_field_count = CountInterfaceFields(klass);

    size_t tmp;
    bool aborted = ReportFieldsRecursive(obj,
                                         klass,
                                         interface_field_count,
                                         skip_java_lang_object,
                                         static_prim_visitor,
                                         static_ref_visitor,
                                         instance_prim_visitor,
                                         instance_ref_visitor,
                                         &tmp);
    return aborted;
  }

  // Visit primitive fields in an object (instance). Return true if the visit was aborted.
  template <typename StaticPrimitiveVisitor,
            typename StaticReferenceVisitor,
            typename InstancePrimitiveVisitor,
            typename InstanceReferenceVisitor>
  bool ReportFieldsRecursive(art::ObjPtr<art::mirror::Object> obj,
                             art::ObjPtr<art::mirror::Class> klass,
                             size_t interface_fields,
                             bool skip_java_lang_object,
                             StaticPrimitiveVisitor& static_prim_visitor,
                             StaticReferenceVisitor& static_ref_visitor,
                             InstancePrimitiveVisitor& instance_prim_visitor,
                             InstanceReferenceVisitor& instance_ref_visitor,
                             size_t* field_index_out)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    DCHECK(klass != nullptr);
    size_t field_index;
    if (klass->GetSuperClass() == nullptr) {
      // j.l.Object. Start with the fields from interfaces.
      field_index = interface_fields;
      if (skip_java_lang_object) {
        *field_index_out = field_index;
        return false;
      }
    } else {
      // Report superclass fields.
      if (kCallVisitorOnRecursion) {
        if (ReportFieldsRecursive(obj,
                                  klass->GetSuperClass(),
                                  interface_fields,
                                  skip_java_lang_object,
                                  static_prim_visitor,
                                  static_ref_visitor,
                                  instance_prim_visitor,
                                  instance_ref_visitor,
                                  &field_index)) {
          return true;
        }
      } else {
        // Still call, but with empty visitor. This is required for correct counting.
        ReportFieldsRecursive(obj,
                              klass->GetSuperClass(),
                              interface_fields,
                              skip_java_lang_object,
                              VisitorFalse<UserData>,
                              VisitorFalse<UserData>,
                              VisitorFalse<UserData>,
                              VisitorFalse<UserData>,
                              &field_index);
      }
    }

    // Now visit fields for the current klass.

    for (auto& static_field : klass->GetSFields()) {
      if (static_field.IsPrimitiveType()) {
        if (static_prim_visitor(obj,
                                klass,
                                static_field,
                                field_index,
                                user_data_)) {
          return true;
        }
      } else {
        if (static_ref_visitor(obj,
                               klass,
                               static_field,
                               field_index,
                               user_data_)) {
          return true;
        }
      }
      field_index++;
    }

    for (auto& instance_field : klass->GetIFields()) {
      if (instance_field.IsPrimitiveType()) {
        if (instance_prim_visitor(obj,
                                  klass,
                                  instance_field,
                                  field_index,
                                  user_data_)) {
          return true;
        }
      } else {
        if (instance_ref_visitor(obj,
                                 klass,
                                 instance_field,
                                 field_index,
                                 user_data_)) {
          return true;
        }
      }
      field_index++;
    }

    *field_index_out = field_index;
    return false;
  }

  // Implements a visit of the implemented interfaces of a given class.
  template <typename T>
  struct RecursiveInterfaceVisit {
    static void VisitStatic(art::Thread* self, art::ObjPtr<art::mirror::Class> klass, T& visitor)
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      RecursiveInterfaceVisit rv;
      rv.Visit(self, klass, visitor);
    }

    void Visit(art::Thread* self, art::ObjPtr<art::mirror::Class> klass, T& visitor)
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      // First visit the parent, to get the order right.
      // (We do this in preparation for actual visiting of interface fields.)
      if (klass->GetSuperClass() != nullptr) {
        Visit(self, klass->GetSuperClass(), visitor);
      }
      for (uint32_t i = 0; i != klass->NumDirectInterfaces(); ++i) {
        art::ObjPtr<art::mirror::Class> inf_klass =
            art::mirror::Class::GetDirectInterface(self, klass, i);
        DCHECK(inf_klass != nullptr);
        VisitInterface(self, inf_klass, visitor);
      }
    }

    void VisitInterface(art::Thread* self, art::ObjPtr<art::mirror::Class> inf_klass, T& visitor)
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      auto it = visited_interfaces.find(inf_klass.Ptr());
      if (it != visited_interfaces.end()) {
        return;
      }
      visited_interfaces.insert(inf_klass.Ptr());

      // Let the visitor know about this one. Note that this order is acceptable, as the ordering
      // of these fields never matters for known visitors.
      visitor(inf_klass);

      // Now visit the superinterfaces.
      for (uint32_t i = 0; i != inf_klass->NumDirectInterfaces(); ++i) {
        art::ObjPtr<art::mirror::Class> super_inf_klass =
            art::mirror::Class::GetDirectInterface(self, inf_klass, i);
        DCHECK(super_inf_klass != nullptr);
        VisitInterface(self, super_inf_klass, visitor);
      }
    }

    std::unordered_set<art::mirror::Class*> visited_interfaces;
  };

  // Counting interface fields. Note that we cannot use the interface table, as that only contains
  // "non-marker" interfaces (= interfaces with methods).
  static size_t CountInterfaceFields(art::ObjPtr<art::mirror::Class> klass)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    // Do we have a cached value?
    IndexCache tmp;
    if (gIndexCachingTable.GetTag(klass.Ptr(), &tmp)) {
      return tmp.interface_fields;
    }

    size_t count = 0;
    auto visitor = [&count](art::ObjPtr<art::mirror::Class> inf_klass)
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      DCHECK(inf_klass->IsInterface());
      DCHECK_EQ(0u, inf_klass->NumInstanceFields());
      count += inf_klass->NumStaticFields();
    };
    RecursiveInterfaceVisit<decltype(visitor)>::VisitStatic(art::Thread::Current(), klass, visitor);

    // Store this into the cache.
    tmp.interface_fields = count;
    gIndexCachingTable.Set(klass.Ptr(), tmp);

    return count;
  }

  UserData* user_data_;
};

// Debug helper. Prints the structure of an object.
template <bool kStatic, bool kRef>
struct DumpVisitor {
  static bool Callback(art::ObjPtr<art::mirror::Object> obj ATTRIBUTE_UNUSED,
                       art::ObjPtr<art::mirror::Class> klass ATTRIBUTE_UNUSED,
                       art::ArtField& field,
                       size_t field_index,
                       void* user_data ATTRIBUTE_UNUSED)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    LOG(ERROR) << (kStatic ? "static " : "instance ")
               << (kRef ? "ref " : "primitive ")
               << field.PrettyField()
               << " @ "
               << field_index;
    return false;
  }
};
ATTRIBUTE_UNUSED
void DumpObjectFields(art::ObjPtr<art::mirror::Object> obj)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  if (obj->IsClass()) {
    FieldVisitor<void, false>:: ReportFields(obj,
                                             nullptr,
                                             DumpVisitor<true, false>::Callback,
                                             DumpVisitor<true, true>::Callback,
                                             DumpVisitor<false, false>::Callback,
                                             DumpVisitor<false, true>::Callback);
  } else {
    FieldVisitor<void, true>::ReportFields(obj,
                                           nullptr,
                                           DumpVisitor<true, false>::Callback,
                                           DumpVisitor<true, true>::Callback,
                                           DumpVisitor<false, false>::Callback,
                                           DumpVisitor<false, true>::Callback);
  }
}

class ReportPrimitiveField {
 public:
  static bool Report(art::ObjPtr<art::mirror::Object> obj,
                     ObjectTagTable* tag_table,
                     const jvmtiHeapCallbacks* cb,
                     const void* user_data)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    if (UNLIKELY(cb->primitive_field_callback != nullptr)) {
      jlong class_tag = tag_table->GetTagOrZero(obj->GetClass());
      ReportPrimitiveField rpf(tag_table, class_tag, cb, user_data);
      if (obj->IsClass()) {
        return FieldVisitor<ReportPrimitiveField, false>::ReportFields(
            obj,
            &rpf,
            ReportPrimitiveFieldCallback<true>,
            VisitorFalse<ReportPrimitiveField>,
            VisitorFalse<ReportPrimitiveField>,
            VisitorFalse<ReportPrimitiveField>);
      } else {
        return FieldVisitor<ReportPrimitiveField, true>::ReportFields(
            obj,
            &rpf,
            VisitorFalse<ReportPrimitiveField>,
            VisitorFalse<ReportPrimitiveField>,
            ReportPrimitiveFieldCallback<false>,
            VisitorFalse<ReportPrimitiveField>);
      }
    }
    return false;
  }


 private:
  ReportPrimitiveField(ObjectTagTable* tag_table,
                       jlong class_tag,
                       const jvmtiHeapCallbacks* cb,
                       const void* user_data)
      : tag_table_(tag_table), class_tag_(class_tag), cb_(cb), user_data_(user_data) {}

  template <bool kReportStatic>
  static bool ReportPrimitiveFieldCallback(art::ObjPtr<art::mirror::Object> obj,
                                           art::ObjPtr<art::mirror::Class> klass,
                                           art::ArtField& field,
                                           size_t field_index,
                                           ReportPrimitiveField* user_data)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    art::Primitive::Type art_prim_type = field.GetTypeAsPrimitiveType();
    jvmtiPrimitiveType prim_type =
        static_cast<jvmtiPrimitiveType>(art::Primitive::Descriptor(art_prim_type)[0]);
    DCHECK(prim_type == JVMTI_PRIMITIVE_TYPE_BOOLEAN ||
           prim_type == JVMTI_PRIMITIVE_TYPE_BYTE ||
           prim_type == JVMTI_PRIMITIVE_TYPE_CHAR ||
           prim_type == JVMTI_PRIMITIVE_TYPE_SHORT ||
           prim_type == JVMTI_PRIMITIVE_TYPE_INT ||
           prim_type == JVMTI_PRIMITIVE_TYPE_LONG ||
           prim_type == JVMTI_PRIMITIVE_TYPE_FLOAT ||
           prim_type == JVMTI_PRIMITIVE_TYPE_DOUBLE);
    jvmtiHeapReferenceInfo info;
    info.field.index = field_index;

    jvalue value;
    memset(&value, 0, sizeof(jvalue));
    art::ObjPtr<art::mirror::Object> src = kReportStatic ? klass : obj;
    switch (art_prim_type) {
      case art::Primitive::Type::kPrimBoolean:
        value.z = field.GetBoolean(src) == 0 ? JNI_FALSE : JNI_TRUE;
        break;
      case art::Primitive::Type::kPrimByte:
        value.b = field.GetByte(src);
        break;
      case art::Primitive::Type::kPrimChar:
        value.c = field.GetChar(src);
        break;
      case art::Primitive::Type::kPrimShort:
        value.s = field.GetShort(src);
        break;
      case art::Primitive::Type::kPrimInt:
        value.i = field.GetInt(src);
        break;
      case art::Primitive::Type::kPrimLong:
        value.j = field.GetLong(src);
        break;
      case art::Primitive::Type::kPrimFloat:
        value.f = field.GetFloat(src);
        break;
      case art::Primitive::Type::kPrimDouble:
        value.d = field.GetDouble(src);
        break;
      case art::Primitive::Type::kPrimVoid:
      case art::Primitive::Type::kPrimNot: {
        LOG(FATAL) << "Should not reach here";
        UNREACHABLE();
      }
    }

    jlong obj_tag = user_data->tag_table_->GetTagOrZero(src.Ptr());
    const jlong saved_obj_tag = obj_tag;

    jint ret = user_data->cb_->primitive_field_callback(kReportStatic
                                                            ? JVMTI_HEAP_REFERENCE_STATIC_FIELD
                                                            : JVMTI_HEAP_REFERENCE_FIELD,
                                                        &info,
                                                        user_data->class_tag_,
                                                        &obj_tag,
                                                        value,
                                                        prim_type,
                                                        const_cast<void*>(user_data->user_data_));

    if (saved_obj_tag != obj_tag) {
      user_data->tag_table_->Set(src.Ptr(), obj_tag);
    }

    if ((ret & JVMTI_VISIT_ABORT) != 0) {
      return true;
    }

    return false;
  }

  ObjectTagTable* tag_table_;
  jlong class_tag_;
  const jvmtiHeapCallbacks* cb_;
  const void* user_data_;
};

struct HeapFilter {
  explicit HeapFilter(jint heap_filter)
      : filter_out_tagged((heap_filter & JVMTI_HEAP_FILTER_TAGGED) != 0),
        filter_out_untagged((heap_filter & JVMTI_HEAP_FILTER_UNTAGGED) != 0),
        filter_out_class_tagged((heap_filter & JVMTI_HEAP_FILTER_CLASS_TAGGED) != 0),
        filter_out_class_untagged((heap_filter & JVMTI_HEAP_FILTER_CLASS_UNTAGGED) != 0),
        any_filter(filter_out_tagged ||
                   filter_out_untagged ||
                   filter_out_class_tagged ||
                   filter_out_class_untagged) {
  }

  bool ShouldReportByHeapFilter(jlong tag, jlong class_tag) const {
    if (!any_filter) {
      return true;
    }

    if ((tag == 0 && filter_out_untagged) || (tag != 0 && filter_out_tagged)) {
      return false;
    }

    if ((class_tag == 0 && filter_out_class_untagged) ||
        (class_tag != 0 && filter_out_class_tagged)) {
      return false;
    }

    return true;
  }

  const bool filter_out_tagged;
  const bool filter_out_untagged;
  const bool filter_out_class_tagged;
  const bool filter_out_class_untagged;
  const bool any_filter;
};

}  // namespace

void HeapUtil::Register() {
  art::Runtime::Current()->AddSystemWeakHolder(&gIndexCachingTable);
}

void HeapUtil::Unregister() {
  art::Runtime::Current()->RemoveSystemWeakHolder(&gIndexCachingTable);
}

jvmtiError HeapUtil::IterateOverInstancesOfClass(jvmtiEnv* env,
                                                 jclass klass,
                                                 jvmtiHeapObjectFilter filter,
                                                 jvmtiHeapObjectCallback cb,
                                                 const void* user_data) {
  if (cb == nullptr || klass == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::Thread* self = art::Thread::Current();
  art::ScopedObjectAccess soa(self);      // Now we know we have the shared lock.
  art::StackHandleScope<1> hs(self);

  art::ObjPtr<art::mirror::Object> klass_ptr(soa.Decode<art::mirror::Class>(klass));
  if (!klass_ptr->IsClass()) {
    return ERR(INVALID_CLASS);
  }
  art::Handle<art::mirror::Class> filter_klass(hs.NewHandle(klass_ptr->AsClass()));
  ObjectTagTable* tag_table = ArtJvmTiEnv::AsArtJvmTiEnv(env)->object_tag_table.get();
  bool stop_reports = false;
  auto visitor = [&](art::mirror::Object* obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    // Early return, as we can't really stop visiting.
    if (stop_reports) {
      return;
    }

    art::ScopedAssertNoThreadSuspension no_suspension("IterateOverInstancesOfClass");

    art::ObjPtr<art::mirror::Class> klass = obj->GetClass();

    if (filter_klass != nullptr && !filter_klass->IsAssignableFrom(klass)) {
      return;
    }

    jlong tag = 0;
    tag_table->GetTag(obj, &tag);
    if ((filter != JVMTI_HEAP_OBJECT_EITHER) &&
        ((tag == 0 && filter == JVMTI_HEAP_OBJECT_TAGGED) ||
         (tag != 0 && filter == JVMTI_HEAP_OBJECT_UNTAGGED))) {
      return;
    }

    jlong class_tag = 0;
    tag_table->GetTag(klass.Ptr(), &class_tag);

    jlong saved_tag = tag;
    jint ret = cb(class_tag, obj->SizeOf(), &tag, const_cast<void*>(user_data));

    stop_reports = (ret == JVMTI_ITERATION_ABORT);

    if (tag != saved_tag) {
      tag_table->Set(obj, tag);
    }
  };
  art::Runtime::Current()->GetHeap()->VisitObjects(visitor);

  return OK;
}

template <typename T>
static jvmtiError DoIterateThroughHeap(T fn,
                                       jvmtiEnv* env,
                                       ObjectTagTable* tag_table,
                                       jint heap_filter_int,
                                       jclass klass,
                                       const jvmtiHeapCallbacks* callbacks,
                                       const void* user_data) {
  if (callbacks == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::Thread* self = art::Thread::Current();
  art::ScopedObjectAccess soa(self);      // Now we know we have the shared lock.

  bool stop_reports = false;
  const HeapFilter heap_filter(heap_filter_int);
  art::StackHandleScope<1> hs(self);
  art::Handle<art::mirror::Class> filter_klass(hs.NewHandle(soa.Decode<art::mirror::Class>(klass)));
  auto visitor = [&](art::mirror::Object* obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    // Early return, as we can't really stop visiting.
    if (stop_reports) {
      return;
    }

    art::ScopedAssertNoThreadSuspension no_suspension("IterateThroughHeapCallback");

    jlong tag = 0;
    tag_table->GetTag(obj, &tag);

    jlong class_tag = 0;
    art::ObjPtr<art::mirror::Class> klass = obj->GetClass();
    tag_table->GetTag(klass.Ptr(), &class_tag);
    // For simplicity, even if we find a tag = 0, assume 0 = not tagged.

    if (!heap_filter.ShouldReportByHeapFilter(tag, class_tag)) {
      return;
    }

    if (filter_klass != nullptr) {
      if (filter_klass.Get() != klass) {
        return;
      }
    }

    jlong size = obj->SizeOf();

    jint length = -1;
    if (obj->IsArrayInstance()) {
      length = obj->AsArray()->GetLength();
    }

    jlong saved_tag = tag;
    jint ret = fn(obj, callbacks, class_tag, size, &tag, length, const_cast<void*>(user_data));

    if (tag != saved_tag) {
      tag_table->Set(obj, tag);
    }

    stop_reports = (ret & JVMTI_VISIT_ABORT) != 0;

    if (!stop_reports) {
      jint string_ret = ReportString(obj, env, tag_table, callbacks, user_data);
      stop_reports = (string_ret & JVMTI_VISIT_ABORT) != 0;
    }

    if (!stop_reports) {
      jint array_ret = ReportPrimitiveArray(obj, env, tag_table, callbacks, user_data);
      stop_reports = (array_ret & JVMTI_VISIT_ABORT) != 0;
    }

    if (!stop_reports) {
      stop_reports = ReportPrimitiveField::Report(obj, tag_table, callbacks, user_data);
    }
  };
  art::Runtime::Current()->GetHeap()->VisitObjects(visitor);

  return ERR(NONE);
}

jvmtiError HeapUtil::IterateThroughHeap(jvmtiEnv* env,
                                        jint heap_filter,
                                        jclass klass,
                                        const jvmtiHeapCallbacks* callbacks,
                                        const void* user_data) {
  auto JvmtiIterateHeap = [](art::mirror::Object* obj ATTRIBUTE_UNUSED,
                             const jvmtiHeapCallbacks* cb_callbacks,
                             jlong class_tag,
                             jlong size,
                             jlong* tag,
                             jint length,
                             void* cb_user_data)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return cb_callbacks->heap_iteration_callback(class_tag,
                                                 size,
                                                 tag,
                                                 length,
                                                 cb_user_data);
  };
  return DoIterateThroughHeap(JvmtiIterateHeap,
                              env,
                              ArtJvmTiEnv::AsArtJvmTiEnv(env)->object_tag_table.get(),
                              heap_filter,
                              klass,
                              callbacks,
                              user_data);
}

class FollowReferencesHelper final {
 public:
  FollowReferencesHelper(HeapUtil* h,
                         jvmtiEnv* jvmti_env,
                         art::ObjPtr<art::mirror::Object> initial_object,
                         const jvmtiHeapCallbacks* callbacks,
                         art::ObjPtr<art::mirror::Class> class_filter,
                         jint heap_filter,
                         const void* user_data)
      : env(jvmti_env),
        tag_table_(h->GetTags()),
        initial_object_(initial_object),
        callbacks_(callbacks),
        class_filter_(class_filter),
        heap_filter_(heap_filter),
        user_data_(user_data),
        start_(0),
        stop_reports_(false) {
  }

  void Init()
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    if (initial_object_.IsNull()) {
      CollectAndReportRootsVisitor carrv(this, tag_table_, &worklist_, &visited_);

      // We need precise info (e.g., vregs).
      constexpr art::VisitRootFlags kRootFlags = static_cast<art::VisitRootFlags>(
          art::VisitRootFlags::kVisitRootFlagAllRoots | art::VisitRootFlags::kVisitRootFlagPrecise);
      art::Runtime::Current()->VisitRoots(&carrv, kRootFlags);

      art::Runtime::Current()->VisitImageRoots(&carrv);
      stop_reports_ = carrv.IsStopReports();

      if (stop_reports_) {
        worklist_.clear();
      }
    } else {
      visited_.insert(initial_object_.Ptr());
      worklist_.push_back(initial_object_.Ptr());
    }
  }

  void Work()
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    // Currently implemented as a BFS. To lower overhead, we don't erase elements immediately
    // from the head of the work list, instead postponing until there's a gap that's "large."
    //
    // Alternatively, we can implement a DFS and use the work list as a stack.
    while (start_ < worklist_.size()) {
      art::mirror::Object* cur_obj = worklist_[start_];
      start_++;

      if (start_ >= kMaxStart) {
        worklist_.erase(worklist_.begin(), worklist_.begin() + start_);
        start_ = 0;
      }

      VisitObject(cur_obj);

      if (stop_reports_) {
        break;
      }
    }
  }

 private:
  class CollectAndReportRootsVisitor final : public art::RootVisitor {
   public:
    CollectAndReportRootsVisitor(FollowReferencesHelper* helper,
                                 ObjectTagTable* tag_table,
                                 std::vector<art::mirror::Object*>* worklist,
                                 std::unordered_set<art::mirror::Object*>* visited)
        : helper_(helper),
          tag_table_(tag_table),
          worklist_(worklist),
          visited_(visited),
          stop_reports_(false) {}

    void VisitRoots(art::mirror::Object*** roots, size_t count, const art::RootInfo& info)
        override
        REQUIRES_SHARED(art::Locks::mutator_lock_)
        REQUIRES(!*helper_->tag_table_->GetAllowDisallowLock()) {
      for (size_t i = 0; i != count; ++i) {
        AddRoot(*roots[i], info);
      }
    }

    void VisitRoots(art::mirror::CompressedReference<art::mirror::Object>** roots,
                    size_t count,
                    const art::RootInfo& info)
        override REQUIRES_SHARED(art::Locks::mutator_lock_)
        REQUIRES(!*helper_->tag_table_->GetAllowDisallowLock()) {
      for (size_t i = 0; i != count; ++i) {
        AddRoot(roots[i]->AsMirrorPtr(), info);
      }
    }

    bool IsStopReports() {
      return stop_reports_;
    }

   private:
    void AddRoot(art::mirror::Object* root_obj, const art::RootInfo& info)
        REQUIRES_SHARED(art::Locks::mutator_lock_)
        REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
      if (stop_reports_) {
        return;
      }
      bool add_to_worklist = ReportRoot(root_obj, info);
      // We use visited_ to mark roots already so we do not need another set.
      if (visited_->find(root_obj) == visited_->end()) {
        if (add_to_worklist) {
          visited_->insert(root_obj);
          worklist_->push_back(root_obj);
        }
      }
    }

    // Remove NO_THREAD_SAFETY_ANALYSIS once ASSERT_CAPABILITY works correctly.
    art::Thread* FindThread(const art::RootInfo& info) NO_THREAD_SAFETY_ANALYSIS {
      art::Locks::thread_list_lock_->AssertExclusiveHeld(art::Thread::Current());
      return art::Runtime::Current()->GetThreadList()->FindThreadByThreadId(info.GetThreadId());
    }

    jvmtiHeapReferenceKind GetReferenceKind(const art::RootInfo& info,
                                            jvmtiHeapReferenceInfo* ref_info)
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      // TODO: Fill in ref_info.
      memset(ref_info, 0, sizeof(jvmtiHeapReferenceInfo));

      switch (info.GetType()) {
        case art::RootType::kRootJNIGlobal:
          return JVMTI_HEAP_REFERENCE_JNI_GLOBAL;

        case art::RootType::kRootJNILocal:
        {
          uint32_t thread_id = info.GetThreadId();
          ref_info->jni_local.thread_id = thread_id;

          art::Thread* thread = FindThread(info);
          if (thread != nullptr) {
            art::mirror::Object* thread_obj;
            if (thread->IsStillStarting()) {
              thread_obj = nullptr;
            } else {
              thread_obj = thread->GetPeerFromOtherThread();
            }
            if (thread_obj != nullptr) {
              ref_info->jni_local.thread_tag = tag_table_->GetTagOrZero(thread_obj);
            }
          }

          // TODO: We don't have this info.
          if (thread != nullptr) {
            ref_info->jni_local.depth = 0;
            art::ArtMethod* method = thread->GetCurrentMethod(nullptr,
                                                              /* check_suspended= */ true,
                                                              /* abort_on_error= */ false);
            if (method != nullptr) {
              ref_info->jni_local.method = art::jni::EncodeArtMethod(method);
            }
          }

          return JVMTI_HEAP_REFERENCE_JNI_LOCAL;
        }

        case art::RootType::kRootJavaFrame:
        {
          uint32_t thread_id = info.GetThreadId();
          ref_info->stack_local.thread_id = thread_id;

          art::Thread* thread = FindThread(info);
          if (thread != nullptr) {
            art::mirror::Object* thread_obj;
            if (thread->IsStillStarting()) {
              thread_obj = nullptr;
            } else {
              thread_obj = thread->GetPeerFromOtherThread();
            }
            if (thread_obj != nullptr) {
              ref_info->stack_local.thread_tag = tag_table_->GetTagOrZero(thread_obj);
            }
          }

          auto& java_info = static_cast<const art::JavaFrameRootInfo&>(info);
          size_t vreg = java_info.GetVReg();
          ref_info->stack_local.slot = static_cast<jint>(
              vreg <= art::JavaFrameRootInfo::kMaxVReg ? vreg : -1);
          const art::StackVisitor* visitor = java_info.GetVisitor();
          ref_info->stack_local.location =
              static_cast<jlocation>(visitor->GetDexPc(/* abort_on_failure= */ false));
          ref_info->stack_local.depth = static_cast<jint>(visitor->GetFrameDepth());
          art::ArtMethod* method = visitor->GetMethod();
          if (method != nullptr) {
            ref_info->stack_local.method = art::jni::EncodeArtMethod(method);
          }

          return JVMTI_HEAP_REFERENCE_STACK_LOCAL;
        }

        case art::RootType::kRootNativeStack:
        case art::RootType::kRootThreadBlock:
        case art::RootType::kRootThreadObject:
          return JVMTI_HEAP_REFERENCE_THREAD;

        case art::RootType::kRootStickyClass:
        case art::RootType::kRootInternedString:
          // Note: this isn't a root in the RI.
          return JVMTI_HEAP_REFERENCE_SYSTEM_CLASS;

        case art::RootType::kRootMonitorUsed:
        case art::RootType::kRootJNIMonitor:
          return JVMTI_HEAP_REFERENCE_MONITOR;

        case art::RootType::kRootFinalizing:
        case art::RootType::kRootDebugger:
        case art::RootType::kRootReferenceCleanup:
        case art::RootType::kRootVMInternal:
        case art::RootType::kRootUnknown:
          return JVMTI_HEAP_REFERENCE_OTHER;
      }
      LOG(FATAL) << "Unreachable";
      UNREACHABLE();
    }

    bool ReportRoot(art::mirror::Object* root_obj, const art::RootInfo& info)
        REQUIRES_SHARED(art::Locks::mutator_lock_)
        REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
      jvmtiHeapReferenceInfo ref_info;
      jvmtiHeapReferenceKind kind = GetReferenceKind(info, &ref_info);
      jint result = helper_->ReportReference(kind, &ref_info, nullptr, root_obj);
      if ((result & JVMTI_VISIT_ABORT) != 0) {
        stop_reports_ = true;
      }
      return (result & JVMTI_VISIT_OBJECTS) != 0;
    }

   private:
    FollowReferencesHelper* helper_;
    ObjectTagTable* tag_table_;
    std::vector<art::mirror::Object*>* worklist_;
    std::unordered_set<art::mirror::Object*>* visited_;
    bool stop_reports_;
  };

  void VisitObject(art::mirror::Object* obj)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    if (obj->IsClass()) {
      VisitClass(obj->AsClass().Ptr());
      return;
    }
    if (obj->IsArrayInstance()) {
      VisitArray(obj);
      return;
    }

    // All instance fields.
    auto report_instance_field = [&](art::ObjPtr<art::mirror::Object> src,
                                     art::ObjPtr<art::mirror::Class> obj_klass ATTRIBUTE_UNUSED,
                                     art::ArtField& field,
                                     size_t field_index,
                                     void* user_data ATTRIBUTE_UNUSED)
        REQUIRES_SHARED(art::Locks::mutator_lock_)
        REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
      art::ObjPtr<art::mirror::Object> field_value = field.GetObject(src);
      if (field_value != nullptr) {
        jvmtiHeapReferenceInfo reference_info;
        memset(&reference_info, 0, sizeof(reference_info));

        reference_info.field.index = field_index;

        jvmtiHeapReferenceKind kind =
            field.GetOffset().Int32Value() == art::mirror::Object::ClassOffset().Int32Value()
                ? JVMTI_HEAP_REFERENCE_CLASS
                : JVMTI_HEAP_REFERENCE_FIELD;
        const jvmtiHeapReferenceInfo* reference_info_ptr =
            kind == JVMTI_HEAP_REFERENCE_CLASS ? nullptr : &reference_info;

        return !ReportReferenceMaybeEnqueue(kind, reference_info_ptr, src.Ptr(), field_value.Ptr());
      }
      return false;
    };
    stop_reports_ = FieldVisitor<void, true>::ReportFields(obj,
                                                           nullptr,
                                                           VisitorFalse<void>,
                                                           VisitorFalse<void>,
                                                           VisitorFalse<void>,
                                                           report_instance_field);
    if (stop_reports_) {
      return;
    }

    jint string_ret = ReportString(obj, env, tag_table_, callbacks_, user_data_);
    stop_reports_ = (string_ret & JVMTI_VISIT_ABORT) != 0;
    if (stop_reports_) {
      return;
    }

    stop_reports_ = ReportPrimitiveField::Report(obj, tag_table_, callbacks_, user_data_);
  }

  void VisitArray(art::mirror::Object* array)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    stop_reports_ = !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_CLASS,
                                                 nullptr,
                                                 array,
                                                 array->GetClass());
    if (stop_reports_) {
      return;
    }

    if (array->IsObjectArray()) {
      art::ObjPtr<art::mirror::ObjectArray<art::mirror::Object>> obj_array =
          array->AsObjectArray<art::mirror::Object>();
      for (auto elem_pair : art::ZipCount(obj_array->Iterate())) {
        if (elem_pair.first != nullptr) {
          jvmtiHeapReferenceInfo reference_info;
          reference_info.array.index = elem_pair.second;
          stop_reports_ = !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_ARRAY_ELEMENT,
                                                       &reference_info,
                                                       array,
                                                       elem_pair.first.Ptr());
          if (stop_reports_) {
            break;
          }
        }
      }
    } else {
      if (!stop_reports_) {
        jint array_ret = ReportPrimitiveArray(array, env, tag_table_, callbacks_, user_data_);
        stop_reports_ = (array_ret & JVMTI_VISIT_ABORT) != 0;
      }
    }
  }

  void VisitClass(art::mirror::Class* klass)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    // TODO: Are erroneous classes reported? Are non-prepared ones? For now, just use resolved ones.
    if (!klass->IsResolved()) {
      return;
    }

    // Superclass.
    stop_reports_ = !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_SUPERCLASS,
                                                 nullptr,
                                                 klass,
                                                 klass->GetSuperClass().Ptr());
    if (stop_reports_) {
      return;
    }

    // Directly implemented or extended interfaces.
    art::Thread* self = art::Thread::Current();
    art::StackHandleScope<1> hs(self);
    art::Handle<art::mirror::Class> h_klass(hs.NewHandle<art::mirror::Class>(klass));
    for (size_t i = 0; i < h_klass->NumDirectInterfaces(); ++i) {
      art::ObjPtr<art::mirror::Class> inf_klass =
          art::mirror::Class::ResolveDirectInterface(self, h_klass, i);
      if (inf_klass == nullptr) {
        // TODO: With a resolved class this should not happen...
        self->ClearException();
        break;
      }

      stop_reports_ = !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_INTERFACE,
                                                   nullptr,
                                                   klass,
                                                   inf_klass.Ptr());
      if (stop_reports_) {
        return;
      }
    }

    // Classloader.
    // TODO: What about the boot classpath loader? We'll skip for now, but do we have to find the
    //       fake BootClassLoader?
    if (klass->GetClassLoader() != nullptr) {
      stop_reports_ = !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_CLASS_LOADER,
                                                   nullptr,
                                                   klass,
                                                   klass->GetClassLoader().Ptr());
      if (stop_reports_) {
        return;
      }
    }
    DCHECK_EQ(h_klass.Get(), klass);

    // Declared static fields.
    auto report_static_field = [&](art::ObjPtr<art::mirror::Object> obj ATTRIBUTE_UNUSED,
                                   art::ObjPtr<art::mirror::Class> obj_klass,
                                   art::ArtField& field,
                                   size_t field_index,
                                   void* user_data ATTRIBUTE_UNUSED)
        REQUIRES_SHARED(art::Locks::mutator_lock_)
        REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
      art::ObjPtr<art::mirror::Object> field_value = field.GetObject(obj_klass);
      if (field_value != nullptr) {
        jvmtiHeapReferenceInfo reference_info;
        memset(&reference_info, 0, sizeof(reference_info));

        reference_info.field.index = static_cast<jint>(field_index);

        return !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_STATIC_FIELD,
                                            &reference_info,
                                            obj_klass.Ptr(),
                                            field_value.Ptr());
      }
      return false;
    };
    stop_reports_ = FieldVisitor<void, false>::ReportFields(klass,
                                                            nullptr,
                                                            VisitorFalse<void>,
                                                            report_static_field,
                                                            VisitorFalse<void>,
                                                            VisitorFalse<void>);
    if (stop_reports_) {
      return;
    }

    stop_reports_ = ReportPrimitiveField::Report(klass, tag_table_, callbacks_, user_data_);
  }

  void MaybeEnqueue(art::mirror::Object* obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    if (visited_.find(obj) == visited_.end()) {
      worklist_.push_back(obj);
      visited_.insert(obj);
    }
  }

  bool ReportReferenceMaybeEnqueue(jvmtiHeapReferenceKind kind,
                                   const jvmtiHeapReferenceInfo* reference_info,
                                   art::mirror::Object* referree,
                                   art::mirror::Object* referrer)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    jint result = ReportReference(kind, reference_info, referree, referrer);
    if ((result & JVMTI_VISIT_ABORT) == 0) {
      if ((result & JVMTI_VISIT_OBJECTS) != 0) {
        MaybeEnqueue(referrer);
      }
      return true;
    } else {
      return false;
    }
  }

  jint ReportReference(jvmtiHeapReferenceKind kind,
                       const jvmtiHeapReferenceInfo* reference_info,
                       art::mirror::Object* referrer,
                       art::mirror::Object* referree)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    if (referree == nullptr || stop_reports_) {
      return 0;
    }

    if (UNLIKELY(class_filter_ != nullptr) && class_filter_ != referree->GetClass()) {
      return JVMTI_VISIT_OBJECTS;
    }

    const jlong class_tag = tag_table_->GetTagOrZero(referree->GetClass());
    jlong tag = tag_table_->GetTagOrZero(referree);

    if (!heap_filter_.ShouldReportByHeapFilter(tag, class_tag)) {
      return JVMTI_VISIT_OBJECTS;
    }

    const jlong referrer_class_tag =
        referrer == nullptr ? 0 : tag_table_->GetTagOrZero(referrer->GetClass());
    const jlong size = static_cast<jlong>(referree->SizeOf());
    jlong saved_tag = tag;
    jlong referrer_tag = 0;
    jlong saved_referrer_tag = 0;
    jlong* referrer_tag_ptr;
    if (referrer == nullptr) {
      referrer_tag_ptr = nullptr;
    } else {
      if (referrer == referree) {
        referrer_tag_ptr = &tag;
      } else {
        referrer_tag = saved_referrer_tag = tag_table_->GetTagOrZero(referrer);
        referrer_tag_ptr = &referrer_tag;
      }
    }

    jint length = -1;
    if (referree->IsArrayInstance()) {
      length = referree->AsArray()->GetLength();
    }

    jint result = callbacks_->heap_reference_callback(kind,
                                                      reference_info,
                                                      class_tag,
                                                      referrer_class_tag,
                                                      size,
                                                      &tag,
                                                      referrer_tag_ptr,
                                                      length,
                                                      const_cast<void*>(user_data_));

    if (tag != saved_tag) {
      tag_table_->Set(referree, tag);
    }
    if (referrer_tag != saved_referrer_tag) {
      tag_table_->Set(referrer, referrer_tag);
    }

    return result;
  }

  jvmtiEnv* env;
  ObjectTagTable* tag_table_;
  art::ObjPtr<art::mirror::Object> initial_object_;
  const jvmtiHeapCallbacks* callbacks_;
  art::ObjPtr<art::mirror::Class> class_filter_;
  const HeapFilter heap_filter_;
  const void* user_data_;

  std::vector<art::mirror::Object*> worklist_;
  size_t start_;
  static constexpr size_t kMaxStart = 1000000U;

  std::unordered_set<art::mirror::Object*> visited_;

  bool stop_reports_;

  friend class CollectAndReportRootsVisitor;
};

jvmtiError HeapUtil::FollowReferences(jvmtiEnv* env,
                                      jint heap_filter,
                                      jclass klass,
                                      jobject initial_object,
                                      const jvmtiHeapCallbacks* callbacks,
                                      const void* user_data) {
  if (callbacks == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::Thread* self = art::Thread::Current();

  art::gc::Heap* heap = art::Runtime::Current()->GetHeap();
  if (heap->IsGcConcurrentAndMoving()) {
    // Need to take a heap dump while GC isn't running. See the
    // comment in Heap::VisitObjects().
    heap->IncrementDisableMovingGC(self);
  }
  {
    art::ScopedObjectAccess soa(self);      // Now we know we have the shared lock.
    art::jni::ScopedEnableSuspendAllJniIdQueries sjni;  // make sure we can get JNI ids.
    art::ScopedThreadSuspension sts(self, art::kWaitingForVisitObjects);
    art::ScopedSuspendAll ssa("FollowReferences");

    art::ObjPtr<art::mirror::Class> class_filter = klass == nullptr
        ? nullptr
        : art::ObjPtr<art::mirror::Class>::DownCast(self->DecodeJObject(klass));
    FollowReferencesHelper frh(this,
                               env,
                               self->DecodeJObject(initial_object),
                               callbacks,
                               class_filter,
                               heap_filter,
                               user_data);
    frh.Init();
    frh.Work();
  }
  if (heap->IsGcConcurrentAndMoving()) {
    heap->DecrementDisableMovingGC(self);
  }

  return ERR(NONE);
}

jvmtiError HeapUtil::GetLoadedClasses(jvmtiEnv* env,
                                      jint* class_count_ptr,
                                      jclass** classes_ptr) {
  if (class_count_ptr == nullptr || classes_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  class ReportClassVisitor : public art::ClassVisitor {
   public:
    explicit ReportClassVisitor(art::Thread* self) : self_(self) {}

    bool operator()(art::ObjPtr<art::mirror::Class> klass)
        override REQUIRES_SHARED(art::Locks::mutator_lock_) {
      if (klass->IsLoaded() || klass->IsErroneous()) {
        classes_.push_back(self_->GetJniEnv()->AddLocalReference<jclass>(klass));
      }
      return true;
    }

    art::Thread* self_;
    std::vector<jclass> classes_;
  };

  art::Thread* self = art::Thread::Current();
  ReportClassVisitor rcv(self);
  {
    art::ScopedObjectAccess soa(self);
    art::Runtime::Current()->GetClassLinker()->VisitClasses(&rcv);
  }

  size_t size = rcv.classes_.size();
  jclass* classes = nullptr;
  jvmtiError alloc_ret = env->Allocate(static_cast<jlong>(size * sizeof(jclass)),
                                       reinterpret_cast<unsigned char**>(&classes));
  if (alloc_ret != ERR(NONE)) {
    return alloc_ret;
  }

  for (size_t i = 0; i < size; ++i) {
    classes[i] = rcv.classes_[i];
  }
  *classes_ptr = classes;
  *class_count_ptr = static_cast<jint>(size);

  return ERR(NONE);
}

jvmtiError HeapUtil::ForceGarbageCollection(jvmtiEnv* env ATTRIBUTE_UNUSED) {
  art::Runtime::Current()->GetHeap()->CollectGarbage(/* clear_soft_references= */ false);

  return ERR(NONE);
}

static constexpr jint kHeapIdDefault = 0;
static constexpr jint kHeapIdImage = 1;
static constexpr jint kHeapIdZygote = 2;
static constexpr jint kHeapIdApp = 3;

static jint GetHeapId(art::ObjPtr<art::mirror::Object> obj)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  if (obj == nullptr) {
    return -1;
  }

  art::gc::Heap* const heap = art::Runtime::Current()->GetHeap();
  const art::gc::space::ContinuousSpace* const space =
      heap->FindContinuousSpaceFromObject(obj, true);
  jint heap_type = kHeapIdApp;
  if (space != nullptr) {
    if (space->IsZygoteSpace()) {
      heap_type = kHeapIdZygote;
    } else if (space->IsImageSpace() && heap->ObjectIsInBootImageSpace(obj)) {
      // Only count objects in the boot image as HPROF_HEAP_IMAGE, this leaves app image objects
      // as HPROF_HEAP_APP. b/35762934
      heap_type = kHeapIdImage;
    }
  } else {
    const auto* los = heap->GetLargeObjectsSpace();
    if (los->Contains(obj.Ptr()) && los->IsZygoteLargeObject(art::Thread::Current(), obj.Ptr())) {
      heap_type = kHeapIdZygote;
    }
  }
  return heap_type;
};

jvmtiError HeapExtensions::GetObjectHeapId(jvmtiEnv* env, jlong tag, jint* heap_id, ...) {
  if (heap_id == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::Thread* self = art::Thread::Current();

  auto work = [&]() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    ObjectTagTable* tag_table = ArtJvmTiEnv::AsArtJvmTiEnv(env)->object_tag_table.get();
    art::ObjPtr<art::mirror::Object> obj = tag_table->Find(tag);
    jint heap_type = GetHeapId(obj);
    if (heap_type == -1) {
      return ERR(NOT_FOUND);
    }
    *heap_id = heap_type;
    return ERR(NONE);
  };

  if (!art::Locks::mutator_lock_->IsSharedHeld(self)) {
    if (!self->IsThreadSuspensionAllowable()) {
      return ERR(INTERNAL);
    }
    art::ScopedObjectAccess soa(self);
    return work();
  } else {
    // We cannot use SOA in this case. We might be holding the lock, but may not be in the
    // runnable state (e.g., during GC).
    art::Locks::mutator_lock_->AssertSharedHeld(self);
    // TODO: Investigate why ASSERT_SHARED_CAPABILITY doesn't work.
    auto annotalysis_workaround = [&]() NO_THREAD_SAFETY_ANALYSIS {
      return work();
    };
    return annotalysis_workaround();
  }
}

static jvmtiError CopyStringAndReturn(jvmtiEnv* env, const char* in, char** out) {
  jvmtiError error;
  JvmtiUniquePtr<char[]> param_name = CopyString(env, in, &error);
  if (param_name == nullptr) {
    return error;
  }
  *out = param_name.release();
  return ERR(NONE);
}

static constexpr const char* kHeapIdDefaultName = "default";
static constexpr const char* kHeapIdImageName = "image";
static constexpr const char* kHeapIdZygoteName = "zygote";
static constexpr const char* kHeapIdAppName = "app";

jvmtiError HeapExtensions::GetHeapName(jvmtiEnv* env, jint heap_id, char** heap_name, ...) {
  switch (heap_id) {
    case kHeapIdDefault:
      return CopyStringAndReturn(env, kHeapIdDefaultName, heap_name);
    case kHeapIdImage:
      return CopyStringAndReturn(env, kHeapIdImageName, heap_name);
    case kHeapIdZygote:
      return CopyStringAndReturn(env, kHeapIdZygoteName, heap_name);
    case kHeapIdApp:
      return CopyStringAndReturn(env, kHeapIdAppName, heap_name);

    default:
      return ERR(ILLEGAL_ARGUMENT);
  }
}

jvmtiError HeapExtensions::IterateThroughHeapExt(jvmtiEnv* env,
                                                 jint heap_filter,
                                                 jclass klass,
                                                 const jvmtiHeapCallbacks* callbacks,
                                                 const void* user_data) {
  if (ArtJvmTiEnv::AsArtJvmTiEnv(env)->capabilities.can_tag_objects != 1) { \
    return ERR(MUST_POSSESS_CAPABILITY); \
  }

  // ART extension API: Also pass the heap id.
  auto ArtIterateHeap = [](art::mirror::Object* obj,
                           const jvmtiHeapCallbacks* cb_callbacks,
                           jlong class_tag,
                           jlong size,
                           jlong* tag,
                           jint length,
                           void* cb_user_data)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    jint heap_id = GetHeapId(obj);
    using ArtExtensionAPI = jint (*)(jlong, jlong, jlong*, jint length, void*, jint);
    return reinterpret_cast<ArtExtensionAPI>(cb_callbacks->heap_iteration_callback)(
        class_tag, size, tag, length, cb_user_data, heap_id);
  };
  return DoIterateThroughHeap(ArtIterateHeap,
                              env,
                              ArtJvmTiEnv::AsArtJvmTiEnv(env)->object_tag_table.get(),
                              heap_filter,
                              klass,
                              callbacks,
                              user_data);
}

namespace {

using ObjectPtr = art::ObjPtr<art::mirror::Object>;
using ObjectMap = std::unordered_map<ObjectPtr, ObjectPtr, art::HashObjPtr>;

static void ReplaceObjectReferences(const ObjectMap& map)
    REQUIRES(art::Locks::mutator_lock_,
             art::Roles::uninterruptible_) {
  art::Runtime::Current()->GetHeap()->VisitObjectsPaused(
      [&](art::mirror::Object* ref) REQUIRES_SHARED(art::Locks::mutator_lock_) {
        // Rewrite all references in the object if needed.
        class ResizeReferenceVisitor {
         public:
          using CompressedObj = art::mirror::CompressedReference<art::mirror::Object>;
          explicit ResizeReferenceVisitor(const ObjectMap& map, ObjectPtr ref)
              : map_(map), ref_(ref) {}

          // Ignore class roots.
          void VisitRootIfNonNull(CompressedObj* root) const
              REQUIRES_SHARED(art::Locks::mutator_lock_) {
            if (root != nullptr) {
              VisitRoot(root);
            }
          }
          void VisitRoot(CompressedObj* root) const REQUIRES_SHARED(art::Locks::mutator_lock_) {
            auto it = map_.find(root->AsMirrorPtr());
            if (it != map_.end()) {
              root->Assign(it->second);
              art::WriteBarrier::ForEveryFieldWrite(ref_);
            }
          }

          void operator()(art::ObjPtr<art::mirror::Object> obj,
                          art::MemberOffset off,
                          bool is_static) const
              REQUIRES_SHARED(art::Locks::mutator_lock_) {
            auto it = map_.find(obj->GetFieldObject<art::mirror::Object>(off));
            if (it != map_.end()) {
              UNUSED(is_static);
              if (UNLIKELY(!is_static && off == art::mirror::Object::ClassOffset())) {
                // We don't want to update the declaring class of any objects. They will be replaced
                // in the heap and we need the declaring class to know its size.
                return;
              } else if (UNLIKELY(!is_static && off == art::mirror::Class::SuperClassOffset() &&
                                  obj->IsClass())) {
                // We don't want to be messing with the class hierarcy either.
                return;
              }
              VLOG(plugin) << "Updating field at offset " << off.Uint32Value() << " of type "
                           << obj->GetClass()->PrettyClass();
              obj->SetFieldObject</*transaction*/ false>(off, it->second);
              art::WriteBarrier::ForEveryFieldWrite(obj);
            }
          }

          // java.lang.ref.Reference visitor.
          void operator()(art::ObjPtr<art::mirror::Class> klass ATTRIBUTE_UNUSED,
                          art::ObjPtr<art::mirror::Reference> ref) const
              REQUIRES_SHARED(art::Locks::mutator_lock_) {
            operator()(ref, art::mirror::Reference::ReferentOffset(), /* is_static */ false);
          }

         private:
          const ObjectMap& map_;
          ObjectPtr ref_;
        };

        ResizeReferenceVisitor rrv(map, ref);
        if (ref->IsClass()) {
          // Class object native roots are the ArtField and ArtMethod 'declaring_class_' fields
          // which we don't want to be messing with as it would break ref-visitor assumptions about
          // what a class looks like. We want to keep the default behavior in other cases (such as
          // dex-cache) though. Unfortunately there is no way to tell from the visitor where exactly
          // the root came from.
          // TODO It might be nice to have the visitors told where the reference came from.
          ref->VisitReferences</*kVisitNativeRoots*/false>(rrv, rrv);
        } else {
          ref->VisitReferences</*kVisitNativeRoots*/true>(rrv, rrv);
        }
      });
}

static void ReplaceStrongRoots(art::Thread* self, const ObjectMap& map)
    REQUIRES(art::Locks::mutator_lock_, art::Roles::uninterruptible_) {
  // replace root references expcept java frames.
  struct ResizeRootVisitor : public art::RootVisitor {
   public:
    explicit ResizeRootVisitor(const ObjectMap& map) : map_(map) {}

    // TODO It's somewhat annoying to have to have this function implemented twice. It might be
    // good/useful to implement operator= for CompressedReference to allow us to use a template to
    // implement both of these.
    void VisitRoots(art::mirror::Object*** roots, size_t count, const art::RootInfo& info) override
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      art::mirror::Object*** end = roots + count;
      for (art::mirror::Object** obj = *roots; roots != end; obj = *(++roots)) {
        auto it = map_.find(*obj);
        if (it != map_.end()) {
          // Java frames might have the JIT doing optimizations (for example loop-unrolling or
          // eliding bounds checks) so we need deopt them once we're done here.
          if (info.GetType() == art::RootType::kRootJavaFrame) {
            const art::JavaFrameRootInfo& jfri =
                art::down_cast<const art::JavaFrameRootInfo&>(info);
            if (jfri.GetVReg() == art::JavaFrameRootInfo::kMethodDeclaringClass) {
              info.Describe(VLOG_STREAM(plugin) << "Not changing declaring-class during stack"
                                                << " walk. Found obsolete java frame id ");
              continue;
            } else {
              info.Describe(VLOG_STREAM(plugin) << "Found java frame id ");
              threads_with_roots_.insert(info.GetThreadId());
            }
          }
          *obj = it->second.Ptr();
        }
      }
    }

    void VisitRoots(art::mirror::CompressedReference<art::mirror::Object>** roots,
                    size_t count,
                    const art::RootInfo& info) override REQUIRES_SHARED(art::Locks::mutator_lock_) {
      art::mirror::CompressedReference<art::mirror::Object>** end = roots + count;
      for (art::mirror::CompressedReference<art::mirror::Object>* obj = *roots; roots != end;
           obj = *(++roots)) {
        auto it = map_.find(obj->AsMirrorPtr());
        if (it != map_.end()) {
          // Java frames might have the JIT doing optimizations (for example loop-unrolling or
          // eliding bounds checks) so we need deopt them once we're done here.
          if (info.GetType() == art::RootType::kRootJavaFrame) {
            const art::JavaFrameRootInfo& jfri =
                art::down_cast<const art::JavaFrameRootInfo&>(info);
            if (jfri.GetVReg() == art::JavaFrameRootInfo::kMethodDeclaringClass) {
              info.Describe(VLOG_STREAM(plugin) << "Not changing declaring-class during stack"
                                                << " walk. Found obsolete java frame id ");
              continue;
            } else {
              info.Describe(VLOG_STREAM(plugin) << "Found java frame id ");
              threads_with_roots_.insert(info.GetThreadId());
            }
          }
          obj->Assign(it->second);
        }
      }
    }

    const std::unordered_set<uint32_t>& GetThreadsWithJavaFrameRoots() const {
      return threads_with_roots_;
    }

   private:
    const ObjectMap& map_;
    std::unordered_set<uint32_t> threads_with_roots_;
  };
  ResizeRootVisitor rrv(map);
  art::Runtime::Current()->VisitRoots(&rrv, art::VisitRootFlags::kVisitRootFlagAllRoots);
  // Handle java Frames. Annoyingly the JIT can embed information about the length of the array into
  // the compiled code. By changing the length of the array we potentially invalidate these
  // assumptions and so could cause (eg) OOB array access or other issues.
  if (!rrv.GetThreadsWithJavaFrameRoots().empty()) {
    art::MutexLock mu(self, *art::Locks::thread_list_lock_);
    art::ThreadList* thread_list = art::Runtime::Current()->GetThreadList();
    art::instrumentation::Instrumentation* instr = art::Runtime::Current()->GetInstrumentation();
    for (uint32_t id : rrv.GetThreadsWithJavaFrameRoots()) {
      art::Thread* t = thread_list->FindThreadByThreadId(id);
      CHECK(t != nullptr) << "id " << id << " does not refer to a valid thread."
                          << " Where did the roots come from?";
      VLOG(plugin) << "Instrumenting thread stack of thread " << *t;
      // TODO Use deopt manager. We need a version that doesn't acquire all the locks we
      // already have.
      // TODO We technically only need to do this if the frames are not already being interpreted.
      // The cost for doing an extra stack walk is unlikely to be worth it though.
      instr->InstrumentThreadStack(t);
    }
  }
}

static void ReplaceWeakRoots(art::Thread* self,
                             EventHandler* event_handler,
                             const ObjectMap& map)
    REQUIRES(art::Locks::mutator_lock_, art::Roles::uninterruptible_) {
  // Handle tags. We want to do this seprately from other weak-refs (handled below) because we need
  // to send additional events and handle cases where the agent might have tagged the new
  // replacement object during the VMObjectAlloc. We do this by removing all tags associated with
  // both the obsolete and the new arrays. Then we send the ObsoleteObjectCreated event and cache
  // the new tag values. We next update all the other weak-references (the tags have been removed)
  // and finally update the tag table with the new values. Doing things in this way (1) keeps all
  // code relating to updating weak-references together and (2) ensures we don't end up in strange
  // situations where the order of weak-ref visiting affects the final tagging state. Since we have
  // the mutator_lock_ and gc-paused throughout this whole process no threads should be able to see
  // the interval where the objects are not tagged.
  struct NewTagValue {
   public:
    ObjectPtr obsolete_obj_;
    jlong obsolete_tag_;
    ObjectPtr new_obj_;
    jlong new_tag_;
  };

  // Map from the environment to the list of <obsolete_tag, new_tag> pairs that were changed.
  std::unordered_map<ArtJvmTiEnv*, std::vector<NewTagValue>> changed_tags;
  event_handler->ForEachEnv(self, [&](ArtJvmTiEnv* env) {
    // Cannot have REQUIRES(art::Locks::mutator_lock_) since ForEachEnv doesn't require it.
    art::Locks::mutator_lock_->AssertExclusiveHeld(self);
    env->object_tag_table->Lock();
    // Get the tags and clear them (so we don't need to special-case the normal weak-ref visitor)
    for (auto it : map) {
      jlong new_tag = 0;
      jlong obsolete_tag = 0;
      bool had_obsolete_tag = env->object_tag_table->RemoveLocked(it.first, &obsolete_tag);
      bool had_new_tag = env->object_tag_table->RemoveLocked(it.second, &new_tag);
      // Dispatch event.
      if (had_obsolete_tag || had_new_tag) {
        event_handler->DispatchEventOnEnv<ArtJvmtiEvent::kObsoleteObjectCreated>(
            env, self, &obsolete_tag, &new_tag);
        changed_tags.try_emplace(env).first->second.push_back(
            { it.first, obsolete_tag, it.second, new_tag });
      }
    }
    // After weak-ref update we need to go back and re-add obsoletes. We wait to avoid having to
    // deal with the visit-weaks overwriting the initial new_obj_ptr tag and generally making things
    // difficult.
    env->object_tag_table->Unlock();
  });
  // Handle weak-refs.
  struct ReplaceWeaksVisitor : public art::IsMarkedVisitor {
   public:
    ReplaceWeaksVisitor(const ObjectMap& map) : map_(map) {}

    art::mirror::Object* IsMarked(art::mirror::Object* obj)
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      auto it = map_.find(obj);
      if (it != map_.end()) {
        return it->second.Ptr();
      } else {
        return obj;
      }
    }

   private:
    const ObjectMap& map_;
  };
  ReplaceWeaksVisitor rwv(map);
  art::Runtime::Current()->SweepSystemWeaks(&rwv);
  // Re-add the object tags. At this point all weak-references to the old_obj_ptr are gone.
  event_handler->ForEachEnv(self, [&](ArtJvmTiEnv* env) {
    // Cannot have REQUIRES(art::Locks::mutator_lock_) since ForEachEnv doesn't require it.
    art::Locks::mutator_lock_->AssertExclusiveHeld(self);
    env->object_tag_table->Lock();
    auto it = changed_tags.find(env);
    if (it != changed_tags.end()) {
      for (const NewTagValue& v : it->second) {
        env->object_tag_table->SetLocked(v.obsolete_obj_, v.obsolete_tag_);
        env->object_tag_table->SetLocked(v.new_obj_, v.new_tag_);
      }
    }
    env->object_tag_table->Unlock();
  });
}

}  // namespace

void HeapExtensions::ReplaceReference(art::Thread* self,
                                      art::ObjPtr<art::mirror::Object> old_obj_ptr,
                                      art::ObjPtr<art::mirror::Object> new_obj_ptr) {
  ObjectMap map { { old_obj_ptr, new_obj_ptr } };
  ReplaceReferences(self, map);
}

void HeapExtensions::ReplaceReferences(art::Thread* self, const ObjectMap& map) {
  ReplaceObjectReferences(map);
  ReplaceStrongRoots(self, map);
  ReplaceWeakRoots(self, HeapExtensions::gEventHandler, map);
}

jvmtiError HeapExtensions::ChangeArraySize(jvmtiEnv* env, jobject arr, jsize new_size) {
  if (ArtJvmTiEnv::AsArtJvmTiEnv(env)->capabilities.can_tag_objects != 1) {
    return ERR(MUST_POSSESS_CAPABILITY);
  }
  art::Thread* self = art::Thread::Current();
  ScopedNoUserCodeSuspension snucs(self);
  art::ScopedObjectAccess soa(self);
  if (arr == nullptr) {
    JVMTI_LOG(INFO, env) << "Cannot resize a null object";
    return ERR(NULL_POINTER);
  }
  art::ObjPtr<art::mirror::Class> klass(soa.Decode<art::mirror::Object>(arr)->GetClass());
  if (!klass->IsArrayClass()) {
    JVMTI_LOG(INFO, env) << klass->PrettyClass() << " is not an array class!";
    return ERR(ILLEGAL_ARGUMENT);
  }
  if (new_size < 0) {
    JVMTI_LOG(INFO, env) << "Cannot resize an array to a negative size";
    return ERR(ILLEGAL_ARGUMENT);
  }
  // Allocate the new copy.
  art::StackHandleScope<2> hs(self);
  art::Handle<art::mirror::Array> old_arr(hs.NewHandle(soa.Decode<art::mirror::Array>(arr)));
  art::MutableHandle<art::mirror::Array> new_arr(hs.NewHandle<art::mirror::Array>(nullptr));
  if (klass->IsObjectArrayClass()) {
    new_arr.Assign(
        art::mirror::ObjectArray<art::mirror::Object>::Alloc(self, old_arr->GetClass(), new_size));
  } else {
    // NB This also copies the old array but since we aren't suspended we need to do this again to
    // catch any concurrent modifications.
    new_arr.Assign(art::mirror::Array::CopyOf(old_arr, self, new_size));
  }
  if (new_arr.IsNull()) {
    self->AssertPendingOOMException();
    JVMTI_LOG(INFO, env) << "Unable to allocate " << old_arr->GetClass()->PrettyClass()
                         << " (length: " << new_size << ") due to OOME. Error was: "
                         << self->GetException()->Dump();
    self->ClearException();
    return ERR(OUT_OF_MEMORY);
  } else {
    self->AssertNoPendingException();
  }
  // Suspend everything.
  art::ScopedThreadSuspension sts(self, art::ThreadState::kSuspended);
  art::gc::ScopedGCCriticalSection sgccs(
      self, art::gc::GcCause::kGcCauseDebugger, art::gc::CollectorType::kCollectorTypeDebugger);
  art::ScopedSuspendAll ssa("Resize array!");
  // Replace internals.
  new_arr->SetLockWord(old_arr->GetLockWord(false), false);
  old_arr->SetLockWord(art::LockWord::Default(), false);
  // Copy the contents now when everything is suspended.
  int32_t size = std::min(old_arr->GetLength(), new_size);
  switch (old_arr->GetClass()->GetComponentType()->GetPrimitiveType()) {
    case art::Primitive::kPrimBoolean:
      new_arr->AsBooleanArray()->Memcpy(0, old_arr->AsBooleanArray(), 0, size);
      break;
    case art::Primitive::kPrimByte:
      new_arr->AsByteArray()->Memcpy(0, old_arr->AsByteArray(), 0, size);
      break;
    case art::Primitive::kPrimChar:
      new_arr->AsCharArray()->Memcpy(0, old_arr->AsCharArray(), 0, size);
      break;
    case art::Primitive::kPrimShort:
      new_arr->AsShortArray()->Memcpy(0, old_arr->AsShortArray(), 0, size);
      break;
    case art::Primitive::kPrimInt:
      new_arr->AsIntArray()->Memcpy(0, old_arr->AsIntArray(), 0, size);
      break;
    case art::Primitive::kPrimLong:
      new_arr->AsLongArray()->Memcpy(0, old_arr->AsLongArray(), 0, size);
      break;
    case art::Primitive::kPrimFloat:
      new_arr->AsFloatArray()->Memcpy(0, old_arr->AsFloatArray(), 0, size);
      break;
    case art::Primitive::kPrimDouble:
      new_arr->AsDoubleArray()->Memcpy(0, old_arr->AsDoubleArray(), 0, size);
      break;
    case art::Primitive::kPrimNot:
      for (int32_t i = 0; i < size; i++) {
        new_arr->AsObjectArray<art::mirror::Object>()->Set(
            i, old_arr->AsObjectArray<art::mirror::Object>()->Get(i));
      }
      break;
    case art::Primitive::kPrimVoid:
      LOG(FATAL) << "void-array is not a legal type!";
      UNREACHABLE();
  }
  // Actually replace all the pointers.
  ReplaceReference(self, old_arr.Get(), new_arr.Get());
  return OK;
}

void HeapExtensions::Register(EventHandler* eh) {
  gEventHandler = eh;
}

}  // namespace openjdkjvmti
