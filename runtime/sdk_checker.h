/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef ART_RUNTIME_SDK_CHECKER_H_
#define ART_RUNTIME_SDK_CHECKER_H_

#include "art_field.h"
#include "art_method.h"
#include "base/locks.h"
#include "dex/dex_file.h"

namespace art {

/**
 * The SdkChecker verifies if a given symbol is present in a given classpath.
 *
 * For convenience and future extensibility the classpath is given as set of
 * dex files, simillar to a regular classpath the APKs use.
 *
 * The symbol (method, field, class) is checked based on its descriptor and not
 * according the any access check semantic.
 *
 * This class is intended to be used during off-device AOT verification when
 * only some predefined symbols should be resolved (e.g. belonging to some public
 * API classpath).
 */
class SdkChecker {
 public:
  // Constructs and SDK Checker from the given public sdk paths. The public_sdk
  // format is the same as the classpath format (e.g. `dex1:dex2:dex3`). The
  // method will attempt to open the dex files and if there are errors it will
  // return a nullptr and set the error_msg appropriately.
  static SdkChecker* Create(const std::string& public_sdk, std::string* error_msg);

  // Verify if it should deny access to the given methods.
  // The decision is based on whether or not any of the API dex files declares a method
  // with the same signature.
  //
  // NOTE: This is an expensive check as it searches the dex files for the necessary type
  // and string ids. This is OK because the functionality here is indended to be used
  // only in AOT verification.
  bool ShouldDenyAccess(ArtMethod* art_method) const REQUIRES_SHARED(Locks::mutator_lock_);

  // Similar to ShouldDenyAccess(ArtMethod* art_method).
  bool ShouldDenyAccess(ArtField* art_field) const REQUIRES_SHARED(Locks::mutator_lock_);

  // Similar to ShouldDenyAccess(ArtMethod* art_method).
  bool ShouldDenyAccess(const char* type_descriptor) const;

 private:
  SdkChecker();

  std::vector<std::unique_ptr<const DexFile>> sdk_dex_files_;
};

}  // namespace art

#endif  // ART_RUNTIME_SDK_CHECKER_H_
