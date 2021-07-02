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

#include "logging.h"

// This file defines a set of flags that can be used to enable/disable features within ART or
// otherwise tune ART's behavior. Flags can be set through command line options, server side
// configuration, system properties, or default values. This flexibility enables easier development
// and also larger experiments.
//
// The value is retrieved in the following oder:
//   1) server side (device config) property
//   2) system property
//   3) cmdline flag
//   4) default value
//
// The flags are defined in the Flags struct near the bottom of the file. To define a new flag, add
// a Flag field to the struct. Then to read the value of the flag, use gFlag.MyNewFlag().

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {

// Enum representing the type of the ART flag.
enum class FlagType {
  // A flag that only looks at the cmdline argument to retrieve its value.
  kCmdlineOnly,
  // A flag that also looks at system properties and device config
  // (phenotype properties) when retrieving its value.
  kDeviceConfig,
};

// FlagMetaBase handles automatically adding flags to the command line parser. It is parameterized
// by all supported flag types. In general, this should be treated as though it does not exist and
// FlagBase, which is already specialized to the types we support, should be used instead.
template <typename... T>
class FlagMetaBase {
 public:
  FlagMetaBase(const std::string&& command_line_argument_name,
               const std::string&& system_property_name,
               const std::string&& server_setting_name,
               FlagType type) :
      command_line_argument_name_(command_line_argument_name),
      system_property_name_(system_property_name),
      server_setting_name_(server_setting_name),
      type_(type) {}
  virtual ~FlagMetaBase() {}

  template <typename Builder>
  static void AddFlagsToCmdlineParser(Builder* builder) {
    for (auto* flag : ALL_FLAGS) {
      // Each flag can return a pointer to where its command line value is stored. Because these can
      // be different types, the return value comes as a variant. The cases list below contains a
      // lambda that is specialized to handle each branch of the variant and call the correct
      // methods on the command line parser builder.
      FlagValuePointer location = flag->GetCmdLineLocation();
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

  // Reload the value of the flags.
  //
  // DO NOT CALL this outside Runtime Init or Zygote post fork.
  // This is a convention, as we should strive to have a constant view
  // of the flags and not change the runtime behaviour midway during execution.
  static void ReloadAllFlags(const std::string& caller) {
    // Check the caller. This is a simple workaround to attract the attention
    // to a possible dangerous call to ReloadAllFlags, while avoid building
    // a lot of infra for it or having a complex friend definition.
    DCHECK(caller == "Init"
        || caller == "ZygoteHooks_nativePostForkChild"
        || caller == "ZygoteHooks_nativePostForkSystemServer"
        || caller == "test") << caller;
    for (auto* flag : ALL_FLAGS) {
      flag->Reload();
    }

    if (VLOG_IS_ON(startup)) {
      VLOG_STREAM(startup) << "Dumping flags for " << caller;
      DumpFlags(VLOG_STREAM(startup));
    }
  }

  // Dump all the flags info to the given stream.
  static void DumpFlags(std::ostream& oss) {
    for (auto* flag : ALL_FLAGS) {
      oss << "\n{\n";
      flag->Dump(oss);
      oss << "\n}";
    }
  }

 protected:
  using FlagValuePointer = std::variant<std::optional<T>*...>;
  // Return the pointer to the value holder associated with the cmd line location.
  virtual FlagValuePointer GetCmdLineLocation() = 0;
  // Reloads the flag values.
  virtual void Reload() = 0;
  // Dumps the flags info to the given stream.
  virtual void Dump(std::ostream& oss) const = 0;

  static std::forward_list<FlagMetaBase<T...>*> ALL_FLAGS;

  const std::string command_line_argument_name_;
  const std::string system_property_name_;
  const std::string server_setting_name_;
  FlagType type_;
};

using FlagBase = FlagMetaBase<bool, int32_t, uint32_t, std::string>;

template <>
std::forward_list<FlagBase*> FlagBase::ALL_FLAGS;

class FlagsTests;

// Describes the possible origins of a flag value.
enum class FlagOrigin {
  kDefaultValue,
  kCmdlineArg,
  kSystemProperty,
  kServerSetting,
};

// This class defines a flag with a value of a particular type.
template <typename Value>
class Flag : public FlagBase {
 public:
  // Create a new Flag. The name parameter is used to generate the names from the various parameter
  // sources. See the documentation on the Flags struct for an example.
  Flag(const std::string& name, Value default_value, FlagType type);
  virtual ~Flag();


  // Returns the flag value.
  //
  // The value is retrieved in the following oder:
  //   1) server side (device config) property
  //   2) system property
  //   3) cmdline flag
  //   4) default value
  ALWAYS_INLINE Value GetValue() const {
    return std::get<0>(GetValueAndOrigin());
  }

  ALWAYS_INLINE Value operator()() const {
    return GetValue();
  }

  // Return the value of the flag as optional.
  //
  // Returns the value of the flag if and only if the flag is set via
  // a server side setting, system property or a cmdline arg.
  // Otherwise it returns nullopt (meaning this never returns the default value).
  //
  // This is useful for properties that do not have a good default natural value
  // (e.g. file path arguments).
  ALWAYS_INLINE std::optional<Value> GetValueOptional() const {
    std::pair<Value, FlagOrigin> result = GetValueAndOrigin();
    return std::get<1>(result) == FlagOrigin::kDefaultValue
      ? std::nullopt
      : std::make_optional(std::get<0>(result));
  }

  // Returns the value and the origin of that value for the given flag.
  ALWAYS_INLINE std::pair<Value, FlagOrigin> GetValueAndOrigin() const {
    DCHECK(initialized_);
    if (from_server_setting_.has_value()) {
      return std::pair{from_server_setting_.value(), FlagOrigin::kServerSetting};
    }
    if (from_system_property_.has_value()) {
      return std::pair{from_system_property_.value(), FlagOrigin::kSystemProperty};
    }
    if (from_command_line_.has_value()) {
      return std::pair{from_command_line_.value(), FlagOrigin::kCmdlineArg};
    }
    return std::pair{default_, FlagOrigin::kDefaultValue};
  }

  void Dump(std::ostream& oss) const override;

 protected:
  FlagValuePointer GetCmdLineLocation() override { return &from_command_line_; }


  // Reload the server-configured value and system property values. In general this should not be
  // used directly, but it can be used to support reloading the value without restarting the device.
  void Reload() override;

 private:
  bool initialized_;
  const Value default_;
  std::optional<Value> from_command_line_;
  std::optional<Value> from_system_property_;
  std::optional<Value> from_server_setting_;

  friend class TestFlag;
};

// This struct contains the list of ART flags. Flags are parameterized by the type of value they
// support (bool, int, string, etc.). In addition to field name, flags have a name for the parameter
// as well.
//
// Example:
//
//     Flag<int> WriteMetricsToLog{"my-feature-test.flag", 42, FlagType::kDeviceConfig};
//
// This creates a boolean flag that can be read through gFlags.WriteMetricsToLog(). The default
// value is false. Note that the default value can be left unspecified, in which the value of the
// type's default constructor will be used.
//
// The flag can be set through the following generated means:
//
// Command Line:
//
//     -Xmy-feature-test-flag=1
//
// Server Side (Phenotype) Configuration:
//
//     persist.device_config.runtime_native.my-feature-test.flag
//
// System Property:
//
//     setprop dalvik.vm.metrics.my-feature-test.flag 2
struct Flags {
  // Flag used to test the infra.
  // TODO: can be removed once we add real flags.
  Flag<int32_t> MyFeatureTestFlag{"my-feature-test.flag", 42, FlagType::kDeviceConfig};


  // Metric infra flags.

  // The reporting spec for regular apps. An example of valid value is "S,1,2,4,*".
  // See metrics::ReportingPeriodSpec for complete docs.
  Flag<std::string> MetricsReportingSpec{"metrics.reporting-spec", "", FlagType::kDeviceConfig};

  // The reporting spec for the system server. See MetricsReportingSpec as well.
  Flag<std::string> MetricsReportingSpecSystemServer{"metrics.reporting-spec-server", "",
      FlagType::kDeviceConfig};

  // The mods that should report metrics. Together with MetricsReportingNumMods, they
  // dictate what percentage of the runtime execution will report metrics.
  // If the `session_id (a random number) % MetricsReportingNumMods < MetricsReportingMods`
  // then the runtime session will report metrics.
  //
  // By default, the mods are 0, which means the reporting is disabled.
  Flag<uint32_t> MetricsReportingMods{"metrics.reporting-mods", 0,
      FlagType::kDeviceConfig};
  Flag<uint32_t> MetricsReportingModsServer{"metrics.reporting-mods-server", 0,
      FlagType::kDeviceConfig};

  // See MetricsReportingMods docs.
  //
  // By default the number of mods is 100, so MetricsReportingMods will naturally
  // read as the percent of runtime sessions that will report metrics. If a finer
  // grain unit is needed (e.g. a tenth of a percent), the num-mods can be increased.
  Flag<uint32_t> MetricsReportingNumMods{"metrics.reporting-num-mods", 100,
      FlagType::kDeviceConfig};
  Flag<uint32_t> MetricsReportingNumModsServer{"metrics.reporting-num-mods-server", 100,
      FlagType::kDeviceConfig};

  // Whether or not we should write metrics to statsd.
  // Note that the actual write is still controlled by
  // MetricsReportingMods and MetricsReportingNumMods.
  Flag<bool> MetricsWriteToStatsd{ "metrics.write-to-statsd", false, FlagType::kDeviceConfig};

  // Whether or not we should write metrics to logcat.
  // Note that the actual write is still controlled by
  // MetricsReportingMods and MetricsReportingNumMods.
  Flag<bool> MetricsWriteToLogcat{ "metrics.write-to-logcat", false, FlagType::kCmdlineOnly};

  // Whether or not we should write metrics to a file.
  // Note that the actual write is still controlled by
  // MetricsReportingMods and MetricsReportingNumMods.
  Flag<std::string> MetricsWriteToFile{"metrics.write-to-file", "", FlagType::kCmdlineOnly};
};

// This is the actual instance of all the flags.
extern Flags gFlags;

}  // namespace art

#pragma clang diagnostic pop  // -Wconversion

#endif  // ART_LIBARTBASE_BASE_FLAGS_H_
