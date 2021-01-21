/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef ART_RUNTIME_SCOPED_DISABLE_PUBLIC_SDK_CHECKER_H_
#define ART_RUNTIME_SCOPED_DISABLE_PUBLIC_SDK_CHECKER_H_

#include "class_linker.h"

namespace art {

// Utility class to disabled the public sdk checker within a scope (if installed).
class ScopedDisablePublicSdkChecker : public ValueObject {
 public:
  ALWAYS_INLINE ScopedDisablePublicSdkChecker() {
    Runtime* runtime = Runtime::Current();
    if (UNLIKELY(runtime->IsAotCompiler())) {
      runtime->GetClassLinker()->SetEnablePublicSdkChecks(false);
    }
  }

  ALWAYS_INLINE ~ScopedDisablePublicSdkChecker() {
    Runtime* runtime = Runtime::Current();
    if (UNLIKELY(runtime->IsAotCompiler())) {
      runtime->GetClassLinker()->SetEnablePublicSdkChecks(true);
    }
  }

  DISALLOW_COPY_AND_ASSIGN(ScopedDisablePublicSdkChecker);
};

}  // namespace art

#endif  // ART_RUNTIME_SCOPED_DISABLE_PUBLIC_SDK_CHECKER_H_
