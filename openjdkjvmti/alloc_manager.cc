
/* Copyright (C) 2019 The Android Open Source Project
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This file implements interfaces from the file jvmti.h. This implementation
 * is licensed under the same terms as the file jvmti.h.  The
 * copyright and license information for the file jvmti.h follows.
 *
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "alloc_manager.h"

#include <atomic>
#include <sstream>

#include "base/logging.h"
#include "gc/allocation_listener.h"
#include "gc/heap.h"
#include "handle.h"
#include "mirror/class-inl.h"
#include "runtime.h"
#include "runtime_globals.h"
#include "scoped_thread_state_change-inl.h"
#include "scoped_thread_state_change.h"
#include "thread-current-inl.h"
#include "thread_list.h"
#include "thread_pool.h"

namespace openjdkjvmti {

template<typename T>
void AllocationManager::PauseForAllocation(art::Thread* self, T msg) {
  // The suspension can pause us for arbitrary times. We need to do it to sleep unfortunately. So we
  // do test, suspend, test again, sleep, repeat.
  std::string cause;
  const bool is_logging = VLOG_IS_ON(plugin);
  while (true) {
    // We always return when there is no pause and we are runnable.
    art::Thread* pausing_thread = allocations_paused_thread_.load(std::memory_order_seq_cst);
    if (LIKELY(pausing_thread == nullptr || pausing_thread == self)) {
      return;
    }
    if (UNLIKELY(is_logging && cause.empty())) {
      cause = msg();
    }
    art::ScopedThreadSuspension sts(self, art::ThreadState::kSuspended);
    art::MutexLock mu(self, alloc_listener_mutex_);
    pausing_thread = allocations_paused_thread_.load(std::memory_order_seq_cst);
    CHECK_NE(pausing_thread, self) << "We should always be setting pausing_thread = self!"
                                   << " How did this happen? " << *self;
    if (pausing_thread != nullptr) {
      VLOG(plugin) << "Suspending " << *self << " due to " << cause << ". Allocation pause "
                   << "initiated by " << *pausing_thread;
      alloc_pause_cv_.Wait(self);
    }
  }
}

extern AllocationManager* gAllocManager;
AllocationManager* AllocationManager::Get() {
  return gAllocManager;
}

void JvmtiAllocationListener::ObjectAllocated(art::Thread* self,
                                              art::ObjPtr<art::mirror::Object>* obj,
                                              size_t cnt) {
  auto cb = manager_->callback_;
  if (cb != nullptr && manager_->callback_enabled_.load(std::memory_order_seq_cst)) {
    cb->ObjectAllocated(self, obj, cnt);
  }
}

bool JvmtiAllocationListener::HasPreAlloc() const {
  return manager_->allocations_paused_ever_.load(std::memory_order_seq_cst);
}

void JvmtiAllocationListener::PreObjectAllocated(art::Thread* self,
                                                 art::MutableHandle<art::mirror::Class> type,
                                                 size_t* byte_count) {
  manager_->PauseForAllocation(self, [&]() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    std::ostringstream oss;
    oss << "allocating " << *byte_count << " bytes of type " << type->PrettyClass();
    return oss.str();
  });
  if (!type->IsVariableSize()) {
    *byte_count =
        std::max(art::RoundUp(static_cast<size_t>(type->GetObjectSize()), art::kObjectAlignment),
                 *byte_count);
  }
}

AllocationManager::AllocationManager()
    : alloc_listener_(nullptr),
      alloc_listener_mutex_("JVMTI Alloc listener",
                            art::LockLevel::kPostUserCodeSuspensionTopLevelLock),
      alloc_pause_cv_("JVMTI Allocation Pause Condvar", alloc_listener_mutex_) {
  alloc_listener_.reset(new JvmtiAllocationListener(this));
}

void AllocationManager::DisableAllocationCallback(art::Thread* self) {
  callback_enabled_.store(false);
  DecrListenerInstall(self);
}

void AllocationManager::EnableAllocationCallback(art::Thread* self) {
  IncrListenerInstall(self);
  callback_enabled_.store(true);
}

void AllocationManager::SetAllocListener(AllocationCallback* callback) {
  CHECK(callback_ == nullptr) << "Already setup!";
  callback_ = callback;
  alloc_listener_.reset(new JvmtiAllocationListener(this));
}

void AllocationManager::RemoveAllocListener() {
  callback_enabled_.store(false, std::memory_order_seq_cst);
  callback_ = nullptr;
}

void AllocationManager::DecrListenerInstall(art::Thread* self) {
  art::ScopedThreadSuspension sts(self, art::ThreadState::kSuspended);
  art::MutexLock mu(self, alloc_listener_mutex_);
  // We don't need any particular memory-order here since we're under the lock, they aren't
  // changing.
  if (--listener_refcount_ == 0) {
    art::Runtime::Current()->GetHeap()->RemoveAllocationListener();
  }
}

void AllocationManager::IncrListenerInstall(art::Thread* self) {
  art::ScopedThreadSuspension sts(self, art::ThreadState::kSuspended);
  art::MutexLock mu(self, alloc_listener_mutex_);
  // We don't need any particular memory-order here since we're under the lock, they aren't
  // changing.
  if (listener_refcount_++ == 0) {
    art::Runtime::Current()->GetHeap()->SetAllocationListener(alloc_listener_.get());
  }
}

void AllocationManager::PauseAllocations(art::Thread* self) {
  art::Thread* null_thr = nullptr;
  // Unfortunately once we've paused allocations once we have to leave the listener and
  // PreObjectAlloc event enabled forever. This is to avoid an instance of the ABA problem. We need
  // to make sure that every thread gets a chance to see the PreObjectAlloc event at least once or
  // else it could miss the fact that the object its allocating had its size changed.
  //
  // Consider the following 2 threads. T1 is allocating an object of class K. It is suspended (by
  // user code) somewhere in the AllocObjectWithAllocator function, perhaps while doing a GC to
  // attempt to clear space. With that thread suspended on thread T2 we decide to structurally
  // redefine 'K', changing its size. To do this we insert this PreObjectAlloc event to check and
  // update the size of the class being allocated. This is done successfully. Now imagine if T2
  // removed the listener event then T1 subsequently resumes. T1 would see there is no
  // PreObjectAlloc event and so allocate using the old object size. This leads to it not allocating
  // enough. To prevent this we simply force every allocation after our first pause to go through
  // the PreObjectAlloc event.
  //
  // TODO Technically we could do better than this. We just need to be able to require that all
  // threads within allocation functions go through the PreObjectAlloc at least once after we turn
  // it on. This is easier said than done though since we don't want to place a marker on threads
  // (allocation is just too common) and we can't just have every thread go through the event since
  // there are some threads that never or almost never allocate. We would also need to ensure that
  // this thread doesn't pause waiting for all threads to pass the barrier since the other threads
  // might be suspended. We could accomplish this by storing callbacks on each thread that would do
  // the work. Honestly though this is a debug feature and it doesn't slow things down very much so
  // simply leaving it on forever is simpler and safer.
  bool expected = false;
  if (allocations_paused_ever_.compare_exchange_strong(expected, true, std::memory_order_seq_cst)) {
    IncrListenerInstall(self);
  }
  do {
    PauseForAllocation(self, []() { return "request to pause allocations on other threads"; });
  } while (!allocations_paused_thread_.compare_exchange_strong(
      null_thr, self, std::memory_order_seq_cst));
  // Make sure everything else can see this and isn't in the middle of final allocation.
  // Force every thread to either be suspended or pass through a barrier.
  art::ScopedThreadSuspension sts(self, art::ThreadState::kSuspended);
  art::Barrier barrier(0);
  art::FunctionClosure fc([&](art::Thread* thr ATTRIBUTE_UNUSED) {
    barrier.Pass(art::Thread::Current());
  });
  size_t requested = art::Runtime::Current()->GetThreadList()->RunCheckpoint(&fc);
  barrier.Increment(self, requested);
}

void AllocationManager::ResumeAllocations(art::Thread* self) {
  CHECK_EQ(allocations_paused_thread_.load(), self) << "not paused! ";
  // See above for why we don't decr the install count.
  CHECK(allocations_paused_ever_.load(std::memory_order_seq_cst));
  art::ScopedThreadSuspension sts(self, art::ThreadState::kSuspended);
  art::MutexLock mu(self, alloc_listener_mutex_);
  allocations_paused_thread_.store(nullptr, std::memory_order_seq_cst);
  alloc_pause_cv_.Broadcast(self);
}

}  // namespace openjdkjvmti
