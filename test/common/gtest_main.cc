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

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "base/logging.h"
#include "base/mem_map.h"
#include "base/mutex.h"
#include "gtest_extras/IsolateMain.h"
#include "runtime.h"

extern "C" bool GetInitialArgs(const char*** args, size_t* num_args) {
  static const char* initial_args[] = {
      "--deadline_threshold_ms=1200000",  // hwasan takes ~10min.
      "--slow_threshold_ms=300000",
  };
  *args = initial_args;
  *num_args = 2;
  return true;
}

// Allow other test code to run global initialization/configuration before gtest infra takes over.
extern "C" __attribute__((visibility("default"))) __attribute__((weak)) void ArtTestGlobalInit();

int main(int argc, char** argv, char** envp) {
  // Gtests can be very noisy. For example, an executable with multiple tests will trigger native
  // bridge warnings. The following line reduces the minimum log severity to ERROR and suppresses
  // everything else. In case you want to see all messages, comment out the line.
  setenv("ANDROID_LOG_TAGS", "*:e", 1);

  art::Locks::Init();
  art::InitLogging(argv, art::Runtime::Abort);
  art::MemMap::Init();
  LOG(INFO) << "Running main() from common_runtime_test.cc...";
  if (ArtTestGlobalInit != nullptr) {
    ArtTestGlobalInit();
  }
  return IsolateMain(argc, argv, envp);
}
