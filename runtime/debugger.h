/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef ART_RUNTIME_DEBUGGER_H_
#define ART_RUNTIME_DEBUGGER_H_

#include <pthread.h>

#include <set>
#include <string>
#include <vector>

#include "art_method.h"
#include "base/array_ref.h"
#include "base/locks.h"
#include "base/logging.h"
#include "jni.h"
#include "runtime_callbacks.h"
#include "thread.h"
#include "thread_state.h"

namespace art {

class Dbg {
 public:
  static void SetJdwpAllowed(bool allowed);
  static bool IsJdwpAllowed();

  // Invoked by the GC in case we need to keep DDMS informed.
  static void GcDidFinish() REQUIRES(!Locks::mutator_lock_);

  static uint8_t ToJdwpThreadStatus(ThreadState state);

  // Indicates whether we need to force the use of interpreter when returning from the
  // interpreter into the runtime. This allows to deoptimize the stack and continue
  // execution with interpreter for debugging.
  static bool IsForcedInterpreterNeededForUpcall(Thread* thread, ArtMethod* m)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (LIKELY(!thread->HasDebuggerShadowFrames())) {
      return false;
    }
    // If we have debugger stack frames we always need to go back to interpreter unless we are
    // native or a proxy.
    return m != nullptr && !m->IsProxyMethod() && !m->IsNative();
  }

  // Indicates whether we need to force the use of interpreter when handling an
  // exception. This allows to deoptimize the stack and continue execution with
  // the interpreter.
  // Note: the interpreter will start by handling the exception when executing
  // the deoptimized frames.
  static bool IsForcedInterpreterNeededForException(Thread* thread)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (LIKELY(!thread->HasDebuggerShadowFrames())) {
      return false;
    }
    return IsForcedInterpreterNeededForExceptionImpl(thread);
  }


  /*
   * DDM support.
   */
  static void DdmSendThreadNotification(Thread* t, uint32_t type)
      REQUIRES_SHARED(Locks::mutator_lock_);
  static void DdmSetThreadNotification(bool enable)
      REQUIRES(!Locks::thread_list_lock_);
  static bool DdmHandleChunk(
      JNIEnv* env,
      uint32_t type,
      const ArrayRef<const jbyte>& data,
      /*out*/uint32_t* out_type,
      /*out*/std::vector<uint8_t>* out_data);

  static void DdmConnected() REQUIRES_SHARED(Locks::mutator_lock_);
  static void DdmDisconnected() REQUIRES_SHARED(Locks::mutator_lock_);

  /*
   * Allocation tracking support.
   */
  static void SetAllocTrackingEnabled(bool enabled) REQUIRES(!Locks::alloc_tracker_lock_);
  static jbyteArray GetRecentAllocations()
      REQUIRES(!Locks::alloc_tracker_lock_) REQUIRES_SHARED(Locks::mutator_lock_);
  static void DumpRecentAllocations() REQUIRES(!Locks::alloc_tracker_lock_);

  enum HpifWhen {
    HPIF_WHEN_NEVER = 0,
    HPIF_WHEN_NOW = 1,
    HPIF_WHEN_NEXT_GC = 2,
    HPIF_WHEN_EVERY_GC = 3
  };
  static int DdmHandleHpifChunk(HpifWhen when)
      REQUIRES_SHARED(Locks::mutator_lock_);

  enum HpsgWhen {
    HPSG_WHEN_NEVER = 0,
    HPSG_WHEN_EVERY_GC = 1,
  };
  enum HpsgWhat {
    HPSG_WHAT_MERGED_OBJECTS = 0,
    HPSG_WHAT_DISTINCT_OBJECTS = 1,
  };
  static bool DdmHandleHpsgNhsgChunk(HpsgWhen when, HpsgWhat what, bool native);

  static void DdmSendHeapInfo(HpifWhen reason)
      REQUIRES_SHARED(Locks::mutator_lock_);
  static void DdmSendHeapSegments(bool native)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static ThreadLifecycleCallback* GetThreadLifecycleCallback() {
    return &thread_lifecycle_callback_;
  }

 private:
  static void DdmBroadcast(bool connect) REQUIRES_SHARED(Locks::mutator_lock_);

  static void PostThreadStart(Thread* t)
      REQUIRES_SHARED(Locks::mutator_lock_);
  static void PostThreadDeath(Thread* t)
      REQUIRES_SHARED(Locks::mutator_lock_);
  static void PostThreadStartOrStop(Thread*, uint32_t)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static bool IsForcedInterpreterNeededForExceptionImpl(Thread* thread)
      REQUIRES_SHARED(Locks::mutator_lock_);

  class DbgThreadLifecycleCallback : public ThreadLifecycleCallback {
   public:
    void ThreadStart(Thread* self) override REQUIRES_SHARED(Locks::mutator_lock_);
    void ThreadDeath(Thread* self) override REQUIRES_SHARED(Locks::mutator_lock_);
  };

  static DbgThreadLifecycleCallback thread_lifecycle_callback_;

  DISALLOW_COPY_AND_ASSIGN(Dbg);
};

#define CHUNK_TYPE(_name) \
    static_cast<uint32_t>((_name)[0] << 24 | (_name)[1] << 16 | (_name)[2] << 8 | (_name)[3])

}  // namespace art

#endif  // ART_RUNTIME_DEBUGGER_H_
