/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "base/flags.h"

#include <optional>

#include "android-base/properties.h"
#include "common_runtime_test.h"


namespace art {

class FlagsTests : public CommonRuntimeTest {
 protected:
  void assertCmdlineValue(bool has_value, int expected) {
    ASSERT_EQ(gFlags.MyFeatureTestFlag.from_command_line_.has_value(), has_value);
    if (has_value) {
      ASSERT_EQ(gFlags.MyFeatureTestFlag.from_command_line_.value(), expected);
    }
  }

  void assertSysPropValue(bool has_value, int expected) {
    ASSERT_EQ(gFlags.MyFeatureTestFlag.from_system_property_.has_value(), has_value);
    if (has_value) {
      ASSERT_EQ(gFlags.MyFeatureTestFlag.from_system_property_.value(), expected);
    }
  }

  void assertServerSettingValue(bool has_value, int expected) {
    ASSERT_EQ(gFlags.MyFeatureTestFlag.from_server_setting_.has_value(), has_value);
    if (has_value) {
      ASSERT_EQ(gFlags.MyFeatureTestFlag.from_server_setting_.value(), expected);
    }
  }

  void assertDefaultValue(int expected) {
    ASSERT_EQ(gFlags.MyFeatureTestFlag.default_, expected);
  }
};

class FlagsTestsWithCmdLine : public FlagsTests {
 public:
  ~FlagsTestsWithCmdLine() {
    android::base::SetProperty("dalvik.vm.my-feature-test.flag", "");
    android::base::SetProperty("persist.device_config.runtime_native.my-feature-test.flag", "");
  }

 protected:
  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    // Disable implicit dex2oat invocations when loading image spaces.
    options->emplace_back("-Xmy-feature-test-flag:1", nullptr);
  }
};

// Validate that when no flag is set, the default is taken and none of the other
// locations are populated
TEST_F(FlagsTests, ValidateDefaultValue) {
  FlagBase::ReloadAllFlags("test");

  assertCmdlineValue(false, 1);
  assertSysPropValue(false, 2);
  assertServerSettingValue(false, 3);
  assertDefaultValue(42);

  ASSERT_EQ(gFlags.MyFeatureTestFlag(), 42);
}

// Validate that the server side config is picked when it is set.
TEST_F(FlagsTestsWithCmdLine, FlagsTestsGetValueServerSetting) {
  android::base::SetProperty("dalvik.vm.my-feature-test.flag", "2");
  android::base::SetProperty("persist.device_config.runtime_native.my-feature-test.flag", "3");

  FlagBase::ReloadAllFlags("test");

  assertCmdlineValue(true, 1);
  assertSysPropValue(true, 2);
  assertServerSettingValue(true, 3);
  assertDefaultValue(42);

  ASSERT_EQ(gFlags.MyFeatureTestFlag(), 3);
}

// Validate that the system property value is picked when the server one is not set.
TEST_F(FlagsTestsWithCmdLine, FlagsTestsGetValueSysProperty) {
  android::base::SetProperty("dalvik.vm.my-feature-test.flag", "2");

  FlagBase::ReloadAllFlags("test");

  assertCmdlineValue(true, 1);
  assertSysPropValue(true, 2);
  assertServerSettingValue(false, 3);
  assertDefaultValue(42);

  ASSERT_EQ(gFlags.MyFeatureTestFlag(), 2);
}

// Validate that the cmdline value is picked when no properties are set.
TEST_F(FlagsTestsWithCmdLine, FlagsTestsGetValueCmdline) {
  FlagBase::ReloadAllFlags("test");

  assertCmdlineValue(true, 1);
  assertSysPropValue(false, 2);
  assertServerSettingValue(false, 3);
  assertDefaultValue(42);

  ASSERT_EQ(gFlags.MyFeatureTestFlag(), 1);
}

}  // namespace art
