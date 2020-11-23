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

#include "compat_framework.h"

#include "android-base/logging.h"
#include <sys/types.h>
#include <unistd.h>

namespace art {

// Compat change states as strings.
static constexpr char kUnknownChangeState[] = "UNKNOWN";
static constexpr char kEnabledChangeState[] = "ENABLED";
static constexpr char kDisabledChangeState[] = "DISABLED";
static constexpr char kLoggedState[] = "LOGGED";

bool CompatFramework::IsChangeEnabled(uint64_t change_id) {
  const auto enabled = disabled_compat_changes_.count(change_id) == 0;
  ReportChange(change_id, enabled ? ChangeState::kEnabled : ChangeState::kDisabled);
  return enabled;
}

void CompatFramework::LogChange(uint64_t change_id) {
  ReportChange(change_id, ChangeState::kLogged);
}

void CompatFramework::ReportChange(uint64_t change_id, ChangeState state) {
  bool already_reported = reported_compat_changes_.count(change_id) != 0;
  if (already_reported) {
    return;
  }
  LOG(DEBUG) << "Compat change id reported: " << change_id << "; UID " << getuid()
            << "; state: " << ChangeStateToString(state);
  // TODO(145743810): add an up call to java to log to statsd
  reported_compat_changes_.emplace(change_id);
}

std::string_view CompatFramework::ChangeStateToString(ChangeState state) {
  switch (state) {
    case ChangeState::kUnknown:
      return kUnknownChangeState;
    case ChangeState::kEnabled:
      return kEnabledChangeState;
    case ChangeState::kDisabled:
      return kDisabledChangeState;
    case ChangeState::kLogged:
      return kLoggedState;
  }
}

}  // namespace art
