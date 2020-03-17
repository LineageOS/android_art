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

#include "jni_macro_assembler_arm_vixl.h"

#include <iostream>
#include <type_traits>

#include "entrypoints/quick/quick_entrypoints.h"
#include "thread.h"

using namespace vixl::aarch32;  // NOLINT(build/namespaces)
namespace vixl32 = vixl::aarch32;

using vixl::ExactAssemblyScope;
using vixl::CodeBufferCheckScope;

namespace art {
namespace arm {

#ifdef ___
#error "ARM Assembler macro already defined."
#else
#define ___   asm_.GetVIXLAssembler()->
#endif

// The AAPCS requires 8-byte alignement. This is not as strict as the Managed ABI stack alignment.
static constexpr size_t kAapcsStackAlignment = 8u;
static_assert(kAapcsStackAlignment < kStackAlignment);

// STRD immediate can encode any 4-byte aligned offset smaller than this cutoff.
static constexpr size_t kStrdOffsetCutoff = 1024u;

vixl::aarch32::Register AsVIXLRegister(ArmManagedRegister reg) {
  CHECK(reg.IsCoreRegister());
  return vixl::aarch32::Register(reg.RegId());
}

static inline vixl::aarch32::SRegister AsVIXLSRegister(ArmManagedRegister reg) {
  CHECK(reg.IsSRegister());
  return vixl::aarch32::SRegister(reg.RegId() - kNumberOfCoreRegIds);
}

static inline vixl::aarch32::DRegister AsVIXLDRegister(ArmManagedRegister reg) {
  CHECK(reg.IsDRegister());
  return vixl::aarch32::DRegister(reg.RegId() - kNumberOfCoreRegIds - kNumberOfSRegIds);
}

static inline vixl::aarch32::Register AsVIXLRegisterPairLow(ArmManagedRegister reg) {
  return vixl::aarch32::Register(reg.AsRegisterPairLow());
}

static inline vixl::aarch32::Register AsVIXLRegisterPairHigh(ArmManagedRegister reg) {
  return vixl::aarch32::Register(reg.AsRegisterPairHigh());
}

void ArmVIXLJNIMacroAssembler::FinalizeCode() {
  for (const std::unique_ptr<
      ArmVIXLJNIMacroAssembler::ArmException>& exception : exception_blocks_) {
    EmitExceptionPoll(exception.get());
  }
  asm_.FinalizeCode();
}

static constexpr size_t kFramePointerSize = static_cast<size_t>(kArmPointerSize);

void ArmVIXLJNIMacroAssembler::BuildFrame(size_t frame_size,
                                          ManagedRegister method_reg,
                                          ArrayRef<const ManagedRegister> callee_save_regs) {
  // If we're creating an actual frame with the method, enforce managed stack alignment,
  // otherwise only the native stack alignment.
  if (method_reg.IsNoRegister()) {
    CHECK_ALIGNED_PARAM(frame_size, kAapcsStackAlignment);
  } else {
    CHECK_ALIGNED_PARAM(frame_size, kStackAlignment);
  }

  // Push callee saves and link register.
  RegList core_spill_mask = 0;
  uint32_t fp_spill_mask = 0;
  for (const ManagedRegister& reg : callee_save_regs) {
    if (reg.AsArm().IsCoreRegister()) {
      core_spill_mask |= 1 << reg.AsArm().AsCoreRegister();
    } else {
      fp_spill_mask |= 1 << reg.AsArm().AsSRegister();
    }
  }
  if (core_spill_mask != 0u) {
    ___ Push(RegisterList(core_spill_mask));
    cfi().AdjustCFAOffset(POPCOUNT(core_spill_mask) * kFramePointerSize);
    cfi().RelOffsetForMany(DWARFReg(r0), 0, core_spill_mask, kFramePointerSize);
  }
  if (fp_spill_mask != 0) {
    uint32_t first = CTZ(fp_spill_mask);

    // Check that list is contiguous.
    DCHECK_EQ(fp_spill_mask >> CTZ(fp_spill_mask), ~0u >> (32 - POPCOUNT(fp_spill_mask)));

    ___ Vpush(SRegisterList(vixl32::SRegister(first), POPCOUNT(fp_spill_mask)));
    cfi().AdjustCFAOffset(POPCOUNT(fp_spill_mask) * kFramePointerSize);
    cfi().RelOffsetForMany(DWARFReg(s0), 0, fp_spill_mask, kFramePointerSize);
  }

  // Increase frame to required size.
  int pushed_values = POPCOUNT(core_spill_mask) + POPCOUNT(fp_spill_mask);
  // Must at least have space for Method* if we're going to spill it.
  CHECK_GE(frame_size, (pushed_values + (method_reg.IsRegister() ? 1u : 0u)) * kFramePointerSize);
  IncreaseFrameSize(frame_size - pushed_values * kFramePointerSize);  // handles CFI as well.

  if (method_reg.IsRegister()) {
    // Write out Method*.
    CHECK(r0.Is(AsVIXLRegister(method_reg.AsArm())));
    asm_.StoreToOffset(kStoreWord, r0, sp, 0);
  }
}

void ArmVIXLJNIMacroAssembler::RemoveFrame(size_t frame_size,
                                           ArrayRef<const ManagedRegister> callee_save_regs,
                                           bool may_suspend) {
  CHECK_ALIGNED(frame_size, kAapcsStackAlignment);
  cfi().RememberState();

  // Compute callee saves to pop.
  RegList core_spill_mask = 0u;
  uint32_t fp_spill_mask = 0u;
  for (const ManagedRegister& reg : callee_save_regs) {
    if (reg.AsArm().IsCoreRegister()) {
      core_spill_mask |= 1u << reg.AsArm().AsCoreRegister();
    } else {
      fp_spill_mask |= 1u << reg.AsArm().AsSRegister();
    }
  }

  // Decrease frame to start of callee saves.
  size_t pop_values = POPCOUNT(core_spill_mask) + POPCOUNT(fp_spill_mask);
  CHECK_GE(frame_size, pop_values * kFramePointerSize);
  DecreaseFrameSize(frame_size - (pop_values * kFramePointerSize));  // handles CFI as well.

  // Pop FP callee saves.
  if (fp_spill_mask != 0u) {
    uint32_t first = CTZ(fp_spill_mask);
    // Check that list is contiguous.
     DCHECK_EQ(fp_spill_mask >> CTZ(fp_spill_mask), ~0u >> (32 - POPCOUNT(fp_spill_mask)));

    ___ Vpop(SRegisterList(vixl32::SRegister(first), POPCOUNT(fp_spill_mask)));
    cfi().AdjustCFAOffset(-kFramePointerSize * POPCOUNT(fp_spill_mask));
    cfi().RestoreMany(DWARFReg(s0), fp_spill_mask);
  }

  // Pop core callee saves and LR.
  if (core_spill_mask != 0u) {
    ___ Pop(RegisterList(core_spill_mask));
  }

  if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
    if (may_suspend) {
      // The method may be suspended; refresh the Marking Register.
      ___ Ldr(mr, MemOperand(tr, Thread::IsGcMarkingOffset<kArmPointerSize>().Int32Value()));
    } else {
      // The method shall not be suspended; no need to refresh the Marking Register.

      // The Marking Register is a callee-save register, and thus has been
      // preserved by native code following the AAPCS calling convention.

      // The following condition is a compile-time one, so it does not have a run-time cost.
      if (kIsDebugBuild) {
        // The following condition is a run-time one; it is executed after the
        // previous compile-time test, to avoid penalizing non-debug builds.
        if (emit_run_time_checks_in_debug_mode_) {
          // Emit a run-time check verifying that the Marking Register is up-to-date.
          UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
          vixl32::Register temp = temps.Acquire();
          // Ensure we are not clobbering a callee-save register that was restored before.
          DCHECK_EQ(core_spill_mask & (1 << temp.GetCode()), 0)
              << "core_spill_mask hould not contain scratch register R" << temp.GetCode();
          asm_.GenerateMarkingRegisterCheck(temp);
        }
      }
    }
  }

  // Return to LR.
  ___ Bx(vixl32::lr);

  // The CFI should be restored for any code that follows the exit block.
  cfi().RestoreState();
  cfi().DefCFAOffset(frame_size);
}


void ArmVIXLJNIMacroAssembler::IncreaseFrameSize(size_t adjust) {
  if (adjust != 0u) {
    asm_.AddConstant(sp, -adjust);
    cfi().AdjustCFAOffset(adjust);
  }
}

void ArmVIXLJNIMacroAssembler::DecreaseFrameSize(size_t adjust) {
  if (adjust != 0u) {
    asm_.AddConstant(sp, adjust);
    cfi().AdjustCFAOffset(-adjust);
  }
}

void ArmVIXLJNIMacroAssembler::Store(FrameOffset dest, ManagedRegister m_src, size_t size) {
  ArmManagedRegister src = m_src.AsArm();
  if (src.IsNoRegister()) {
    CHECK_EQ(0u, size);
  } else if (src.IsCoreRegister()) {
    CHECK_EQ(4u, size);
    UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
    temps.Exclude(AsVIXLRegister(src));
    asm_.StoreToOffset(kStoreWord, AsVIXLRegister(src), sp, dest.Int32Value());
  } else if (src.IsRegisterPair()) {
    CHECK_EQ(8u, size);
    ___ Strd(AsVIXLRegisterPairLow(src),
             AsVIXLRegisterPairHigh(src),
             MemOperand(sp, dest.Int32Value()));
  } else if (src.IsSRegister()) {
    CHECK_EQ(4u, size);
    asm_.StoreSToOffset(AsVIXLSRegister(src), sp, dest.Int32Value());
  } else {
    CHECK_EQ(8u, size);
    CHECK(src.IsDRegister()) << src;
    asm_.StoreDToOffset(AsVIXLDRegister(src), sp, dest.Int32Value());
  }
}

void ArmVIXLJNIMacroAssembler::StoreRef(FrameOffset dest, ManagedRegister msrc) {
  vixl::aarch32::Register src = AsVIXLRegister(msrc.AsArm());
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  temps.Exclude(src);
  asm_.StoreToOffset(kStoreWord, src, sp, dest.Int32Value());
}

void ArmVIXLJNIMacroAssembler::StoreRawPtr(FrameOffset dest, ManagedRegister msrc) {
  vixl::aarch32::Register src = AsVIXLRegister(msrc.AsArm());
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  temps.Exclude(src);
  asm_.StoreToOffset(kStoreWord, src, sp, dest.Int32Value());
}

void ArmVIXLJNIMacroAssembler::StoreSpanning(FrameOffset dest,
                                             ManagedRegister msrc,
                                             FrameOffset in_off) {
  vixl::aarch32::Register src = AsVIXLRegister(msrc.AsArm());
  asm_.StoreToOffset(kStoreWord, src, sp, dest.Int32Value());
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  vixl32::Register scratch = temps.Acquire();
  asm_.LoadFromOffset(kLoadWord, scratch, sp, in_off.Int32Value());
  asm_.StoreToOffset(kStoreWord, scratch, sp, dest.Int32Value() + 4);
}

void ArmVIXLJNIMacroAssembler::CopyRef(FrameOffset dest, FrameOffset src) {
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  vixl32::Register scratch = temps.Acquire();
  asm_.LoadFromOffset(kLoadWord, scratch, sp, src.Int32Value());
  asm_.StoreToOffset(kStoreWord, scratch, sp, dest.Int32Value());
}

void ArmVIXLJNIMacroAssembler::CopyRef(FrameOffset dest,
                                       ManagedRegister base,
                                       MemberOffset offs,
                                       bool unpoison_reference) {
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  vixl32::Register scratch = temps.Acquire();
  asm_.LoadFromOffset(kLoadWord, scratch, AsVIXLRegister(base.AsArm()), offs.Int32Value());
  if (unpoison_reference) {
    asm_.MaybeUnpoisonHeapReference(scratch);
  }
  asm_.StoreToOffset(kStoreWord, scratch, sp, dest.Int32Value());
}

void ArmVIXLJNIMacroAssembler::LoadRef(ManagedRegister mdest,
                                       ManagedRegister mbase,
                                       MemberOffset offs,
                                       bool unpoison_reference) {
  vixl::aarch32::Register dest = AsVIXLRegister(mdest.AsArm());
  vixl::aarch32::Register base = AsVIXLRegister(mbase.AsArm());
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  temps.Exclude(dest, base);
  asm_.LoadFromOffset(kLoadWord, dest, base, offs.Int32Value());

  if (unpoison_reference) {
    asm_.MaybeUnpoisonHeapReference(dest);
  }
}

void ArmVIXLJNIMacroAssembler::LoadRef(ManagedRegister dest ATTRIBUTE_UNUSED,
                                       FrameOffset src ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmVIXLJNIMacroAssembler::LoadRawPtr(ManagedRegister dest ATTRIBUTE_UNUSED,
                                          ManagedRegister base ATTRIBUTE_UNUSED,
                                          Offset offs ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmVIXLJNIMacroAssembler::StoreImmediateToFrame(FrameOffset dest, uint32_t imm) {
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  vixl32::Register scratch = temps.Acquire();
  asm_.LoadImmediate(scratch, imm);
  asm_.StoreToOffset(kStoreWord, scratch, sp, dest.Int32Value());
}

void ArmVIXLJNIMacroAssembler::Load(ManagedRegister m_dst, FrameOffset src, size_t size) {
  return Load(m_dst.AsArm(), sp, src.Int32Value(), size);
}

void ArmVIXLJNIMacroAssembler::LoadFromThread(ManagedRegister m_dst,
                                              ThreadOffset32 src,
                                              size_t size) {
  return Load(m_dst.AsArm(), tr, src.Int32Value(), size);
}

void ArmVIXLJNIMacroAssembler::LoadRawPtrFromThread(ManagedRegister mdest, ThreadOffset32 offs) {
  vixl::aarch32::Register dest = AsVIXLRegister(mdest.AsArm());
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  temps.Exclude(dest);
  asm_.LoadFromOffset(kLoadWord, dest, tr, offs.Int32Value());
}

void ArmVIXLJNIMacroAssembler::CopyRawPtrFromThread(FrameOffset fr_offs, ThreadOffset32 thr_offs) {
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  vixl32::Register scratch = temps.Acquire();
  asm_.LoadFromOffset(kLoadWord, scratch, tr, thr_offs.Int32Value());
  asm_.StoreToOffset(kStoreWord, scratch, sp, fr_offs.Int32Value());
}

void ArmVIXLJNIMacroAssembler::CopyRawPtrToThread(ThreadOffset32 thr_offs ATTRIBUTE_UNUSED,
                                                  FrameOffset fr_offs ATTRIBUTE_UNUSED,
                                                  ManagedRegister mscratch ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmVIXLJNIMacroAssembler::StoreStackOffsetToThread(ThreadOffset32 thr_offs,
                                                        FrameOffset fr_offs) {
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  vixl32::Register scratch = temps.Acquire();
  asm_.AddConstant(scratch, sp, fr_offs.Int32Value());
  asm_.StoreToOffset(kStoreWord, scratch, tr, thr_offs.Int32Value());
}

void ArmVIXLJNIMacroAssembler::StoreStackPointerToThread(ThreadOffset32 thr_offs) {
  asm_.StoreToOffset(kStoreWord, sp, tr, thr_offs.Int32Value());
}

void ArmVIXLJNIMacroAssembler::SignExtend(ManagedRegister mreg ATTRIBUTE_UNUSED,
                                          size_t size ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL) << "no sign extension necessary for arm";
}

void ArmVIXLJNIMacroAssembler::ZeroExtend(ManagedRegister mreg ATTRIBUTE_UNUSED,
                                          size_t size ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL) << "no zero extension necessary for arm";
}

static inline bool IsCoreRegisterOrPair(ArmManagedRegister reg) {
  return reg.IsCoreRegister() || reg.IsRegisterPair();
}

static inline bool NoSpillGap(const ArgumentLocation& loc1, const ArgumentLocation& loc2) {
  DCHECK(!loc1.IsRegister());
  DCHECK(!loc2.IsRegister());
  uint32_t loc1_offset = loc1.GetFrameOffset().Uint32Value();
  uint32_t loc2_offset = loc2.GetFrameOffset().Uint32Value();
  DCHECK_LT(loc1_offset, loc2_offset);
  return loc1_offset + loc1.GetSize() == loc2_offset;
}

static inline uint32_t GetSRegisterNumber(ArmManagedRegister reg) {
  if (reg.IsSRegister()) {
    return static_cast<uint32_t>(reg.AsSRegister());
  } else {
    DCHECK(reg.IsDRegister());
    return 2u * static_cast<uint32_t>(reg.AsDRegister());
  }
}

// Get the number of locations to spill together.
static inline size_t GetSpillChunkSize(ArrayRef<ArgumentLocation> dests,
                                       ArrayRef<ArgumentLocation> srcs,
                                       size_t start,
                                       bool have_extra_temp) {
  DCHECK_LT(start, dests.size());
  DCHECK_ALIGNED(dests[start].GetFrameOffset().Uint32Value(), 4u);
  const ArgumentLocation& first_src = srcs[start];
  if (!first_src.IsRegister()) {
    DCHECK_ALIGNED(first_src.GetFrameOffset().Uint32Value(), 4u);
    // If we have an extra temporary, look for opportunities to move 2 words
    // at a time with LDRD/STRD when the source types are word-sized.
    if (have_extra_temp &&
        start + 1u != dests.size() &&
        !srcs[start + 1u].IsRegister() &&
        first_src.GetSize() == 4u &&
        srcs[start + 1u].GetSize() == 4u &&
        NoSpillGap(first_src, srcs[start + 1u]) &&
        NoSpillGap(dests[start], dests[start + 1u]) &&
        dests[start].GetFrameOffset().Uint32Value() < kStrdOffsetCutoff) {
      // Note: The source and destination may not be 8B aligned (but they are 4B aligned).
      return 2u;
    }
    return 1u;
  }
  ArmManagedRegister first_src_reg = first_src.GetRegister().AsArm();
  size_t end = start + 1u;
  if (IsCoreRegisterOrPair(first_src_reg)) {
    while (end != dests.size() &&
           NoSpillGap(dests[end - 1u], dests[end]) &&
           srcs[end].IsRegister() &&
           IsCoreRegisterOrPair(srcs[end].GetRegister().AsArm())) {
      ++end;
    }
  } else {
    DCHECK(first_src_reg.IsSRegister() || first_src_reg.IsDRegister());
    uint32_t next_sreg = GetSRegisterNumber(first_src_reg) + first_src.GetSize() / kSRegSizeInBytes;
    while (end != dests.size() &&
           NoSpillGap(dests[end - 1u], dests[end]) &&
           srcs[end].IsRegister() &&
           !IsCoreRegisterOrPair(srcs[end].GetRegister().AsArm()) &&
           GetSRegisterNumber(srcs[end].GetRegister().AsArm()) == next_sreg) {
      next_sreg += srcs[end].GetSize() / kSRegSizeInBytes;
      ++end;
    }
  }
  return end - start;
}

static inline uint32_t GetCoreRegisterMask(ArmManagedRegister reg) {
  if (reg.IsCoreRegister()) {
    return 1u << static_cast<size_t>(reg.AsCoreRegister());
  } else {
    DCHECK(reg.IsRegisterPair());
    DCHECK_LT(reg.AsRegisterPairLow(), reg.AsRegisterPairHigh());
    return (1u << static_cast<size_t>(reg.AsRegisterPairLow())) |
           (1u << static_cast<size_t>(reg.AsRegisterPairHigh()));
  }
}

static inline uint32_t GetCoreRegisterMask(ArrayRef<ArgumentLocation> srcs) {
  uint32_t mask = 0u;
  for (const ArgumentLocation& loc : srcs) {
    DCHECK(loc.IsRegister());
    mask |= GetCoreRegisterMask(loc.GetRegister().AsArm());
  }
  return mask;
}

static inline bool UseStrdForChunk(ArrayRef<ArgumentLocation> srcs, size_t start, size_t length) {
  DCHECK_GE(length, 2u);
  DCHECK(srcs[start].IsRegister());
  DCHECK(srcs[start + 1u].IsRegister());
  // The destination may not be 8B aligned (but it is 4B aligned).
  // Allow arbitrary destination offset, macro assembler will use a temp if needed.
  // Note: T32 allows unrelated registers in STRD. (A32 does not.)
  return length == 2u &&
         srcs[start].GetRegister().AsArm().IsCoreRegister() &&
         srcs[start + 1u].GetRegister().AsArm().IsCoreRegister();
}

static inline bool UseVstrForChunk(ArrayRef<ArgumentLocation> srcs, size_t start, size_t length) {
  DCHECK_GE(length, 2u);
  DCHECK(srcs[start].IsRegister());
  DCHECK(srcs[start + 1u].IsRegister());
  // The destination may not be 8B aligned (but it is 4B aligned).
  // Allow arbitrary destination offset, macro assembler will use a temp if needed.
  return length == 2u &&
         srcs[start].GetRegister().AsArm().IsSRegister() &&
         srcs[start + 1u].GetRegister().AsArm().IsSRegister() &&
         IsAligned<2u>(static_cast<size_t>(srcs[start].GetRegister().AsArm().AsSRegister()));
}

void ArmVIXLJNIMacroAssembler::MoveArguments(ArrayRef<ArgumentLocation> dests,
                                             ArrayRef<ArgumentLocation> srcs) {
  DCHECK_EQ(dests.size(), srcs.size());

  // Native ABI is soft-float, so all destinations should be core registers or stack offsets.
  // And register locations should be first, followed by stack locations with increasing offset.
  auto is_register = [](const ArgumentLocation& loc) { return loc.IsRegister(); };
  DCHECK(std::is_partitioned(dests.begin(), dests.end(), is_register));
  size_t num_reg_dests =
      std::distance(dests.begin(), std::partition_point(dests.begin(), dests.end(), is_register));
  DCHECK(std::is_sorted(
      dests.begin() + num_reg_dests,
      dests.end(),
      [](const ArgumentLocation& lhs, const ArgumentLocation& rhs) {
        return lhs.GetFrameOffset().Uint32Value() < rhs.GetFrameOffset().Uint32Value();
      }));

  // Collect registers to move. No need to record FP regs as destinations are only core regs.
  uint32_t src_regs = 0u;
  uint32_t dest_regs = 0u;
  for (size_t i = 0; i != num_reg_dests; ++i) {
    const ArgumentLocation& src = srcs[i];
    const ArgumentLocation& dest = dests[i];
    DCHECK(dest.IsRegister() && IsCoreRegisterOrPair(dest.GetRegister().AsArm()));
    if (src.IsRegister() && IsCoreRegisterOrPair(src.GetRegister().AsArm())) {
      if (src.GetRegister().Equals(dest.GetRegister())) {
        continue;
      }
      src_regs |= GetCoreRegisterMask(src.GetRegister().AsArm());
    }
    dest_regs |= GetCoreRegisterMask(dest.GetRegister().AsArm());
  }

  // Spill args first. Look for opportunities to spill multiple arguments at once.
  {
    UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
    vixl32::Register xtemp;  // Extra temp register;
    if ((dest_regs & ~src_regs) != 0u) {
      xtemp = vixl32::Register(CTZ(dest_regs & ~src_regs));
      DCHECK(!temps.IsAvailable(xtemp));
    }
    auto move_two_words = [&](FrameOffset dest_offset, FrameOffset src_offset) {
      DCHECK(xtemp.IsValid());
      DCHECK_LT(dest_offset.Uint32Value(), kStrdOffsetCutoff);
      // VIXL macro assembler can use destination registers for loads from large offsets.
      UseScratchRegisterScope temps2(asm_.GetVIXLAssembler());
      vixl32::Register temp2 = temps2.Acquire();
      ___ Ldrd(xtemp, temp2, MemOperand(sp, src_offset.Uint32Value()));
      ___ Strd(xtemp, temp2, MemOperand(sp, dest_offset.Uint32Value()));
    };
    for (size_t i = num_reg_dests, arg_count = dests.size(); i != arg_count; ) {
      const ArgumentLocation& src = srcs[i];
      const ArgumentLocation& dest = dests[i];
      DCHECK_EQ(src.GetSize(), dest.GetSize());
      DCHECK(!dest.IsRegister());
      uint32_t frame_offset = dest.GetFrameOffset().Uint32Value();
      size_t chunk_size = GetSpillChunkSize(dests, srcs, i, xtemp.IsValid());
      DCHECK_NE(chunk_size, 0u);
      if (chunk_size == 1u) {
        if (src.IsRegister()) {
          Store(dest.GetFrameOffset(), src.GetRegister(), dest.GetSize());
        } else if (dest.GetSize() == 8u && xtemp.IsValid() && frame_offset < kStrdOffsetCutoff) {
          move_two_words(dest.GetFrameOffset(), src.GetFrameOffset());
        } else {
          Copy(dest.GetFrameOffset(), src.GetFrameOffset(), dest.GetSize());
        }
      } else if (!src.IsRegister()) {
        DCHECK_EQ(chunk_size, 2u);
        DCHECK_EQ(dest.GetSize(), 4u);
        DCHECK_EQ(dests[i + 1u].GetSize(), 4u);
        move_two_words(dest.GetFrameOffset(), src.GetFrameOffset());
      } else if (UseStrdForChunk(srcs, i, chunk_size)) {
        ___ Strd(AsVIXLRegister(srcs[i].GetRegister().AsArm()),
                 AsVIXLRegister(srcs[i + 1u].GetRegister().AsArm()),
                 MemOperand(sp, frame_offset));
      } else if (UseVstrForChunk(srcs, i, chunk_size)) {
        size_t sreg = GetSRegisterNumber(src.GetRegister().AsArm());
        DCHECK_ALIGNED(sreg, 2u);
        ___ Vstr(vixl32::DRegister(sreg / 2u), MemOperand(sp, frame_offset));
      } else {
        UseScratchRegisterScope temps2(asm_.GetVIXLAssembler());
        vixl32::Register base_reg;
        if (frame_offset == 0u) {
          base_reg = sp;
        } else {
          base_reg = temps2.Acquire();
          ___ Add(base_reg, sp, frame_offset);
        }

        ArmManagedRegister src_reg = src.GetRegister().AsArm();
        if (IsCoreRegisterOrPair(src_reg)) {
          uint32_t core_reg_mask = GetCoreRegisterMask(srcs.SubArray(i, chunk_size));
          ___ Stm(base_reg, NO_WRITE_BACK, RegisterList(core_reg_mask));
        } else {
          uint32_t start_sreg = GetSRegisterNumber(src_reg);
          const ArgumentLocation& last_dest = dests[i + chunk_size - 1u];
          uint32_t total_size =
              last_dest.GetFrameOffset().Uint32Value() + last_dest.GetSize() - frame_offset;
          if (IsAligned<2u>(start_sreg) &&
              IsAligned<kDRegSizeInBytes>(frame_offset) &&
              IsAligned<kDRegSizeInBytes>(total_size)) {
            uint32_t dreg_count = total_size / kDRegSizeInBytes;
            DRegisterList dreg_list(vixl32::DRegister(start_sreg / 2u), dreg_count);
            ___ Vstm(F64, base_reg, NO_WRITE_BACK, dreg_list);
          } else {
            uint32_t sreg_count = total_size / kSRegSizeInBytes;
            SRegisterList sreg_list(vixl32::SRegister(start_sreg), sreg_count);
            ___ Vstm(F32, base_reg, NO_WRITE_BACK, sreg_list);
          }
        }
      }
      i += chunk_size;
    }
  }

  // Fill destination registers from source core registers.
  // There should be no cycles, so this algorithm should make progress.
  while (src_regs != 0u) {
    uint32_t old_src_regs = src_regs;
    for (size_t i = 0; i != num_reg_dests; ++i) {
      DCHECK(dests[i].IsRegister() && IsCoreRegisterOrPair(dests[i].GetRegister().AsArm()));
      if (!srcs[i].IsRegister() || !IsCoreRegisterOrPair(srcs[i].GetRegister().AsArm())) {
        continue;
      }
      uint32_t dest_reg_mask = GetCoreRegisterMask(dests[i].GetRegister().AsArm());
      if ((dest_reg_mask & dest_regs) == 0u) {
        continue;  // Equals source, or already filled in one of previous iterations.
      }
      // There are no partial overlaps of 8-byte arguments, otherwise we would have to
      // tweak this check; Move() can deal with partial overlap for historical reasons.
      if ((dest_reg_mask & src_regs) != 0u) {
        continue;  // Cannot clobber this register yet.
      }
      Move(dests[i].GetRegister(), srcs[i].GetRegister(), dests[i].GetSize());
      uint32_t src_reg_mask = GetCoreRegisterMask(srcs[i].GetRegister().AsArm());
      DCHECK_EQ(src_regs & src_reg_mask, src_reg_mask);
      src_regs &= ~src_reg_mask;  // Allow clobbering the source register or pair.
      dest_regs &= ~dest_reg_mask;  // Destination register or pair was filled.
    }
    CHECK_NE(old_src_regs, src_regs);
    DCHECK_EQ(0u, src_regs & ~old_src_regs);
  }

  // Now fill destination registers from FP registers or stack slots, looking for
  // opportunities to use LDRD/VMOV to fill 2 registers with one instruction.
  for (size_t i = 0, j; i != num_reg_dests; i = j) {
    j = i + 1u;
    DCHECK(dests[i].IsRegister() && IsCoreRegisterOrPair(dests[i].GetRegister().AsArm()));
    if (srcs[i].IsRegister() && IsCoreRegisterOrPair(srcs[i].GetRegister().AsArm())) {
      DCHECK_EQ(GetCoreRegisterMask(dests[i].GetRegister().AsArm()) & dest_regs, 0u);
      continue;  // Equals destination or moved above.
    }
    DCHECK_NE(GetCoreRegisterMask(dests[i].GetRegister().AsArm()) & dest_regs, 0u);
    if (dests[i].GetSize() == 4u) {
      // Find next register to load.
      while (j != num_reg_dests &&
             (srcs[j].IsRegister() && IsCoreRegisterOrPair(srcs[j].GetRegister().AsArm()))) {
        DCHECK_EQ(GetCoreRegisterMask(dests[j].GetRegister().AsArm()) & dest_regs, 0u);
        ++j;  // Equals destination or moved above.
      }
      if (j != num_reg_dests && dests[j].GetSize() == 4u) {
        if (!srcs[i].IsRegister() && !srcs[j].IsRegister() && NoSpillGap(srcs[i], srcs[j])) {
          ___ Ldrd(AsVIXLRegister(dests[i].GetRegister().AsArm()),
                   AsVIXLRegister(dests[j].GetRegister().AsArm()),
                   MemOperand(sp, srcs[i].GetFrameOffset().Uint32Value()));
          ++j;
          continue;
        }
        if (srcs[i].IsRegister() && srcs[j].IsRegister()) {
          uint32_t first_sreg = GetSRegisterNumber(srcs[i].GetRegister().AsArm());
          if (IsAligned<2u>(first_sreg) &&
              first_sreg + 1u == GetSRegisterNumber(srcs[j].GetRegister().AsArm())) {
            ___ Vmov(AsVIXLRegister(dests[i].GetRegister().AsArm()),
                     AsVIXLRegister(dests[j].GetRegister().AsArm()),
                     vixl32::DRegister(first_sreg / 2u));
            ++j;
            continue;
          }
        }
      }
    }
    if (srcs[i].IsRegister()) {
      Move(dests[i].GetRegister(), srcs[i].GetRegister(), dests[i].GetSize());
    } else {
      Load(dests[i].GetRegister(), srcs[i].GetFrameOffset(), dests[i].GetSize());
    }
  }
}

void ArmVIXLJNIMacroAssembler::Move(ManagedRegister mdst,
                                    ManagedRegister msrc,
                                    size_t size  ATTRIBUTE_UNUSED) {
  ArmManagedRegister dst = mdst.AsArm();
  if (kIsDebugBuild) {
    // Check that the destination is not a scratch register.
    UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
    if (dst.IsCoreRegister()) {
      CHECK(!temps.IsAvailable(AsVIXLRegister(dst)));
    } else if (dst.IsDRegister()) {
      CHECK(!temps.IsAvailable(AsVIXLDRegister(dst)));
    } else if (dst.IsSRegister()) {
      CHECK(!temps.IsAvailable(AsVIXLSRegister(dst)));
    } else {
      CHECK(dst.IsRegisterPair()) << dst;
      CHECK(!temps.IsAvailable(AsVIXLRegisterPairLow(dst)));
      CHECK(!temps.IsAvailable(AsVIXLRegisterPairHigh(dst)));
    }
  }
  ArmManagedRegister src = msrc.AsArm();
  if (!dst.Equals(src)) {
    if (dst.IsCoreRegister()) {
      if (src.IsCoreRegister()) {
        ___ Mov(AsVIXLRegister(dst), AsVIXLRegister(src));
      } else {
        CHECK(src.IsSRegister()) << src;
        ___ Vmov(AsVIXLRegister(dst), AsVIXLSRegister(src));
      }
    } else if (dst.IsDRegister()) {
      if (src.IsDRegister()) {
        ___ Vmov(F64, AsVIXLDRegister(dst), AsVIXLDRegister(src));
      } else {
        // VMOV Dn, Rlo, Rhi (Dn = {Rlo, Rhi})
        CHECK(src.IsRegisterPair()) << src;
        ___ Vmov(AsVIXLDRegister(dst), AsVIXLRegisterPairLow(src), AsVIXLRegisterPairHigh(src));
      }
    } else if (dst.IsSRegister()) {
      if (src.IsSRegister()) {
        ___ Vmov(F32, AsVIXLSRegister(dst), AsVIXLSRegister(src));
      } else {
        // VMOV Sn, Rn  (Sn = Rn)
        CHECK(src.IsCoreRegister()) << src;
        ___ Vmov(AsVIXLSRegister(dst), AsVIXLRegister(src));
      }
    } else {
      CHECK(dst.IsRegisterPair()) << dst;
      if (src.IsRegisterPair()) {
        // Ensure that the first move doesn't clobber the input of the second.
        if (src.AsRegisterPairHigh() != dst.AsRegisterPairLow()) {
          ___ Mov(AsVIXLRegisterPairLow(dst),  AsVIXLRegisterPairLow(src));
          ___ Mov(AsVIXLRegisterPairHigh(dst), AsVIXLRegisterPairHigh(src));
        } else {
          ___ Mov(AsVIXLRegisterPairHigh(dst), AsVIXLRegisterPairHigh(src));
          ___ Mov(AsVIXLRegisterPairLow(dst),  AsVIXLRegisterPairLow(src));
        }
      } else {
        CHECK(src.IsDRegister()) << src;
        ___ Vmov(AsVIXLRegisterPairLow(dst), AsVIXLRegisterPairHigh(dst), AsVIXLDRegister(src));
      }
    }
  }
}

void ArmVIXLJNIMacroAssembler::Copy(FrameOffset dest, FrameOffset src, size_t size) {
  DCHECK(size == 4 || size == 8) << size;
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  vixl32::Register scratch = temps.Acquire();
  if (size == 4) {
    asm_.LoadFromOffset(kLoadWord, scratch, sp, src.Int32Value());
    asm_.StoreToOffset(kStoreWord, scratch, sp, dest.Int32Value());
  } else if (size == 8) {
    asm_.LoadFromOffset(kLoadWord, scratch, sp, src.Int32Value());
    asm_.StoreToOffset(kStoreWord, scratch, sp, dest.Int32Value());
    asm_.LoadFromOffset(kLoadWord, scratch, sp, src.Int32Value() + 4);
    asm_.StoreToOffset(kStoreWord, scratch, sp, dest.Int32Value() + 4);
  }
}

void ArmVIXLJNIMacroAssembler::Copy(FrameOffset dest ATTRIBUTE_UNUSED,
                                    ManagedRegister src_base ATTRIBUTE_UNUSED,
                                    Offset src_offset ATTRIBUTE_UNUSED,
                                    ManagedRegister mscratch ATTRIBUTE_UNUSED,
                                    size_t size ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmVIXLJNIMacroAssembler::Copy(ManagedRegister dest_base ATTRIBUTE_UNUSED,
                                    Offset dest_offset ATTRIBUTE_UNUSED,
                                    FrameOffset src ATTRIBUTE_UNUSED,
                                    ManagedRegister mscratch ATTRIBUTE_UNUSED,
                                    size_t size ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmVIXLJNIMacroAssembler::Copy(FrameOffset dst ATTRIBUTE_UNUSED,
                                    FrameOffset src_base ATTRIBUTE_UNUSED,
                                    Offset src_offset ATTRIBUTE_UNUSED,
                                    ManagedRegister mscratch ATTRIBUTE_UNUSED,
                                    size_t size ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmVIXLJNIMacroAssembler::Copy(ManagedRegister dest ATTRIBUTE_UNUSED,
                                    Offset dest_offset ATTRIBUTE_UNUSED,
                                    ManagedRegister src ATTRIBUTE_UNUSED,
                                    Offset src_offset ATTRIBUTE_UNUSED,
                                    ManagedRegister mscratch ATTRIBUTE_UNUSED,
                                    size_t size ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmVIXLJNIMacroAssembler::Copy(FrameOffset dst ATTRIBUTE_UNUSED,
                                    Offset dest_offset ATTRIBUTE_UNUSED,
                                    FrameOffset src ATTRIBUTE_UNUSED,
                                    Offset src_offset ATTRIBUTE_UNUSED,
                                    ManagedRegister scratch ATTRIBUTE_UNUSED,
                                    size_t size ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmVIXLJNIMacroAssembler::CreateHandleScopeEntry(ManagedRegister mout_reg,
                                                      FrameOffset handle_scope_offset,
                                                      ManagedRegister min_reg,
                                                      bool null_allowed) {
  vixl::aarch32::Register out_reg = AsVIXLRegister(mout_reg.AsArm());
  vixl::aarch32::Register in_reg =
      min_reg.AsArm().IsNoRegister() ? vixl::aarch32::Register() : AsVIXLRegister(min_reg.AsArm());
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  temps.Exclude(out_reg);
  if (null_allowed) {
    // Null values get a handle scope entry value of 0.  Otherwise, the handle scope entry is
    // the address in the handle scope holding the reference.
    // e.g. out_reg = (handle == 0) ? 0 : (SP+handle_offset)
    if (!in_reg.IsValid()) {
      asm_.LoadFromOffset(kLoadWord, out_reg, sp, handle_scope_offset.Int32Value());
      in_reg = out_reg;
    }

    temps.Exclude(in_reg);
    ___ Cmp(in_reg, 0);

    if (asm_.ShifterOperandCanHold(ADD, handle_scope_offset.Int32Value())) {
      if (!out_reg.Is(in_reg)) {
        ExactAssemblyScope guard(asm_.GetVIXLAssembler(),
                                 3 * vixl32::kMaxInstructionSizeInBytes,
                                 CodeBufferCheckScope::kMaximumSize);
        ___ it(eq, 0xc);
        ___ mov(eq, out_reg, 0);
        asm_.AddConstantInIt(out_reg, sp, handle_scope_offset.Int32Value(), ne);
      } else {
        ExactAssemblyScope guard(asm_.GetVIXLAssembler(),
                                 2 * vixl32::kMaxInstructionSizeInBytes,
                                 CodeBufferCheckScope::kMaximumSize);
        ___ it(ne, 0x8);
        asm_.AddConstantInIt(out_reg, sp, handle_scope_offset.Int32Value(), ne);
      }
    } else {
      // TODO: Implement this (old arm assembler would have crashed here).
      UNIMPLEMENTED(FATAL);
    }
  } else {
    asm_.AddConstant(out_reg, sp, handle_scope_offset.Int32Value());
  }
}

void ArmVIXLJNIMacroAssembler::CreateHandleScopeEntry(FrameOffset out_off,
                                                      FrameOffset handle_scope_offset,
                                                      bool null_allowed) {
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  vixl32::Register scratch = temps.Acquire();
  if (null_allowed) {
    asm_.LoadFromOffset(kLoadWord, scratch, sp, handle_scope_offset.Int32Value());
    // Null values get a handle scope entry value of 0.  Otherwise, the handle scope entry is
    // the address in the handle scope holding the reference.
    // e.g. scratch = (scratch == 0) ? 0 : (SP+handle_scope_offset)
    ___ Cmp(scratch, 0);

    if (asm_.ShifterOperandCanHold(ADD, handle_scope_offset.Int32Value())) {
      ExactAssemblyScope guard(asm_.GetVIXLAssembler(),
                               2 * vixl32::kMaxInstructionSizeInBytes,
                               CodeBufferCheckScope::kMaximumSize);
      ___ it(ne, 0x8);
      asm_.AddConstantInIt(scratch, sp, handle_scope_offset.Int32Value(), ne);
    } else {
      // TODO: Implement this (old arm assembler would have crashed here).
      UNIMPLEMENTED(FATAL);
    }
  } else {
    asm_.AddConstant(scratch, sp, handle_scope_offset.Int32Value());
  }
  asm_.StoreToOffset(kStoreWord, scratch, sp, out_off.Int32Value());
}

void ArmVIXLJNIMacroAssembler::LoadReferenceFromHandleScope(
    ManagedRegister mout_reg ATTRIBUTE_UNUSED,
    ManagedRegister min_reg ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmVIXLJNIMacroAssembler::VerifyObject(ManagedRegister src ATTRIBUTE_UNUSED,
                                            bool could_be_null ATTRIBUTE_UNUSED) {
  // TODO: not validating references.
}

void ArmVIXLJNIMacroAssembler::VerifyObject(FrameOffset src ATTRIBUTE_UNUSED,
                                            bool could_be_null ATTRIBUTE_UNUSED) {
  // TODO: not validating references.
}

void ArmVIXLJNIMacroAssembler::Jump(ManagedRegister mbase, Offset offset) {
  vixl::aarch32::Register base = AsVIXLRegister(mbase.AsArm());
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  vixl32::Register scratch = temps.Acquire();
  asm_.LoadFromOffset(kLoadWord, scratch, base, offset.Int32Value());
  ___ Bx(scratch);
}

void ArmVIXLJNIMacroAssembler::Call(ManagedRegister mbase, Offset offset) {
  vixl::aarch32::Register base = AsVIXLRegister(mbase.AsArm());
  asm_.LoadFromOffset(kLoadWord, lr, base, offset.Int32Value());
  ___ Blx(lr);
  // TODO: place reference map on call.
}

void ArmVIXLJNIMacroAssembler::Call(FrameOffset base, Offset offset) {
  // Call *(*(SP + base) + offset)
  asm_.LoadFromOffset(kLoadWord, lr, sp, base.Int32Value());
  asm_.LoadFromOffset(kLoadWord, lr, lr, offset.Int32Value());
  ___ Blx(lr);
  // TODO: place reference map on call
}

void ArmVIXLJNIMacroAssembler::CallFromThread(ThreadOffset32 offset ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmVIXLJNIMacroAssembler::GetCurrentThread(ManagedRegister dest) {
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  temps.Exclude(AsVIXLRegister(dest.AsArm()));
  ___ Mov(AsVIXLRegister(dest.AsArm()), tr);
}

void ArmVIXLJNIMacroAssembler::GetCurrentThread(FrameOffset dest_offset) {
  asm_.StoreToOffset(kStoreWord, tr, sp, dest_offset.Int32Value());
}

void ArmVIXLJNIMacroAssembler::ExceptionPoll(size_t stack_adjust) {
  CHECK_ALIGNED(stack_adjust, kAapcsStackAlignment);
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  vixl32::Register scratch = temps.Acquire();
  exception_blocks_.emplace_back(
      new ArmVIXLJNIMacroAssembler::ArmException(scratch, stack_adjust));
  asm_.LoadFromOffset(kLoadWord,
                      scratch,
                      tr,
                      Thread::ExceptionOffset<kArmPointerSize>().Int32Value());

  ___ Cmp(scratch, 0);
  vixl32::Label* label = exception_blocks_.back()->Entry();
  ___ BPreferNear(ne, label);
  // TODO: think about using CBNZ here.
}

std::unique_ptr<JNIMacroLabel> ArmVIXLJNIMacroAssembler::CreateLabel() {
  return std::unique_ptr<JNIMacroLabel>(new ArmVIXLJNIMacroLabel());
}

void ArmVIXLJNIMacroAssembler::Jump(JNIMacroLabel* label) {
  CHECK(label != nullptr);
  ___ B(ArmVIXLJNIMacroLabel::Cast(label)->AsArm());
}

void ArmVIXLJNIMacroAssembler::TestGcMarking(JNIMacroLabel* label, JNIMacroUnaryCondition cond) {
  CHECK(label != nullptr);

  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  vixl32::Register scratch = temps.Acquire();
  DCHECK_EQ(Thread::IsGcMarkingSize(), 4u);
  ___ Ldr(scratch, MemOperand(tr, Thread::IsGcMarkingOffset<kArmPointerSize>().Int32Value()));
  switch (cond) {
    case JNIMacroUnaryCondition::kZero:
      ___ CompareAndBranchIfZero(scratch, ArmVIXLJNIMacroLabel::Cast(label)->AsArm());
      break;
    case JNIMacroUnaryCondition::kNotZero:
      ___ CompareAndBranchIfNonZero(scratch, ArmVIXLJNIMacroLabel::Cast(label)->AsArm());
      break;
    default:
      LOG(FATAL) << "Not implemented unary condition: " << static_cast<int>(cond);
      UNREACHABLE();
  }
}

void ArmVIXLJNIMacroAssembler::Bind(JNIMacroLabel* label) {
  CHECK(label != nullptr);
  ___ Bind(ArmVIXLJNIMacroLabel::Cast(label)->AsArm());
}

void ArmVIXLJNIMacroAssembler::EmitExceptionPoll(
    ArmVIXLJNIMacroAssembler::ArmException* exception) {
  ___ Bind(exception->Entry());
  if (exception->stack_adjust_ != 0) {  // Fix up the frame.
    DecreaseFrameSize(exception->stack_adjust_);
  }

  vixl32::Register scratch = exception->scratch_;
  UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
  temps.Exclude(scratch);
  // Pass exception object as argument.
  // Don't care about preserving r0 as this won't return.
  ___ Mov(r0, scratch);
  ___ Ldr(lr,
          MemOperand(tr,
              QUICK_ENTRYPOINT_OFFSET(kArmPointerSize, pDeliverException).Int32Value()));
  ___ Blx(lr);
}

void ArmVIXLJNIMacroAssembler::MemoryBarrier(ManagedRegister scratch ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmVIXLJNIMacroAssembler::Load(ArmManagedRegister
                                    dest,
                                    vixl32::Register base,
                                    int32_t offset,
                                    size_t size) {
  if (dest.IsNoRegister()) {
    CHECK_EQ(0u, size) << dest;
  } else if (dest.IsCoreRegister()) {
    vixl::aarch32::Register dst = AsVIXLRegister(dest);
    CHECK(!dst.Is(sp)) << dest;

    UseScratchRegisterScope temps(asm_.GetVIXLAssembler());
    temps.Exclude(dst);

    if (size == 1u) {
      ___ Ldrb(dst, MemOperand(base, offset));
    } else {
      CHECK_EQ(4u, size) << dest;
      ___ Ldr(dst, MemOperand(base, offset));
    }
  } else if (dest.IsRegisterPair()) {
    CHECK_EQ(8u, size) << dest;
    ___ Ldr(AsVIXLRegisterPairLow(dest),  MemOperand(base, offset));
    ___ Ldr(AsVIXLRegisterPairHigh(dest), MemOperand(base, offset + 4));
  } else if (dest.IsSRegister()) {
    ___ Vldr(AsVIXLSRegister(dest), MemOperand(base, offset));
  } else {
    CHECK(dest.IsDRegister()) << dest;
    ___ Vldr(AsVIXLDRegister(dest), MemOperand(base, offset));
  }
}

}  // namespace arm
}  // namespace art
