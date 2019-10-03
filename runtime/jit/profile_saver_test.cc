/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <gtest/gtest.h>

#include "common_runtime_test.h"
#include "compiler_callbacks.h"
#include "jit/jit.h"
#include "profile_saver.h"
#include "profile/profile_compilation_info.h"

namespace art {

using Hotness = ProfileCompilationInfo::MethodHotness;

class ProfileSaverTest : public CommonRuntimeTest {
 public:
  void SetUpRuntimeOptions(RuntimeOptions *options) override {
    // Reset the callbacks so that the runtime doesn't think it's for AOT.
    callbacks_ = nullptr;
    CommonRuntimeTest::SetUpRuntimeOptions(options);
    // Enable profile saving and the jit.
    options->push_back(std::make_pair("-Xjitsaveprofilinginfo", nullptr));
    options->push_back(std::make_pair("-Xusejit:true", nullptr));
  }

  void PostRuntimeCreate() override {
    // Create a profile saver.
    Runtime* runtime = Runtime::Current();
    const std::vector<std::string> code_paths;
    const std::string fake_file = "fake_file";
    profile_saver_ = new ProfileSaver(
        runtime->GetJITOptions()->GetProfileSaverOptions(),
        fake_file,
        runtime->GetJitCodeCache(),
        code_paths);
  }

  ~ProfileSaverTest() {
    if (profile_saver_ != nullptr) {
      delete profile_saver_;
    }
  }

  ProfileCompilationInfo::ProfileSampleAnnotation GetProfileSampleAnnotation() {
    return profile_saver_->GetProfileSampleAnnotation();
  }

  Hotness::Flag AnnotateSampleFlags(uint32_t flags) {
    return profile_saver_->AnnotateSampleFlags(flags);
  }

 protected:
  ProfileSaver* profile_saver_ = nullptr;
};

// Test profile saving operations for boot image.
class ProfileSaverForBootTest : public ProfileSaverTest {
 public:
  void SetUpRuntimeOptions(RuntimeOptions *options) override {
    ProfileSaverTest::SetUpRuntimeOptions(options);
    options->push_back(std::make_pair("-Xps-profile-boot-class-path", nullptr));
  }
};

TEST_F(ProfileSaverTest, GetProfileSampleAnnotation) {
  ASSERT_EQ(ProfileCompilationInfo::ProfileSampleAnnotation::kNone,
            GetProfileSampleAnnotation());
}

TEST_F(ProfileSaverForBootTest, GetProfileSampleAnnotationUnkown) {
  ProfileCompilationInfo::ProfileSampleAnnotation expected("unknown");
  ASSERT_EQ(expected, GetProfileSampleAnnotation());
}

TEST_F(ProfileSaverForBootTest, GetProfileSampleAnnotation) {
  Runtime::Current()->SetProcessPackageName("test.package");
  ProfileCompilationInfo::ProfileSampleAnnotation expected("test.package");
  ASSERT_EQ(expected, GetProfileSampleAnnotation());
}

TEST_F(ProfileSaverForBootTest, AnnotateSampleFlags) {
  Hotness::Flag expected_flag = Is64BitInstructionSet(Runtime::Current()->GetInstructionSet())
        ? Hotness::kFlag64bit
        : Hotness::kFlag32bit;
  Hotness::Flag actual = AnnotateSampleFlags(Hotness::kFlagHot);

  ASSERT_EQ(static_cast<Hotness::Flag>(expected_flag | Hotness::kFlagHot), actual);
}

TEST_F(ProfileSaverTest, AnnotateSampleFlags) {
  Hotness::Flag actual = AnnotateSampleFlags(Hotness::kFlagHot);

  ASSERT_EQ(Hotness::kFlagHot, actual);
}

}  // namespace art
