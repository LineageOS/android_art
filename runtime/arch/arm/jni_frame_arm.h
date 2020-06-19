/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef ART_RUNTIME_ARCH_ARM_JNI_FRAME_ARM_H_
#define ART_RUNTIME_ARCH_ARM_JNI_FRAME_ARM_H_

#include <string.h>

#include "arch/instruction_set.h"
#include "base/bit_utils.h"
#include "base/globals.h"
#include "base/logging.h"

namespace art {
namespace arm {

constexpr size_t kFramePointerSize = static_cast<size_t>(PointerSize::k32);
static_assert(kArmPointerSize == PointerSize::k32, "Unexpected ARM pointer size");

// The AAPCS requires 8-byte alignement. This is not as strict as the Managed ABI stack alignment.
static constexpr size_t kAapcsStackAlignment = 8u;
static_assert(kAapcsStackAlignment < kStackAlignment);

// How many registers can be used for passing arguments.
// Note: AAPCS is soft-float, so these are all core registers.
constexpr size_t kJniArgumentRegisterCount = 4u;

// Get stack args size for @CriticalNative method calls.
inline size_t GetCriticalNativeCallArgsSize(const char* shorty, uint32_t shorty_len) {
  DCHECK_EQ(shorty_len, strlen(shorty));

  size_t reg = 0;  // Register for the current argument; if reg >= 4, we shall use stack.
  for (size_t i = 1; i != shorty_len; ++i) {
    if (shorty[i] == 'J' || shorty[i] == 'D') {
      // 8-byte args need to start in even-numbered register or at aligned stack position.
      reg += (reg & 1);
      // Count first word and let the common path count the second.
      reg += 1u;
    }
    reg += 1u;
  }
  size_t stack_args = std::max(reg, kJniArgumentRegisterCount) - kJniArgumentRegisterCount;
  return kFramePointerSize * stack_args;
}

// Get the frame size for @CriticalNative method stub.
// This must match the size of the frame emitted by the JNI compiler at the native call site.
inline size_t GetCriticalNativeStubFrameSize(const char* shorty, uint32_t shorty_len) {
  // The size of outgoing arguments.
  size_t size = GetCriticalNativeCallArgsSize(shorty, shorty_len);

  // Check if this is a tail call, i.e. there are no stack args and the return type
  // is not  an FP type (otherwise we need to move the result to FP register).
  // No need to sign/zero extend small return types thanks to AAPCS.
  if (size != 0u || shorty[0] == 'F' || shorty[0] == 'D') {
    size += kFramePointerSize;  // We need to spill LR with the args.
  }
  return RoundUp(size, kAapcsStackAlignment);
}

// Get the frame size for direct call to a @CriticalNative method.
// This must match the size of the extra frame emitted by the compiler at the native call site.
inline size_t GetCriticalNativeDirectCallFrameSize(const char* shorty, uint32_t shorty_len) {
  // The size of outgoing arguments.
  size_t size = GetCriticalNativeCallArgsSize(shorty, shorty_len);

  // No return PC to save, zero- and sign-extension and FP value moves are handled by the caller.
  return RoundUp(size, kAapcsStackAlignment);
}

}  // namespace arm
}  // namespace art

#endif  // ART_RUNTIME_ARCH_ARM_JNI_FRAME_ARM_H_

