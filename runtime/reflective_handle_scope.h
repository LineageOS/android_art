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

#ifndef ART_RUNTIME_REFLECTIVE_HANDLE_SCOPE_H_
#define ART_RUNTIME_REFLECTIVE_HANDLE_SCOPE_H_

#include <android-base/logging.h>

#include <array>
#include <compare>
#include <functional>
#include <stack>

#include "android-base/macros.h"
#include "base/enums.h"
#include "base/globals.h"
#include "base/locks.h"
#include "base/macros.h"
#include "base/value_object.h"
#include "reflective_handle.h"
#include "reflective_reference.h"
#include "reflective_value_visitor.h"

namespace art {

class ArtField;
class ArtMethod;
class BaseReflectiveHandleScope;
class Thread;

// This is a holder similar to StackHandleScope that is used to hold reflective references to
// ArtField and ArtMethod structures. A reflective reference is one that must be updated if the
// underlying class or instances are replaced due to structural redefinition or some other process.
// In general these don't need to be used. It's only when it's important that a reference to a field
// not become obsolete and it needs to be held over a suspend point that this should be used. This
// takes care of the book-keeping to allow the runtime to visit and update ReflectiveHandles when
// structural redefinition occurs.
class BaseReflectiveHandleScope {
 public:
  template <typename Visitor>
  ALWAYS_INLINE void VisitTargets(Visitor& visitor) REQUIRES_SHARED(Locks::mutator_lock_) {
    FunctionReflectiveValueVisitor v(&visitor);
    VisitTargets(&v);
  }

  ALWAYS_INLINE virtual ~BaseReflectiveHandleScope() {
    DCHECK(link_ == nullptr);
  }

  virtual void VisitTargets(ReflectiveValueVisitor* visitor)
      REQUIRES_SHARED(Locks::mutator_lock_) = 0;

  BaseReflectiveHandleScope* GetLink() {
    return link_;
  }

  Thread* GetThread() {
    return self_;
  }

  void Describe(std::ostream& os) const;

 protected:
  ALWAYS_INLINE BaseReflectiveHandleScope() : self_(nullptr), link_(nullptr) {}

  ALWAYS_INLINE inline void PushScope(Thread* self) REQUIRES_SHARED(Locks::mutator_lock_);
  ALWAYS_INLINE inline void PopScope() REQUIRES_SHARED(Locks::mutator_lock_);

  // Thread this node is rooted in.
  Thread* self_;
  // Next node in the handle-scope linked list. Root is held by Thread.
  BaseReflectiveHandleScope* link_;

 private:
  DISALLOW_COPY_AND_ASSIGN(BaseReflectiveHandleScope);
};
std::ostream& operator<<(std::ostream& os, const BaseReflectiveHandleScope& brhs);

template <size_t kNumFields, size_t kNumMethods>
class StackReflectiveHandleScope : public BaseReflectiveHandleScope {
 private:
  static constexpr bool kHasFields = kNumFields > 0;
  static constexpr bool kHasMethods = kNumMethods > 0;

 public:
  ALWAYS_INLINE explicit StackReflectiveHandleScope(Thread* self)
      REQUIRES_SHARED(Locks::mutator_lock_);
  ALWAYS_INLINE ~StackReflectiveHandleScope() REQUIRES_SHARED(Locks::mutator_lock_);

  void VisitTargets(ReflectiveValueVisitor* visitor) override REQUIRES_SHARED(Locks::mutator_lock_);

  template <typename T,
            typename = typename std::enable_if_t<(kHasFields && std::is_same_v<T, ArtField>) ||
                                                 (kHasMethods && std::is_same_v<T, ArtMethod>)>>
  ALWAYS_INLINE MutableReflectiveHandle<T> NewHandle(T* t) REQUIRES_SHARED(Locks::mutator_lock_) {
    if constexpr (std::is_same_v<T, ArtField>) {
      return NewFieldHandle(t);
    } else {
      static_assert(std::is_same_v<T, ArtMethod>, "Expected ArtField or ArtMethod");
      return NewMethodHandle(t);
    }
  }
  template<typename T>
  ALWAYS_INLINE ReflectiveHandleWrapper<T> NewReflectiveHandleWrapper(T** t)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return ReflectiveHandleWrapper<T>(t, NewHandle(*t));
  }

  ALWAYS_INLINE MutableReflectiveHandle<ArtField> NewFieldHandle(ArtField* f)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    static_assert(kHasFields, "No fields");
    DCHECK_LT(field_pos_, kNumFields);
    MutableReflectiveHandle<ArtField> fh(GetMutableFieldHandle(field_pos_++));
    fh.Assign(f);
    return fh;
  }
  ALWAYS_INLINE ReflectiveHandleWrapper<ArtField> NewReflectiveFieldHandleWrapper(ArtField** f)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return ReflectiveHandleWrapper<ArtField>(f, NewMethodHandle(*f));
  }

  ALWAYS_INLINE ArtField* GetField(size_t i) {
    static_assert(kHasFields, "No fields");
    return GetFieldReference(i)->Ptr();
  }
  ALWAYS_INLINE ReflectiveHandle<ArtField> GetFieldHandle(size_t i) {
    static_assert(kHasFields, "No fields");
    return ReflectiveHandle<ArtField>(GetFieldReference(i));
  }
  ALWAYS_INLINE MutableReflectiveHandle<ArtField> GetMutableFieldHandle(size_t i) {
    static_assert(kHasFields, "No fields");
    return MutableReflectiveHandle<ArtField>(GetFieldReference(i));
  }

  ALWAYS_INLINE MutableReflectiveHandle<ArtMethod> NewMethodHandle(ArtMethod* m)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    static_assert(kHasMethods, "No methods");
    DCHECK_LT(method_pos_, kNumMethods);
    MutableReflectiveHandle<ArtMethod> mh(GetMutableMethodHandle(method_pos_++));
    mh.Assign(m);
    return mh;
  }
  ALWAYS_INLINE ReflectiveHandleWrapper<ArtMethod> NewReflectiveMethodHandleWrapper(ArtMethod** m)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return ReflectiveHandleWrapper<ArtMethod>(m, NewMethodHandle(*m));
  }

  ALWAYS_INLINE ArtMethod* GetMethod(size_t i) {
    static_assert(kHasMethods, "No methods");
    return GetMethodReference(i)->Ptr();
  }
  ALWAYS_INLINE ReflectiveHandle<ArtMethod> GetMethodHandle(size_t i) {
    static_assert(kHasMethods, "No methods");
    return ReflectiveHandle<ArtMethod>(GetMethodReference(i));
  }
  ALWAYS_INLINE MutableReflectiveHandle<ArtMethod> GetMutableMethodHandle(size_t i) {
    static_assert(kHasMethods, "No methods");
    return MutableReflectiveHandle<ArtMethod>(GetMethodReference(i));
  }

  size_t RemainingFieldSlots() const {
    return kNumFields - field_pos_;
  }

  size_t RemainingMethodSlots() const {
    return kNumMethods - method_pos_;
  }

 private:
  ReflectiveReference<ArtMethod>* GetMethodReference(size_t i) {
    DCHECK_LT(i, method_pos_);
    return &methods_[i];
  }

  ReflectiveReference<ArtField>* GetFieldReference(size_t i) {
    DCHECK_LT(i, field_pos_);
    return &fields_[i];
  }

  size_t field_pos_;
  size_t method_pos_;
  std::array<ReflectiveReference<ArtField>, kNumFields> fields_;
  std::array<ReflectiveReference<ArtMethod>, kNumMethods> methods_;
};

template <size_t kNumMethods>
using StackArtMethodHandleScope = StackReflectiveHandleScope</*kNumFields=*/0, kNumMethods>;

template <size_t kNumFields>
using StackArtFieldHandleScope = StackReflectiveHandleScope<kNumFields, /*kNumMethods=*/0>;

}  // namespace art

#endif  // ART_RUNTIME_REFLECTIVE_HANDLE_SCOPE_H_
