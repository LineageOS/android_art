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

#ifndef ART_RUNTIME_MONITOR_H_
#define ART_RUNTIME_MONITOR_H_

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#include <atomic>
#include <iosfwd>
#include <list>
#include <vector>

#include "base/allocator.h"
#include "base/atomic.h"
#include "base/mutex.h"
#include "gc_root.h"
#include "lock_word.h"
#include "obj_ptr.h"
#include "read_barrier_option.h"
#include "runtime_callbacks.h"
#include "thread_state.h"

namespace art {

class ArtMethod;
class IsMarkedVisitor;
class LockWord;
template<class T> class Handle;
class StackVisitor;
class Thread;
typedef uint32_t MonitorId;

namespace mirror {
class Object;
}  // namespace mirror

enum class LockReason {
  kForWait,
  kForLock,
};

class Monitor {
 public:
  // The default number of spins that are done before thread suspension is used to forcibly inflate
  // a lock word. See Runtime::max_spins_before_thin_lock_inflation_.
  constexpr static size_t kDefaultMaxSpinsBeforeThinLockInflation = 50;

  static constexpr int kDefaultMonitorTimeoutMs = 500;

  static constexpr int kMonitorTimeoutMinMs = 200;

  static constexpr int kMonitorTimeoutMaxMs = 1000;  // 1 second

  ~Monitor();

  static void Init(uint32_t lock_profiling_threshold, uint32_t stack_dump_lock_profiling_threshold);

  // Return the thread id of the lock owner or 0 when there is no owner.
  static uint32_t GetLockOwnerThreadId(ObjPtr<mirror::Object> obj)
      NO_THREAD_SAFETY_ANALYSIS;  // TODO: Reading lock owner without holding lock is racy.

  // NO_THREAD_SAFETY_ANALYSIS for mon->Lock.
  static ObjPtr<mirror::Object> MonitorEnter(Thread* thread,
                                             ObjPtr<mirror::Object> obj,
                                             bool trylock)
      EXCLUSIVE_LOCK_FUNCTION(obj.Ptr())
      NO_THREAD_SAFETY_ANALYSIS
      REQUIRES(!Roles::uninterruptible_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // NO_THREAD_SAFETY_ANALYSIS for mon->Unlock.
  static bool MonitorExit(Thread* thread, ObjPtr<mirror::Object> obj)
      NO_THREAD_SAFETY_ANALYSIS
      REQUIRES(!Roles::uninterruptible_)
      REQUIRES_SHARED(Locks::mutator_lock_)
      UNLOCK_FUNCTION(obj.Ptr());

  static void Notify(Thread* self, ObjPtr<mirror::Object> obj)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DoNotify(self, obj, false);
  }
  static void NotifyAll(Thread* self, ObjPtr<mirror::Object> obj)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DoNotify(self, obj, true);
  }

  // Object.wait().  Also called for class init.
  // NO_THREAD_SAFETY_ANALYSIS for mon->Wait.
  static void Wait(Thread* self,
                   ObjPtr<mirror::Object> obj,
                   int64_t ms,
                   int32_t ns,
                   bool interruptShouldThrow, ThreadState why)
      REQUIRES_SHARED(Locks::mutator_lock_) NO_THREAD_SAFETY_ANALYSIS;

  static ThreadState FetchState(const Thread* thread,
                                /* out */ ObjPtr<mirror::Object>* monitor_object,
                                /* out */ uint32_t* lock_owner_tid)
      REQUIRES(!Locks::thread_suspend_count_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Used to implement JDWP's ThreadReference.CurrentContendedMonitor.
  static ObjPtr<mirror::Object> GetContendedMonitor(Thread* thread)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Calls 'callback' once for each lock held in the single stack frame represented by
  // the current state of 'stack_visitor'.
  // The abort_on_failure flag allows to not die when the state of the runtime is unorderly. This
  // is necessary when we have already aborted but want to dump the stack as much as we can.
  static void VisitLocks(StackVisitor* stack_visitor,
                         void (*callback)(ObjPtr<mirror::Object>, void*),
                         void* callback_context,
                         bool abort_on_failure = true)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static bool IsValidLockWord(LockWord lock_word);

  template<ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ObjPtr<mirror::Object> GetObject() REQUIRES_SHARED(Locks::mutator_lock_);

  void SetObject(ObjPtr<mirror::Object> object);

  // Provides no memory ordering guarantees.
  Thread* GetOwner() const {
    return owner_.load(std::memory_order_relaxed);
  }

  int32_t GetHashCode();

  // Is the monitor currently locked? Debug only, provides no memory ordering guarantees.
  bool IsLocked() REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!monitor_lock_);

  bool HasHashCode() const {
    return hash_code_.load(std::memory_order_relaxed) != 0;
  }

  MonitorId GetMonitorId() const {
    return monitor_id_;
  }

  // Inflate the lock on obj. May fail to inflate for spurious reasons, always re-check.
  static void InflateThinLocked(Thread* self, Handle<mirror::Object> obj, LockWord lock_word,
                                uint32_t hash_code) REQUIRES_SHARED(Locks::mutator_lock_);

  // Not exclusive because ImageWriter calls this during a Heap::VisitObjects() that
  // does not allow a thread suspension in the middle. TODO: maybe make this exclusive.
  // NO_THREAD_SAFETY_ANALYSIS for monitor->monitor_lock_.
  static bool Deflate(Thread* self, ObjPtr<mirror::Object> obj)
      REQUIRES_SHARED(Locks::mutator_lock_) NO_THREAD_SAFETY_ANALYSIS;

#ifndef __LP64__
  void* operator new(size_t size) {
    // Align Monitor* as per the monitor ID field size in the lock word.
    void* result;
    int error = posix_memalign(&result, LockWord::kMonitorIdAlignment, size);
    CHECK_EQ(error, 0) << strerror(error);
    return result;
  }

  void operator delete(void* ptr) {
    free(ptr);
  }
#endif

 private:
  Monitor(Thread* self, Thread* owner, ObjPtr<mirror::Object> obj, int32_t hash_code)
      REQUIRES_SHARED(Locks::mutator_lock_);
  Monitor(Thread* self, Thread* owner, ObjPtr<mirror::Object> obj, int32_t hash_code, MonitorId id)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Install the monitor into its object, may fail if another thread installs a different monitor
  // first. Monitor remains in the same logical state as before, i.e. held the same # of times.
  bool Install(Thread* self)
      REQUIRES(!monitor_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Links a thread into a monitor's wait set.  The monitor lock must be held by the caller of this
  // routine.
  void AppendToWaitSet(Thread* thread) REQUIRES(monitor_lock_);

  // Unlinks a thread from a monitor's wait set.  The monitor lock must be held by the caller of
  // this routine.
  void RemoveFromWaitSet(Thread* thread) REQUIRES(monitor_lock_);

  // Release the monitor lock and signal a waiting thread that has been notified and now needs the
  // lock. Assumes the monitor lock is held exactly once, and the owner_ field has been reset to
  // null. Caller may be suspended (Wait) or runnable (MonitorExit).
  void SignalWaiterAndReleaseMonitorLock(Thread* self) RELEASE(monitor_lock_);

  // Changes the shape of a monitor from thin to fat, preserving the internal lock state. The
  // calling thread must own the lock or the owner must be suspended. There's a race with other
  // threads inflating the lock, installing hash codes and spurious failures. The caller should
  // re-read the lock word following the call.
  static void Inflate(Thread* self, Thread* owner, ObjPtr<mirror::Object> obj, int32_t hash_code)
      REQUIRES_SHARED(Locks::mutator_lock_)
      NO_THREAD_SAFETY_ANALYSIS;  // For m->Install(self)

  void LogContentionEvent(Thread* self,
                          uint32_t wait_ms,
                          uint32_t sample_percent,
                          ArtMethod* owner_method,
                          uint32_t owner_dex_pc)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static void FailedUnlock(ObjPtr<mirror::Object> obj,
                           uint32_t expected_owner_thread_id,
                           uint32_t found_owner_thread_id,
                           Monitor* mon)
      REQUIRES(!Locks::thread_list_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Try to lock without blocking, returns true if we acquired the lock.
  // If spin is true, then we spin for a short period before failing.
  bool TryLock(Thread* self, bool spin = false)
      TRY_ACQUIRE(true, monitor_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  template<LockReason reason = LockReason::kForLock>
  void Lock(Thread* self)
      ACQUIRE(monitor_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool Unlock(Thread* thread)
      RELEASE(monitor_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static void DoNotify(Thread* self, ObjPtr<mirror::Object> obj, bool notify_all)
      REQUIRES_SHARED(Locks::mutator_lock_) NO_THREAD_SAFETY_ANALYSIS;  // For mon->Notify.

  void Notify(Thread* self)
      REQUIRES(monitor_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void NotifyAll(Thread* self)
      REQUIRES(monitor_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static std::string PrettyContentionInfo(const std::string& owner_name,
                                          pid_t owner_tid,
                                          ArtMethod* owners_method,
                                          uint32_t owners_dex_pc,
                                          size_t num_waiters)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Wait on a monitor until timeout, interrupt, or notification.  Used for Object.wait() and
  // (somewhat indirectly) Thread.sleep() and Thread.join().
  //
  // If another thread calls Thread.interrupt(), we throw InterruptedException and return
  // immediately if one of the following are true:
  //  - blocked in wait(), wait(long), or wait(long, int) methods of Object
  //  - blocked in join(), join(long), or join(long, int) methods of Thread
  //  - blocked in sleep(long), or sleep(long, int) methods of Thread
  // Otherwise, we set the "interrupted" flag.
  //
  // Checks to make sure that "ns" is in the range 0-999999 (i.e. fractions of a millisecond) and
  // throws the appropriate exception if it isn't.
  //
  // The spec allows "spurious wakeups", and recommends that all code using Object.wait() do so in
  // a loop.  This appears to derive from concerns about pthread_cond_wait() on multiprocessor
  // systems.  Some commentary on the web casts doubt on whether these can/should occur.
  //
  // Since we're allowed to wake up "early", we clamp extremely long durations to return at the end
  // of the 32-bit time epoch.
  void Wait(Thread* self, int64_t msec, int32_t nsec, bool interruptShouldThrow, ThreadState why)
      REQUIRES(monitor_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Translates the provided method and pc into its declaring class' source file and line number.
  static void TranslateLocation(ArtMethod* method, uint32_t pc,
                                const char** source_file,
                                int32_t* line_number)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Provides no memory ordering guarantees.
  uint32_t GetOwnerThreadId() REQUIRES(!monitor_lock_);

  // Set locking_method_ and locking_dex_pc_ corresponding to owner's current stack.
  // owner is either self or suspended.
  void SetLockingMethod(Thread* owner) REQUIRES(monitor_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // The same, but without checking for a proxy method. Currently requires owner == self.
  void SetLockingMethodNoProxy(Thread* owner) REQUIRES(monitor_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Support for systrace output of monitor operations.
  ALWAYS_INLINE static void AtraceMonitorLock(Thread* self,
                                              ObjPtr<mirror::Object> obj,
                                              bool is_wait)
      REQUIRES_SHARED(Locks::mutator_lock_);
  static void AtraceMonitorLockImpl(Thread* self,
                                    ObjPtr<mirror::Object> obj,
                                    bool is_wait)
      REQUIRES_SHARED(Locks::mutator_lock_);
  ALWAYS_INLINE static void AtraceMonitorUnlock();

  static uint32_t lock_profiling_threshold_;
  static uint32_t stack_dump_lock_profiling_threshold_;
  static bool capture_method_eagerly_;

  // Holding the monitor N times is represented by holding monitor_lock_ N times.
  Mutex monitor_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;

  // Pretend to unlock monitor lock.
  void FakeUnlockMonitorLock() RELEASE(monitor_lock_) NO_THREAD_SAFETY_ANALYSIS {}

  // Number of threads either waiting on the condition or waiting on a contended
  // monitor acquisition. Prevents deflation.
  std::atomic<size_t> num_waiters_;

  // Which thread currently owns the lock? monitor_lock_ only keeps the tid.
  // Only set while holding monitor_lock_. Non-locking readers only use it to
  // compare to self or for debugging.
  std::atomic<Thread*> owner_;

  // Owner's recursive lock depth. Owner_ non-null, and lock_count_ == 0 ==> held once.
  unsigned int lock_count_ GUARDED_BY(monitor_lock_);

  // Owner's recursive lock depth is given by monitor_lock_.GetDepth().

  // What object are we part of. This is a weak root. Do not access
  // this directly, use GetObject() to read it so it will be guarded
  // by a read barrier.
  GcRoot<mirror::Object> obj_;

  // Threads currently waiting on this monitor.
  Thread* wait_set_ GUARDED_BY(monitor_lock_);

  // Threads that were waiting on this monitor, but are now contending on it.
  Thread* wake_set_ GUARDED_BY(monitor_lock_);

  // Stored object hash code, generated lazily by GetHashCode.
  AtomicInteger hash_code_;

  // Data structure used to remember the method and dex pc of a recent holder of the
  // lock. Used for tracing and contention reporting. Setting these is expensive, since it
  // involves a partial stack walk. We set them only as follows, to minimize the cost:
  // - If tracing is enabled, they are needed immediately when we first notice contention, so we
  //   set them unconditionally when a monitor is acquired.
  // - If contention reporting is enabled, we use the lock_owner_request_ field to have the
  //   contending thread request them. The current owner then sets them when releasing the monitor,
  //   making them available when the contending thread acquires the monitor.
  // - If tracing and contention reporting are enabled, we do both. This usually prevents us from
  //   switching between reporting the end and beginning of critical sections for contention logging
  //   when tracing is enabled.  We expect that tracing overhead is normally much higher than for
  //   contention logging, so the added cost should be small. It also minimizes glitches when
  //   enabling and disabling traces.
  // We're tolerant of missing information. E.g. when tracing is initially turned on, we may
  // not have the lock holder information if the holder acquired the lock with tracing off.
  //
  // We make this data unconditionally atomic; for contention logging all accesses are in fact
  // protected by the monitor, but for tracing, reads are not. Writes are always
  // protected by the monitor.
  //
  // The fields are always accessed without memory ordering. We store a checksum, and reread if
  // the checksum doesn't correspond to the values.  This results in values that are correct with
  // very high probability, but not certainty.
  //
  // If we need lock_owner information for a certain thread for contenion logging, we store its
  // tid in lock_owner_request_. To satisfy the request, we store lock_owner_tid_,
  // lock_owner_method_, and lock_owner_dex_pc_ and the corresponding checksum while holding the
  // monitor.
  //
  // At all times, either lock_owner_ is zero, the checksum is valid, or a thread is actively
  // in the process of establishing one of those states. Only one thread at a time can be actively
  // establishing such a state, since writes are protected by the monitor.
  std::atomic<Thread*> lock_owner_;  // *lock_owner_ may no longer exist!
  std::atomic<ArtMethod*> lock_owner_method_;
  std::atomic<uint32_t> lock_owner_dex_pc_;
  std::atomic<uintptr_t> lock_owner_sum_;

  // Request lock owner save method and dex_pc. Written asynchronously.
  std::atomic<Thread*> lock_owner_request_;

  // Compute method, dex pc, and tid "checksum".
  uintptr_t LockOwnerInfoChecksum(ArtMethod* m, uint32_t dex_pc, Thread* t);

  // Set owning method, dex pc, and tid. owner_ field is set and points to current thread.
  void SetLockOwnerInfo(ArtMethod* method, uint32_t dex_pc, Thread* t)
      REQUIRES(monitor_lock_);

  // Get owning method and dex pc for the given thread, if available.
  void GetLockOwnerInfo(/*out*/ArtMethod** method, /*out*/uint32_t* dex_pc, Thread* t);

  // Do the same, while holding the monitor. There are no concurrent updates.
  void GetLockOwnerInfoLocked(/*out*/ArtMethod** method, /*out*/uint32_t* dex_pc,
                              uint32_t thread_id)
      REQUIRES(monitor_lock_);

  // We never clear lock_owner method and dex pc. Since it often reflects
  // ownership when we last detected contention, it may be inconsistent with owner_
  // and not 100% reliable. For lock contention monitoring, in the absence of tracing,
  // there is a small risk that the current owner may finish before noticing the request,
  // or the information will be overwritten by another intervening request and monitor
  // release, so it's also not 100% reliable. But if we report information at all, it
  // should generally (modulo accidental checksum matches) pertain to to an acquisition of the
  // right monitor by the right thread, so it's extremely unlikely to be seriously misleading.
  // Since we track threads by a pointer to the Thread structure, there is a small chance we may
  // confuse threads allocated at the same exact address, if a contending thread dies before
  // we inquire about it.

  // Check for and act on a pending lock_owner_request_
  void CheckLockOwnerRequest(Thread* self)
      REQUIRES(monitor_lock_) REQUIRES_SHARED(Locks::mutator_lock_);

  void MaybeEnableTimeout() REQUIRES(Locks::mutator_lock_);

  // The denser encoded version of this monitor as stored in the lock word.
  MonitorId monitor_id_;

#ifdef __LP64__
  // Free list for monitor pool.
  Monitor* next_free_ GUARDED_BY(Locks::allocated_monitor_ids_lock_);
#endif

  friend class MonitorInfo;
  friend class MonitorList;
  friend class MonitorPool;
  friend class mirror::Object;
  DISALLOW_COPY_AND_ASSIGN(Monitor);
};

class MonitorList {
 public:
  MonitorList();
  ~MonitorList();

  void Add(Monitor* m) REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!monitor_list_lock_);

  void SweepMonitorList(IsMarkedVisitor* visitor)
      REQUIRES(!monitor_list_lock_) REQUIRES_SHARED(Locks::mutator_lock_);
  void DisallowNewMonitors() REQUIRES(!monitor_list_lock_);
  void AllowNewMonitors() REQUIRES(!monitor_list_lock_);
  void BroadcastForNewMonitors() REQUIRES(!monitor_list_lock_);
  // Returns how many monitors were deflated.
  size_t DeflateMonitors() REQUIRES(!monitor_list_lock_) REQUIRES(Locks::mutator_lock_);
  size_t Size() REQUIRES(!monitor_list_lock_);

  typedef std::list<Monitor*, TrackingAllocator<Monitor*, kAllocatorTagMonitorList>> Monitors;

 private:
  // During sweeping we may free an object and on a separate thread have an object created using
  // the newly freed memory. That object may then have its lock-word inflated and a monitor created.
  // If we allow new monitor registration during sweeping this monitor may be incorrectly freed as
  // the object wasn't marked when sweeping began.
  bool allow_new_monitors_ GUARDED_BY(monitor_list_lock_);
  Mutex monitor_list_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  ConditionVariable monitor_add_condition_ GUARDED_BY(monitor_list_lock_);
  Monitors list_ GUARDED_BY(monitor_list_lock_);

  friend class Monitor;
  DISALLOW_COPY_AND_ASSIGN(MonitorList);
};

// Collects information about the current state of an object's monitor.
// This is very unsafe, and must only be called when all threads are suspended.
// For use only by the JDWP implementation.
class MonitorInfo {
 public:
  MonitorInfo() : owner_(nullptr), entry_count_(0) {}
  MonitorInfo(const MonitorInfo&) = default;
  MonitorInfo& operator=(const MonitorInfo&) = default;
  explicit MonitorInfo(ObjPtr<mirror::Object> o) REQUIRES(Locks::mutator_lock_);

  Thread* owner_;
  size_t entry_count_;
  std::vector<Thread*> waiters_;
};

}  // namespace art

#endif  // ART_RUNTIME_MONITOR_H_
