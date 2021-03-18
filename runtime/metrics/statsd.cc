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

#include "statsd.h"

#include "base/compiler_filter.h"
#include "base/metrics/metrics.h"
#include "statslog_art.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {

namespace {

// EncodeDatumId returns a std::optional that provides a enum value from atoms.proto if the datum is
// one that we support logging to statsd. The list of datums that ART collects is a superset of what
// we report to statsd. Therefore, we only have mappings for the DatumIds that statsd recognizes.
//
// Other code can use whether the result of this function has a value to decide whether to report
// the atom to statsd.
//
// To report additional measurements to statsd, first add an entry in atoms.proto and then add an
// entry to this function as well.
constexpr std::optional<int32_t> EncodeDatumId(DatumId datum_id) {
  switch (datum_id) {
    case DatumId::kClassVerificationTotalTime:
      return std::make_optional(
          statsd::ART_DATUM_REPORTED__KIND__ART_DATUM_CLASS_VERIFICATION_TIME);
    case DatumId::kJitMethodCompileTime:
      return std::make_optional(
          statsd::ART_DATUM_REPORTED__KIND__ART_DATUM_JIT_METHOD_COMPILE_TIME);
    default:
      return std::nullopt;
  }
}

constexpr int32_t EncodeCompileFilter(std::optional<CompilerFilter::Filter> filter) {
  if (filter.has_value()) {
    switch (filter.value()) {
      case CompilerFilter::kAssumeVerified:
        return statsd::ART_DATUM_REPORTED__COMPILE_FILTER__ART_COMPILATION_FILTER_ASSUMED_VERIFIED;
      case CompilerFilter::kExtract:
        return statsd::ART_DATUM_REPORTED__COMPILE_FILTER__ART_COMPILATION_FILTER_EXTRACT;
      case CompilerFilter::kVerify:
        return statsd::ART_DATUM_REPORTED__COMPILE_FILTER__ART_COMPILATION_FILTER_VERIFY;
      case CompilerFilter::kSpaceProfile:
        return statsd::ART_DATUM_REPORTED__COMPILE_FILTER__ART_COMPILATION_FILTER_SPACE_PROFILE;
      case CompilerFilter::kSpace:
        return statsd::ART_DATUM_REPORTED__COMPILE_FILTER__ART_COMPILATION_FILTER_SPACE;
      case CompilerFilter::kSpeedProfile:
        return statsd::ART_DATUM_REPORTED__COMPILE_FILTER__ART_COMPILATION_FILTER_SPEED_PROFILE;
      case CompilerFilter::kSpeed:
        return statsd::ART_DATUM_REPORTED__COMPILE_FILTER__ART_COMPILATION_FILTER_SPEED;
      case CompilerFilter::kEverythingProfile:
        return statsd::
            ART_DATUM_REPORTED__COMPILE_FILTER__ART_COMPILATION_FILTER_EVERYTHING_PROFILE;
      case CompilerFilter::kEverything:
        return statsd::ART_DATUM_REPORTED__COMPILE_FILTER__ART_COMPILATION_FILTER_EVERYTHING;
    }
  } else {
    return statsd::ART_DATUM_REPORTED__COMPILE_FILTER__ART_COMPILATION_FILTER_UNKNOWN;
  }
}

constexpr int32_t EncodeCompilationReason(CompilationReason reason) {
  switch (reason) {
    case CompilationReason::kUnknown:
      return statsd::ART_DATUM_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_UNKNOWN;
    case CompilationReason::kABOTA:
      return statsd::ART_DATUM_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_AB_OTA;
    case CompilationReason::kBgDexopt:
      return statsd::ART_DATUM_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_BG_DEXOPT;
    case CompilationReason::kBoot:
      return statsd::ART_DATUM_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_BOOT;
    case CompilationReason::kError:
      return statsd::ART_DATUM_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_ERROR;
    case CompilationReason::kFirstBoot:
      return statsd::ART_DATUM_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_FIRST_BOOT;
    case CompilationReason::kInactive:
      return statsd::ART_DATUM_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INACTIVE;
    case CompilationReason::kInstall:
      return statsd::ART_DATUM_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL;
    case CompilationReason::kInstallWithDexMetadata:
      return statsd::
          ART_DATUM_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_WITH_DEX_METADATA;
    case CompilationReason::kShared:
      return statsd::ART_DATUM_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_SHARED;
  }
}

class StatsdBackend : public MetricsBackend {
 public:
  void BeginSession(const SessionData& session_data) override { session_data_ = session_data; }

 protected:
  void BeginReport(uint64_t timestamp_since_start_ms) override {
    current_timestamp_ = static_cast<int64_t>(timestamp_since_start_ms);
  }

  void ReportCounter(DatumId counter_type, uint64_t value) override {
    std::optional<int32_t> datum_id = EncodeDatumId(counter_type);
    if (datum_id.has_value()) {
      statsd::stats_write(
          statsd::ART_DATUM_REPORTED,
          session_data_.session_id,
          session_data_.uid,
          EncodeCompileFilter(session_data_.compiler_filter),
          EncodeCompilationReason(session_data_.compilation_reason),
          current_timestamp_,
          /*thread_type=*/0,  // TODO: collect and report thread type (0 means UNKNOWN, but that
                              // constant is not present in all branches)
          datum_id.value(),
          static_cast<int64_t>(value),
          /*dex_metadata_type=*/0);  // Update the value once the enum is changed
    }
  }

  void ReportHistogram(DatumId /*histogram_type*/,
                       int64_t /*low_value*/,
                       int64_t /*high_value*/,
                       const std::vector<uint32_t>& /*buckets*/) override {
    // TODO: implement this once ArtDatumReported in atoms.proto supports histograms.
    LOG_STREAM(DEBUG) << "Attempting to write histogram to statsd. This is not supported yet.";
  }

  void EndReport() override {}

 private:
  SessionData session_data_;
  // The timestamp provided to the last call to BeginReport
  int64_t current_timestamp_;
};

}  // namespace

std::unique_ptr<MetricsBackend> CreateStatsdBackend() { return std::make_unique<StatsdBackend>(); }

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
