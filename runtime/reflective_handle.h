/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef ART_RUNTIME_REFLECTIVE_HANDLE_H_
#define ART_RUNTIME_REFLECTIVE_HANDLE_H_

#include "base/value_object.h"
#include "reflective_reference.h"

namespace art {

// This is a holder similar to Handle<T> that is used to hold reflective references to ArtField and
// ArtMethod structures. A reflective reference is one that must be updated if the underlying class
// or instances are replaced due to structural redefinition or some other process. In general these
// don't need to be used. It's only when it's important that a reference to a field not become
// obsolete and it needs to be held over a suspend point that this should be used.
template <typename T>
class ReflectiveHandle : public ValueObject {
 public:
  static_assert(std::is_same_v<T, ArtField> || std::is_same_v<T, ArtMethod>,
                "Expected ArtField or ArtMethod");

  ReflectiveHandle() : reference_(nullptr) {}

  ALWAYS_INLINE ReflectiveHandle(const ReflectiveHandle<T>& handle) = default;
  ALWAYS_INLINE ReflectiveHandle<T>& operator=(const ReflectiveHandle<T>& handle) = default;

  ALWAYS_INLINE explicit ReflectiveHandle(ReflectiveReference<T>* reference)
      : reference_(reference) {}

  ALWAYS_INLINE T& operator*() const REQUIRES_SHARED(Locks::mutator_lock_) {
    return *Get();
  }

  ALWAYS_INLINE T* operator->() const REQUIRES_SHARED(Locks::mutator_lock_) {
    return Get();
  }

  ALWAYS_INLINE T* Get() const REQUIRES_SHARED(Locks::mutator_lock_) {
    return reference_->Ptr();
  }

  ALWAYS_INLINE bool IsNull() const {
    // It's safe to null-check it without a read barrier.
    return reference_->IsNull();
  }

  ALWAYS_INLINE bool operator!=(std::nullptr_t) const REQUIRES_SHARED(Locks::mutator_lock_) {
    return !IsNull();
  }

  ALWAYS_INLINE bool operator==(std::nullptr_t) const REQUIRES_SHARED(Locks::mutator_lock_) {
    return IsNull();
  }

 protected:
  ReflectiveReference<T>* reference_;

 private:
  friend class BaseReflectiveHandleScope;
  template <size_t kNumFieldReferences, size_t kNumMethodReferences>
  friend class StackReflectiveHandleScope;
};

// Handles that support assignment.
template <typename T>
class MutableReflectiveHandle : public ReflectiveHandle<T> {
 public:
  MutableReflectiveHandle() {}

  ALWAYS_INLINE MutableReflectiveHandle(const MutableReflectiveHandle<T>& handle)
      REQUIRES_SHARED(Locks::mutator_lock_) = default;

  ALWAYS_INLINE MutableReflectiveHandle<T>& operator=(const MutableReflectiveHandle<T>& handle)
      REQUIRES_SHARED(Locks::mutator_lock_) = default;

  ALWAYS_INLINE explicit MutableReflectiveHandle(ReflectiveReference<T>* reference)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : ReflectiveHandle<T>(reference) {}

  ALWAYS_INLINE T* Assign(T* reference) REQUIRES_SHARED(Locks::mutator_lock_) {
    ReflectiveReference<T>* ref = ReflectiveHandle<T>::reference_;
    T* old = ref->Ptr();
    ref->Assign(reference);
    return old;
  }

 private:
  friend class BaseReflectiveHandleScope;
  template <size_t kNumFieldReferences, size_t kNumMethodReferences>
  friend class StackReflectiveHandleScope;
};

template<typename T>
class ReflectiveHandleWrapper : public MutableReflectiveHandle<T> {
 public:
  ReflectiveHandleWrapper(T** obj, const MutableReflectiveHandle<T>& handle)
     : MutableReflectiveHandle<T>(handle), obj_(obj) {
  }

  ReflectiveHandleWrapper(const ReflectiveHandleWrapper&) = default;

  ~ReflectiveHandleWrapper() {
    *obj_ = MutableReflectiveHandle<T>::Get();
  }

 private:
  T** const obj_;
};

}  // namespace art

#endif  // ART_RUNTIME_REFLECTIVE_HANDLE_H_
