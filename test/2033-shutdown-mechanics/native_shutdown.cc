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

#include "base/time_utils.h"
#include "base/utils.h"
#include "jni/jni_env_ext.h"
#include "jni/jni_internal.h"

#include "jni.h"

#include <stdio.h>

namespace art {

static void MaybePrintTime() {
  constexpr bool kPrintTime = false;
  if (kPrintTime) {
    printf("At %u msecs:", static_cast<int>(MilliTime()));
  }
}


extern "C" [[noreturn]] JNIEXPORT void JNICALL Java_Main_monitorShutdown(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED) {
  bool found_shutdown = false;
  bool found_runtime_deleted = false;
  JNIEnvExt* const extEnv = down_cast<JNIEnvExt*>(env);
  while (true) {
    if (!found_shutdown && env->functions == GetRuntimeShutdownNativeInterface()) {
      found_shutdown = true;
      MaybePrintTime();
      printf("Saw RuntimeShutdownFunctions\n");
      fflush(stdout);
    }
    if (!found_runtime_deleted && extEnv->IsRuntimeDeleted()) {
      found_runtime_deleted = true;
      MaybePrintTime();
      printf("Saw RuntimeDeleted\n");
      fflush(stdout);
    }
    if (found_shutdown && found_runtime_deleted) {
      // All JNI calls should now get rerouted to SleepForever();
      (void) env->NewByteArray(17);
      printf("Unexpectedly returned from JNI call\n");
      fflush(stdout);
      SleepForever();
    }
  }
}

}  // namespace art


