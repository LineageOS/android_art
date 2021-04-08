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

#ifndef ART_ODREFRESH_ODR_METRICS_H_
#define ART_ODREFRESH_ODR_METRICS_H_

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>

#include "base/macros.h"
#include "odr_metrics_record.h"

namespace art {
namespace odrefresh {

class OdrMetrics final {
 public:
  // Enumeration used to track the latest stage reached running odrefresh.
  //
  // These values mirror those in OdrefreshReported::Stage in frameworks/proto_logging/atoms.proto.
  // NB There are gaps between the values in case an additional stages are introduced.
  enum class Stage : uint8_t {
    kUnknown = 0,
    kCheck = 10,
    kPreparation = 20,
    kPrimaryBootClasspath = 30,
    kSecondaryBootClasspath = 40,
    kSystemServerClasspath = 50,
    kComplete = 60,
  };

  // Enumeration describing the overall status, processing stops on the first error discovered.
  //
  // These values mirror those in OdrefreshReported::Status in frameworks/proto_logging/atoms.proto.
  enum class Status : uint8_t {
    kUnknown = 0,
    kOK = 1,
    kNoSpace = 2,
    kIoError = 3,
    kDex2OatError = 4,
    kTimeLimitExceeded = 5,
    kStagingFailed = 6,
    kInstallFailed = 7,
  };

  // Enumeration describing the cause of compilation (if any) in odrefresh.
  //
  // These values mirror those in OdrefreshReported::Trigger in
  // frameworks/proto_logging/atoms.proto.
  enum class Trigger : uint8_t {
    kUnknown = 0,
    kApexVersionMismatch = 1,
    kDexFilesChanged = 2,
    kMissingArtifacts = 3,
  };

  explicit OdrMetrics(const std::string& cache_directory,
                      const std::string& metrics_file = kOdrefreshMetricsFile);
  ~OdrMetrics();

  // Sets the ART APEX that metrics are being collected on behalf of.
  void SetArtApexVersion(int64_t version) {
    art_apex_version_ = version;
  }

  // Sets the trigger for metrics collection. The trigger is the reason why odrefresh considers
  // compilation necessary. Only call this method if compilation is necessary as the presence
  // of a trigger means we will try to record and upload metrics.
  void SetTrigger(const Trigger trigger) {
    trigger_ = trigger;
  }

  // Sets the execution status of the current odrefresh processing stage.
  void SetStatus(const Status status) {
    status_ = status;
  }

  // Sets the current odrefresh processing stage.
  void SetStage(Stage stage);

  // Record metrics into an OdrMetricsRecord.
  // returns true on success, false if instance is not valid (because the trigger value is not set).
  bool ToRecord(/*out*/OdrMetricsRecord* record) const;

 private:
  OdrMetrics(const OdrMetrics&) = delete;
  OdrMetrics operator=(const OdrMetrics&) = delete;

  static int32_t GetFreeSpaceMiB(const std::string& path);
  static void WriteToFile(const std::string& path, const OdrMetrics* metrics);

  void SetCompilationTime(int32_t seconds);

  const std::string cache_directory_;
  const std::string metrics_file_;

  int64_t art_apex_version_ = 0;
  std::optional<Trigger> trigger_ = {};  // metrics are only logged if compilation is triggered.
  Stage stage_ = Stage::kUnknown;
  Status status_ = Status::kUnknown;

  int32_t primary_bcp_compilation_seconds_ = 0;
  int32_t secondary_bcp_compilation_seconds_ = 0;
  int32_t system_server_compilation_seconds_ = 0;
  int32_t cache_space_free_start_mib_ = 0;
  int32_t cache_space_free_end_mib_ = 0;

  friend class ScopedOdrCompilationTimer;
};

// Timer used to measure compilation time (in seconds). Automatically associates the time recorded
// with the current stage of the metrics used.
class ScopedOdrCompilationTimer final {
 public:
  explicit ScopedOdrCompilationTimer(OdrMetrics& metrics) :
    metrics_(metrics), start_(std::chrono::steady_clock::now()) {}

  ~ScopedOdrCompilationTimer() {
    auto elapsed_time = std::chrono::steady_clock::now() - start_;
    auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed_time);
    metrics_.SetCompilationTime(static_cast<int32_t>(elapsed_seconds.count()));
  }

 private:
  OdrMetrics& metrics_;
  std::chrono::time_point<std::chrono::steady_clock> start_;

  DISALLOW_ALLOCATION();
};

// Generated ostream operators.
std::ostream& operator<<(std::ostream& os, OdrMetrics::Status status);
std::ostream& operator<<(std::ostream& os, OdrMetrics::Stage stage);
std::ostream& operator<<(std::ostream& os, OdrMetrics::Trigger trigger);

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_ODR_METRICS_H_
