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

#include "base/message_queue.h"
#include "base/metrics/metrics.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {

// Defines the set of options for how metrics reporting happens.
struct ReportingConfig {
  static ReportingConfig FromRuntimeArguments(const RuntimeArgumentMap& args);

  // Causes metrics to be written to the log, which makes them show up in logcat.
  bool dump_to_logcat{false};

  // Causes metrics to be written to statsd, which causes them to be uploaded to Westworld.
  bool dump_to_statsd{false};

  // If set, provides a file name to enable metrics logging to a file.
  std::optional<std::string> dump_to_file;

  // Indicates whether to report the final state of metrics on shutdown.
  //
  // Note that reporting only happens if some output, such as logcat, is enabled.
  bool report_metrics_on_shutdown{true};

  // If set, metrics will be reported every time this many seconds elapses.
  std::optional<unsigned int> periodic_report_seconds;
};

// MetricsReporter handles periodically reporting ART metrics.
class MetricsReporter {
 public:
  // Creates a MetricsReporter instance that matches the options selected in ReportingConfig.
  static std::unique_ptr<MetricsReporter> Create(ReportingConfig config, Runtime* runtime);

  ~MetricsReporter();

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

  bool IsPeriodicReportingEnabled() const;

  // Changes the reporting period.
  //
  // This function is not thread safe and may only be called before the background reporting thread
  // has been started.
  void SetReportingPeriod(unsigned int period_seconds);

  // Requests a metrics report
  //
  // If synchronous is set to true, this function will block until the report has completed.
  void RequestMetricsReport(bool synchronous = true);

  void SetCompilationInfo(CompilationReason compilation_reason,
                          CompilerFilter::Filter compiler_filter);

  static constexpr const char* kBackgroundThreadName = "Metrics Background Reporting Thread";

 private:
  MetricsReporter(ReportingConfig config, Runtime* runtime);

  // The background reporting thread main loop.
  void BackgroundThreadRun();

  // Calls messages_.SetTimeout if needed.
  void MaybeResetTimeout();

  // Outputs the current state of the metrics to the destination set by config_.
  void ReportMetrics();

  ReportingConfig config_;
  Runtime* runtime_;
  std::vector<std::unique_ptr<MetricsBackend>> backends_;
  std::optional<std::thread> thread_;

  // A message indicating that the reporting thread should shut down.
  struct ShutdownRequestedMessage {};

  // A message indicating that app startup has completed.
  struct StartupCompletedMessage {};

  // A message marking the beginning of a metrics logging session.
  //
  // The primary purpose of this is to pass the session metadata from the Runtime to the metrics
  // backends.
  struct BeginSessionMessage{ SessionData session_data; };

  // A message requesting an explicit metrics report.
  //
  // The synchronous field specifies whether the reporting thread will send a message back when
  // reporting is complete.
  struct RequestMetricsReportMessage {
    bool synchronous;
  };

  struct CompilationInfoMessage {
    CompilationReason compilation_reason;
    CompilerFilter::Filter compiler_filter;
  };

  MessageQueue<ShutdownRequestedMessage,
               StartupCompletedMessage,
               BeginSessionMessage,
               RequestMetricsReportMessage,
               CompilationInfoMessage>
      messages_;

  // A message indicating a requested report has been finished.
  struct ReportCompletedMessage {};

  MessageQueue<ReportCompletedMessage> thread_to_host_messages_;

  SessionData session_data_{};
  bool session_started_{false};
};

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion

#endif  // ART_RUNTIME_METRICS_REPORTER_H_
