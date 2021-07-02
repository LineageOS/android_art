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

#include "reporter.h"

#include "gtest/gtest.h"

#include "common_runtime_test.h"
#include "base/metrics/metrics.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {

// Helper class to verify the metrics reporter.
// The functionality is identical to the MetricsReporter with the exception of
// the metrics source. Instead of taking its metrics from the current Runtime,
// this class will keep its own copy so that it does not get interference from
// other runtime setup logic.
class MockMetricsReporter : public MetricsReporter {
 protected:
  MockMetricsReporter(const ReportingConfig& config, Runtime* runtime) :
      MetricsReporter(config, runtime),
      art_metrics_(new ArtMetrics()) {}

  const ArtMetrics* GetMetrics() override {
    return art_metrics_.get();
  }

  std::unique_ptr<ArtMetrics> art_metrics_;

  friend class MetricsReporterTest;
};

// A test backend which keeps track of all metrics reporting.
class TestBackend : public MetricsBackend {
 public:
  struct Report {
    uint64_t timestamp_millis;
    SafeMap<DatumId, uint64_t> data;
  };

  void BeginOrUpdateSession(const SessionData& session_data) override {
    session_data_ = session_data;
  }

  void BeginReport(uint64_t timestamp_millis) override {
    current_report_.reset(new Report());
    current_report_->timestamp_millis = timestamp_millis;
  }

  void ReportCounter(DatumId counter_type, uint64_t value) override {
    current_report_->data.Put(counter_type, value);
  }

  void ReportHistogram(DatumId histogram_type ATTRIBUTE_UNUSED,
                       int64_t low_value ATTRIBUTE_UNUSED,
                       int64_t high_value ATTRIBUTE_UNUSED,
                       const std::vector<uint32_t>& buckets ATTRIBUTE_UNUSED) override {
    // TODO: nothing yet. We should implement and test histograms as well.
  }

  void EndReport() override {
    reports_.push_back(*current_report_);
    current_report_ = nullptr;
  }

  const std::vector<Report>& GetReports() {
    return reports_;
  }

  const SessionData& GetSessionData() {
    return session_data_;
  }

 private:
  SessionData session_data_;
  std::vector<Report> reports_;
  std::unique_ptr<Report> current_report_;
};

// The actual metrics test class
class MetricsReporterTest : public CommonRuntimeTest {
 protected:
  void SetUp() override {
    // Do the normal setup.
    CommonRuntimeTest::SetUp();

    // We need to start the runtime in order to run threads.
    Thread::Current()->TransitionFromSuspendedToRunnable();
    bool started = runtime_->Start();
    CHECK(started);
  }

  // Configures the metric reporting.
  void SetupReporter(const char* period_spec,
                     uint32_t session_id = 1,
                     uint32_t reporting_mods = 100) {
    ReportingConfig config;
    if (period_spec != nullptr) {
      std::string error;
      config.reporting_mods = reporting_mods;
      config.period_spec = ReportingPeriodSpec::Parse(period_spec, &error);
      ASSERT_TRUE(config.period_spec.has_value());
    }

    reporter_.reset(new MockMetricsReporter(std::move(config), Runtime::Current()));
    backend_ = new TestBackend();
    reporter_->backends_.emplace_back(backend_);

    session_data_ = metrics::SessionData::CreateDefault();
    session_data_.session_id = session_id;
  }

  void TearDown() override {
    reporter_ = nullptr;
    backend_ = nullptr;
  }

  bool ShouldReportAtStartup() {
    return reporter_->ShouldReportAtStartup();
  }

  bool ShouldContinueReporting() {
    return reporter_->ShouldContinueReporting();
  }

  uint32_t GetNextPeriodSeconds() {
    return reporter_->GetNextPeriodSeconds();
  }

  void ReportMetrics() {
    reporter_->ReportMetrics();
  }

  void NotifyStartupCompleted() {
    reporter_->NotifyStartupCompleted();
  }

  // Starts the reporting thread and adds some metrics if necessary.
  bool MaybeStartBackgroundThread(bool add_metrics) {
    // TODO: set session_data.compilation_reason and session_data.compiler_filter
    bool result = reporter_->MaybeStartBackgroundThread(session_data_);
    if (add_metrics) {
      reporter_->art_metrics_->JitMethodCompileCount()->Add(1);
      reporter_->art_metrics_->ClassVerificationCount()->Add(2);
    }
    return result;
  }

  // Right now we either:
  //   1) don't add metrics (with_metrics = false)
  //   2) or always add the same metrics (see MaybeStartBackgroundThread)
  // So we can write a global verify method.
  void VerifyReports(
        uint32_t size,
        bool with_metrics,
        CompilerFilterReporting filter = CompilerFilterReporting::kUnknown,
        CompilationReason reason = CompilationReason::kUnknown) {
    // TODO: we should iterate through all the other metrics to make sure they were not
    // reported. However, we don't have an easy to use iteration mechanism over metrics yet.
    // We should ads one
    ASSERT_EQ(backend_->GetReports().size(), size);
    for (auto report : backend_->GetReports()) {
      ASSERT_EQ(report.data.Get(DatumId::kClassVerificationCount), with_metrics ? 2u : 0u);
      ASSERT_EQ(report.data.Get(DatumId::kJitMethodCompileCount), with_metrics ? 1u : 0u);
    }

    ASSERT_EQ(backend_->GetSessionData().compiler_filter, filter);
    ASSERT_EQ(backend_->GetSessionData().compilation_reason, reason);
  }

  // Sleeps until the backend received the give number of reports.
  void WaitForReport(uint32_t report_count, uint32_t sleep_period_ms) {
    while (true) {
      if (backend_->GetReports().size() == report_count) {
        return;
      }
      usleep(sleep_period_ms * 1000);
    }
  }

  void NotifyAppInfoUpdated(AppInfo* app_info) {
    reporter_->NotifyAppInfoUpdated(app_info);
  }

 private:
  std::unique_ptr<MockMetricsReporter> reporter_;
  TestBackend* backend_;
  metrics::SessionData session_data_;
};

// Verifies startup reporting.
TEST_F(MetricsReporterTest, StartupOnly) {
  SetupReporter("S");

  // Verify startup conditions
  ASSERT_TRUE(ShouldReportAtStartup());
  ASSERT_FALSE(ShouldContinueReporting());

  // Start the thread and notify the startup. This will advance the state.
  MaybeStartBackgroundThread(/*add_metrics=*/ true);

  NotifyStartupCompleted();
  WaitForReport(/*report_count=*/ 1, /*sleep_period_ms=*/ 50);
  VerifyReports(/*size=*/ 1, /*with_metrics*/ true);

  // We still should not report at period.
  ASSERT_FALSE(ShouldContinueReporting());
}

// LARGE TEST: This test takes 1s to run.
// Verifies startup reporting, followed by a fixed, one time only reporting.
TEST_F(MetricsReporterTest, StartupAndPeriod) {
  SetupReporter("S,1");

  // Verify startup conditions
  ASSERT_TRUE(ShouldReportAtStartup());
  ASSERT_FALSE(ShouldContinueReporting());

  // Start the thread and notify the startup. This will advance the state.
  MaybeStartBackgroundThread(/*add_metrics=*/ true);
  NotifyStartupCompleted();

  // We're waiting for 2 reports: the startup one, and the 1s one.
  WaitForReport(/*report_count=*/ 2, /*sleep_period_ms=*/ 500);
  VerifyReports(/*size=*/ 2, /*with_metrics*/ true);

  // We should not longer report at period.
  ASSERT_FALSE(ShouldContinueReporting());
}

// LARGE TEST: This takes take 2s to run.
// Verifies startup reporting, followed by continuous reporting.
TEST_F(MetricsReporterTest, StartupAndPeriodContinuous) {
  SetupReporter("S,1,*");

  // Verify startup conditions
  ASSERT_TRUE(ShouldReportAtStartup());
  ASSERT_FALSE(ShouldContinueReporting());

  // Start the thread and notify the startup. This will advance the state.
  MaybeStartBackgroundThread(/*add_metrics=*/ true);
  NotifyStartupCompleted();

  // We're waiting for 3 reports: the startup one, and the 1s one.
  WaitForReport(/*report_count=*/ 3, /*sleep_period_ms=*/ 500);
  VerifyReports(/*size=*/ 3, /*with_metrics*/ true);

  // We should keep reporting at period.
  ASSERT_TRUE(ShouldContinueReporting());
}

// LARGE TEST: This test takes 1s to run.
// Verifies a fixed, one time only reporting.
TEST_F(MetricsReporterTest, OneTime) {
  SetupReporter("1");

  // Verify startup conditions
  ASSERT_FALSE(ShouldReportAtStartup());
  ASSERT_TRUE(ShouldContinueReporting());

  // Start the thread and notify the startup. This will advance the state.
  MaybeStartBackgroundThread(/*add_metrics=*/ true);

  // We're waiting for 1 report
  WaitForReport(/*report_count=*/ 1, /*sleep_period_ms=*/ 500);
  VerifyReports(/*size=*/ 1, /*with_metrics*/ true);

  // We should not longer report at period.
  ASSERT_FALSE(ShouldContinueReporting());
}

// LARGE TEST: This takes take 5s to run.
// Verifies a sequence of reporting, at different interval of times.
TEST_F(MetricsReporterTest, PeriodContinuous) {
  SetupReporter("1,2,*");

  // Verify startup conditions
  ASSERT_FALSE(ShouldReportAtStartup());
  ASSERT_TRUE(ShouldContinueReporting());

  // Start the thread and notify the startup. This will advance the state.
  MaybeStartBackgroundThread(/*add_metrics=*/ true);
  NotifyStartupCompleted();

  // We're waiting for 2 reports: the startup one, and the 1s one.
  WaitForReport(/*report_count=*/ 3, /*sleep_period_ms=*/ 500);
  VerifyReports(/*size=*/ 3, /*with_metrics*/ true);

  // We should keep reporting at period.
  ASSERT_TRUE(ShouldContinueReporting());
}

// LARGE TEST: This test takes 1s to run.
// Verifies reporting when no metrics where recorded.
TEST_F(MetricsReporterTest, NoMetrics) {
  SetupReporter("1");

  // Verify startup conditions
  ASSERT_FALSE(ShouldReportAtStartup());
  ASSERT_TRUE(ShouldContinueReporting());

  // Start the thread and notify the startup. This will advance the state.
  MaybeStartBackgroundThread(/*add_metrics=*/ false);

  // We're waiting for 1 report
  WaitForReport(/*report_count=*/ 1, /*sleep_period_ms=*/ 500);
  VerifyReports(/*size=*/ 1, /*with_metrics*/ false);

  // We should not longer report at period.
  ASSERT_FALSE(ShouldContinueReporting());
}

// Verify we don't start reporting if the sample rate is set to 0.
TEST_F(MetricsReporterTest, SampleRateDisable) {
  SetupReporter("1", /*session_id=*/ 1, /*reporting_mods=*/ 0);

  // The background thread should not start.
  ASSERT_FALSE(MaybeStartBackgroundThread(/*add_metrics=*/ false));

  ASSERT_FALSE(ShouldReportAtStartup());
  ASSERT_FALSE(ShouldContinueReporting());
}

// Verify we don't start reporting if the sample rate is low and the session does
// not meet conditions.
TEST_F(MetricsReporterTest, SampleRateDisable24) {
  SetupReporter("1", /*session_id=*/ 125, /*reporting_mods=*/ 24);

  // The background thread should not start.
  ASSERT_FALSE(MaybeStartBackgroundThread(/*add_metrics=*/ false));

  ASSERT_FALSE(ShouldReportAtStartup());
  ASSERT_FALSE(ShouldContinueReporting());
}

// Verify we start reporting if the sample rate and the session meet
// reporting conditions
TEST_F(MetricsReporterTest, SampleRateEnable50) {
  SetupReporter("1", /*session_id=*/ 125, /*reporting_mods=*/ 50);

  // The background thread should not start.
  ASSERT_TRUE(MaybeStartBackgroundThread(/*add_metrics=*/ false));

  ASSERT_FALSE(ShouldReportAtStartup());
  ASSERT_TRUE(ShouldContinueReporting());
}

// Verify we start reporting if the sample rate and the session meet
// reporting conditions
TEST_F(MetricsReporterTest, SampleRateEnableAll) {
  SetupReporter("1", /*session_id=*/ 1099, /*reporting_mods=*/ 100);

  // The background thread should start.
  ASSERT_TRUE(MaybeStartBackgroundThread(/*add_metrics=*/ false));

  ASSERT_FALSE(ShouldReportAtStartup());
  ASSERT_TRUE(ShouldContinueReporting());
}

TEST_F(MetricsReporterTest, CompilerFilter) {
  SetupReporter("1", /*session_id=*/ 1099, /*reporting_mods=*/ 100);
  ASSERT_TRUE(MaybeStartBackgroundThread(/*add_metrics=*/ true));

  AppInfo app_info;
  app_info.RegisterOdexStatus(
      "code_location",
      "verify",
      "install",
      "odex_status");
  app_info.RegisterAppInfo(
      "package_name",
      std::vector<std::string>({"code_location"}),
      "",
      "",
      AppInfo::CodeType::kPrimaryApk);
  NotifyAppInfoUpdated(&app_info);

  WaitForReport(/*report_count=*/ 1, /*sleep_period_ms=*/ 500);
  VerifyReports(
      /*size=*/ 1,
      /*with_metrics*/ true,
      CompilerFilterReporting::kVerify,
      CompilationReason::kInstall);
}

// Test class for period spec parsing
class ReportingPeriodSpecTest : public testing::Test {
 public:
  void VerifyFalse(const std::string& spec_str) {
    Verify(spec_str, false, false, false, {});
  }

  void VerifyTrue(
      const std::string& spec_str,
      bool startup_first,
      bool continuous,
      std::vector<uint32_t> periods) {
    Verify(spec_str, true, startup_first, continuous, periods);
  }

  void Verify(
      const std::string& spec_str,
      bool valid,
      bool startup_first,
      bool continuous,
      std::vector<uint32_t> periods) {
    std::string error_msg;
    std::optional<ReportingPeriodSpec> spec = ReportingPeriodSpec::Parse(spec_str, &error_msg);

    ASSERT_EQ(valid, spec.has_value()) << spec_str;
    if (valid) {
        ASSERT_EQ(spec->spec, spec_str) << spec_str;
        ASSERT_EQ(spec->report_startup_first, startup_first) << spec_str;
        ASSERT_EQ(spec->continuous_reporting, continuous) << spec_str;
        ASSERT_EQ(spec->periods_seconds, periods) << spec_str;
    }
  }
};

TEST_F(ReportingPeriodSpecTest, ParseTestsInvalid) {
  VerifyFalse("");
  VerifyFalse("*");
  VerifyFalse("S,*");
  VerifyFalse("foo");
  VerifyFalse("-1");
  VerifyFalse("1,S");
  VerifyFalse("*,1");
  VerifyFalse("1,2,3,-1,3");
  VerifyFalse("1,*,2");
  VerifyFalse("1,S,2");
}

TEST_F(ReportingPeriodSpecTest, ParseTestsValid) {
  VerifyTrue("S", true, false, {});
  VerifyTrue("S,1", true, false, {1});
  VerifyTrue("S,1,2,3,4", true, false, {1, 2, 3, 4});
  VerifyTrue("S,1,*", true, true, {1});
  VerifyTrue("S,1,2,3,4,*", true, true, {1, 2, 3, 4});

  VerifyTrue("1", false, false, {1});
  VerifyTrue("1,2,3,4", false, false, {1, 2, 3, 4});
  VerifyTrue("1,*", false, true, {1});
  VerifyTrue("1,2,3,4,*", false, true, {1, 2, 3, 4});
}

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
