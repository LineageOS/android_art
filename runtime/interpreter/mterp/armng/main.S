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
#include "arch/arm/asm_support_arm.S"

/**
 * ARM EABI general notes:
 *
 * r0-r3 hold first 4 args to a method; they are not preserved across method calls
 * r4-r8 are available for general use
 * r9 is given special treatment in some situations, but not for us
 * r10 (sl) seems to be generally available
 * r11 (fp) is used by gcc (unless -fomit-frame-pointer is set)
 * r12 (ip) is scratch -- not preserved across method calls
 * r13 (sp) should be managed carefully in case a signal arrives
 * r14 (lr) must be preserved
 * r15 (pc) can be tinkered with directly
 *
 * r0 holds returns of <= 4 bytes
 * r0-r1 hold returns of 8 bytes, low word in r0
 *
 * Callee must save/restore r4+ (except r12) if it modifies them.  If VFP
 * is present, registers s16-s31 (a/k/a d8-d15, a/k/a q4-q7) must be preserved,
 * s0-s15 (d0-d7, q0-a3) do not need to be.
 *
 * Stack is "full descending".  Only the arguments that don't fit in the first 4
 * registers are placed on the stack.  "sp" points at the first stacked argument
 * (i.e. the 5th arg).
 *
 * Native ABI uses soft-float, single-precision results are in r0,
 * double-precision results in r0-r1.
 *
 * In the EABI, "sp" must be 64-bit aligned on entry to a function, and any
 * 64-bit quantities (long long, double) must be 64-bit aligned.
 *
 * Nterp notes:
 *
 * The following registers have fixed assignments:
 *
 *   reg nick      purpose
 *   r5  rFP       interpreted frame pointer, used for accessing locals and args
 *   r6  rREFS     base of object references of dex registers
 *   r7  rINST     first 16-bit code unit of current instruction
 *   r8  rMR       marking register
 *   r9  rSELF     self (Thread) pointer
 *   r10 rIBASE    interpreted instruction base pointer, used for computed goto
 *   r11 rPC       interpreted program counter, used for fetching instructions
 *
 *   r4, ip, and lr can be used as temporary
 *
 * Note that r4 is a callee-save register in ARM EABI, but not in managed code.
 *
 */

/* single-purpose registers, given names for clarity */
#define CFI_DEX  11 // DWARF register number of the register holding dex-pc (rPC).
#define CFI_TMP  0  // DWARF register number of the first argument register (r0).
#define CFI_REFS 6
#define rFP      r5
#define rREFS    r6
#define rINST    r7
#define rSELF    r9
#define rIBASE   r10
#define rPC      r11

// To avoid putting ifdefs arond the use of rMR, make sure it's defined.
// IsNterpSupported returns false for configurations that don't have rMR (typically CMS).
#ifndef rMR
#define rMR r8
#endif

// Temporary registers while setting up a frame.
#define rNEW_FP   r8
#define rNEW_REFS r10
#define CFI_NEW_REFS 10

#define CALLEE_SAVES_SIZE (9 * 4 + 16 * 4)

// +4 for the ArtMethod of the caller.
#define OFFSET_TO_FIRST_ARGUMENT_IN_STACK (CALLEE_SAVES_SIZE + 4)

/*
 * Fetch the next instruction from rPC into rINST.  Does not advance rPC.
 */
.macro FETCH_INST
    ldrh    rINST, [rPC]
.endm

/*
 * Fetch the next instruction from the specified offset.  Advances rPC
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
    ldrh    rINST, [rPC, #((\count)*2)]!
.endm

/*
 * Similar to FETCH_ADVANCE_INST, but does not update xPC.  Used to load
 * rINST ahead of possible exception point.  Be sure to manually advance xPC
 * later.
 */
.macro PREFETCH_INST count
    ldrh    rINST, [rPC, #((\count)*2)]
.endm

/* Advance xPC by some number of code units. */
.macro ADVANCE count
  add  rPC, #((\count)*2)
.endm

/*
 * Fetch the next instruction from an offset specified by "reg" and advance xPC.
 * xPC to point to the next instruction.  "reg" must specify the distance
 * in bytes, *not* 16-bit code units, and may be a signed value.
 */
.macro FETCH_ADVANCE_INST_RB reg
    ldrh    rINST, [rPC, \reg]!
.endm

/*
 * Fetch a half-word code unit from an offset past the current PC.  The
 * "count" value is in 16-bit code units.  Does not advance xPC.
 *
 * The "_S" variant works the same but treats the value as signed.
 */
.macro FETCH reg, count
    ldrh    \reg, [rPC, #((\count)*2)]
.endm

.macro FETCH_S reg, count
    ldrsh   \reg, [rPC, #((\count)*2)]
.endm

/*
 * Fetch one byte from an offset past the current PC.  Pass in the same
 * "count" as you would for FETCH, and an additional 0/1 indicating which
 * byte of the halfword you want (lo/hi).
 */
.macro FETCH_B reg, count, byte
    ldrb     \reg, [rPC, #((\count)*2+(\byte))]
.endm

/*
 * Put the instruction's opcode field into the specified register.
 */
.macro GET_INST_OPCODE reg
    and     \reg, rINST, #255
.endm

/*
 * Begin executing the opcode in _reg.  Clobbers reg
 */

.macro GOTO_OPCODE reg
    add     pc, rIBASE, \reg, lsl #${handler_size_bits}
.endm

/*
 * Get/set value from a Dalvik register.
 */
.macro GET_VREG reg, vreg
    ldr     \reg, [rFP, \vreg, lsl #2]
.endm
.macro GET_VREG_OBJECT reg, vreg
    ldr     \reg, [rREFS, \vreg, lsl #2]
.endm
.macro SET_VREG reg, vreg
    str     \reg, [rFP, \vreg, lsl #2]
    mov     \reg, #0
    str     \reg, [rREFS, \vreg, lsl #2]
.endm
.macro SET_VREG_OBJECT reg, vreg
    str     \reg, [rFP, \vreg, lsl #2]
    str     \reg, [rREFS, \vreg, lsl #2]
.endm
.macro SET_VREG_FLOAT reg, vreg, tmpreg
    add     \tmpreg, rFP, \vreg, lsl #2
    vstr    \reg, [\tmpreg]
    mov     \tmpreg, #0
    str     \tmpreg, [rREFS, \vreg, lsl #2]
.endm
.macro GET_VREG_WIDE_BY_ADDR reg0, reg1, addr
    ldmia \addr, {\reg0, \reg1}
.endm
.macro SET_VREG_WIDE_BY_ADDR reg0, reg1, addr
    stmia \addr, {\reg0, \reg1}
.endm
.macro GET_VREG_FLOAT sreg, vreg
    ldr  \vreg, [rFP, \vreg, lsl #2]
    vmov \sreg, \vreg
.endm
.macro GET_VREG_FLOAT_BY_ADDR reg, addr
    vldr \reg, [\addr]
.endm
.macro SET_VREG_FLOAT_BY_ADDR reg, addr
    vstr \reg, [\addr]
.endm
.macro GET_VREG_DOUBLE_BY_ADDR reg, addr
    vldr \reg, [\addr]
.endm
.macro SET_VREG_DOUBLE_BY_ADDR reg, addr
    vstr \reg, [\addr]
.endm
.macro SET_VREG_SHADOW reg, vreg
    str     \reg, [rREFS, \vreg, lsl #2]
.endm
.macro CLEAR_SHADOW_PAIR vreg, tmp1, tmp2
    mov     \tmp1, #0
    add     \tmp2, \vreg, #1
    SET_VREG_SHADOW \tmp1, \vreg
    SET_VREG_SHADOW \tmp1, \tmp2
.endm
.macro VREG_INDEX_TO_ADDR reg, vreg
    add     \reg, rFP, \vreg, lsl #2
.endm

// An assembly entry that has a OatQuickMethodHeader prefix.
.macro OAT_ENTRY name, end
    .arm
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
    .arm
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
  SETUP_SAVE_REFS_ONLY_FRAME ip
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
  rsb \reg, \reg, #0
.endm

.macro EXPORT_PC
    str    rPC, [rREFS, #-8]
.endm

.macro BRANCH
    // Update method counter and do a suspend check if the branch is negative.
    cmp rINST, #0
    blt 2f
1:
    add r2, rINST, rINST                // r2<- byte offset
    FETCH_ADVANCE_INST_RB r2            // update xPC, load rINST
    GET_INST_OPCODE ip                  // extract opcode from rINST
    GOTO_OPCODE ip                      // jump to next instruction
2:
    ldr r0, [sp]
    ldrh r2, [r0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
    add r2, r2, #1
    ubfx r2, r2, #0, #NTERP_HOTNESS_BITS
    strh r2, [r0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
    // If the counter overflows, handle this in the runtime.
    cmp r2, #0
    beq NterpHandleHotnessOverflow
    // Otherwise, do a suspend check.
    ldr r0, [rSELF, #THREAD_FLAGS_OFFSET]
    ands r0, r0, #THREAD_SUSPEND_OR_CHECKPOINT_REQUEST
    beq 1b
    EXPORT_PC
    bl    art_quick_test_suspend
    b 1b
.endm

// Expects:
// - ip and lr to be available.
// Outputs:
// - \registers contains the dex registers size
// - \outs contains the outs size
// - if load_ins is 1, \ins contains the ins
// - \code_item is replaced with a pointer to the instructions
.macro FETCH_CODE_ITEM_INFO code_item, registers, outs, ins, load_ins
    tst \code_item, #1
    beq 5f
    bic \code_item, \code_item, #1 // Remove the extra bit that marks it's a compact dex file
    ldrh lr, [\code_item, #COMPACT_CODE_ITEM_FIELDS_OFFSET]
    ubfx \registers, lr, #COMPACT_CODE_ITEM_REGISTERS_SIZE_SHIFT, #4
    ubfx \outs, lr, #COMPACT_CODE_ITEM_OUTS_SIZE_SHIFT, #4
    .if \load_ins
    ubfx \ins, lr, #COMPACT_CODE_ITEM_INS_SIZE_SHIFT, #4
    .else
    ubfx ip, lr, #COMPACT_CODE_ITEM_INS_SIZE_SHIFT, #4
    add \registers, \registers, ip
    .endif

    ldrh lr, [\code_item, #COMPACT_CODE_ITEM_FLAGS_OFFSET]
    tst lr, #COMPACT_CODE_ITEM_REGISTERS_INS_OUTS_FLAGS
    beq 4f
    mov ip, \code_item
    tst lr, #COMPACT_CODE_ITEM_INSNS_FLAG
    beq 1f
    sub ip, ip, #4
1:
    tst lr, #COMPACT_CODE_ITEM_REGISTERS_FLAG
    beq 2f
    ldrh lr, [ip, #-2]!
    add \registers, \registers, lr
    ldrh lr, [\code_item, #COMPACT_CODE_ITEM_FLAGS_OFFSET]
2:
    tst lr, #COMPACT_CODE_ITEM_INS_FLAG
    beq 3f
    ldrh lr, [ip, #-2]!
    .if \load_ins
    add \ins, \ins, lr
    .else
    add \registers, \registers, lr
    .endif
    ldrh lr, [\code_item, #COMPACT_CODE_ITEM_FLAGS_OFFSET]
3:
    tst lr, #COMPACT_CODE_ITEM_OUTS_FLAG
    beq 4f
    ldrh lr, [ip, #-2]!
    add \outs, \outs, lr
4:
    .if \load_ins
    add \registers, \registers, \ins
    .endif
    add \code_item, \code_item, #COMPACT_CODE_ITEM_INSNS_OFFSET
    b 6f
5:
    // Fetch dex register size.
    ldrh \registers, [\code_item, #CODE_ITEM_REGISTERS_SIZE_OFFSET]
    // Fetch outs size.
    ldrh \outs, [\code_item, #CODE_ITEM_OUTS_SIZE_OFFSET]
    .if \load_ins
    ldrh \ins, [\code_item, #CODE_ITEM_INS_SIZE_OFFSET]
    .endif
    add \code_item, \code_item, #CODE_ITEM_INSNS_OFFSET
6:
.endm

// Setup the stack to start executing the method. Expects:
// - r0 to contain the ArtMethod
// - \code_item to already contain the code item
// - rINST, ip, lr to be available
//
// Outputs
// - rINST contains the dex registers size
// - ip contains the old stack pointer.
// - \code_item is replaced with a pointer to the instructions
// - if load_ins is 1, r4 contains the ins
//
.macro SETUP_STACK_FRAME code_item, refs, fp, cfi_refs, load_ins
    FETCH_CODE_ITEM_INFO \code_item, rINST, \refs, r4, \load_ins

    // Compute required frame size: ((2 * rINST) + \refs) * 4 + 12
    // 12 is for saving the previous frame, pc, and method being executed.
    add ip, \refs, rINST, lsl #1

    // Compute new stack pointer in lr
    sub lr, sp, #12
    sub lr, lr, ip, lsl #2
    // Alignment
    and lr, lr, #-16

    // Set reference and dex registers.
    add \refs, lr, \refs, lsl #2
    add \refs, \refs, #12
    add \fp, \refs, rINST, lsl #2

    // Now setup the stack pointer.
    mov ip, sp
    .cfi_def_cfa_register ip
    mov sp, lr
    str ip, [\refs, #-4]
    CFI_DEF_CFA_BREG_PLUS_UCONST \cfi_refs, -4, CALLEE_SAVES_SIZE

    // Save the ArtMethod, and use r0 as a temporary.
    str r0, [sp]

    // Put nulls in reference frame.
    cmp rINST, #0
    beq 2f
    mov lr, \refs
    mov r0, #0
1:
    str r0, [lr], #4
    str r0, [lr], #4  // May clear vreg[0].
    cmp lr, \fp
    blo 1b
2:
    ldr r0, [sp]  // Reload the ArtMethod, expected by the callers.
.endm

// Increase method hotness and do suspend check before starting executing the method.
.macro START_EXECUTING_INSTRUCTIONS
    ldr r0, [sp]
    ldrh r2, [r0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
    add r2, r2, #1
    ubfx r2, r2, #0, #NTERP_HOTNESS_BITS
    strh r2, [r0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
    // If the counter overflows, handle this in the runtime.
    cmp r2, #0
    beq 2f
    ldr r0, [rSELF, #THREAD_FLAGS_OFFSET]
    tst r0, #THREAD_SUSPEND_OR_CHECKPOINT_REQUEST
    bne 3f
1:
    FETCH_INST
    GET_INST_OPCODE ip
    GOTO_OPCODE ip
2:
    mov r1, #0
    mov r2, rFP
    bl nterp_hot_method
    b 1b
3:
    EXPORT_PC
    bl art_quick_test_suspend
    b 1b
.endm

.macro SPILL_ALL_CALLEE_SAVES
    SPILL_ALL_CALLEE_SAVE_GPRS                    @ 9 words (36 bytes) of callee saves.
    vpush {s16-s31}                               @ 16 words (64 bytes) of floats.
    .cfi_adjust_cfa_offset 64
.endm

.macro RESTORE_ALL_CALLEE_SAVES lr_to_pc=0
    vpop {s16-s31}
    .cfi_adjust_cfa_offset -64
    pop {r4-r7}
    .cfi_adjust_cfa_offset -16
    .cfi_restore r4
    .cfi_restore r5
    .cfi_restore r6
    .cfi_restore r7
    // Don't restore r8, the marking register gets updated when coming back from runtime.
    add sp, sp, #4
    .cfi_adjust_cfa_offset -4
    .if \lr_to_pc
    pop {r9-r11, pc}  @ 9 words of callee saves and args.
    .else
    pop {r9-r11, lr}  @ 9 words of callee saves and args.
    .cfi_adjust_cfa_offset -16
    .cfi_restore r9
    .cfi_restore r10
    .cfi_restore r11
    .cfi_restore lr
    .endif
.endm

.macro SPILL_ALL_ARGUMENTS
    // We spill r4 for stack alignment.
    push {r0-r4}
    .cfi_adjust_cfa_offset 20
    .cfi_rel_offset r0, 0
    .cfi_rel_offset r1, 4
    .cfi_rel_offset r2, 8
    .cfi_rel_offset r3, 12
    .cfi_rel_offset r4, 16
    vpush {s0-s15}
    .cfi_adjust_cfa_offset 64
.endm

.macro RESTORE_ALL_ARGUMENTS
    vpop {s0-s15}
    .cfi_adjust_cfa_offset -64
    pop {r0-r4}
    .cfi_restore r0
    .cfi_restore r1
    .cfi_restore r2
    .cfi_restore r3
    .cfi_restore r4
    .cfi_adjust_cfa_offset -20
.endm

// Helper to setup the stack after doing a nterp to nterp call. This will setup:
// - rNEW_FP: the new pointer to dex registers
// - rNEW_REFS: the new pointer to references
// - rPC: the new PC pointer to execute
// - r2: value in instruction to decode the number of arguments.
// - r3: first dex register for range invokes, up to 4 arguments for non-range invokes.
// - r4: top of dex register array
//
// The method expects:
// - r0 to contain the ArtMethod
// - r4 to contain the code item
.macro SETUP_STACK_FOR_INVOKE
   // We do the same stack overflow check as the compiler. See CanMethodUseNterp
   // in how we limit the maximum nterp frame size.
   sub ip, sp, #STACK_OVERFLOW_RESERVED_BYTES
   ldr ip, [ip]

   // Spill all callee saves to have a consistent stack frame whether we
   // are called by compiled code or nterp.
   SPILL_ALL_CALLEE_SAVES

   // Setup the frame.
   SETUP_STACK_FRAME r4, rNEW_REFS, rNEW_FP, CFI_NEW_REFS, load_ins=0

   // Fetch instruction information before replacing rPC.
   FETCH_B r2, 0, 1
   FETCH r3, 2

   // Set the dex pc pointer.
   mov rPC, r4

   // Make r4 point to the top of the dex register array.
   add r4, rNEW_FP, rINST, lsl #2

   CFI_DEFINE_DEX_PC_WITH_OFFSET(CFI_TMP, CFI_DEX, 0)
.endm

// Setup arguments based on a non-range nterp to nterp call, and start executing
// the method. We expect:
// - rNEW_FP: the new pointer to dex registers
// - rPC: the new PC pointer to execute
// - r2: number of arguments (bits 4-7), 5th argument if any (bits 0-3)
// - r3: up to four dex register arguments
// - r4: top of dex register array
// - r1: receiver if non-static.
//
// Uses r0 and rINST as temporaries.
.macro SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=0
   // /* op vA, vB, {vC...vG} */
   .if \is_static
   asrs   r0, r2, #4
   beq    6f
   .else
   asr    r0, r2, #4
   .endif
   mov rINST, #-4
   cmp r0, #2
   blt 1f
   beq 2f
   cmp r0, #4
   blt 3f
   beq 4f

  // We use a decrementing rINST to store references relative
  // to rNEW_FP and dex registers relative to r4
  //
  // TODO: We could set up rINST as the number of registers (this can be an additional output from
  // SETUP_STACK_FOR_INVOKE) and then just decrement it by one before copying each arg.
  // Maybe even introduce macros NEW_VREG_ADDRESS/NEW_VREG_REF_ADDRESS.
5:
   and         r2, r2, #15
   GET_VREG_OBJECT r0, r2
   str         r0, [rNEW_FP, rINST]
   GET_VREG    r0, r2
   str         r0, [r4, rINST]
   sub         rINST, rINST, #4
4:
   asr         r2, r3, #12
   GET_VREG_OBJECT r0, r2
   str         r0, [rNEW_FP, rINST]
   GET_VREG    r0, r2
   str         r0, [r4, rINST]
   sub         rINST, rINST, #4
3:
   ubfx        r2, r3, #8, #4
   GET_VREG_OBJECT r0, r2
   str         r0, [rNEW_FP, rINST]
   GET_VREG    r0, r2
   str         r0, [r4, rINST]
   sub         rINST, rINST, #4
2:
   ubfx        r2, r3, #4, #4
   GET_VREG_OBJECT r0, r2
   str         r0, [rNEW_FP, rINST]
   GET_VREG    r0, r2
   str         r0, [r4, rINST]
   .if !\is_string_init
   sub         rINST, rINST, #4
   .endif
1:
   .if \is_string_init
   // Ignore the first argument
   .elseif \is_static
   and         r2, r3, #0xf
   GET_VREG_OBJECT r0, r2
   str         r0, [rNEW_FP, rINST]
   GET_VREG    r0, r2
   str         r0, [r4, rINST]
   .else
   str         r1, [rNEW_FP, rINST]
   str         r1, [r4, rINST]
   .endif

6:
   // Start executing the method.
   mov rFP, rNEW_FP
   mov rREFS, rNEW_REFS
   CFI_DEF_CFA_BREG_PLUS_UCONST CFI_REFS, -4, CALLEE_SAVES_SIZE
   // r8 was used for setting up the frame, restore it now.
   REFRESH_MARKING_REGISTER
   // Branch to the main handler, which will reload rIBASE,
   // that was used for setting up the frame.
   b .Lexecute_instructions
.endm

// Setup arguments based on a range nterp to nterp call, and start executing
// the method.
// - rNEW_FP: the new pointer to dex registers
// - rNEW_REFS: the new pointer to references
// - rPC: the new PC pointer to execute
// - r2: number of arguments
// - r3: first dex register
// - r4: top of dex register array
// - r1: receiver if non-static.
//
// Expects r0 to be available.
.macro SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=0
   mov r0, #-4
   .if \is_string_init
   // Ignore the first argument
   sub r2, r2, #1
   add r3, r3, #1
   .elseif !\is_static
   sub r2, r2, #1
   add r3, r3, #1
   .endif

   cmp r2, #0
   beq 2f
   add rREFS, rREFS, r3, lsl #2  // pointer to first argument in reference array
   add rREFS, rREFS, r2, lsl #2    // pointer to last argument in reference array
   add rFP, rFP, r3, lsl #2     // pointer to first argument in register array
   add rFP, rFP, r2, lsl #2      // pointer to last argument in register array
1:
   ldr  r3, [rREFS, #-4]!
   str  r3, [rNEW_FP, r0]
   subs r2, r2, 1
   ldr  r3, [rFP, #-4]!
   str  r3, [r4, r0]
   sub r0, r0, 4
   bne 1b
2:
   .if \is_string_init
   // Ignore first argument
   .elseif !\is_static
   str r1, [rNEW_FP, r0]
   str r1, [r4, r0]
   .endif
   mov rFP, rNEW_FP
   mov rREFS, rNEW_REFS
   CFI_DEF_CFA_BREG_PLUS_UCONST CFI_REFS, -4, CALLEE_SAVES_SIZE
   // r8 was used for setting up the frame, restore it now.
   REFRESH_MARKING_REGISTER
   // Branch to the main handler, which will reload rIBASE,
   // that was used for setting up the frame.
   b .Lexecute_instructions
.endm

.macro GET_SHORTY dest, is_interface, is_polymorphic, is_custom
   push {r0-r3}
   .if \is_polymorphic
   ldr r0, [sp, #16]
   mov r1, rPC
   bl NterpGetShortyFromInvokePolymorphic
   .elseif \is_custom
   ldr r0, [sp, #16]
   mov r1, rPC
   bl NterpGetShortyFromInvokeCustom
   .elseif \is_interface
   ldr r0, [sp, #16]
   FETCH r1, 1
   bl NterpGetShortyFromMethodId
   .else
   bl NterpGetShorty
   .endif
   mov \dest, r0
   pop {r0-r3}
.endm

// Input:  r0 contains the ArtMethod
// Output: r4 contains the code item
.macro GET_CODE_ITEM
   ldr r4, [r0, #ART_METHOD_DATA_OFFSET_32]
.endm

.macro DO_ENTRY_POINT_CHECK call_compiled_code, name
   // On entry, the method is r0, the instance is r1
   ldr r2, .Lfetch_nterp_\name
.Lfetch_location_\name:
   // Note that this won't work for thumb.
   sub r2, pc, r2
   ldr r3, [r0, #ART_METHOD_QUICK_CODE_OFFSET_32]
   cmp r2, r3
   bne  \call_compiled_code
.endm

// Expects ip and lr to be available.
.macro UPDATE_REGISTERS_FOR_STRING_INIT old_value, new_value
   mov ip, #0
1:
   GET_VREG_OBJECT lr, ip
   cmp lr, \old_value
   bne 2f
   SET_VREG_OBJECT \new_value, ip
2:
   add ip, ip, #1
   add lr, rREFS, ip, lsl #2
   cmp lr, rFP
   bne 1b
.endm

// Puts the next floating point argument into the expected register,
// fetching values based on a non-range invoke.
// Uses ip and lr.
.macro LOOP_OVER_SHORTY_LOADING_FPS dreg, sreg, inst, shorty, arg_index, finished, if_double
1: // LOOP
    ldrb ip, [\shorty], #1          // Load next character in shorty, and increment.
    cmp ip, #0
    beq \finished                   // if (ip == '\0') goto finished
    cmp ip, #68                    // if (ip == 'D') goto FOUND_DOUBLE
    beq 2f
    cmp ip, #70                    // if (ip == 'F') goto FOUND_FLOAT
    beq 3f
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    //  Handle extra argument in arg array taken by a long.
    cmp ip, #74                   // if (ip != 'J') goto LOOP
    bne 1b
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    and ip, \inst, #0xf
    GET_VREG ip, ip
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    cmp \arg_index, #4
    beq 5f
    and lr, \inst, #0xf
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 6f
5:
    FETCH_B lr, 0, 1
    and lr, lr, #0xf
6:
    GET_VREG lr, lr
    vmov \dreg, ip, lr
    b \if_double
3:  // FOUND_FLOAT
    cmp \arg_index, #4
    beq 7f
    and ip, \inst, #0xf
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 8f
7:
    FETCH_B ip, 0, 1
    and ip, ip, #0xf
8:
    GET_VREG_FLOAT \sreg, ip
.endm

// Puts the next int/long/object argument in the expected register,
// fetching values based on a non-range invoke.
// Uses ip.
.macro LOOP_OVER_SHORTY_LOADING_GPRS gpr_reg, inst, shorty, arg_index, finished, if_long, is_r3
1: // LOOP
    ldrb ip, [\shorty], #1         // Load next character in shorty, and increment.
    cmp ip, #0
    beq \finished                   // if (ip == '\0') goto finished
    cmp ip, #74                    // if (ip == 'J') goto FOUND_LONG
    beq 2f
    cmp ip, #70                    // if (ip == 'F') goto SKIP_FLOAT
    beq 3f
    cmp ip, #68                    // if (ip == 'D') goto SKIP_DOUBLE
    beq 4f
    cmp \arg_index, #4
    beq 7f
    and ip, \inst, #0xf
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 8f
7:
    FETCH_B ip, 0, 1
    and ip, ip, #0xf
8:
    GET_VREG \gpr_reg, ip
    b 5f
2:  // FOUND_LONG
    .if \is_r3
    // Put back shorty and exit
    sub \shorty, \shorty, #1
    b 5f
    .endif
    and ip, \inst, #0xf
    GET_VREG ip, ip
    // The only one possible for non-range long is r2-r3
    mov r2, ip
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    cmp \arg_index, #4
    beq 9f
    and ip, \inst, #0xf
    lsr \inst, \inst, #4
    b 10f
9:
    FETCH_B ip, 0, 1
    and ip, ip, #0xf
10:
    GET_VREG ip, ip
    // The only one possible for non-range long is r2-r3
    mov r3, ip
    add \arg_index, \arg_index, #1
    b \if_long
3:  // SKIP_FLOAT
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 1b
4:  // SKIP_DOUBLE
    lsr \inst, \inst, #8
    add \arg_index, \arg_index, #2
    b 1b
5:
.endm

// Puts the next int/long/object argument in the expected stack slot,
// fetching values based on a non-range invoke.
// Uses ip as temporary.
.macro LOOP_OVER_SHORTY_LOADING_INTs shorty, inst, arg_index, finished, is_string_init
1: // LOOP
    ldrb ip, [\shorty], #1         // Load next character in shorty, and increment.
    cmp ip, #0
    beq \finished                  // if (ip == '\0') goto finished
    cmp ip, #74                    // if (ip == 'J') goto FOUND_LONG
    beq 2f
    cmp ip, #70                    // if (ip == 'F') goto SKIP_FLOAT
    beq 3f
    cmp ip, #68                    // if (ip == 'D') goto SKIP_DOUBLE
    beq 4f
    .if \is_string_init
    cmp \arg_index, #4
    .else
    cmp \arg_index, #(4+1)         // +1 for ArtMethod
    .endif
    beq 7f
    and ip, \inst, #0xf
    lsr \inst, \inst, #4
    b 8f
7:
    FETCH_B ip, 0, 1
    and ip, ip, #0xf
8:
    GET_VREG ip, ip
    str ip, [sp, \arg_index, lsl #2]
    add \arg_index, \arg_index, #1
    b 1b
2:  // FOUND_LONG
    and ip, \inst, #0xf
    GET_VREG ip, ip
    str ip, [sp, \arg_index, lsl #2]
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    .if \is_string_init
    cmp \arg_index, #4
    .else
    cmp \arg_index, #(4+1)         // +1 for ArtMethod
    .endif
    beq 9f
    and ip, \inst, #0xf
    lsr \inst, \inst, #4
    b 10f
9:
    FETCH_B ip, 0, 1
    and ip, ip, #0xf
10:
    GET_VREG ip, ip
    str ip, [sp, \arg_index, lsl #2]
    add \arg_index, \arg_index, #1
    b 1b
3:  // SKIP_FLOAT
    lsr \inst, \inst, #4
    add \arg_index, \arg_index, #1
    b 1b
4:  // SKIP_DOUBLE
    lsr \inst, \inst, #8
    add \arg_index, \arg_index, #2
    b 1b
.endm

.macro COMMON_INVOKE_NON_RANGE is_static=0, is_interface=0, suffix="", is_string_init=0, is_polymorphic=0, is_custom=0
   .if \is_polymorphic
   // We always go to compiled code for polymorphic calls.
   .elseif \is_custom
   // We always go to compiled code for custom calls.
   .else
     DO_ENTRY_POINT_CHECK .Lcall_compiled_code_\suffix, \suffix
     GET_CODE_ITEM
     .if \is_string_init
     bl nterp_to_nterp_string_init_non_range
     .elseif \is_static
     bl nterp_to_nterp_static_non_range
     .else
     bl nterp_to_nterp_instance_non_range
     .endif
     b .Ldone_return_\suffix
.Lfetch_nterp_\suffix:
    .word   (.Lfetch_location_\suffix+8) - ExecuteNterpImpl
   .endif

.Lcall_compiled_code_\suffix:
   GET_SHORTY rINST, \is_interface, \is_polymorphic, \is_custom
   // From this point:
   // - rINST contains shorty (in callee-save to switch over return value after call).
   // - r0 contains method
   // - r1 contains 'this' pointer for instance method.
   // We need three registers.
   add r3, rINST, #1  // shorty + 1  ; ie skip return arg character
   FETCH r2, 2 // arguments
   .if \is_string_init
   lsr r2, r2, #4
   mov r4, #1       // ignore first argument
   .elseif \is_static
   mov r4, #0      // arg_index
   .else
   lsr r2, r2, #4
   mov r4, #1       // ignore first argument
   .endif
   LOOP_OVER_SHORTY_LOADING_FPS d0, s0, r2, r3, r4, .Lxmm_setup_finished_\suffix, .Ld1_s2_\suffix
.Ld1_s1_\suffix:
   LOOP_OVER_SHORTY_LOADING_FPS d1, s1, r2, r3, r4, .Lxmm_setup_finished_\suffix, .Ld2_s1_\suffix
.Ld1_s2_\suffix:
   LOOP_OVER_SHORTY_LOADING_FPS d1, s2, r2, r3, r4, .Lxmm_setup_finished_\suffix, .Ls4_\suffix
.Ld2_s3_\suffix:
   LOOP_OVER_SHORTY_LOADING_FPS d2, s3, r2, r3, r4, .Lxmm_setup_finished_\suffix, .Lxmm_setup_finished_\suffix
   b .Ls4_\suffix
.Ld2_s1_\suffix:
   LOOP_OVER_SHORTY_LOADING_FPS d2, s1, r2, r3, r4, .Lxmm_setup_finished_\suffix, .Lxmm_setup_finished_\suffix
.Ls4_\suffix:
   // If we arrive here, we can only have a float.
   LOOP_OVER_SHORTY_LOADING_FPS d2, s4, r2, r3, r4, .Lxmm_setup_finished_\suffix, .Lxmm_setup_finished_\suffix
.Lxmm_setup_finished_\suffix:
   add r4, rINST, #1  // shorty + 1  ; ie skip return arg character
   FETCH r8, 2 // arguments
   .if \is_string_init
   lsr r8, r8, #4
   mov lr, #1       // ignore first argument
   LOOP_OVER_SHORTY_LOADING_GPRS r1, r8, r4, lr, .Lgpr_setup_finished_\suffix, .Lif_long_\suffix, is_r3=0
   .elseif \is_static
   mov lr, #0      // arg_index
   LOOP_OVER_SHORTY_LOADING_GPRS r1, r8, r4, lr, .Lgpr_setup_finished_\suffix, .Lif_long_\suffix, is_r3=0
   .else
   lsr r8, r8, #4
   mov lr, #1       // ignore first argument
   .endif
   LOOP_OVER_SHORTY_LOADING_GPRS r2, r8, r4, lr, .Lgpr_setup_finished_\suffix, .Lif_long_\suffix, is_r3=0
   LOOP_OVER_SHORTY_LOADING_GPRS r3, r8, r4, lr, .Lgpr_setup_finished_\suffix, .Lif_long_\suffix, is_r3=1
.Lif_long_\suffix:
   // Store in the outs array (stored above the ArtMethod in the stack). We only do this for non-string-init
   // calls as the index is already adjusted above.
   .if !\is_string_init
   add lr, lr, #1
   .endif
   LOOP_OVER_SHORTY_LOADING_INTs r4, r8, lr, .Lgpr_setup_finished_\suffix, \is_string_init
.Lgpr_setup_finished_\suffix:
   REFRESH_MARKING_REGISTER // r8 was used when setting parameters, restore it.
   .if \is_polymorphic
   bl art_quick_invoke_polymorphic
   .elseif \is_custom
   bl art_quick_invoke_custom
   .else
      .if \is_interface
      // Setup hidden argument. As we don't have access to the interface method,
      // just pass the method from the IMT. If the method is the conflict trampoline,
      // this will make the stub go to runtime, otherwise the hidden argument is unused.
      mov ip, r0
      .endif
      ldr lr, [r0, #ART_METHOD_QUICK_CODE_OFFSET_32]
      blx lr
   .endif
   ldrb ip, [rINST]
   cmp ip, #68       // Test if result type char == 'D'.
   beq .Lreturn_double_\suffix
   cmp ip, #70
   bne .Ldone_return_\suffix
.Lreturn_float_\suffix:
   vmov r0, s0
   b .Ldone_return_\suffix
.Lreturn_double_\suffix:
   vmov r0, r1, d0
.Ldone_return_\suffix:
   /* resume execution of caller */
   .if \is_string_init
   FETCH ip, 2 // arguments
   and ip, ip, #0xf
   GET_VREG r1, ip
   UPDATE_REGISTERS_FOR_STRING_INIT r1, r0
   .endif

   .if \is_polymorphic
   FETCH_ADVANCE_INST 4
   .else
   FETCH_ADVANCE_INST 3
   .endif
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.endm

// Puts the next int/long/object argument in the expected register,
// fetching values based on a range invoke.
// Uses ip as temporary.
.macro LOOP_RANGE_OVER_SHORTY_LOADING_GPRS reg32, shorty, arg_index, stack_index, finished, if_long, is_r3
1: // LOOP
    ldrb ip, [\shorty], #1         // Load next character in shorty, and increment.
    cmp ip, #0
    beq \finished                  // if (ip == '\0') goto finished
    cmp ip, #74                    // if (ip == 'J') goto FOUND_LONG
    beq 2f
    cmp ip, #70                    // if (ip == 'F') goto SKIP_FLOAT
    beq 3f
    cmp ip, #68                    // if (ip == 'D') goto SKIP_DOUBLE
    beq 4f
    GET_VREG \reg32, \arg_index
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    b 5f
2:  // FOUND_LONG
    .if \is_r3
    // Put back shorty and jump to \if_long
    sub \shorty, \shorty, #1
    .else
    GET_VREG r2, \arg_index
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    GET_VREG r3, \arg_index
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    .endif
    b \if_long
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
    ldrb ip, [\shorty], #1         // Load next character in shorty, and increment.
    cmp ip, #0
    beq \finished                     // if (ip == '\0') goto finished
    cmp ip, #74                    // if (ip == 'J') goto FOUND_LONG
    beq 2f
    cmp ip, #70                    // if (ip == 'F') goto SKIP_FLOAT
    beq 3f
    cmp ip, #68                    // if (ip == 'D') goto SKIP_DOUBLE
    beq 4f
    GET_VREG ip, \arg_index
    str ip, [sp, \stack_index, lsl #2]
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    b 1b
2:  // FOUND_LONG
    GET_VREG ip, \arg_index
    str ip, [sp, \stack_index, lsl #2]
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
    GET_VREG ip, \arg_index
    str ip, [sp, \stack_index, lsl #2]
    add \arg_index, \arg_index, #1
    add \stack_index, \stack_index, #1
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
     DO_ENTRY_POINT_CHECK .Lcall_compiled_code_range_\suffix, range_\suffix
     GET_CODE_ITEM
     .if \is_string_init
     bl nterp_to_nterp_string_init_range
     .elseif \is_static
     bl nterp_to_nterp_static_range
     .else
     bl nterp_to_nterp_instance_range
     .endif
     b .Ldone_return_range_\suffix
.Lfetch_nterp_range_\suffix:
    .word   (.Lfetch_location_range_\suffix+8) - ExecuteNterpImpl
   .endif

.Lcall_compiled_code_range_\suffix:
   GET_SHORTY rINST, \is_interface, \is_polymorphic, \is_custom
   // From this point:
   // - rINST contains shorty (in callee-save to switch over return value after call).
   // - r0 contains method
   // - r1 contains 'this' pointer for instance method.
   //
   // Save r0 and r1 before calling NterpSetupArm32Fprs.
   push {r0, r1}
   add r0, rINST, #1  // shorty + 1  ; ie skip return arg character
   FETCH r1, 2 // arguments
   .if \is_string_init
   add r1, r1, #1  // arg start index
   mov r2, #1       // index in stack
   .elseif \is_static
   mov r2, #0       // index in stack
   .else
   add r1, r1, #1  // arg start index
   mov r2, #1       // index in stack
   .endif
   vpush {s0-s15}
   mov r3, sp
   // Pass the stack address for arguments, +16 for fprs, +2 for saved registers,
   // +1 for ArtMethod.
   add lr, sp, #((16 + 2 + 1) * 4)
   push {rFP, lr}
   bl NterpSetupArm32Fprs
   add sp, sp, #8
   vpop {s0-s15}
   pop {r0, r1}
.Lxmm_setup_finished_range_\suffix:
   add r8, rINST, #1  // shorty + 1  ; ie skip return arg character
   FETCH lr, 2 // arguments
   .if \is_string_init
   add lr, lr, #1  // arg start index
   mov r4, #0      // index in stack
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS r1, r8, lr, r4, .Lgpr_setup_finished_range_\suffix, .Lif_long_range_\suffix, is_r3=0
   .elseif \is_static
   mov r4, #0      // index in stack
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS r1, r8, lr, r4, .Lgpr_setup_finished_range_\suffix, .Lif_long_range_\suffix, is_r3=0
   .else
   add lr, lr, #1  // arg start index
   mov r4, #1       // index in stack
   .endif
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS r2, r8, lr, r4, .Lgpr_setup_finished_range_\suffix, .Lif_long_range_\suffix, is_r3=0
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS r3, r8, lr, r4, .Lgpr_setup_finished_range_\suffix, .Lif_long_range_\suffix, is_r3=1
.Lif_long_range_\suffix:
   // Add 1 word for the ArtMethod stored before the outs.
   add r4, r4, #1
   LOOP_RANGE_OVER_INTs r8, lr, r4, .Lgpr_setup_finished_range_\suffix
.Lgpr_setup_finished_range_\suffix:
   REFRESH_MARKING_REGISTER // r8 was used when setting parameters, restore it.
   .if \is_polymorphic
   bl art_quick_invoke_polymorphic
   .elseif \is_custom
   bl art_quick_invoke_custom
   .else
      .if \is_interface
      // Setup hidden argument. As we don't have access to the interface method,
      // just pass the method from the IMT. If the method is the conflict trampoline,
      // this will make the stub go to runtime, otherwise the hidden argument is unused.
      mov ip, r0
      .endif
      ldr lr, [r0, #ART_METHOD_QUICK_CODE_OFFSET_32]
      blx lr
   .endif
   ldrb ip, [rINST]
   cmp ip, #68       // Test if result type char == 'D'.
   beq .Lreturn_double_range_\suffix
   cmp ip, #70
   bne .Ldone_return_range_\suffix
.Lreturn_float_range_\suffix:
   vmov r0, s0
   b .Ldone_return_range_\suffix
.Lreturn_double_range_\suffix:
   vmov r0, r1, d0
.Ldone_return_range_\suffix:
   /* resume execution of caller */
   .if \is_string_init
   FETCH ip, 2 // arguments
   GET_VREG r1, ip
   UPDATE_REGISTERS_FOR_STRING_INIT r1, r0
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
// Uses ip and lr as temporaries.
.macro FETCH_FROM_THREAD_CACHE dest_reg, slow_path
   add      ip, rSELF, #THREAD_INTERPRETER_CACHE_OFFSET       // cache address
   ubfx     lr, rPC, #2, #THREAD_INTERPRETER_CACHE_SIZE_LOG2  // entry index
   add      ip, ip, lr, lsl #3             // entry address within the cache
   ldr      \dest_reg, [ip, #4]            // value (offset)
   ldr      ip, [ip]                       // entry key (pc)
   cmp      ip, rPC
   bne \slow_path
.endm

// Helper for static field get.
.macro OP_SGET load="ldr", wide="0"
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 4f
1:
   ldr r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   ldr r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp rMR, #0
   bne 3f
2:
   lsr r2, rINST, #8              // w2 <- A
   .if \wide
   add r0, r0, r1
   ldrd r0, r1, [r0]
   CLEAR_SHADOW_PAIR r2, ip, lr
   VREG_INDEX_TO_ADDR r2, r2
   SET_VREG_WIDE_BY_ADDR r0, r1, r2      // fp[A] <- value
   .else
   \load r0, [r0, r1]
   SET_VREG r0, r2               // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   bl art_quick_read_barrier_mark_reg00
   b 2b
4:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   EXPORT_PC
   bl nterp_get_static_field
   tst r0, #1
   beq 1b
   CLEAR_STATIC_VOLATILE_MARKER r0
   ldr r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   ldr r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp rMR, #0
   bne 7f
5:
   lsr r2, rINST, #8              // w2 <- A
   .if \wide
   dmb ish
   add ip, r0, r1
6:
   ldrexd   r0, r1, [ip]
   strexd   r3, r0, r1, [ip]
   cmp      r3, #0
   bne      6b
   dmb ish
   CLEAR_SHADOW_PAIR r2, ip, lr
   VREG_INDEX_TO_ADDR r2, r2
   SET_VREG_WIDE_BY_ADDR r0, r1, r2      // fp[A] <- value
   .else
   dmb ish
   \load r3, [r0, r1]
   dmb ish
   SET_VREG r3, r2               // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
7:
   bl art_quick_read_barrier_mark_reg00
   b 5b
.endm

// Helper for static field put.
.macro OP_SPUT store="str", wide="0"
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 4f
1:
   ldr r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   ldr r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp rMR, #0
   bne 3f
2:
   lsr r2, rINST, #8              // w2 <- A
   .if \wide
   VREG_INDEX_TO_ADDR r2, r2
   GET_VREG_WIDE_BY_ADDR r2, r3, r2      // fp[A] <- value
   add r0, r0, r1
   strd r2, r3, [r0]
   .else
   GET_VREG r2, r2                // w2 <- v[A]
   \store    r2, [r0, r1]
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   bl art_quick_read_barrier_mark_reg00
   b 2b
4:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   EXPORT_PC
   bl nterp_get_static_field
   tst r0, #1
   beq 1b
   CLEAR_STATIC_VOLATILE_MARKER r0
   ldr r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   ldr r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp rMR, #0
   bne 6f
5:
   lsr r2, rINST, #8              // r2 <- A
   .if \wide
   VREG_INDEX_TO_ADDR r2, r2
   GET_VREG_WIDE_BY_ADDR r2, r3, r2
   add ip, r0, r1
   dmb ish
7:
   ldrexd r0, r1, [ip]
   strexd r0, r2, r3, [ip]
   cmp r0, #0
   bne 7b
   dmb ish
   .else
   GET_VREG r2, r2                // r2 <- v[A]
   dmb ish
   \store r2, [r0, r1]
   dmb ish
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
6:
   bl art_quick_read_barrier_mark_reg00
   b 5b
.endm


// Helper for instance field put.
.macro OP_IPUT store="str", wide="0":
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 2f
1:
   ubfx    r1, rINST, #8, #4           // r1<- A
   lsr     r4, rINST, #12              // r2<- B
   GET_VREG r4, r4                     // vB (object we're operating on)
   cmp r4, #0
   beq common_errNullObject
   .if \wide
   VREG_INDEX_TO_ADDR r1, r1
   GET_VREG_WIDE_BY_ADDR r2, r3, r1      // fp[A] <- value
   add r4, r4, r0
   strd r2, r3, [r4]
   .else
   GET_VREG r1, r1                     // r1 <- v[A]
   \store r1, [r4, r0]
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
2:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   EXPORT_PC
   bl nterp_get_instance_field_offset
   cmp r0, #0
   bge 1b
   CLEAR_INSTANCE_VOLATILE_MARKER r0
   ubfx    r1, rINST, #8, #4           // r1<- A
   lsr     r4, rINST, #12              // r2<- B
   GET_VREG r4, r4                     // vB (object we're operating on)
   cmp r4, #0
   beq common_errNullObject
   .if \wide
   VREG_INDEX_TO_ADDR r1, r1
   GET_VREG_WIDE_BY_ADDR r2, r3, r1
   add ip, r4, r0
   dmb ish
3:
   ldrexd r0, r1, [ip]
   strexd r0, r2, r3, [ip]
   cmp r0, #0
   bne 3b
   dmb ish
   .else
   GET_VREG r1, r1                     // r1 <- v[A]
   dmb ish
   \store r1, [r4, r0]
   dmb ish
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.endm

// Helper for instance field get.
.macro OP_IGET load="ldr", wide="0"
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 2f
1:
   lsr     r2, rINST, #12              // w2<- B
   GET_VREG r3, r2                     // w3<- object we're operating on
   ubfx    r2, rINST, #8, #4           // w2<- A
   cmp     r3, #0
   beq common_errNullObject    // object was null
   .if \wide
   add r3, r3, r0
   ldrd r0, r1, [r3]
   CLEAR_SHADOW_PAIR r2, ip, lr
   VREG_INDEX_TO_ADDR r2, r2
   SET_VREG_WIDE_BY_ADDR r0, r1, r2      // fp[A] <- value
   .else
   \load r0, [r3, r0]
   SET_VREG r0, r2                     // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
2:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   EXPORT_PC
   bl nterp_get_instance_field_offset
   cmp r0, #0
   bge 1b
   CLEAR_INSTANCE_VOLATILE_MARKER r0
   lsr     r2, rINST, #12              // r2<- B
   GET_VREG r3, r2                     // r3<- object we're operating on
   ubfx    r2, rINST, #8, #4           // r2<- A
   cmp     r3, #0
   beq common_errNullObject    // object was null
   .if \wide
   dmb ish
   add ip, r3, r0
3:
   ldrexd   r0, r1, [ip]
   strexd   r3, r0, r1, [ip]
   cmp      r3, #0
   bne      3b
   dmb ish
   CLEAR_SHADOW_PAIR r2, ip, lr
   VREG_INDEX_TO_ADDR r2, r2
   SET_VREG_WIDE_BY_ADDR r0, r1, r2      // fp[A] <- value
   dmb ish
   .else
   dmb ish
   \load r0, [r3, r0]
   dmb ish
   SET_VREG r0, r2                     // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.endm

// Puts the next int/long/object parameter passed in physical register
// in the expected dex register array entry, and in case of object in the
// expected reference array entry.
// Uses ip as temporary.
.macro LOOP_OVER_SHORTY_STORING_GPRS gpr_32, shorty, arg_offset, regs, refs, finished, if_long, is_r3
1: // LOOP
    ldrb ip, [\shorty], #1       // Load next character in shorty, and increment.
    cmp ip, #0
    beq \finished            // if (ip == '\0') goto finished
    cmp ip, #74                  // if (ip == 'J') goto FOUND_LONG
    beq 2f
    cmp ip, #70                  // if (ip == 'F') goto SKIP_FLOAT
    beq 3f
    cmp ip, #68                  // if (ip == 'D') goto SKIP_DOUBLE
    beq 4f
    str \gpr_32, [\regs, \arg_offset]
    cmp ip, #76                  // if (ip != 'L') goto NOT_REFERENCE
    bne 6f
    str \gpr_32, [\refs, \arg_offset]
6:  // NOT_REFERENCE
    add \arg_offset, \arg_offset, #4
    b 5f
2:  // FOUND_LONG
    .if \is_r3
    // Put back shorty and jump to \if_long
    sub \shorty, \shorty, #1
    .else
    // A long can only be in r2, r3
    str r2, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #4
    str r3, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #4
    .endif
    b \if_long
3:  // SKIP_FLOAT
    add \arg_offset, \arg_offset, #4
    b 1b
4:  // SKIP_DOUBLE
    add \arg_offset, \arg_offset, #8
    b 1b
5:
.endm

// Puts the next int/long/object parameter passed in stack
// in the expected dex register array entry, and in case of object in the
// expected reference array entry.
.macro LOOP_OVER_INTs shorty, arg_offset, regs, refs, stack_ptr, tmp1, tmp2, finished
1: // LOOP
    ldrb \tmp1, [\shorty], #1       // Load next character in shorty, and increment.
    cmp \tmp1, #0
    beq \finished                   // if (\tmp1 == '\0') goto finished
    cmp \tmp1, #74                  // if (\tmp1 == 'J') goto FOUND_LONG
    beq 2f
    cmp \tmp1, #70                  // if (\tmp1 == 'F') goto SKIP_FLOAT
    beq 3f
    cmp \tmp1, #68                  // if (\tmp1 == 'D') goto SKIP_DOUBLE
    beq 4f
    add \tmp2, \stack_ptr, \arg_offset
    ldr \tmp2, [\tmp2,  #OFFSET_TO_FIRST_ARGUMENT_IN_STACK]
    str \tmp2, [\regs, \arg_offset]
    cmp \tmp1, #76                  // if (\tmp1 != 'L') goto loop
    bne 3f
    str \tmp2, [\refs, \arg_offset]
    add \arg_offset, \arg_offset, #4
    b 1b
2:  // FOUND_LONG
    add \tmp1, \stack_ptr, \arg_offset
    ldr \tmp1, [\tmp1,  #OFFSET_TO_FIRST_ARGUMENT_IN_STACK]
    str \tmp1, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #4
    add \tmp1, \stack_ptr, \arg_offset
    ldr \tmp1, [\tmp1,  #OFFSET_TO_FIRST_ARGUMENT_IN_STACK]
    str \tmp1, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #4
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
 *  r0   ArtMethod* callee
 *  rest  method parameters
 */

OAT_ENTRY ExecuteNterpImpl, EndExecuteNterpImpl
    .cfi_startproc
    sub ip, sp, #STACK_OVERFLOW_RESERVED_BYTES
    ldr ip, [ip]
    /* Spill callee save regs */
    SPILL_ALL_CALLEE_SAVES

    // TODO: Get shorty in a better way and remove below
    SPILL_ALL_ARGUMENTS

    bl NterpGetShorty
    // Save shorty in callee-save rIBASE.
    mov rIBASE, r0

    RESTORE_ALL_ARGUMENTS

    ldr rPC, [r0, #ART_METHOD_DATA_OFFSET_32]

    // Setup the stack for executing the method.
    SETUP_STACK_FRAME rPC, rREFS, rFP, CFI_REFS, load_ins=1

    // Setup the parameters
    cmp r4, #0
    beq .Lxmm_setup_finished

    sub r4, rINST, r4
    lsl r4, r4, #2 // r4 is now the offset for inputs into the registers array.

    mov lr, ip // lr contains the old stack pointer

    ldr ip, [r0, #ART_METHOD_ACCESS_FLAGS_OFFSET]
    // r0 is now available.
    // Setup shorty, pointer to inputs in FP and pointer to inputs in REFS
    add r0, rIBASE, #1  // shorty + 1  ; ie skip return arg character
    add r7, rFP, r4
    add r8, rREFS, r4
    tst ip, #ART_METHOD_IS_STATIC_FLAG
    bne .Lhandle_static_method
    str r1, [r7], #4
    str r1, [r8], #4
    add lr, lr, #4
    mov r4, #0
    b .Lcontinue_setup_gprs
.Lhandle_static_method:
    mov r4, #0
    LOOP_OVER_SHORTY_STORING_GPRS r1, r0, r4, r7, r8, .Lgpr_setup_finished, .Lif_long, is_r3=0
.Lcontinue_setup_gprs:
    LOOP_OVER_SHORTY_STORING_GPRS r2, r0, r4, r7, r8, .Lgpr_setup_finished, .Lif_long, is_r3=0
    LOOP_OVER_SHORTY_STORING_GPRS r3, r0, r4, r7, r8, .Lgpr_setup_finished, .Lif_long, is_r3=1
.Lif_long:
    LOOP_OVER_INTs r0, r4, r7, r8, lr, ip, r1, .Lgpr_setup_finished
.Lgpr_setup_finished:
    add r0, rIBASE, #1  // shorty + 1  ; ie skip return arg character
    mov r1, r7
    add r2, lr, #OFFSET_TO_FIRST_ARGUMENT_IN_STACK
    vpush {s0-s15}
    mov r3, sp
    bl NterpStoreArm32Fprs
    add sp, sp, #(16 * 4)
.Lxmm_setup_finished:
    CFI_DEFINE_DEX_PC_WITH_OFFSET(CFI_TMP, CFI_DEX, 0)
    // r8 was used for setting up the frame, restore it now.
    REFRESH_MARKING_REGISTER
.Lexecute_instructions:
    // Set rIBASE
    adr rIBASE, artNterpAsmInstructionStart
    /* start executing the instruction at rPC */
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
// as the assembler compiled each interpreter separately.
common_errDivideByZero:
    EXPORT_PC
    bl art_quick_throw_div_zero

// Expect index in r1, length in r3
common_errArrayIndex:
    EXPORT_PC
    mov r0, r1
    mov r1, r3
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
   FETCH_FROM_THREAD_CACHE r0, 2f
   cmp rMR, #0
   bne 3f
4:
   ldr lr, [rSELF, #THREAD_ALLOC_OBJECT_ENTRYPOINT_OFFSET]
   blx lr
1:
   lsr r1, rINST, #8                    // r1 <- A
   SET_VREG_OBJECT r0, r1               // fp[A] <- value
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
2:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   bl nterp_get_class_or_allocate_object
   b 1b
3:
   bl art_quick_read_barrier_mark_reg00
   b 4b

NterpNewArray:
   /* new-array vA, vB, class@CCCC */
   EXPORT_PC
   // Fast-path which gets the class from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 2f
   cmp rMR, #0
   bne 3f
1:
   lsr     r1, rINST, #12              // r1<- B
   GET_VREG r1, r1                     // r1<- vB (array length)
   ldr lr, [rSELF, #THREAD_ALLOC_ARRAY_ENTRYPOINT_OFFSET]
   blx lr
   ubfx    r1, rINST, #8, #4           // r1<- A
   SET_VREG_OBJECT r0, r1
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
2:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   bl nterp_get_class_or_allocate_object
   b 1b
3:
   bl art_quick_read_barrier_mark_reg00
   b 1b

NterpPutObjectInstanceField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 3f
1:
   ubfx    r1, rINST, #8, #4           // r1<- A
   lsr     r2, rINST, #12              // r2<- B
   GET_VREG r2, r2                     // vB (object we're operating on)
   cmp r2, #0
   beq common_errNullObject            // is object null?
   GET_VREG r1, r1                     // r1 <- v[A]
   str r1, [r2, r0]
4:
   cmp r1, #0
   beq 2f
   ldr r1, [rSELF, #THREAD_CARD_TABLE_OFFSET]
   lsr r3, r2, #CARD_TABLE_CARD_SHIFT
   strb r1, [r1, r3]
2:
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   EXPORT_PC
   bl nterp_get_instance_field_offset
   cmp r0, #0
   bge 1b
   CLEAR_INSTANCE_VOLATILE_MARKER r0
   ubfx    r1, rINST, #8, #4           // r1<- A
   lsr     r2, rINST, #12              // r2<- B
   GET_VREG r2, r2                     // vB (object we're operating on)
   cmp r2, #0
   beq common_errNullObject            // is object null?
   GET_VREG r1, r1                     // r1 <- v[A]
   dmb ish
   str r1, [r2, r0]
   dmb ish
   b 4b

NterpGetObjectInstanceField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 4f
1:
   ubfx    r1, rINST, #8, #4           // r1<- A
   lsr     r2, rINST, #12              // r2<- B
   GET_VREG r2, r2                     // vB (object we're operating on)
   cmp r2, #0
   beq common_errNullObject
   ldr r0, [r2, r0]
7:
   cmp rMR, #0
   bne 3f
2:
   SET_VREG_OBJECT r0, r1              // fp[A] <- value
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   bl art_quick_read_barrier_mark_reg00
   b 2b
4:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   EXPORT_PC
   bl nterp_get_instance_field_offset
   cmp r0, #0
   bge 1b
   CLEAR_INSTANCE_VOLATILE_MARKER r0
   ubfx    r1, rINST, #8, #4           // r1<- A
   lsr     r2, rINST, #12              // r2<- B
   GET_VREG r2, r2                     // vB (object we're operating on)
   cmp r2, #0
   beq common_errNullObject
   dmb ish
   ldr r0, [r2, r0]
   dmb ish
   b 7b

NterpPutObjectStaticField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 5f
1:
   ldr r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   ldr r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp rMR, #0
   bne 4f
2:
   lsr r2, rINST, #8                    // w2 <- A
   GET_VREG r2, r2
   str r2, [r0, r1]
8:
   cmp r2, #0
   beq 3f
   ldr r1, [rSELF, #THREAD_CARD_TABLE_OFFSET]
   lsr r3, r0, #CARD_TABLE_CARD_SHIFT
   strb r1, [r1, r3]
3:
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
4:
   bl art_quick_read_barrier_mark_reg00
   b 2b
5:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   EXPORT_PC
   bl nterp_get_static_field
   tst r0, #1
   beq 1b
   CLEAR_STATIC_VOLATILE_MARKER r0
   ldr r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   ldr r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp rMR, #0
   bne 7f
6:
   lsr r2, rINST, #8                    // 21 <- A
   GET_VREG r2, r2
   dmb ish
   str r2, [r0, r1]
   dmb ish
   b 8b
7:
   bl art_quick_read_barrier_mark_reg00
   b 6b

NterpGetObjectStaticField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 4f
1:
   ldr r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   ldr r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp rMR, #0
   bne 3f
   ldr r0, [r0, r1]
   // No need to check the marking register, we know it's not set here.
2:
   lsr r1, rINST, #8                    // r1 <- A
   SET_VREG_OBJECT r0, r1               // fp[A] <- value
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   bl art_quick_read_barrier_mark_reg00
   ldr r0, [r0, r1]
   // Here, we know the marking register is set.
   bl art_quick_read_barrier_mark_reg00
   b 2b
4:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   EXPORT_PC
   bl nterp_get_static_field
   tst r0, #1
   beq 1b
   CLEAR_STATIC_VOLATILE_MARKER r0
   ldr r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   ldr r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp rMR, #0
   bne 7f
5:
   dmb ish
   ldr r0, [r0, r1]
   dmb ish
   cmp rMR, #0
   bne 8f
   b 2b
7:
   bl art_quick_read_barrier_mark_reg00
   b 5b
8:
   bl art_quick_read_barrier_mark_reg00
   b 2b

NterpGetBooleanStaticField:
  OP_SGET load="ldrb", wide=0

NterpGetByteStaticField:
  OP_SGET load="ldrsb", wide=0

NterpGetCharStaticField:
  OP_SGET load="ldrh", wide=0

NterpGetShortStaticField:
  OP_SGET load="ldrsh", wide=0

NterpGetWideStaticField:
  OP_SGET load="ldr", wide=1

NterpGetIntStaticField:
  OP_SGET load="ldr", wide=0

NterpPutStaticField:
  OP_SPUT store="str", wide=0

NterpPutBooleanStaticField:
NterpPutByteStaticField:
  OP_SPUT store="strb", wide=0

NterpPutCharStaticField:
NterpPutShortStaticField:
  OP_SPUT store="strh", wide=0

NterpPutWideStaticField:
  OP_SPUT store="str", wide=1

NterpPutInstanceField:
  OP_IPUT store="str", wide=0

NterpPutBooleanInstanceField:
NterpPutByteInstanceField:
  OP_IPUT store="strb", wide=0

NterpPutCharInstanceField:
NterpPutShortInstanceField:
  OP_IPUT store="strh", wide=0

NterpPutWideInstanceField:
  OP_IPUT store="str", wide=1

NterpGetBooleanInstanceField:
  OP_IGET load="ldrb", wide=0

NterpGetByteInstanceField:
  OP_IGET load="ldrsb", wide=0

NterpGetCharInstanceField:
  OP_IGET load="ldrh", wide=0

NterpGetShortInstanceField:
  OP_IGET load="ldrsh", wide=0

NterpGetWideInstanceField:
  OP_IGET load="ldr", wide=1

NterpGetInstanceField:
  OP_IGET load="ldr", wide=0

NterpInstanceOf:
   /* instance-of vA, vB, class@CCCC */
   // Fast-path which gets the class from thread-local cache.
   EXPORT_PC
   FETCH_FROM_THREAD_CACHE r1, 3f
   cmp rMR, #0
   bne 4f
1:
   lsr     r2, rINST, #12              // r2<- B
   GET_VREG r0, r2                     // r0<- vB (object)
   cmp r0, #0
   beq 2f
   bl artInstanceOfFromCode
2:
   ubfx    r1, rINST, #8, #4           // r1<- A
   SET_VREG r0, r1
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   bl nterp_get_class_or_allocate_object
   mov r1, r0
   b 1b
4:
   bl art_quick_read_barrier_mark_reg01
   b 1b

NterpCheckCast:
   // Fast-path which gets the class from thread-local cache.
   EXPORT_PC
   FETCH_FROM_THREAD_CACHE r1, 3f
   cmp rMR, #0
   bne 4f
1:
   lsr     r2, rINST, #8               // r2<- A
   GET_VREG r0, r2                     // r0<- vA (object)
   cmp r0, #0
   beq 2f
   bl art_quick_check_instance_of
2:
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   bl nterp_get_class_or_allocate_object
   mov r1, r0
   b 1b
4:
   bl art_quick_read_barrier_mark_reg01
   b 1b

NterpHandleInvokeInterfaceOnObjectMethodRange:
   // First argument is the 'this' pointer.
   FETCH r1, 2
   GET_VREG r1, r1
   // Note: if r1 is null, this will be handled by our SIGSEGV handler.
   ldr r2, [r1, #MIRROR_OBJECT_CLASS_OFFSET]
   add r2, r2, #MIRROR_CLASS_VTABLE_OFFSET_32
   ldr r0, [r2, r0, lsl #2]
   b NterpCommonInvokeInstanceRange

NterpHandleInvokeInterfaceOnObjectMethod:
   // First argument is the 'this' pointer.
   FETCH r1, 2
   and r1, r1, #0xf
   GET_VREG r1, r1
   // Note: if r1 is null, this will be handled by our SIGSEGV handler.
   ldr r2, [r1, #MIRROR_OBJECT_CLASS_OFFSET]
   add r2, r2, #MIRROR_CLASS_VTABLE_OFFSET_32
   ldr r0, [r2, r0, lsl #2]
   b NterpCommonInvokeInstance

NterpHandleHotnessOverflow:
    add r1, rPC, rINST, lsl #1
    mov r2, rFP
    bl nterp_hot_method
    cmp r0, #0
    bne 1f
    add r2, rINST, rINST                // w2<- byte offset
    FETCH_ADVANCE_INST_RB r2            // update rPC, load rINST
    GET_INST_OPCODE ip                  // extract opcode from rINST
    GOTO_OPCODE ip                      // jump to next instruction
1:
    // Drop the current frame.
    ldr ip, [rREFS, #-4]
    mov sp, ip
    .cfi_def_cfa sp, CALLEE_SAVES_SIZE

    // The transition frame of type SaveAllCalleeSaves saves r4, r8, and r9,
    // but not managed ABI. So we need to restore callee-saves of the nterp frame,
    // and save managed ABI callee saves, which will be restored by the callee upon
    // return.

    RESTORE_ALL_CALLEE_SAVES
    push {r5-r7, r10-r11, lr}
   .cfi_adjust_cfa_offset 24
   .cfi_rel_offset r5, 0
   .cfi_rel_offset r6, 4
   .cfi_rel_offset r7, 8
   .cfi_rel_offset r10, 12
   .cfi_rel_offset r11, 16
   .cfi_rel_offset lr, 20
    vpush {s16-s31}
    .cfi_adjust_cfa_offset 64

    // Setup the new frame
    ldr r1, [r0, #OSR_DATA_FRAME_SIZE]
    // Given stack size contains all callee saved registers, remove them.
    sub r1, r1, #(CALLEE_SAVES_SIZE - 12)

    // We know r1 cannot be 0, as it at least contains the ArtMethod.

    // Remember CFA in a callee-save register.
    mov rINST, sp
    .cfi_def_cfa_register rINST

    sub sp, sp, r1

    add r2, r0, #OSR_DATA_MEMORY
2:
    sub r1, r1, #4
    ldr ip, [r2, r1]
    str ip, [sp, r1]
    cmp r1, #0
    bne 2b

    // Fetch the native PC to jump to and save it in a callee-save register.
    ldr rFP, [r0, #OSR_DATA_NATIVE_PC]

    // Free the memory holding OSR Data.
    bl free

    // Jump to the compiled code.
    bx rFP
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
    SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=1, is_string_init=0
    .cfi_endproc

nterp_to_nterp_string_init_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=1
    .cfi_endproc

nterp_to_nterp_instance_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=0
    .cfi_endproc

// This is the end of PCs contained by the OatQuickMethodHeader created for the interpreter
// entry point.
    .type EndExecuteNterpImpl, #function
    .hidden EndExecuteNterpImpl
    .global EndExecuteNterpImpl
EndExecuteNterpImpl:

/*
 * Convert the double in r0/r1 to a long in r0/r1.
 *
 * We have to clip values to long min/max per the specification.  The
 * expected common case is a "reasonable" value that converts directly
 * to modest integer.  The EABI convert function isn't doing this for us.
 */
nterp_d2l_doconv:
    ubfx    r2, r1, #20, #11            @ grab the exponent
    movw    r3, #0x43e
    cmp     r2, r3                      @ MINLONG < x > MAXLONG?
    bhs     d2l_special_cases
    b       __aeabi_d2lz                @ tail call to convert double to long
d2l_special_cases:
    movw    r3, #0x7ff
    cmp     r2, r3
    beq     d2l_maybeNaN                @ NaN?
d2l_notNaN:
    adds    r1, r1, r1                  @ sign bit to carry
    mov     r0, #0xffffffff             @ assume maxlong for lsw
    mov     r1, #0x7fffffff             @ assume maxlong for msw
    adc     r0, r0, #0
    adc     r1, r1, #0                  @ convert maxlong to minlong if exp negative
    bx      lr                          @ return
d2l_maybeNaN:
    orrs    r3, r0, r1, lsl #12
    beq     d2l_notNaN                  @ if fraction is non-zero, it's a NaN
    mov     r0, #0
    mov     r1, #0
    bx      lr                          @ return 0 for NaN

/*
 * Convert the float in r0 to a long in r0/r1.
 *
 * We have to clip values to long min/max per the specification.  The
 * expected common case is a "reasonable" value that converts directly
 * to modest integer.  The EABI convert function isn't doing this for us.
 */
nterp_f2l_doconv:
    ubfx    r2, r0, #23, #8             @ grab the exponent
    cmp     r2, #0xbe                   @ MININT < x > MAXINT?
    bhs     f2l_special_cases
    b       __aeabi_f2lz                @ tail call to convert float to long
f2l_special_cases:
    cmp     r2, #0xff                   @ NaN or infinity?
    beq     f2l_maybeNaN
f2l_notNaN:
    adds    r0, r0, r0                  @ sign bit to carry
    mov     r0, #0xffffffff             @ assume maxlong for lsw
    mov     r1, #0x7fffffff             @ assume maxlong for msw
    adc     r0, r0, #0
    adc     r1, r1, #0                  @ convert maxlong to minlong if exp negative
    bx      lr                          @ return
f2l_maybeNaN:
    lsls    r3, r0, #9
    beq     f2l_notNaN                  @ if fraction is non-zero, it's a NaN
    mov     r0, #0
    mov     r1, #0
    bx      lr                          @ return 0 for NaN

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

    .type artNterpAsmInstructionEnd, #object
    .hidden artNterpAsmInstructionEnd
    .global artNterpAsmInstructionEnd
artNterpAsmInstructionEnd:
    // artNterpAsmInstructionEnd is used as landing pad for exception handling.
    FETCH_INST
    GET_INST_OPCODE ip
    GOTO_OPCODE ip

%def instruction_start():

    .type artNterpAsmInstructionStart, #object
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
