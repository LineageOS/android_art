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

#ifndef ART_LIBARTBASE_BASE_FLAGS_H_
#define ART_LIBARTBASE_BASE_FLAGS_H_

#include <forward_list>
#include <optional>
#include <string>
#include <variant>

// This file defines a set of flags that can be used to enable/disable features within ART or
// otherwise tune ART's behavior. Flags can be set through command line options, system properties,
// or default values. This flexibility enables easier development and also larger experiments.
//
// The flags are defined in the Flags struct near the bottom of the file. To define a new flag, add
// a Flag field to the struct. Then to read the value of the flag, use gFlag.MyNewFlag().

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {

// FlagMetaBase handles automatically adding flags to the command line parser. It is parameterized
// by all supported flag types. In general, this should be treated as though it does not exist and
// FlagBase, which is already specialized to the types we support, should be used instead.
template <typename... T>
class FlagMetaBase {
 public:
  virtual ~FlagMetaBase() {}

  template <typename Builder>
  static void AddFlagsToCmdlineParser(Builder* builder) {
    for (auto* flag : ALL_FLAGS) {
      // Each flag can return a pointer to where its command line value is stored. Because these can
      // be different types, the return value comes as a variant. The cases list below contains a
      // lambda that is specialized to handle each branch of the variant and call the correct
      // methods on the command line parser builder.
      FlagValuePointer location = flag->GetLocation();
      auto cases = {std::function<void()>([&]() {
        if (std::holds_alternative<std::optional<T>*>(location)) {
          builder = &builder->Define(flag->command_line_argument_name_.c_str())
                         .template WithType<T>()
                         .IntoLocation(std::get<std::optional<T>*>(location));
        }
      })...};
      for (auto c : cases) {
        c();
      }
    }
  }

 protected:
  using FlagValuePointer = std::variant<std::optional<T>*...>;
  static std::forward_list<FlagMetaBase<T...>*> ALL_FLAGS;

  std::string command_line_argument_name_;
  std::string system_property_name_;

  virtual FlagValuePointer GetLocation() = 0;
};

using FlagBase = FlagMetaBase<bool, int, std::string>;

template <>
std::forward_list<FlagBase*> FlagBase::ALL_FLAGS;

// This class defines a flag with a value of a particular type.
template <typename Value>
class Flag : public FlagBase {
 public:
  // Create a new Flag. The name parameter is used to generate the names from the various parameter
  // sources. See the documentation on the Flags struct for an example.
  explicit Flag(const std::string& name, Value default_value = {});
  virtual ~Flag() {}

  // Returns the value of the flag.
  //
  // The value returned will be the command line argument, if present, otherwise the
  // server-configured value, if present, otherwise the system property value, if present, and
  // finally, the default value.
  Value operator()();

  // Returns the value of the flag or an empty option if it was not set.
  std::optional<Value> GetOptional();

  // Reload the system property values. In general this should not be used directly, but it can be
  // used to support reloading the value without restarting the device.
  void Reload();

 protected:
  FlagValuePointer GetLocation() override { return &from_command_line_; }

 private:
  bool initialized_{false};
  const Value default_;
  std::optional<Value> from_command_line_;
  std::optional<Value> from_system_property_;
};

// This struct contains the list of ART flags. Flags are parameterized by the type of value they
// support (bool, int, string, etc.). In addition to field name, flags have a name for the parameter
// as well.
//
// Example:
//
//     Flag<bool> WriteMetricsToLog{"metrics.write-to-log", false};
//
// This creates a boolean flag that can be read through gFlags.WriteMetricsToLog(). The default
// value is false. Note that the default value can be left unspecified, in which the value of the
// type's default constructor will be used.
//
// The flag can be set through the following generated means:
//
// Command Line:
//
//     -Xmetrics-write-to-log=true
//
// System Property:
//
//     setprop dalvik.vm.metrics.write-to-log true
struct Flags {
  Flag<bool> WriteMetricsToLog{"metrics.write-to-log", false};
  Flag<bool> WriteMetricsToStatsd{"metrics.write-to-statsd", false};
  Flag<bool> ReportMetricsOnShutdown{"metrics.report-on-shutdown", true};
  Flag<int> MetricsReportingPeriod{"metrics.reporting-period"};
  Flag<std::string> WriteMetricsToFile{"metrics.write-to-file"};
};

// This is the actual instance of all the flags.
extern Flags gFlags;

}  // namespace art

#pragma clang diagnostic pop  // -Wconversion

#endif  // ART_LIBARTBASE_BASE_FLAGS_H_
