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

#include <sstream>

#include "android-base/logging.h"
#include "base/macros.h"
#include "metrics.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

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

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
