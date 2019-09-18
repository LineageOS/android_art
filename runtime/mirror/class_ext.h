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

#ifndef ART_RUNTIME_MIRROR_CLASS_EXT_H_
#define ART_RUNTIME_MIRROR_CLASS_EXT_H_

#include "array.h"
#include "class.h"
#include "dex_cache.h"
#include "object.h"
#include "object_array.h"
#include "string.h"

namespace art {

struct ClassExtOffsets;

namespace mirror {

// C++ mirror of dalvik.system.ClassExt
class MANAGED ClassExt : public Object {
 public:
  static uint32_t ClassSize(PointerSize pointer_size);

  // Size of an instance of dalvik.system.ClassExt.
  static constexpr uint32_t InstanceSize() {
    return sizeof(ClassExt);
  }

  void SetVerifyError(ObjPtr<Object> obj) REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<Object> GetVerifyError() REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<ObjectArray<DexCache>> GetObsoleteDexCaches() REQUIRES_SHARED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ObjPtr<PointerArray> EnsureInstanceJFieldIDsArrayPresent(size_t count)
      REQUIRES_SHARED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ObjPtr<PointerArray> GetInstanceJFieldIDs() REQUIRES_SHARED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ObjPtr<PointerArray> EnsureStaticJFieldIDsArrayPresent(size_t count)
      REQUIRES_SHARED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ObjPtr<PointerArray> GetStaticJFieldIDs() REQUIRES_SHARED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ObjPtr<PointerArray> EnsureJMethodIDsArrayPresent(size_t count)
      REQUIRES_SHARED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ObjPtr<PointerArray> GetJMethodIDs() REQUIRES_SHARED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ObjPtr<PointerArray> GetObsoleteMethods() REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<Object> GetOriginalDexFile() REQUIRES_SHARED(Locks::mutator_lock_);

  void SetOriginalDexFile(ObjPtr<Object> bytes) REQUIRES_SHARED(Locks::mutator_lock_);

  uint16_t GetPreRedefineClassDefIndex() REQUIRES_SHARED(Locks::mutator_lock_) {
    return static_cast<uint16_t>(
        GetField32(OFFSET_OF_OBJECT_MEMBER(ClassExt, pre_redefine_class_def_index_)));
  }

  void SetPreRedefineClassDefIndex(uint16_t index) REQUIRES_SHARED(Locks::mutator_lock_);

  const DexFile* GetPreRedefineDexFile() REQUIRES_SHARED(Locks::mutator_lock_) {
    return reinterpret_cast<const DexFile*>(static_cast<uintptr_t>(
        GetField64(OFFSET_OF_OBJECT_MEMBER(ClassExt, pre_redefine_dex_file_ptr_))));
  }

  void SetPreRedefineDexFile(const DexFile* dex_file) REQUIRES_SHARED(Locks::mutator_lock_);

  void SetObsoleteArrays(ObjPtr<PointerArray> methods, ObjPtr<ObjectArray<DexCache>> dex_caches)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Extend the obsolete arrays by the given amount.
  static bool ExtendObsoleteArrays(Handle<ClassExt> h_this, Thread* self, uint32_t increase)
      REQUIRES_SHARED(Locks::mutator_lock_);

  template<ReadBarrierOption kReadBarrierOption = kWithReadBarrier, class Visitor>
  inline void VisitNativeRoots(Visitor& visitor, PointerSize pointer_size)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static ObjPtr<ClassExt> Alloc(Thread* self) REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ObjPtr<PointerArray> EnsureJniIdsArrayPresent(MemberOffset off, size_t count)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Field order required by test "ValidateFieldOrderOfJavaCppUnionClasses".
  // An array containing the jfieldIDs assigned to each field in the corresponding position in the
  // classes ifields_ array or '0' if no id has been assigned to that field yet.
  HeapReference<PointerArray> instance_jfield_ids_;

  // An array containing the jmethodIDs assigned to each method in the corresponding position in
  // the classes methods_ array or '0' if no id has been assigned to that method yet.
  HeapReference<PointerArray> jmethod_ids_;

  HeapReference<ObjectArray<DexCache>> obsolete_dex_caches_;

  HeapReference<PointerArray> obsolete_methods_;

  HeapReference<Object> original_dex_file_;

  // An array containing the jfieldIDs assigned to each field in the corresponding position in the
  // classes sfields_ array or '0' if no id has been assigned to that field yet.
  HeapReference<PointerArray> static_jfield_ids_;

  // The saved verification error of this class.
  HeapReference<Object> verify_error_;

  // Native pointer to DexFile and ClassDef index of this class before it was JVMTI-redefined.
  int32_t pre_redefine_class_def_index_;
  int64_t pre_redefine_dex_file_ptr_;

  friend struct art::ClassExtOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(ClassExt);
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_CLASS_EXT_H_
