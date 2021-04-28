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


#include "odr_statslog/odr_statslog.h"

#include <cstdint>
#include <fstream>
#include <istream>
#include <string>

#include "android-base/logging.h"
#include "android-base/stringprintf.h"
#include "odr_metrics.h"
#include "odr_metrics_record.h"
#include "statslog_odrefresh.h"

namespace art {
namespace odrefresh {

using android::base::StringPrintf;

namespace {

// Convert bare value from art::metrics::Stage to value defined in atoms.proto.
int32_t TranslateStage(int32_t art_metrics_stage) {
  switch (static_cast<OdrMetrics::Stage>(art_metrics_stage)) {
    case OdrMetrics::Stage::kUnknown:
      return metrics::statsd::ODREFRESH_REPORTED__STAGE_REACHED__STAGE_UNKNOWN;
    case OdrMetrics::Stage::kCheck:
      return metrics::statsd::ODREFRESH_REPORTED__STAGE_REACHED__STAGE_CHECK;
    case OdrMetrics::Stage::kPreparation:
      return metrics::statsd::ODREFRESH_REPORTED__STAGE_REACHED__STAGE_PREPARATION;
    case OdrMetrics::Stage::kPrimaryBootClasspath:
      return metrics::statsd::ODREFRESH_REPORTED__STAGE_REACHED__STAGE_PRIMARY_BOOT_CLASSPATH;
    case OdrMetrics::Stage::kSecondaryBootClasspath:
      return metrics::statsd::ODREFRESH_REPORTED__STAGE_REACHED__STAGE_SECONDARY_BOOT_CLASSPATH;
    case OdrMetrics::Stage::kSystemServerClasspath:
      return metrics::statsd::ODREFRESH_REPORTED__STAGE_REACHED__STAGE_SYSTEM_SERVER_CLASSPATH;
    case OdrMetrics::Stage::kComplete:
      return metrics::statsd::ODREFRESH_REPORTED__STAGE_REACHED__STAGE_COMPLETE;
  }

  LOG(ERROR) << "Unknown stage value: " << art_metrics_stage;
  return -1;
}

// Convert bare value from art::metrics::Status to value defined in atoms.proto.
int32_t TranslateStatus(int32_t art_metrics_status) {
  switch (static_cast<OdrMetrics::Status>(art_metrics_status)) {
    case OdrMetrics::Status::kUnknown:
      return metrics::statsd::ODREFRESH_REPORTED__STATUS__STATUS_UNKNOWN;
    case OdrMetrics::Status::kOK:
      return metrics::statsd::ODREFRESH_REPORTED__STATUS__STATUS_OK;
    case OdrMetrics::Status::kNoSpace:
      return metrics::statsd::ODREFRESH_REPORTED__STATUS__STATUS_NO_SPACE;
    case OdrMetrics::Status::kIoError:
      return metrics::statsd::ODREFRESH_REPORTED__STATUS__STATUS_IO_ERROR;
    case OdrMetrics::Status::kDex2OatError:
      return metrics::statsd::ODREFRESH_REPORTED__STATUS__STATUS_DEX2OAT_ERROR;
    case OdrMetrics::Status::kTimeLimitExceeded:
      return metrics::statsd::ODREFRESH_REPORTED__STATUS__STATUS_TIME_LIMIT_EXCEEDED;
    case OdrMetrics::Status::kStagingFailed:
      return metrics::statsd::ODREFRESH_REPORTED__STATUS__STATUS_STAGING_FAILED;
    case OdrMetrics::Status::kInstallFailed:
      return metrics::statsd::ODREFRESH_REPORTED__STATUS__STATUS_INSTALL_FAILED;
  }

  LOG(ERROR) << "Unknown status value: " << art_metrics_status;
  return -1;
}

// Convert bare value from art::metrics::Trigger to value defined in atoms.proto.
int32_t TranslateTrigger(int32_t art_metrics_trigger) {
  switch (static_cast<OdrMetrics::Trigger>(art_metrics_trigger)) {
    case OdrMetrics::Trigger::kUnknown:
      return metrics::statsd::ODREFRESH_REPORTED__TRIGGER__TRIGGER_UNKNOWN;
    case OdrMetrics::Trigger::kApexVersionMismatch:
      return metrics::statsd::ODREFRESH_REPORTED__TRIGGER__TRIGGER_APEX_VERSION_MISMATCH;
    case OdrMetrics::Trigger::kDexFilesChanged:
      return metrics::statsd::ODREFRESH_REPORTED__TRIGGER__TRIGGER_DEX_FILES_CHANGED;
    case OdrMetrics::Trigger::kMissingArtifacts:
      return metrics::statsd::ODREFRESH_REPORTED__TRIGGER__TRIGGER_MISSING_ARTIFACTS;
  }

  LOG(ERROR) << "Unknown trigger value: " << art_metrics_trigger;
  return -1;
}

bool ReadValues(const char* metrics_file,
                /*out*/ OdrMetricsRecord* record,
                /*out*/ std::string* error_msg) {
  std::ifstream ifs(metrics_file);
  if (!ifs) {
    *error_msg = android::base::StringPrintf(
        "metrics file '%s' could not be opened: %s", metrics_file, strerror(errno));
    return false;
  }

  ifs >> *record;
  if (!ifs) {
    *error_msg = "file parsing error.";
    return false;
  }

  //
  // Convert values defined as enums to their statsd values.
  //

  record->trigger = TranslateTrigger(record->trigger);
  if (record->trigger < 0) {
    *error_msg = "failed to parse trigger.";
    return false;
  }

  record->stage_reached = TranslateStage(record->stage_reached);
  if (record->stage_reached < 0) {
    *error_msg = "failed to parse stage_reached.";
    return false;
  }

  record->status = TranslateStatus(record->status);
  if (record->status < 0) {
    *error_msg = "failed to parse status.";
    return false;
  }

  return true;
}

}  // namespace

bool UploadStatsIfAvailable(/*out*/std::string* error_msg) {
  OdrMetricsRecord record;
  if (!ReadValues(kOdrefreshMetricsFile, &record, error_msg)) {
    return false;
  }

  // Write values to statsd. The order of values passed is the same as the order of the
  // fields in OdrMetricsRecord.
  int bytes_written = art::metrics::statsd::stats_write(metrics::statsd::ODREFRESH_REPORTED,
                                                        record.art_apex_version,
                                                        record.trigger,
                                                        record.stage_reached,
                                                        record.status,
                                                        record.primary_bcp_compilation_seconds,
                                                        record.secondary_bcp_compilation_seconds,
                                                        record.system_server_compilation_seconds,
                                                        record.cache_space_free_start_mib,
                                                        record.cache_space_free_end_mib);
  if (bytes_written <= 0) {
    *error_msg = android::base::StringPrintf("stats_write returned %d", bytes_written);
    return false;
  }

  if (unlink(kOdrefreshMetricsFile) != 0) {
    *error_msg = StringPrintf("failed to unlink '%s': %s", kOdrefreshMetricsFile, strerror(errno));
    return false;
  }

  return true;
}

}  // namespace odrefresh
}  // namespace art
