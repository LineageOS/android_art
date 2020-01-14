/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_MIRROR_OBJECT_ARRAY_H_
#define ART_RUNTIME_MIRROR_OBJECT_ARRAY_H_

#include <iterator>
#include "array.h"
#include "base/iteration_range.h"
#include "obj_ptr.h"

namespace art {
namespace mirror {

template<typename T, typename Container> class ArrayIter;
template <typename T> using ConstObjPtrArrayIter = ArrayIter<T, const ObjPtr<ObjectArray<T>>>;
template <typename T> using ConstHandleArrayIter = ArrayIter<T, const Handle<ObjectArray<T>>>;
template <typename T> using ObjPtrArrayIter = ArrayIter<T, ObjPtr<ObjectArray<T>>>;
template <typename T> using HandleArrayIter = ArrayIter<T, Handle<ObjectArray<T>>>;

template<class T>
class MANAGED ObjectArray: public Array {
 public:
  // The size of Object[].class.
  static uint32_t ClassSize(PointerSize pointer_size) {
    return Array::ClassSize(pointer_size);
  }

  static ObjPtr<ObjectArray<T>> Alloc(Thread* self,
                                      ObjPtr<Class> object_array_class,
                                      int32_t length,
                                      gc::AllocatorType allocator_type)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!Roles::uninterruptible_);

  static ObjPtr<ObjectArray<T>> Alloc(Thread* self,
                                      ObjPtr<Class> object_array_class,
                                      int32_t length)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!Roles::uninterruptible_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ALWAYS_INLINE ObjPtr<T> Get(int32_t i) REQUIRES_SHARED(Locks::mutator_lock_);

  // Returns true if the object can be stored into the array. If not, throws
  // an ArrayStoreException and returns false.
  // TODO fix thread safety analysis: should be REQUIRES_SHARED(Locks::mutator_lock_).
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool CheckAssignable(ObjPtr<T> object) NO_THREAD_SAFETY_ANALYSIS;

  ALWAYS_INLINE void Set(int32_t i, ObjPtr<T> object) REQUIRES_SHARED(Locks::mutator_lock_);
  // TODO fix thread safety analysis: should be REQUIRES_SHARED(Locks::mutator_lock_).
  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ALWAYS_INLINE void Set(int32_t i, ObjPtr<T> object) NO_THREAD_SAFETY_ANALYSIS;

  // Set element without bound and element type checks, to be used in limited
  // circumstances, such as during boot image writing.
  // TODO fix thread safety analysis broken by the use of template. This should be
  // REQUIRES_SHARED(Locks::mutator_lock_).
  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ALWAYS_INLINE void SetWithoutChecks(int32_t i, ObjPtr<T> object) NO_THREAD_SAFETY_ANALYSIS;
  // TODO fix thread safety analysis broken by the use of template. This should be
  // REQUIRES_SHARED(Locks::mutator_lock_).
  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ALWAYS_INLINE void SetWithoutChecksAndWriteBarrier(int32_t i, ObjPtr<T> object)
      NO_THREAD_SAFETY_ANALYSIS;

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ALWAYS_INLINE ObjPtr<T> GetWithoutChecks(int32_t i) REQUIRES_SHARED(Locks::mutator_lock_);

  // Copy src into this array (dealing with overlaps as memmove does) without assignability checks.
  void AssignableMemmove(int32_t dst_pos,
                         ObjPtr<ObjectArray<T>> src,
                         int32_t src_pos,
                         int32_t count)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Copy src into this array assuming no overlap and without assignability checks.
  void AssignableMemcpy(int32_t dst_pos,
                        ObjPtr<ObjectArray<T>> src,
                        int32_t src_pos,
                        int32_t count)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Copy src into this array with assignability checks.
  template<bool kTransactionActive>
  void AssignableCheckingMemcpy(int32_t dst_pos,
                                ObjPtr<ObjectArray<T>> src,
                                int32_t src_pos,
                                int32_t count,
                                bool throw_exception)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static ObjPtr<ObjectArray<T>> CopyOf(Handle<ObjectArray<T>> h_this,
                                       Thread* self,
                                       int32_t new_length)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Roles::uninterruptible_);

  static MemberOffset OffsetOfElement(int32_t i);

  inline ConstObjPtrArrayIter<T> cbegin() const REQUIRES_SHARED(Locks::mutator_lock_);
  inline ConstObjPtrArrayIter<T> cend() const REQUIRES_SHARED(Locks::mutator_lock_);
  inline IterationRange<ConstObjPtrArrayIter<T>> ConstIterate() const REQUIRES_SHARED(Locks::mutator_lock_) {
    return IterationRange(cbegin(), cend());
  }
  inline ObjPtrArrayIter<T> begin() REQUIRES_SHARED(Locks::mutator_lock_);
  inline ObjPtrArrayIter<T> end() REQUIRES_SHARED(Locks::mutator_lock_);
  inline IterationRange<ObjPtrArrayIter<T>> Iterate() REQUIRES_SHARED(Locks::mutator_lock_) {
    return IterationRange(begin(), end());
  }

  static inline ConstHandleArrayIter<T> cbegin(const Handle<ObjectArray<T>>& h_this)
      REQUIRES_SHARED(Locks::mutator_lock_);
  static inline ConstHandleArrayIter<T> cend(const Handle<ObjectArray<T>>& h_this)
      REQUIRES_SHARED(Locks::mutator_lock_);
  static inline IterationRange<ConstHandleArrayIter<T>> ConstIterate(
      const Handle<ObjectArray<T>>& h_this) REQUIRES_SHARED(Locks::mutator_lock_) {
    return IterationRange(cbegin(h_this), cend(h_this));
  }
  static inline HandleArrayIter<T> begin(Handle<ObjectArray<T>>& h_this)
      REQUIRES_SHARED(Locks::mutator_lock_);
  static inline HandleArrayIter<T> end(Handle<ObjectArray<T>>& h_this)
      REQUIRES_SHARED(Locks::mutator_lock_);
  static inline IterationRange<HandleArrayIter<T>> Iterate(Handle<ObjectArray<T>>& h_this)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return IterationRange(begin(h_this), end(h_this));
  }

 private:
  // TODO fix thread safety analysis broken by the use of template. This should be
  // REQUIRES_SHARED(Locks::mutator_lock_).
  template<typename Visitor>
  void VisitReferences(const Visitor& visitor) NO_THREAD_SAFETY_ANALYSIS;

  friend class Object;  // For VisitReferences
  DISALLOW_IMPLICIT_CONSTRUCTORS(ObjectArray);
};

// Everything is NO_THREAD_SAFETY_ANALYSIS to work-around STL incompat with thread-annotations.
// Everything should have REQUIRES_SHARED(Locks::mutator_lock_).
template <typename T, typename Container>
class ArrayIter : public std::iterator<std::forward_iterator_tag, ObjPtr<T>> {
 private:
  using Iter = ArrayIter<T, Container>;

 public:
  ArrayIter(Container array, int32_t idx) NO_THREAD_SAFETY_ANALYSIS : array_(array), idx_(idx) {
    CheckIdx();
  }

  ArrayIter(const Iter& other) = default;  // NOLINT(runtime/explicit)
  Iter& operator=(const Iter& other) = default;

  bool operator!=(const Iter& other) const NO_THREAD_SAFETY_ANALYSIS {
    CheckIdx();
    return !(*this == other);
  }
  bool operator==(const Iter& other) const NO_THREAD_SAFETY_ANALYSIS {
    return Ptr(other.array_) == Ptr(array_) && other.idx_ == idx_;
  }
  Iter& operator++() NO_THREAD_SAFETY_ANALYSIS {
    idx_++;
    CheckIdx();
    return *this;
  }
  Iter operator++(int) NO_THREAD_SAFETY_ANALYSIS {
    Iter res(this);
    idx_++;
    CheckIdx();
    return res;
  }
  ObjPtr<T> operator->() const NO_THREAD_SAFETY_ANALYSIS {
    CheckIdx();
    return array_->GetWithoutChecks(idx_);
  }
  ObjPtr<T> operator*() const NO_THREAD_SAFETY_ANALYSIS {
    CheckIdx();
    return array_->GetWithoutChecks(idx_);
  }

 private:
  // Checks current index and that locks are properly held.
  void CheckIdx() const REQUIRES_SHARED(Locks::mutator_lock_);

  static ObjectArray<T>* Ptr(const Handle<ObjectArray<T>>& p)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return p.Get();
  }
  static ObjectArray<T>* Ptr(const ObjPtr<ObjectArray<T>>& p)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return p.Ptr();
  }

  Container array_;
  int32_t idx_;
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_OBJECT_ARRAY_H_
