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

#ifndef ART_RUNTIME_HIDDEN_API_JNI_H_
#define ART_RUNTIME_HIDDEN_API_JNI_H_

#include "base/macros.h"

namespace art {

class Thread;

namespace hiddenapi {

// Stack markers that should be instantiated in JNI Get{Field,Method}Id methods (and
// their static equivalents) to allow native caller checks to take place.
class ScopedCorePlatformApiCheck final {
 public:
  ScopedCorePlatformApiCheck();
  ~ScopedCorePlatformApiCheck();

  // Check whether the caller is automatically approved based on location. Code in the run-time or
  // in an APEX is considered to be automatically approved.
  static bool IsCurrentCallerApproved(Thread* self);

 private:
  // Captures calling PC for frame above the frame allocating the current ScopedCorePlatformApiCheck
  // instance.
  void* CaptureCallerPc();

  // Instances should only be stack allocated, copy and assignment not useful.
  DISALLOW_ALLOCATION();
  DISALLOW_COPY_AND_ASSIGN(ScopedCorePlatformApiCheck);
};

void JniInitializeNativeCallerCheck();
void JniShutdownNativeCallerCheck();

}  // namespace hiddenapi
}  // namespace art

#endif  // ART_RUNTIME_HIDDEN_API_JNI_H_
