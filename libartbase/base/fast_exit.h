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

#ifndef ART_LIBARTBASE_BASE_FAST_EXIT_H_
#define ART_LIBARTBASE_BASE_FAST_EXIT_H_

// Header-only definition of `art::FastExit`.
//
// Ideally, this routine should be declared in `base/os.h` and defined in
// `base/os_linux.cc`, but as `libartbase` is not linked (directly) with
// `dalvikvm`, we would not be able to easily use `art::FastExit` in
// `dex2oat`. Use a header-only approach and define `art::FastExit` in its own
// file for clarity.

#include <base/macros.h>

namespace art {

#ifdef __ANDROID_CLANG_COVERAGE__
static constexpr bool kAndroidClangCoverage = true;
#else
static constexpr bool kAndroidClangCoverage = false;
#endif

// Terminate program without completely cleaning the resources (e.g. without
// calling destructors), unless ART is built with Clang (native) code coverage
// instrumentation; in that case, exit normally to allow LLVM's code coverage
// profile dumping routine (`__llvm_profile_write_file`), registered via
// `atexit` in Android when Clang instrumentation is enabled, to be called
// before terminating the program.
NO_RETURN inline void FastExit(int exit_code) {
  if (kAndroidClangCoverage) {
    exit(exit_code);
  } else {
    _exit(exit_code);
  }
}

}  // namespace art

#endif  // ART_LIBARTBASE_BASE_FAST_EXIT_H_
