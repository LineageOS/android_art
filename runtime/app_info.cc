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

#include <app_info.h>

#include "base/logging.h"
#include "base/mutex.h"
#include "base/safe_map.h"
#include "thread-inl.h"

namespace art {

static constexpr const char* kUnknownValue = "unknown";

AppInfo::AppInfo()
    : update_mutex_("app_info_update_mutex", LockLevel::kGenericBottomLock) {}

// Converts VMRuntime.java constansts to a CodeType.
AppInfo::CodeType AppInfo::FromVMRuntimeConstants(uint32_t code_type) {
  switch (code_type) {
    case kVMRuntimePrimaryApk : return CodeType::kPrimaryApk;
    case kVMRuntimeSplitApk : return CodeType::kPrimaryApk;
    case kVMRuntimeSecondaryDex : return CodeType::kSecondaryDex;
    default:
      LOG(WARNING) << "Unknown code type: " << code_type;
      return CodeType::kUnknown;
  }
}

static const char* CodeTypeName(AppInfo::CodeType code_type) {
  switch (code_type) {
    case AppInfo::CodeType::kPrimaryApk : return "primary-apk";
    case AppInfo::CodeType::kSplitApk : return "split-apk";
    case AppInfo::CodeType::kSecondaryDex : return "secondary-dex";
    case AppInfo::CodeType::kUnknown : return "unknown";
  }
}

void AppInfo::RegisterAppInfo(const std::string& package_name,
                              const std::vector<std::string>& code_paths,
                              const std::string& cur_profile_path,
                              const std::string& ref_profile_path,
                              AppInfo::CodeType code_type) {
  MutexLock mu(Thread::Current(), update_mutex_);

  package_name_ = package_name;

  for (const std::string& code_path : code_paths) {
    CodeLocationInfo& cli = registered_code_locations_.GetOrCreate(
        code_path, []() { return CodeLocationInfo(); });
    cli.cur_profile_path = cur_profile_path;
    cli.ref_profile_path = ref_profile_path;
    cli.code_type = code_type;

    VLOG(startup) << "Registering code path. "
        << "\npackage_name=" << package_name
        << "\ncode_path=" << code_path
        << "\ncode_type=" << CodeTypeName(code_type)
        << "\ncur_profile=" << cur_profile_path
        << "\nref_profile=" << ref_profile_path;
  }
}

void AppInfo::RegisterOdexStatus(const std::string& code_path,
                                 const std::string& compiler_filter,
                                 const std::string& compilation_reason,
                                 const std::string& odex_status) {
  MutexLock mu(Thread::Current(), update_mutex_);

  CodeLocationInfo& cli = registered_code_locations_.GetOrCreate(
      code_path, []() { return CodeLocationInfo(); });
  cli.compiler_filter = compiler_filter;
  cli.compilation_reason = compilation_reason;
  cli.odex_status = odex_status;

  VLOG(startup) << "Registering odex status. "
        << "\ncode_path=" << code_path
        << "\ncompiler_filter=" << compiler_filter
        << "\ncompilation_reason=" << compilation_reason
        << "\nodex_status=" << odex_status;
}

void AppInfo::GetPrimaryApkOptimizationStatus(
    std::string* out_compiler_filter,
    std::string* out_compilation_reason) {
  MutexLock mu(Thread::Current(), update_mutex_);

  for (const auto& it : registered_code_locations_) {
    const CodeLocationInfo& cli = it.second;
    if (cli.code_type == CodeType::kPrimaryApk) {
      *out_compiler_filter = cli.compiler_filter.value_or(kUnknownValue);
      *out_compilation_reason = cli.compilation_reason.value_or(kUnknownValue);
      return;
    }
  }
  *out_compiler_filter = kUnknownValue;
  *out_compilation_reason = kUnknownValue;
}

std::ostream& operator<<(std::ostream& os, AppInfo& rhs) {
  MutexLock mu(Thread::Current(), rhs.update_mutex_);

  os << "AppInfo for package_name=" << rhs.package_name_.value_or(kUnknownValue) << "\n";
  for (const auto& it : rhs.registered_code_locations_) {
    const std::string code_path = it.first;
    const AppInfo::CodeLocationInfo& cli = it.second;

    os << "\ncode_path=" << code_path
        << "\ncode_type=" << CodeTypeName(cli.code_type)
        << "\ncompiler_filter=" << cli.compiler_filter.value_or(kUnknownValue)
        << "\ncompilation_reason=" << cli.compilation_reason.value_or(kUnknownValue)
        << "\nodex_status=" << cli.odex_status.value_or(kUnknownValue)
        << "\ncur_profile=" << cli.cur_profile_path.value_or(kUnknownValue)
        << "\nref_profile=" << cli.ref_profile_path.value_or(kUnknownValue)
        << "\n";
  }
  return os;
}

}  // namespace art
