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

#ifndef ART_ODREFRESH_ODR_COMPILATION_LOG_H_
#define ART_ODREFRESH_ODR_COMPILATION_LOG_H_

#include <time.h>

#include <cstdint>
#include <iosfwd>
#include <vector>

#include <odrefresh/odrefresh.h>
#include <odr_metrics.h>

namespace art {
namespace odrefresh {

// OdrCompilationLogEntry represents the result of a compilation attempt by odrefresh.
struct OdrCompilationLogEntry {
  int64_t apex_version;
  int64_t last_update_millis;
  int32_t trigger;
  time_t when;
  int32_t exit_code;
};

// Read an `OdrCompilationLogEntry` from an input stream.
std::istream& operator>>(std::istream& is, OdrCompilationLogEntry& entry);

// Write an `OdrCompilationLogEntry` to an output stream.
std::ostream& operator<<(std::ostream& os, const OdrCompilationLogEntry& entry);

// Equality test for two `OdrCompilationLogEntry` instances.
bool operator==(const OdrCompilationLogEntry& lhs, const OdrCompilationLogEntry& rhs);
bool operator!=(const OdrCompilationLogEntry& lhs, const OdrCompilationLogEntry& rhs);

class OdrCompilationLog {
 public:
  // The compilation log location is in the same directory as used for the metricss.log. This
  // directory is only used by odrefresh whereas the ART apexdata directory is also used by odsign
  // and others which may lead to the deletion (or rollback) of the log file.
  static constexpr const char* kCompilationLogFile = "/data/misc/odrefresh/compilation-log.txt";

  // Version string that appears on the first line of the compilation log.
  static constexpr const char kLogVersion[] = "CompilationLog/1.0";

  // Number of log entries in the compilation log.
  static constexpr const size_t kMaxLoggedEntries = 4;

  explicit OdrCompilationLog(const char* compilation_log_path = kCompilationLogFile);
  ~OdrCompilationLog();

  // Applies policy to compilation log to determine whether to recompile.
  bool ShouldAttemptCompile(int64_t apex_version,
                            int64_t last_update_millis,
                            OdrMetrics::Trigger trigger,
                            time_t now = 0) const;

  // Returns the number of entries in the log. The log never exceeds `kMaxLoggedEntries`.
  size_t NumberOfEntries() const;

  // Returns the entry at position `index` or nullptr if `index` is out of bounds.
  const OdrCompilationLogEntry* Peek(size_t index) const;

  void Log(int64_t apex_version,
           int64_t last_update_millis,
           OdrMetrics::Trigger trigger,
           ExitCode compilation_result);

  void Log(int64_t apex_version,
           int64_t last_update_millis,
           OdrMetrics::Trigger trigger,
           time_t when,
           ExitCode compilation_result);

  // Truncates the in memory log to have `kMaxLoggedEntries` records.
  void Truncate();

 private:
  bool Read();
  bool Write() const;

  std::vector<OdrCompilationLogEntry> entries_;
  const char* log_path_;
};

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_ODR_COMPILATION_LOG_H_
