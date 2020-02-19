/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef ART_RUNTIME_ARCH_ARM_ASM_SUPPORT_ARM_S_
#define ART_RUNTIME_ARCH_ARM_ASM_SUPPORT_ARM_S_

#include "asm_support_arm.h"

// Define special registers.

// Register holding suspend check count down.
#define rSUSPEND r4
// Register holding Thread::Current().
#define rSELF r9

#if defined(USE_READ_BARRIER) && defined(USE_BAKER_READ_BARRIER)
// Marking Register, holding Thread::Current()->GetIsGcMarking().
// Only used with the Concurrent Copying (CC) garbage
// collector, with the Baker read barrier configuration.
#define rMR r8
#endif

.syntax unified
.arch armv7-a
.thumb

// Macro to generate the value of Runtime::Current into rDest. As it uses labels
// then the labels need to be unique. We bind these to the function name in the ENTRY macros.
.macro RUNTIME_CURRENT name, num, rDest
    .if .Lruntime_current\num\()_used
         .error
    .endif
    .set .Lruntime_current\num\()_used, 1
    ldr \rDest, .Lruntime_instance_\name\()_\num  @ Load GOT_PREL offset of Runtime::instance_.
.Lload_got_\name\()_\num\():
    add \rDest, pc                                @ Fixup GOT_PREL address.
    ldr \rDest, [\rDest]                          @ Load address of Runtime::instance_.
    ldr \rDest, [\rDest]                          @ Load Runtime::instance_.
.endm

// Common ENTRY declaration code for ARM and thumb, an ENTRY should always be paired with an END.
// Declares the RUNTIME_CURRENT[123] macros that can be used within an ENTRY and will have literals
// generated at END.
.macro DEF_ENTRY thumb_or_arm, name, alignment
    \thumb_or_arm
// Clang ignores .thumb_func and requires an explicit .thumb. Investigate whether we should still
// carry around the .thumb_func.
    .ifc \thumb_or_arm, .thumb_func
        .thumb
    .endif
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
    .fnstart
    // Track whether RUNTIME_CURRENT was used.
    .set .Lruntime_current1_used, 0
    .set .Lruntime_current2_used, 0
    .set .Lruntime_current3_used, 0
    // The RUNTIME_CURRENT macros that are bound to the \name argument of DEF_ENTRY to ensure
    // that label names are unique.
    .macro RUNTIME_CURRENT1 rDest
        RUNTIME_CURRENT \name, 1, \rDest
    .endm
    .macro RUNTIME_CURRENT2 rDest
        RUNTIME_CURRENT \name, 2, \rDest
    .endm
    .macro RUNTIME_CURRENT3 rDest
        RUNTIME_CURRENT \name, 3, \rDest
    .endm
.endm

// A thumb2 style ENTRY.
.macro ENTRY name
    DEF_ENTRY .thumb_func, \name, 16
.endm
.macro ENTRY_ALIGNED name, alignment
    DEF_ENTRY .thumb_func, \name, \alignment
.endm

// A ARM style ENTRY.
.macro ARM_ENTRY name
    DEF_ENTRY .arm, \name, 16
.endm

// Terminate an ENTRY and generate GOT_PREL references.
.macro END name
     // Generate offsets of GOT and Runtime::instance_ used in RUNTIME_CURRENT.
     .if .Lruntime_current1_used
         .Lruntime_instance_\name\()_1:
             .word   _ZN3art7Runtime9instance_E(GOT_PREL)-(.Lload_got_\name\()_1+4)
     .endif
     .if .Lruntime_current2_used
         .Lruntime_instance_\name\()_2:
             .word   _ZN3art7Runtime9instance_E(GOT_PREL)-(.Lload_got_\name\()_2+4)
    .endif
     .if .Lruntime_current3_used
         .Lruntime_instance_\name\()_3:
             .word   _ZN3art7Runtime9instance_E(GOT_PREL)-(.Lload_got_\name\()_3+4)
    .endif
    // Remove the RUNTIME_CURRENTx macros so they get rebound in the next function entry.
    .purgem RUNTIME_CURRENT1
    .purgem RUNTIME_CURRENT2
    .purgem RUNTIME_CURRENT3
    .fnend
    .cfi_endproc
    .size \name, .-\name
.endm

// Declare an unimplemented ENTRY that will halt a debugger.
.macro UNIMPLEMENTED name
    ENTRY \name
    bkpt
    bkpt
    END \name
.endm

// Macro to poison (negate) the reference for heap poisoning.
.macro POISON_HEAP_REF rRef
#ifdef USE_HEAP_POISONING
    rsb \rRef, \rRef, #0
#endif  // USE_HEAP_POISONING
.endm

// Macro to unpoison (negate) the reference for heap poisoning.
.macro UNPOISON_HEAP_REF rRef
#ifdef USE_HEAP_POISONING
    rsb \rRef, \rRef, #0
#endif  // USE_HEAP_POISONING
.endm

// Macro to refresh the Marking Register (R8).
//
// This macro must be called at the end of functions implementing
// entrypoints that possibly (directly or indirectly) perform a
// suspend check (before they return).
.macro REFRESH_MARKING_REGISTER
#if defined(USE_READ_BARRIER) && defined(USE_BAKER_READ_BARRIER)
    ldr rMR, [rSELF, #THREAD_IS_GC_MARKING_OFFSET]
#endif
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveRefsAndArgs), except for storing the method.
     */
.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_REGISTERS_ONLY
    // Note: We could avoid saving R8 in the case of Baker read
    // barriers, as it is overwritten by REFRESH_MARKING_REGISTER
    // later; but it's not worth handling this special case.
    push {r1-r3, r5-r8, r10-r11, lr}   @ 10 words of callee saves and args.
    .cfi_adjust_cfa_offset 40
    .cfi_rel_offset r1, 0
    .cfi_rel_offset r2, 4
    .cfi_rel_offset r3, 8
    .cfi_rel_offset r5, 12
    .cfi_rel_offset r6, 16
    .cfi_rel_offset r7, 20
    .cfi_rel_offset r8, 24
    .cfi_rel_offset r10, 28
    .cfi_rel_offset r11, 32
    .cfi_rel_offset lr, 36
    vpush {s0-s15}                     @ 16 words of float args.
    .cfi_adjust_cfa_offset 64
    sub sp, #8                         @ 2 words of space, alignment padding and Method*
    .cfi_adjust_cfa_offset 8
    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_REFS_AND_ARGS != 40 + 64 + 8)
#error "FRAME_SIZE_SAVE_REFS_AND_ARGS(ARM) size not as expected."
#endif
.endm

.macro RESTORE_SAVE_REFS_AND_ARGS_FRAME
    add  sp, #8                      @ rewind sp
    .cfi_adjust_cfa_offset -8
    vpop {s0-s15}
    .cfi_adjust_cfa_offset -64
    // Note: Likewise, we could avoid restoring R8 in the case of Baker
    // read barriers, as it is overwritten by REFRESH_MARKING_REGISTER
    // later; but it's not worth handling this special case.
    pop {r1-r3, r5-r8, r10-r11, lr}  @ 10 words of callee saves and args.
    .cfi_restore r1
    .cfi_restore r2
    .cfi_restore r3
    .cfi_restore r5
    .cfi_restore r6
    .cfi_restore r7
    .cfi_restore r8
    .cfi_restore r10
    .cfi_restore r11
    .cfi_restore lr
    .cfi_adjust_cfa_offset -40
.endm

    /*
     * Macro to spill the GPRs.
     */
.macro SPILL_ALL_CALLEE_SAVE_GPRS
    push {r4-r11, lr}                             @ 9 words (36 bytes) of callee saves.
    .cfi_adjust_cfa_offset 36
    .cfi_rel_offset r4, 0
    .cfi_rel_offset r5, 4
    .cfi_rel_offset r6, 8
    .cfi_rel_offset r7, 12
    .cfi_rel_offset r8, 16
    .cfi_rel_offset r9, 20
    .cfi_rel_offset r10, 24
    .cfi_rel_offset r11, 28
    .cfi_rel_offset lr, 32
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveAllCalleeSaves)
     */
.macro SETUP_SAVE_ALL_CALLEE_SAVES_FRAME rTemp
    SPILL_ALL_CALLEE_SAVE_GPRS                    @ 9 words (36 bytes) of callee saves.
    vpush {s16-s31}                               @ 16 words (64 bytes) of floats.
    .cfi_adjust_cfa_offset 64
    sub sp, #12                                   @ 3 words of space, bottom word will hold Method*
    .cfi_adjust_cfa_offset 12
    RUNTIME_CURRENT1 \rTemp                       @ Load Runtime::Current into rTemp.
    @ Load kSaveAllCalleeSaves Method* into rTemp.
    ldr \rTemp, [\rTemp, #RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET]
    str \rTemp, [sp, #0]                          @ Place Method* at bottom of stack.
    str sp, [rSELF, #THREAD_TOP_QUICK_FRAME_OFFSET]  @ Place sp in Thread::Current()->top_quick_frame.

     // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_ALL_CALLEE_SAVES != 36 + 64 + 12)
#error "FRAME_SIZE_SAVE_ALL_CALLEE_SAVES(ARM) size not as expected."
#endif
.endm

    /*
     * Macro that calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_ when the runtime method frame is ready.
     */
.macro DELIVER_PENDING_EXCEPTION_FRAME_READY
    mov    r0, rSELF                           @ pass Thread::Current
    bl     artDeliverPendingExceptionFromCode  @ artDeliverPendingExceptionFromCode(Thread*)
.endm

    /*
     * Macro that calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_.
     */
.macro DELIVER_PENDING_EXCEPTION
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME r0       @ save callee saves for throw
    DELIVER_PENDING_EXCEPTION_FRAME_READY
.endm

#endif  // ART_RUNTIME_ARCH_X86_ASM_SUPPORT_X86_S_
