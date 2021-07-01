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

#ifndef ART_LIBARTBASE_BASE_METRICS_METRICS_TEST_H_
#define ART_LIBARTBASE_BASE_METRICS_METRICS_TEST_H_

#include "metrics.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {
namespace test {

// This namespace contains functions that are helpful for testing metrics but should not be used in
// production code.

// A trivial MetricsBackend that does nothing for all of the members. This can be overridden by
// test cases to test specific behaviors.
class TestBackendBase : public MetricsBackend {
 public:
  void BeginOrUpdateSession([[maybe_unused]] const SessionData& session_data) override {}

  void BeginReport([[maybe_unused]] uint64_t timestamp_since_start_ms) override {}

  void ReportCounter([[maybe_unused]] DatumId counter_type,
                     [[maybe_unused]] uint64_t value) override {}

  void ReportHistogram([[maybe_unused]] DatumId histogram_type,
                       [[maybe_unused]] int64_t low_value_,
                       [[maybe_unused]] int64_t high_value,
                       [[maybe_unused]] const std::vector<uint32_t>& buckets) override {}

  void EndReport() override {}
};

template <typename MetricType>
uint64_t CounterValue(const MetricType& counter) {
  uint64_t counter_value{0};
  struct CounterBackend : public TestBackendBase {
    explicit CounterBackend(uint64_t* counter_value) : counter_value_{counter_value} {}

    void ReportCounter(DatumId, uint64_t value) override { *counter_value_ = value; }

    uint64_t* counter_value_;
  } backend{&counter_value};
  counter.Report(&backend);
  return counter_value;
}

template <DatumId histogram_type, size_t num_buckets, int64_t low_value, int64_t high_value>
std::vector<uint32_t> GetBuckets(
    const MetricsHistogram<histogram_type, num_buckets, low_value, high_value>& histogram) {
  std::vector<uint32_t> buckets;
  struct HistogramBackend : public TestBackendBase {
    explicit HistogramBackend(std::vector<uint32_t>* buckets) : buckets_{buckets} {}

    void ReportHistogram(DatumId, int64_t, int64_t, const std::vector<uint32_t>& buckets) override {
      *buckets_ = buckets;
    }

    std::vector<uint32_t>* buckets_;
  } backend{&buckets};
  histogram.Report(&backend);
  return buckets;
}

}  // namespace test
}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion

#endif  // ART_LIBARTBASE_BASE_METRICS_METRICS_TEST_H_
