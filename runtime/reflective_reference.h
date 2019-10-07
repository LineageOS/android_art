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

#ifndef ART_RUNTIME_REFLECTIVE_REFERENCE_H_
#define ART_RUNTIME_REFLECTIVE_REFERENCE_H_

#include "android-base/macros.h"
#include "base/macros.h"
#include "mirror/object_reference.h"

namespace art {

class ArtField;
class ArtMethod;
// A reference to a ArtField or ArtMethod.
template <class ReflectiveType>
class ReflectiveReference {
 public:
  static_assert(std::is_same_v<ReflectiveType, ArtMethod> ||
                    std::is_same_v<ReflectiveType, ArtField>,
                "Uknown type!");
  ReflectiveReference() : val_(nullptr) {}
  explicit ReflectiveReference(ReflectiveType* r) : val_(r) {}
  ReflectiveReference<ReflectiveType>& operator=(const ReflectiveReference<ReflectiveType>& t) =
      default;

  ReflectiveType* Ptr() {
    return val_;
  }

  void Assign(ReflectiveType* r) {
    val_ = r;
  }

  bool IsNull() const {
    return val_ == nullptr;
  }

  bool operator==(const ReflectiveReference<ReflectiveType>& rr) const {
    return val_ == rr.val_;
  }
  bool operator!=(const ReflectiveReference<ReflectiveType>& rr) const {
    return !operator==(rr);
  }
  bool operator==(std::nullptr_t) const {
    return IsNull();
  }
  bool operator!=(std::nullptr_t) const {
    return !IsNull();
  }

 private:
  ReflectiveType* val_;
};

}  // namespace art

#endif  // ART_RUNTIME_REFLECTIVE_REFERENCE_H_
