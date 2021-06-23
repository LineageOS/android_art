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

#include "flags.h"

#include <algorithm>

#include "android-base/parsebool.h"
#include "android-base/parseint.h"
#include "android-base/properties.h"

#include "base/utils.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace {
constexpr const char* kPhenotypeFlagPrefix = "persist.device_config.runtime_native.";
constexpr const char* kSysPropertyFlagPrefix = "dalvik.vm.";
constexpr const char* kUndefinedValue = "";

// The various ParseValue functions store the parsed value into *destination. If parsing fails for
// some reason, ParseValue makes no changes to *destination.

bool ParseValue(const std::string_view value, std::optional<bool>* destination) {
  switch (::android::base::ParseBool(value)) {
    case ::android::base::ParseBoolResult::kError:
      return false;
    case ::android::base::ParseBoolResult::kTrue:
      *destination = true;
      return true;
    case ::android::base::ParseBoolResult::kFalse:
      *destination = false;
      return true;
  }
}

bool ParseValue(const std::string_view value, std::optional<int32_t>* destination) {
  int32_t parsed_value = 0;
  if (::android::base::ParseInt(std::string{value}, &parsed_value)) {
    *destination = parsed_value;
    return true;
  }
  return false;
}

bool ParseValue(const std::string_view value,
                std::optional<uint32_t>* destination) {
  uint32_t parsed_value = 0;
  if (::android::base::ParseUint(std::string{value}, &parsed_value)) {
    *destination = parsed_value;
    return true;
  }
  return false;
}

bool ParseValue(const std::string_view value, std::optional<std::string>* destination) {
  *destination = value;
  return true;
}

}  // namespace

namespace art {

template <>
std::forward_list<FlagBase*> FlagBase::ALL_FLAGS{};

// gFlags must be defined after FlagBase::ALL_FLAGS so the constructors run in the right order.
Flags gFlags;

static std::string GenerateCmdLineArgName(const std::string& name) {
  std::string result = "-X" + name + ":_";
  std::replace(result.begin(), result.end(), '.', '-');
  return result;
}

static std::string GenerateSysPropName(const std::string& name) {
  return kSysPropertyFlagPrefix + name;
}

static std::string GeneratePhenotypeName(const std::string& name) {
  return kPhenotypeFlagPrefix + name;
}

template <typename Value>
Flag<Value>::Flag(const std::string& name, Value default_value, FlagType type) :
    FlagBase(GenerateCmdLineArgName(name),
             GenerateSysPropName(name),
             GeneratePhenotypeName(name),
             type),
    initialized_{false},
    default_{default_value} {
  ALL_FLAGS.push_front(this);
}

template <typename Value>
Flag<Value>::~Flag() {
  ALL_FLAGS.remove(this);
}

template <typename Value>
void Flag<Value>::Reload() {
  initialized_ = true;

  // The cmdline flags are loaded by the parsed_options infra. No action needed here.
  if (type_ == FlagType::kCmdlineOnly) {
    return;
  }

  // Load system properties.
  from_system_property_ = std::nullopt;
  const std::string sysprop = ::android::base::GetProperty(system_property_name_, kUndefinedValue);
  if (sysprop != kUndefinedValue) {
    if (!ParseValue(sysprop, &from_system_property_)) {
      LOG(ERROR) << "Failed to parse " << system_property_name_ << "=" << sysprop;
    }
  }

  // Load the server-side configuration.
  from_server_setting_ = std::nullopt;
  const std::string server_config =
      ::android::base::GetProperty(server_setting_name_, kUndefinedValue);
  if (server_config != kUndefinedValue) {
    if (!ParseValue(server_config, &from_server_setting_)) {
      LOG(ERROR) << "Failed to parse " << server_setting_name_ << "=" << server_config;
    }
  }
}

template <typename Value>
void DumpValue(std::ostream& oss, const std::optional<Value>& val) {
  if (val.has_value()) {
    oss << val.value();
  } else {
    oss << kUndefinedValue;
  }
}

template <typename Value>
void Flag<Value>::Dump(std::ostream& oss) const {
  std::pair<Value, FlagOrigin> valueOrigin = GetValueAndOrigin();
  std::string origin;
  switch (std::get<1>(valueOrigin)) {
    case FlagOrigin::kDefaultValue: origin = "default_value"; break;
    case FlagOrigin::kCmdlineArg: origin = "cmdline_arg"; break;
    case FlagOrigin::kSystemProperty: origin = "system_property"; break;
    case FlagOrigin::kServerSetting: origin = "server_setting"; break;
  }

  oss << "value: " << std::get<0>(valueOrigin) << " (from " << origin << ")";

  oss << "\n default: " << default_;
  oss << "\n " << command_line_argument_name_ << ": ";
  DumpValue(oss, from_command_line_);
  oss << "\n " << system_property_name_ << ": ";
  DumpValue(oss, from_system_property_);
  oss << "\n " << server_setting_name_ << ": ";
  DumpValue(oss, from_server_setting_);
}

template class Flag<bool>;
template class Flag<int>;
template class Flag<std::string>;

}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
