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

#ifndef ART_RUNTIME_COMPAT_FRAMEWORK_H_
#define ART_RUNTIME_COMPAT_FRAMEWORK_H_

#include <set>

#include "base/string_view_cpp20.h"

namespace art {

// ART counterpart of the compat framework (go/compat-framework).
// Created in order to avoid repeated up-calls to Java.
class CompatFramework {
 public:
  // Compat change reported state
  // This must be kept in sync with AppCompatibilityChangeReported.State in
  // frameworks/base/cmds/statsd/src/atoms.proto
  enum class ChangeState {
    kUnknown,
    kEnabled,
    kDisabled,
    kLogged
  };

  void SetDisabledCompatChanges(const std::set<uint64_t>& disabled_changes) {
    disabled_compat_changes_ = disabled_changes;
  }

  const std::set<uint64_t>& GetDisabledCompatChanges() const {
    return disabled_compat_changes_;
  }
  // Query if a given compatibility change is enabled for the current process.
  // This also gets logged to logcat, and we add the information we logged in
  // reported_compat_changes_. This ensures we only log once per change id for the app's lifetime.
  bool IsChangeEnabled(uint64_t change_id);

  // Logs that the code path for this compatibility change has been reached.
  // This also gets logged to logcat, and we add the information we logged in
  // reported_compat_changes_. This ensures we only log once per change id for the app's lifetime.
  void LogChange(uint64_t change_id);

 private:
  // Get a string equivalent for a compatibility change state.
  static std::string_view ChangeStateToString(ChangeState s);
  // Report the state of a compatibility change to logcat.
  // TODO(145743810): also report to statsd.
  void ReportChange(uint64_t change_id, ChangeState state);

  // A set of disabled compat changes for the running app, all other changes are enabled.
  std::set<uint64_t> disabled_compat_changes_;

  // A set of repoted compat changes for the running app.
  std::set<uint64_t> reported_compat_changes_;
};

}  // namespace art

#endif  // ART_RUNTIME_COMPAT_FRAMEWORK_H_
