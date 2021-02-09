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

// We use invalid sentinel values since we currently do not have a way to leave fields in the proto
// missing.
constexpr int64_t kInvalidSessionId = -1;
constexpr int32_t kInvalidUid = -1;
constexpr int64_t kInvalidTimestamp = -1;
// kInvalidThreadType is set to 3, which is the value of the next enum entry in ArtThreadType in
// atoms.proto. This means any atoms reported using this value should be forward compatible.
constexpr int32_t kInvalidThreadType = 3;
constexpr int64_t kInvalidValue = -1;

// Encapsulates an ART Atom that will be passed to statsd.
//
// We create our own wrapper class because the generated stats_write is kind of unwieldy when your
// atom has a lot of fields.
//
// This class must be kept in sync with the ArtDatumReported definition in atoms.proto.
class Atom {
 public:
  Atom& SetSessionId(int64_t session_id) {
    session_id_ = session_id;
    return *this;
  }

  Atom& SetUid(int32_t uid) {
    uid_ = uid;
    return *this;
  }

  Atom& SetCompilerFilter(CompilerFilter::Filter filter) {
    compile_filter_ = filter;
    return *this;
  }

  Atom& SetCompilationReason(CompilationReason reason) {
    compilation_reason_ = reason;
    return *this;
  }

  Atom& SetTimestampMillis(int64_t timestamp) {
    timestamp_millis_ = timestamp;
    return *this;
  }

  Atom& SetThreadType(ThreadType thread_type) {
    thread_type_ = thread_type;
    return *this;
  }

  Atom& SetDatumId(DatumId kind) {
    datum_id_ = kind;
    return *this;
  }

  Atom& SetValue(int64_t value) {
    value_ = value;
    return *this;
  }

  void WriteToStatsd() const {
    // We might not have an int32_t mapping for the current DatumId because statsd supports a subset
    // of what ART measures. In the case that there is no mapped value, we omit writing this atom.
    const std::optional<int32_t> datum_id = EncodeDatumId();
    if (datum_id.has_value()) {
      statsd::stats_write(statsd::ART_DATUM_REPORTED,
                          EncodeSessionId(),
                          EncodeUid(),
                          EncodeCompileFilter(),
                          EncodeCompilationReason(),
                          EncodeTimestamp(),
                          EncodeThreadType(),
                          datum_id.value(),
                          EncodeValue());
    }
  }

 private:
  // Everything is an optional because all fields in protos are optional.
  std::optional<int64_t> session_id_;
  std::optional<int32_t> uid_;
  std::optional<CompilerFilter::Filter> compile_filter_;
  std::optional<CompilationReason> compilation_reason_;
  std::optional<int64_t> timestamp_millis_;
  std::optional<ThreadType> thread_type_;
  std::optional<DatumId> datum_id_;
  std::optional<int64_t> value_;

  // The following encode methods convert values into the format that is needed for sending to
  // statsd.

  constexpr int64_t EncodeSessionId() const { return session_id_.value_or(kInvalidSessionId); }

  constexpr int32_t EncodeUid() const { return uid_.value_or(kInvalidUid); }

  constexpr int64_t EncodeTimestamp() const {
    return timestamp_millis_.value_or(kInvalidTimestamp);
  }

  constexpr int64_t EncodeValue() const { return value_.value_or(kInvalidValue); }

  constexpr int32_t EncodeCompileFilter() const {
    if (compile_filter_.has_value()) {
      switch (compile_filter_.value()) {
        case CompilerFilter::kAssumeVerified:
          return statsd::
              ART_DATUM_REPORTED__COMPILE_FILTER__ART_COMPILATION_FILTER_ASSUMED_VERIFIED;
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

  constexpr int32_t EncodeCompilationReason() const {
    if (compilation_reason_.has_value()) {
      switch (compilation_reason_.value()) {
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
    } else {
      return statsd::ART_DATUM_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_UNKNOWN;
    }
  }

  // EncodeDatumId is slightly special in that it returns a std::optional instead of the value
  // directly. The reason is that the list of datums that ART collects is a superset of what we
  // report to statsd. Therefore, we only have mappings for the DatumIds that statsd recognizes.
  //
  // Other code can use whether the result of this function has a value to decide whether to report
  // the atom to statsd.
  constexpr std::optional<int32_t> EncodeDatumId() const {
    if (datum_id_.has_value()) {
      switch (datum_id_.value()) {
        case DatumId::kClassVerificationTotalTime:
          return std::make_optional(
              statsd::ART_DATUM_REPORTED__KIND__ART_DATUM_CLASS_VERIFICATION_TIME);
        case DatumId::kJitMethodCompileTime:
          return std::make_optional(
              statsd::ART_DATUM_REPORTED__KIND__ART_DATUM_JIT_METHOD_COMPILE_TIME);
        default:
          return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }

  constexpr int32_t EncodeThreadType() const {
    if (thread_type_.has_value()) {
      switch (thread_type_.value()) {
        case ThreadType::kBackground:
          return statsd::ART_DATUM_REPORTED__THREAD_TYPE__ART_THREAD_BACKGROUND;
        case ThreadType::kMain:
          return statsd::ART_DATUM_REPORTED__THREAD_TYPE__ART_THREAD_MAIN;
      }
    } else {
      // TODO: Add an invalid entry to the ArtThreadType enum in atoms.proto and use that here.
      return kInvalidThreadType;
    }
  }
};

class StatsdBackend : public MetricsBackend {
 public:
  void BeginSession(const SessionData& session_data) override { session_data_ = session_data; }

 protected:
  void BeginReport(uint64_t timestamp_since_start_ms) override {
    current_atom_ = {};
    current_atom_.SetTimestampMillis(static_cast<int64_t>(timestamp_since_start_ms))
        .SetCompilationReason(session_data_.compilation_reason)
        .SetSessionId(session_data_.session_id)
        .SetUid(session_data_.uid);
    if (session_data_.compiler_filter.has_value()) {
      current_atom_.SetCompilerFilter(session_data_.compiler_filter.value());
    }
  }

  void ReportCounter(DatumId counter_type, uint64_t value) override {
    current_atom_.SetDatumId(counter_type).SetValue(static_cast<int64_t>(value)).WriteToStatsd();
  }

  void ReportHistogram([[maybe_unused]] DatumId histogram_type,
                       [[maybe_unused]] int64_t low_value,
                       [[maybe_unused]] int64_t high_value,
                       [[maybe_unused]] const std::vector<uint32_t>& buckets) override {
    // TODO: implement this once ArtDatumReported in atoms.proto supports histograms.
  }

  void EndReport() override {}

 private:
  SessionData session_data_;
  Atom current_atom_;
};

}  // namespace

std::unique_ptr<MetricsBackend> CreateStatsdBackend() { return std::make_unique<StatsdBackend>(); }

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
