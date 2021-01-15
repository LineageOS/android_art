/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "metrics.h"

#include <sstream>

#include "android-base/file.h"
#include "android-base/logging.h"
#include "base/macros.h"
#include "base/scoped_flock.h"
#include "runtime.h"
#include "runtime_options.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

using android::base::WriteStringToFd;

namespace art {
namespace metrics {

std::string DatumName(DatumId datum) {
  switch (datum) {
#define ART_COUNTER(name) \
  case DatumId::k##name:  \
    return #name;
    ART_COUNTERS(ART_COUNTER)
#undef ART_COUNTER

#define ART_HISTOGRAM(name, num_buckets, low_value, high_value) \
  case DatumId::k##name:                                        \
    return #name;
    ART_HISTOGRAMS(ART_HISTOGRAM)
#undef ART_HISTOGRAM

    default:
      LOG(FATAL) << "Unknown datum id: " << static_cast<unsigned>(datum);
      UNREACHABLE();
  }
}

ArtMetrics::ArtMetrics() : unused_ {}
#define ART_COUNTER(name) \
  , name##_ {}
ART_COUNTERS(ART_COUNTER)
#undef ART_COUNTER
#define ART_HISTOGRAM(name, num_buckets, low_value, high_value) \
  , name##_ {}
ART_HISTOGRAMS(ART_HISTOGRAM)
#undef ART_HISTOGRAM
{
}

void ArtMetrics::ReportAllMetrics(MetricsBackend* backend) const {
// Dump counters
#define ART_COUNTER(name) name()->Report(backend);
  ART_COUNTERS(ART_COUNTER)
#undef ART_COUNTERS

// Dump histograms
#define ART_HISTOGRAM(name, num_buckets, low_value, high_value) name()->Report(backend);
  ART_HISTOGRAMS(ART_HISTOGRAM)
#undef ART_HISTOGRAM
}

void ArtMetrics::DumpForSigQuit(std::ostream& os) const {
  os << "\n*** ART internal metrics ***\n\n";
  StreamBackend backend{os};
  ReportAllMetrics(&backend);
  os << "\n*** Done dumping ART internal metrics ***\n";
}

StreamBackend::StreamBackend(std::ostream& os) : os_{os} {}

void StreamBackend::BeginSession([[maybe_unused]] const SessionData& session_data) {
  // Not needed for now.
}

void StreamBackend::EndSession() {
  // Not needed for now.
}

void StreamBackend::ReportCounter(DatumId counter_type, uint64_t value) {
  os_ << DatumName(counter_type) << ": count = " << value << "\n";
}

void StreamBackend::ReportHistogram(DatumId histogram_type,
                                    int64_t minimum_value_,
                                    int64_t maximum_value_,
                                    const std::vector<uint32_t>& buckets) {
  os_ << DatumName(histogram_type) << ": range = " << minimum_value_ << "..." << maximum_value_;
  if (buckets.size() > 0) {
    os_ << ", buckets: ";
    bool first = true;
    for (const auto& count : buckets) {
      if (!first) {
        os_ << ",";
      }
      first = false;
      os_ << count;
    }
    os_ << "\n";
  } else {
    os_ << ", no buckets\n";
  }
}

std::unique_ptr<MetricsReporter> MetricsReporter::Create(ReportingConfig config,
                                                         const ArtMetrics* metrics) {
  std::unique_ptr<MetricsBackend> backend;

  // We can't use std::make_unique here because the MetricsReporter constructor is private.
  return std::unique_ptr<MetricsReporter>{new MetricsReporter{std::move(config), metrics}};
}

MetricsReporter::MetricsReporter(ReportingConfig config, const ArtMetrics* metrics)
    : config_{std::move(config)}, metrics_{metrics} {}

MetricsReporter::~MetricsReporter() {
  // If we are configured to report metrics, do one final report at the end.
  if (config_.ReportingEnabled()) {
    LOG_STREAM(INFO) << "\n*** ART internal metrics ***\n\n";
    // LOG_STREAM(INFO) destroys the stream at the end of the statement, which makes it tricky pass
    // it to store as a field in StreamBackend. To get around this, we use an immediately-invoked
    // lambda expression to act as a let-binding, letting us access the stream for long enough to
    // dump the metrics.
    [this](std::ostream& os) {
      StreamBackend backend{os};
      metrics_->ReportAllMetrics(&backend);
    }(LOG_STREAM(INFO));
    LOG_STREAM(INFO) << "\n*** Done dumping ART internal metrics ***\n";
  }
  if (config_.dump_to_file.has_value()) {
    const auto& filename = config_.dump_to_file.value();
    std::ostringstream stream;
    StreamBackend backend{stream};
    metrics_->ReportAllMetrics(&backend);

    std::string error_message;
    auto file{
        LockedFile::Open(filename.c_str(), O_CREAT | O_WRONLY | O_APPEND, true, &error_message)};
    if (file.get() == nullptr) {
      LOG(WARNING) << "Could open metrics file '" << filename << "': " << error_message;
    } else {
      if (!WriteStringToFd(stream.str(), file.get()->Fd())) {
        PLOG(WARNING) << "Error writing metrics to file";
      }
    }
  }
}

ReportingConfig ReportingConfig::FromRuntimeArguments(const RuntimeArgumentMap& args) {
  using M = RuntimeArgumentMap;
  return {
      .dump_to_logcat = args.Exists(M::WriteMetricsToLog),
      .dump_to_file = args.GetOptional(M::WriteMetricsToFile),
  };
}

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
