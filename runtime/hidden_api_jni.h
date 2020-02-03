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

#include <optional>

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
  // Instances should only be stack allocated, copy and assignment not useful.
  DISALLOW_ALLOCATION();
  DISALLOW_COPY_AND_ASSIGN(ScopedCorePlatformApiCheck);
};

// Kind of memory page from mapped shared object files.
enum class SharedObjectKind {
  kArtModule = 0,   // Part of the ART module.
  kOther = 1        // Neither of the above.
};


class JniLibraryPathClassifier {
 public:
  virtual std::optional<SharedObjectKind> Classify(const char* so_name) = 0;
  virtual ~JniLibraryPathClassifier() {}
};

void JniInitializeNativeCallerCheck(JniLibraryPathClassifier* fc = nullptr);
void JniShutdownNativeCallerCheck();

}  // namespace hiddenapi
}  // namespace art

#endif  // ART_RUNTIME_HIDDEN_API_JNI_H_
