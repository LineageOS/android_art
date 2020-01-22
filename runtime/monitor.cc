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

#include "monitor-inl.h"

#include <vector>

#include "android-base/stringprintf.h"

#include "art_method-inl.h"
#include "base/logging.h"  // For VLOG.
#include "base/mutex.h"
#include "base/quasi_atomic.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/time_utils.h"
#include "class_linker.h"
#include "dex/dex_file-inl.h"
#include "dex/dex_file_types.h"
#include "dex/dex_instruction-inl.h"
#include "lock_word-inl.h"
#include "mirror/class-inl.h"
#include "mirror/object-inl.h"
#include "object_callbacks.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "thread.h"
#include "thread_list.h"
#include "verifier/method_verifier.h"
#include "well_known_classes.h"

namespace art {

using android::base::StringPrintf;

static constexpr uint64_t kDebugThresholdFudgeFactor = kIsDebugBuild ? 10 : 1;
static constexpr uint64_t kLongWaitMs = 100 * kDebugThresholdFudgeFactor;

/*
 * Every Object has a monitor associated with it, but not every Object is actually locked.  Even
 * the ones that are locked do not need a full-fledged monitor until a) there is actual contention
 * or b) wait() is called on the Object, or (c) we need to lock an object that also has an
 * identity hashcode.
 *
 * For Android, we have implemented a scheme similar to the one described in Bacon et al.'s
 * "Thin locks: featherweight synchronization for Java" (ACM 1998).  Things are even easier for us,
 * though, because we have a full 32 bits to work with.
 *
 * The two states of an Object's lock are referred to as "thin" and "fat".  A lock may transition
 * from the "thin" state to the "fat" state and this transition is referred to as inflation. We
 * deflate locks from time to time as part of heap trimming.
 *
 * The lock value itself is stored in mirror::Object::monitor_ and the representation is described
 * in the LockWord value type.
 *
 * Monitors provide:
 *  - mutually exclusive access to resources
 *  - a way for multiple threads to wait for notification
 *
 * In effect, they fill the role of both mutexes and condition variables.
 *
 * Only one thread can own the monitor at any time.  There may be several threads waiting on it
 * (the wait call unlocks it).  One or more waiting threads may be getting interrupted or notified
 * at any given time.
 */

uint32_t Monitor::lock_profiling_threshold_ = 0;
uint32_t Monitor::stack_dump_lock_profiling_threshold_ = 0;

void Monitor::Init(uint32_t lock_profiling_threshold,
                   uint32_t stack_dump_lock_profiling_threshold) {
  // It isn't great to always include the debug build fudge factor for command-
  // line driven arguments, but it's easier to adjust here than in the build.
  lock_profiling_threshold_ =
      lock_profiling_threshold * kDebugThresholdFudgeFactor;
  stack_dump_lock_profiling_threshold_ =
      stack_dump_lock_profiling_threshold * kDebugThresholdFudgeFactor;
}

Monitor::Monitor(Thread* self, Thread* owner, ObjPtr<mirror::Object> obj, int32_t hash_code)
    : monitor_lock_("a monitor lock", kMonitorLock),
      num_waiters_(0),
      owner_(owner),
      lock_count_(0),
      obj_(GcRoot<mirror::Object>(obj)),
      wait_set_(nullptr),
      wake_set_(nullptr),
      hash_code_(hash_code),
      lock_owner_(nullptr),
      lock_owner_method_(nullptr),
      lock_owner_dex_pc_(0),
      lock_owner_sum_(0),
      lock_owner_request_(nullptr),
      monitor_id_(MonitorPool::ComputeMonitorId(this, self)) {
#ifdef __LP64__
  DCHECK(false) << "Should not be reached in 64b";
  next_free_ = nullptr;
#endif
  // We should only inflate a lock if the owner is ourselves or suspended. This avoids a race
  // with the owner unlocking the thin-lock.
  CHECK(owner == nullptr || owner == self || owner->IsSuspended());
  // The identity hash code is set for the life time of the monitor.
}

Monitor::Monitor(Thread* self,
                 Thread* owner,
                 ObjPtr<mirror::Object> obj,
                 int32_t hash_code,
                 MonitorId id)
    : monitor_lock_("a monitor lock", kMonitorLock),
      num_waiters_(0),
      owner_(owner),
      lock_count_(0),
      obj_(GcRoot<mirror::Object>(obj)),
      wait_set_(nullptr),
      wake_set_(nullptr),
      hash_code_(hash_code),
      lock_owner_(nullptr),
      lock_owner_method_(nullptr),
      lock_owner_dex_pc_(0),
      lock_owner_sum_(0),
      lock_owner_request_(nullptr),
      monitor_id_(id) {
#ifdef __LP64__
  next_free_ = nullptr;
#endif
  // We should only inflate a lock if the owner is ourselves or suspended. This avoids a race
  // with the owner unlocking the thin-lock.
  CHECK(owner == nullptr || owner == self || owner->IsSuspended());
  // The identity hash code is set for the life time of the monitor.
}

int32_t Monitor::GetHashCode() {
  int32_t hc = hash_code_.load(std::memory_order_relaxed);
  if (!HasHashCode()) {
    // Use a strong CAS to prevent spurious failures since these can make the boot image
    // non-deterministic.
    hash_code_.CompareAndSetStrongRelaxed(0, mirror::Object::GenerateIdentityHashCode());
    hc = hash_code_.load(std::memory_order_relaxed);
  }
  DCHECK(HasHashCode());
  return hc;
}

void Monitor::SetLockingMethod(Thread* owner) {
  DCHECK(owner == Thread::Current() || owner->IsSuspended());
  // Do not abort on dex pc errors. This can easily happen when we want to dump a stack trace on
  // abort.
  ArtMethod* lock_owner_method;
  uint32_t lock_owner_dex_pc;
  lock_owner_method = owner->GetCurrentMethod(&lock_owner_dex_pc, false);
  if (lock_owner_method != nullptr && UNLIKELY(lock_owner_method->IsProxyMethod())) {
    // Grab another frame. Proxy methods are not helpful for lock profiling. This should be rare
    // enough that it's OK to walk the stack twice.
    struct NextMethodVisitor final : public StackVisitor {
      explicit NextMethodVisitor(Thread* thread) REQUIRES_SHARED(Locks::mutator_lock_)
          : StackVisitor(thread,
                         nullptr,
                         StackVisitor::StackWalkKind::kIncludeInlinedFrames,
                         false),
            count_(0),
            method_(nullptr),
            dex_pc_(0) {}
      bool VisitFrame() override REQUIRES_SHARED(Locks::mutator_lock_) {
        ArtMethod* m = GetMethod();
        if (m->IsRuntimeMethod()) {
          // Continue if this is a runtime method.
          return true;
        }
        count_++;
        if (count_ == 2u) {
          method_ = m;
          dex_pc_ = GetDexPc(false);
          return false;
        }
        return true;
      }
      size_t count_;
      ArtMethod* method_;
      uint32_t dex_pc_;
    };
    NextMethodVisitor nmv(owner_.load(std::memory_order_relaxed));
    nmv.WalkStack();
    lock_owner_method = nmv.method_;
    lock_owner_dex_pc = nmv.dex_pc_;
  }
  SetLockOwnerInfo(lock_owner_method, lock_owner_dex_pc, owner);
  DCHECK(lock_owner_method == nullptr || !lock_owner_method->IsProxyMethod());
}

void Monitor::SetLockingMethodNoProxy(Thread *owner) {
  DCHECK(owner == Thread::Current());
  uint32_t lock_owner_dex_pc;
  ArtMethod* lock_owner_method = owner->GetCurrentMethod(&lock_owner_dex_pc);
  // We don't expect a proxy method here.
  DCHECK(lock_owner_method == nullptr || !lock_owner_method->IsProxyMethod());
  SetLockOwnerInfo(lock_owner_method, lock_owner_dex_pc, owner);
}

bool Monitor::Install(Thread* self) NO_THREAD_SAFETY_ANALYSIS {
  // This may or may not result in acquiring monitor_lock_. Its behavior is much more complicated
  // than what clang thread safety analysis understands.
  // Monitor is not yet public.
  Thread* owner = owner_.load(std::memory_order_relaxed);
  CHECK(owner == nullptr || owner == self || (ART_USE_FUTEXES && owner->IsSuspended()));
  // Propagate the lock state.
  LockWord lw(GetObject()->GetLockWord(false));
  switch (lw.GetState()) {
    case LockWord::kThinLocked: {
      DCHECK(owner != nullptr);
      CHECK_EQ(owner->GetThreadId(), lw.ThinLockOwner());
      DCHECK_EQ(monitor_lock_.GetExclusiveOwnerTid(), 0) << " my tid = " << SafeGetTid(self);
      lock_count_ = lw.ThinLockCount();
#if ART_USE_FUTEXES
      monitor_lock_.ExclusiveLockUncontendedFor(owner);
#else
      monitor_lock_.ExclusiveLock(owner);
#endif
      DCHECK_EQ(monitor_lock_.GetExclusiveOwnerTid(), owner->GetTid())
          << " my tid = " << SafeGetTid(self);
      LockWord fat(this, lw.GCState());
      // Publish the updated lock word, which may race with other threads.
      bool success = GetObject()->CasLockWord(lw, fat, CASMode::kWeak, std::memory_order_release);
      if (success) {
        if (ATraceEnabled()) {
          SetLockingMethod(owner);
        }
        return true;
      } else {
#if ART_USE_FUTEXES
        monitor_lock_.ExclusiveUnlockUncontended();
#else
        for (uint32_t i = 0; i <= lockCount; ++i) {
          monitor_lock_.ExclusiveUnlock(owner);
        }
#endif
        return false;
      }
    }
    case LockWord::kHashCode: {
      CHECK_EQ(hash_code_.load(std::memory_order_relaxed), static_cast<int32_t>(lw.GetHashCode()));
      DCHECK_EQ(monitor_lock_.GetExclusiveOwnerTid(), 0) << " my tid = " << SafeGetTid(self);
      LockWord fat(this, lw.GCState());
      return GetObject()->CasLockWord(lw, fat, CASMode::kWeak, std::memory_order_release);
    }
    case LockWord::kFatLocked: {
      // The owner_ is suspended but another thread beat us to install a monitor.
      return false;
    }
    case LockWord::kUnlocked: {
      LOG(FATAL) << "Inflating unlocked lock word";
      UNREACHABLE();
    }
    default: {
      LOG(FATAL) << "Invalid monitor state " << lw.GetState();
      UNREACHABLE();
    }
  }
}

Monitor::~Monitor() {
  // Deflated monitors have a null object.
}

void Monitor::AppendToWaitSet(Thread* thread) {
  // Not checking that the owner is equal to this thread, since we've released
  // the monitor by the time this method is called.
  DCHECK(thread != nullptr);
  DCHECK(thread->GetWaitNext() == nullptr) << thread->GetWaitNext();
  if (wait_set_ == nullptr) {
    wait_set_ = thread;
    return;
  }

  // push_back.
  Thread* t = wait_set_;
  while (t->GetWaitNext() != nullptr) {
    t = t->GetWaitNext();
  }
  t->SetWaitNext(thread);
}

void Monitor::RemoveFromWaitSet(Thread *thread) {
  DCHECK(owner_ == Thread::Current());
  DCHECK(thread != nullptr);
  auto remove = [&](Thread*& set){
    if (set != nullptr) {
      if (set == thread) {
        set = thread->GetWaitNext();
        thread->SetWaitNext(nullptr);
        return true;
      }
      Thread* t = set;
      while (t->GetWaitNext() != nullptr) {
        if (t->GetWaitNext() == thread) {
          t->SetWaitNext(thread->GetWaitNext());
          thread->SetWaitNext(nullptr);
          return true;
        }
        t = t->GetWaitNext();
      }
    }
    return false;
  };
  if (remove(wait_set_)) {
    return;
  }
  remove(wake_set_);
}

void Monitor::SetObject(ObjPtr<mirror::Object> object) {
  obj_ = GcRoot<mirror::Object>(object);
}

// This function is inlined and just helps to not have the VLOG and ATRACE check at all the
// potential tracing points.
void Monitor::AtraceMonitorLock(Thread* self, ObjPtr<mirror::Object> obj, bool is_wait) {
  if (UNLIKELY(VLOG_IS_ON(systrace_lock_logging) && ATraceEnabled())) {
    AtraceMonitorLockImpl(self, obj, is_wait);
  }
}

void Monitor::AtraceMonitorLockImpl(Thread* self, ObjPtr<mirror::Object> obj, bool is_wait) {
  // Wait() requires a deeper call stack to be useful. Otherwise you'll see "Waiting at
  // Object.java". Assume that we'll wait a nontrivial amount, so it's OK to do a longer
  // stack walk than if !is_wait.
  const size_t wanted_frame_number = is_wait ? 1U : 0U;

  ArtMethod* method = nullptr;
  uint32_t dex_pc = 0u;

  size_t current_frame_number = 0u;
  StackVisitor::WalkStack(
      // Note: Adapted from CurrentMethodVisitor in thread.cc. We must not resolve here.
      [&](const art::StackVisitor* stack_visitor) REQUIRES_SHARED(Locks::mutator_lock_) {
        ArtMethod* m = stack_visitor->GetMethod();
        if (m == nullptr || m->IsRuntimeMethod()) {
          // Runtime method, upcall, or resolution issue. Skip.
          return true;
        }

        // Is this the requested frame?
        if (current_frame_number == wanted_frame_number) {
          method = m;
          dex_pc = stack_visitor->GetDexPc(false /* abort_on_error*/);
          return false;
        }

        // Look for more.
        current_frame_number++;
        return true;
      },
      self,
      /* context= */ nullptr,
      art::StackVisitor::StackWalkKind::kIncludeInlinedFrames);

  const char* prefix = is_wait ? "Waiting on " : "Locking ";

  const char* filename;
  int32_t line_number;
  TranslateLocation(method, dex_pc, &filename, &line_number);

  // It would be nice to have a stable "ID" for the object here. However, the only stable thing
  // would be the identity hashcode. But we cannot use IdentityHashcode here: For one, there are
  // times when it is unsafe to make that call (see stack dumping for an explanation). More
  // importantly, we would have to give up on thin-locking when adding systrace locks, as the
  // identity hashcode is stored in the lockword normally (so can't be used with thin-locks).
  //
  // Because of thin-locks we also cannot use the monitor id (as there is no monitor). Monitor ids
  // also do not have to be stable, as the monitor may be deflated.
  std::string tmp = StringPrintf("%s %d at %s:%d",
      prefix,
      (obj == nullptr ? -1 : static_cast<int32_t>(reinterpret_cast<uintptr_t>(obj.Ptr()))),
      (filename != nullptr ? filename : "null"),
      line_number);
  ATraceBegin(tmp.c_str());
}

void Monitor::AtraceMonitorUnlock() {
  if (UNLIKELY(VLOG_IS_ON(systrace_lock_logging))) {
    ATraceEnd();
  }
}

std::string Monitor::PrettyContentionInfo(const std::string& owner_name,
                                          pid_t owner_tid,
                                          ArtMethod* owners_method,
                                          uint32_t owners_dex_pc,
                                          size_t num_waiters) {
  Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
  const char* owners_filename;
  int32_t owners_line_number = 0;
  if (owners_method != nullptr) {
    TranslateLocation(owners_method, owners_dex_pc, &owners_filename, &owners_line_number);
  }
  std::ostringstream oss;
  oss << "monitor contention with owner " << owner_name << " (" << owner_tid << ")";
  if (owners_method != nullptr) {
    oss << " at " << owners_method->PrettyMethod();
    oss << "(" << owners_filename << ":" << owners_line_number << ")";
  }
  oss << " waiters=" << num_waiters;
  return oss.str();
}

bool Monitor::TryLock(Thread* self, bool spin) {
  Thread *owner = owner_.load(std::memory_order_relaxed);
  if (owner == self) {
    lock_count_++;
    CHECK_NE(lock_count_, 0u);  // Abort on overflow.
  } else {
    bool success = spin ? monitor_lock_.ExclusiveTryLockWithSpinning(self)
        : monitor_lock_.ExclusiveTryLock(self);
    if (!success) {
      return false;
    }
    DCHECK(owner_.load(std::memory_order_relaxed) == nullptr);
    owner_.store(self, std::memory_order_relaxed);
    CHECK_EQ(lock_count_, 0u);
    if (ATraceEnabled()) {
      SetLockingMethodNoProxy(self);
    }
  }
  DCHECK(monitor_lock_.IsExclusiveHeld(self));
  AtraceMonitorLock(self, GetObject(), /* is_wait= */ false);
  return true;
}

template <LockReason reason>
void Monitor::Lock(Thread* self) {
  bool called_monitors_callback = false;
  if (TryLock(self, /*spin=*/ true)) {
    // TODO: This preserves original behavior. Correct?
    if (called_monitors_callback) {
      CHECK(reason == LockReason::kForLock);
      Runtime::Current()->GetRuntimeCallbacks()->MonitorContendedLocked(this);
    }
    return;
  }
  // Contended; not reentrant. We hold no locks, so tread carefully.
  const bool log_contention = (lock_profiling_threshold_ != 0);
  uint64_t wait_start_ms = log_contention ? MilliTime() : 0;

  Thread *orig_owner = nullptr;
  ArtMethod* owners_method;
  uint32_t owners_dex_pc;

  // Do this before releasing the mutator lock so that we don't get deflated.
  size_t num_waiters = num_waiters_.fetch_add(1, std::memory_order_relaxed);

  bool started_trace = false;
  if (ATraceEnabled() && owner_.load(std::memory_order_relaxed) != nullptr) {
    // Acquiring thread_list_lock_ ensures that owner doesn't disappear while
    // we're looking at it.
    Locks::thread_list_lock_->ExclusiveLock(self);
    orig_owner = owner_.load(std::memory_order_relaxed);
    if (orig_owner != nullptr) {  // Did the owner_ give the lock up?
      const uint32_t orig_owner_thread_id = orig_owner->GetThreadId();
      GetLockOwnerInfo(&owners_method, &owners_dex_pc, orig_owner);
      std::ostringstream oss;
      std::string name;
      orig_owner->GetThreadName(name);
      oss << PrettyContentionInfo(name,
                                  orig_owner_thread_id,
                                  owners_method,
                                  owners_dex_pc,
                                  num_waiters);
      Locks::thread_list_lock_->ExclusiveUnlock(self);
      // Add info for contending thread.
      uint32_t pc;
      ArtMethod* m = self->GetCurrentMethod(&pc);
      const char* filename;
      int32_t line_number;
      TranslateLocation(m, pc, &filename, &line_number);
      oss << " blocking from "
          << ArtMethod::PrettyMethod(m) << "(" << (filename != nullptr ? filename : "null")
          << ":" << line_number << ")";
      ATraceBegin(oss.str().c_str());
      started_trace = true;
    } else {
      Locks::thread_list_lock_->ExclusiveUnlock(self);
    }
  }
  if (log_contention) {
    // Request the current holder to set lock_owner_info.
    // Do this even if tracing is enabled, so we semi-consistently get the information
    // corresponding to MonitorExit.
    // TODO: Consider optionally obtaining a stack trace here via a checkpoint.  That would allow
    // us to see what the other thread is doing while we're waiting.
    orig_owner = owner_.load(std::memory_order_relaxed);
    lock_owner_request_.store(orig_owner, std::memory_order_relaxed);
  }
  // Call the contended locking cb once and only once. Also only call it if we are locking for
  // the first time, not during a Wait wakeup.
  if (reason == LockReason::kForLock && !called_monitors_callback) {
    called_monitors_callback = true;
    Runtime::Current()->GetRuntimeCallbacks()->MonitorContendedLocking(this);
  }
  self->SetMonitorEnterObject(GetObject().Ptr());
  {
    ScopedThreadSuspension tsc(self, kBlocked);  // Change to blocked and give up mutator_lock_.

    // Acquire monitor_lock_ without mutator_lock_, expecting to block this time.
    // We already tried spinning above. The shutdown procedure currently assumes we stop
    // touching monitors shortly after we suspend, so don't spin again here.
    monitor_lock_.ExclusiveLock(self);

    if (log_contention && orig_owner != nullptr) {
      // Woken from contention.
      uint64_t wait_ms = MilliTime() - wait_start_ms;
      uint32_t sample_percent;
      if (wait_ms >= lock_profiling_threshold_) {
        sample_percent = 100;
      } else {
        sample_percent = 100 * wait_ms / lock_profiling_threshold_;
      }
      if (sample_percent != 0 && (static_cast<uint32_t>(rand() % 100) < sample_percent)) {
        // Do this unconditionally for consistency. It's possible another thread
        // snuck in in the middle, and tracing was enabled. In that case, we may get its
        // MonitorEnter information. We can live with that.
        GetLockOwnerInfo(&owners_method, &owners_dex_pc, orig_owner);

        // Reacquire mutator_lock_ for logging.
        ScopedObjectAccess soa(self);

        const bool should_dump_stacks = stack_dump_lock_profiling_threshold_ > 0 &&
            wait_ms > stack_dump_lock_profiling_threshold_;

        // Acquire thread-list lock to find thread and keep it from dying until we've got all
        // the info we need.
        Locks::thread_list_lock_->ExclusiveLock(self);

        // Is there still a thread at the same address as the original owner?
        // We tolerate the fact that it may occasionally be the wrong one.
        if (Runtime::Current()->GetThreadList()->Contains(orig_owner)) {
          uint32_t original_owner_tid = orig_owner->GetTid();  // System thread id.
          std::string original_owner_name;
          orig_owner->GetThreadName(original_owner_name);
          std::string owner_stack_dump;

          if (should_dump_stacks) {
            // Very long contention. Dump stacks.
            struct CollectStackTrace : public Closure {
              void Run(art::Thread* thread) override
                  REQUIRES_SHARED(art::Locks::mutator_lock_) {
                thread->DumpJavaStack(oss);
              }

              std::ostringstream oss;
            };
            CollectStackTrace owner_trace;
            // RequestSynchronousCheckpoint releases the thread_list_lock_ as a part of its
            // execution.
            orig_owner->RequestSynchronousCheckpoint(&owner_trace);
            owner_stack_dump = owner_trace.oss.str();
          } else {
            Locks::thread_list_lock_->ExclusiveUnlock(self);
          }

          // This is all the data we need. We dropped the thread-list lock, it's OK for the
          // owner to go away now.

          if (should_dump_stacks) {
            // Give the detailed traces for really long contention.
            // This must be here (and not above) because we cannot hold the thread-list lock
            // while running the checkpoint.
            std::ostringstream self_trace_oss;
            self->DumpJavaStack(self_trace_oss);

            uint32_t pc;
            ArtMethod* m = self->GetCurrentMethod(&pc);

            LOG(WARNING) << "Long "
                << PrettyContentionInfo(original_owner_name,
                                        original_owner_tid,
                                        owners_method,
                                        owners_dex_pc,
                                        num_waiters)
                << " in " << ArtMethod::PrettyMethod(m) << " for "
                << PrettyDuration(MsToNs(wait_ms)) << "\n"
                << "Current owner stack:\n" << owner_stack_dump
                << "Contender stack:\n" << self_trace_oss.str();
          } else if (wait_ms > kLongWaitMs && owners_method != nullptr) {
            uint32_t pc;
            ArtMethod* m = self->GetCurrentMethod(&pc);
            // TODO: We should maybe check that original_owner is still a live thread.
            LOG(WARNING) << "Long "
                << PrettyContentionInfo(original_owner_name,
                                        original_owner_tid,
                                        owners_method,
                                        owners_dex_pc,
                                        num_waiters)
                << " in " << ArtMethod::PrettyMethod(m) << " for "
                << PrettyDuration(MsToNs(wait_ms));
          }
          LogContentionEvent(self,
                            wait_ms,
                            sample_percent,
                            owners_method,
                            owners_dex_pc);
        } else {
          Locks::thread_list_lock_->ExclusiveUnlock(self);
        }
      }
    }
  }
  // We've successfully acquired monitor_lock_, released thread_list_lock, and are runnable.

  // We avoided touching monitor fields while suspended, so set owner_ here.
  owner_.store(self, std::memory_order_relaxed);
  DCHECK_EQ(lock_count_, 0u);

  if (ATraceEnabled()) {
    SetLockingMethodNoProxy(self);
  }
  if (started_trace) {
    ATraceEnd();
  }
  self->SetMonitorEnterObject(nullptr);
  num_waiters_.fetch_sub(1, std::memory_order_relaxed);
  DCHECK(monitor_lock_.IsExclusiveHeld(self));
  // We need to pair this with a single contended locking call. NB we match the RI behavior and call
  // this even if MonitorEnter failed.
  if (called_monitors_callback) {
    CHECK(reason == LockReason::kForLock);
    Runtime::Current()->GetRuntimeCallbacks()->MonitorContendedLocked(this);
  }
}

template void Monitor::Lock<LockReason::kForLock>(Thread* self);
template void Monitor::Lock<LockReason::kForWait>(Thread* self);

static void ThrowIllegalMonitorStateExceptionF(const char* fmt, ...)
                                              __attribute__((format(printf, 1, 2)));

static void ThrowIllegalMonitorStateExceptionF(const char* fmt, ...)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  va_list args;
  va_start(args, fmt);
  Thread* self = Thread::Current();
  self->ThrowNewExceptionV("Ljava/lang/IllegalMonitorStateException;", fmt, args);
  if (!Runtime::Current()->IsStarted() || VLOG_IS_ON(monitor)) {
    std::ostringstream ss;
    self->Dump(ss);
    LOG(Runtime::Current()->IsStarted() ? ::android::base::INFO : ::android::base::ERROR)
        << self->GetException()->Dump() << "\n" << ss.str();
  }
  va_end(args);
}

static std::string ThreadToString(Thread* thread) {
  if (thread == nullptr) {
    return "nullptr";
  }
  std::ostringstream oss;
  // TODO: alternatively, we could just return the thread's name.
  oss << *thread;
  return oss.str();
}

void Monitor::FailedUnlock(ObjPtr<mirror::Object> o,
                           uint32_t expected_owner_thread_id,
                           uint32_t found_owner_thread_id,
                           Monitor* monitor) {
  std::string current_owner_string;
  std::string expected_owner_string;
  std::string found_owner_string;
  uint32_t current_owner_thread_id = 0u;
  {
    MutexLock mu(Thread::Current(), *Locks::thread_list_lock_);
    ThreadList* const thread_list = Runtime::Current()->GetThreadList();
    Thread* expected_owner = thread_list->FindThreadByThreadId(expected_owner_thread_id);
    Thread* found_owner = thread_list->FindThreadByThreadId(found_owner_thread_id);

    // Re-read owner now that we hold lock.
    Thread* current_owner = (monitor != nullptr) ? monitor->GetOwner() : nullptr;
    if (current_owner != nullptr) {
      current_owner_thread_id = current_owner->GetThreadId();
    }
    // Get short descriptions of the threads involved.
    current_owner_string = ThreadToString(current_owner);
    expected_owner_string = expected_owner != nullptr ? ThreadToString(expected_owner) : "unnamed";
    found_owner_string = found_owner != nullptr ? ThreadToString(found_owner) : "unnamed";
  }

  if (current_owner_thread_id == 0u) {
    if (found_owner_thread_id == 0u) {
      ThrowIllegalMonitorStateExceptionF("unlock of unowned monitor on object of type '%s'"
                                         " on thread '%s'",
                                         mirror::Object::PrettyTypeOf(o).c_str(),
                                         expected_owner_string.c_str());
    } else {
      // Race: the original read found an owner but now there is none
      ThrowIllegalMonitorStateExceptionF("unlock of monitor owned by '%s' on object of type '%s'"
                                         " (where now the monitor appears unowned) on thread '%s'",
                                         found_owner_string.c_str(),
                                         mirror::Object::PrettyTypeOf(o).c_str(),
                                         expected_owner_string.c_str());
    }
  } else {
    if (found_owner_thread_id == 0u) {
      // Race: originally there was no owner, there is now
      ThrowIllegalMonitorStateExceptionF("unlock of monitor owned by '%s' on object of type '%s'"
                                         " (originally believed to be unowned) on thread '%s'",
                                         current_owner_string.c_str(),
                                         mirror::Object::PrettyTypeOf(o).c_str(),
                                         expected_owner_string.c_str());
    } else {
      if (found_owner_thread_id != current_owner_thread_id) {
        // Race: originally found and current owner have changed
        ThrowIllegalMonitorStateExceptionF("unlock of monitor originally owned by '%s' (now"
                                           " owned by '%s') on object of type '%s' on thread '%s'",
                                           found_owner_string.c_str(),
                                           current_owner_string.c_str(),
                                           mirror::Object::PrettyTypeOf(o).c_str(),
                                           expected_owner_string.c_str());
      } else {
        ThrowIllegalMonitorStateExceptionF("unlock of monitor owned by '%s' on object of type '%s'"
                                           " on thread '%s",
                                           current_owner_string.c_str(),
                                           mirror::Object::PrettyTypeOf(o).c_str(),
                                           expected_owner_string.c_str());
      }
    }
  }
}

bool Monitor::Unlock(Thread* self) {
  DCHECK(self != nullptr);
  Thread* owner = owner_.load(std::memory_order_relaxed);
  if (owner == self) {
    // We own the monitor, so nobody else can be in here.
    CheckLockOwnerRequest(self);
    AtraceMonitorUnlock();
    if (lock_count_ == 0) {
      owner_.store(nullptr, std::memory_order_relaxed);
      SignalWaiterAndReleaseMonitorLock(self);
    } else {
      --lock_count_;
      DCHECK(monitor_lock_.IsExclusiveHeld(self));
      DCHECK_EQ(owner_.load(std::memory_order_relaxed), self);
      // Keep monitor_lock_, but pretend we released it.
      FakeUnlockMonitorLock();
    }
    return true;
  }
  // We don't own this, so we're not allowed to unlock it.
  // The JNI spec says that we should throw IllegalMonitorStateException in this case.
  uint32_t owner_thread_id = 0u;
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    owner = owner_.load(std::memory_order_relaxed);
    if (owner != nullptr) {
      owner_thread_id = owner->GetThreadId();
    }
  }
  FailedUnlock(GetObject(), self->GetThreadId(), owner_thread_id, this);
  // Pretend to release monitor_lock_, which we should not.
  FakeUnlockMonitorLock();
  return false;
}

void Monitor::SignalWaiterAndReleaseMonitorLock(Thread* self) {
  // We want to release the monitor and signal up to one thread that was waiting
  // but has since been notified.
  DCHECK_EQ(lock_count_, 0u);
  DCHECK(monitor_lock_.IsExclusiveHeld(self));
  while (wake_set_ != nullptr) {
    // No risk of waking ourselves here; since monitor_lock_ is not released until we're ready to
    // return, notify can't move the current thread from wait_set_ to wake_set_ until this
    // method is done checking wake_set_.
    Thread* thread = wake_set_;
    wake_set_ = thread->GetWaitNext();
    thread->SetWaitNext(nullptr);
    DCHECK(owner_.load(std::memory_order_relaxed) == nullptr);

    // Check to see if the thread is still waiting.
    {
      // In the case of wait(), we'll be acquiring another thread's GetWaitMutex with
      // self's GetWaitMutex held. This does not risk deadlock, because we only acquire this lock
      // for threads in the wake_set_. A thread can only enter wake_set_ from Notify or NotifyAll,
      // and those hold monitor_lock_. Thus, the threads whose wait mutexes we acquire here must
      // have already been released from wait(), since we have not released monitor_lock_ until
      // after we've chosen our thread to wake, so there is no risk of the following lock ordering
      // leading to deadlock:
      // Thread 1 waits
      // Thread 2 waits
      // Thread 3 moves threads 1 and 2 from wait_set_ to wake_set_
      // Thread 1 enters this block, and attempts to acquire Thread 2's GetWaitMutex to wake it
      // Thread 2 enters this block, and attempts to acquire Thread 1's GetWaitMutex to wake it
      //
      // Since monitor_lock_ is not released until the thread-to-be-woken-up's GetWaitMutex is
      // acquired, two threads cannot attempt to acquire each other's GetWaitMutex while holding
      // their own and cause deadlock.
      MutexLock wait_mu(self, *thread->GetWaitMutex());
      if (thread->GetWaitMonitor() != nullptr) {
        // Release the lock, so that a potentially awakened thread will not
        // immediately contend on it. The lock ordering here is:
        // monitor_lock_, self->GetWaitMutex, thread->GetWaitMutex
        monitor_lock_.Unlock(self);  // Releases contenders.
        thread->GetWaitConditionVariable()->Signal(self);
        return;
      }
    }
  }
  monitor_lock_.Unlock(self);
  DCHECK(!monitor_lock_.IsExclusiveHeld(self));
}

void Monitor::Wait(Thread* self, int64_t ms, int32_t ns,
                   bool interruptShouldThrow, ThreadState why) {
  DCHECK(self != nullptr);
  DCHECK(why == kTimedWaiting || why == kWaiting || why == kSleeping);

  // Make sure that we hold the lock.
  if (owner_.load(std::memory_order_relaxed) != self) {
    ThrowIllegalMonitorStateExceptionF("object not locked by thread before wait()");
    return;
  }

  // We need to turn a zero-length timed wait into a regular wait because
  // Object.wait(0, 0) is defined as Object.wait(0), which is defined as Object.wait().
  if (why == kTimedWaiting && (ms == 0 && ns == 0)) {
    why = kWaiting;
  }

  // Enforce the timeout range.
  if (ms < 0 || ns < 0 || ns > 999999) {
    self->ThrowNewExceptionF("Ljava/lang/IllegalArgumentException;",
                             "timeout arguments out of range: ms=%" PRId64 " ns=%d", ms, ns);
    return;
  }

  CheckLockOwnerRequest(self);

  /*
   * Release our hold - we need to let it go even if we're a few levels
   * deep in a recursive lock, and we need to restore that later.
   */
  unsigned int prev_lock_count = lock_count_;
  lock_count_ = 0;

  AtraceMonitorUnlock();  // For the implict Unlock() just above. This will only end the deepest
                          // nesting, but that is enough for the visualization, and corresponds to
                          // the single Lock() we do afterwards.
  AtraceMonitorLock(self, GetObject(), /* is_wait= */ true);

  bool was_interrupted = false;
  bool timed_out = false;
  // Update monitor state now; it's not safe once we're "suspended".
  owner_.store(nullptr, std::memory_order_relaxed);
  num_waiters_.fetch_add(1, std::memory_order_relaxed);
  {
    // Update thread state. If the GC wakes up, it'll ignore us, knowing
    // that we won't touch any references in this state, and we'll check
    // our suspend mode before we transition out.
    ScopedThreadSuspension sts(self, why);

    // Pseudo-atomically wait on self's wait_cond_ and release the monitor lock.
    MutexLock mu(self, *self->GetWaitMutex());

    /*
     * Add ourselves to the set of threads waiting on this monitor.
     * It's important that we are only added to the wait set after
     * acquiring our GetWaitMutex, so that calls to Notify() that occur after we
     * have released monitor_lock_ will not move us from wait_set_ to wake_set_
     * until we've signalled contenders on this monitor.
     */
    AppendToWaitSet(self);

    // Set wait_monitor_ to the monitor object we will be waiting on. When wait_monitor_ is
    // non-null a notifying or interrupting thread must signal the thread's wait_cond_ to wake it
    // up.
    DCHECK(self->GetWaitMonitor() == nullptr);
    self->SetWaitMonitor(this);

    // Release the monitor lock.
    DCHECK(monitor_lock_.IsExclusiveHeld(self));
    SignalWaiterAndReleaseMonitorLock(self);

    // Handle the case where the thread was interrupted before we called wait().
    if (self->IsInterrupted()) {
      was_interrupted = true;
    } else {
      // Wait for a notification or a timeout to occur.
      if (why == kWaiting) {
        self->GetWaitConditionVariable()->Wait(self);
      } else {
        DCHECK(why == kTimedWaiting || why == kSleeping) << why;
        timed_out = self->GetWaitConditionVariable()->TimedWait(self, ms, ns);
      }
      was_interrupted = self->IsInterrupted();
    }
  }

  {
    // We reset the thread's wait_monitor_ field after transitioning back to runnable so
    // that a thread in a waiting/sleeping state has a non-null wait_monitor_ for debugging
    // and diagnostic purposes. (If you reset this earlier, stack dumps will claim that threads
    // are waiting on "null".)
    MutexLock mu(self, *self->GetWaitMutex());
    DCHECK(self->GetWaitMonitor() != nullptr);
    self->SetWaitMonitor(nullptr);
  }

  // Allocate the interrupted exception not holding the monitor lock since it may cause a GC.
  // If the GC requires acquiring the monitor for enqueuing cleared references, this would
  // cause a deadlock if the monitor is held.
  if (was_interrupted && interruptShouldThrow) {
    /*
     * We were interrupted while waiting, or somebody interrupted an
     * un-interruptible thread earlier and we're bailing out immediately.
     *
     * The doc sayeth: "The interrupted status of the current thread is
     * cleared when this exception is thrown."
     */
    self->SetInterrupted(false);
    self->ThrowNewException("Ljava/lang/InterruptedException;", nullptr);
  }

  AtraceMonitorUnlock();  // End Wait().

  // We just slept, tell the runtime callbacks about this.
  Runtime::Current()->GetRuntimeCallbacks()->MonitorWaitFinished(this, timed_out);

  // Re-acquire the monitor and lock.
  Lock<LockReason::kForWait>(self);
  lock_count_ = prev_lock_count;
  DCHECK(monitor_lock_.IsExclusiveHeld(self));
  self->GetWaitMutex()->AssertNotHeld(self);

  num_waiters_.fetch_sub(1, std::memory_order_relaxed);
  RemoveFromWaitSet(self);
}

void Monitor::Notify(Thread* self) {
  DCHECK(self != nullptr);
  // Make sure that we hold the lock.
  if (owner_.load(std::memory_order_relaxed) != self) {
    ThrowIllegalMonitorStateExceptionF("object not locked by thread before notify()");
    return;
  }
  // Move one thread from waiters to wake set
  Thread* to_move = wait_set_;
  if (to_move != nullptr) {
    wait_set_ = to_move->GetWaitNext();
    to_move->SetWaitNext(wake_set_);
    wake_set_ = to_move;
  }
}

void Monitor::NotifyAll(Thread* self) {
  DCHECK(self != nullptr);
  // Make sure that we hold the lock.
  if (owner_.load(std::memory_order_relaxed) != self) {
    ThrowIllegalMonitorStateExceptionF("object not locked by thread before notifyAll()");
    return;
  }

  // Move all threads from waiters to wake set
  Thread* to_move = wait_set_;
  if (to_move != nullptr) {
    wait_set_ = nullptr;
    Thread* move_to = wake_set_;
    if (move_to == nullptr) {
      wake_set_ = to_move;
      return;
    }
    while (move_to->GetWaitNext() != nullptr) {
      move_to = move_to->GetWaitNext();
    }
    move_to->SetWaitNext(to_move);
  }
}

bool Monitor::Deflate(Thread* self, ObjPtr<mirror::Object> obj) {
  DCHECK(obj != nullptr);
  // Don't need volatile since we only deflate with mutators suspended.
  LockWord lw(obj->GetLockWord(false));
  // If the lock isn't an inflated monitor, then we don't need to deflate anything.
  if (lw.GetState() == LockWord::kFatLocked) {
    Monitor* monitor = lw.FatLockMonitor();
    DCHECK(monitor != nullptr);
    // Can't deflate if we have anybody waiting on the CV or trying to acquire the monitor.
    if (monitor->num_waiters_.load(std::memory_order_relaxed) > 0) {
      return false;
    }
    if (!monitor->monitor_lock_.ExclusiveTryLock(self)) {
      // We cannot deflate a monitor that's currently held. It's unclear whether we should if
      // we could.
      return false;
    }
    DCHECK_EQ(monitor->lock_count_, 0u);
    DCHECK_EQ(monitor->owner_.load(std::memory_order_relaxed), static_cast<Thread*>(nullptr));
    if (monitor->HasHashCode()) {
      LockWord new_lw = LockWord::FromHashCode(monitor->GetHashCode(), lw.GCState());
      // Assume no concurrent read barrier state changes as mutators are suspended.
      obj->SetLockWord(new_lw, false);
      VLOG(monitor) << "Deflated " << obj << " to hash monitor " << monitor->GetHashCode();
    } else {
      // No lock and no hash, just put an empty lock word inside the object.
      LockWord new_lw = LockWord::FromDefault(lw.GCState());
      // Assume no concurrent read barrier state changes as mutators are suspended.
      obj->SetLockWord(new_lw, false);
      VLOG(monitor) << "Deflated" << obj << " to empty lock word";
    }
    monitor->monitor_lock_.ExclusiveUnlock(self);
    DCHECK(!(monitor->monitor_lock_.IsExclusiveHeld(self)));
    // The monitor is deflated, mark the object as null so that we know to delete it during the
    // next GC.
    monitor->obj_ = GcRoot<mirror::Object>(nullptr);
  }
  return true;
}

void Monitor::Inflate(Thread* self, Thread* owner, ObjPtr<mirror::Object> obj, int32_t hash_code) {
  DCHECK(self != nullptr);
  DCHECK(obj != nullptr);
  // Allocate and acquire a new monitor.
  Monitor* m = MonitorPool::CreateMonitor(self, owner, obj, hash_code);
  DCHECK(m != nullptr);
  if (m->Install(self)) {
    if (owner != nullptr) {
      VLOG(monitor) << "monitor: thread" << owner->GetThreadId()
          << " created monitor " << m << " for object " << obj;
    } else {
      VLOG(monitor) << "monitor: Inflate with hashcode " << hash_code
          << " created monitor " << m << " for object " << obj;
    }
    Runtime::Current()->GetMonitorList()->Add(m);
    CHECK_EQ(obj->GetLockWord(true).GetState(), LockWord::kFatLocked);
  } else {
    MonitorPool::ReleaseMonitor(self, m);
  }
}

void Monitor::InflateThinLocked(Thread* self, Handle<mirror::Object> obj, LockWord lock_word,
                                uint32_t hash_code) {
  DCHECK_EQ(lock_word.GetState(), LockWord::kThinLocked);
  uint32_t owner_thread_id = lock_word.ThinLockOwner();
  if (owner_thread_id == self->GetThreadId()) {
    // We own the monitor, we can easily inflate it.
    Inflate(self, self, obj.Get(), hash_code);
  } else {
    ThreadList* thread_list = Runtime::Current()->GetThreadList();
    // Suspend the owner, inflate. First change to blocked and give up mutator_lock_.
    self->SetMonitorEnterObject(obj.Get());
    bool timed_out;
    Thread* owner;
    {
      ScopedThreadSuspension sts(self, kWaitingForLockInflation);
      owner = thread_list->SuspendThreadByThreadId(owner_thread_id,
                                                   SuspendReason::kInternal,
                                                   &timed_out);
    }
    if (owner != nullptr) {
      // We succeeded in suspending the thread, check the lock's status didn't change.
      lock_word = obj->GetLockWord(true);
      if (lock_word.GetState() == LockWord::kThinLocked &&
          lock_word.ThinLockOwner() == owner_thread_id) {
        // Go ahead and inflate the lock.
        Inflate(self, owner, obj.Get(), hash_code);
      }
      bool resumed = thread_list->Resume(owner, SuspendReason::kInternal);
      DCHECK(resumed);
    }
    self->SetMonitorEnterObject(nullptr);
  }
}

// Fool annotalysis into thinking that the lock on obj is acquired.
static ObjPtr<mirror::Object> FakeLock(ObjPtr<mirror::Object> obj)
    EXCLUSIVE_LOCK_FUNCTION(obj.Ptr()) NO_THREAD_SAFETY_ANALYSIS {
  return obj;
}

// Fool annotalysis into thinking that the lock on obj is release.
static ObjPtr<mirror::Object> FakeUnlock(ObjPtr<mirror::Object> obj)
    UNLOCK_FUNCTION(obj.Ptr()) NO_THREAD_SAFETY_ANALYSIS {
  return obj;
}

ObjPtr<mirror::Object> Monitor::MonitorEnter(Thread* self,
                                             ObjPtr<mirror::Object> obj,
                                             bool trylock) {
  DCHECK(self != nullptr);
  DCHECK(obj != nullptr);
  self->AssertThreadSuspensionIsAllowable();
  obj = FakeLock(obj);
  uint32_t thread_id = self->GetThreadId();
  size_t contention_count = 0;
  StackHandleScope<1> hs(self);
  Handle<mirror::Object> h_obj(hs.NewHandle(obj));
#if !ART_USE_FUTEXES
  // In this case we cannot inflate an unowned monitor, so we sometimes defer inflation.
  bool should_inflate = false;
#endif
  while (true) {
    // We initially read the lockword with ordinary Java/relaxed semantics. When stronger
    // semantics are needed, we address it below. Since GetLockWord bottoms out to a relaxed load,
    // we can fix it later, in an infrequently executed case, with a fence.
    LockWord lock_word = h_obj->GetLockWord(false);
    switch (lock_word.GetState()) {
      case LockWord::kUnlocked: {
        // No ordering required for preceding lockword read, since we retest.
        LockWord thin_locked(LockWord::FromThinLockId(thread_id, 0, lock_word.GCState()));
        if (h_obj->CasLockWord(lock_word, thin_locked, CASMode::kWeak, std::memory_order_acquire)) {
#if !ART_USE_FUTEXES
          if (should_inflate) {
            InflateThinLocked(self, h_obj, lock_word, 0);
          }
#endif
          AtraceMonitorLock(self, h_obj.Get(), /* is_wait= */ false);
          return h_obj.Get();  // Success!
        }
        continue;  // Go again.
      }
      case LockWord::kThinLocked: {
        uint32_t owner_thread_id = lock_word.ThinLockOwner();
        if (owner_thread_id == thread_id) {
          // No ordering required for initial lockword read.
          // We own the lock, increase the recursion count.
          uint32_t new_count = lock_word.ThinLockCount() + 1;
          if (LIKELY(new_count <= LockWord::kThinLockMaxCount)) {
            LockWord thin_locked(LockWord::FromThinLockId(thread_id,
                                                          new_count,
                                                          lock_word.GCState()));
            // Only this thread pays attention to the count. Thus there is no need for stronger
            // than relaxed memory ordering.
            if (!kUseReadBarrier) {
              h_obj->SetLockWord(thin_locked, /* as_volatile= */ false);
              AtraceMonitorLock(self, h_obj.Get(), /* is_wait= */ false);
              return h_obj.Get();  // Success!
            } else {
              // Use CAS to preserve the read barrier state.
              if (h_obj->CasLockWord(lock_word,
                                     thin_locked,
                                     CASMode::kWeak,
                                     std::memory_order_relaxed)) {
                AtraceMonitorLock(self, h_obj.Get(), /* is_wait= */ false);
                return h_obj.Get();  // Success!
              }
            }
            continue;  // Go again.
          } else {
            // We'd overflow the recursion count, so inflate the monitor.
            InflateThinLocked(self, h_obj, lock_word, 0);
          }
        } else {
          if (trylock) {
            return nullptr;
          }
          // Contention.
          contention_count++;
          Runtime* runtime = Runtime::Current();
          if (contention_count <= runtime->GetMaxSpinsBeforeThinLockInflation()) {
            // TODO: Consider switching the thread state to kWaitingForLockInflation when we are
            // yielding.  Use sched_yield instead of NanoSleep since NanoSleep can wait much longer
            // than the parameter you pass in. This can cause thread suspension to take excessively
            // long and make long pauses. See b/16307460.
            // TODO: We should literally spin first, without sched_yield. Sched_yield either does
            // nothing (at significant expense), or guarantees that we wait at least microseconds.
            // If the owner is running, I would expect the median lock hold time to be hundreds
            // of nanoseconds or less.
            sched_yield();
          } else {
#if ART_USE_FUTEXES
            contention_count = 0;
            // No ordering required for initial lockword read. Install rereads it anyway.
            InflateThinLocked(self, h_obj, lock_word, 0);
#else
            // Can't inflate from non-owning thread. Keep waiting. Bad for power, but this code
            // isn't used on-device.
            should_inflate = true;
            usleep(10);
#endif
          }
        }
        continue;  // Start from the beginning.
      }
      case LockWord::kFatLocked: {
        // We should have done an acquire read of the lockword initially, to ensure
        // visibility of the monitor data structure. Use an explicit fence instead.
        std::atomic_thread_fence(std::memory_order_acquire);
        Monitor* mon = lock_word.FatLockMonitor();
        if (trylock) {
          return mon->TryLock(self) ? h_obj.Get() : nullptr;
        } else {
          mon->Lock(self);
          DCHECK(mon->monitor_lock_.IsExclusiveHeld(self));
          return h_obj.Get();  // Success!
        }
      }
      case LockWord::kHashCode:
        // Inflate with the existing hashcode.
        // Again no ordering required for initial lockword read, since we don't rely
        // on the visibility of any prior computation.
        Inflate(self, nullptr, h_obj.Get(), lock_word.GetHashCode());
        continue;  // Start from the beginning.
      default: {
        LOG(FATAL) << "Invalid monitor state " << lock_word.GetState();
        UNREACHABLE();
      }
    }
  }
}

bool Monitor::MonitorExit(Thread* self, ObjPtr<mirror::Object> obj) {
  DCHECK(self != nullptr);
  DCHECK(obj != nullptr);
  self->AssertThreadSuspensionIsAllowable();
  obj = FakeUnlock(obj);
  StackHandleScope<1> hs(self);
  Handle<mirror::Object> h_obj(hs.NewHandle(obj));
  while (true) {
    LockWord lock_word = obj->GetLockWord(true);
    switch (lock_word.GetState()) {
      case LockWord::kHashCode:
        // Fall-through.
      case LockWord::kUnlocked:
        FailedUnlock(h_obj.Get(), self->GetThreadId(), 0u, nullptr);
        return false;  // Failure.
      case LockWord::kThinLocked: {
        uint32_t thread_id = self->GetThreadId();
        uint32_t owner_thread_id = lock_word.ThinLockOwner();
        if (owner_thread_id != thread_id) {
          FailedUnlock(h_obj.Get(), thread_id, owner_thread_id, nullptr);
          return false;  // Failure.
        } else {
          // We own the lock, decrease the recursion count.
          LockWord new_lw = LockWord::Default();
          if (lock_word.ThinLockCount() != 0) {
            uint32_t new_count = lock_word.ThinLockCount() - 1;
            new_lw = LockWord::FromThinLockId(thread_id, new_count, lock_word.GCState());
          } else {
            new_lw = LockWord::FromDefault(lock_word.GCState());
          }
          if (!kUseReadBarrier) {
            DCHECK_EQ(new_lw.ReadBarrierState(), 0U);
            // TODO: This really only needs memory_order_release, but we currently have
            // no way to specify that. In fact there seem to be no legitimate uses of SetLockWord
            // with a final argument of true. This slows down x86 and ARMv7, but probably not v8.
            h_obj->SetLockWord(new_lw, true);
            AtraceMonitorUnlock();
            // Success!
            return true;
          } else {
            // Use CAS to preserve the read barrier state.
            if (h_obj->CasLockWord(lock_word, new_lw, CASMode::kWeak, std::memory_order_release)) {
              AtraceMonitorUnlock();
              // Success!
              return true;
            }
          }
          continue;  // Go again.
        }
      }
      case LockWord::kFatLocked: {
        Monitor* mon = lock_word.FatLockMonitor();
        return mon->Unlock(self);
      }
      default: {
        LOG(FATAL) << "Invalid monitor state " << lock_word.GetState();
        UNREACHABLE();
      }
    }
  }
}

void Monitor::Wait(Thread* self,
                   ObjPtr<mirror::Object> obj,
                   int64_t ms,
                   int32_t ns,
                   bool interruptShouldThrow,
                   ThreadState why) {
  DCHECK(self != nullptr);
  DCHECK(obj != nullptr);
  StackHandleScope<1> hs(self);
  Handle<mirror::Object> h_obj(hs.NewHandle(obj));

  Runtime::Current()->GetRuntimeCallbacks()->ObjectWaitStart(h_obj, ms);
  if (UNLIKELY(self->ObserveAsyncException() || self->IsExceptionPending())) {
    // See b/65558434 for information on handling of exceptions here.
    return;
  }

  LockWord lock_word = h_obj->GetLockWord(true);
  while (lock_word.GetState() != LockWord::kFatLocked) {
    switch (lock_word.GetState()) {
      case LockWord::kHashCode:
        // Fall-through.
      case LockWord::kUnlocked:
        ThrowIllegalMonitorStateExceptionF("object not locked by thread before wait()");
        return;  // Failure.
      case LockWord::kThinLocked: {
        uint32_t thread_id = self->GetThreadId();
        uint32_t owner_thread_id = lock_word.ThinLockOwner();
        if (owner_thread_id != thread_id) {
          ThrowIllegalMonitorStateExceptionF("object not locked by thread before wait()");
          return;  // Failure.
        } else {
          // We own the lock, inflate to enqueue ourself on the Monitor. May fail spuriously so
          // re-load.
          Inflate(self, self, h_obj.Get(), 0);
          lock_word = h_obj->GetLockWord(true);
        }
        break;
      }
      case LockWord::kFatLocked:  // Unreachable given the loop condition above. Fall-through.
      default: {
        LOG(FATAL) << "Invalid monitor state " << lock_word.GetState();
        UNREACHABLE();
      }
    }
  }
  Monitor* mon = lock_word.FatLockMonitor();
  mon->Wait(self, ms, ns, interruptShouldThrow, why);
}

void Monitor::DoNotify(Thread* self, ObjPtr<mirror::Object> obj, bool notify_all) {
  DCHECK(self != nullptr);
  DCHECK(obj != nullptr);
  LockWord lock_word = obj->GetLockWord(true);
  switch (lock_word.GetState()) {
    case LockWord::kHashCode:
      // Fall-through.
    case LockWord::kUnlocked:
      ThrowIllegalMonitorStateExceptionF("object not locked by thread before notify()");
      return;  // Failure.
    case LockWord::kThinLocked: {
      uint32_t thread_id = self->GetThreadId();
      uint32_t owner_thread_id = lock_word.ThinLockOwner();
      if (owner_thread_id != thread_id) {
        ThrowIllegalMonitorStateExceptionF("object not locked by thread before notify()");
        return;  // Failure.
      } else {
        // We own the lock but there's no Monitor and therefore no waiters.
        return;  // Success.
      }
    }
    case LockWord::kFatLocked: {
      Monitor* mon = lock_word.FatLockMonitor();
      if (notify_all) {
        mon->NotifyAll(self);
      } else {
        mon->Notify(self);
      }
      return;  // Success.
    }
    default: {
      LOG(FATAL) << "Invalid monitor state " << lock_word.GetState();
      UNREACHABLE();
    }
  }
}

uint32_t Monitor::GetLockOwnerThreadId(ObjPtr<mirror::Object> obj) {
  DCHECK(obj != nullptr);
  LockWord lock_word = obj->GetLockWord(true);
  switch (lock_word.GetState()) {
    case LockWord::kHashCode:
      // Fall-through.
    case LockWord::kUnlocked:
      return ThreadList::kInvalidThreadId;
    case LockWord::kThinLocked:
      return lock_word.ThinLockOwner();
    case LockWord::kFatLocked: {
      Monitor* mon = lock_word.FatLockMonitor();
      return mon->GetOwnerThreadId();
    }
    default: {
      LOG(FATAL) << "Unreachable";
      UNREACHABLE();
    }
  }
}

ThreadState Monitor::FetchState(const Thread* thread,
                                /* out */ ObjPtr<mirror::Object>* monitor_object,
                                /* out */ uint32_t* lock_owner_tid) {
  DCHECK(monitor_object != nullptr);
  DCHECK(lock_owner_tid != nullptr);

  *monitor_object = nullptr;
  *lock_owner_tid = ThreadList::kInvalidThreadId;

  ThreadState state = thread->GetState();

  switch (state) {
    case kWaiting:
    case kTimedWaiting:
    case kSleeping:
    {
      Thread* self = Thread::Current();
      MutexLock mu(self, *thread->GetWaitMutex());
      Monitor* monitor = thread->GetWaitMonitor();
      if (monitor != nullptr) {
        *monitor_object = monitor->GetObject();
      }
    }
    break;

    case kBlocked:
    case kWaitingForLockInflation:
    {
      ObjPtr<mirror::Object> lock_object = thread->GetMonitorEnterObject();
      if (lock_object != nullptr) {
        if (kUseReadBarrier && Thread::Current()->GetIsGcMarking()) {
          // We may call Thread::Dump() in the middle of the CC thread flip and this thread's stack
          // may have not been flipped yet and "pretty_object" may be a from-space (stale) ref, in
          // which case the GetLockOwnerThreadId() call below will crash. So explicitly mark/forward
          // it here.
          lock_object = ReadBarrier::Mark(lock_object.Ptr());
        }
        *monitor_object = lock_object;
        *lock_owner_tid = lock_object->GetLockOwnerThreadId();
      }
    }
    break;

    default:
      break;
  }

  return state;
}

ObjPtr<mirror::Object> Monitor::GetContendedMonitor(Thread* thread) {
  // This is used to implement JDWP's ThreadReference.CurrentContendedMonitor, and has a bizarre
  // definition of contended that includes a monitor a thread is trying to enter...
  ObjPtr<mirror::Object> result = thread->GetMonitorEnterObject();
  if (result == nullptr) {
    // ...but also a monitor that the thread is waiting on.
    MutexLock mu(Thread::Current(), *thread->GetWaitMutex());
    Monitor* monitor = thread->GetWaitMonitor();
    if (monitor != nullptr) {
      result = monitor->GetObject();
    }
  }
  return result;
}

void Monitor::VisitLocks(StackVisitor* stack_visitor,
                         void (*callback)(ObjPtr<mirror::Object>, void*),
                         void* callback_context,
                         bool abort_on_failure) {
  ArtMethod* m = stack_visitor->GetMethod();
  CHECK(m != nullptr);

  // Native methods are an easy special case.
  // TODO: use the JNI implementation's table of explicit MonitorEnter calls and dump those too.
  if (m->IsNative()) {
    if (m->IsSynchronized()) {
      ObjPtr<mirror::Object> jni_this =
          stack_visitor->GetCurrentHandleScope(sizeof(void*))->GetReference(0);
      callback(jni_this, callback_context);
    }
    return;
  }

  // Proxy methods should not be synchronized.
  if (m->IsProxyMethod()) {
    CHECK(!m->IsSynchronized());
    return;
  }

  // Is there any reason to believe there's any synchronization in this method?
  CHECK(m->GetCodeItem() != nullptr) << m->PrettyMethod();
  CodeItemDataAccessor accessor(m->DexInstructionData());
  if (accessor.TriesSize() == 0) {
    return;  // No "tries" implies no synchronization, so no held locks to report.
  }

  // Get the dex pc. If abort_on_failure is false, GetDexPc will not abort in the case it cannot
  // find the dex pc, and instead return kDexNoIndex. Then bail out, as it indicates we have an
  // inconsistent stack anyways.
  uint32_t dex_pc = stack_visitor->GetDexPc(abort_on_failure);
  if (!abort_on_failure && dex_pc == dex::kDexNoIndex) {
    LOG(ERROR) << "Could not find dex_pc for " << m->PrettyMethod();
    return;
  }

  // Ask the verifier for the dex pcs of all the monitor-enter instructions corresponding to
  // the locks held in this stack frame.
  std::vector<verifier::MethodVerifier::DexLockInfo> monitor_enter_dex_pcs;
  verifier::MethodVerifier::FindLocksAtDexPc(m,
                                             dex_pc,
                                             &monitor_enter_dex_pcs,
                                             Runtime::Current()->GetTargetSdkVersion());
  for (verifier::MethodVerifier::DexLockInfo& dex_lock_info : monitor_enter_dex_pcs) {
    // As a debug check, check that dex PC corresponds to a monitor-enter.
    if (kIsDebugBuild) {
      const Instruction& monitor_enter_instruction = accessor.InstructionAt(dex_lock_info.dex_pc);
      CHECK_EQ(monitor_enter_instruction.Opcode(), Instruction::MONITOR_ENTER)
          << "expected monitor-enter @" << dex_lock_info.dex_pc << "; was "
          << reinterpret_cast<const void*>(&monitor_enter_instruction);
    }

    // Iterate through the set of dex registers, as the compiler may not have held all of them
    // live.
    bool success = false;
    for (uint32_t dex_reg : dex_lock_info.dex_registers) {
      uint32_t value;

      // For optimized code we expect the DexRegisterMap to be present - monitor information
      // not be optimized out.
      success = stack_visitor->GetVReg(m, dex_reg, kReferenceVReg, &value);
      if (success) {
        ObjPtr<mirror::Object> o = reinterpret_cast<mirror::Object*>(value);
        callback(o, callback_context);
        break;
      }
    }
    DCHECK(success) << "Failed to find/read reference for monitor-enter at dex pc "
                    << dex_lock_info.dex_pc
                    << " in method "
                    << m->PrettyMethod();
    if (!success) {
      LOG(WARNING) << "Had a lock reported for dex pc " << dex_lock_info.dex_pc
                   << " but was not able to fetch a corresponding object!";
    }
  }
}

bool Monitor::IsValidLockWord(LockWord lock_word) {
  switch (lock_word.GetState()) {
    case LockWord::kUnlocked:
      // Nothing to check.
      return true;
    case LockWord::kThinLocked:
      // Basic sanity check of owner.
      return lock_word.ThinLockOwner() != ThreadList::kInvalidThreadId;
    case LockWord::kFatLocked: {
      // Check the  monitor appears in the monitor list.
      Monitor* mon = lock_word.FatLockMonitor();
      MonitorList* list = Runtime::Current()->GetMonitorList();
      MutexLock mu(Thread::Current(), list->monitor_list_lock_);
      for (Monitor* list_mon : list->list_) {
        if (mon == list_mon) {
          return true;  // Found our monitor.
        }
      }
      return false;  // Fail - unowned monitor in an object.
    }
    case LockWord::kHashCode:
      return true;
    default:
      LOG(FATAL) << "Unreachable";
      UNREACHABLE();
  }
}

bool Monitor::IsLocked() REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetOwner() != nullptr;
}

void Monitor::TranslateLocation(ArtMethod* method,
                                uint32_t dex_pc,
                                const char** source_file,
                                int32_t* line_number) {
  // If method is null, location is unknown
  if (method == nullptr) {
    *source_file = "";
    *line_number = 0;
    return;
  }
  *source_file = method->GetDeclaringClassSourceFile();
  if (*source_file == nullptr) {
    *source_file = "";
  }
  *line_number = method->GetLineNumFromDexPC(dex_pc);
}

uint32_t Monitor::GetOwnerThreadId() {
  // Make sure owner is not deallocated during access.
  MutexLock mu(Thread::Current(), *Locks::thread_list_lock_);
  Thread* owner = GetOwner();
  if (owner != nullptr) {
    return owner->GetThreadId();
  } else {
    return ThreadList::kInvalidThreadId;
  }
}

MonitorList::MonitorList()
    : allow_new_monitors_(true), monitor_list_lock_("MonitorList lock", kMonitorListLock),
      monitor_add_condition_("MonitorList disallow condition", monitor_list_lock_) {
}

MonitorList::~MonitorList() {
  Thread* self = Thread::Current();
  MutexLock mu(self, monitor_list_lock_);
  // Release all monitors to the pool.
  // TODO: Is it an invariant that *all* open monitors are in the list? Then we could
  // clear faster in the pool.
  MonitorPool::ReleaseMonitors(self, &list_);
}

void MonitorList::DisallowNewMonitors() {
  CHECK(!kUseReadBarrier);
  MutexLock mu(Thread::Current(), monitor_list_lock_);
  allow_new_monitors_ = false;
}

void MonitorList::AllowNewMonitors() {
  CHECK(!kUseReadBarrier);
  Thread* self = Thread::Current();
  MutexLock mu(self, monitor_list_lock_);
  allow_new_monitors_ = true;
  monitor_add_condition_.Broadcast(self);
}

void MonitorList::BroadcastForNewMonitors() {
  Thread* self = Thread::Current();
  MutexLock mu(self, monitor_list_lock_);
  monitor_add_condition_.Broadcast(self);
}

void MonitorList::Add(Monitor* m) {
  Thread* self = Thread::Current();
  MutexLock mu(self, monitor_list_lock_);
  // CMS needs this to block for concurrent reference processing because an object allocated during
  // the GC won't be marked and concurrent reference processing would incorrectly clear the JNI weak
  // ref. But CC (kUseReadBarrier == true) doesn't because of the to-space invariant.
  while (!kUseReadBarrier && UNLIKELY(!allow_new_monitors_)) {
    // Check and run the empty checkpoint before blocking so the empty checkpoint will work in the
    // presence of threads blocking for weak ref access.
    self->CheckEmptyCheckpointFromWeakRefAccess(&monitor_list_lock_);
    monitor_add_condition_.WaitHoldingLocks(self);
  }
  list_.push_front(m);
}

void MonitorList::SweepMonitorList(IsMarkedVisitor* visitor) {
  Thread* self = Thread::Current();
  MutexLock mu(self, monitor_list_lock_);
  for (auto it = list_.begin(); it != list_.end(); ) {
    Monitor* m = *it;
    // Disable the read barrier in GetObject() as this is called by GC.
    ObjPtr<mirror::Object> obj = m->GetObject<kWithoutReadBarrier>();
    // The object of a monitor can be null if we have deflated it.
    ObjPtr<mirror::Object> new_obj = obj != nullptr ? visitor->IsMarked(obj.Ptr()) : nullptr;
    if (new_obj == nullptr) {
      VLOG(monitor) << "freeing monitor " << m << " belonging to unmarked object "
                    << obj;
      MonitorPool::ReleaseMonitor(self, m);
      it = list_.erase(it);
    } else {
      m->SetObject(new_obj);
      ++it;
    }
  }
}

size_t MonitorList::Size() {
  Thread* self = Thread::Current();
  MutexLock mu(self, monitor_list_lock_);
  return list_.size();
}

class MonitorDeflateVisitor : public IsMarkedVisitor {
 public:
  MonitorDeflateVisitor() : self_(Thread::Current()), deflate_count_(0) {}

  mirror::Object* IsMarked(mirror::Object* object) override
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (Monitor::Deflate(self_, object)) {
      DCHECK_NE(object->GetLockWord(true).GetState(), LockWord::kFatLocked);
      ++deflate_count_;
      // If we deflated, return null so that the monitor gets removed from the array.
      return nullptr;
    }
    return object;  // Monitor was not deflated.
  }

  Thread* const self_;
  size_t deflate_count_;
};

size_t MonitorList::DeflateMonitors() {
  MonitorDeflateVisitor visitor;
  Locks::mutator_lock_->AssertExclusiveHeld(visitor.self_);
  SweepMonitorList(&visitor);
  return visitor.deflate_count_;
}

MonitorInfo::MonitorInfo(ObjPtr<mirror::Object> obj) : owner_(nullptr), entry_count_(0) {
  DCHECK(obj != nullptr);
  LockWord lock_word = obj->GetLockWord(true);
  switch (lock_word.GetState()) {
    case LockWord::kUnlocked:
      // Fall-through.
    case LockWord::kForwardingAddress:
      // Fall-through.
    case LockWord::kHashCode:
      break;
    case LockWord::kThinLocked:
      owner_ = Runtime::Current()->GetThreadList()->FindThreadByThreadId(lock_word.ThinLockOwner());
      DCHECK(owner_ != nullptr) << "Thin-locked without owner!";
      entry_count_ = 1 + lock_word.ThinLockCount();
      // Thin locks have no waiters.
      break;
    case LockWord::kFatLocked: {
      Monitor* mon = lock_word.FatLockMonitor();
      owner_ = mon->owner_.load(std::memory_order_relaxed);
      // Here it is okay for the owner to be null since we don't reset the LockWord back to
      // kUnlocked until we get a GC. In cases where this hasn't happened yet we will have a fat
      // lock without an owner.
      // Neither owner_ nor entry_count_ is touched by threads in "suspended" state, so
      // we must see consistent values.
      if (owner_ != nullptr) {
        entry_count_ = 1 + mon->lock_count_;
      } else {
        DCHECK_EQ(mon->lock_count_, 0u) << "Monitor is fat-locked without any owner!";
      }
      for (Thread* waiter = mon->wait_set_; waiter != nullptr; waiter = waiter->GetWaitNext()) {
        waiters_.push_back(waiter);
      }
      break;
    }
  }
}

}  // namespace art
