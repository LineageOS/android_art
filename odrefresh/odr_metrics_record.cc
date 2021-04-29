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

#include "odr_metrics_record.h"

#include <iosfwd>
#include <istream>
#include <ostream>
#include <streambuf>
#include <string>

namespace art {
namespace odrefresh {

std::istream& operator>>(std::istream& is, OdrMetricsRecord& record) {
  // Block I/O related exceptions
  auto saved_exceptions = is.exceptions();
  is.exceptions(std::ios_base::iostate {});

  // The order here matches the field order of MetricsRecord.
  is >> record.art_apex_version >> std::ws;
  is >> record.trigger >> std::ws;
  is >> record.stage_reached >> std::ws;
  is >> record.status >> std::ws;
  is >> record.primary_bcp_compilation_seconds >> std::ws;
  is >> record.secondary_bcp_compilation_seconds >> std::ws;
  is >> record.system_server_compilation_seconds >> std::ws;
  is >> record.cache_space_free_start_mib >> std::ws;
  is >> record.cache_space_free_end_mib >> std::ws;

  // Restore I/O related exceptions
  is.exceptions(saved_exceptions);
  return is;
}

std::ostream& operator<<(std::ostream& os, const OdrMetricsRecord& record) {
  static const char kSpace = ' ';

  // Block I/O related exceptions
  auto saved_exceptions = os.exceptions();
  os.exceptions(std::ios_base::iostate {});

  // The order here matches the field order of MetricsRecord.
  os << record.art_apex_version << kSpace;
  os << record.trigger << kSpace;
  os << record.stage_reached << kSpace;
  os << record.status << kSpace;
  os << record.primary_bcp_compilation_seconds << kSpace;
  os << record.secondary_bcp_compilation_seconds << kSpace;
  os << record.system_server_compilation_seconds << kSpace;
  os << record.cache_space_free_start_mib << kSpace;
  os << record.cache_space_free_end_mib << std::endl;

  // Restore I/O related exceptions
  os.exceptions(saved_exceptions);
  return os;
}

}  // namespace odrefresh
}  // namespace art
