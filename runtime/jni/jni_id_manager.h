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

#ifndef ART_RUNTIME_JNI_JNI_ID_MANAGER_H_
#define ART_RUNTIME_JNI_JNI_ID_MANAGER_H_

#include <atomic>
#include <jni.h>
#include <vector>

#include "art_field.h"
#include "art_method.h"
#include "base/mutex.h"

namespace art {
namespace jni {

class ScopedEnableSuspendAllJniIdQueries;
class JniIdManager {
 public:
  ArtMethod* DecodeMethodId(jmethodID method) REQUIRES(!Locks::jni_id_lock_);
  ArtField* DecodeFieldId(jfieldID field) REQUIRES(!Locks::jni_id_lock_);
  jmethodID EncodeMethodId(ArtMethod* method) REQUIRES(!Locks::jni_id_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);
  jfieldID EncodeFieldId(ArtField* field) REQUIRES(!Locks::jni_id_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  template <typename ArtType>
  uintptr_t EncodeGenericId(ArtType* t) REQUIRES(!Locks::jni_id_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);
  template <typename ArtType>
  ArtType* DecodeGenericId(uintptr_t input) REQUIRES(!Locks::jni_id_lock_);
  template <typename ArtType> std::vector<ArtType*>& GetGenericMap() REQUIRES(Locks::jni_id_lock_);
  template <typename ArtType> uintptr_t GetNextId() REQUIRES(Locks::jni_id_lock_);
  template <typename ArtType>
  size_t GetLinearSearchStartId(ArtType* t) REQUIRES(Locks::jni_id_lock_);

  void StartDefer() REQUIRES(!Locks::jni_id_lock_) REQUIRES_SHARED(Locks::mutator_lock_);
  void EndDefer() REQUIRES(!Locks::jni_id_lock_) REQUIRES_SHARED(Locks::mutator_lock_);

  uintptr_t next_method_id_ GUARDED_BY(Locks::jni_id_lock_) = 1u;
  std::vector<ArtMethod*> method_id_map_ GUARDED_BY(Locks::jni_id_lock_);
  uintptr_t next_field_id_ GUARDED_BY(Locks::jni_id_lock_) = 1u;
  std::vector<ArtField*> field_id_map_ GUARDED_BY(Locks::jni_id_lock_);

  // If non-zero indicates that some thread is trying to allocate ids without being able to update
  // the method->id mapping (due to not being able to allocate or something). In this case decode
  // and encode need to do a linear scan of the lists. The ScopedEnableSuspendAllJniIdQueries struct
  // will deal with fixing everything up.
  size_t deferred_allocation_refcount_ GUARDED_BY(Locks::jni_id_lock_) = 0;
  // min jmethodID that might not have it's method->id mapping filled in.
  uintptr_t deferred_allocation_method_id_start_ GUARDED_BY(Locks::jni_id_lock_) = 0u;
  // min jfieldID that might not have it's field->id mapping filled in.
  uintptr_t deferred_allocation_field_id_start_ GUARDED_BY(Locks::jni_id_lock_) = 0u;

  friend class ScopedEnableSuspendAllJniIdQueries;
};

// A scope that will enable using the Encode/Decode JNI id functions with all threads suspended.
// This is required since normally we need to be able to allocate to encode new ids. This should
// only be used when absolutely required, for example to invoke user-callbacks during heap walking
// or similar.
class ScopedEnableSuspendAllJniIdQueries {
 public:
  ScopedEnableSuspendAllJniIdQueries() REQUIRES_SHARED(Locks::mutator_lock_);
  ~ScopedEnableSuspendAllJniIdQueries() REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  JniIdManager* manager_;
};

}  // namespace jni
}  // namespace art

#endif  // ART_RUNTIME_JNI_JNI_ID_MANAGER_H_
