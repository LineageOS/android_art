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

#include "gtest/gtest.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {

class MetricsTest : public testing::Test {};

namespace {

// MetricsHistogram::GetBuckets is protected because we only want limited places to be able to read
// the buckets. Tests are one of those places, but because making a gTest a friend is difficult,
// instead we subclass MetricsHistogram and expose GetBuckets as GetBucksForTest.
template <size_t num_buckets, int64_t low_value, int64_t high_value>
class TestMetricsHistogram : public MetricsHistogram<num_buckets, low_value, high_value> {
 public:
  std::vector<uint32_t> GetBucketsForTest() const { return this->GetBuckets(); }
};

// A trivial MetricsBackend that does nothing for all of the members. This can be overridden by
// test cases to test specific behaviors.
class TestBackendBase : public MetricsBackend {
 public:
  void BeginSession([[maybe_unused]] const SessionData& session_data) override {}
  void EndSession() override {}

  void ReportCounter([[maybe_unused]] DatumId counter_type,
                     [[maybe_unused]] uint64_t value) override {}

  void ReportHistogram([[maybe_unused]] DatumId histogram_type,
                       [[maybe_unused]] int64_t low_value_,
                       [[maybe_unused]] int64_t high_value,
                       [[maybe_unused]] const std::vector<uint32_t>& buckets) override {}
};

}  // namespace

TEST_F(MetricsTest, SimpleCounter) {
  MetricsCounter test_counter;

  EXPECT_EQ(0u, test_counter.Value());

  test_counter.AddOne();
  EXPECT_EQ(1u, test_counter.Value());

  test_counter.Add(5);
  EXPECT_EQ(6u, test_counter.Value());
}

TEST_F(MetricsTest, DatumName) {
  EXPECT_EQ("ClassVerificationTotalTime", DatumName(DatumId::kClassVerificationTotalTime));
}

TEST_F(MetricsTest, SimpleHistogramTest) {
  TestMetricsHistogram<5, 0, 100> histogram;

  // bucket 0: 0-19
  histogram.Add(10);

  // bucket 1: 20-39
  histogram.Add(20);
  histogram.Add(25);

  // bucket 2: 40-59
  histogram.Add(56);
  histogram.Add(57);
  histogram.Add(58);
  histogram.Add(59);

  // bucket 3: 60-79
  histogram.Add(70);
  histogram.Add(70);
  histogram.Add(70);

  // bucket 4: 80-99
  // leave this bucket empty

  std::vector<uint32_t> buckets{histogram.GetBucketsForTest()};
  EXPECT_EQ(1u, buckets[0u]);
  EXPECT_EQ(2u, buckets[1u]);
  EXPECT_EQ(4u, buckets[2u]);
  EXPECT_EQ(3u, buckets[3u]);
  EXPECT_EQ(0u, buckets[4u]);
}

// Make sure values added outside the range of the histogram go into the first or last bucket.
TEST_F(MetricsTest, HistogramOutOfRangeTest) {
  TestMetricsHistogram<2, 0, 100> histogram;

  // bucket 0: 0-49
  histogram.Add(-500);

  // bucket 1: 50-99
  histogram.Add(250);
  histogram.Add(1000);

  std::vector<uint32_t> buckets{histogram.GetBucketsForTest()};
  EXPECT_EQ(1u, buckets[0u]);
  EXPECT_EQ(2u, buckets[1u]);
}

// Test adding values to ArtMetrics and reporting them through a test backend.
TEST_F(MetricsTest, ArtMetricsReport) {
  ArtMetrics metrics;

  // Collect some data
  static constexpr uint64_t verification_time = 42;
  metrics.ClassVerificationTotalTime()->Add(verification_time);
  // Add a negative value so we are guaranteed that it lands in the first bucket.
  metrics.JitMethodCompileTime()->Add(-5);

  // Report and check the data
  class TestBackend : public TestBackendBase {
   public:
    ~TestBackend() {
      EXPECT_TRUE(found_counter_);
      EXPECT_TRUE(found_histogram_);
    }

    void ReportCounter(DatumId counter_type, uint64_t value) override {
      if (counter_type == DatumId::kClassVerificationTotalTime) {
        EXPECT_EQ(value, verification_time);
        found_counter_ = true;
      } else {
        EXPECT_EQ(value, 0u);
      }
    }

    void ReportHistogram(DatumId histogram_type,
                         int64_t,
                         int64_t,
                         const std::vector<uint32_t>& buckets) override {
      if (histogram_type == DatumId::kJitMethodCompileTime) {
        EXPECT_EQ(buckets[0], 1u);
        for (size_t i = 1; i < buckets.size(); ++i) {
          EXPECT_EQ(buckets[i], 0u);
        }
        found_histogram_ = true;
      } else {
        for (size_t i = 0; i < buckets.size(); ++i) {
          EXPECT_EQ(buckets[i], 0u);
        }
      }
    }

   private:
    bool found_counter_{false};
    bool found_histogram_{false};
  } backend;

  metrics.ReportAllMetrics(&backend);
}

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
