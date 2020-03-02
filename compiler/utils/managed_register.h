/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_COMPILER_UTILS_MANAGED_REGISTER_H_
#define ART_COMPILER_UTILS_MANAGED_REGISTER_H_

#include <type_traits>
#include <vector>

#include "base/value_object.h"

namespace art {

namespace arm {
class ArmManagedRegister;
}  // namespace arm
namespace arm64 {
class Arm64ManagedRegister;
}  // namespace arm64

namespace x86 {
class X86ManagedRegister;
}  // namespace x86

namespace x86_64 {
class X86_64ManagedRegister;
}  // namespace x86_64

class ManagedRegister : public ValueObject {
 public:
  // ManagedRegister is a value class. There exists no method to change the
  // internal state. We therefore allow a copy constructor and an
  // assignment-operator.
  constexpr ManagedRegister(const ManagedRegister& other) = default;

  ManagedRegister& operator=(const ManagedRegister& other) = default;

  constexpr arm::ArmManagedRegister AsArm() const;
  constexpr arm64::Arm64ManagedRegister AsArm64() const;
  constexpr x86::X86ManagedRegister AsX86() const;
  constexpr x86_64::X86_64ManagedRegister AsX86_64() const;

  // It is valid to invoke Equals on and with a NoRegister.
  constexpr bool Equals(const ManagedRegister& other) const {
    return id_ == other.id_;
  }

  constexpr bool IsRegister() const {
    return id_ != kNoRegister;
  }

  constexpr bool IsNoRegister() const {
    return id_ == kNoRegister;
  }

  static constexpr ManagedRegister NoRegister() {
    return ManagedRegister();
  }

  constexpr int RegId() const { return id_; }
  explicit constexpr ManagedRegister(int reg_id) : id_(reg_id) { }

 protected:
  static const int kNoRegister = -1;

  constexpr ManagedRegister() : id_(kNoRegister) { }

  int id_;
};

static_assert(std::is_trivially_copyable<ManagedRegister>::value,
              "ManagedRegister should be trivially copyable");

}  // namespace art

#endif  // ART_COMPILER_UTILS_MANAGED_REGISTER_H_
