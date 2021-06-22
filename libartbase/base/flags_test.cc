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

// Tests may be run in parallel so this helper class ensures
// that we generate a unique test flag each time to avoid
// tests stepping on each other
class TestFlag {
 public:
  // Takes control of the tmp_file pointer.
  TestFlag(ScratchFile* tmp_file, FlagType flag_type) {
    tmp_file_.reset(tmp_file);

    std::string tmp_name = tmp_file_->GetFilename();
    size_t tmp_last_slash = tmp_name.rfind('/');
    tmp_name = tmp_name.substr(tmp_last_slash + 1);

    flag_name_ = "art.gtest." + tmp_name;
    system_prop_name_ = "dalvik.vm." + flag_name_;
    server_name_ = "persist.device_config.runtime_native." + flag_name_;
    cmd_line_name_ = flag_name_;
    std::replace(cmd_line_name_.begin(), cmd_line_name_.end(), '.', '-');

    flag_.reset(new Flag<int>(flag_name_, /*default_value=*/ 42, flag_type));
  }

  void AssertCmdlineValue(bool has_value, int expected) {
    ASSERT_EQ(flag_->from_command_line_.has_value(), has_value);
    if (has_value) {
      ASSERT_EQ(flag_->from_command_line_.value(), expected);
    }
  }

  void AssertSysPropValue(bool has_value, int expected) {
    ASSERT_EQ(flag_->from_system_property_.has_value(), has_value);
    if (has_value) {
      ASSERT_EQ(flag_->from_system_property_.value(), expected);
    }
  }

  void AssertServerSettingValue(bool has_value, int expected) {
    ASSERT_EQ(flag_->from_server_setting_.has_value(), has_value);
    if (has_value) {
      ASSERT_EQ(flag_->from_server_setting_.value(), expected);
    }
  }

  void AssertDefaultValue(int expected) {
    ASSERT_EQ(flag_->default_, expected);
  }

  int Value() {
    return (*flag_)();
  }

  std::string SystemProperty() const {
    return system_prop_name_;
  }

  std::string ServerSetting() const {
    return server_name_;
  }

  std::string CmdLineName() const {
    return cmd_line_name_;
  }

 private:
  std::unique_ptr<ScratchFile> tmp_file_;
  std::unique_ptr<Flag<int>> flag_;
  std::string flag_name_;
  std::string cmd_line_name_;
  std::string system_prop_name_;
  std::string server_name_;
};

class FlagsTests : public CommonRuntimeTest {
 protected:
  // We need to initialize the flag after the ScratchDir is created
  // but before we configure the runtime options (so that we can get
  // the right name for the config).
  //
  // So we do it in SetUpRuntimeOptions.
  virtual void SetUpRuntimeOptions(RuntimeOptions* options) {
    test_flag_.reset(new TestFlag(new ScratchFile(), FlagType::kDeviceConfig));
    CommonRuntimeTest::SetUpRuntimeOptions(options);
  }

  virtual void TearDown() {
    test_flag_ = nullptr;
    CommonRuntimeTest::TearDown();
  }

  std::unique_ptr<TestFlag> test_flag_;
};

class FlagsTestsWithCmdLineBase : public FlagsTests {
 public:
  explicit FlagsTestsWithCmdLineBase(FlagType type) : flag_type_(type) {
  }

 protected:
  virtual void TearDown() {
    android::base::SetProperty(test_flag_->SystemProperty(), "");
    android::base::SetProperty(test_flag_->ServerSetting(), "");
    FlagsTests::TearDown();
  }

  virtual void SetUpRuntimeOptions(RuntimeOptions* options) {
    test_flag_.reset(new TestFlag(new ScratchFile(), flag_type_));
    std::string option = "-X" + test_flag_->CmdLineName() + ":1";
    options->emplace_back(option.c_str(), nullptr);
  }

  FlagType flag_type_;
};

class FlagsTestsWithCmdLine : public FlagsTestsWithCmdLineBase {
 public:
  FlagsTestsWithCmdLine() : FlagsTestsWithCmdLineBase(FlagType::kDeviceConfig) {
  }
};

class FlagsTestsCmdLineOnly : public FlagsTestsWithCmdLineBase {
 public:
  FlagsTestsCmdLineOnly() : FlagsTestsWithCmdLineBase(FlagType::kCmdlineOnly) {
  }
};

// Validate that when no flag is set, the default is taken and none of the other
// locations are populated
TEST_F(FlagsTests, ValidateDefaultValue) {
  FlagBase::ReloadAllFlags("test");

  test_flag_->AssertCmdlineValue(false, 1);
  test_flag_->AssertSysPropValue(false, 2);
  test_flag_->AssertServerSettingValue(false, 3);
  test_flag_->AssertDefaultValue(42);

  ASSERT_EQ(test_flag_->Value(), 42);
}

// Validate that the server side config is picked when it is set.
TEST_F(FlagsTestsWithCmdLine, FlagsTestsGetValueServerSetting) {
  // On older releases (e.g. nougat) the system properties have very strict
  // limitations (e.g. for length) and setting the properties will fail.
  // On modern platforms this should not be the case, so condition the test
  // based on the success of setting the properties.
  if (!android::base::SetProperty(test_flag_->SystemProperty(), "2")) {
    LOG(ERROR) << "Release does not support property setting, skipping test: "
        << test_flag_->SystemProperty();
    return;
  }

  if (android::base::SetProperty(test_flag_->ServerSetting(), "3")) {
    LOG(ERROR) << "Release does not support property setting, skipping test: "
        << test_flag_->ServerSetting();
    return;
  }

  FlagBase::ReloadAllFlags("test");

  test_flag_->AssertCmdlineValue(true, 1);
  test_flag_->AssertSysPropValue(true, 2);
  test_flag_->AssertServerSettingValue(true, 3);
  test_flag_->AssertDefaultValue(42);

  ASSERT_EQ(test_flag_->Value(), 3);
}

// Validate that the system property value is picked when the server one is not set.
TEST_F(FlagsTestsWithCmdLine, FlagsTestsGetValueSysProperty) {
  if (!android::base::SetProperty(test_flag_->SystemProperty(), "2")) {
    LOG(ERROR) << "Release does not support property setting, skipping test: "
        << test_flag_->SystemProperty();
    return;
  }

  FlagBase::ReloadAllFlags("test");

  test_flag_->AssertCmdlineValue(true, 1);
  test_flag_->AssertSysPropValue(true, 2);
  test_flag_->AssertServerSettingValue(false, 3);
  test_flag_->AssertDefaultValue(42);

  ASSERT_EQ(test_flag_->Value(), 2);
}

// Validate that the cmdline value is picked when no properties are set.
TEST_F(FlagsTestsWithCmdLine, FlagsTestsGetValueCmdline) {
  FlagBase::ReloadAllFlags("test");

  test_flag_->AssertCmdlineValue(true, 1);
  test_flag_->AssertSysPropValue(false, 2);
  test_flag_->AssertServerSettingValue(false, 3);
  test_flag_->AssertDefaultValue(42);

  ASSERT_EQ(test_flag_->Value(), 1);
}

// Validate that cmdline only flags don't read system properties.
TEST_F(FlagsTestsCmdLineOnly, CmdlineOnlyFlags) {
  if (!android::base::SetProperty(test_flag_->SystemProperty(), "2")) {
    LOG(ERROR) << "Release does not support property setting, skipping test: "
        << test_flag_->SystemProperty();
    return;
  }

  if (android::base::SetProperty(test_flag_->ServerSetting(), "3")) {
    LOG(ERROR) << "Release does not support property setting, skipping test: "
        << test_flag_->ServerSetting();
    return;
  }

  FlagBase::ReloadAllFlags("test");

  test_flag_->AssertCmdlineValue(true, 1);
  test_flag_->AssertSysPropValue(false, 2);
  test_flag_->AssertServerSettingValue(false, 3);
  test_flag_->AssertDefaultValue(42);

  ASSERT_EQ(test_flag_->Value(), 1);
}

}  // namespace art
