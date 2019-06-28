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

#ifndef ART_RUNTIME_JNI_JNI_INTERNAL_H_
#define ART_RUNTIME_JNI_JNI_INTERNAL_H_

#include <jni.h>
#include <iosfwd>

#include "base/locks.h"
#include "base/macros.h"
#include "runtime.h"

namespace art {

class ArtField;
class ArtMethod;

const JNINativeInterface* GetJniNativeInterface();
const JNINativeInterface* GetRuntimeShutdownNativeInterface();

int ThrowNewException(JNIEnv* env, jclass exception_class, const char* msg, jobject cause);

// Enables native stack checking for field and method resolutions via JNI. This should be called
// during runtime initialization after libjavacore and libopenjdk have been dlopen()'ed.
void JniInitializeNativeCallerCheck();

// Removes native stack checking state.
void JniShutdownNativeCallerCheck();

namespace jni {

// We want to maintain a branchless fast-path for performance reasons. The JniIdManager is the
// ultimate source of truth for how the IDs are handed out but we inline the normal non-index cases
// here.

template <bool kEnableIndexIds>
ALWAYS_INLINE
static bool IsIndexId(jmethodID mid) {
  return kEnableIndexIds && ((reinterpret_cast<uintptr_t>(mid) % 2) != 0);
}

template <bool kEnableIndexIds>
ALWAYS_INLINE
static bool IsIndexId(jfieldID fid) {
  return kEnableIndexIds && ((reinterpret_cast<uintptr_t>(fid) % 2) != 0);
}

template <bool kEnableIndexIds = true>
ALWAYS_INLINE
static inline ArtField* DecodeArtField(jfieldID fid) {
  if (IsIndexId<kEnableIndexIds>(fid)) {
    return Runtime::Current()->GetJniIdManager()->DecodeFieldId(fid);
  } else {
    return reinterpret_cast<ArtField*>(fid);
  }
}

template <bool kEnableIndexIds = true>
ALWAYS_INLINE
static inline jfieldID EncodeArtField(ArtField* field) REQUIRES_SHARED(Locks::mutator_lock_)  {
  if (kEnableIndexIds && Runtime::Current()->JniIdsAreIndices()) {
    return Runtime::Current()->GetJniIdManager()->EncodeFieldId(field);
  } else {
    return reinterpret_cast<jfieldID>(field);
  }
}

template <bool kEnableIndexIds = true>
ALWAYS_INLINE
static inline jmethodID EncodeArtMethod(ArtMethod* art_method)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  if (kEnableIndexIds && Runtime::Current()->JniIdsAreIndices()) {
    return Runtime::Current()->GetJniIdManager()->EncodeMethodId(art_method);
  } else {
    return reinterpret_cast<jmethodID>(art_method);
  }
}

template <bool kEnableIndexIds = true>
ALWAYS_INLINE
static inline ArtMethod* DecodeArtMethod(jmethodID method_id) {
  if (IsIndexId<kEnableIndexIds>(method_id)) {
    return Runtime::Current()->GetJniIdManager()->DecodeMethodId(method_id);
  } else {
    return reinterpret_cast<ArtMethod*>(method_id);
  }
}

}  // namespace jni
}  // namespace art

std::ostream& operator<<(std::ostream& os, const jobjectRefType& rhs);

#endif  // ART_RUNTIME_JNI_JNI_INTERNAL_H_
