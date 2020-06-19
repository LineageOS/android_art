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

#ifndef ART_RUNTIME_ARCH_ARM64_JNI_FRAME_ARM64_H_
#define ART_RUNTIME_ARCH_ARM64_JNI_FRAME_ARM64_H_

#include <string.h>

#include "arch/instruction_set.h"
#include "base/bit_utils.h"
#include "base/globals.h"
#include "base/logging.h"

namespace art {
namespace arm64 {

constexpr size_t kFramePointerSize = static_cast<size_t>(PointerSize::k64);
static_assert(kArm64PointerSize == PointerSize::k64, "Unexpected ARM64 pointer size");

// The AAPCS64 requires 16-byte alignement. This is the same as the Managed ABI stack alignment.
static constexpr size_t kAapcs64StackAlignment = 16u;
static_assert(kAapcs64StackAlignment == kStackAlignment);

// Up to how many float-like (float, double) args can be in registers.
// The rest of the args must go on the stack.
constexpr size_t kMaxFloatOrDoubleRegisterArguments = 8u;
// Up to how many integer-like (pointers, objects, longs, int, short, bool, etc) args can be
// in registers. The rest of the args must go on the stack.
constexpr size_t kMaxIntLikeRegisterArguments = 8u;

// Get the size of the arguments for a native call.
inline size_t GetNativeOutArgsSize(size_t num_fp_args, size_t num_non_fp_args) {
  // Account for FP arguments passed through v0-v7.
  size_t num_stack_fp_args =
      num_fp_args - std::min(kMaxFloatOrDoubleRegisterArguments, num_fp_args);
  // Account for other (integer and pointer) arguments passed through GPR (x0-x7).
  size_t num_stack_non_fp_args =
      num_non_fp_args - std::min(kMaxIntLikeRegisterArguments, num_non_fp_args);
  // Each stack argument takes 8 bytes.
  return (num_stack_fp_args + num_stack_non_fp_args) * static_cast<size_t>(kArm64PointerSize);
}

// Get stack args size for @CriticalNative method calls.
inline size_t GetCriticalNativeCallArgsSize(const char* shorty, uint32_t shorty_len) {
  DCHECK_EQ(shorty_len, strlen(shorty));

  size_t num_fp_args =
      std::count_if(shorty + 1, shorty + shorty_len, [](char c) { return c == 'F' || c == 'D'; });
  size_t num_non_fp_args = shorty_len - 1u - num_fp_args;

  return GetNativeOutArgsSize(num_fp_args, num_non_fp_args);
}

// Get the frame size for @CriticalNative method stub.
// This must match the size of the extra frame emitted by the compiler at the native call site.
inline size_t GetCriticalNativeStubFrameSize(const char* shorty, uint32_t shorty_len) {
  // The size of outgoing arguments.
  size_t size = GetCriticalNativeCallArgsSize(shorty, shorty_len);

  // We can make a tail call if there are no stack args and we do not need
  // to extend the result. Otherwise, add space for return PC.
  if (size != 0u || shorty[0] == 'B' || shorty[0] == 'C' || shorty[0] == 'S' || shorty[0] == 'Z') {
    size += kFramePointerSize;  // We need to spill LR with the args.
  }
  return RoundUp(size, kAapcs64StackAlignment);
}

// Get the frame size for direct call to a @CriticalNative method.
// This must match the size of the frame emitted by the JNI compiler at the native call site.
inline size_t GetCriticalNativeDirectCallFrameSize(const char* shorty, uint32_t shorty_len) {
  // The size of outgoing arguments.
  size_t size = GetCriticalNativeCallArgsSize(shorty, shorty_len);

  // No return PC to save, zero- and sign-extension are handled by the caller.
  return RoundUp(size, kAapcs64StackAlignment);
}

}  // namespace arm64
}  // namespace art

#endif  // ART_RUNTIME_ARCH_ARM64_JNI_FRAME_ARM64_H_

