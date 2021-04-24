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

#ifndef ART_RUNTIME_MIRROR_EXECUTABLE_H_
#define ART_RUNTIME_MIRROR_EXECUTABLE_H_

#include "accessible_object.h"
#include "object.h"
#include "read_barrier_option.h"

namespace art {

struct ExecutableOffsets;
class ArtMethod;
class ReflectiveValueVisitor;

namespace mirror {

// C++ mirror of java.lang.reflect.Executable.
class MANAGED Executable : public AccessibleObject {
 public:
  MIRROR_CLASS("Ljava/lang/reflect/Executable;");

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ArtMethod* GetArtMethod() REQUIRES_SHARED(Locks::mutator_lock_) {
    return reinterpret_cast64<ArtMethod*>(GetField64<kVerifyFlags>(ArtMethodOffset()));
  }

  template <VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  inline void VisitTarget(ReflectiveValueVisitor* v) REQUIRES(Locks::mutator_lock_);

  template <bool kTransactionActive = false,
            bool kCheckTransaction = true,
            VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  void SetArtMethod(ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<mirror::Class> GetDeclaringClass() REQUIRES_SHARED(Locks::mutator_lock_);

  static MemberOffset ArtMethodOffset() {
    return MemberOffset(OFFSETOF_MEMBER(Executable, art_method_));
  }

 protected:
  // Called from Constructor::CreateFromArtMethod, Method::CreateFromArtMethod.
  template <PointerSize kPointerSize>
  void InitializeFromArtMethod(ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);


 private:
  uint8_t has_real_parameter_data_;

  // Padding required for matching alignment with the Java peer.
  uint8_t padding_[2] ATTRIBUTE_UNUSED;

  HeapReference<mirror::Class> declaring_class_;
  HeapReference<mirror::Class> declaring_class_of_overridden_method_;
  HeapReference<mirror::Array> parameters_;
  uint64_t art_method_;
  uint32_t access_flags_;
  uint32_t dex_method_index_;

  template<bool kTransactionActive = false>
  void SetDeclaringClass(ObjPtr<mirror::Class> klass) REQUIRES_SHARED(Locks::mutator_lock_) {
    SetFieldObject<kTransactionActive>(DeclaringClassOffset(), klass);
  }

  template<bool kTransactionActive = false>
  void SetAccessFlags(uint32_t flags) REQUIRES_SHARED(Locks::mutator_lock_) {
    SetField32<kTransactionActive>(AccessFlagsOffset(), flags);
  }

  template<bool kTransactionActive = false>
  void SetDexMethodIndex(uint32_t idx) REQUIRES_SHARED(Locks::mutator_lock_) {
    SetField32<kTransactionActive>(DexMethodIndexOffset(), idx);
  }

  static MemberOffset DeclaringClassOffset() {
    return MemberOffset(OFFSETOF_MEMBER(Executable, declaring_class_));
  }
  static MemberOffset DeclaringClassOfOverriddenMethodOffset() {
    return MemberOffset(OFFSETOF_MEMBER(Executable, declaring_class_of_overridden_method_));
  }
  static MemberOffset AccessFlagsOffset() {
    return MemberOffset(OFFSETOF_MEMBER(Executable, access_flags_));
  }
  static MemberOffset DexMethodIndexOffset() {
    return MemberOffset(OFFSETOF_MEMBER(Executable, dex_method_index_));
  }

  friend struct art::ExecutableOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(Executable);
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_EXECUTABLE_H_
