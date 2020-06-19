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

#ifndef ART_RUNTIME_ARCH_X86_JNI_FRAME_X86_H_
#define ART_RUNTIME_ARCH_X86_JNI_FRAME_X86_H_

#include <string.h>

#include "arch/instruction_set.h"
#include "base/bit_utils.h"
#include "base/globals.h"
#include "base/logging.h"

namespace art {
namespace x86 {

constexpr size_t kFramePointerSize = static_cast<size_t>(PointerSize::k32);
static_assert(kX86PointerSize == PointerSize::k32, "Unexpected x86 pointer size");

static constexpr size_t kNativeStackAlignment = 16;  // IA-32 cdecl requires 16 byte alignment.
static_assert(kNativeStackAlignment == kStackAlignment);

// Get the size of the arguments for a native call.
inline size_t GetNativeOutArgsSize(size_t num_args, size_t num_long_or_double_args) {
  size_t num_arg_words = num_args + num_long_or_double_args;
  return num_arg_words * static_cast<size_t>(kX86PointerSize);
}

// Get stack args size for @CriticalNative method calls.
inline size_t GetCriticalNativeCallArgsSize(const char* shorty, uint32_t shorty_len) {
  DCHECK_EQ(shorty_len, strlen(shorty));

  size_t num_long_or_double_args =
      std::count_if(shorty + 1, shorty + shorty_len, [](char c) { return c == 'J' || c == 'D'; });

  return GetNativeOutArgsSize(/*num_args=*/ shorty_len - 1u, num_long_or_double_args);
}

// Get the frame size for @CriticalNative method stub.
// This must match the size of the frame emitted by the JNI compiler at the native call site.
inline size_t GetCriticalNativeStubFrameSize(const char* shorty, uint32_t shorty_len) {
  // The size of outgoing arguments.
  size_t size = GetCriticalNativeCallArgsSize(shorty, shorty_len);

  // We can make a tail call if there are no stack args and the return type is not
  // FP type (needs moving from ST0 to MMX0) and we do not need to extend the result.
  bool return_type_ok = shorty[0] == 'I' || shorty[0] == 'J' || shorty[0] == 'V';
  if (return_type_ok && size == 0u) {
    return 0u;
  }

  // Add return address size.
  size += kFramePointerSize;
  return RoundUp(size, kNativeStackAlignment);
}

// Get the frame size for direct call to a @CriticalNative method.
// This must match the size of the extra frame emitted by the compiler at the native call site.
inline size_t GetCriticalNativeDirectCallFrameSize(const char* shorty, uint32_t shorty_len) {
  // The size of outgoing arguments.
  size_t size = GetCriticalNativeCallArgsSize(shorty, shorty_len);

  // No return PC to save, zero- and sign-extension and FP value moves are handled by the caller.
  return RoundUp(size, kNativeStackAlignment);
}

}  // namespace x86
}  // namespace art

#endif  // ART_RUNTIME_ARCH_X86_JNI_FRAME_X86_H_

