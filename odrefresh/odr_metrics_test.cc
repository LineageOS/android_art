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

#include "odr_metrics.h"
#include "base/casts.h"
#include "odr_metrics_record.h"

#include <unistd.h>

#include <fstream>
#include <memory>
#include <string>

#include "base/common_art_test.h"

namespace art {
namespace odrefresh {

class OdrMetricsTest : public CommonArtTest {
 public:
  void SetUp() override {
    CommonArtTest::SetUp();

    scratch_dir_ = std::make_unique<ScratchDir>();
    metrics_file_path_ = scratch_dir_->GetPath() + "/metrics.txt";
    cache_directory_ = scratch_dir_->GetPath() + "/dir";
    mkdir(cache_directory_.c_str(), S_IRWXU);
  }

  void TearDown() override {
    scratch_dir_.reset();
  }

  bool MetricsFileExists() const {
    const char* path = metrics_file_path_.c_str();
    return OS::FileExists(path);
  }

  bool RemoveMetricsFile() const {
    const char* path = metrics_file_path_.c_str();
    if (OS::FileExists(path)) {
      return unlink(path) == 0;
    }
    return true;
  }

  const std::string GetCacheDirectory() const { return cache_directory_; }
  const std::string GetMetricsFilePath() const { return metrics_file_path_; }

 protected:
  std::unique_ptr<ScratchDir> scratch_dir_;
  std::string metrics_file_path_;
  std::string cache_directory_;
};

TEST_F(OdrMetricsTest, ToRecordFailsIfNotTriggered) {
  {
    OdrMetrics metrics(GetCacheDirectory(), GetMetricsFilePath());
    OdrMetricsRecord record {};
    EXPECT_FALSE(metrics.ToRecord(&record));
  }

  {
    OdrMetrics metrics(GetCacheDirectory(), GetMetricsFilePath());
    metrics.SetArtApexVersion(99);
    metrics.SetStage(OdrMetrics::Stage::kCheck);
    metrics.SetStatus(OdrMetrics::Status::kNoSpace);
    OdrMetricsRecord record {};
    EXPECT_FALSE(metrics.ToRecord(&record));
  }
}

TEST_F(OdrMetricsTest, ToRecordSucceedsIfTriggered) {
  OdrMetrics metrics(GetCacheDirectory(), GetMetricsFilePath());
  metrics.SetArtApexVersion(99);
  metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
  metrics.SetStage(OdrMetrics::Stage::kCheck);
  metrics.SetStatus(OdrMetrics::Status::kNoSpace);

  OdrMetricsRecord record{};
  EXPECT_TRUE(metrics.ToRecord(&record));

  EXPECT_EQ(99, record.art_apex_version);
  EXPECT_EQ(OdrMetrics::Trigger::kApexVersionMismatch,
            enum_cast<OdrMetrics::Trigger>(record.trigger));
  EXPECT_EQ(OdrMetrics::Stage::kCheck, enum_cast<OdrMetrics::Stage>(record.stage_reached));
  EXPECT_EQ(OdrMetrics::Status::kNoSpace, enum_cast<OdrMetrics::Status>(record.status));
}

TEST_F(OdrMetricsTest, MetricsFileIsNotCreatedIfNotTriggered) {
  EXPECT_TRUE(RemoveMetricsFile());

  // Metrics file is (potentially) written in OdrMetrics destructor.
  {
    OdrMetrics metrics(GetCacheDirectory(), GetMetricsFilePath());
    metrics.SetArtApexVersion(99);
    metrics.SetStage(OdrMetrics::Stage::kCheck);
    metrics.SetStatus(OdrMetrics::Status::kNoSpace);
  }
  EXPECT_FALSE(MetricsFileExists());
}

TEST_F(OdrMetricsTest, NoMetricsFileIsCreatedIfTriggered) {
  EXPECT_TRUE(RemoveMetricsFile());

  // Metrics file is (potentially) written in OdrMetrics destructor.
  {
    OdrMetrics metrics(GetCacheDirectory(), GetMetricsFilePath());
    metrics.SetArtApexVersion(101);
    metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
    metrics.SetStage(OdrMetrics::Stage::kCheck);
    metrics.SetStatus(OdrMetrics::Status::kNoSpace);
  }
  EXPECT_TRUE(MetricsFileExists());
}

TEST_F(OdrMetricsTest, StageDoesNotAdvancedAfterFailure) {
  OdrMetrics metrics(GetCacheDirectory(), GetMetricsFilePath());
  metrics.SetArtApexVersion(1999);
  metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
  metrics.SetStage(OdrMetrics::Stage::kCheck);
  metrics.SetStatus(OdrMetrics::Status::kNoSpace);
  metrics.SetStage(OdrMetrics::Stage::kComplete);

  OdrMetricsRecord record{};
  EXPECT_TRUE(metrics.ToRecord(&record));

  EXPECT_EQ(OdrMetrics::Stage::kCheck, enum_cast<OdrMetrics::Stage>(record.stage_reached));
}

TEST_F(OdrMetricsTest, TimeValuesAreRecorded) {
  OdrMetrics metrics(GetCacheDirectory(), GetMetricsFilePath());
  metrics.SetArtApexVersion(1999);
  metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
  metrics.SetStage(OdrMetrics::Stage::kCheck);
  metrics.SetStatus(OdrMetrics::Status::kOK);

  // Primary boot classpath compilation time.
  OdrMetricsRecord record{};
  {
    metrics.SetStage(OdrMetrics::Stage::kPrimaryBootClasspath);
    ScopedOdrCompilationTimer timer(metrics);
    sleep(2u);
  }
  EXPECT_TRUE(metrics.ToRecord(&record));
  EXPECT_EQ(OdrMetrics::Stage::kPrimaryBootClasspath,
            enum_cast<OdrMetrics::Stage>(record.stage_reached));
  EXPECT_NE(0, record.primary_bcp_compilation_seconds);
  EXPECT_GT(10, record.primary_bcp_compilation_seconds);
  EXPECT_EQ(0, record.secondary_bcp_compilation_seconds);
  EXPECT_EQ(0, record.system_server_compilation_seconds);

  // Secondary boot classpath compilation time.
  {
    metrics.SetStage(OdrMetrics::Stage::kSecondaryBootClasspath);
    ScopedOdrCompilationTimer timer(metrics);
    sleep(2u);
  }
  EXPECT_TRUE(metrics.ToRecord(&record));
  EXPECT_EQ(OdrMetrics::Stage::kSecondaryBootClasspath,
            enum_cast<OdrMetrics::Stage>(record.stage_reached));
  EXPECT_NE(0, record.primary_bcp_compilation_seconds);
  EXPECT_NE(0, record.secondary_bcp_compilation_seconds);
  EXPECT_GT(10, record.secondary_bcp_compilation_seconds);
  EXPECT_EQ(0, record.system_server_compilation_seconds);

  // system_server classpath compilation time.
  {
    metrics.SetStage(OdrMetrics::Stage::kSystemServerClasspath);
    ScopedOdrCompilationTimer timer(metrics);
    sleep(2u);
  }
  EXPECT_TRUE(metrics.ToRecord(&record));
  EXPECT_EQ(OdrMetrics::Stage::kSystemServerClasspath,
            enum_cast<OdrMetrics::Stage>(record.stage_reached));
  EXPECT_NE(0, record.primary_bcp_compilation_seconds);
  EXPECT_NE(0, record.secondary_bcp_compilation_seconds);
  EXPECT_NE(0, record.system_server_compilation_seconds);
  EXPECT_GT(10, record.system_server_compilation_seconds);
}

TEST_F(OdrMetricsTest, CacheSpaceValuesAreUpdated) {
  OdrMetricsRecord snap {};
  snap.cache_space_free_start_mib = -1;
  snap.cache_space_free_end_mib = -1;
  {
    OdrMetrics metrics(GetCacheDirectory(), GetMetricsFilePath());
    metrics.SetArtApexVersion(1999);
    metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
    metrics.SetStage(OdrMetrics::Stage::kCheck);
    metrics.SetStatus(OdrMetrics::Status::kOK);
    EXPECT_TRUE(metrics.ToRecord(&snap));
    EXPECT_NE(0, snap.cache_space_free_start_mib);
    EXPECT_EQ(0, snap.cache_space_free_end_mib);
  }

  OdrMetricsRecord on_disk;
  std::ifstream ifs(GetMetricsFilePath());
  EXPECT_TRUE(ifs);
  ifs >> on_disk;
  EXPECT_TRUE(ifs);
  EXPECT_EQ(snap.cache_space_free_start_mib, on_disk.cache_space_free_start_mib);
  EXPECT_NE(0, on_disk.cache_space_free_end_mib);
}

}  // namespace odrefresh
}  // namespace art
