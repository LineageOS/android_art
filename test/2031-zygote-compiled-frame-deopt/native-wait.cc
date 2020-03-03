/*
 * Copyright 2020 The Android Open Source Project
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

#include <atomic>
#include <sstream>

#include "base/globals.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "jni.h"
#include "runtime.h"
#include "thread_list.h"

namespace art {
namespace NativeWait {

std::atomic<bool> native_waiting = false;
std::atomic<bool> native_wait = false;

// Perform late debuggable switch in the same way the zygote would (clear-jit,
// unmark-zygote, set-debuggable, deopt boot, restart jit). NB This skips
// restarting the heap threads since that doesn't seem to be needed to trigger
// b/144947842.
extern "C" JNIEXPORT void JNICALL Java_art_Test2031_simulateZygoteFork(JNIEnv*, jclass) {
  Runtime* runtime = Runtime::Current();
  bool has_jit = runtime->GetJit() != nullptr;
  if (has_jit) {
    runtime->GetJit()->PreZygoteFork();
  }
  runtime->SetAsZygoteChild(/*is_system_server=*/false, /*is_zygote=*/false);
  runtime->AddCompilerOption("--debuggable");
  runtime->SetJavaDebuggable(true);
  {
    // Deoptimize the boot image as it may be non-debuggable.
    ScopedSuspendAll ssa(__FUNCTION__);
    runtime->DeoptimizeBootImage();
  }

  if (has_jit) {
    runtime->GetJitCodeCache()->PostForkChildAction(false, false);
    runtime->GetJit()->PostForkChildAction(false, false);
    // We have "zygote" code that isn't really part of the BCP. just don't collect it.
    runtime->GetJitCodeCache()->SetGarbageCollectCode(false);
  }
}

extern "C" JNIEXPORT void JNICALL Java_art_Test2031_setupJvmti(JNIEnv* env,
                                                               jclass,
                                                               jstring testdir) {
  const char* td = env->GetStringUTFChars(testdir, nullptr);
  std::string testdir_str;
  testdir_str.resize(env->GetStringUTFLength(testdir));
  memcpy(testdir_str.data(), td, testdir_str.size());
  env->ReleaseStringUTFChars(testdir, td);
  std::ostringstream oss;
  Runtime* runtime = Runtime::Current();
  oss << testdir_str << (kIsDebugBuild ? "libtiagentd.so" : "libtiagent.so")
      << "=2031-zygote-compiled-frame-deopt,art";
  LOG(INFO) << "agent " << oss.str();
  runtime->AttachAgent(env, oss.str(), nullptr);
}
extern "C" JNIEXPORT void JNICALL Java_art_Test2031_waitForNativeSleep(JNIEnv*, jclass) {
  while (!native_waiting) {
  }
}
extern "C" JNIEXPORT void JNICALL Java_art_Test2031_wakeupNativeSleep(JNIEnv*, jclass) {
  native_wait = false;
}
extern "C" JNIEXPORT void JNICALL Java_art_Test2031_nativeSleep(JNIEnv*, jclass) {
  native_wait = true;
  do {
    native_waiting = true;
  } while (native_wait);
  native_waiting = false;
}

}  // namespace NativeWait
}  // namespace art
