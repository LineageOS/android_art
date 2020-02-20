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

#include "calling_convention_x86.h"

#include <android-base/logging.h>

#include "arch/instruction_set.h"
#include "arch/x86/jni_frame_x86.h"
#include "handle_scope-inl.h"
#include "utils/x86/managed_register_x86.h"

namespace art {
namespace x86 {

static_assert(kX86PointerSize == PointerSize::k32, "Unexpected x86 pointer size");

static constexpr ManagedRegister kCalleeSaveRegisters[] = {
    // Core registers.
    X86ManagedRegister::FromCpuRegister(EBP),
    X86ManagedRegister::FromCpuRegister(ESI),
    X86ManagedRegister::FromCpuRegister(EDI),
    // No hard float callee saves.
};

template <size_t size>
static constexpr uint32_t CalculateCoreCalleeSpillMask(
    const ManagedRegister (&callee_saves)[size]) {
  // The spilled PC gets a special marker.
  uint32_t result = 1 << kNumberOfCpuRegisters;
  for (auto&& r : callee_saves) {
    if (r.AsX86().IsCpuRegister()) {
      result |= (1 << r.AsX86().AsCpuRegister());
    }
  }
  return result;
}

static constexpr uint32_t kCoreCalleeSpillMask = CalculateCoreCalleeSpillMask(kCalleeSaveRegisters);
static constexpr uint32_t kFpCalleeSpillMask = 0u;

static constexpr ManagedRegister kNativeCalleeSaveRegisters[] = {
    // Core registers.
    X86ManagedRegister::FromCpuRegister(EBX),
    X86ManagedRegister::FromCpuRegister(EBP),
    X86ManagedRegister::FromCpuRegister(ESI),
    X86ManagedRegister::FromCpuRegister(EDI),
    // No hard float callee saves.
};

static constexpr uint32_t kNativeCoreCalleeSpillMask =
    CalculateCoreCalleeSpillMask(kNativeCalleeSaveRegisters);
static constexpr uint32_t kNativeFpCalleeSpillMask = 0u;

// Calling convention

ManagedRegister X86ManagedRuntimeCallingConvention::InterproceduralScratchRegister() const {
  return X86ManagedRegister::FromCpuRegister(ECX);
}

ManagedRegister X86JniCallingConvention::InterproceduralScratchRegister() const {
  return X86ManagedRegister::FromCpuRegister(ECX);
}

ManagedRegister X86JniCallingConvention::ReturnScratchRegister() const {
  return ManagedRegister::NoRegister();  // No free regs, so assembler uses push/pop
}

static ManagedRegister ReturnRegisterForShorty(const char* shorty, bool jni) {
  if (shorty[0] == 'F' || shorty[0] == 'D') {
    if (jni) {
      return X86ManagedRegister::FromX87Register(ST0);
    } else {
      return X86ManagedRegister::FromXmmRegister(XMM0);
    }
  } else if (shorty[0] == 'J') {
    return X86ManagedRegister::FromRegisterPair(EAX_EDX);
  } else if (shorty[0] == 'V') {
    return ManagedRegister::NoRegister();
  } else {
    return X86ManagedRegister::FromCpuRegister(EAX);
  }
}

ManagedRegister X86ManagedRuntimeCallingConvention::ReturnRegister() {
  return ReturnRegisterForShorty(GetShorty(), false);
}

ManagedRegister X86JniCallingConvention::ReturnRegister() {
  return ReturnRegisterForShorty(GetShorty(), true);
}

ManagedRegister X86JniCallingConvention::IntReturnRegister() {
  return X86ManagedRegister::FromCpuRegister(EAX);
}

// Managed runtime calling convention

ManagedRegister X86ManagedRuntimeCallingConvention::MethodRegister() {
  return X86ManagedRegister::FromCpuRegister(EAX);
}

bool X86ManagedRuntimeCallingConvention::IsCurrentParamInRegister() {
  return false;  // Everything is passed by stack
}

bool X86ManagedRuntimeCallingConvention::IsCurrentParamOnStack() {
  // We assume all parameters are on stack, args coming via registers are spilled as entry_spills.
  return true;
}

ManagedRegister X86ManagedRuntimeCallingConvention::CurrentParamRegister() {
  ManagedRegister res = ManagedRegister::NoRegister();
  if (!IsCurrentParamAFloatOrDouble()) {
    switch (gpr_arg_count_) {
      case 0:
        res = X86ManagedRegister::FromCpuRegister(ECX);
        break;
      case 1:
        res = X86ManagedRegister::FromCpuRegister(EDX);
        break;
      case 2:
        // Don't split a long between the last register and the stack.
        if (IsCurrentParamALong()) {
          return ManagedRegister::NoRegister();
        }
        res = X86ManagedRegister::FromCpuRegister(EBX);
        break;
    }
  } else if (itr_float_and_doubles_ < 4) {
    // First four float parameters are passed via XMM0..XMM3
    res = X86ManagedRegister::FromXmmRegister(
                                 static_cast<XmmRegister>(XMM0 + itr_float_and_doubles_));
  }
  return res;
}

ManagedRegister X86ManagedRuntimeCallingConvention::CurrentParamHighLongRegister() {
  ManagedRegister res = ManagedRegister::NoRegister();
  DCHECK(IsCurrentParamALong());
  switch (gpr_arg_count_) {
    case 0: res = X86ManagedRegister::FromCpuRegister(EDX); break;
    case 1: res = X86ManagedRegister::FromCpuRegister(EBX); break;
  }
  return res;
}

FrameOffset X86ManagedRuntimeCallingConvention::CurrentParamStackOffset() {
  return FrameOffset(displacement_.Int32Value() +   // displacement
                     kFramePointerSize +                 // Method*
                     (itr_slots_ * kFramePointerSize));  // offset into in args
}

const ManagedRegisterEntrySpills& X86ManagedRuntimeCallingConvention::EntrySpills() {
  // We spill the argument registers on X86 to free them up for scratch use, we then assume
  // all arguments are on the stack.
  if (entry_spills_.size() == 0) {
    ResetIterator(FrameOffset(0));
    while (HasNext()) {
      ManagedRegister in_reg = CurrentParamRegister();
      bool is_long = IsCurrentParamALong();
      if (!in_reg.IsNoRegister()) {
        int32_t size = IsParamADouble(itr_args_) ? 8 : 4;
        int32_t spill_offset = CurrentParamStackOffset().Uint32Value();
        ManagedRegisterSpill spill(in_reg, size, spill_offset);
        entry_spills_.push_back(spill);
        if (is_long) {
          // special case, as we need a second register here.
          in_reg = CurrentParamHighLongRegister();
          DCHECK(!in_reg.IsNoRegister());
          // We have to spill the second half of the long.
          ManagedRegisterSpill spill2(in_reg, size, spill_offset + 4);
          entry_spills_.push_back(spill2);
        }

        // Keep track of the number of GPRs allocated.
        if (!IsCurrentParamAFloatOrDouble()) {
          if (is_long) {
            // Long was allocated in 2 registers.
            gpr_arg_count_ += 2;
          } else {
            gpr_arg_count_++;
          }
        }
      } else if (is_long) {
        // We need to skip the unused last register, which is empty.
        // If we are already out of registers, this is harmless.
        gpr_arg_count_ += 2;
      }
      Next();
    }
  }
  return entry_spills_;
}

// JNI calling convention

X86JniCallingConvention::X86JniCallingConvention(bool is_static,
                                                 bool is_synchronized,
                                                 bool is_critical_native,
                                                 const char* shorty)
    : JniCallingConvention(is_static,
                           is_synchronized,
                           is_critical_native,
                           shorty,
                           kX86PointerSize) {
}

uint32_t X86JniCallingConvention::CoreSpillMask() const {
  return is_critical_native_ ? 0u : kCoreCalleeSpillMask;
}

uint32_t X86JniCallingConvention::FpSpillMask() const {
  return is_critical_native_ ? 0u : kFpCalleeSpillMask;
}

size_t X86JniCallingConvention::FrameSize() const {
  if (is_critical_native_) {
    CHECK(!SpillsMethod());
    CHECK(!HasLocalReferenceSegmentState());
    CHECK(!HasHandleScope());
    CHECK(!SpillsReturnValue());
    return 0u;  // There is no managed frame for @CriticalNative.
  }

  // Method*, PC return address and callee save area size, local reference segment state
  CHECK(SpillsMethod());
  const size_t method_ptr_size = static_cast<size_t>(kX86PointerSize);
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

size_t X86JniCallingConvention::OutArgSize() const {
  // Count param args, including JNIEnv* and jclass*; count 8-byte args twice.
  size_t all_args = NumberOfExtraArgumentsForJni() + NumArgs() + NumLongOrDoubleArgs();
  // The size of outgoiong arguments.
  size_t size = all_args * kFramePointerSize;

  // @CriticalNative can use tail call as all managed callee saves are preserved by AAPCS.
  static_assert((kCoreCalleeSpillMask & ~kNativeCoreCalleeSpillMask) == 0u);
  static_assert((kFpCalleeSpillMask & ~kNativeFpCalleeSpillMask) == 0u);

  if (UNLIKELY(IsCriticalNative())) {
    // Add return address size for @CriticalNative.
    // For normal native the return PC is part of the managed stack frame instead of out args.
    size += kFramePointerSize;
    // For @CriticalNative, we can make a tail call if there are no stack args
    // and the return type is not FP type (needs moving from ST0 to MMX0) and
    // we do not need to extend the result.
    bool return_type_ok = GetShorty()[0] == 'I' || GetShorty()[0] == 'J' || GetShorty()[0] == 'V';
    DCHECK_EQ(
        return_type_ok,
        GetShorty()[0] != 'F' && GetShorty()[0] != 'D' && !RequiresSmallResultTypeExtension());
    if (return_type_ok && size == kFramePointerSize) {
      // Note: This is not aligned to kNativeStackAlignment but that's OK for tail call.
      static_assert(kFramePointerSize < kNativeStackAlignment);
      DCHECK_EQ(kFramePointerSize, GetCriticalNativeOutArgsSize(GetShorty(), NumArgs() + 1u));
      return kFramePointerSize;
    }
  }

  size_t out_args_size = RoundUp(size, kNativeStackAlignment);
  if (UNLIKELY(IsCriticalNative())) {
    DCHECK_EQ(out_args_size, GetCriticalNativeOutArgsSize(GetShorty(), NumArgs() + 1u));
  }
  return out_args_size;
}

ArrayRef<const ManagedRegister> X86JniCallingConvention::CalleeSaveRegisters() const {
  if (UNLIKELY(IsCriticalNative())) {
    // Do not spill anything, whether tail call or not (return PC is already on the stack).
    return ArrayRef<const ManagedRegister>();
  } else {
    return ArrayRef<const ManagedRegister>(kCalleeSaveRegisters);
  }
}

bool X86JniCallingConvention::IsCurrentParamInRegister() {
  return false;  // Everything is passed by stack.
}

bool X86JniCallingConvention::IsCurrentParamOnStack() {
  return true;  // Everything is passed by stack.
}

ManagedRegister X86JniCallingConvention::CurrentParamRegister() {
  LOG(FATAL) << "Should not reach here";
  UNREACHABLE();
}

FrameOffset X86JniCallingConvention::CurrentParamStackOffset() {
  return FrameOffset(displacement_.Int32Value() - OutArgSize() + (itr_slots_ * kFramePointerSize));
}

ManagedRegister X86JniCallingConvention::HiddenArgumentRegister() const {
  CHECK(IsCriticalNative());
  // EAX is neither managed callee-save, nor argument register, nor scratch register.
  DCHECK(std::none_of(kCalleeSaveRegisters,
                      kCalleeSaveRegisters + std::size(kCalleeSaveRegisters),
                      [](ManagedRegister callee_save) constexpr {
                        return callee_save.Equals(X86ManagedRegister::FromCpuRegister(EAX));
                      }));
  DCHECK(!InterproceduralScratchRegister().Equals(X86ManagedRegister::FromCpuRegister(EAX)));
  return X86ManagedRegister::FromCpuRegister(EAX);
}

bool X86JniCallingConvention::UseTailCall() const {
  CHECK(IsCriticalNative());
  return OutArgSize() == kFramePointerSize;
}

}  // namespace x86
}  // namespace art
