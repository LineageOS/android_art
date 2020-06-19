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

#include "calling_convention_arm64.h"

#include <android-base/logging.h>

#include "arch/arm64/jni_frame_arm64.h"
#include "arch/instruction_set.h"
#include "handle_scope-inl.h"
#include "utils/arm64/managed_register_arm64.h"

namespace art {
namespace arm64 {

static const XRegister kXArgumentRegisters[] = {
  X0, X1, X2, X3, X4, X5, X6, X7
};
static_assert(kMaxIntLikeRegisterArguments == arraysize(kXArgumentRegisters));

static const WRegister kWArgumentRegisters[] = {
  W0, W1, W2, W3, W4, W5, W6, W7
};
static_assert(kMaxIntLikeRegisterArguments == arraysize(kWArgumentRegisters));

static const DRegister kDArgumentRegisters[] = {
  D0, D1, D2, D3, D4, D5, D6, D7
};
static_assert(kMaxFloatOrDoubleRegisterArguments == arraysize(kDArgumentRegisters));

static const SRegister kSArgumentRegisters[] = {
  S0, S1, S2, S3, S4, S5, S6, S7
};
static_assert(kMaxFloatOrDoubleRegisterArguments == arraysize(kSArgumentRegisters));

static constexpr ManagedRegister kCalleeSaveRegisters[] = {
    // Core registers.
    // Note: The native jni function may call to some VM runtime functions which may suspend
    // or trigger GC. And the jni method frame will become top quick frame in those cases.
    // So we need to satisfy GC to save LR and callee-save registers which is similar to
    // CalleeSaveMethod(RefOnly) frame.
    // Jni function is the native function which the java code wants to call.
    // Jni method is the method that is compiled by jni compiler.
    // Call chain: managed code(java) --> jni method --> jni function.
    // This does not apply to the @CriticalNative.

    // Thread register(X19) is saved on stack.
    Arm64ManagedRegister::FromXRegister(X19),
    Arm64ManagedRegister::FromXRegister(X20),
    Arm64ManagedRegister::FromXRegister(X21),
    Arm64ManagedRegister::FromXRegister(X22),
    Arm64ManagedRegister::FromXRegister(X23),
    Arm64ManagedRegister::FromXRegister(X24),
    Arm64ManagedRegister::FromXRegister(X25),
    Arm64ManagedRegister::FromXRegister(X26),
    Arm64ManagedRegister::FromXRegister(X27),
    Arm64ManagedRegister::FromXRegister(X28),
    Arm64ManagedRegister::FromXRegister(X29),
    Arm64ManagedRegister::FromXRegister(LR),
    // Hard float registers.
    // Considering the case, java_method_1 --> jni method --> jni function --> java_method_2,
    // we may break on java_method_2 and we still need to find out the values of DEX registers
    // in java_method_1. So all callee-saves(in managed code) need to be saved.
    Arm64ManagedRegister::FromDRegister(D8),
    Arm64ManagedRegister::FromDRegister(D9),
    Arm64ManagedRegister::FromDRegister(D10),
    Arm64ManagedRegister::FromDRegister(D11),
    Arm64ManagedRegister::FromDRegister(D12),
    Arm64ManagedRegister::FromDRegister(D13),
    Arm64ManagedRegister::FromDRegister(D14),
    Arm64ManagedRegister::FromDRegister(D15),
};

template <size_t size>
static constexpr uint32_t CalculateCoreCalleeSpillMask(
    const ManagedRegister (&callee_saves)[size]) {
  uint32_t result = 0u;
  for (auto&& r : callee_saves) {
    if (r.AsArm64().IsXRegister()) {
      result |= (1u << r.AsArm64().AsXRegister());
    }
  }
  return result;
}

template <size_t size>
static constexpr uint32_t CalculateFpCalleeSpillMask(const ManagedRegister (&callee_saves)[size]) {
  uint32_t result = 0u;
  for (auto&& r : callee_saves) {
    if (r.AsArm64().IsDRegister()) {
      result |= (1u << r.AsArm64().AsDRegister());
    }
  }
  return result;
}

static constexpr uint32_t kCoreCalleeSpillMask = CalculateCoreCalleeSpillMask(kCalleeSaveRegisters);
static constexpr uint32_t kFpCalleeSpillMask = CalculateFpCalleeSpillMask(kCalleeSaveRegisters);

static constexpr ManagedRegister kAapcs64CalleeSaveRegisters[] = {
    // Core registers.
    Arm64ManagedRegister::FromXRegister(X19),
    Arm64ManagedRegister::FromXRegister(X20),
    Arm64ManagedRegister::FromXRegister(X21),
    Arm64ManagedRegister::FromXRegister(X22),
    Arm64ManagedRegister::FromXRegister(X23),
    Arm64ManagedRegister::FromXRegister(X24),
    Arm64ManagedRegister::FromXRegister(X25),
    Arm64ManagedRegister::FromXRegister(X26),
    Arm64ManagedRegister::FromXRegister(X27),
    Arm64ManagedRegister::FromXRegister(X28),
    Arm64ManagedRegister::FromXRegister(X29),
    Arm64ManagedRegister::FromXRegister(LR),
    // Hard float registers.
    Arm64ManagedRegister::FromDRegister(D8),
    Arm64ManagedRegister::FromDRegister(D9),
    Arm64ManagedRegister::FromDRegister(D10),
    Arm64ManagedRegister::FromDRegister(D11),
    Arm64ManagedRegister::FromDRegister(D12),
    Arm64ManagedRegister::FromDRegister(D13),
    Arm64ManagedRegister::FromDRegister(D14),
    Arm64ManagedRegister::FromDRegister(D15),
};

static constexpr uint32_t kAapcs64CoreCalleeSpillMask =
    CalculateCoreCalleeSpillMask(kAapcs64CalleeSaveRegisters);
static constexpr uint32_t kAapcs64FpCalleeSpillMask =
    CalculateFpCalleeSpillMask(kAapcs64CalleeSaveRegisters);

// Calling convention
static ManagedRegister ReturnRegisterForShorty(const char* shorty) {
  if (shorty[0] == 'F') {
    return Arm64ManagedRegister::FromSRegister(S0);
  } else if (shorty[0] == 'D') {
    return Arm64ManagedRegister::FromDRegister(D0);
  } else if (shorty[0] == 'J') {
    return Arm64ManagedRegister::FromXRegister(X0);
  } else if (shorty[0] == 'V') {
    return Arm64ManagedRegister::NoRegister();
  } else {
    return Arm64ManagedRegister::FromWRegister(W0);
  }
}

ManagedRegister Arm64ManagedRuntimeCallingConvention::ReturnRegister() {
  return ReturnRegisterForShorty(GetShorty());
}

ManagedRegister Arm64JniCallingConvention::ReturnRegister() {
  return ReturnRegisterForShorty(GetShorty());
}

ManagedRegister Arm64JniCallingConvention::IntReturnRegister() {
  return Arm64ManagedRegister::FromWRegister(W0);
}

// Managed runtime calling convention

ManagedRegister Arm64ManagedRuntimeCallingConvention::MethodRegister() {
  return Arm64ManagedRegister::FromXRegister(X0);
}

bool Arm64ManagedRuntimeCallingConvention::IsCurrentParamInRegister() {
  if (IsCurrentParamAFloatOrDouble()) {
    return itr_float_and_doubles_ < kMaxFloatOrDoubleRegisterArguments;
  } else {
    size_t non_fp_arg_number = itr_args_ - itr_float_and_doubles_;
    return /* method */ 1u + non_fp_arg_number < kMaxIntLikeRegisterArguments;
  }
}

bool Arm64ManagedRuntimeCallingConvention::IsCurrentParamOnStack() {
  return !IsCurrentParamInRegister();
}

ManagedRegister Arm64ManagedRuntimeCallingConvention::CurrentParamRegister() {
  DCHECK(IsCurrentParamInRegister());
  if (IsCurrentParamAFloatOrDouble()) {
    if (IsCurrentParamADouble()) {
      return Arm64ManagedRegister::FromDRegister(kDArgumentRegisters[itr_float_and_doubles_]);
    } else {
      return Arm64ManagedRegister::FromSRegister(kSArgumentRegisters[itr_float_and_doubles_]);
    }
  } else {
    size_t non_fp_arg_number = itr_args_ - itr_float_and_doubles_;
    if (IsCurrentParamALong()) {
      XRegister x_reg = kXArgumentRegisters[/* method */ 1u + non_fp_arg_number];
      return Arm64ManagedRegister::FromXRegister(x_reg);
    } else {
      WRegister w_reg = kWArgumentRegisters[/* method */ 1u + non_fp_arg_number];
      return Arm64ManagedRegister::FromWRegister(w_reg);
    }
  }
}

FrameOffset Arm64ManagedRuntimeCallingConvention::CurrentParamStackOffset() {
  return FrameOffset(displacement_.Int32Value() +  // displacement
                     kFramePointerSize +  // Method ref
                     (itr_slots_ * sizeof(uint32_t)));  // offset into in args
}

// JNI calling convention

Arm64JniCallingConvention::Arm64JniCallingConvention(bool is_static,
                                                     bool is_synchronized,
                                                     bool is_critical_native,
                                                     const char* shorty)
    : JniCallingConvention(is_static,
                           is_synchronized,
                           is_critical_native,
                           shorty,
                           kArm64PointerSize) {
}

uint32_t Arm64JniCallingConvention::CoreSpillMask() const {
  return is_critical_native_ ? 0u : kCoreCalleeSpillMask;
}

uint32_t Arm64JniCallingConvention::FpSpillMask() const {
  return is_critical_native_ ? 0u : kFpCalleeSpillMask;
}

ManagedRegister Arm64JniCallingConvention::ReturnScratchRegister() const {
  return ManagedRegister::NoRegister();
}

size_t Arm64JniCallingConvention::FrameSize() const {
  if (is_critical_native_) {
    CHECK(!SpillsMethod());
    CHECK(!HasLocalReferenceSegmentState());
    CHECK(!HasHandleScope());
    CHECK(!SpillsReturnValue());
    return 0u;  // There is no managed frame for @CriticalNative.
  }

  // Method*, callee save area size, local reference segment state
  CHECK(SpillsMethod());
  size_t method_ptr_size = static_cast<size_t>(kFramePointerSize);
  size_t callee_save_area_size = CalleeSaveRegisters().size() * kFramePointerSize;
  size_t total_size = method_ptr_size + callee_save_area_size;

  CHECK(HasLocalReferenceSegmentState());
  total_size += sizeof(uint32_t);

  CHECK(HasHandleScope());
  total_size += HandleScope::SizeOf(kArm64PointerSize, ReferenceCount());

  // Plus return value spill area size
  CHECK(SpillsReturnValue());
  total_size += SizeOfReturnValue();

  return RoundUp(total_size, kStackAlignment);
}

size_t Arm64JniCallingConvention::OutFrameSize() const {
  // Count param args, including JNIEnv* and jclass*.
  size_t all_args = NumberOfExtraArgumentsForJni() + NumArgs();
  size_t num_fp_args = NumFloatOrDoubleArgs();
  DCHECK_GE(all_args, num_fp_args);
  size_t num_non_fp_args = all_args - num_fp_args;
  // The size of outgoing arguments.
  size_t size = GetNativeOutArgsSize(num_fp_args, num_non_fp_args);

  // @CriticalNative can use tail call as all managed callee saves are preserved by AAPCS64.
  static_assert((kCoreCalleeSpillMask & ~kAapcs64CoreCalleeSpillMask) == 0u);
  static_assert((kFpCalleeSpillMask & ~kAapcs64FpCalleeSpillMask) == 0u);

  // For @CriticalNative, we can make a tail call if there are no stack args and
  // we do not need to extend the result. Otherwise, add space for return PC.
  if (is_critical_native_ && (size != 0u || RequiresSmallResultTypeExtension())) {
    size += kFramePointerSize;  // We need to spill LR with the args.
  }
  size_t out_args_size = RoundUp(size, kAapcs64StackAlignment);
  if (UNLIKELY(IsCriticalNative())) {
    DCHECK_EQ(out_args_size, GetCriticalNativeStubFrameSize(GetShorty(), NumArgs() + 1u));
  }
  return out_args_size;
}

ArrayRef<const ManagedRegister> Arm64JniCallingConvention::CalleeSaveRegisters() const {
  if (UNLIKELY(IsCriticalNative())) {
    if (UseTailCall()) {
      return ArrayRef<const ManagedRegister>();  // Do not spill anything.
    } else {
      // Spill LR with out args.
      static_assert((kCoreCalleeSpillMask >> LR) == 1u);  // Contains LR as the highest bit.
      constexpr size_t lr_index = POPCOUNT(kCoreCalleeSpillMask) - 1u;
      static_assert(kCalleeSaveRegisters[lr_index].Equals(
                        Arm64ManagedRegister::FromXRegister(LR)));
      return ArrayRef<const ManagedRegister>(kCalleeSaveRegisters).SubArray(
          /*pos*/ lr_index, /*length=*/ 1u);
    }
  } else {
    return ArrayRef<const ManagedRegister>(kCalleeSaveRegisters);
  }
}

bool Arm64JniCallingConvention::IsCurrentParamInRegister() {
  if (IsCurrentParamAFloatOrDouble()) {
    return (itr_float_and_doubles_ < kMaxFloatOrDoubleRegisterArguments);
  } else {
    return ((itr_args_ - itr_float_and_doubles_) < kMaxIntLikeRegisterArguments);
  }
  // TODO: Can we just call CurrentParamRegister to figure this out?
}

bool Arm64JniCallingConvention::IsCurrentParamOnStack() {
  // Is this ever not the same for all the architectures?
  return !IsCurrentParamInRegister();
}

ManagedRegister Arm64JniCallingConvention::CurrentParamRegister() {
  CHECK(IsCurrentParamInRegister());
  if (IsCurrentParamAFloatOrDouble()) {
    CHECK_LT(itr_float_and_doubles_, kMaxFloatOrDoubleRegisterArguments);
    if (IsCurrentParamADouble()) {
      return Arm64ManagedRegister::FromDRegister(kDArgumentRegisters[itr_float_and_doubles_]);
    } else {
      return Arm64ManagedRegister::FromSRegister(kSArgumentRegisters[itr_float_and_doubles_]);
    }
  } else {
    int gp_reg = itr_args_ - itr_float_and_doubles_;
    CHECK_LT(static_cast<unsigned int>(gp_reg), kMaxIntLikeRegisterArguments);
    if (IsCurrentParamALong() || IsCurrentParamAReference() || IsCurrentParamJniEnv())  {
      return Arm64ManagedRegister::FromXRegister(kXArgumentRegisters[gp_reg]);
    } else {
      return Arm64ManagedRegister::FromWRegister(kWArgumentRegisters[gp_reg]);
    }
  }
}

FrameOffset Arm64JniCallingConvention::CurrentParamStackOffset() {
  CHECK(IsCurrentParamOnStack());
  size_t args_on_stack = itr_args_
                  - std::min(kMaxFloatOrDoubleRegisterArguments,
                             static_cast<size_t>(itr_float_and_doubles_))
                  - std::min(kMaxIntLikeRegisterArguments,
                             static_cast<size_t>(itr_args_ - itr_float_and_doubles_));
  size_t offset = displacement_.Int32Value() - OutFrameSize() + (args_on_stack * kFramePointerSize);
  CHECK_LT(offset, OutFrameSize());
  return FrameOffset(offset);
}

ManagedRegister Arm64JniCallingConvention::HiddenArgumentRegister() const {
  CHECK(IsCriticalNative());
  // X15 is neither managed callee-save, nor argument register, nor scratch register.
  // TODO: Change to static_assert; std::none_of should be constexpr since C++20.
  DCHECK(std::none_of(kCalleeSaveRegisters,
                      kCalleeSaveRegisters + std::size(kCalleeSaveRegisters),
                      [](ManagedRegister callee_save) constexpr {
                        return callee_save.Equals(Arm64ManagedRegister::FromXRegister(X15));
                      }));
  DCHECK(std::none_of(kXArgumentRegisters,
                      kXArgumentRegisters + std::size(kXArgumentRegisters),
                      [](XRegister reg) { return reg == X15; }));
  return Arm64ManagedRegister::FromXRegister(X15);
}

// Whether to use tail call (used only for @CriticalNative).
bool Arm64JniCallingConvention::UseTailCall() const {
  CHECK(IsCriticalNative());
  return OutFrameSize() == 0u;
}

}  // namespace arm64
}  // namespace art
