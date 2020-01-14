/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "art_method-inl.h"
#include "dex/code_item_accessors.h"
#include "entrypoints/quick/callee_save_frame.h"
#include "interpreter/interpreter_mterp_impl.h"
#include "nterp_helpers.h"
#include "oat_quick_method_header.h"
#include "quick/quick_method_frame_info.h"

namespace art {

/**
 * An nterp frame follows the optimizing compiler's ABI conventions, with
 * int/long/reference parameters being passed in core registers / stack and
 * float/double parameters being passed in floating point registers / stack.
 *
 * There are no ManagedStack transitions between compiler and nterp frames.
 *
 * On entry, nterp will copy its parameters to a dex register array allocated on
 * the stack. There is a fast path when calling from nterp to nterp to not
 * follow the ABI but just copy the parameters from the caller's dex registers
 * to the callee's dex registers.
 *
 * The stack layout of an nterp frame is:
 *    ----------------
 *    |              |      All callee save registers of the platform
 *    | callee-save  |      (core and floating point).
 *    | registers    |      On x86 and x64 this includes the return address,
 *    |              |      already spilled on entry.
 *    ----------------
 *    |  alignment   |      Stack aligment of kStackAlignment.
 *    ----------------
 *    |              |      Contains `registers_size` entries (of size 4) from
 *    |    dex       |      the code item information of the method.
 *    |  registers   |
 *    |              |
 *    ----------------
 *    |              |      A copy of the dex registers above, but only
 *    |  reference   |      containing references, used for GC.
 *    |  registers   |
 *    |              |
 *    ----------------
 *    |  caller fp   |      Frame pointer of caller. Stored below the reference
 *    ----------------      registers array for easy access from nterp when returning.
 *    |  dex_pc_ptr  |      Pointer to the dex instruction being executed.
 *    ----------------      Stored whenever nterp goes into the runtime.
 *    |              |      In case nterp calls compiled code, we reserve space
 *    |     out      |      for out registers. This space will be used for
 *    |   registers  |      arguments passed on stack.
 *    |              |
 *    ----------------
 *    |  ArtMethod*  |      The method being currently executed.
 *    ----------------
 *
 *    Exception handling:
 *    Nterp follows the same convention than the compiler,
 *    with the addition of:
 *    - All catch handlers have the same landing pad.
 *    - Before doing the longjmp for exception delivery, the register containing the
 *      dex PC pointer must be updated.
 *
 *    Stack walking:
 *    An nterp frame is walked like a compiled code frame. We add an
 *    OatQuickMethodHeader prefix to the nterp entry point, which contains:
 *    - vmap_table_offset=0 (nterp doesn't need one).
 *    - code_size=NterpEnd-NterpStart
 */

static constexpr size_t kPointerSize = static_cast<size_t>(kRuntimePointerSize);

static constexpr size_t NterpGetFrameEntrySize() {
  uint32_t core_spills =
      RuntimeCalleeSaveFrame::GetCoreSpills(CalleeSaveType::kSaveAllCalleeSaves);
  uint32_t fp_spills =
      RuntimeCalleeSaveFrame::GetFpSpills(CalleeSaveType::kSaveAllCalleeSaves);
  // Note: the return address is considered part of the callee saves.
  return (POPCOUNT(core_spills) + POPCOUNT(fp_spills)) * kPointerSize;
}

size_t NterpGetFrameSize(ArtMethod* method) {
  CodeItemDataAccessor accessor(method->DexInstructionData());
  const uint16_t num_regs = accessor.RegistersSize();
  const uint16_t out_regs = accessor.OutsSize();

  size_t frame_size =
      NterpGetFrameEntrySize() +
      (num_regs * kVRegSize) * 2 +  // dex registers and reference registers
      kPointerSize +  // previous frame
      kPointerSize +  // saved dex pc
      (out_regs * kVRegSize) +  // out arguments
      kPointerSize;  // method
  return RoundUp(frame_size, kStackAlignment);
}

QuickMethodFrameInfo NterpFrameInfo(ArtMethod** frame) {
  uint32_t core_spills =
      RuntimeCalleeSaveFrame::GetCoreSpills(CalleeSaveType::kSaveAllCalleeSaves);
  uint32_t fp_spills =
      RuntimeCalleeSaveFrame::GetFpSpills(CalleeSaveType::kSaveAllCalleeSaves);
  return QuickMethodFrameInfo(NterpGetFrameSize(*frame), core_spills, fp_spills);
}

uintptr_t NterpGetRegistersArray(ArtMethod** frame) {
  CodeItemDataAccessor accessor((*frame)->DexInstructionData());
  const uint16_t num_regs = accessor.RegistersSize();
  // The registers array is just above the reference array.
  return NterpGetReferenceArray(frame) + (num_regs * kVRegSize);
}

uintptr_t NterpGetReferenceArray(ArtMethod** frame) {
  CodeItemDataAccessor accessor((*frame)->DexInstructionData());
  const uint16_t out_regs = accessor.OutsSize();
  // The references array is just above the saved frame pointer.
  return reinterpret_cast<uintptr_t>(frame) +
      kPointerSize +  // method
      (out_regs * kVRegSize) +  // out arguments
      kPointerSize +  // saved dex pc
      kPointerSize;  // previous frame.
}

uint32_t NterpGetDexPC(ArtMethod** frame) {
  CodeItemDataAccessor accessor((*frame)->DexInstructionData());
  const uint16_t out_regs = accessor.OutsSize();
  uintptr_t dex_pc_ptr = reinterpret_cast<uintptr_t>(frame) +
      kPointerSize +  // method
      (out_regs * kVRegSize);  // out arguments
  CodeItemInstructionAccessor instructions((*frame)->DexInstructions());
  return *reinterpret_cast<const uint16_t**>(dex_pc_ptr) - instructions.Insns();
}

uint32_t NterpGetVReg(ArtMethod** frame, uint16_t vreg) {
  return reinterpret_cast<uint32_t*>(NterpGetRegistersArray(frame))[vreg];
}

uint32_t NterpGetVRegReference(ArtMethod** frame, uint16_t vreg) {
  return reinterpret_cast<uint32_t*>(NterpGetReferenceArray(frame))[vreg];
}

uintptr_t NterpGetCatchHandler() {
  // Nterp uses the same landing pad for all exceptions. The dex_pc_ptr set before
  // longjmp will actually be used to jmp to the catch handler.
  return reinterpret_cast<uintptr_t>(artNterpAsmInstructionEnd);
}

}  // namespace art
