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
#include "android-base/properties.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace {
constexpr const char* kServerConfigurableFlagsPrefix = "persist.device_config.runtime_native_boot.";
constexpr const char* kUndefinedValue = "UNSET";

// The various ParseValue functions store the parsed value into *destination. If parsing fails for
// some reason, ParseValue makes no changes to *destination.

void ParseValue(const std::string_view value, std::optional<bool>* destination) {
  switch (::android::base::ParseBool(value)) {
    case ::android::base::ParseBoolResult::kError:
      return;
    case ::android::base::ParseBoolResult::kTrue:
      *destination = true;
      return;
    case ::android::base::ParseBoolResult::kFalse:
      *destination = false;
      return;
  }
}

}  // namespace

namespace art {

template <>
std::forward_list<FlagBase*> FlagBase::ALL_FLAGS{};

// gFlags must be defined after FlagBase::ALL_FLAGS so the constructors run in the right order.
Flags gFlags;

template <typename Value>
Flag<Value>::Flag(const std::string& name, Value default_value) : default_{default_value} {
  command_line_argument_name_ = "-X" + name + "=_";
  std::replace(command_line_argument_name_.begin(), command_line_argument_name_.end(), '.', '-');
  system_property_name_ = "dalvik.vm." + name;

  std::string server_setting = name;
  std::replace(server_setting.begin(), server_setting.end(), '.', '_');
  std::replace(server_setting.begin(), server_setting.end(), '-', '_');
  server_setting_name_ = kServerConfigurableFlagsPrefix + server_setting;

  ALL_FLAGS.push_front(this);
}

template <typename Value>
Value Flag<Value>::operator()() {
  if (!initialized_) {
    Reload();
  }
  if (from_command_line_.has_value()) {
    return from_command_line_.value();
  }
  if (from_server_setting_.has_value()) {
    return from_server_setting_.value();
  }
  if (from_system_property_.has_value()) {
    return from_system_property_.value();
  }
  return default_;
}

template <typename Value>
void Flag<Value>::Reload() {
  // Check system properties and server configured value.
  from_system_property_ = std::nullopt;
  const std::string sysprop = ::android::base::GetProperty(system_property_name_, kUndefinedValue);
  if (sysprop != kUndefinedValue) {
    ParseValue(sysprop, &from_system_property_);
  }

  // Check the server-side configuration
  from_server_setting_ = std::nullopt;
  // Read the device_config setting directly to avoid a dependency on server_configurable_flags.
  const std::string server_config =
      ::android::base::GetProperty(server_setting_name_, kUndefinedValue);
  if (server_config != kUndefinedValue) {
    ParseValue(server_config, &from_server_setting_);
  }

  // Command line argument cannot be reloaded. It must be set during initial command line parsing.

  initialized_ = true;
}

template class Flag<bool>;

}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
