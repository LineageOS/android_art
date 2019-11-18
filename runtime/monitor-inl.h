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

#ifndef ART_RUNTIME_MONITOR_INL_H_
#define ART_RUNTIME_MONITOR_INL_H_

#include "monitor.h"

#include "gc_root-inl.h"
#include "obj_ptr-inl.h"

namespace art {

template<ReadBarrierOption kReadBarrierOption>
inline ObjPtr<mirror::Object> Monitor::GetObject() REQUIRES_SHARED(Locks::mutator_lock_) {
  return obj_.Read<kReadBarrierOption>();
}

// Lock monitor lock n more times.
void Monitor::LockMonitorLock(Thread* thread, int n) NO_THREAD_SAFETY_ANALYSIS {
  // Since this only adjusts the number of times a lock is held, we pretend it
  // doesn't acquire any locks.
  // The expected value of n is zero; the obvious inefficiency doesn't matter.
  for (int i = 0; i < n; ++i) {
    monitor_lock_.Lock(thread);
  }
}

// Unlock monitor n times, but not completely.
void Monitor::UnlockMonitorLock(Thread* thread, int n) NO_THREAD_SAFETY_ANALYSIS {
  // We lie about locking behavior as in UnlockMonitorLock().
  for (int i = 0; i < n; ++i) {
    monitor_lock_.Unlock(thread);
  }
}

// Check for request to set lock owner info.
void Monitor::CheckLockOwnerRequest(Thread* self) {
  uint32_t request_tid = lock_owner_request_.load(std::memory_order_relaxed);
  if (request_tid != 0 && request_tid == self->GetThreadId()) {
    SetLockingMethod(self);
    // Only do this the first time after a request.
    lock_owner_request_.store(0, std::memory_order_relaxed);
  }
}

uintptr_t Monitor::LockOwnerInfoChecksum(ArtMethod* m, uint32_t dex_pc, uint32_t thread_id) {
  uintptr_t dpc_and_thread_id = static_cast<uintptr_t>((dex_pc << 8) ^ thread_id);
  return reinterpret_cast<uintptr_t>(m) ^ dpc_and_thread_id
      ^ (dpc_and_thread_id << (/* ptr_size / 2 */ (sizeof m) << 2));
}

void Monitor::SetLockOwnerInfo(ArtMethod* method, uint32_t dex_pc, uint32_t thread_id) {
  lock_owner_method_.store(method, std::memory_order_relaxed);
  lock_owner_dex_pc_.store(dex_pc, std::memory_order_relaxed);
  lock_owner_thread_id_.store(thread_id, std::memory_order_relaxed);
  uintptr_t sum = LockOwnerInfoChecksum(method, dex_pc, thread_id);
  lock_owner_sum_.store(sum, std::memory_order_relaxed);
}

void Monitor::GetLockOwnerInfo(/*out*/ArtMethod** method, /*out*/uint32_t* dex_pc,
                               uint32_t thread_id) {
  ArtMethod* owners_method;
  uint32_t owners_dex_pc;
  uint32_t owners_thread_id;
  uintptr_t owners_sum;
  DCHECK_NE(thread_id, 0u);
  do {
    owners_thread_id = lock_owner_thread_id_.load(std::memory_order_relaxed);
    if (owners_thread_id == 0u) {
      break;
    }
    owners_method = lock_owner_method_.load(std::memory_order_relaxed);
    owners_dex_pc = lock_owner_dex_pc_.load(std::memory_order_relaxed);
    owners_sum = lock_owner_sum_.load(std::memory_order_relaxed);
  } while (owners_sum != LockOwnerInfoChecksum(owners_method, owners_dex_pc, owners_thread_id));
  if (owners_thread_id == thread_id) {
    *method = owners_method;
    *dex_pc = owners_dex_pc;
  } else {
    *method = nullptr;
    *dex_pc = 0;
  }
}


}  // namespace art

#endif  // ART_RUNTIME_MONITOR_INL_H_
