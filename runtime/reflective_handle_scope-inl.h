/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef ART_RUNTIME_REFLECTIVE_HANDLE_SCOPE_INL_H_
#define ART_RUNTIME_REFLECTIVE_HANDLE_SCOPE_INL_H_

#include "android-base/thread_annotations.h"
#include "base/mutex.h"
#include "reflective_handle.h"
#include "reflective_handle_scope.h"
#include "thread-current-inl.h"

namespace art {

template <size_t kNumFields, size_t kNumMethods>
StackReflectiveHandleScope<kNumFields, kNumMethods>::StackReflectiveHandleScope(Thread* self) : field_pos_(0), method_pos_(0) {
  DCHECK_EQ(self, Thread::Current());
  PushScope(self);
}

template <size_t kNumFields, size_t kNumMethods>
void StackReflectiveHandleScope<kNumFields, kNumMethods>::VisitTargets(
    ReflectiveValueVisitor* visitor) {
  Thread* self = Thread::Current();
  DCHECK(GetThread() == self ||
         Locks::mutator_lock_->IsExclusiveHeld(self))
      << *GetThread() << " on thread " << *self;
  auto visit_one = [&](auto& rv) NO_THREAD_SAFETY_ANALYSIS {
    Locks::mutator_lock_->AssertSharedHeld(self);
    if (!rv.IsNull()) {
      rv.Assign((*visitor)(rv.Ptr(), ReflectiveHandleScopeSourceInfo(this)));
    }
  };
  std::for_each(fields_.begin(), fields_.begin() + field_pos_, visit_one);
  std::for_each(methods_.begin(), methods_.begin() + method_pos_, visit_one);
}

template <size_t kNumFields, size_t kNumMethods>
StackReflectiveHandleScope<kNumFields, kNumMethods>::~StackReflectiveHandleScope() {
  PopScope();
}

void BaseReflectiveHandleScope::PushScope(Thread* self) {
  DCHECK_EQ(self, Thread::Current());
  self_ = self;
  link_ = self_->GetTopReflectiveHandleScope();
  self_->PushReflectiveHandleScope(this);
}

void BaseReflectiveHandleScope::PopScope() {
  auto* prev = self_->PopReflectiveHandleScope();
  CHECK_EQ(prev, this);
  link_ = nullptr;
}

}  // namespace art

#endif  // ART_RUNTIME_REFLECTIVE_HANDLE_SCOPE_INL_H_
