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

#include "metrics_reporter.h"

#include "android-base/file.h"
#include "base/scoped_flock.h"
#include "runtime.h"
#include "runtime_options.h"
#include "thread-current-inl.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {

using android::base::WriteStringToFd;

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
