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

#ifndef ART_RUNTIME_APP_INFO_H_
#define ART_RUNTIME_APP_INFO_H_

#include <vector>

#include "base/mutex.h"
#include <base/safe_map.h>

namespace art {

// Constants used by VMRuntime.java to interface with the runtime.
// We could get them from the well known class but it's simpler to
// redefine them here.

// VMRuntime.CODE_PATH_TYPE_PRIMARY_APK
static constexpr int32_t kVMRuntimePrimaryApk = 1 << 0;
// VMRuntime.CODE_PATH_TYPE_SPLIT_APK
static constexpr int32_t kVMRuntimeSplitApk = 1 << 1;
// VMRuntime.CODE_PATH_TYPE_SECONDARY_DEX
static constexpr int32_t kVMRuntimeSecondaryDex = 1 << 2;

// Encapsulates the information the runtime has about the application.
//
// The app's info comes from 2 channels:
//   1) during class loading, when we load oat files.
//   2) during app startup, when the framework calls VMRuntime#registerAppInfo.
// In general the class loading event happens before VMRuntime#registerAppInfo.
class AppInfo {
 public:
  enum class CodeType {
    kUnknown,
    kPrimaryApk,
    kSplitApk,
    kSecondaryDex,
  };

  // Converts VMRuntime.java constansts to a CodeType.
  static CodeType FromVMRuntimeConstants(uint32_t code_type);

  AppInfo();

  // Registers the application code paths, types, and associated profiles.
  void RegisterAppInfo(const std::string& package_name,
                       const std::vector<std::string>& code_paths,
                       const std::string& profile_output_filename,
                       const std::string& ref_profile_filename,
                       CodeType code_type);

  // Registers the optimization status for single code path.
  void RegisterOdexStatus(const std::string& code_path,
                          const std::string& compiler_filter,
                          const std::string& compilation_reason,
                          const std::string& odex_status);

  // Extracts the optimization status of the primary apk into the given arguments.
  // If there are multiple primary APKs registed via RegisterAppInfo, the method
  // will assign the status of the first APK, sorted by the location name.
  //
  // Assigns "unknown" if there is no primary apk or the optimization status was
  // not set via RegisterOdexStatus,
  void GetPrimaryApkOptimizationStatus(std::string* out_compiler_filter,
                                       std::string* out_compilation_reason);

 private:
  // Encapsulates optimization information about a particular code location.
  struct CodeLocationInfo {
    // The type of the code location (primary, split, secondary, unknown).
    CodeType code_type{CodeType::kUnknown};

    // The compiler filter of the oat file. Note that this contains
    // the output of OatFileAssistant#GetOptimizationStatus() which may
    // contain values outside the scope of the CompilerFilter enum.
    std::optional<std::string> compiler_filter;

    // The compiler reason of the oat file. Note that this contains
    // the output of OatFileAssistant#GetOptimizationStatus().
    std::optional<std::string> compilation_reason;

    // The odes status as produced by OatFileAssistant#GetOptimizationStatus().
    std::optional<std::string> odex_status;

    // The path to the primary profile if given.
    std::optional<std::string> cur_profile_path;

    // The path to the reference profile if given.
    std::optional<std::string> ref_profile_path;
  };

  // The name of the package if set.
  std::optional<std::string> package_name_ GUARDED_BY(update_mutex_);

  // The registered code locations.
  SafeMap<std::string, CodeLocationInfo> registered_code_locations_ GUARDED_BY(update_mutex_);

  // Lock to touch the state ot the AppInfo object.
  art::Mutex update_mutex_ BOTTOM_MUTEX_ACQUIRED_AFTER;

  friend std::ostream& operator<<(std::ostream& os, AppInfo& rhs);
};

std::ostream& operator<<(std::ostream& os, AppInfo& rhs);

}  // namespace art

#endif  // ART_RUNTIME_APP_INFO_H_
