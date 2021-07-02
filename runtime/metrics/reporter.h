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

#ifndef ART_RUNTIME_METRICS_REPORTER_H_
#define ART_RUNTIME_METRICS_REPORTER_H_

#include "app_info.h"
#include "base/message_queue.h"
#include "base/metrics/metrics.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {

/**
 * Encapsulates the specification of the metric reporting periods.
 *
 * The period spec follows the following regex: "(S,)?(\d+,)*\*?"
 * with the following semantics:
 *   "S"         - will only report at startup.
 *
 *   "S,1,1"     - will report startup, than 1 second later, then another
 *                 second later.
 *
 *   "S,1,2,4  " - will report at Startup time, then 1 seconds later,
 *                 then 2, then finally 4 seconds later. After that, the
 *                 reporting will stop.
 *
 *   "S,1,2,4,*" - same as above, but after the final 4s period, the
 *                 reporting will continue every other 4s.
 *                 '*' is an indication we should report continuously
 *                 every N seconds, where N is the last period.
 *
 *   "2,*"       - will report every 2 seconds
 *
 * Note that "", "*", or "S,*" are not valid specs, and 'S' can only occur
 * in the beginning.
 */
struct ReportingPeriodSpec {
  static std::optional<ReportingPeriodSpec> Parse(
      const std::string& spec_str, std::string* error_msg);

  // The original spec.
  std::string spec;
  // The intervals when we should report.
  std::vector<uint32_t> periods_seconds;
  // Whether or not the reporting is continuous (contains a '*').
  bool continuous_reporting{false};
  // Whether or not the reporting should start after startup event (starts with an 'S').
  bool report_startup_first{false};
};

// Defines the set of options for how metrics reporting happens.
struct ReportingConfig {
  static ReportingConfig FromFlags(bool is_system_server = false);

  // Causes metrics to be written to the log, which makes them show up in logcat.
  bool dump_to_logcat{false};

  // Causes metrics to be written to statsd, which causes them to be uploaded to Westworld.
  bool dump_to_statsd{false};

  // If set, provides a file name to enable metrics logging to a file.
  std::optional<std::string> dump_to_file;

  // The reporting period configuration.
  std::optional<ReportingPeriodSpec> period_spec;

  // The mods that should report metrics. Together with reporting_num_mods, they
  // dictate what percentage of the runtime execution will report metrics.
  // If the `session_id (a random number) % reporting_num_mods < reporting_mods`
  // then the runtime session will report metrics.
  uint32_t reporting_mods{0};
  uint32_t reporting_num_mods{100};
};

// MetricsReporter handles periodically reporting ART metrics.
class MetricsReporter {
 public:
  // Creates a MetricsReporter instance that matches the options selected in ReportingConfig.
  static std::unique_ptr<MetricsReporter> Create(const ReportingConfig& config, Runtime* runtime);

  virtual ~MetricsReporter();

  // Creates and runs the background reporting thread.
  //
  // Does nothing if the reporting config does not have any outputs enabled.
  //
  // Returns true if the thread was started, false otherwise.
  bool MaybeStartBackgroundThread(SessionData session_data);

  // Sends a request to the background thread to shutdown.
  void MaybeStopBackgroundThread();

  // Causes metrics to be reported so we can see a snapshot of the metrics after app startup
  // completes.
  void NotifyStartupCompleted();

  // Notifies the reporter that the app info was updated. This is used to detect / infer
  // the compiler filter / reason of primary apks.
  void NotifyAppInfoUpdated(AppInfo* app_info);

  // Requests a metrics report
  //
  // If synchronous is set to true, this function will block until the report has completed.
  void RequestMetricsReport(bool synchronous = true);

  // Reloads the metrics config from the given value.
  // Can only be called before starting the background thread.
  void ReloadConfig(const ReportingConfig& config);

  void SetCompilationInfo(CompilationReason compilation_reason,
                          CompilerFilterReporting compiler_filter);

  static constexpr const char* kBackgroundThreadName = "Metrics Background Reporting Thread";

 protected:
  // Returns the metrics to be reported.
  // This exists only for testing purposes so that we can verify reporting with minimum
  // runtime interference.
  virtual const ArtMetrics* GetMetrics();

  MetricsReporter(const ReportingConfig& config, Runtime* runtime);

 private:
  // Whether or not we should reporting metrics according to the sampling rate.
  bool IsMetricsReportingEnabled(const SessionData& session_data) const;

  // The background reporting thread main loop.
  void BackgroundThreadRun();

  // Calls messages_.SetTimeout if needed.
  void MaybeResetTimeout();

  // Outputs the current state of the metrics to the destination set by config_.
  void ReportMetrics();

  // Updates the session data in all the backends.
  void UpdateSessionInBackends();

  // Whether or not we should wait for startup before reporting for the first time.
  bool ShouldReportAtStartup() const;

  // Whether or not we should continue reporting (either because we still
  // have periods to report, or because we are in continuous mode).
  bool ShouldContinueReporting() const;

  // Returns the next reporting period.
  // Must be called only if ShouldContinueReporting() is true.
  uint32_t GetNextPeriodSeconds();

  ReportingConfig config_;
  Runtime* runtime_;
  std::vector<std::unique_ptr<MetricsBackend>> backends_;
  std::optional<std::thread> thread_;
  // Whether or not we reported the startup event.
  bool startup_reported_;
  // The index into period_spec.periods_seconds which tells the next delay in
  // seconds for the next reporting.
  uint32_t report_interval_index_;

  // A message indicating that the reporting thread should shut down.
  struct ShutdownRequestedMessage {};

  // A message indicating that app startup has completed.
  struct StartupCompletedMessage {};

  // A message requesting an explicit metrics report.
  //
  // The synchronous field specifies whether the reporting thread will send a message back when
  // reporting is complete.
  struct RequestMetricsReportMessage {
    bool synchronous;
  };

  struct CompilationInfoMessage {
    CompilationReason compilation_reason;
    CompilerFilterReporting compiler_filter;
  };

  MessageQueue<ShutdownRequestedMessage,
               StartupCompletedMessage,
               RequestMetricsReportMessage,
               CompilationInfoMessage>
      messages_;

  // A message indicating a requested report has been finished.
  struct ReportCompletedMessage {};

  MessageQueue<ReportCompletedMessage> thread_to_host_messages_;

  SessionData session_data_{};
  bool session_started_{false};

  friend class MetricsReporterTest;
};

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion

#endif  // ART_RUNTIME_METRICS_REPORTER_H_
