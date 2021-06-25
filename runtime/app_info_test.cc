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

#include "app_info.h"

#include <vector>

#include "gtest/gtest.h"

namespace art {

TEST(AppInfoTest, RegisterAppInfo) {
  AppInfo app_info;
  app_info.RegisterAppInfo(
      "package_name",
      std::vector<std::string>({"code_location"}),
      "",
      "",
      AppInfo::CodeType::kPrimaryApk);

  std::string filter;
  std::string reason;
  app_info.GetPrimaryApkOptimizationStatus(&filter, &reason);

  // Odex status was not registered.
  ASSERT_EQ(filter, "unknown");
  ASSERT_EQ(reason, "unknown");
}

TEST(AppInfoTest, RegisterAppInfoWithOdexStatus) {
  AppInfo app_info;
  app_info.RegisterAppInfo(
      "package_name",
      std::vector<std::string>({"code_location"}),
      "",
      "",
      AppInfo::CodeType::kPrimaryApk);
  app_info.RegisterOdexStatus(
      "code_location",
      "filter",
      "reason",
      "odex_status");

  std::string filter;
  std::string reason;
  app_info.GetPrimaryApkOptimizationStatus(&filter, &reason);

  ASSERT_EQ(filter, "filter");
  ASSERT_EQ(reason, "reason");
}

TEST(AppInfoTest, RegisterAppInfoWithOdexStatusMultiplePrimary) {
  AppInfo app_info;
  app_info.RegisterOdexStatus(
      "code_location",
      "filter",
      "reason",
      "odex_status");
  app_info.RegisterOdexStatus(
      "code_location2",
      "filter2",
      "reason2",
      "odex_status");
  app_info.RegisterAppInfo(
      "package_name",
      std::vector<std::string>({"code_location"}),
      "",
      "",
      AppInfo::CodeType::kPrimaryApk);

  std::string filter;
  std::string reason;
  app_info.GetPrimaryApkOptimizationStatus(&filter, &reason);

  // The optimization status should be the one of the first apk.
  ASSERT_EQ(filter, "filter");
  ASSERT_EQ(reason, "reason");
}

TEST(AppInfoTest, RegisterAppInfoWithOdexStatusNoPrimary) {
  AppInfo app_info;

  // Check that the status is not known in an empty app_info.
  std::string filter;
  std::string reason;
  app_info.GetPrimaryApkOptimizationStatus(&filter, &reason);

  // Register a split.s
  app_info.RegisterAppInfo(
      "package_name",
      std::vector<std::string>({"code_location"}),
      "",
      "",
      AppInfo::CodeType::kSplitApk);
  app_info.RegisterOdexStatus(
      "code_location",
      "filter",
      "reason",
      "odex_status");


  // The optimization status is unknown since we don't have primary apks.
  app_info.GetPrimaryApkOptimizationStatus(&filter, &reason);
  ASSERT_EQ(filter, "unknown");
  ASSERT_EQ(reason, "unknown");
}

}  // namespace art
