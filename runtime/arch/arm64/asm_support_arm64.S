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

#ifndef ART_RUNTIME_ARCH_ARM64_ASM_SUPPORT_ARM64_S_
#define ART_RUNTIME_ARCH_ARM64_ASM_SUPPORT_ARM64_S_

#include "asm_support_arm64.h"

// Define special registers.

// Register holding Thread::Current().
#define xSELF x19
// Frame Pointer
#define xFP   x29
// Link Register
#define xLR   x30
// Define the intraprocedural linkage temporary registers.
#define xIP0 x16
#define wIP0 w16
#define xIP1 x17
#define wIP1 w17

#if defined(USE_READ_BARRIER) && defined(USE_BAKER_READ_BARRIER)
// Marking Register, holding Thread::Current()->GetIsGcMarking().
// Only used with the Concurrent Copying (CC) garbage
// collector, with the Baker read barrier configuration.
#define wMR w20
#endif

.macro ENTRY_ALIGNED name, alignment
    .type \name, #function
    .hidden \name  // Hide this as a global symbol, so we do not incur plt calls.
    .global \name
    // ART-compiled functions have OatQuickMethodHeader but assembly funtions do not.
    // Prefix the assembly code with 0xFFs, which means there is no method header.
    .byte 0xFF, 0xFF, 0xFF, 0xFF
    // Cache alignment for function entry.
    // NB: 0xFF because there is a bug in balign where 0x00 creates nop instructions.
    .balign \alignment, 0xFF
\name:
    .cfi_startproc
.endm

.macro ENTRY name
    ENTRY_ALIGNED \name, 16
.endm

.macro END name
    .cfi_endproc
    .size \name, .-\name
.endm

.macro UNIMPLEMENTED name
    ENTRY \name
    brk 0
    END \name
.endm

// Macro to poison (negate) the reference for heap poisoning.
.macro POISON_HEAP_REF rRef
#ifdef USE_HEAP_POISONING
    neg \rRef, \rRef
#endif  // USE_HEAP_POISONING
.endm

// Macro to unpoison (negate) the reference for heap poisoning.
.macro UNPOISON_HEAP_REF rRef
#ifdef USE_HEAP_POISONING
    neg \rRef, \rRef
#endif  // USE_HEAP_POISONING
.endm

.macro INCREASE_FRAME frame_adjustment
    sub sp, sp, #(\frame_adjustment)
    .cfi_adjust_cfa_offset (\frame_adjustment)
.endm

.macro DECREASE_FRAME frame_adjustment
    add sp, sp, #(\frame_adjustment)
    .cfi_adjust_cfa_offset -(\frame_adjustment)
.endm

.macro SAVE_REG reg, offset
    str \reg, [sp, #(\offset)]
    .cfi_rel_offset \reg, (\offset)
.endm

.macro RESTORE_REG reg, offset
    ldr \reg, [sp, #(\offset)]
    .cfi_restore \reg
.endm

.macro SAVE_TWO_REGS_BASE base, reg1, reg2, offset
    stp \reg1, \reg2, [\base, #(\offset)]
    .cfi_rel_offset \reg1, (\offset)
    .cfi_rel_offset \reg2, (\offset) + 8
.endm

.macro SAVE_TWO_REGS reg1, reg2, offset
    SAVE_TWO_REGS_BASE sp, \reg1, \reg2, \offset
.endm

.macro RESTORE_TWO_REGS_BASE base, reg1, reg2, offset
    ldp \reg1, \reg2, [\base, #(\offset)]
    .cfi_restore \reg1
    .cfi_restore \reg2
.endm

.macro RESTORE_TWO_REGS reg1, reg2, offset
    RESTORE_TWO_REGS_BASE sp, \reg1, \reg2, \offset
.endm

.macro LOAD_RUNTIME_INSTANCE reg
#if __has_feature(hwaddress_sanitizer) && __clang_major__ >= 10
    adrp xIP0, :pg_hi21_nc:_ZN3art7Runtime9instance_E
#else
    adrp xIP0, _ZN3art7Runtime9instance_E
#endif
    ldr xIP0, [xIP0, #:lo12:_ZN3art7Runtime9instance_E]
.endm

// Macro to refresh the Marking Register (W20).
//
// This macro must be called at the end of functions implementing
// entrypoints that possibly (directly or indirectly) perform a
// suspend check (before they return).
.macro REFRESH_MARKING_REGISTER
#if defined(USE_READ_BARRIER) && defined(USE_BAKER_READ_BARRIER)
    ldr wMR, [xSELF, #THREAD_IS_GC_MARKING_OFFSET]
#endif
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveRefsOnly).
     */
.macro SETUP_SAVE_REFS_ONLY_FRAME
    // art::Runtime* xIP0 = art::Runtime::instance_;
    // Our registers aren't intermixed - just spill in order.
    LOAD_RUNTIME_INSTANCE xIP0

    // ArtMethod* xIP0 = Runtime::instance_->callee_save_methods_[kSaveRefOnly];
    ldr xIP0, [xIP0, RUNTIME_SAVE_REFS_ONLY_METHOD_OFFSET]

    INCREASE_FRAME 96

    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_REFS_ONLY != 96)
#error "FRAME_SIZE_SAVE_REFS_ONLY(ARM64) size not as expected."
#endif

    // GP callee-saves.
    // x20 paired with ArtMethod* - see below.
    SAVE_TWO_REGS x21, x22, 16
    SAVE_TWO_REGS x23, x24, 32
    SAVE_TWO_REGS x25, x26, 48
    SAVE_TWO_REGS x27, x28, 64
    SAVE_TWO_REGS x29, xLR, 80

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveRefsOnly].
    // Note: We could avoid saving X20 in the case of Baker read
    // barriers, as it is overwritten by REFRESH_MARKING_REGISTER
    // later; but it's not worth handling this special case.
    stp xIP0, x20, [sp]
    .cfi_rel_offset x20, 8

    // Place sp in Thread::Current()->top_quick_frame.
    mov xIP0, sp
    str xIP0, [xSELF, # THREAD_TOP_QUICK_FRAME_OFFSET]
.endm

// TODO: Probably no need to restore registers preserved by aapcs64.
.macro RESTORE_SAVE_REFS_ONLY_FRAME
    // Callee-saves.
    // Note: Likewise, we could avoid restoring X20 in the case of Baker
    // read barriers, as it is overwritten by REFRESH_MARKING_REGISTER
    // later; but it's not worth handling this special case.
    RESTORE_REG x20, 8
    RESTORE_TWO_REGS x21, x22, 16
    RESTORE_TWO_REGS x23, x24, 32
    RESTORE_TWO_REGS x25, x26, 48
    RESTORE_TWO_REGS x27, x28, 64
    RESTORE_TWO_REGS x29, xLR, 80

    DECREASE_FRAME 96
.endm

.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL base
    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_REFS_AND_ARGS != 224)
#error "FRAME_SIZE_SAVE_REFS_AND_ARGS(ARM64) size not as expected."
#endif

    // Stack alignment filler [\base, #8].
    // FP args.
    stp d0, d1, [\base, #16]
    stp d2, d3, [\base, #32]
    stp d4, d5, [\base, #48]
    stp d6, d7, [\base, #64]

    // Core args.
    SAVE_TWO_REGS_BASE \base, x1, x2, 80
    SAVE_TWO_REGS_BASE \base, x3, x4, 96
    SAVE_TWO_REGS_BASE \base, x5, x6, 112

    // x7, Callee-saves.
    // Note: We could avoid saving X20 in the case of Baker read
    // barriers, as it is overwritten by REFRESH_MARKING_REGISTER
    // later; but it's not worth handling this special case.
    SAVE_TWO_REGS_BASE \base, x7, x20, 128
    SAVE_TWO_REGS_BASE \base, x21, x22, 144
    SAVE_TWO_REGS_BASE \base, x23, x24, 160
    SAVE_TWO_REGS_BASE \base, x25, x26, 176
    SAVE_TWO_REGS_BASE \base, x27, x28, 192

    // x29(callee-save) and LR.
    SAVE_TWO_REGS_BASE \base, x29, xLR, 208
.endm

// TODO: Probably no need to restore registers preserved by aapcs64. (That would require
// auditing all users to make sure they restore aapcs64 callee-save registers they clobber.)
.macro RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL base
    // FP args.
    ldp d0, d1, [\base, #16]
    ldp d2, d3, [\base, #32]
    ldp d4, d5, [\base, #48]
    ldp d6, d7, [\base, #64]

    // Core args.
    RESTORE_TWO_REGS_BASE \base, x1, x2, 80
    RESTORE_TWO_REGS_BASE \base, x3, x4, 96
    RESTORE_TWO_REGS_BASE \base, x5, x6, 112

    // x7, Callee-saves.
    // Note: Likewise, we could avoid restoring X20 in the case of Baker
    // read barriers, as it is overwritten by REFRESH_MARKING_REGISTER
    // later; but it's not worth handling this special case.
    RESTORE_TWO_REGS_BASE \base, x7, x20, 128
    RESTORE_TWO_REGS_BASE \base, x21, x22, 144
    RESTORE_TWO_REGS_BASE \base, x23, x24, 160
    RESTORE_TWO_REGS_BASE \base, x25, x26, 176
    RESTORE_TWO_REGS_BASE \base, x27, x28, 192

    // x29(callee-save) and LR.
    RESTORE_TWO_REGS_BASE \base, x29, xLR, 208
.endm

.macro RESTORE_SAVE_REFS_AND_ARGS_FRAME
    RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL sp
    DECREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveAllCalleeSaves)
     */
.macro SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    // art::Runtime* xIP0 = art::Runtime::instance_;
    // Our registers aren't intermixed - just spill in order.
    LOAD_RUNTIME_INSTANCE xIP0

    // ArtMethod* xIP0 = Runtime::instance_->callee_save_methods_[kSaveAllCalleeSaves];
    ldr xIP0, [xIP0, RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET]

    INCREASE_FRAME 176

    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_ALL_CALLEE_SAVES != 176)
#error "FRAME_SIZE_SAVE_ALL_CALLEE_SAVES(ARM64) size not as expected."
#endif

    // Stack alignment filler [sp, #8].
    // FP callee-saves.
    stp d8, d9,   [sp, #16]
    stp d10, d11, [sp, #32]
    stp d12, d13, [sp, #48]
    stp d14, d15, [sp, #64]

    // GP callee-saves
    SAVE_TWO_REGS x19, x20, 80
    SAVE_TWO_REGS x21, x22, 96
    SAVE_TWO_REGS x23, x24, 112
    SAVE_TWO_REGS x25, x26, 128
    SAVE_TWO_REGS x27, x28, 144
    SAVE_TWO_REGS x29, xLR, 160

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveAllCalleeSaves].
    str xIP0, [sp]
    // Place sp in Thread::Current()->top_quick_frame.
    mov xIP0, sp
    str xIP0, [xSELF, # THREAD_TOP_QUICK_FRAME_OFFSET]
.endm

    /*
     * Macro that calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_ when the runtime method frame is ready.
     */
.macro DELIVER_PENDING_EXCEPTION_FRAME_READY
    mov x0, xSELF

    // Point of no return.
    bl artDeliverPendingExceptionFromCode  // artDeliverPendingExceptionFromCode(Thread*)
    brk 0  // Unreached
.endm

    /*
     * Macro that calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_.
     */
.macro DELIVER_PENDING_EXCEPTION
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    DELIVER_PENDING_EXCEPTION_FRAME_READY
.endm

.macro RETURN_OR_DELIVER_PENDING_EXCEPTION_REG reg
    ldr \reg, [xSELF, # THREAD_EXCEPTION_OFFSET]   // Get exception field.
    cbnz \reg, 1f
    ret
1:
    DELIVER_PENDING_EXCEPTION
.endm

.macro RETURN_OR_DELIVER_PENDING_EXCEPTION
    RETURN_OR_DELIVER_PENDING_EXCEPTION_REG xIP0
.endm

#endif  // ART_RUNTIME_ARCH_ARM64_ASM_SUPPORT_ARM64_S_
