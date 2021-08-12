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

#include <algorithm>

#include <android-base/parseint.h>

#include "base/flags.h"
#include "oat_file_manager.h"
#include "runtime.h"
#include "runtime_options.h"
#include "statsd.h"
#include "thread-current-inl.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {

std::unique_ptr<MetricsReporter> MetricsReporter::Create(
    const ReportingConfig& config, Runtime* runtime) {
  // We can't use std::make_unique here because the MetricsReporter constructor is private.
  return std::unique_ptr<MetricsReporter>{new MetricsReporter{std::move(config), runtime}};
}

MetricsReporter::MetricsReporter(const ReportingConfig& config, Runtime* runtime)
    : config_{config},
      runtime_{runtime},
      startup_reported_{false},
      report_interval_index_{0} {}

MetricsReporter::~MetricsReporter() { MaybeStopBackgroundThread(); }

void MetricsReporter::ReloadConfig(const ReportingConfig& config) {
  DCHECK(!thread_.has_value()) << "The config cannot be reloaded after the background "
                                  "reporting thread is started.";
  config_ = config;
}

bool MetricsReporter::IsMetricsReportingEnabled(const SessionData& session_data) const {
  return session_data.session_id % config_.reporting_num_mods < config_.reporting_mods;
}

bool MetricsReporter::MaybeStartBackgroundThread(SessionData session_data) {
  CHECK(!thread_.has_value());

  session_data_ = session_data;
  LOG_STREAM(DEBUG) << "Received session metadata: " << session_data_.session_id;

  if (!IsMetricsReportingEnabled(session_data_)) {
    return false;
  }

  thread_.emplace(&MetricsReporter::BackgroundThreadRun, this);
  return true;
}

void MetricsReporter::MaybeStopBackgroundThread() {
  if (thread_.has_value()) {
    messages_.SendMessage(ShutdownRequestedMessage{});
    thread_->join();
    thread_.reset();
  }
}

void MetricsReporter::NotifyStartupCompleted() {
  if (ShouldReportAtStartup() && thread_.has_value()) {
    messages_.SendMessage(StartupCompletedMessage{});
  }
}

void MetricsReporter::NotifyAppInfoUpdated(AppInfo* app_info) {
  std::string compilation_reason;
  std::string compiler_filter;

  app_info->GetPrimaryApkOptimizationStatus(
      &compiler_filter, &compilation_reason);

  SetCompilationInfo(
      CompilationReasonFromName(compilation_reason),
      CompilerFilterReportingFromName(compiler_filter));
}

void MetricsReporter::RequestMetricsReport(bool synchronous) {
  if (thread_.has_value()) {
    messages_.SendMessage(RequestMetricsReportMessage{synchronous});
    if (synchronous) {
      thread_to_host_messages_.ReceiveMessage();
    }
  }
}

void MetricsReporter::SetCompilationInfo(CompilationReason compilation_reason,
                                         CompilerFilterReporting compiler_filter) {
  if (thread_.has_value()) {
    messages_.SendMessage(CompilationInfoMessage{compilation_reason, compiler_filter});
  }
}

void MetricsReporter::BackgroundThreadRun() {
  LOG_STREAM(DEBUG) << "Metrics reporting thread started";

  // AttachCurrentThread is needed so we can safely use the ART concurrency primitives within the
  // messages_ MessageQueue.
  const bool attached = runtime_->AttachCurrentThread(kBackgroundThreadName,
                                                      /*as_daemon=*/true,
                                                      runtime_->GetSystemThreadGroup(),
                                                      /*create_peer=*/true);
  bool running = true;

  // Configure the backends
  if (config_.dump_to_logcat) {
    backends_.emplace_back(new LogBackend(LogSeverity::INFO));
  }
  if (config_.dump_to_file.has_value()) {
    backends_.emplace_back(new FileBackend(config_.dump_to_file.value()));
  }
  if (config_.dump_to_statsd) {
    auto backend = CreateStatsdBackend();
    if (backend != nullptr) {
      backends_.emplace_back(std::move(backend));
    }
  }

  MaybeResetTimeout();

  while (running) {
    messages_.SwitchReceive(
        [&]([[maybe_unused]] ShutdownRequestedMessage message) {
          LOG_STREAM(DEBUG) << "Shutdown request received " << session_data_.session_id;
          running = false;

          ReportMetrics();
        },
        [&](RequestMetricsReportMessage message) {
          LOG_STREAM(DEBUG) << "Explicit report request received " << session_data_.session_id;
          ReportMetrics();
          if (message.synchronous) {
            thread_to_host_messages_.SendMessage(ReportCompletedMessage{});
          }
        },
        [&]([[maybe_unused]] TimeoutExpiredMessage message) {
          LOG_STREAM(DEBUG) << "Timer expired, reporting metrics " << session_data_.session_id;

          ReportMetrics();
          MaybeResetTimeout();
        },
        [&]([[maybe_unused]] StartupCompletedMessage message) {
          LOG_STREAM(DEBUG) << "App startup completed, reporting metrics "
              << session_data_.session_id;
          ReportMetrics();
          startup_reported_ = true;
          MaybeResetTimeout();
        },
        [&](CompilationInfoMessage message) {
          LOG_STREAM(DEBUG) << "Compilation info received " << session_data_.session_id;
          session_data_.compilation_reason = message.compilation_reason;
          session_data_.compiler_filter = message.compiler_filter;

          UpdateSessionInBackends();
        });
  }

  if (attached) {
    runtime_->DetachCurrentThread();
  }
  LOG_STREAM(DEBUG) << "Metrics reporting thread terminating " << session_data_.session_id;
}

void MetricsReporter::MaybeResetTimeout() {
  if (ShouldContinueReporting()) {
    messages_.SetTimeout(SecondsToMs(GetNextPeriodSeconds()));
  }
}

const ArtMetrics* MetricsReporter::GetMetrics() {
  return runtime_->GetMetrics();
}

void MetricsReporter::ReportMetrics() {
  const ArtMetrics* metrics = GetMetrics();

  if (!session_started_) {
    for (auto& backend : backends_) {
      backend->BeginOrUpdateSession(session_data_);
    }
    session_started_ = true;
  }

  for (auto& backend : backends_) {
    metrics->ReportAllMetrics(backend.get());
  }
}

void MetricsReporter::UpdateSessionInBackends() {
  if (session_started_) {
    for (auto& backend : backends_) {
      backend->BeginOrUpdateSession(session_data_);
    }
  }
}

bool MetricsReporter::ShouldReportAtStartup() const {
  return IsMetricsReportingEnabled(session_data_) &&
      config_.period_spec.has_value() &&
      config_.period_spec->report_startup_first;
}

bool MetricsReporter::ShouldContinueReporting() const {
  bool result =
      // Only if the reporting is enabled
      IsMetricsReportingEnabled(session_data_) &&
      // and if we have period spec
      config_.period_spec.has_value() &&
      // and the periods are non empty
      !config_.period_spec->periods_seconds.empty() &&
      // and we already reported startup or not required to report startup
      (startup_reported_ || !config_.period_spec->report_startup_first) &&
      // and we still have unreported intervals or we are asked to report continuously.
      (config_.period_spec->continuous_reporting ||
              (report_interval_index_ < config_.period_spec->periods_seconds.size()));
  return result;
}

uint32_t MetricsReporter::GetNextPeriodSeconds() {
  DCHECK(ShouldContinueReporting());

  // The index is either the current report_interval_index or the last index
  // if we are in continuous mode and reached the end.
  uint32_t index = std::min(
      report_interval_index_,
      static_cast<uint32_t>(config_.period_spec->periods_seconds.size() - 1));

  uint32_t result = config_.period_spec->periods_seconds[index];

  // Advance the index if we didn't get to the end.
  if (report_interval_index_ < config_.period_spec->periods_seconds.size()) {
    report_interval_index_++;
  }
  return result;
}

ReportingConfig ReportingConfig::FromFlags(bool is_system_server) {
  std::optional<std::string> spec_str = is_system_server
      ? gFlags.MetricsReportingSpecSystemServer.GetValueOptional()
      : gFlags.MetricsReportingSpec.GetValueOptional();

  std::optional<ReportingPeriodSpec> period_spec = std::nullopt;

  if (spec_str.has_value()) {
    std::string error;
    period_spec = ReportingPeriodSpec::Parse(spec_str.value(), &error);
    if (!period_spec.has_value()) {
      LOG(ERROR) << "Failed to create metrics reporting spec from: " << spec_str.value()
          << " with error: " << error;
    }
  }

  uint32_t reporting_num_mods = is_system_server
      ? gFlags.MetricsReportingNumModsServer()
      : gFlags.MetricsReportingNumMods();
  uint32_t reporting_mods = is_system_server
      ? gFlags.MetricsReportingModsServer()
      : gFlags.MetricsReportingMods();

  if (reporting_mods > reporting_num_mods || reporting_num_mods == 0) {
    LOG(ERROR) << "Invalid metrics reporting mods: " << reporting_mods
        << " num modes=" << reporting_num_mods
        << ". The reporting is disabled";
    reporting_mods = 0;
    reporting_num_mods = 100;
  }

  return {
      .dump_to_logcat = gFlags.MetricsWriteToLogcat(),
      .dump_to_file = gFlags.MetricsWriteToFile.GetValueOptional(),
      .dump_to_statsd = gFlags.MetricsWriteToStatsd(),
      .period_spec = period_spec,
      .reporting_num_mods = reporting_num_mods,
      .reporting_mods = reporting_mods,
  };
}

std::optional<ReportingPeriodSpec> ReportingPeriodSpec::Parse(
    const std::string& spec_str, std::string* error_msg) {
  *error_msg = "";
  if (spec_str.empty()) {
    *error_msg = "Invalid empty spec.";
    return std::nullopt;
  }

  // Split the string. Each element is separated by comma.
  std::vector<std::string> elems;
  Split(spec_str, ',', &elems);

  // Check the startup marker (front) and the continuous one (back).
  std::optional<ReportingPeriodSpec> spec = std::make_optional(ReportingPeriodSpec());
  spec->spec = spec_str;
  spec->report_startup_first = elems.front() == "S";
  spec->continuous_reporting = elems.back() == "*";

  // Compute the indices for the period values.
  size_t start_interval_idx = spec->report_startup_first ? 1 : 0;
  size_t end_interval_idx = spec->continuous_reporting ? (elems.size() - 1) : elems.size();

  // '*' needs a numeric interval before in order to be valid.
  if (spec->continuous_reporting &&
      end_interval_idx == start_interval_idx) {
    *error_msg = "Invalid period value in spec: " + spec_str;
    return std::nullopt;
  }

  // Parse the periods.
  for (size_t i = start_interval_idx; i < end_interval_idx; i++) {
    uint32_t period;
    if (!android::base::ParseUint(elems[i], &period)) {
        *error_msg = "Invalid period value in spec: " + spec_str;
        return std::nullopt;
    }
    spec->periods_seconds.push_back(period);
  }

  return spec;
}

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
