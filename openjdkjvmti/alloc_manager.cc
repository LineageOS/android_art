
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
  return manager_->allocations_paused_thread_.load(std::memory_order_seq_cst) != nullptr;
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
    *byte_count = type->GetObjectSize();
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
  IncrListenerInstall(self);
  do {
    PauseForAllocation(self, []() { return "request to pause allocations on other threads"; });
  } while (allocations_paused_thread_.compare_exchange_strong(
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
  DecrListenerInstall(self);
  art::ScopedThreadSuspension sts(self, art::ThreadState::kSuspended);
  art::MutexLock mu(self, alloc_listener_mutex_);
  allocations_paused_thread_.store(nullptr, std::memory_order_seq_cst);
  alloc_pause_cv_.Broadcast(self);
}

}  // namespace openjdkjvmti
