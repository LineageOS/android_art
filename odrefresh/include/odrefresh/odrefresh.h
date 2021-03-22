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

#ifndef ART_ODREFRESH_INCLUDE_ODREFRESH_ODREFRESH_H_
#define ART_ODREFRESH_INCLUDE_ODREFRESH_ODREFRESH_H_

#include <sysexits.h>

namespace art {
namespace odrefresh {

static constexpr const char* kOdrefreshArtifactDirectory =
    "/data/misc/apexdata/com.android.art/dalvik-cache";

//
// Exit codes from the odrefresh process (in addition to standard exit codes in sysexits.h).
//
// NB if odrefresh crashes, then the caller should not sign any artifacts and should remove any
// unsigned artifacts under `kOdrefreshArtifactDirectory`.
//
enum ExitCode : int {
  // No compilation required, all artifacts look good or there is insufficient space to compile.
  // For ART APEX in the system image, there may be no artifacts present under
  // `kOdrefreshArtifactDirectory`.
  kOkay = EX_OK,

  // Compilation required (only returned for --check). Re-run program with --compile on the
  // command-line to generate + new artifacts under `kOdrefreshArtifactDirectory`.
  kCompilationRequired = EX__MAX + 1,

  // New artifacts successfully generated under `kOdrefreshArtifactDirectory`.
  kCompilationSuccess = EX__MAX + 2,

  // Compilation failed. Any artifacts under `kOdrefreshArtifactDirectory` are valid and should not
  // be removed. This may happen, for example, if compilation of boot extensions succeeds, but the
  // compilation of the system_server jars fails due to lack of storage space.
  kCompilationFailed = EX__MAX + 3,

  // Removal of existing artifacts (or files under `kOdrefreshArtifactDirectory`) failed. Artifacts
  // should be treated as invalid and should be removed if possible.
  kCleanupFailed = EX__MAX + 4,

  // Last exit code defined.
  kLastExitCode = kCleanupFailed,
};

static_assert(EX_OK == 0);
static_assert(ExitCode::kOkay < EX__BASE);
static_assert(ExitCode::kLastExitCode < 0xff);  // The `exit()` man page discusses the mask value.

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_INCLUDE_ODREFRESH_ODREFRESH_H_
