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

#ifndef ART_OPENJDKJVMTI_ALLOC_MANAGER_H_
#define ART_OPENJDKJVMTI_ALLOC_MANAGER_H_

#include <jvmti.h>

#include <atomic>

#include "base/locks.h"
#include "base/mutex.h"
#include "gc/allocation_listener.h"

namespace art {
template <typename T> class MutableHandle;
template <typename T> class ObjPtr;
class Thread;
namespace mirror {
class Class;
class Object;
}  // namespace mirror
}  // namespace art

namespace openjdkjvmti {

class AllocationManager;

class JvmtiAllocationListener : public art::gc::AllocationListener {
 public:
  explicit JvmtiAllocationListener(AllocationManager* manager) : manager_(manager) {}
  void ObjectAllocated(art::Thread* self,
                       art::ObjPtr<art::mirror::Object>* obj,
                       size_t cnt) override REQUIRES_SHARED(art::Locks::mutator_lock_);
  bool HasPreAlloc() const override REQUIRES_SHARED(art::Locks::mutator_lock_);
  void PreObjectAllocated(art::Thread* self,
                          art::MutableHandle<art::mirror::Class> type,
                          size_t* byte_count) override REQUIRES_SHARED(art::Locks::mutator_lock_);

 private:
  AllocationManager* manager_;
};

class AllocationManager {
 public:
  class AllocationCallback {
   public:
    virtual ~AllocationCallback() {}
    virtual void ObjectAllocated(art::Thread* self,
                                 art::ObjPtr<art::mirror::Object>* obj,
                                 size_t byte_count) REQUIRES_SHARED(art::Locks::mutator_lock_) = 0;
  };

  AllocationManager();

  void SetAllocListener(AllocationCallback* callback);
  void RemoveAllocListener();

  static AllocationManager* Get();

  void PauseAllocations(art::Thread* self) REQUIRES_SHARED(art::Locks::mutator_lock_);
  void ResumeAllocations(art::Thread* self) REQUIRES_SHARED(art::Locks::mutator_lock_);

  void EnableAllocationCallback(art::Thread* self) REQUIRES_SHARED(art::Locks::mutator_lock_);
  void DisableAllocationCallback(art::Thread* self) REQUIRES_SHARED(art::Locks::mutator_lock_);

 private:
  template<typename T>
  void PauseForAllocation(art::Thread* self, T msg) REQUIRES_SHARED(art::Locks::mutator_lock_);
  void IncrListenerInstall(art::Thread* self) REQUIRES_SHARED(art::Locks::mutator_lock_);
  void DecrListenerInstall(art::Thread* self) REQUIRES_SHARED(art::Locks::mutator_lock_);

  AllocationCallback* callback_ = nullptr;
  uint32_t listener_refcount_ GUARDED_BY(alloc_listener_mutex_) = 0;
  std::atomic<bool> allocations_paused_ever_ = false;
  std::atomic<art::Thread*> allocations_paused_thread_ = nullptr;
  std::atomic<bool> callback_enabled_ = false;
  std::unique_ptr<JvmtiAllocationListener> alloc_listener_ = nullptr;
  art::Mutex alloc_listener_mutex_ ACQUIRED_AFTER(art::Locks::user_code_suspension_lock_);
  art::ConditionVariable alloc_pause_cv_;

  friend class JvmtiAllocationListener;
};

}  // namespace openjdkjvmti

#endif  // ART_OPENJDKJVMTI_ALLOC_MANAGER_H_
