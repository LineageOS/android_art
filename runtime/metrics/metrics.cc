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
#include "thread-current-inl.h"

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

std::vector<uint32_t> MetricsBackend::CumulativeBuckets(
    const std::vector<uint32_t>& buckets) const {
  std::vector<uint32_t> cumulative_buckets(buckets.size() + 1);
  uint32_t total_count = 0;
  size_t i = 0;
  for (auto& bucket : buckets) {
    cumulative_buckets[i++] = bucket + total_count;
    total_count += bucket;
  }
  cumulative_buckets[i] = total_count;
  return cumulative_buckets;
}

int64_t MetricsBackend::HistogramPercentile(double percentile,
                                            int64_t minimum_value,
                                            int64_t maximum_value,
                                            const std::vector<uint32_t>& cumulative_buckets) const {
  const uint32_t total_count = cumulative_buckets[cumulative_buckets.size() - 1];
  // Find which bucket has the right percentile
  const uint32_t percentile_count = static_cast<uint32_t>(percentile * total_count);
  size_t bucket_index;
  // We could use a binary search here, but that complicates the code and linear search is usually
  // faster for up to 100 elements, and our histograms should normally have less than 100 buckets.
  for (bucket_index = 0; bucket_index < cumulative_buckets.size(); bucket_index++) {
    if (cumulative_buckets[bucket_index] > percentile_count) {
      break;
    }
  }
  // Find the bounds in both count and percentile of the bucket we landed in.
  const uint32_t lower_count = bucket_index > 0 ? cumulative_buckets[bucket_index] : 0;
  const uint32_t upper_count = cumulative_buckets[bucket_index];

  const double lower_percentile = static_cast<double>(lower_count) / total_count;
  const double upper_percentile = static_cast<double>(upper_count) / total_count;
  const double width_percentile = upper_percentile - lower_percentile;

  // Compute what values the bucket covers
  const size_t num_buckets = cumulative_buckets.size() - 1;
  const int64_t bucket_width = (maximum_value - minimum_value) / static_cast<int64_t>(num_buckets);
  const int64_t lower_value = bucket_width * static_cast<int64_t>(bucket_index);
  const int64_t upper_value = lower_value + bucket_width;

  // Now linearly interpolate a value between lower_value and upper_value.
  const double in_bucket_location = (percentile - lower_percentile) / width_percentile;
  return static_cast<int64_t>(static_cast<double>(lower_value + upper_value) * in_bucket_location);
}

std::pair<int64_t, int64_t> MetricsBackend::HistogramConfidenceInterval(
    double interval,
    int64_t minimum_value,
    int64_t maximum_value,
    const std::vector<uint32_t>& cumulative_buckets) const {
  const double lower_percentile = (1.0 - interval) / 2.0;
  const double upper_percentile = lower_percentile + interval;

  return std::make_pair(
      HistogramPercentile(lower_percentile, minimum_value, maximum_value, cumulative_buckets),
      HistogramPercentile(upper_percentile, minimum_value, maximum_value, cumulative_buckets));
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
                                    int64_t minimum_value,
                                    int64_t maximum_value,
                                    const std::vector<uint32_t>& buckets) {
  os_ << DatumName(histogram_type) << ": range = " << minimum_value << "..." << maximum_value
      << "\n";
  std::vector<uint32_t> cumulative_buckets = CumulativeBuckets(buckets);
  std::pair<int64_t, int64_t> confidence_interval = HistogramConfidenceInterval(
      /*interval=*/0.99, minimum_value, maximum_value, cumulative_buckets);
  os_ << "  99% confidence interval: " << confidence_interval.first << "..."
      << confidence_interval.second << "\n";

  if (buckets.size() > 0) {
    os_ << "  buckets: ";
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

std::unique_ptr<MetricsReporter> MetricsReporter::Create(ReportingConfig config, Runtime* runtime) {
  // We can't use std::make_unique here because the MetricsReporter constructor is private.
  return std::unique_ptr<MetricsReporter>{new MetricsReporter{std::move(config), runtime}};
}

MetricsReporter::MetricsReporter(ReportingConfig config, Runtime* runtime)
    : config_{std::move(config)}, runtime_{runtime} {}

MetricsReporter::~MetricsReporter() { MaybeStopBackgroundThread(); }

void MetricsReporter::MaybeStartBackgroundThread() {
  if (config_.BackgroundReportingEnabled()) {
    CHECK(!thread_.has_value());

    thread_.emplace(&MetricsReporter::BackgroundThreadRun, this);
  }
}

void MetricsReporter::MaybeStopBackgroundThread() {
  if (thread_.has_value()) {
    messages_.SendMessage(ShutdownRequestedMessage{});
    thread_->join();
  }
  // Do one final metrics report, if enabled.
  if (config_.report_metrics_on_shutdown) {
    ReportMetrics();
  }
}

void MetricsReporter::BackgroundThreadRun() {
  LOG_STREAM(DEBUG) << "Metrics reporting thread started";

  // AttachCurrentThread is needed so we can safely use the ART concurrency primitives within the
  // messages_ MessageQueue.
  runtime_->AttachCurrentThread(kBackgroundThreadName,
                                /*as_daemon=*/true,
                                runtime_->GetSystemThreadGroup(),
                                /*create_peer=*/true);
  bool running = true;

  MaybeResetTimeout();

  while (running) {
    messages_.SwitchReceive(
        [&]([[maybe_unused]] ShutdownRequestedMessage message) {
          LOG_STREAM(DEBUG) << "Shutdown request received";
          running = false;
        },
        [&]([[maybe_unused]] TimeoutExpiredMessage message) {
          LOG_STREAM(DEBUG) << "Timer expired, reporting metrics";

          ReportMetrics();

          MaybeResetTimeout();
        });
  }

  runtime_->DetachCurrentThread();
  LOG_STREAM(DEBUG) << "Metrics reporting thread terminating";
}

void MetricsReporter::MaybeResetTimeout() {
  if (config_.periodic_report_seconds.has_value()) {
    messages_.SetTimeout(SecondsToMs(config_.periodic_report_seconds.value()));
  }
}

void MetricsReporter::ReportMetrics() const {
  if (config_.dump_to_logcat) {
    LOG_STREAM(INFO) << "\n*** ART internal metrics ***\n\n";
    // LOG_STREAM(INFO) destroys the stream at the end of the statement, which makes it tricky pass
    // it to store as a field in StreamBackend. To get around this, we use an immediately-invoked
    // lambda expression to act as a let-binding, letting us access the stream for long enough to
    // dump the metrics.
    [this](std::ostream& os) {
      StreamBackend backend{os};
      runtime_->GetMetrics()->ReportAllMetrics(&backend);
    }(LOG_STREAM(INFO));
    LOG_STREAM(INFO) << "\n*** Done dumping ART internal metrics ***\n";
  }
  if (config_.dump_to_file.has_value()) {
    const auto& filename = config_.dump_to_file.value();
    std::ostringstream stream;
    StreamBackend backend{stream};
    runtime_->GetMetrics()->ReportAllMetrics(&backend);

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
      .report_metrics_on_shutdown = !args.Exists(M::DisableFinalMetricsReport),
      .periodic_report_seconds = args.GetOptional(M::MetricsReportingPeriod),
  };
}

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
