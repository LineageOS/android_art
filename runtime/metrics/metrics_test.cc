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
#include "metrics_test.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {

using test::CounterValue;
using test::GetBuckets;
using test::TestBackendBase;

class MetricsTest : public testing::Test {};

TEST_F(MetricsTest, SimpleCounter) {
  MetricsCounter<DatumId::kClassVerificationTotalTime> test_counter;

  EXPECT_EQ(0u, CounterValue(test_counter));

  test_counter.AddOne();
  EXPECT_EQ(1u, CounterValue(test_counter));

  test_counter.Add(5);
  EXPECT_EQ(6u, CounterValue(test_counter));
}

TEST_F(MetricsTest, CounterTimer) {
  MetricsCounter<DatumId::kClassVerificationTotalTime> test_counter;
  {
    AutoTimer timer{&test_counter};
    // Sleep for 2µs so the counter will be greater than 0.
    NanoSleep(2'000);
  }
  EXPECT_GT(CounterValue(test_counter), 0u);
}

TEST_F(MetricsTest, CounterTimerExplicitStop) {
  MetricsCounter<DatumId::kClassVerificationTotalTime> test_counter;
  AutoTimer timer{&test_counter};
  // Sleep for 2µs so the counter will be greater than 0.
  NanoSleep(2'000);
  timer.Stop();
  EXPECT_GT(CounterValue(test_counter), 0u);
}

TEST_F(MetricsTest, CounterTimerExplicitStart) {
  MetricsCounter<DatumId::kClassVerificationTotalTime> test_counter;
  {
    AutoTimer timer{&test_counter, /*autostart=*/false};
    // Sleep for 2µs so the counter will be greater than 0.
    NanoSleep(2'000);
  }
  EXPECT_EQ(CounterValue(test_counter), 0u);

  {
    AutoTimer timer{&test_counter, /*autostart=*/false};
    timer.Start();
    // Sleep for 2µs so the counter will be greater than 0.
    NanoSleep(2'000);
  }
  EXPECT_GT(CounterValue(test_counter), 0u);
}

TEST_F(MetricsTest, CounterTimerExplicitStartStop) {
  MetricsCounter<DatumId::kClassVerificationTotalTime> test_counter;
  AutoTimer timer{&test_counter, /*autostart=*/false};
  // Sleep for 2µs so the counter will be greater than 0.
  timer.Start();
  NanoSleep(2'000);
  timer.Stop();
  EXPECT_GT(CounterValue(test_counter), 0u);
}

TEST_F(MetricsTest, DatumName) {
  EXPECT_EQ("ClassVerificationTotalTime", DatumName(DatumId::kClassVerificationTotalTime));
}

TEST_F(MetricsTest, SimpleHistogramTest) {
  MetricsHistogram<DatumId::kJitMethodCompileTime, 5, 0, 100> histogram;

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

  std::vector<uint32_t> buckets{GetBuckets(histogram)};
  EXPECT_EQ(1u, buckets[0u]);
  EXPECT_EQ(2u, buckets[1u]);
  EXPECT_EQ(4u, buckets[2u]);
  EXPECT_EQ(3u, buckets[3u]);
  EXPECT_EQ(0u, buckets[4u]);
}

// Make sure values added outside the range of the histogram go into the first or last bucket.
TEST_F(MetricsTest, HistogramOutOfRangeTest) {
  MetricsHistogram<DatumId::kJitMethodCompileTime, 2, 0, 100> histogram;

  // bucket 0: 0-49
  histogram.Add(-500);

  // bucket 1: 50-99
  histogram.Add(250);
  histogram.Add(1000);

  std::vector<uint32_t> buckets{GetBuckets(histogram)};
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

TEST_F(MetricsTest, HistogramTimer) {
  MetricsHistogram<DatumId::kJitMethodCompileTime, 1, 0, 100> test_histogram;
  {
    AutoTimer timer{&test_histogram};
    // Sleep for 2µs so the counter will be greater than 0.
    NanoSleep(2'000);
  }

  EXPECT_GT(GetBuckets(test_histogram)[0], 0u);
}

// Makes sure all defined metrics are included when dumping through StreamBackend.
TEST_F(MetricsTest, StreamBackendDumpAllMetrics) {
  ArtMetrics metrics;
  std::stringstream os;
  StreamBackend backend(os);

  metrics.ReportAllMetrics(&backend);

  // Make sure the resulting string lists all the counters.
#define COUNTER(name) \
  EXPECT_NE(os.str().find(DatumName(DatumId::k##name)), std::string::npos)
  ART_COUNTERS(COUNTER);
#undef COUNTER

  // Make sure the resulting string lists all the histograms.
#define HISTOGRAM(name, num_buckets, minimum_value, maximum_value) \
  EXPECT_NE(os.str().find(DatumName(DatumId::k##name)), std::string::npos)
  ART_HISTOGRAMS(HISTOGRAM);
#undef HISTOGRAM
}

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
