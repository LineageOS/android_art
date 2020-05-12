/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef ART_RUNTIME_MIRROR_CLASS_EXT_INL_H_
#define ART_RUNTIME_MIRROR_CLASS_EXT_INL_H_

#include "class_ext.h"

#include "array-inl.h"
#include "art_method-inl.h"
#include "base/enums.h"
#include "base/globals.h"
#include "handle_scope.h"
#include "jni/jni_internal.h"
#include "jni_id_type.h"
#include "mirror/array.h"
#include "mirror/object.h"
#include "object-inl.h"
#include "verify_object.h"
#include "well_known_classes.h"

namespace art {
namespace mirror {

template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline bool ClassExt::EnsureJniIdsArrayPresent(MemberOffset off, size_t count) {
  ObjPtr<Object> existing(
      GetFieldObject<Object, kVerifyFlags, kReadBarrierOption>(off));
  if (!existing.IsNull()) {
    return true;
  }
  Thread* self = Thread::Current();
  StackHandleScope<2> hs(self);
  Handle<ClassExt> h_this(hs.NewHandle(this));
  MutableHandle<Object> new_arr(hs.NewHandle<Object>(nullptr));
  if (UNLIKELY(Runtime::Current()->GetJniIdType() == JniIdType::kSwapablePointer)) {
    new_arr.Assign(Runtime::Current()->GetJniIdManager()->GetPointerMarker());
  } else {
    new_arr.Assign(Runtime::Current()->GetClassLinker()->AllocPointerArray(self, count));
  }
  if (new_arr.IsNull()) {
    // Fail.
    self->AssertPendingOOMException();
    return false;
  }
  bool set;
  // Set the ext_data_ field using CAS semantics.
  if (Runtime::Current()->IsActiveTransaction()) {
    set = h_this->CasFieldObject<true>(
        off, nullptr, new_arr.Get(), CASMode::kStrong, std::memory_order_seq_cst);
  } else {
    set = h_this->CasFieldObject<false>(
        off, nullptr, new_arr.Get(), CASMode::kStrong, std::memory_order_seq_cst);
  }
  if (kIsDebugBuild) {
    ObjPtr<Object> ret(
        set ? new_arr.Get()
            : h_this->GetFieldObject<PointerArray, kVerifyFlags, kReadBarrierOption>(off));
    CHECK(!ret.IsNull());
  }
  return true;
}

template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline bool ClassExt::EnsureJMethodIDsArrayPresent(size_t count) {
  return EnsureJniIdsArrayPresent<kVerifyFlags, kReadBarrierOption>(
      MemberOffset(OFFSET_OF_OBJECT_MEMBER(ClassExt, jmethod_ids_)), count);
}
template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline bool ClassExt::EnsureStaticJFieldIDsArrayPresent(size_t count) {
  return EnsureJniIdsArrayPresent<kVerifyFlags, kReadBarrierOption>(
      MemberOffset(OFFSET_OF_OBJECT_MEMBER(ClassExt, static_jfield_ids_)), count);
}
template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline bool ClassExt::EnsureInstanceJFieldIDsArrayPresent(size_t count) {
  return EnsureJniIdsArrayPresent<kVerifyFlags, kReadBarrierOption>(
      MemberOffset(OFFSET_OF_OBJECT_MEMBER(ClassExt, instance_jfield_ids_)), count);
}

template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<Object> ClassExt::GetInstanceJFieldIDs() {
  return GetFieldObject<Object, kVerifyFlags, kReadBarrierOption>(
      OFFSET_OF_OBJECT_MEMBER(ClassExt, instance_jfield_ids_));
}
template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline bool ClassExt::HasInstanceFieldPointerIdMarker() {
  ObjPtr<Object> arr(GetInstanceJFieldIDs<kVerifyFlags, kReadBarrierOption>());
  return !arr.IsNull() && !arr->IsArrayInstance();
}
template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<PointerArray> ClassExt::GetInstanceJFieldIDsPointerArray() {
  DCHECK(!HasInstanceFieldPointerIdMarker());
  return down_cast<PointerArray*>(GetInstanceJFieldIDs<kVerifyFlags, kReadBarrierOption>().Ptr());
}

template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<Object> ClassExt::GetStaticJFieldIDs() {
  return GetFieldObject<Object, kVerifyFlags, kReadBarrierOption>(
      OFFSET_OF_OBJECT_MEMBER(ClassExt, static_jfield_ids_));
}
template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<PointerArray> ClassExt::GetStaticJFieldIDsPointerArray() {
  DCHECK(!HasStaticFieldPointerIdMarker());
  return down_cast<PointerArray*>(GetStaticJFieldIDs<kVerifyFlags, kReadBarrierOption>().Ptr());
}
template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline bool ClassExt::HasStaticFieldPointerIdMarker() {
  ObjPtr<Object> arr(GetStaticJFieldIDs<kVerifyFlags, kReadBarrierOption>());
  return !arr.IsNull() && !arr->IsArrayInstance();
}

template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<Class> ClassExt::GetObsoleteClass() {
  return GetFieldObject<Class, kVerifyFlags, kReadBarrierOption>(
      OFFSET_OF_OBJECT_MEMBER(ClassExt, obsolete_class_));
}

template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<Object> ClassExt::GetJMethodIDs() {
  return GetFieldObject<Object, kVerifyFlags, kReadBarrierOption>(
      OFFSET_OF_OBJECT_MEMBER(ClassExt, jmethod_ids_));
}
template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<PointerArray> ClassExt::GetJMethodIDsPointerArray() {
  DCHECK(!HasMethodPointerIdMarker());
  return down_cast<PointerArray*>(GetJMethodIDs<kVerifyFlags, kReadBarrierOption>().Ptr());
}
template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline bool ClassExt::HasMethodPointerIdMarker() {
  ObjPtr<Object> arr(GetJMethodIDs<kVerifyFlags, kReadBarrierOption>());
  return !arr.IsNull() && !arr->IsArrayInstance();
}


inline ObjPtr<Object> ClassExt::GetVerifyError() {
  return GetFieldObject<ClassExt>(OFFSET_OF_OBJECT_MEMBER(ClassExt, verify_error_));
}

inline ObjPtr<ObjectArray<DexCache>> ClassExt::GetObsoleteDexCaches() {
  return GetFieldObject<ObjectArray<DexCache>>(
      OFFSET_OF_OBJECT_MEMBER(ClassExt, obsolete_dex_caches_));
}

template<VerifyObjectFlags kVerifyFlags,
         ReadBarrierOption kReadBarrierOption>
inline ObjPtr<PointerArray> ClassExt::GetObsoleteMethods() {
  return GetFieldObject<PointerArray, kVerifyFlags, kReadBarrierOption>(
      OFFSET_OF_OBJECT_MEMBER(ClassExt, obsolete_methods_));
}

inline ObjPtr<Object> ClassExt::GetOriginalDexFile() {
  return GetFieldObject<Object>(OFFSET_OF_OBJECT_MEMBER(ClassExt, original_dex_file_));
}

template<ReadBarrierOption kReadBarrierOption, class Visitor>
void ClassExt::VisitNativeRoots(Visitor& visitor, PointerSize pointer_size) {
  VisitMethods<kReadBarrierOption>([&](ArtMethod* method) {
    method->VisitRoots<kReadBarrierOption>(visitor, pointer_size);
  }, pointer_size);
}

template<ReadBarrierOption kReadBarrierOption, class Visitor>
void ClassExt::VisitMethods(Visitor visitor, PointerSize pointer_size) {
  ObjPtr<PointerArray> arr(GetObsoleteMethods<kDefaultVerifyFlags, kReadBarrierOption>());
  if (!arr.IsNull()) {
    int32_t len = arr->GetLength();
    for (int32_t i = 0; i < len; i++) {
      ArtMethod* method = arr->GetElementPtrSize<ArtMethod*>(i, pointer_size);
      if (method != nullptr) {
        visitor(method);
      }
    }
  }
}

template<ReadBarrierOption kReadBarrierOption, class Visitor>
void ClassExt::VisitJMethodIDs(Visitor v) {
  ObjPtr<Object> arr(GetJMethodIDs<kDefaultVerifyFlags, kReadBarrierOption>());
  if (!arr.IsNull() && arr->IsArrayInstance()) {
    ObjPtr<PointerArray> marr(down_cast<PointerArray*>(arr.Ptr()));
    int32_t len = marr->GetLength();
    for (int32_t i = 0; i < len; i++) {
      jmethodID id = marr->GetElementPtrSize<jmethodID>(i, kRuntimePointerSize);
      if (id != nullptr) {
        v(id, i);
      }
    }
  }
}
template<ReadBarrierOption kReadBarrierOption, class Visitor>
void ClassExt::VisitJFieldIDs(Visitor v) {
  ObjPtr<Object> sarr_obj(GetStaticJFieldIDs<kDefaultVerifyFlags, kReadBarrierOption>());
  if (!sarr_obj.IsNull() && sarr_obj->IsArrayInstance()) {
    ObjPtr<PointerArray> sarr(down_cast<PointerArray*>(sarr_obj->AsArray().Ptr()));
    int32_t len = sarr->GetLength();
    for (int32_t i = 0; i < len; i++) {
      jfieldID id = sarr->GetElementPtrSize<jfieldID>(i, kRuntimePointerSize);
      if (id != nullptr) {
        v(id, i, true);
      }
    }
  }
  ObjPtr<PointerArray> iarr_obj(GetInstanceJFieldIDs<kDefaultVerifyFlags, kReadBarrierOption>());
  if (!iarr_obj.IsNull() && iarr_obj->IsArrayInstance()) {
    ObjPtr<PointerArray> iarr(down_cast<PointerArray*>(iarr_obj->AsArray().Ptr()));
    int32_t len = iarr->GetLength();
    for (int32_t i = 0; i < len; i++) {
      jfieldID id = iarr->GetElementPtrSize<jfieldID>(i, kRuntimePointerSize);
      if (id != nullptr) {
        v(id, i, false);
      }
    }
  }
}

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_CLASS_EXT_INL_H_
