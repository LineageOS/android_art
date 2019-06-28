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
#include "handle_scope.h"
#include "object-inl.h"

namespace art {
namespace mirror {

template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<PointerArray> ClassExt::EnsureJniIdsArrayPresent(MemberOffset off, size_t count) {
  ObjPtr<PointerArray> existing(
      GetFieldObject<PointerArray, kVerifyFlags, kReadBarrierOption>(off));
  if (!existing.IsNull()) {
    return existing;
  }
  Thread* self = Thread::Current();
  StackHandleScope<2> hs(self);
  Handle<ClassExt> h_this(hs.NewHandle(this));
  Handle<PointerArray> new_arr(
      hs.NewHandle(Runtime::Current()->GetClassLinker()->AllocPointerArray(self, count)));
  if (new_arr.IsNull()) {
    // Fail.
    self->AssertPendingOOMException();
    return nullptr;
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
  ObjPtr<PointerArray> ret(
      set ? new_arr.Get()
          : h_this->GetFieldObject<PointerArray, kVerifyFlags, kReadBarrierOption>(off));
  CHECK(!ret.IsNull());
  return ret;
}

template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<PointerArray> ClassExt::EnsureJMethodIDsArrayPresent(size_t count) {
  return EnsureJniIdsArrayPresent<kVerifyFlags, kReadBarrierOption>(
      MemberOffset(OFFSET_OF_OBJECT_MEMBER(ClassExt, jmethod_ids_)), count);
}
template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<PointerArray> ClassExt::EnsureStaticJFieldIDsArrayPresent(size_t count) {
  return EnsureJniIdsArrayPresent<kVerifyFlags, kReadBarrierOption>(
      MemberOffset(OFFSET_OF_OBJECT_MEMBER(ClassExt, static_jfield_ids_)), count);
}
template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<PointerArray> ClassExt::EnsureInstanceJFieldIDsArrayPresent(size_t count) {
  return EnsureJniIdsArrayPresent<kVerifyFlags, kReadBarrierOption>(
      MemberOffset(OFFSET_OF_OBJECT_MEMBER(ClassExt, instance_jfield_ids_)), count);
}

template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<PointerArray> ClassExt::GetInstanceJFieldIDs() {
  return GetFieldObject<PointerArray, kVerifyFlags, kReadBarrierOption>(
      OFFSET_OF_OBJECT_MEMBER(ClassExt, instance_jfield_ids_));
}

template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<PointerArray> ClassExt::GetStaticJFieldIDs() {
  return GetFieldObject<PointerArray, kVerifyFlags, kReadBarrierOption>(
      OFFSET_OF_OBJECT_MEMBER(ClassExt, static_jfield_ids_));
}

template <VerifyObjectFlags kVerifyFlags, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<PointerArray> ClassExt::GetJMethodIDs() {
  return GetFieldObject<PointerArray, kVerifyFlags, kReadBarrierOption>(
      OFFSET_OF_OBJECT_MEMBER(ClassExt, jmethod_ids_));
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
  ObjPtr<PointerArray> arr(GetObsoleteMethods<kDefaultVerifyFlags, kReadBarrierOption>());
  if (arr.IsNull()) {
    return;
  }
  int32_t len = arr->GetLength();
  for (int32_t i = 0; i < len; i++) {
    ArtMethod* method = arr->GetElementPtrSize<ArtMethod*, kDefaultVerifyFlags>(i, pointer_size);
    if (method != nullptr) {
      method->VisitRoots<kReadBarrierOption>(visitor, pointer_size);
    }
  }
}

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_CLASS_EXT_INL_H_
