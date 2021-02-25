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

#include "odr_artifacts.h"

#include "arch/instruction_set.h"
#include "base/common_art_test.h"
#include "base/file_utils.h"
#include "base/string_view_cpp20.h"
#include "odrefresh/odrefresh.h"

namespace art {
namespace odrefresh {

TEST(OdrArtifactsTest, ForBootImageExtension) {
  ScopedUnsetEnvironmentVariable no_env("ART_APEX_DATA");

  const std::string image_location = GetApexDataBootImage("/system/framework/framework.jar");
  EXPECT_TRUE(StartsWith(image_location, GetArtApexData()));

  const std::string image_filename =
      GetSystemImageFilename(image_location.c_str(), InstructionSet::kArm64);

  const auto artifacts = OdrArtifacts::ForBootImageExtension(image_filename);
  CHECK_EQ(std::string(kOdrefreshArtifactDirectory) + "/arm64/boot-framework.art",
           artifacts.ImagePath());
  CHECK_EQ(std::string(kOdrefreshArtifactDirectory) + "/arm64/boot-framework.oat",
           artifacts.OatPath());
  CHECK_EQ(std::string(kOdrefreshArtifactDirectory) + "/arm64/boot-framework.vdex",
           artifacts.VdexPath());
}

TEST(OdrArtifactsTest, ForSystemServer) {
  ScopedUnsetEnvironmentVariable no_env("ART_APEX_DATA");

  const std::string image_location = GetApexDataImage("/system/framework/services.jar");
  EXPECT_TRUE(StartsWith(image_location, GetArtApexData()));

  const std::string image_filename =
      GetSystemImageFilename(image_location.c_str(), InstructionSet::kX86);
  const auto artifacts = OdrArtifacts::ForSystemServer(image_filename);
  CHECK_EQ(
      std::string(kOdrefreshArtifactDirectory) + "/x86/system@framework@services.jar@classes.art",
      artifacts.ImagePath());
  CHECK_EQ(
      std::string(kOdrefreshArtifactDirectory) + "/x86/system@framework@services.jar@classes.odex",
      artifacts.OatPath());
  CHECK_EQ(
      std::string(kOdrefreshArtifactDirectory) + "/x86/system@framework@services.jar@classes.vdex",
      artifacts.VdexPath());
}

}  // namespace odrefresh
}  // namespace art
