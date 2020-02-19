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

#include "calling_convention_x86_64.h"

#include <android-base/logging.h>

#include "arch/instruction_set.h"
#include "arch/x86_64/jni_frame_x86_64.h"
#include "base/bit_utils.h"
#include "handle_scope-inl.h"
#include "utils/x86_64/managed_register_x86_64.h"

namespace art {
namespace x86_64 {

static constexpr ManagedRegister kCalleeSaveRegisters[] = {
    // Core registers.
    X86_64ManagedRegister::FromCpuRegister(RBX),
    X86_64ManagedRegister::FromCpuRegister(RBP),
    X86_64ManagedRegister::FromCpuRegister(R12),
    X86_64ManagedRegister::FromCpuRegister(R13),
    X86_64ManagedRegister::FromCpuRegister(R14),
    X86_64ManagedRegister::FromCpuRegister(R15),
    // Hard float registers.
    X86_64ManagedRegister::FromXmmRegister(XMM12),
    X86_64ManagedRegister::FromXmmRegister(XMM13),
    X86_64ManagedRegister::FromXmmRegister(XMM14),
    X86_64ManagedRegister::FromXmmRegister(XMM15),
};

template <size_t size>
static constexpr uint32_t CalculateCoreCalleeSpillMask(
    const ManagedRegister (&callee_saves)[size]) {
  // The spilled PC gets a special marker.
  uint32_t result = 1u << kNumberOfCpuRegisters;
  for (auto&& r : callee_saves) {
    if (r.AsX86_64().IsCpuRegister()) {
      result |= (1u << r.AsX86_64().AsCpuRegister().AsRegister());
    }
  }
  return result;
}

template <size_t size>
static constexpr uint32_t CalculateFpCalleeSpillMask(const ManagedRegister (&callee_saves)[size]) {
  uint32_t result = 0u;
  for (auto&& r : callee_saves) {
    if (r.AsX86_64().IsXmmRegister()) {
      result |= (1u << r.AsX86_64().AsXmmRegister().AsFloatRegister());
    }
  }
  return result;
}

static constexpr uint32_t kCoreCalleeSpillMask = CalculateCoreCalleeSpillMask(kCalleeSaveRegisters);
static constexpr uint32_t kFpCalleeSpillMask = CalculateFpCalleeSpillMask(kCalleeSaveRegisters);

static constexpr ManagedRegister kNativeCalleeSaveRegisters[] = {
    // Core registers.
    X86_64ManagedRegister::FromCpuRegister(RBX),
    X86_64ManagedRegister::FromCpuRegister(RBP),
    X86_64ManagedRegister::FromCpuRegister(R12),
    X86_64ManagedRegister::FromCpuRegister(R13),
    X86_64ManagedRegister::FromCpuRegister(R14),
    X86_64ManagedRegister::FromCpuRegister(R15),
    // No callee-save float registers.
};

static constexpr uint32_t kNativeCoreCalleeSpillMask =
    CalculateCoreCalleeSpillMask(kNativeCalleeSaveRegisters);
static constexpr uint32_t kNativeFpCalleeSpillMask =
    CalculateFpCalleeSpillMask(kNativeCalleeSaveRegisters);

// Calling convention

ManagedRegister X86_64ManagedRuntimeCallingConvention::InterproceduralScratchRegister() const {
  return X86_64ManagedRegister::FromCpuRegister(RAX);
}

ManagedRegister X86_64JniCallingConvention::InterproceduralScratchRegister() const {
  return X86_64ManagedRegister::FromCpuRegister(RAX);
}

ManagedRegister X86_64JniCallingConvention::ReturnScratchRegister() const {
  return ManagedRegister::NoRegister();  // No free regs, so assembler uses push/pop
}

static ManagedRegister ReturnRegisterForShorty(const char* shorty, bool jni ATTRIBUTE_UNUSED) {
  if (shorty[0] == 'F' || shorty[0] == 'D') {
    return X86_64ManagedRegister::FromXmmRegister(XMM0);
  } else if (shorty[0] == 'J') {
    return X86_64ManagedRegister::FromCpuRegister(RAX);
  } else if (shorty[0] == 'V') {
    return ManagedRegister::NoRegister();
  } else {
    return X86_64ManagedRegister::FromCpuRegister(RAX);
  }
}

ManagedRegister X86_64ManagedRuntimeCallingConvention::ReturnRegister() {
  return ReturnRegisterForShorty(GetShorty(), false);
}

ManagedRegister X86_64JniCallingConvention::ReturnRegister() {
  return ReturnRegisterForShorty(GetShorty(), true);
}

ManagedRegister X86_64JniCallingConvention::IntReturnRegister() {
  return X86_64ManagedRegister::FromCpuRegister(RAX);
}

// Managed runtime calling convention

ManagedRegister X86_64ManagedRuntimeCallingConvention::MethodRegister() {
  return X86_64ManagedRegister::FromCpuRegister(RDI);
}

bool X86_64ManagedRuntimeCallingConvention::IsCurrentParamInRegister() {
  return !IsCurrentParamOnStack();
}

bool X86_64ManagedRuntimeCallingConvention::IsCurrentParamOnStack() {
  // We assume all parameters are on stack, args coming via registers are spilled as entry_spills
  return true;
}

ManagedRegister X86_64ManagedRuntimeCallingConvention::CurrentParamRegister() {
  ManagedRegister res = ManagedRegister::NoRegister();
  if (!IsCurrentParamAFloatOrDouble()) {
    switch (itr_args_ - itr_float_and_doubles_) {
    case 0: res = X86_64ManagedRegister::FromCpuRegister(RSI); break;
    case 1: res = X86_64ManagedRegister::FromCpuRegister(RDX); break;
    case 2: res = X86_64ManagedRegister::FromCpuRegister(RCX); break;
    case 3: res = X86_64ManagedRegister::FromCpuRegister(R8); break;
    case 4: res = X86_64ManagedRegister::FromCpuRegister(R9); break;
    }
  } else if (itr_float_and_doubles_ < kMaxFloatOrDoubleRegisterArguments) {
    // First eight float parameters are passed via XMM0..XMM7
    res = X86_64ManagedRegister::FromXmmRegister(
                                 static_cast<FloatRegister>(XMM0 + itr_float_and_doubles_));
  }
  return res;
}

FrameOffset X86_64ManagedRuntimeCallingConvention::CurrentParamStackOffset() {
  CHECK(IsCurrentParamOnStack());
  return FrameOffset(displacement_.Int32Value() +  // displacement
                     static_cast<size_t>(kX86_64PointerSize) +  // Method ref
                     itr_slots_ * sizeof(uint32_t));  // offset into in args
}

const ManagedRegisterEntrySpills& X86_64ManagedRuntimeCallingConvention::EntrySpills() {
  // We spill the argument registers on X86 to free them up for scratch use, we then assume
  // all arguments are on the stack.
  if (entry_spills_.size() == 0) {
    ResetIterator(FrameOffset(0));
    while (HasNext()) {
      ManagedRegister in_reg = CurrentParamRegister();
      if (!in_reg.IsNoRegister()) {
        int32_t size = IsParamALongOrDouble(itr_args_) ? 8 : 4;
        int32_t spill_offset = CurrentParamStackOffset().Uint32Value();
        ManagedRegisterSpill spill(in_reg, size, spill_offset);
        entry_spills_.push_back(spill);
      }
      Next();
    }
  }
  return entry_spills_;
}

// JNI calling convention

X86_64JniCallingConvention::X86_64JniCallingConvention(bool is_static,
                                                       bool is_synchronized,
                                                       bool is_critical_native,
                                                       const char* shorty)
    : JniCallingConvention(is_static,
                           is_synchronized,
                           is_critical_native,
                           shorty,
                           kX86_64PointerSize) {
}

uint32_t X86_64JniCallingConvention::CoreSpillMask() const {
  return is_critical_native_ ? 0u : kCoreCalleeSpillMask;
}

uint32_t X86_64JniCallingConvention::FpSpillMask() const {
  return is_critical_native_ ? 0u : kFpCalleeSpillMask;
}

size_t X86_64JniCallingConvention::FrameSize() const {
  if (is_critical_native_) {
    CHECK(!SpillsMethod());
    CHECK(!HasLocalReferenceSegmentState());
    CHECK(!HasHandleScope());
    CHECK(!SpillsReturnValue());
    return 0u;  // There is no managed frame for @CriticalNative.
  }

  // Method*, PC return address and callee save area size, local reference segment state
  CHECK(SpillsMethod());
  const size_t method_ptr_size = static_cast<size_t>(kX86_64PointerSize);
  const size_t pc_return_addr_size = kFramePointerSize;
  const size_t callee_save_area_size = CalleeSaveRegisters().size() * kFramePointerSize;
  size_t total_size = method_ptr_size + pc_return_addr_size + callee_save_area_size;

  CHECK(HasLocalReferenceSegmentState());
  total_size += kFramePointerSize;

  CHECK(HasHandleScope());
  total_size += HandleScope::SizeOf(kX86_64PointerSize, ReferenceCount());

  // Plus return value spill area size
  CHECK(SpillsReturnValue());
  total_size += SizeOfReturnValue();

  return RoundUp(total_size, kStackAlignment);
}

size_t X86_64JniCallingConvention::OutArgSize() const {
  // Count param args, including JNIEnv* and jclass*.
  size_t all_args = NumberOfExtraArgumentsForJni() + NumArgs();
  size_t num_fp_args = NumFloatOrDoubleArgs();
  DCHECK_GE(all_args, num_fp_args);
  size_t num_non_fp_args = all_args - num_fp_args;
  // Account for FP arguments passed through Xmm0..Xmm7.
  size_t num_stack_fp_args =
      num_fp_args - std::min(kMaxFloatOrDoubleRegisterArguments, num_fp_args);
  // Account for other (integer) arguments passed through GPR (RDI, RSI, RDX, RCX, R8, R9).
  size_t num_stack_non_fp_args =
      num_non_fp_args - std::min(kMaxIntLikeRegisterArguments, num_non_fp_args);
  // The size of outgoing arguments.
  static_assert(kFramePointerSize == kMmxSpillSize);
  size_t size = (num_stack_fp_args + num_stack_non_fp_args) * kFramePointerSize;

  if (UNLIKELY(IsCriticalNative())) {
    // We always need to spill xmm12-xmm15 as they are managed callee-saves
    // but not native callee-saves.
    static_assert((kCoreCalleeSpillMask & ~kNativeCoreCalleeSpillMask) == 0u);
    static_assert((kFpCalleeSpillMask & ~kNativeFpCalleeSpillMask) != 0u);
    static_assert(
        kAlwaysSpilledMmxRegisters == POPCOUNT(kFpCalleeSpillMask & ~kNativeFpCalleeSpillMask));
    size += kAlwaysSpilledMmxRegisters * kMmxSpillSize;
    // Add return address size for @CriticalNative
    // For normal native the return PC is part of the managed stack frame instead of out args.
    size += kFramePointerSize;
  }

  size_t out_args_size = RoundUp(size, kNativeStackAlignment);
  if (UNLIKELY(IsCriticalNative())) {
    DCHECK_EQ(out_args_size, GetCriticalNativeOutArgsSize(GetShorty(), NumArgs() + 1u));
  }
  return out_args_size;
}

ArrayRef<const ManagedRegister> X86_64JniCallingConvention::CalleeSaveRegisters() const {
  if (UNLIKELY(IsCriticalNative())) {
    DCHECK(!UseTailCall());
    static_assert(std::size(kCalleeSaveRegisters) > std::size(kNativeCalleeSaveRegisters));
    // TODO: Change to static_assert; std::equal should be constexpr since C++20.
    DCHECK(std::equal(kCalleeSaveRegisters,
                      kCalleeSaveRegisters + std::size(kNativeCalleeSaveRegisters),
                      kNativeCalleeSaveRegisters,
                      [](ManagedRegister lhs, ManagedRegister rhs) { return lhs.Equals(rhs); }));
    return ArrayRef<const ManagedRegister>(kCalleeSaveRegisters).SubArray(
        /*pos=*/ std::size(kNativeCalleeSaveRegisters));
  } else {
    return ArrayRef<const ManagedRegister>(kCalleeSaveRegisters);
  }
}

bool X86_64JniCallingConvention::IsCurrentParamInRegister() {
  return !IsCurrentParamOnStack();
}

bool X86_64JniCallingConvention::IsCurrentParamOnStack() {
  return CurrentParamRegister().IsNoRegister();
}

ManagedRegister X86_64JniCallingConvention::CurrentParamRegister() {
  ManagedRegister res = ManagedRegister::NoRegister();
  if (!IsCurrentParamAFloatOrDouble()) {
    switch (itr_args_ - itr_float_and_doubles_) {
    case 0: res = X86_64ManagedRegister::FromCpuRegister(RDI); break;
    case 1: res = X86_64ManagedRegister::FromCpuRegister(RSI); break;
    case 2: res = X86_64ManagedRegister::FromCpuRegister(RDX); break;
    case 3: res = X86_64ManagedRegister::FromCpuRegister(RCX); break;
    case 4: res = X86_64ManagedRegister::FromCpuRegister(R8); break;
    case 5: res = X86_64ManagedRegister::FromCpuRegister(R9); break;
    static_assert(5u == kMaxIntLikeRegisterArguments - 1, "Missing case statement(s)");
    }
  } else if (itr_float_and_doubles_ < kMaxFloatOrDoubleRegisterArguments) {
    // First eight float parameters are passed via XMM0..XMM7
    res = X86_64ManagedRegister::FromXmmRegister(
                                 static_cast<FloatRegister>(XMM0 + itr_float_and_doubles_));
  }
  return res;
}

FrameOffset X86_64JniCallingConvention::CurrentParamStackOffset() {
  CHECK(IsCurrentParamOnStack());
  size_t args_on_stack = itr_args_
      - std::min(kMaxFloatOrDoubleRegisterArguments,
                 static_cast<size_t>(itr_float_and_doubles_))
          // Float arguments passed through Xmm0..Xmm7
      - std::min(kMaxIntLikeRegisterArguments,
                 static_cast<size_t>(itr_args_ - itr_float_and_doubles_));
          // Integer arguments passed through GPR
  size_t offset = displacement_.Int32Value() - OutArgSize() + (args_on_stack * kFramePointerSize);
  CHECK_LT(offset, OutArgSize());
  return FrameOffset(offset);
}

ManagedRegister X86_64JniCallingConvention::HiddenArgumentRegister() const {
  CHECK(IsCriticalNative());
  // R11 is neither managed callee-save, nor argument register, nor scratch register.
  DCHECK(std::none_of(kCalleeSaveRegisters,
                      kCalleeSaveRegisters + std::size(kCalleeSaveRegisters),
                      [](ManagedRegister callee_save) constexpr {
                        return callee_save.Equals(X86_64ManagedRegister::FromCpuRegister(R11));
                      }));
  DCHECK(!InterproceduralScratchRegister().Equals(X86_64ManagedRegister::FromCpuRegister(R11)));
  return X86_64ManagedRegister::FromCpuRegister(R11);
}

// Whether to use tail call (used only for @CriticalNative).
bool X86_64JniCallingConvention::UseTailCall() const {
  CHECK(IsCriticalNative());
  // We always need to spill xmm12-xmm15 as they are managed callee-saves
  // but not native callee-saves, so we can never use a tail call.
  return false;
}

}  // namespace x86_64
}  // namespace art
