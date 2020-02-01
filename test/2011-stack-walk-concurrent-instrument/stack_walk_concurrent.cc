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
#include <string_view>

#include "arch/context.h"
#include "art_method-inl.h"
#include "jni.h"
#include "scoped_thread_state_change.h"
#include "stack.h"
#include "thread.h"

namespace art {
namespace StackWalkConcurrentInstrument {

std::atomic<bool> instrument_waiting = false;
std::atomic<bool> instrumented = false;

// Spin lock.
static void WaitForInstrument() REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedThreadSuspension sts(Thread::Current(), ThreadState::kWaitingForDeoptimization);
  instrument_waiting = true;
  while (!instrumented) {
  }
}

class SelfStackWalkVisitor : public StackVisitor {
 public:
  explicit SelfStackWalkVisitor(Thread* thread) REQUIRES_SHARED(Locks::mutator_lock_)
      : StackVisitor(thread, Context::Create(), StackWalkKind::kIncludeInlinedFrames) {}

  bool VisitFrame() override REQUIRES_SHARED(Locks::mutator_lock_) {
    if (GetMethod()->GetNameView() == "$noinline$f") {
      CHECK(!found_f_);
      found_f_ = true;
    } else if (GetMethod()->GetNameView() == "$noinline$g") {
      CHECK(!found_g_);
      found_g_ = true;
      WaitForInstrument();
    } else if (GetMethod()->GetNameView() == "$noinline$h") {
      CHECK(!found_h_);
      found_h_ = true;
    }
    return true;
  }

  bool found_f_ = false;
  bool found_g_ = false;
  bool found_h_ = false;
};

extern "C" JNIEXPORT void JNICALL Java_Main_resetTest(JNIEnv*, jobject) {
  instrument_waiting = false;
  instrumented = false;
}

extern "C" JNIEXPORT void JNICALL Java_Main_doSelfStackWalk(JNIEnv*, jobject) {
  ScopedObjectAccess soa(Thread::Current());
  SelfStackWalkVisitor sswv(Thread::Current());
  sswv.WalkStack();
  CHECK(sswv.found_f_);
  CHECK(sswv.found_g_);
  CHECK(sswv.found_h_);
}
extern "C" JNIEXPORT void JNICALL Java_Main_waitAndDeopt(JNIEnv*, jobject, jobject target) {
  while (!instrument_waiting) {
  }
  bool timed_out = false;
  Thread* other = Runtime::Current()->GetThreadList()->SuspendThreadByPeer(
      target, true, SuspendReason::kInternal, &timed_out);
  CHECK(!timed_out);
  CHECK(other != nullptr);
  ScopedSuspendAll ssa(__FUNCTION__);
  Runtime::Current()->GetInstrumentation()->InstrumentThreadStack(other);
  MutexLock mu(Thread::Current(), *Locks::thread_suspend_count_lock_);
  bool updated = other->ModifySuspendCount(Thread::Current(), -1, nullptr, SuspendReason::kInternal);
  CHECK(updated);
  instrumented = true;
  return;
}

}  // namespace StackWalkConcurrentInstrument
}  // namespace art
