/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "reflective_value_visitor.h"
#include <sstream>

#include "base/locks.h"
#include "base/mutex-inl.h"
#include "mirror/class.h"
#include "mirror/object-inl.h"

namespace art {

void HeapReflectiveSourceInfo::Describe(std::ostream& os) const {
  Locks::mutator_lock_->AssertExclusiveHeld(Thread::Current());
  ReflectionSourceInfo::Describe(os);
  os << " Class=" << src_->GetClass()->PrettyClass();
}

template<>
void JniIdReflectiveSourceInfo<jfieldID>::Describe(std::ostream& os) const {
  ReflectionSourceInfo::Describe(os);
  os << " jfieldID=" << reinterpret_cast<uintptr_t>(id_);
}

template<>
void JniIdReflectiveSourceInfo<jmethodID>::Describe(std::ostream& os) const {
  ReflectionSourceInfo::Describe(os);
  os << " jmethodID=" << reinterpret_cast<uintptr_t>(id_);
}

void ReflectiveHandleScopeSourceInfo::Describe(std::ostream& os) const {
  ReflectionSourceInfo::Describe(os);
  os << " source= (" << source_ << ") ";
  if (source_ == nullptr) {
    os << "nullptr";
  } else {
    os << *source_;
  }
}
}  // namespace art
