%def header():
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

/*
 * This is a #include, not a %include, because we want the C pre-processor
 * to expand the macros into assembler assignment statements.
 */
#include "asm_support.h"
#include "arch/arm64/asm_support_arm64.S"
#include "interpreter/cfi_asm_support.h"

/**
 * ARM64 Runtime register usage conventions.
 *
 *   r0     : w0 is 32-bit return register and x0 is 64-bit.
 *   r0-r7  : Argument registers.
 *   r8-r15 : Caller save registers (used as temporary registers).
 *   r16-r17: Also known as ip0-ip1, respectively. Used as scratch registers by
 *            the linker, by the trampolines and other stubs (the compiler uses
 *            these as temporary registers).
 *   r18    : Reserved for platform (SCS, shadow call stack)
 *   r19    : Pointer to thread-local storage.
 *   r20-r29: Callee save registers.
 *   r30    : (lr) is reserved (the link register).
 *   rsp    : (sp) is reserved (the stack pointer).
 *   rzr    : (zr) is reserved (the zero register).
 *
 *   Floating-point registers
 *   v0-v31
 *
 *   v0     : s0 is return register for singles (32-bit) and d0 for doubles (64-bit).
 *            This is analogous to the C/C++ (hard-float) calling convention.
 *   v0-v7  : Floating-point argument registers in both Dalvik and C/C++ conventions.
 *            Also used as temporary and codegen scratch registers.
 *
 *   v0-v7 and v16-v31 : Caller save registers (used as temporary registers).
 *   v8-v15 : bottom 64-bits preserved across C calls (d8-d15 are preserved).
 *
 *   v16-v31: Used as codegen temp/scratch.
 *   v8-v15 : Can be used for promotion.
 *
 *   Must maintain 16-byte stack alignment.
 *
 * Nterp notes:
 *
 * The following registers have fixed assignments:
 *
 *   reg nick      purpose
 *   x19  xSELF     self (Thread) pointer
 *   x20  wMR       marking register
 *   x29  xFP       interpreted frame pointer, used for accessing locals and args
 *   x22  xPC       interpreted program counter, used for fetching instructions
 *   x23  xINST     first 16-bit code unit of current instruction
 *   x24  xIBASE    interpreted instruction base pointer, used for computed goto
 *   x25  xREFS     base of object references of dex registers.
 *   x16  ip        scratch reg
 *   x17  ip2       scratch reg (used by macros)
 *
 * Macros are provided for common operations.  They MUST NOT alter unspecified registers or
 * condition codes.
*/

/* single-purpose registers, given names for clarity */
#define xSELF    x19
#define CFI_DEX  22 // DWARF register number of the register holding dex-pc (xPC).
#define CFI_TMP  0  // DWARF register number of the first argument register (r0).
#define xPC      x22
#define xINST    x23
#define wINST    w23
#define xIBASE   x24
#define xREFS    x25
#define CFI_REFS 25
#define ip       x16
#define ip2      x17
#define wip      w16
#define wip2     w17

// To avoid putting ifdefs arond the use of wMR, make sure it's defined.
// IsNterpSupported returns false for configurations that don't have wMR (typically CMS).
#ifndef wMR
#define wMR w20
#endif

// Temporary registers while setting up a frame.
#define xNEW_FP   x26
#define xNEW_REFS x27
#define CFI_NEW_REFS 27

// +8 for the ArtMethod of the caller.
#define OFFSET_TO_FIRST_ARGUMENT_IN_STACK (CALLEE_SAVES_SIZE + 8)

/*
 * Fetch the next instruction from xPC into wINST.  Does not advance xPC.
 */
.macro FETCH_INST
    ldrh    wINST, [xPC]
.endm

/*
 * Fetch the next instruction from the specified offset.  Advances xPC
 * to point to the next instruction.  "count" is in 16-bit code units.
 *
 * Because of the limited size of immediate constants on ARM, this is only
 * suitable for small forward movements (i.e. don't try to implement "goto"
 * with this).
 *
 * This must come AFTER anything that can throw an exception, or the
 * exception catch may miss.  (This also implies that it must come after
 * EXPORT_PC.)
 */
.macro FETCH_ADVANCE_INST count
    ldrh    wINST, [xPC, #((\count)*2)]!
.endm

/*
 * Similar to FETCH_ADVANCE_INST, but does not update xPC.  Used to load
 * xINST ahead of possible exception point.  Be sure to manually advance xPC
 * later.
 */
.macro PREFETCH_INST count
    ldrh    wINST, [xPC, #((\count)*2)]
.endm

/* Advance xPC by some number of code units. */
.macro ADVANCE count
  add  xPC, xPC, #((\count)*2)
.endm

/*
 * Fetch the next instruction from an offset specified by "reg" and advance xPC.
 * xPC to point to the next instruction.  "reg" must specify the distance
 * in bytes, *not* 16-bit code units, and may be a signed value.
 *
 */
.macro FETCH_ADVANCE_INST_RB reg
    add     xPC, xPC, \reg, sxtw
    ldrh    wINST, [xPC]
.endm

/*
 * Fetch a half-word code unit from an offset past the current PC.  The
 * "count" value is in 16-bit code units.  Does not advance xPC.
 *
 * The "_S" variant works the same but treats the value as signed.
 */
.macro FETCH reg, count
    ldrh    \reg, [xPC, #((\count)*2)]
.endm

.macro FETCH_S reg, count
    ldrsh   \reg, [xPC, #((\count)*2)]
.endm

/*
 * Fetch one byte from an offset past the current PC.  Pass in the same
 * "count" as you would for FETCH, and an additional 0/1 indicating which
 * byte of the halfword you want (lo/hi).
 */
.macro FETCH_B reg, count, byte
    ldrb     \reg, [xPC, #((\count)*2+(\byte))]
.endm

/*
 * Put the instruction's opcode field into the specified register.
 */
.macro GET_INST_OPCODE reg
    and     \reg, xINST, #255
.endm

/*
 * Begin executing the opcode in _reg.  Clobbers reg
 */

.macro GOTO_OPCODE reg
    add     \reg, xIBASE, \reg, lsl #${handler_size_bits}
    br      \reg
.endm

/*
 * Get/set the 32-bit value from a Dalvik register.
 */
.macro GET_VREG reg, vreg
    ldr     \reg, [xFP, \vreg, uxtw #2]
.endm
.macro GET_VREG_OBJECT reg, vreg
    ldr     \reg, [xREFS, \vreg, uxtw #2]
.endm
.macro SET_VREG reg, vreg
    str     \reg, [xFP, \vreg, uxtw #2]
    str     wzr, [xREFS, \vreg, uxtw #2]
.endm
.macro SET_VREG_OBJECT reg, vreg, tmpreg
    str     \reg, [xFP, \vreg, uxtw #2]
    str     \reg, [xREFS, \vreg, uxtw #2]
.endm
.macro SET_VREG_FLOAT reg, vreg
    str     \reg, [xFP, \vreg, uxtw #2]
    str     wzr, [xREFS, \vreg, uxtw #2]
.endm

/*
 * Get/set the 64-bit value from a Dalvik register.
 */
.macro GET_VREG_WIDE reg, vreg
    add     ip2, xFP, \vreg, uxtw #2
    ldr     \reg, [ip2]
.endm
.macro SET_VREG_WIDE reg, vreg
    add     ip2, xFP, \vreg, uxtw #2
    str     \reg, [ip2]
    add     ip2, xREFS, \vreg, uxtw #2
    str     xzr, [ip2]
.endm
.macro GET_VREG_DOUBLE reg, vreg
    add     ip2, xFP, \vreg, uxtw #2
    ldr     \reg, [ip2]
.endm
.macro SET_VREG_DOUBLE reg, vreg
    add     ip2, xFP, \vreg, uxtw #2
    str     \reg, [ip2]
    add     ip2, xREFS, \vreg, uxtw #2
    str     xzr, [ip2]
.endm

/*
 * Get the 32-bit value from a Dalvik register and sign-extend to 64-bit.
 * Used to avoid an extra instruction in int-to-long.
 */
.macro GET_VREG_S reg, vreg
    ldrsw   \reg, [xFP, \vreg, uxtw #2]
.endm

// An assembly entry that has a OatQuickMethodHeader prefix.
.macro OAT_ENTRY name, end
    .type \name, #function
    .hidden \name
    .global \name
    .balign 16
    // Padding of 8 bytes to get 16 bytes alignment of code entry.
    .long 0
    .long 0
    // OatQuickMethodHeader.
    .long 0
    .long (\end - \name)
\name:
.endm

.macro SIZE name
    .size \name, .-\name
.endm

.macro NAME_START name
    .type \name, #function
    .hidden \name  // Hide this as a global symbol, so we do not incur plt calls.
    .global \name
    /* Cache alignment for function entry */
    .balign 16
\name:
.endm

.macro NAME_END name
  SIZE \name
.endm

// Macro for defining entrypoints into runtime. We don't need to save registers
// (we're not holding references there), but there is no
// kDontSave runtime method. So just use the kSaveRefsOnly runtime method.
.macro NTERP_TRAMPOLINE name, helper
ENTRY \name
  SETUP_SAVE_REFS_ONLY_FRAME
  bl \helper
  RESTORE_SAVE_REFS_ONLY_FRAME
  REFRESH_MARKING_REGISTER
  RETURN_OR_DELIVER_PENDING_EXCEPTION
END \name
.endm

.macro CLEAR_STATIC_VOLATILE_MARKER reg
  and \reg, \reg, #-2
.endm

.macro CLEAR_INSTANCE_VOLATILE_MARKER reg
  neg \reg, \reg
.endm

.macro EXPORT_PC
    str    xPC, [xREFS, #-16]
.endm

.macro BRANCH
    // Update method counter and do a suspend check if the branch is negative.
    tbnz wINST, #31, 2f
1:
    add w2, wINST, wINST                // w2<- byte offset
    FETCH_ADVANCE_INST_RB w2            // update xPC, load wINST
    GET_INST_OPCODE ip                  // extract opcode from wINST
    GOTO_OPCODE ip                      // jump to next instruction
2:
    ldr x0, [sp]
    ldrh w2, [x0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
    add x2, x2, #1
    and w2, w2, #0xffff
    strh w2, [x0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
    // If the counter overflows, handle this in the runtime.
    cbz w2, NterpHandleHotnessOverflow
    // Otherwise, do a suspend check.
    ldr x0, [xSELF, #THREAD_FLAGS_OFFSET]
    ands x0, x0, #THREAD_SUSPEND_OR_CHECKPOINT_REQUEST
    b.eq 1b
    EXPORT_PC
    bl    art_quick_test_suspend
    b 1b
.endm

// Setup the stack to start executing the method. Expects:
// - x0 to contain the ArtMethod
//
// Outputs
// - ip contains the dex registers size
// - x13 contains the old stack pointer.
//
// Uses ip, ip2, x13, x14 as temporaries.
.macro SETUP_STACK_FRAME code_item, refs, fp, cfi_refs
    // Fetch dex register size.
    ldrh wip, [\code_item, #CODE_ITEM_REGISTERS_SIZE_OFFSET]
    // Fetch outs size.
    ldrh wip2, [\code_item, #CODE_ITEM_OUTS_SIZE_OFFSET]

    // Compute required frame size: ((2 * ip) + ip1) * 4 + 24
    // 24 is for saving the previous frame, pc, and method being executed.
    add x14, ip, ip
    add x14, x14, ip2
    lsl x14, x14, #2
    add x14, x14, #24

    // Compute new stack pointer in x14
    sub x14, sp, x14
    // Alignment
    and x14, x14, #-16

    // Set reference and dex registers, align to pointer size for previous frame and dex pc.
    add \refs, x14, ip2, lsl #2
    add \refs, \refs, 28
    and \refs, \refs, #(-__SIZEOF_POINTER__)
    add \fp, \refs, ip, lsl #2

    // Now setup the stack pointer.
    mov x13, sp
    .cfi_def_cfa_register x13
    mov sp, x14
    str x13, [\refs, #-8]
    CFI_DEFINE_CFA_DEREF(\cfi_refs, -8, CALLEE_SAVES_SIZE)

    // Put nulls in reference frame.
    cbz ip, 2f
    mov ip2, \refs
1:
    str xzr, [ip2], #8  // May clear vreg[0].
    cmp ip2, \fp
    b.lo 1b
2:
    // Save the ArtMethod.
    str x0, [sp]
.endm

// Increase method hotness and do suspend check before starting executing the method.
.macro START_EXECUTING_INSTRUCTIONS
    ldr x0, [sp]
    ldrh w2, [x0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
    add x2, x2, #1
    and w2, w2, #0xffff
    strh w2, [x0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
    // If the counter overflows, handle this in the runtime.
    cbz w2, 2f
    ldr x0, [xSELF, #THREAD_FLAGS_OFFSET]
    tst x0, #THREAD_SUSPEND_OR_CHECKPOINT_REQUEST
    b.ne 3f
1:
    FETCH_INST
    GET_INST_OPCODE ip
    GOTO_OPCODE ip
2:
    mov x1, xzr
    mov x2, xFP
    bl nterp_hot_method
    b 1b
3:
    EXPORT_PC
    bl art_quick_test_suspend
    b 1b
.endm

.macro SPILL_ALL_CALLEE_SAVES
    INCREASE_FRAME CALLEE_SAVES_SIZE
    // Note: we technically don't need to save x19 and x20,
    // but the runtime will expect those values to be there when unwinding
    // (see Arm64Context::DoLongJump checking for the thread register).
    SAVE_ALL_CALLEE_SAVES 0
.endm

.macro RESTORE_ALL_CALLEE_SAVES
    // FP callee-saves
    ldp d8, d9, [sp, #0]
    ldp d10, d11, [sp, #16]
    ldp d12, d13, [sp, #32]
    ldp d14, d15, [sp, #48]

    // GP callee-saves.
    // No need to restore x19 (it's always the thread), and
    // don't restore x20 (the marking register) as it may have been updated.
    RESTORE_TWO_REGS x21, x22, 80
    RESTORE_TWO_REGS x23, x24, 96
    RESTORE_TWO_REGS x25, x26, 112
    RESTORE_TWO_REGS x27, x28, 128
    RESTORE_TWO_REGS x29, lr, 144

    DECREASE_FRAME CALLEE_SAVES_SIZE
.endm

.macro SPILL_ALL_ARGUMENTS
    INCREASE_FRAME 128
    // GP arguments.
    SAVE_TWO_REGS x0, x1, 0
    SAVE_TWO_REGS x2, x3, 16
    SAVE_TWO_REGS x4, x5, 32
    SAVE_TWO_REGS x6, x7, 48

    // FP arguments
    stp d0, d1, [sp, #64]
    stp d2, d3, [sp, #80]
    stp d4, d5, [sp, #96]
    stp d6, d7, [sp, #112]
.endm

.macro RESTORE_ALL_ARGUMENTS
    // GP arguments.
    RESTORE_TWO_REGS x0, x1, 0
    RESTORE_TWO_REGS x2, x3, 16
    RESTORE_TWO_REGS x4, x5, 32
    RESTORE_TWO_REGS x6, x7, 48

    // FP arguments
    ldp d0, d1, [sp, #64]
    ldp d2, d3, [sp, #80]
    ldp d4, d5, [sp, #96]
    ldp d6, d7, [sp, #112]
    DECREASE_FRAME 128
.endm

// Helper to setup the stack after doing a nterp to nterp call. This will setup:
// - xNEW_FP: the new pointer to dex registers
// - xNEW_REFS: the new pointer to references
// - xPC: the new PC pointer to execute
// - x2: value in instruction to decode the number of arguments.
// - x3: first dex register
// - x4: top of dex register array
//
// The method expects:
// - x0 to contain the ArtMethod
// - x8 to contain the code item
.macro SETUP_STACK_FOR_INVOKE
   // We do the same stack overflow check as the compiler. See CanMethodUseNterp
   // in how we limit the maximum nterp frame size.
   sub x16, sp, #STACK_OVERFLOW_RESERVED_BYTES
   ldr wzr, [x16]

   // Spill all callee saves to have a consistent stack frame whether we
   // are called by compiled code or nterp.
   SPILL_ALL_CALLEE_SAVES

   // Setup the frame.
   SETUP_STACK_FRAME x8, xNEW_REFS, xNEW_FP, CFI_NEW_REFS
   // Make x4 point to the top of the dex register array.
   add x4, xNEW_FP, ip, uxtx #2

   // Fetch instruction information before replacing xPC.
   // TODO: move this down to the method that uses it, fetching it directly from wINST.
   FETCH_B w2, 0, 1
   // TODO: we could avoid this as instance invokes already fetch it.
   FETCH w3, 2

   // Set the dex pc pointer.
   add xPC, x8, #CODE_ITEM_INSNS_OFFSET
   CFI_DEFINE_DEX_PC_WITH_OFFSET(CFI_TMP, CFI_DEX, 0)
.endm

// Setup arguments based on a non-range nterp to nterp call, and start executing
// the method. We expect:
// - xNEW_FP: the new pointer to dex registers
// - xNEW_REFS: the new pointer to references
// - xPC: the new PC pointer to execute
// - x2: number of arguments (bits 4-7), 5th argument if any (bits 0-3)
// - x3: first dex register
// - x4: top of dex register array
// - x1: receiver if non-static.
.macro SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=0
   // /* op vA, vB, {vC...vG} */
   asr ip2, x2, #4
   cbz ip2, 6f
   mov ip, #-4
   cmp ip2, #2
   b.lt 1f
   b.eq 2f
   cmp ip2, #4
   b.lt 3f
   b.eq 4f

  // We use a decrementing ip to store references relative
  // to xNEW_FP and dex registers relative to x4
  //
  // TODO: We could set up ip as the number of registers (this can be an additional output from
  // SETUP_STACK_FOR_INVOKE) and then just decrement it by one before copying each arg.
  // Maybe even introduce macros NEW_VREG_ADDRESS/NEW_VREG_REF_ADDRESS.
5:
   and         x2, x2, #15
   GET_VREG_OBJECT w5, w2
   str         w5, [xNEW_FP, ip]
   GET_VREG    w5, w2
   str         w5, [x4, ip]
   sub         ip, ip, #4
4:
   asr         x2, x3, #12
   GET_VREG_OBJECT w5, w2
   str         w5, [xNEW_FP, ip]
   GET_VREG    w5, w2
   str         w5, [x4, ip]
   sub         ip, ip, #4
3:
   ubfx        x2, x3, #8, #4
   GET_VREG_OBJECT w5, w2
   str         w5, [xNEW_FP, ip]
   GET_VREG    w5, w2
   str         w5, [x4, ip]
   sub         ip, ip, #4
2:
   ubfx        x2, x3, #4, #4
   GET_VREG_OBJECT w5, w2
   str         w5, [xNEW_FP, ip]
   GET_VREG    w5, w2
   str         w5, [x4, ip]
   .if !\is_string_init
   sub         ip, ip, #4
   .endif
1:
   .if \is_string_init
   // Ignore the first argument
   .elseif \is_static
   and         x2, x3, #0xf
   GET_VREG_OBJECT w5, w2
   str         w5, [xNEW_FP, ip]
   GET_VREG    w5, w2
   str         w5, [x4, ip]
   .else
   str         w1, [xNEW_FP, ip]
   str         w1, [x4, ip]
   .endif

6:
   // Start executing the method.
   mov xFP, xNEW_FP
   mov xREFS, xNEW_REFS
   CFI_DEFINE_CFA_DEREF(CFI_REFS, -8, CALLEE_SAVES_SIZE)
   START_EXECUTING_INSTRUCTIONS
.endm

// Setup arguments based on a range nterp to nterp call, and start executing
// the method.
// - xNEW_FP: the new pointer to dex registers
// - xNEW_REFS: the new pointer to references
// - xPC: the new PC pointer to execute
// - x2: number of arguments
// - x3: first dex register
// - x4: top of dex register array
// - x1: receiver if non-static.
//
// Uses ip, ip2, x5, x6 as temporaries.
.macro SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=0
   mov ip, #-4
   .if \is_string_init
   // Ignore the first argument
   sub x2, x2, #1
   add x3, x3, #1
   .elseif !\is_static
   sub x2, x2, #1
   add x3, x3, #1
   .endif

   cbz x2, 2f
   add ip2, xREFS, x3, lsl #2  // pointer to first argument in reference array
   add ip2, ip2, x2, lsl #2    // pointer to last argument in reference array
   add x5, xFP, x3, lsl #2     // pointer to first argument in register array
   add x6, x5, x2, lsl #2      // pointer to last argument in register array
1:
   ldr  w7, [ip2, #-4]!
   str  w7, [xNEW_FP, ip]
   sub  x2, x2, 1
   ldr  w7, [x6, #-4]!
   str  w7, [x4, ip]
   sub ip, ip, 4
   cbnz x2, 1b
2:
   .if \is_string_init
   // Ignore first argument
   .elseif !\is_static
   str w1, [xNEW_FP, ip]
   str w1, [x4, ip]
   .endif
   mov xFP, xNEW_FP
   mov xREFS, xNEW_REFS
   CFI_DEFINE_CFA_DEREF(CFI_REFS, -8, CALLEE_SAVES_SIZE)
   START_EXECUTING_INSTRUCTIONS
.endm

.macro GET_SHORTY dest, is_interface, is_polymorphic, is_custom
   stp x0, x1, [sp, #-16]!
   .if \is_polymorphic
   ldr x0, [sp, #16]
   mov x1, xPC
   bl NterpGetShortyFromInvokePolymorphic
   .elseif \is_custom
   ldr x0, [sp, #16]
   mov x1, xPC
   bl NterpGetShortyFromInvokeCustom
   .elseif \is_interface
   ldr x0, [sp, #16]
   FETCH w1, 1
   bl NterpGetShortyFromMethodId
   .else
   bl NterpGetShorty
   .endif
   mov \dest, x0
   ldp x0, x1, [sp], #16
.endm

// Output: x8 contains the code item
.macro GET_CODE_ITEM
   // TODO: Get code item in a better way.
   stp x0, x1, [sp, #-16]!
   bl NterpGetCodeItem
   mov x8, x0
   ldp x0, x1, [sp], #16
.endm

.macro DO_ENTRY_POINT_CHECK call_compiled_code
   // On entry, the method is x0, the instance is x1
   adr x2, ExecuteNterpImpl
   ldr x3, [x0, #ART_METHOD_QUICK_CODE_OFFSET_64]
   cmp x2, x3
   b.ne  \call_compiled_code
.endm

.macro UPDATE_REGISTERS_FOR_STRING_INIT old_value, new_value
   mov wip, wzr
1:
   GET_VREG_OBJECT wip2, wip
   cmp wip2, \old_value
   b.ne 2f
   SET_VREG_OBJECT \new_value, wip
2:
   add wip, wip, #1
   add ip2, xREFS, wip, uxtw #2
   cmp ip2, xFP
   b.ne 1b
.endm

// Puts the next floating point argument into the expected register,
// fetching values based on a non-range invoke.
// Uses ip and ip2.
.macro LOOP_OVER_SHORTY_LOADING_FPS dreg, sreg, inst, shorty, arg_index, finished
1: // LOOP
    ldrb wip, [\shorty], #1         // Load next character in shorty, and increment.
    cbz wip, \finished              // if (wip == '\0') goto finished
    cmp wip, #68                    // if (wip == 'D') goto FOUND_DOUBLE
    b.eq 2f
    cmp wip, #70                    // if (wip == 'F') goto FOUND_FLOAT
    b.eq 3f
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    //  Handle extra argument in arg array taken by a long.
    cmp wip, #74                   // if (wip != 'J') goto LOOP
    b.ne 1b
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    and ip, \inst, #0xf
    GET_VREG wip, wip
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    cmp \arg_index, #4
    b.eq 5f
    and ip2, \inst, #0xf
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 6f
5:
    // TODO: Extract from wINST here and below? (Requires using a different register
    // in the COMMON_INVOKE_NON_RANGE.)
    FETCH_B wip2, 0, 1
    and wip2, wip2, #0xf
6:
    GET_VREG wip2, wip2
    add ip, ip, ip2, lsl #32
    fmov \dreg, ip
    b 4f
3:  // FOUND_FLOAT
    cmp \arg_index, #4
    b.eq 7f
    and ip, \inst, #0xf
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 8f
7:
    FETCH_B wip, 0, 1
    and wip, wip, #0xf
8:
    GET_VREG \sreg, wip
4:
.endm

// Puts the next int/long/object argument in the expected register,
// fetching values based on a non-range invoke.
// Uses ip and ip2.
.macro LOOP_OVER_SHORTY_LOADING_GPRS gpr_reg64, gpr_reg32, inst, shorty, arg_index, finished
1: // LOOP
    ldrb wip, [\shorty], #1         // Load next character in shorty, and increment.
    cbz wip, \finished              // if (wip == '\0') goto finished
    cmp wip, #74                    // if (wip == 'J') goto FOUND_LONG
    b.eq 2f
    cmp wip, #70                    // if (wip == 'F') goto SKIP_FLOAT
    b.eq 3f
    cmp wip, #68                    // if (wip == 'D') goto SKIP_DOUBLE
    b.eq 4f
    cmp \arg_index, #4
    b.eq 7f
    and ip, \inst, #0xf
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 8f
7:
    FETCH_B wip, 0, 1
    and wip, wip, #0xf
8:
    GET_VREG \gpr_reg32, wip
    b 5f
2:  // FOUND_LONG
    and ip, \inst, #0xf
    GET_VREG wip, wip
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    cmp \arg_index, #4
    b.eq 9f
    and ip2, \inst, #0xf
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 10f
9:
    FETCH_B wip2, 0, 1
    and wip2, wip2, #0xf
10:
    GET_VREG wip2, wip2
    add \gpr_reg64, ip, ip2, lsl #32
    b 5f
3:  // SKIP_FLOAT
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 1b
4:  // SKIP_DOUBLE
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    cmp \arg_index, #4
    b.eq 1b
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 1b
5:
.endm

.macro COMMON_INVOKE_NON_RANGE is_static=0, is_interface=0, suffix="", is_string_init=0, is_polymorphic=0, is_custom=0
   .if \is_polymorphic
   // We always go to compiled code for polymorphic calls.
   .elseif \is_custom
   // We always go to compiled code for custom calls.
   .else
     DO_ENTRY_POINT_CHECK .Lcall_compiled_code_\suffix
     GET_CODE_ITEM
     .if \is_string_init
     bl nterp_to_nterp_string_init_non_range
     .elseif \is_static
     bl nterp_to_nterp_static_non_range
     .else
     bl nterp_to_nterp_instance_non_range
     .endif
     b .Ldone_return_\suffix
   .endif

.Lcall_compiled_code_\suffix:
   GET_SHORTY xINST, \is_interface, \is_polymorphic, \is_custom
   // From this point:
   // - xINST contains shorty (in callee-save to switch over return value after call).
   // - x0 contains method
   // - x1 contains 'this' pointer for instance method.
   add x9, xINST, #1  // shorty + 1  ; ie skip return arg character
   FETCH w11, 2 // arguments
   .if \is_string_init
   lsr x11, x11, #4
   mov x10, #1       // ignore first argument
   .elseif \is_static
   mov x10, xzr      // arg_index
   .else
   lsr x11, x11, #4
   mov x10, #1       // ignore first argument
   .endif
   LOOP_OVER_SHORTY_LOADING_FPS d0, s0, x11, x9, x10, .Lxmm_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_FPS d1, s1, x11, x9, x10, .Lxmm_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_FPS d2, s2, x11, x9, x10, .Lxmm_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_FPS d3, s3, x11, x9, x10, .Lxmm_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_FPS d4, s4, x11, x9, x10, .Lxmm_setup_finished_\suffix
.Lxmm_setup_finished_\suffix:
   add x9, xINST, #1  // shorty + 1  ; ie skip return arg character
   FETCH w11, 2 // arguments
   .if \is_string_init
   lsr x11, x11, #4
   mov x10, #1       // ignore first argument
   LOOP_OVER_SHORTY_LOADING_GPRS x1, w1, x11, x9, x10, .Lgpr_setup_finished_\suffix
   .elseif \is_static
   mov x10, xzr      // arg_index
   LOOP_OVER_SHORTY_LOADING_GPRS x1, w1, x11, x9, x10, .Lgpr_setup_finished_\suffix
   .else
   lsr x11, x11, #4
   mov x10, #1       // ignore first argument
   .endif
   LOOP_OVER_SHORTY_LOADING_GPRS x2, w2, x11, x9, x10, .Lgpr_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_GPRS x3, w3, x11, x9, x10, .Lgpr_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_GPRS x4, w4, x11, x9, x10, .Lgpr_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_GPRS x5, w5, x11, x9, x10, .Lgpr_setup_finished_\suffix
.Lgpr_setup_finished_\suffix:
   .if \is_polymorphic
   bl art_quick_invoke_polymorphic
   .elseif \is_custom
   bl art_quick_invoke_custom
   .else
      .if \is_interface
      // Setup hidden argument
      FETCH wip2, 1
      .endif
      ldr lr, [x0, #ART_METHOD_QUICK_CODE_OFFSET_64]
      blr lr
   .endif
   ldrb wip, [xINST]
   cmp ip, #68       // Test if result type char == 'D'.
   b.eq .Lreturn_double_\suffix
   cmp ip, #70
   b.ne .Ldone_return_\suffix
.Lreturn_float_\suffix:
   fmov w0, s0
   b .Ldone_return_\suffix
.Lreturn_double_\suffix:
   fmov x0, d0
.Ldone_return_\suffix:
   /* resume execution of caller */
   .if \is_string_init
   FETCH w11, 2 // arguments
   and x11, x11, #0xf
   GET_VREG w1, w11
   UPDATE_REGISTERS_FOR_STRING_INIT w1, w0
   .endif

   .if \is_polymorphic
   FETCH_ADVANCE_INST 4
   .else
   FETCH_ADVANCE_INST 3
   .endif
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.endm

// Puts the next floating point argument into the expected register,
// fetching values based on a range invoke.
// Uses ip as temporary.
.macro LOOP_RANGE_OVER_SHORTY_LOADING_FPS dreg, sreg, shorty, arg_index, stack_index, finished
1: // LOOP
    ldrb wip, [\shorty], #1         // Load next character in shorty, and increment.
    cbz wip, \finished              // if (wip == '\0') goto finished
    cmp wip, #68                    // if (wip == 'D') goto FOUND_DOUBLE
    b.eq 2f
    cmp wip, #70                    // if (wip == 'F') goto FOUND_FLOAT
    b.eq 3f
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    //  Handle extra argument in arg array taken by a long.
    cmp wip, #74                    // if (wip != 'J') goto LOOP
    b.ne 1b
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    b 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    GET_VREG_DOUBLE \dreg, \arg_index
    add \arg_index, \arg_index, #2
    add \stack_index, \stack_index, #2
    b 4f
3:  // FOUND_FLOAT
    GET_VREG \sreg, \arg_index
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
4:
.endm

// Puts the next floating point argument into the expected stack slot,
// fetching values based on a range invoke.
// Uses ip as temporary.
//
// TODO: We could just copy all the vregs to the stack slots in a simple loop
// without looking at the shorty at all. (We could also drop
// the "stack_index" from the macros for loading registers.) We could also do
// that conditionally if argument word count > 6; otherwise we know that all
// args fit into registers.
.macro LOOP_RANGE_OVER_FPs shorty, arg_index, stack_index, finished
1: // LOOP
    ldrb wip, [\shorty], #1         // Load next character in shorty, and increment.
    cbz wip, \finished              // if (wip == '\0') goto finished
    cmp wip, #68                    // if (wip == 'D') goto FOUND_DOUBLE
    b.eq 2f
    cmp wip, #70                    // if (wip == 'F') goto FOUND_FLOAT
    b.eq 3f
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    //  Handle extra argument in arg array taken by a long.
    cmp wip, #74                    // if (wip != 'J') goto LOOP
    b.ne 1b
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    b 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    GET_VREG_WIDE ip, \arg_index
    add ip2, sp, \stack_index, uxtw #2
    str ip, [ip2]
    add \arg_index, \arg_index, #2
    add \stack_index, \stack_index, #2
    b 1b
3:  // FOUND_FLOAT
    GET_VREG wip, \arg_index
    str wip, [sp, \stack_index, uxtw #2]
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    b 1b
.endm

// Puts the next int/long/object argument in the expected register,
// fetching values based on a range invoke.
// Uses ip as temporary.
.macro LOOP_RANGE_OVER_SHORTY_LOADING_GPRS reg64, reg32, shorty, arg_index, stack_index, finished
1: // LOOP
    ldrb wip, [\shorty], #1         // Load next character in shorty, and increment.
    cbz wip, \finished              // if (wip == '\0') goto finished
    cmp wip, #74                    // if (wip == 'J') goto FOUND_LONG
    b.eq 2f
    cmp wip, #70                   // if (wip == 'F') goto SKIP_FLOAT
    b.eq 3f
    cmp wip, #68                    // if (wip == 'D') goto SKIP_DOUBLE
    b.eq 4f
    GET_VREG \reg32, \arg_index
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    b 5f
2:  // FOUND_LONG
    GET_VREG_WIDE \reg64, \arg_index
    add \arg_index, \arg_index, #2
    add \stack_index, \stack_index, #2
    b 5f
3:  // SKIP_FLOAT
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    b 1b
4:  // SKIP_DOUBLE
    add \arg_index, \arg_index, #2
    add \stack_index, \stack_index, #2
    b 1b
5:
.endm

// Puts the next int/long/object argument in the expected stack slot,
// fetching values based on a range invoke.
// Uses ip as temporary.
.macro LOOP_RANGE_OVER_INTs shorty, arg_index, stack_index, finished
1: // LOOP
    ldrb wip, [\shorty], #1         // Load next character in shorty, and increment.
    cbz wip, \finished              // if (wip == '\0') goto finished
    cmp wip, #74                    // if (wip == 'J') goto FOUND_LONG
    b.eq 2f
    cmp wip, #70                    // if (wip == 'F') goto SKIP_FLOAT
    b.eq 3f
    cmp wip, #68                    // if (wip == 'D') goto SKIP_DOUBLE
    b.eq 4f
    GET_VREG wip, \arg_index
    str wip, [sp, \stack_index, uxtw #2]
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    b 1b
2:  // FOUND_LONG
    GET_VREG_WIDE ip, \arg_index
    add ip2, sp, \stack_index, uxtw #2
    str ip, [ip2]
    add \arg_index, \arg_index, #2
    add \stack_index, \stack_index, #2
    b 1b
3:  // SKIP_FLOAT
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    b 1b
4:  // SKIP_DOUBLE
    add \arg_index, \arg_index, #2
    add \stack_index, \stack_index, #2
    b 1b
.endm

.macro COMMON_INVOKE_RANGE is_static=0, is_interface=0, suffix="", is_string_init=0, is_polymorphic=0, is_custom=0
   .if \is_polymorphic
   // We always go to compiled code for polymorphic calls.
   .elseif \is_custom
   // We always go to compiled code for custom calls.
   .else
     DO_ENTRY_POINT_CHECK .Lcall_compiled_code_range_\suffix
     GET_CODE_ITEM
     .if \is_string_init
     bl nterp_to_nterp_string_init_range
     .elseif \is_static
     bl nterp_to_nterp_static_range
     .else
     bl nterp_to_nterp_instance_range
     .endif
     b .Ldone_return_range_\suffix
   .endif

.Lcall_compiled_code_range_\suffix:
   GET_SHORTY xINST, \is_interface, \is_polymorphic, \is_custom
   // From this point:
   // - xINST contains shorty (in callee-save to switch over return value after call).
   // - x0 contains method
   // - x1 contains 'this' pointer for instance method.
   add x9, xINST, #1  // shorty + 1  ; ie skip return arg character
   FETCH w10, 2 // arguments
   .if \is_string_init
   add x10, x10, #1  // arg start index
   mov x11, #1       // index in stack
   .elseif \is_static
   mov x11, xzr      // index in stack
   .else
   add x10, x10, #1  // arg start index
   mov x11, #1       // index in stack
   .endif
   LOOP_RANGE_OVER_SHORTY_LOADING_FPS d0, s0, x9, w10, w11, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_FPS d1, s1, x9, w10, w11, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_FPS d2, s2, x9, w10, w11, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_FPS d3, s3, x9, w10, w11, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_FPS d4, s4, x9, w10, w11, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_FPS d5, s5, x9, w10, w11, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_FPS d6, s6, x9, w10, w11, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_FPS d7, s7, x9, w10, w11, .Lxmm_setup_finished_range_\suffix
   // Store in the outs array (stored above the ArtMethod in the stack)
   add x11, x11, #2 // Add two words for the ArtMethod stored before the outs.
   LOOP_RANGE_OVER_FPs x9, w10, w11, .Lxmm_setup_finished_range_\suffix
.Lxmm_setup_finished_range_\suffix:
   add x9, xINST, #1  // shorty + 1  ; ie skip return arg character
   FETCH w10, 2 // arguments
   .if \is_string_init
   add x10, x10, #1  // arg start index
   mov x11, #1       // index in stack
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS x1, w1, x9, w10, w11, .Lgpr_setup_finished_\suffix
   .elseif \is_static
   mov x11, xzr      // index in stack
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS x1, w1, x9, w10, w11 .Lgpr_setup_finished_\suffix
   .else
   add x10, x10, #1  // arg start index
   mov x11, #1       // index in stack
   .endif
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS x2, w2, x9, w10, w11, .Lgpr_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS x3, w3, x9, w10, w11, .Lgpr_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS x4, w4, x9, w10, w11, .Lgpr_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS x5, w5, x9, w10, w11, .Lgpr_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS x6, w6, x9, w10, w11, .Lgpr_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS x7, w7, x9, w10, w11, .Lgpr_setup_finished_range_\suffix
   // Store in the outs array (stored above the ArtMethod in the stack)
   add x11, x11, #2 // Add two words for the ArtMethod stored before the outs.
   LOOP_RANGE_OVER_INTs x9, w10, w11, .Lgpr_setup_finished_range_\suffix
.Lgpr_setup_finished_range_\suffix:
   .if \is_polymorphic
   bl art_quick_invoke_polymorphic
   .elseif \is_custom
   bl art_quick_invoke_custom
   .else
      .if \is_interface
      // Setup hidden argument
      FETCH wip2, 1
      .endif
      ldr lr, [x0, #ART_METHOD_QUICK_CODE_OFFSET_64]
      blr lr
   .endif
   ldrb wip, [xINST]
   cmp ip, #68       // Test if result type char == 'D'.
   b.eq .Lreturn_double_range_\suffix
   cmp ip, #70
   b.ne .Ldone_return_\suffix
.Lreturn_float_range_\suffix:
   fmov w0, s0
   b .Ldone_return_range_\suffix
.Lreturn_double_range_\suffix:
   fmov x0, d0
.Ldone_return_range_\suffix:
   /* resume execution of caller */
   .if \is_string_init
   FETCH w11, 2 // arguments
   GET_VREG w1, w11
   UPDATE_REGISTERS_FOR_STRING_INIT w1, w0
   .endif

   .if \is_polymorphic
    FETCH_ADVANCE_INST 4
   .else
   FETCH_ADVANCE_INST 3
   .endif
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.endm

// Fetch some information from the thread cache.
// Uses ip and ip2 as temporaries.
.macro FETCH_FROM_THREAD_CACHE dest_reg, slow_path
   add      ip, xSELF, #THREAD_INTERPRETER_CACHE_OFFSET       // cache address
   ubfx     ip2, xPC, #2, #THREAD_INTERPRETER_CACHE_SIZE_LOG2  // entry index
   add      ip, ip, ip2, lsl #4            // entry address within the cache
   ldp      ip, \dest_reg, [ip]           // entry key (pc) and value (offset)
   cmp      ip, xPC
   b.ne \slow_path
.endm

// Helper for static field get.
.macro OP_SGET load="ldr", volatile_load="ldar", maybe_extend="", wide="0"
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 4f
1:
   ldr w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   ldr w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz wMR, 3f
2:
   lsr w2, wINST, #8              // w2 <- A
   .if \wide
   ldr x0, [x0, x1]
   SET_VREG_WIDE x0, w2          // fp[A] <- value
   .else
   \load w0, [x0, x1]
   SET_VREG w0, w2               // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   bl art_quick_read_barrier_mark_reg00
   b 2b
4:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   EXPORT_PC
   bl nterp_get_static_field
   tbz x0, #0, 1b
   CLEAR_STATIC_VOLATILE_MARKER x0
   ldr w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   ldr w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz wMR, 7f
5:
   lsr w2, wINST, #8              // w2 <- A
   add x0, x0, x1
   .if \wide
   ldar x0, [x0]
   SET_VREG_WIDE x0, w2          // fp[A] <- value
   .else
   \volatile_load w0, [x0]
   \maybe_extend
   SET_VREG w0, w2               // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
7:
   bl art_quick_read_barrier_mark_reg00
   b 5b
.endm

// Helper for static field put.
.macro OP_SPUT store="str", volatile_store="stlr", wide="0":
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 4f
1:
   ldr w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   ldr w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz wMR, 3f
2:
   lsr w2, wINST, #8              // w2 <- A
   .if \wide
   GET_VREG_WIDE x2, w2           // x2 <- v[A]
   \store    x2, [x0, x1]
   .else
   GET_VREG w2, w2                // w2 <- v[A]
   \store    w2, [x0, x1]
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   bl art_quick_read_barrier_mark_reg00
   b 2b
4:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   EXPORT_PC
   bl nterp_get_static_field
   tbz x0, #0, 1b
   CLEAR_STATIC_VOLATILE_MARKER x0
   ldr w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   ldr w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz wMR, 6f
5:
   lsr w2, wINST, #8              // w2 <- A
   add x0, x0, x1
   .if \wide
   GET_VREG_WIDE x2, w2           // x2 <- v[A]
   \volatile_store    x2, [x0]
   .else
   GET_VREG w2, w2                // w2 <- v[A]
   \volatile_store    w2, [x0]
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
6:
   bl art_quick_read_barrier_mark_reg00
   b 5b
.endm


// Helper for instance field put.
.macro OP_IPUT store="str", volatile_store="stlr", wide="0":
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 2f
1:
   ubfx    w1, wINST, #8, #4           // w1<- A
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w2, w2                     // vB (object we're operating on)
   cbz w2, common_errNullObject
   .if \wide
   GET_VREG_WIDE x1, w1                // x1<- fp[A]/fp[A+1]
   \store x1, [x2, x0]
   .else
   GET_VREG w1, w1                     // w1 <- v[A]
   \store w1, [x2, x0]
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
2:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   EXPORT_PC
   bl nterp_get_instance_field_offset
   tbz w0, #31, 1b
   CLEAR_INSTANCE_VOLATILE_MARKER w0
   ubfx    w1, wINST, #8, #4           // w1<- A
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w2, w2                     // vB (object we're operating on)
   cbz w2, common_errNullObject
   add x2, x2, x0
   .if \wide
   GET_VREG_WIDE x1, w1                // x1<- fp[A]/fp[A+1]
   \volatile_store x1, [x2]
   .else
   GET_VREG w1, w1                     // w1 <- v[A]
   \volatile_store w1, [x2]
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.endm

// Helper for instance field get.
.macro OP_IGET load="ldr", volatile_load="ldar", maybe_extend="", wide="0"
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 2f
1:
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w3, w2                     // w3<- object we're operating on
   ubfx    w2, wINST, #8, #4           // w2<- A
   cbz     w3, common_errNullObject    // object was null
   .if \wide
   \load x0, [x3, x0]
   SET_VREG_WIDE x0, w2                // fp[A] <- value
   .else
   \load w0, [x3, x0]
   SET_VREG w0, w2                     // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
2:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   EXPORT_PC
   bl nterp_get_instance_field_offset
   tbz w0, #31, 1b
   CLEAR_INSTANCE_VOLATILE_MARKER w0
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w3, w2                     // w3<- object we're operating on
   ubfx    w2, wINST, #8, #4           // w2<- A
   cbz     w3, common_errNullObject    // object was null
   add x3, x3, x0
   .if \wide
   \volatile_load x0, [x3]
   SET_VREG_WIDE x0, w2                // fp[A] <- value
   .else
   \volatile_load w0, [x3]
   \maybe_extend
   SET_VREG w0, w2                     // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.endm

// Puts the next int/long/object parameter passed in physical register
// in the expected dex register array entry, and in case of object in the
// expected reference array entry.
.macro LOOP_OVER_SHORTY_STORING_GPRS gpr_64, gpr_32, shorty, arg_offset, regs, refs, finished
1: // LOOP
    ldrb wip, [\shorty], #1       // Load next character in shorty, and increment.
    cbz wip, \finished            // if (wip == '\0') goto finished
    cmp wip, #74                  // if (wip == 'J') goto FOUND_LONG
    b.eq 2f
    cmp wip, #70                  // if (wip == 'F') goto SKIP_FLOAT
    b.eq 3f
    cmp wip, #68                  // if (wip == 'D') goto SKIP_DOUBLE
    b.eq 4f
    str \gpr_32, [\regs, \arg_offset]
    cmp wip, #76                  // if (wip != 'L') goto NOT_REFERENCE
    b.ne 6f
    str \gpr_32, [\refs, \arg_offset]
6:  // NOT_REFERENCE
    add \arg_offset, \arg_offset, #4
    b 5f
2:  // FOUND_LONG
    str \gpr_64, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #8
    b 5f
3:  // SKIP_FLOAT
    add \arg_offset, \arg_offset, #4
    b 1b
4:  // SKIP_DOUBLE
    add \arg_offset, \arg_offset, #8
    b 1b
5:
.endm

// Puts the next floating point parameter passed in physical register
// in the expected dex register array entry.
// Uses ip as temporary.
.macro LOOP_OVER_SHORTY_STORING_FPS dreg, sreg, shorty, arg_offset, fp, finished
1: // LOOP
    ldrb wip, [\shorty], #1                 // Load next character in shorty, and increment.
    cbz wip, \finished                      // if (wip == '\0') goto finished
    cmp wip, #68                            // if (wip == 'D') goto FOUND_DOUBLE
    b.eq 2f
    cmp wip, #70                            // if (wip == 'F') goto FOUND_FLOAT
    b.eq 3f
    add \arg_offset, \arg_offset, #4
    //  Handle extra argument in arg array taken by a long.
    cmp wip, #74                            // if (wip != 'J') goto LOOP
    b.ne 1b
    add \arg_offset, \arg_offset, #4
    b 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    str \dreg, [\fp, \arg_offset]
    add \arg_offset, \arg_offset, #8
    b 4f
3:  // FOUND_FLOAT
    str \sreg, [\fp, \arg_offset]
    add \arg_offset, \arg_offset, #4
4:
.endm

// Puts the next floating point parameter passed in stack
// in the expected dex register array entry.
// Uses ip as temporary.
//
// TODO: Or we could just spill regs to the reserved slots in the caller's
// frame and copy all regs in a simple loop. This time, however, we would
// need to look at the shorty anyway to look for the references.
// (The trade-off is different for passing arguments and receiving them.)
.macro LOOP_OVER_FPs shorty, arg_offset, regs, stack_ptr, finished
1: // LOOP
    ldrb wip, [\shorty], #1                 // Load next character in shorty, and increment.
    cbz wip, \finished                      // if (wip == '\0') goto finished
    cmp wip, #68                            // if (wip == 'D') goto FOUND_DOUBLE
    b.eq 2f
    cmp wip, #70                            // if (wip == 'F') goto FOUND_FLOAT
    b.eq 3f
    add \arg_offset, \arg_offset, #4
    //  Handle extra argument in arg array taken by a long.
    cmp wip, #74                            // if (wip != 'J') goto LOOP
    b.ne 1b
    add \arg_offset, \arg_offset, #4
    b 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    add ip, \stack_ptr, \arg_offset
    ldr ip, [ip,  #OFFSET_TO_FIRST_ARGUMENT_IN_STACK]
    str ip, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #8
    b 1b
3:  // FOUND_FLOAT
    add ip, \stack_ptr, \arg_offset
    ldr wip, [ip,  #OFFSET_TO_FIRST_ARGUMENT_IN_STACK]
    str wip, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #4
    b 1b
.endm

// Puts the next int/long/object parameter passed in stack
// in the expected dex register array entry, and in case of object in the
// expected reference array entry.
// Uses ip and ip2 as temporary.
.macro LOOP_OVER_INTs shorty, arg_offset, regs, refs, stack_ptr, finished
1: // LOOP
    ldrb wip, [\shorty], #1       // Load next character in shorty, and increment.
    cbz wip, \finished            // if (wip == '\0') goto finished
    cmp wip, #74                  // if (wip == 'J') goto FOUND_LONG
    b.eq 2f
    cmp wip, #70                  // if (wip == 'F') goto SKIP_FLOAT
    b.eq 3f
    cmp wip, #68                  // if (wip == 'D') goto SKIP_DOUBLE
    b.eq 4f
    add ip2, \stack_ptr, \arg_offset
    ldr wip2, [ip2,  #OFFSET_TO_FIRST_ARGUMENT_IN_STACK]
    str wip2, [\regs, \arg_offset]
    cmp wip, #76                  // if (wip != 'L') goto loop
    b.ne 3f
    str wip2, [\refs, \arg_offset]
    add \arg_offset, \arg_offset, #4
    b 1b
2:  // FOUND_LONG
    add ip, \stack_ptr, \arg_offset
    ldr ip, [ip,  #OFFSET_TO_FIRST_ARGUMENT_IN_STACK]
    str ip, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #8
    b 1b
3:  // SKIP_FLOAT
    add \arg_offset, \arg_offset, #4
    b 1b
4:  // SKIP_DOUBLE
    add \arg_offset, \arg_offset, #8
    b 1b
.endm

%def entry():
/*
 * ArtMethod entry point.
 *
 * On entry:
 *  x0   ArtMethod* callee
 *  rest  method parameters
 */

OAT_ENTRY ExecuteNterpImpl, EndExecuteNterpImpl
    .cfi_startproc
    sub x16, sp, #STACK_OVERFLOW_RESERVED_BYTES
    ldr wzr, [x16]
    /* Spill callee save regs */
    SPILL_ALL_CALLEE_SAVES

    // TODO: Get shorty in a better way and remove below
    SPILL_ALL_ARGUMENTS

    // Save method in callee-save xINST
    mov xINST, x0
    bl NterpGetShorty
    // Save shorty in callee-save xIBASE.
    mov xIBASE, x0
    mov x0, xINST
    bl NterpGetCodeItem
    mov xPC, x0

    RESTORE_ALL_ARGUMENTS

    // Setup the stack for executing the method.
    SETUP_STACK_FRAME xPC, xREFS, xFP, CFI_REFS

    // Setup the parameters
    ldrh wip2, [xPC, #CODE_ITEM_INS_SIZE_OFFSET]
    cbz wip2, .Lxmm_setup_finished

    sub ip2, ip, ip2
    lsl x8, ip2, #2 // x8 is now the offset for inputs into the registers array.

    // Setup shorty, pointer to inputs in FP and pointer to inputs in REFS
    add x9, xIBASE, #1  // shorty + 1  ; ie skip return arg character
    add x10, xFP, x8
    add x11, xREFS, x8

    ldr wip, [x0, #ART_METHOD_ACCESS_FLAGS_OFFSET]
    // TODO: could be TBNZ but we'd need a constant for log2(ART_METHOD_IS_STATIC_FLAG).
    tst wip, #ART_METHOD_IS_STATIC_FLAG
    b.ne .Lhandle_static_method
    str w1, [x10], #4
    str w1, [x11], #4
    add x13, x13, #4
    mov x12, #0
    b .Lcontinue_setup_gprs
.Lhandle_static_method:
    mov x12, #0
    LOOP_OVER_SHORTY_STORING_GPRS x1, w1, x9, x12, x10, x11, .Lgpr_setup_finished
.Lcontinue_setup_gprs:
    LOOP_OVER_SHORTY_STORING_GPRS x2, w2, x9, x12, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS x3, w3, x9, x12, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS x4, w4, x9, x12, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS x5, w5, x9, x12, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS x6, w6, x9, x12, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS x7, w7, x9, x12, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_INTs x9, x12, x10, x11, x13, .Lgpr_setup_finished
.Lgpr_setup_finished:
    add x9, xIBASE, #1  // shorty + 1  ; ie skip return arg character
    mov x12, #0  // reset counter
    LOOP_OVER_SHORTY_STORING_FPS d0, s0, x9, x12, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d1, s1, x9, x12, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d2, s2, x9, x12, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d3, s3, x9, x12, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d4, s4, x9, x12, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d5, s5, x9, x12, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d6, s6, x9, x12, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d7, s7, x9, x12, x10, .Lxmm_setup_finished
    LOOP_OVER_FPs x9, x12, x10, x13, .Lxmm_setup_finished
.Lxmm_setup_finished:
    // Set the dex pc pointer.
    add xPC, xPC, #CODE_ITEM_INSNS_OFFSET
    CFI_DEFINE_DEX_PC_WITH_OFFSET(CFI_TMP, CFI_DEX, 0)

    // Set rIBASE
    adr xIBASE, artNterpAsmInstructionStart
    /* start executing the instruction at xPC */
    START_EXECUTING_INSTRUCTIONS
    /* NOTE: no fallthrough */
    // cfi info continues, and covers the whole nterp implementation.
    SIZE ExecuteNterpImpl

%def opcode_pre():

%def helpers():

%def footer():
/*
 * ===========================================================================
 *  Common subroutines and data
 * ===========================================================================
 */

    .text
    .align  2

// Note: mterp also uses the common_* names below for helpers, but that's OK
// as the C compiler compiled each interpreter separately.
common_errDivideByZero:
    EXPORT_PC
    bl art_quick_throw_div_zero

// Expect array in w0, index in w1, length in w2
common_errArrayIndex:
    EXPORT_PC
    mov x0, x1
    mov x1, x3
    bl art_quick_throw_array_bounds

common_errNullObject:
    EXPORT_PC
    bl art_quick_throw_null_pointer_exception

NterpCommonInvokeStatic:
    COMMON_INVOKE_NON_RANGE is_static=1, suffix="invokeStatic"

NterpCommonInvokeStaticRange:
    COMMON_INVOKE_RANGE is_static=1, suffix="invokeStatic"

NterpCommonInvokeInstance:
    COMMON_INVOKE_NON_RANGE suffix="invokeInstance"

NterpCommonInvokeInstanceRange:
    COMMON_INVOKE_RANGE suffix="invokeInstance"

NterpCommonInvokeInterface:
    COMMON_INVOKE_NON_RANGE is_interface=1, suffix="invokeInterface"

NterpCommonInvokeInterfaceRange:
    COMMON_INVOKE_RANGE is_interface=1, suffix="invokeInterface"

NterpCommonInvokePolymorphic:
    COMMON_INVOKE_NON_RANGE is_polymorphic=1, suffix="invokePolymorphic"

NterpCommonInvokePolymorphicRange:
    COMMON_INVOKE_RANGE is_polymorphic=1, suffix="invokePolymorphic"

NterpCommonInvokeCustom:
    COMMON_INVOKE_NON_RANGE is_static=1, is_custom=1, suffix="invokeCustom"

NterpCommonInvokeCustomRange:
    COMMON_INVOKE_RANGE is_static=1, is_custom=1, suffix="invokeCustom"

NterpHandleStringInit:
   COMMON_INVOKE_NON_RANGE is_string_init=1, suffix="stringInit"

NterpHandleStringInitRange:
   COMMON_INVOKE_RANGE is_string_init=1, suffix="stringInit"

NterpNewInstance:
   EXPORT_PC
   // Fast-path which gets the class from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 2f
   cbnz wMR, 3f
4:
   ldr lr, [xSELF, #THREAD_ALLOC_OBJECT_ENTRYPOINT_OFFSET]
   blr lr
1:
   lsr w1, wINST, #8                    // w1 <- A
   SET_VREG_OBJECT w0, w1               // fp[A] <- value
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
2:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   bl nterp_get_class_or_allocate_object
   b 1b
3:
   bl art_quick_read_barrier_mark_reg00
   b 4b

NterpNewArray:
   /* new-array vA, vB, class@CCCC */
   EXPORT_PC
   // Fast-path which gets the class from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 2f
   cbnz wMR, 3f
1:
   lsr     w1, wINST, #12              // w1<- B
   GET_VREG w1, w1                     // w1<- vB (array length)
   ldr lr, [xSELF, #THREAD_ALLOC_ARRAY_ENTRYPOINT_OFFSET]
   blr lr
   ubfx    w1, wINST, #8, #4           // w1<- A
   SET_VREG_OBJECT w0, w1
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
2:
   mov x0, xSELF
   ldr x1, [sp, 0]
   mov x2, xPC
   bl nterp_get_class_or_allocate_object
   b 1b
3:
   bl art_quick_read_barrier_mark_reg00
   b 1b

NterpPutObjectInstanceField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 3f
1:
   ubfx    w1, wINST, #8, #4           // w1<- A
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w2, w2                     // vB (object we're operating on)
   cbz w2, common_errNullObject        // is object null?
   GET_VREG w1, w1                     // w1 <- v[A]
   str w1, [x2, x0]
   cbz w1, 2f
   ldr x1, [xSELF, #THREAD_CARD_TABLE_OFFSET]
   lsr w3, w2, #CARD_TABLE_CARD_SHIFT
   strb w1, [x1, x3]
2:
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   EXPORT_PC
   bl nterp_get_instance_field_offset
   tbz w0, #31, 1b
   CLEAR_INSTANCE_VOLATILE_MARKER w0
   ubfx    w1, wINST, #8, #4           // w1<- A
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w2, w2                     // vB (object we're operating on)
   cbz w2, common_errNullObject        // is object null?
   GET_VREG w1, w1                     // w1 <- v[A]
   add x3, x2, x0
   stlr w1, [x3]
   cbz w1, 2b
   ldr x1, [xSELF, #THREAD_CARD_TABLE_OFFSET]
   lsr w3, w2, #CARD_TABLE_CARD_SHIFT
   strb w1, [x1, x3]
   b 2b

NterpGetObjectInstanceField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 4f
1:
   ubfx    w1, wINST, #8, #4           // w1<- A
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w2, w2                     // vB (object we're operating on)
   cbz w2, common_errNullObject
   ldr w0, [x2, x0]
   cbnz wMR, 3f
2:
   SET_VREG_OBJECT w0, w1              // fp[A] <- value
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   bl art_quick_read_barrier_mark_reg00
   b 2b
4:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   EXPORT_PC
   bl nterp_get_instance_field_offset
   tbz w0, #31, 1b
   CLEAR_INSTANCE_VOLATILE_MARKER w0
   ubfx    w1, wINST, #8, #4           // w1<- A
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w2, w2                     // vB (object we're operating on)
   cbz w2, common_errNullObject
   add x2, x2, x0
   ldar w0, [x2]
   cbnz wMR, 6f
5:
   SET_VREG_OBJECT w0, w1              // fp[A] <- value
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
6:
   bl art_quick_read_barrier_mark_reg00
   b 5b

NterpPutObjectStaticField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 5f
1:
   ldr w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   ldr w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz wMR, 4f
2:
   lsr w2, wINST, #8                    // w2 <- A
   GET_VREG w2, w2
   str w2, [x0, x1]
   cbz w2, 3f
   ldr x1, [xSELF, #THREAD_CARD_TABLE_OFFSET]
   lsr w3, w0, #CARD_TABLE_CARD_SHIFT
   strb w1, [x1, x3]
3:
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
4:
   bl art_quick_read_barrier_mark_reg00
   b 2b
5:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   EXPORT_PC
   bl nterp_get_static_field
   tbz x0, #0, 1b
   CLEAR_STATIC_VOLATILE_MARKER x0
   ldr w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   ldr w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz wMR, 7f
6:
   lsr w2, wINST, #8                    // w1 <- A
   GET_VREG w2, w2
   add ip, x0, x1
   stlr w2, [ip]
   cbz w2, 3b
   ldr x1, [xSELF, #THREAD_CARD_TABLE_OFFSET]
   lsr w3, w0, #CARD_TABLE_CARD_SHIFT
   strb w1, [x1, x3]
   b 3b
7:
   bl art_quick_read_barrier_mark_reg00
   b 6b

NterpGetObjectStaticField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 4f
1:
   ldr w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   ldr w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz wMR, 3f
   ldr w0, [x0, x1]
   // No need to check the marking register, we know it's not set here.
2:
   lsr w1, wINST, #8                    // w1 <- A
   SET_VREG_OBJECT w0, w1               // fp[A] <- value
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   bl art_quick_read_barrier_mark_reg00
   ldr w0, [x0, x1]
   // Here, we know the marking register is set.
   bl art_quick_read_barrier_mark_reg00
   b 2b
4:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   EXPORT_PC
   bl nterp_get_static_field
   tbz x0, #0, 1b
   CLEAR_STATIC_VOLATILE_MARKER x0
   ldr w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   ldr w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz wMR, 7f
5:
   add x0, x0, x1
   ldar w0, [x0]
   cbnz wMR, 8f
6:
   lsr w1, wINST, #8                    // w1 <- A
   SET_VREG_OBJECT w0, w1               // fp[A] <- value
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
7:
   bl art_quick_read_barrier_mark_reg00
   b 5b
8:
   bl art_quick_read_barrier_mark_reg00
   b 6b

NterpGetBooleanStaticField:
  OP_SGET load="ldrb", volatile_load="ldarb", maybe_extend="", wide=0

NterpGetByteStaticField:
  OP_SGET load="ldrsb", volatile_load="ldarb", maybe_extend="sbfx w0, w0, #0, #8", wide=0

NterpGetCharStaticField:
  OP_SGET load="ldrh", volatile_load="ldarh", maybe_extend="", wide=0

NterpGetShortStaticField:
  OP_SGET load="ldrsh", volatile_load="ldarh", maybe_extend="sbfx w0, w0, #0, #16", wide=0

NterpGetWideStaticField:
  OP_SGET load="ldr", volatile_load="ldar", maybe_extend="", wide=1

NterpGetIntStaticField:
  OP_SGET load="ldr", volatile_load="ldar", maybe_extend="", wide=0

NterpPutStaticField:
  OP_SPUT store="str", volatile_store="stlr", wide=0

NterpPutBooleanStaticField:
NterpPutByteStaticField:
  OP_SPUT store="strb", volatile_store="stlrb", wide=0

NterpPutCharStaticField:
NterpPutShortStaticField:
  OP_SPUT store="strh", volatile_store="stlrh", wide=0

NterpPutWideStaticField:
  OP_SPUT store="str", volatile_store="stlr", wide=1

NterpPutInstanceField:
  OP_IPUT store="str", volatile_store="stlr", wide=0

NterpPutBooleanInstanceField:
NterpPutByteInstanceField:
  OP_IPUT store="strb", volatile_store="stlrb", wide=0

NterpPutCharInstanceField:
NterpPutShortInstanceField:
  OP_IPUT store="strh", volatile_store="stlrh", wide=0

NterpPutWideInstanceField:
  OP_IPUT store="str", volatile_store="stlr", wide=1

NterpGetBooleanInstanceField:
  OP_IGET load="ldrb", volatile_load="ldarb", maybe_extend="", wide=0

NterpGetByteInstanceField:
  OP_IGET load="ldrsb", volatile_load="ldarb", maybe_extend="sbfx w0, w0, #0, #8", wide=0

NterpGetCharInstanceField:
  OP_IGET load="ldrh", volatile_load="ldarh", maybe_extend="", wide=0

NterpGetShortInstanceField:
  OP_IGET load="ldrsh", volatile_load="ldarh", maybe_extend="sbfx w0, w0, #0, #16", wide=0

NterpGetWideInstanceField:
  OP_IGET load="ldr", volatile_load="ldar", maybe_extend="", wide=1

NterpGetInstanceField:
  OP_IGET load="ldr", volatile_load="ldar", maybe_extend="", wide=0

NterpInstanceOf:
    /* instance-of vA, vB, class@CCCC */
   // Fast-path which gets the class from thread-local cache.
   EXPORT_PC
   FETCH_FROM_THREAD_CACHE x1, 3f
   cbnz wMR, 4f
1:
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w0, w2                     // w0<- vB (object)
   cbz w0, 2f
   bl artInstanceOfFromCode
2:
   ubfx    w1, wINST, #8, #4           // w1<- A
   SET_VREG w0, w1
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   bl nterp_get_class_or_allocate_object
   mov x1, x0
   b 1b
4:
   bl art_quick_read_barrier_mark_reg01
   b 1b

NterpCheckCast:
   // Fast-path which gets the class from thread-local cache.
   EXPORT_PC
   FETCH_FROM_THREAD_CACHE x1, 3f
   cbnz wMR, 4f
1:
   lsr     w2, wINST, #8               // w2<- A
   GET_VREG w0, w2                     // w2<- vA (object)
   cbz w0, 2f
   bl art_quick_check_instance_of
2:
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   bl nterp_get_class_or_allocate_object
   mov x1, x0
   b 1b
4:
   bl art_quick_read_barrier_mark_reg01
   b 1b

NterpHandleHotnessOverflow:
    add x1, xPC, xINST, lsl #1
    mov x2, xFP
    bl nterp_hot_method
    cbnz x0, 1f
    add w2, wINST, wINST                // w2<- byte offset
    FETCH_ADVANCE_INST_RB w2            // update xPC, load wINST
    GET_INST_OPCODE ip                  // extract opcode from wINST
    GOTO_OPCODE ip                      // jump to next instruction
1:
    // Drop the current frame.
    ldr ip, [xREFS, #-8]
    mov sp, ip
    .cfi_def_cfa sp, CALLEE_SAVES_SIZE

    // The transition frame of type SaveAllCalleeSaves saves x19 and x20,
    // but not managed ABI. So we need to restore callee-saves of the nterp frame,
    // and save managed ABI callee saves, which will be restored by the callee upon
    // return.
    RESTORE_ALL_CALLEE_SAVES
    INCREASE_FRAME ((CALLEE_SAVES_SIZE) - 16)

    // FP callee-saves
    stp d8, d9, [sp, #0]
    stp d10, d11, [sp, #16]
    stp d12, d13, [sp, #32]
    stp d14, d15, [sp, #48]

    // GP callee-saves.
    SAVE_TWO_REGS x21, x22, 64
    SAVE_TWO_REGS x23, x24, 80
    SAVE_TWO_REGS x25, x26, 96
    SAVE_TWO_REGS x27, x28, 112
    SAVE_TWO_REGS x29, lr, 128

    // Setup the new frame
    ldr x1, [x0, #OSR_DATA_FRAME_SIZE]
    // Given stack size contains all callee saved registers, remove them.
    sub x1, x1, #(CALLEE_SAVES_SIZE - 16)

    // We know x1 cannot be 0, as it at least contains the ArtMethod.

    // Remember CFA in a callee-save register.
    mov xINST, sp
    .cfi_def_cfa_register xINST

    sub sp, sp, x1

    add x2, x0, #OSR_DATA_MEMORY
2:
    sub x1, x1, #8
    ldr ip, [x2, x1]
    str ip, [sp, x1]
    cbnz x1, 2b

    // Fetch the native PC to jump to and save it in a callee-save register.
    ldr xFP, [x0, #OSR_DATA_NATIVE_PC]

    // Free the memory holding OSR Data.
    bl free

    // Jump to the compiled code.
    br xFP

NterpHandleInvokeInterfaceOnObjectMethodRange:
   // First argument is the 'this' pointer.
   FETCH w1, 2
   GET_VREG w1, w1
   // Note: x1 is null, this will be handled by our SIGSEGV handler.
   ldr w2, [x1, #MIRROR_OBJECT_CLASS_OFFSET]
   add w2, w2, #MIRROR_CLASS_VTABLE_OFFSET_64
   ldr x0, [x2, w0, sxtw #3]
   b NterpCommonInvokeInstanceRange

NterpHandleInvokeInterfaceOnObjectMethod:
   // First argument is the 'this' pointer.
   FETCH w1, 2
   and w1, w1, #0xf
   GET_VREG w1, w1
   // Note: x1 is null, this will be handled by our SIGSEGV handler.
   ldr w2, [x1, #MIRROR_OBJECT_CLASS_OFFSET]
   add w2, w2, #MIRROR_CLASS_VTABLE_OFFSET_64
   ldr x0, [x2, w0, sxtw #3]
   b NterpCommonInvokeInstance

// This is the logical end of ExecuteNterpImpl, where the frame info applies.
// EndExecuteNterpImpl includes the methods below as we want the runtime to
// see them as part of the Nterp PCs.
.cfi_endproc

nterp_to_nterp_static_non_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=1, is_string_init=0
    .cfi_endproc

nterp_to_nterp_string_init_non_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=1
    .cfi_endproc

nterp_to_nterp_instance_non_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=0
    .cfi_endproc

nterp_to_nterp_static_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=1
    .cfi_endproc

nterp_to_nterp_instance_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=0
    .cfi_endproc

nterp_to_nterp_string_init_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=1
    .cfi_endproc

// This is the end of PCs contained by the OatQuickMethodHeader created for the interpreter
// entry point.
    .type EndExecuteNterpImpl, #function
    .hidden EndExecuteNterpImpl
    .global EndExecuteNterpImpl
EndExecuteNterpImpl:

// Entrypoints into runtime.
NTERP_TRAMPOLINE nterp_get_static_field, NterpGetStaticField
NTERP_TRAMPOLINE nterp_get_instance_field_offset, NterpGetInstanceFieldOffset
NTERP_TRAMPOLINE nterp_filled_new_array, NterpFilledNewArray
NTERP_TRAMPOLINE nterp_filled_new_array_range, NterpFilledNewArrayRange
NTERP_TRAMPOLINE nterp_get_class_or_allocate_object, NterpGetClassOrAllocateObject
NTERP_TRAMPOLINE nterp_get_method, NterpGetMethod
NTERP_TRAMPOLINE nterp_hot_method, NterpHotMethod
NTERP_TRAMPOLINE nterp_load_object, NterpLoadObject

// gen_mterp.py will inline the following definitions
// within [ExecuteNterpImpl, EndExecuteNterpImpl).
%def instruction_end():

    .type artNterpAsmInstructionEnd, #function
    .hidden artNterpAsmInstructionEnd
    .global artNterpAsmInstructionEnd
artNterpAsmInstructionEnd:
    // artNterpAsmInstructionEnd is used as landing pad for exception handling.
    FETCH_INST
    GET_INST_OPCODE ip
    GOTO_OPCODE ip

%def instruction_start():

    .type artNterpAsmInstructionStart, #function
    .hidden artNterpAsmInstructionStart
    .global artNterpAsmInstructionStart
artNterpAsmInstructionStart = .L_op_nop
    .text

%def opcode_start():
    NAME_START nterp_${opcode}
%def opcode_end():
    NAME_END nterp_${opcode}
%def helper_start(name):
    NAME_START ${name}
%def helper_end(name):
    NAME_END ${name}
