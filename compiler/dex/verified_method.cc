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

#include "verified_method.h"

#include <algorithm>
#include <memory>

#include <android-base/logging.h>

#include "dex/code_item_accessors-inl.h"
#include "dex/dex_file.h"
#include "dex/dex_instruction-inl.h"
#include "runtime.h"
#include "verifier/method_verifier-inl.h"
#include "verifier/reg_type-inl.h"
#include "verifier/register_line-inl.h"
#include "verifier/verifier_deps.h"

namespace art {

VerifiedMethod::VerifiedMethod(uint32_t encountered_error_types, bool has_runtime_throw)
    : encountered_error_types_(encountered_error_types),
      has_runtime_throw_(has_runtime_throw) {
}

const VerifiedMethod* VerifiedMethod::Create(verifier::MethodVerifier* method_verifier) {
  DCHECK(Runtime::Current()->IsAotCompiler());
  std::unique_ptr<VerifiedMethod> verified_method(
      new VerifiedMethod(method_verifier->GetEncounteredFailureTypes(),
                         method_verifier->HasInstructionThatWillThrow()));

  return verified_method.release();
}

}  // namespace art
