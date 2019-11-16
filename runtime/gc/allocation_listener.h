/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef ART_RUNTIME_GC_ALLOCATION_LISTENER_H_
#define ART_RUNTIME_GC_ALLOCATION_LISTENER_H_

#include <list>
#include <memory>

#include "base/locks.h"
#include "base/macros.h"
#include "gc_root.h"
#include "handle.h"
#include "obj_ptr.h"

namespace art {

namespace mirror {
class Class;
class Object;
}  // namespace mirror

class Thread;

namespace gc {

class AllocationListener {
 public:
  virtual ~AllocationListener() {}

  // An event to allow a listener to intercept and modify an allocation before it takes place.
  // The listener can change the byte_count and type as they see fit. Extreme caution should be used
  // when doing so. This can also be used to control allocation occurring on another thread.
  //
  // Concurrency guarantees: This might be called multiple times for each single allocation. It's
  // guaranteed that, between the final call to the callback and the object being visible to
  // heap-walks there are no suspensions. If a suspension was allowed between these events the
  // callback will be invoked again after passing the suspend point.
  //
  // If the alloc succeeds it is guaranteed there are no suspend-points between the last return of
  // PreObjectAlloc and the newly allocated object being visible to heap-walks.
  //
  // This can also be used to make any last-minute changes to the type or size of the allocation.
  virtual void PreObjectAllocated(Thread* self ATTRIBUTE_UNUSED,
                                  MutableHandle<mirror::Class> type ATTRIBUTE_UNUSED,
                                  size_t* byte_count ATTRIBUTE_UNUSED)
      REQUIRES(!Roles::uninterruptible_) REQUIRES_SHARED(Locks::mutator_lock_) {}
  // Fast check if we want to get the PreObjectAllocated callback, to avoid the expense of creating
  // handles. Defaults to false.
  virtual bool HasPreAlloc() const { return false; }
  virtual void ObjectAllocated(Thread* self, ObjPtr<mirror::Object>* obj, size_t byte_count)
      REQUIRES_SHARED(Locks::mutator_lock_) = 0;
};

}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_ALLOCATION_LISTENER_H_
