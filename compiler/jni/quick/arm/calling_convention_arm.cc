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

#include "calling_convention_arm.h"

#include <android-base/logging.h>

#include "arch/arm/jni_frame_arm.h"
#include "arch/instruction_set.h"
#include "base/macros.h"
#include "handle_scope-inl.h"
#include "utils/arm/managed_register_arm.h"

namespace art {
namespace arm {

//
// JNI calling convention constants.
//

// List of parameters passed via registers for JNI.
// JNI uses soft-float, so there is only a GPR list.
static const Register kJniArgumentRegisters[] = {
  R0, R1, R2, R3
};

static_assert(kJniArgumentRegisterCount == arraysize(kJniArgumentRegisters));

//
// Managed calling convention constants.
//

// Used by hard float. (General purpose registers.)
static const Register kHFCoreArgumentRegisters[] = {
  R0, R1, R2, R3
};
static constexpr size_t kHFCoreArgumentRegistersCount = arraysize(kHFCoreArgumentRegisters);

// (VFP single-precision registers.)
static const SRegister kHFSArgumentRegisters[] = {
  S0, S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, S12, S13, S14, S15
};
static constexpr size_t kHFSArgumentRegistersCount = arraysize(kHFSArgumentRegisters);

// (VFP double-precision registers.)
static const DRegister kHFDArgumentRegisters[] = {
  D0, D1, D2, D3, D4, D5, D6, D7
};
static constexpr size_t kHFDArgumentRegistersCount = arraysize(kHFDArgumentRegisters);

static_assert(kHFDArgumentRegistersCount * 2 == kHFSArgumentRegistersCount,
    "ks d argument registers mismatch");

//
// Shared managed+JNI calling convention constants.
//

static constexpr ManagedRegister kCalleeSaveRegisters[] = {
    // Core registers.
    ArmManagedRegister::FromCoreRegister(R5),
    ArmManagedRegister::FromCoreRegister(R6),
    ArmManagedRegister::FromCoreRegister(R7),
    ArmManagedRegister::FromCoreRegister(R8),
    ArmManagedRegister::FromCoreRegister(R10),
    ArmManagedRegister::FromCoreRegister(R11),
    ArmManagedRegister::FromCoreRegister(LR),
    // Hard float registers.
    ArmManagedRegister::FromSRegister(S16),
    ArmManagedRegister::FromSRegister(S17),
    ArmManagedRegister::FromSRegister(S18),
    ArmManagedRegister::FromSRegister(S19),
    ArmManagedRegister::FromSRegister(S20),
    ArmManagedRegister::FromSRegister(S21),
    ArmManagedRegister::FromSRegister(S22),
    ArmManagedRegister::FromSRegister(S23),
    ArmManagedRegister::FromSRegister(S24),
    ArmManagedRegister::FromSRegister(S25),
    ArmManagedRegister::FromSRegister(S26),
    ArmManagedRegister::FromSRegister(S27),
    ArmManagedRegister::FromSRegister(S28),
    ArmManagedRegister::FromSRegister(S29),
    ArmManagedRegister::FromSRegister(S30),
    ArmManagedRegister::FromSRegister(S31)
};

template <size_t size>
static constexpr uint32_t CalculateCoreCalleeSpillMask(
    const ManagedRegister (&callee_saves)[size]) {
  // LR is a special callee save which is not reported by CalleeSaveRegisters().
  uint32_t result = 0u;
  for (auto&& r : callee_saves) {
    if (r.AsArm().IsCoreRegister()) {
      result |= (1u << r.AsArm().AsCoreRegister());
    }
  }
  return result;
}

template <size_t size>
static constexpr uint32_t CalculateFpCalleeSpillMask(const ManagedRegister (&callee_saves)[size]) {
  uint32_t result = 0u;
  for (auto&& r : callee_saves) {
    if (r.AsArm().IsSRegister()) {
      result |= (1u << r.AsArm().AsSRegister());
    }
  }
  return result;
}

static constexpr uint32_t kCoreCalleeSpillMask = CalculateCoreCalleeSpillMask(kCalleeSaveRegisters);
static constexpr uint32_t kFpCalleeSpillMask = CalculateFpCalleeSpillMask(kCalleeSaveRegisters);

static constexpr ManagedRegister kAapcsCalleeSaveRegisters[] = {
    // Core registers.
    ArmManagedRegister::FromCoreRegister(R4),
    ArmManagedRegister::FromCoreRegister(R5),
    ArmManagedRegister::FromCoreRegister(R6),
    ArmManagedRegister::FromCoreRegister(R7),
    ArmManagedRegister::FromCoreRegister(R8),
    ArmManagedRegister::FromCoreRegister(R9),  // The platform register is callee-save on Android.
    ArmManagedRegister::FromCoreRegister(R10),
    ArmManagedRegister::FromCoreRegister(R11),
    ArmManagedRegister::FromCoreRegister(LR),
    // Hard float registers.
    ArmManagedRegister::FromSRegister(S16),
    ArmManagedRegister::FromSRegister(S17),
    ArmManagedRegister::FromSRegister(S18),
    ArmManagedRegister::FromSRegister(S19),
    ArmManagedRegister::FromSRegister(S20),
    ArmManagedRegister::FromSRegister(S21),
    ArmManagedRegister::FromSRegister(S22),
    ArmManagedRegister::FromSRegister(S23),
    ArmManagedRegister::FromSRegister(S24),
    ArmManagedRegister::FromSRegister(S25),
    ArmManagedRegister::FromSRegister(S26),
    ArmManagedRegister::FromSRegister(S27),
    ArmManagedRegister::FromSRegister(S28),
    ArmManagedRegister::FromSRegister(S29),
    ArmManagedRegister::FromSRegister(S30),
    ArmManagedRegister::FromSRegister(S31)
};

static constexpr uint32_t kAapcsCoreCalleeSpillMask =
    CalculateCoreCalleeSpillMask(kAapcsCalleeSaveRegisters);
static constexpr uint32_t kAapcsFpCalleeSpillMask =
    CalculateFpCalleeSpillMask(kAapcsCalleeSaveRegisters);

// Calling convention

ManagedRegister ArmManagedRuntimeCallingConvention::ReturnRegister() {
  switch (GetShorty()[0]) {
    case 'V':
      return ArmManagedRegister::NoRegister();
    case 'D':
      return ArmManagedRegister::FromDRegister(D0);
    case 'F':
      return ArmManagedRegister::FromSRegister(S0);
    case 'J':
      return ArmManagedRegister::FromRegisterPair(R0_R1);
    default:
      return ArmManagedRegister::FromCoreRegister(R0);
  }
}

ManagedRegister ArmJniCallingConvention::ReturnRegister() {
  switch (GetShorty()[0]) {
  case 'V':
    return ArmManagedRegister::NoRegister();
  case 'D':
  case 'J':
    return ArmManagedRegister::FromRegisterPair(R0_R1);
  default:
    return ArmManagedRegister::FromCoreRegister(R0);
  }
}

ManagedRegister ArmJniCallingConvention::IntReturnRegister() {
  return ArmManagedRegister::FromCoreRegister(R0);
}

// Managed runtime calling convention

ManagedRegister ArmManagedRuntimeCallingConvention::MethodRegister() {
  return ArmManagedRegister::FromCoreRegister(R0);
}

void ArmManagedRuntimeCallingConvention::ResetIterator(FrameOffset displacement) {
  ManagedRuntimeCallingConvention::ResetIterator(displacement);
  gpr_index_ = 1u;  // Skip r0 for ArtMethod*
  float_index_ = 0u;
  double_index_ = 0u;
}

void ArmManagedRuntimeCallingConvention::Next() {
  if (IsCurrentParamAFloatOrDouble()) {
    if (float_index_ % 2 == 0) {
      // The register for the current float is the same as the first register for double.
      DCHECK_EQ(float_index_, double_index_ * 2u);
    } else {
      // There is a space for an extra float before space for a double.
      DCHECK_LT(float_index_, double_index_ * 2u);
    }
    if (IsCurrentParamADouble()) {
      double_index_ += 1u;
      if (float_index_ % 2 == 0) {
        float_index_ = double_index_ * 2u;
      }
    } else {
      if (float_index_ % 2 == 0) {
        float_index_ += 1u;
        double_index_ += 1u;  // Leaves space for one more float before the next double.
      } else {
        float_index_ = double_index_ * 2u;
      }
    }
  } else {  // Not a float/double.
    if (IsCurrentParamALong()) {
      // Note that the alignment to even register is done lazily.
      gpr_index_ = RoundUp(gpr_index_, 2u) + 2u;
    } else {
      gpr_index_ += 1u;
    }
  }
  ManagedRuntimeCallingConvention::Next();
}

bool ArmManagedRuntimeCallingConvention::IsCurrentParamInRegister() {
  if (IsCurrentParamAFloatOrDouble()) {
    if (IsCurrentParamADouble()) {
      return double_index_ < kHFDArgumentRegistersCount;
    } else {
      return float_index_ < kHFSArgumentRegistersCount;
    }
  } else {
    if (IsCurrentParamALong()) {
      // Round up to even register and do not split a long between the last register and the stack.
      return RoundUp(gpr_index_, 2u) + 1u < kHFCoreArgumentRegistersCount;
    } else {
      return gpr_index_ < kHFCoreArgumentRegistersCount;
    }
  }
}

bool ArmManagedRuntimeCallingConvention::IsCurrentParamOnStack() {
  return !IsCurrentParamInRegister();
}

ManagedRegister ArmManagedRuntimeCallingConvention::CurrentParamRegister() {
  DCHECK(IsCurrentParamInRegister());
  if (IsCurrentParamAFloatOrDouble()) {
    if (IsCurrentParamADouble()) {
      return ArmManagedRegister::FromDRegister(kHFDArgumentRegisters[double_index_]);
    } else {
      return ArmManagedRegister::FromSRegister(kHFSArgumentRegisters[float_index_]);
    }
  } else {
    if (IsCurrentParamALong()) {
      // Currently the only register pair for a long parameter is r2-r3.
      // Note that the alignment to even register is done lazily.
      CHECK_EQ(RoundUp(gpr_index_, 2u), 2u);
      return ArmManagedRegister::FromRegisterPair(R2_R3);
    } else {
      return ArmManagedRegister::FromCoreRegister(kHFCoreArgumentRegisters[gpr_index_]);
    }
  }
}

FrameOffset ArmManagedRuntimeCallingConvention::CurrentParamStackOffset() {
  return FrameOffset(displacement_.Int32Value() +        // displacement
                     kFramePointerSize +                 // Method*
                     (itr_slots_ * kFramePointerSize));  // offset into in args
}

// JNI calling convention

ArmJniCallingConvention::ArmJniCallingConvention(bool is_static,
                                                 bool is_synchronized,
                                                 bool is_critical_native,
                                                 const char* shorty)
    : JniCallingConvention(is_static,
                           is_synchronized,
                           is_critical_native,
                           shorty,
                           kArmPointerSize) {
  // AAPCS 4.1 specifies fundamental alignments for each type. All of our stack arguments are
  // usually 4-byte aligned, however longs and doubles must be 8 bytes aligned. Add padding to
  // maintain 8-byte alignment invariant.
  //
  // Compute padding to ensure longs and doubles are not split in AAPCS.
  size_t shift = 0;

  size_t cur_arg, cur_reg;
  if (LIKELY(HasExtraArgumentsForJni())) {
    // Ignore the 'this' jobject or jclass for static methods and the JNIEnv.
    // We start at the aligned register r2.
    //
    // Ignore the first 2 parameters because they are guaranteed to be aligned.
    cur_arg = NumImplicitArgs();  // skip the "this" arg.
    cur_reg = 2;  // skip {r0=JNIEnv, r1=jobject} / {r0=JNIEnv, r1=jclass} parameters (start at r2).
  } else {
    // Check every parameter.
    cur_arg = 0;
    cur_reg = 0;
  }

  // TODO: Maybe should just use IsCurrentParamALongOrDouble instead to be cleaner?
  // (this just seems like an unnecessary micro-optimization).

  // Shift across a logical register mapping that looks like:
  //
  //   | r0 | r1 | r2 | r3 | SP | SP+4| SP+8 | SP+12 | ... | SP+n | SP+n+4 |
  //
  //   (where SP is some arbitrary stack pointer that our 0th stack arg would go into).
  //
  // Any time there would normally be a long/double in an odd logical register,
  // we have to push out the rest of the mappings by 4 bytes to maintain an 8-byte alignment.
  //
  // This works for both physical register pairs {r0, r1}, {r2, r3} and for when
  // the value is on the stack.
  //
  // For example:
  // (a) long would normally go into r1, but we shift it into r2
  //  | INT | (PAD) | LONG      |
  //  | r0  |  r1   |  r2  | r3 |
  //
  // (b) long would normally go into r3, but we shift it into SP
  //  | INT | INT | INT | (PAD) | LONG     |
  //  | r0  |  r1 |  r2 |  r3   | SP+4 SP+8|
  //
  // where INT is any <=4 byte arg, and LONG is any 8-byte arg.
  for (; cur_arg < NumArgs(); cur_arg++) {
    if (IsParamALongOrDouble(cur_arg)) {
      if ((cur_reg & 1) != 0) {  // check that it's in a logical contiguous register pair
        shift += 4;
        cur_reg++;  // additional bump to ensure alignment
      }
      cur_reg += 2;  // bump the iterator twice for every long argument
    } else {
      cur_reg++;  // bump the iterator for every non-long argument
    }
  }

  if (cur_reg <= kJniArgumentRegisterCount) {
    // As a special case when, as a result of shifting (or not) there are no arguments on the stack,
    // we actually have 0 stack padding.
    //
    // For example with @CriticalNative and:
    // (int, long) -> shifts the long but doesn't need to pad the stack
    //
    //          shift
    //           \/
    //  | INT | (PAD) | LONG      | (EMPTY) ...
    //  | r0  |  r1   |  r2  | r3 |   SP    ...
    //                                /\
    //                          no stack padding
    padding_ = 0;
  } else {
    padding_ = shift;
  }

  // TODO: add some new JNI tests for @CriticalNative that introduced new edge cases
  // (a) Using r0,r1 pair = f(long,...)
  // (b) Shifting r1 long into r2,r3 pair = f(int, long, int, ...);
  // (c) Shifting but not introducing a stack padding = f(int, long);
}

uint32_t ArmJniCallingConvention::CoreSpillMask() const {
  // Compute spill mask to agree with callee saves initialized in the constructor
  return is_critical_native_ ? 0u : kCoreCalleeSpillMask;
}

uint32_t ArmJniCallingConvention::FpSpillMask() const {
  return is_critical_native_ ? 0u : kFpCalleeSpillMask;
}

ManagedRegister ArmJniCallingConvention::ReturnScratchRegister() const {
  return ArmManagedRegister::FromCoreRegister(R2);
}

size_t ArmJniCallingConvention::FrameSize() const {
  if (UNLIKELY(is_critical_native_)) {
    CHECK(!SpillsMethod());
    CHECK(!HasLocalReferenceSegmentState());
    CHECK(!HasHandleScope());
    CHECK(!SpillsReturnValue());
    return 0u;  // There is no managed frame for @CriticalNative.
  }

  // Method*, callee save area size, local reference segment state
  CHECK(SpillsMethod());
  const size_t method_ptr_size = static_cast<size_t>(kArmPointerSize);
  const size_t callee_save_area_size = CalleeSaveRegisters().size() * kFramePointerSize;
  size_t total_size = method_ptr_size + callee_save_area_size;

  CHECK(HasLocalReferenceSegmentState());
  // local reference segment state
  total_size += kFramePointerSize;
  // TODO: Probably better to use sizeof(IRTSegmentState) here...

  CHECK(HasHandleScope());
  total_size += HandleScope::SizeOf(kArmPointerSize, ReferenceCount());

  // Plus return value spill area size
  CHECK(SpillsReturnValue());
  total_size += SizeOfReturnValue();

  return RoundUp(total_size, kStackAlignment);
}

size_t ArmJniCallingConvention::OutFrameSize() const {
  // Count param args, including JNIEnv* and jclass*; count 8-byte args twice.
  size_t all_args = NumberOfExtraArgumentsForJni() + NumArgs() + NumLongOrDoubleArgs();
  // Account for arguments passed through r0-r3. (No FP args, AAPCS32 is soft-float.)
  size_t stack_args = all_args - std::min(kJniArgumentRegisterCount, all_args);
  // The size of outgoing arguments.
  size_t size = stack_args * kFramePointerSize + padding_;

  // @CriticalNative can use tail call as all managed callee saves are preserved by AAPCS.
  static_assert((kCoreCalleeSpillMask & ~kAapcsCoreCalleeSpillMask) == 0u);
  static_assert((kFpCalleeSpillMask & ~kAapcsFpCalleeSpillMask) == 0u);

  // For @CriticalNative, we can make a tail call if there are no stack args and the
  // return type is not an FP type (otherwise we need to move the result to FP register).
  DCHECK(!RequiresSmallResultTypeExtension());
  if (is_critical_native_ && (size != 0u || GetShorty()[0] == 'F' || GetShorty()[0] == 'D')) {
    size += kFramePointerSize;  // We need to spill LR with the args.
  }
  size_t out_args_size = RoundUp(size, kAapcsStackAlignment);
  if (UNLIKELY(IsCriticalNative())) {
    DCHECK_EQ(out_args_size, GetCriticalNativeStubFrameSize(GetShorty(), NumArgs() + 1u));
  }
  return out_args_size;
}

ArrayRef<const ManagedRegister> ArmJniCallingConvention::CalleeSaveRegisters() const {
  if (UNLIKELY(IsCriticalNative())) {
    if (UseTailCall()) {
      return ArrayRef<const ManagedRegister>();  // Do not spill anything.
    } else {
      // Spill LR with out args.
      static_assert((kCoreCalleeSpillMask >> LR) == 1u);  // Contains LR as the highest bit.
      constexpr size_t lr_index = POPCOUNT(kCoreCalleeSpillMask) - 1u;
      static_assert(kCalleeSaveRegisters[lr_index].Equals(
                        ArmManagedRegister::FromCoreRegister(LR)));
      return ArrayRef<const ManagedRegister>(kCalleeSaveRegisters).SubArray(
          /*pos*/ lr_index, /*length=*/ 1u);
    }
  } else {
    return ArrayRef<const ManagedRegister>(kCalleeSaveRegisters);
  }
}

// JniCallingConvention ABI follows AAPCS where longs and doubles must occur
// in even register numbers and stack slots
void ArmJniCallingConvention::Next() {
  // Update the iterator by usual JNI rules.
  JniCallingConvention::Next();

  if (LIKELY(HasNext())) {  // Avoid CHECK failure for IsCurrentParam
    // Ensure slot is 8-byte aligned for longs/doubles (AAPCS).
    if (IsCurrentParamALongOrDouble() && ((itr_slots_ & 0x1u) != 0)) {
      // itr_slots_ needs to be an even number, according to AAPCS.
      itr_slots_++;
    }
  }
}

bool ArmJniCallingConvention::IsCurrentParamInRegister() {
  return itr_slots_ < kJniArgumentRegisterCount;
}

bool ArmJniCallingConvention::IsCurrentParamOnStack() {
  return !IsCurrentParamInRegister();
}

ManagedRegister ArmJniCallingConvention::CurrentParamRegister() {
  CHECK_LT(itr_slots_, kJniArgumentRegisterCount);
  if (IsCurrentParamALongOrDouble()) {
    // AAPCS 5.1.1 requires 64-bit values to be in a consecutive register pair:
    // "A double-word sized type is passed in two consecutive registers (e.g., r0 and r1, or r2 and
    // r3). The content of the registers is as if the value had been loaded from memory
    // representation with a single LDM instruction."
    if (itr_slots_ == 0u) {
      return ArmManagedRegister::FromRegisterPair(R0_R1);
    } else if (itr_slots_ == 2u) {
      return ArmManagedRegister::FromRegisterPair(R2_R3);
    } else {
      // The register can either be R0 (+R1) or R2 (+R3). Cannot be other values.
      LOG(FATAL) << "Invalid iterator register position for a long/double " << itr_args_;
      UNREACHABLE();
    }
  } else {
    // All other types can fit into one register.
    return ArmManagedRegister::FromCoreRegister(kJniArgumentRegisters[itr_slots_]);
  }
}

FrameOffset ArmJniCallingConvention::CurrentParamStackOffset() {
  CHECK_GE(itr_slots_, kJniArgumentRegisterCount);
  size_t offset =
      displacement_.Int32Value()
          - OutFrameSize()
          + ((itr_slots_ - kJniArgumentRegisterCount) * kFramePointerSize);
  CHECK_LT(offset, OutFrameSize());
  return FrameOffset(offset);
}

ManagedRegister ArmJniCallingConvention::HiddenArgumentRegister() const {
  CHECK(IsCriticalNative());
  // R4 is neither managed callee-save, nor argument register, nor scratch register.
  // (It is native callee-save but the value coming from managed code can be clobbered.)
  // TODO: Change to static_assert; std::none_of should be constexpr since C++20.
  DCHECK(std::none_of(kCalleeSaveRegisters,
                      kCalleeSaveRegisters + std::size(kCalleeSaveRegisters),
                      [](ManagedRegister callee_save) constexpr {
                        return callee_save.Equals(ArmManagedRegister::FromCoreRegister(R4));
                      }));
  DCHECK(std::none_of(kJniArgumentRegisters,
                      kJniArgumentRegisters + std::size(kJniArgumentRegisters),
                      [](Register reg) { return reg == R4; }));
  return ArmManagedRegister::FromCoreRegister(R4);
}

// Whether to use tail call (used only for @CriticalNative).
bool ArmJniCallingConvention::UseTailCall() const {
  CHECK(IsCriticalNative());
  return OutFrameSize() == 0u;
}

}  // namespace arm
}  // namespace art
