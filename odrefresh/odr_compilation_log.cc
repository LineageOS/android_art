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

#include <odr_compilation_log.h>

#include <errno.h>

#include <fstream>
#include <ios>
#include <iosfwd>
#include <istream>
#include <ostream>
#include <streambuf>
#include <string>
#include <vector>

#include "android-base/logging.h"
#include "base/os.h"

#include "odrefresh/odrefresh.h"
#include "odr_metrics.h"

namespace art {
namespace odrefresh {

std::istream& operator>>(std::istream& is, OdrCompilationLogEntry& entry) {
  // Block I/O related exceptions
  auto saved_exceptions = is.exceptions();
  is.exceptions(std::ios_base::iostate {});

  // Write log entry. NB update OdrCompilationLog::kLogVersion if changing the format here.
  is >> entry.apex_version >> std::ws;
  is >> entry.last_update_millis >> std::ws;
  is >> entry.trigger >> std::ws;
  is >> entry.when >> std::ws;
  is >> entry.exit_code >> std::ws;

  // Restore I/O related exceptions
  is.exceptions(saved_exceptions);
  return is;
}

std::ostream& operator<<(std::ostream& os, const OdrCompilationLogEntry& entry) {
  static const char kSpace = ' ';

  // Block I/O related exceptions
  auto saved_exceptions = os.exceptions();
  os.exceptions(std::ios_base::iostate {});

  os << entry.apex_version << kSpace;
  os << entry.last_update_millis << kSpace;
  os << entry.trigger << kSpace;
  os << entry.when << kSpace;
  os << entry.exit_code << std::endl;

  // Restore I/O related exceptions
  os.exceptions(saved_exceptions);
  return os;
}

bool operator==(const OdrCompilationLogEntry& lhs, const OdrCompilationLogEntry& rhs) {
  return lhs.apex_version == rhs.apex_version && lhs.last_update_millis == rhs.last_update_millis &&
         lhs.trigger == rhs.trigger && lhs.when == rhs.when && lhs.exit_code == rhs.exit_code;
}

bool operator!=(const OdrCompilationLogEntry& lhs, const OdrCompilationLogEntry& rhs) {
  return !(lhs == rhs);
}

OdrCompilationLog::OdrCompilationLog(const char* compilation_log_path)
    : log_path_(compilation_log_path) {
  if (log_path_ != nullptr && OS::FileExists(log_path_)) {
    if (!Read()) {
      PLOG(ERROR) << "Failed to read compilation log: " << log_path_;
    }
  }
}

OdrCompilationLog::~OdrCompilationLog() {
  if (log_path_ != nullptr && !Write()) {
    PLOG(ERROR) << "Failed to write compilation log: " << log_path_;
  }
}

bool OdrCompilationLog::Read() {
  std::ifstream ifs(log_path_);
  if (!ifs.good()) {
    return false;
  }

  std::string log_version;
  ifs >> log_version >> std::ws;
  if (log_version != kLogVersion) {
    return false;
  }

  while (!ifs.eof()) {
    OdrCompilationLogEntry entry;
    ifs >> entry;
    if (ifs.fail()) {
      entries_.clear();
      return false;
    }
    entries_.push_back(entry);
  }

  return true;
}

bool OdrCompilationLog::Write() const {
  std::ofstream ofs(log_path_, std::ofstream::trunc);
  if (!ofs.good()) {
    return false;
  }

  ofs << kLogVersion << std::endl;
  for (const auto& entry : entries_) {
    ofs << entry;
    if (ofs.fail()) {
      return false;
    }
  }

  return true;
}

void OdrCompilationLog::Truncate() {
  if (entries_.size() < kMaxLoggedEntries) {
    return;
  }

  size_t excess = entries_.size() - kMaxLoggedEntries;
  entries_.erase(entries_.begin(), entries_.begin() + excess);
}

size_t OdrCompilationLog::NumberOfEntries() const {
  return entries_.size();
}

const OdrCompilationLogEntry* OdrCompilationLog::Peek(size_t index) const {
  if (index >= entries_.size()) {
    return nullptr;
  }
  return &entries_[index];
}

void OdrCompilationLog::Log(int64_t apex_version,
                            int64_t last_update_millis,
                            OdrMetrics::Trigger trigger,
                            ExitCode compilation_result) {
  time_t now;
  time(&now);
  Log(apex_version, last_update_millis, trigger, now, compilation_result);
}

void OdrCompilationLog::Log(int64_t apex_version,
                            int64_t last_update_millis,
                            OdrMetrics::Trigger trigger,
                            time_t when,
                            ExitCode compilation_result) {
  entries_.push_back(OdrCompilationLogEntry{apex_version,
                                            last_update_millis,
                                            static_cast<int32_t>(trigger),
                                            when,
                                            static_cast<int32_t>(compilation_result)});
  Truncate();
}

bool OdrCompilationLog::ShouldAttemptCompile(int64_t apex_version,
                                             int64_t last_update_millis,
                                             OdrMetrics::Trigger trigger,
                                             time_t now) const {
  if (entries_.size() == 0) {
    // We have no history, try to compile.
    return true;
  }

  if (apex_version != entries_.back().apex_version) {
    // There is a new ART APEX, we should compile right away.
    return true;
  }

    if (last_update_millis != entries_.back().last_update_millis) {
    // There is a samegrade ART APEX update, we should compile right away.
    return true;
  }

  if (trigger == OdrMetrics::Trigger::kDexFilesChanged) {
    // The DEX files in the classpaths have changed, possibly an OTA has updated them.
    return true;
  }

  // Compute the backoff time based on the number of consecutive failures.
  //
  // Wait 12 hrs * pow(2, consecutive_failures) since the last compilation attempt.
  static const int kSecondsPerDay = 86'400;
  time_t backoff = kSecondsPerDay / 2;
  for (auto it = entries_.crbegin(); it != entries_.crend(); ++it, backoff *= 2) {
    if (it->exit_code == ExitCode::kCompilationSuccess) {
      break;
    }
  }

  if (now == 0) {
    time(&now);
  }

  const time_t last_attempt = entries_.back().when;
  const time_t threshold = last_attempt + backoff;
  return now >= threshold;
}

}  // namespace odrefresh
}  // namespace art
