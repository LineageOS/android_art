%def header():
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

/*
 * This is a #include, not a %include, because we want the C pre-processor
 * to expand the macros into assembler assignment statements.
 */
#include "asm_support.h"
#include "arch/x86_64/asm_support_x86_64.S"
#include "interpreter/cfi_asm_support.h"

/**
 * x86_64 ABI general notes:
 *
 * Caller save set:
 *    rax, rdx, rcx, rsi, rdi, r8-r11, st(0)-st(7)
 * Callee save set:
 *    rbx, rbp, r12-r15
 * Return regs:
 *    32-bit in eax
 *    64-bit in rax
 *    fp on xmm0
 *
 * First 8 fp parameters came in xmm0-xmm7.
 * First 6 non-fp parameters came in rdi, rsi, rdx, rcx, r8, r9.
 * Other parameters passed on stack, pushed right-to-left.  On entry to target, first
 * param is at 8(%esp).
 *
 * Stack must be 16-byte aligned to support SSE in native code.
 */

#define IN_ARG3        %rcx
#define IN_ARG2        %rdx
#define IN_ARG1        %rsi
#define IN_ARG0        %rdi
/* Out Args  */
#define OUT_ARG3       %rcx
#define OUT_ARG2       %rdx
#define OUT_ARG1       %rsi
#define OUT_ARG0       %rdi
#define OUT_32_ARG3    %ecx
#define OUT_32_ARG2    %edx
#define OUT_32_ARG1    %esi
#define OUT_32_ARG0    %edi
#define OUT_FP_ARG1    %xmm1
#define OUT_FP_ARG0    %xmm0

/*
 * single-purpose registers, given names for clarity
 */
#define rSELF    %gs
#define rPC      %r12
#define CFI_DEX  12 // DWARF register number of the register holding dex-pc (rPC).
#define CFI_TMP  5  // DWARF register number of the first argument register (rdi).
#define rFP      %r13
#define rINST    %ebx
#define rINSTq   %rbx
#define rINSTw   %bx
#define rINSTbh  %bh
#define rINSTbl  %bl
#define rIBASE   %r14
#define rREFS    %r15
#define CFI_REFS 15 // DWARF register number of the reference array (r15).

// Temporary registers while setting up a frame.
#define rNEW_FP   %r8
#define rNEW_REFS %r9
#define CFI_NEW_REFS 9

/*
 * Get/set the 32-bit value from a Dalvik register.
 */
#define VREG_ADDRESS(_vreg) (rFP,_vreg,4)
#define VREG_HIGH_ADDRESS(_vreg) 4(rFP,_vreg,4)
#define VREG_REF_ADDRESS(_vreg) (rREFS,_vreg,4)
#define VREG_REF_HIGH_ADDRESS(_vreg) 4(rREFS,_vreg,4)

// Includes the return address implictly pushed on stack by 'call'.
#define CALLEE_SAVES_SIZE (6 * 8 + 4 * 8 + 1 * 8)

// +8 for the ArtMethod of the caller.
#define OFFSET_TO_FIRST_ARGUMENT_IN_STACK (CALLEE_SAVES_SIZE + 8)

/*
 * Refresh rINST.
 * At enter to handler rINST does not contain the opcode number.
 * However some utilities require the full value, so this macro
 * restores the opcode number.
 */
.macro REFRESH_INST _opnum
    movb    rINSTbl, rINSTbh
    movb    $$\_opnum, rINSTbl
.endm

/*
 * Fetch the next instruction from rPC into rINSTw.  Does not advance rPC.
 */
.macro FETCH_INST
    movzwq  (rPC), rINSTq
.endm

/*
 * Remove opcode from rINST, compute the address of handler and jump to it.
 */
.macro GOTO_NEXT
    movzx   rINSTbl,%ecx
    movzbl  rINSTbh,rINST
    shll    MACRO_LITERAL(${handler_size_bits}), %ecx
    addq    rIBASE, %rcx
    jmp     *%rcx
.endm

/*
 * Advance rPC by instruction count.
 */
.macro ADVANCE_PC _count
    leaq    2*\_count(rPC), rPC
.endm

/*
 * Advance rPC by instruction count, fetch instruction and jump to handler.
 */
.macro ADVANCE_PC_FETCH_AND_GOTO_NEXT _count
    ADVANCE_PC \_count
    FETCH_INST
    GOTO_NEXT
.endm

.macro GET_VREG _reg _vreg
    movl    VREG_ADDRESS(\_vreg), \_reg
.endm

.macro GET_VREG_OBJECT _reg _vreg
    movl    VREG_REF_ADDRESS(\_vreg), \_reg
.endm

/* Read wide value. */
.macro GET_WIDE_VREG _reg _vreg
    movq    VREG_ADDRESS(\_vreg), \_reg
.endm

.macro SET_VREG _reg _vreg
    movl    \_reg, VREG_ADDRESS(\_vreg)
    movl    MACRO_LITERAL(0), VREG_REF_ADDRESS(\_vreg)
.endm

/* Write wide value. reg is clobbered. */
.macro SET_WIDE_VREG _reg _vreg
    movq    \_reg, VREG_ADDRESS(\_vreg)
    xorq    \_reg, \_reg
    movq    \_reg, VREG_REF_ADDRESS(\_vreg)
.endm

.macro SET_VREG_OBJECT _reg _vreg
    movl    \_reg, VREG_ADDRESS(\_vreg)
    movl    \_reg, VREG_REF_ADDRESS(\_vreg)
.endm

.macro GET_VREG_HIGH _reg _vreg
    movl    VREG_HIGH_ADDRESS(\_vreg), \_reg
.endm

.macro SET_VREG_HIGH _reg _vreg
    movl    \_reg, VREG_HIGH_ADDRESS(\_vreg)
    movl    MACRO_LITERAL(0), VREG_REF_HIGH_ADDRESS(\_vreg)
.endm

.macro CLEAR_REF _vreg
    movl    MACRO_LITERAL(0), VREG_REF_ADDRESS(\_vreg)
.endm

.macro CLEAR_WIDE_REF _vreg
    movl    MACRO_LITERAL(0), VREG_REF_ADDRESS(\_vreg)
    movl    MACRO_LITERAL(0), VREG_REF_HIGH_ADDRESS(\_vreg)
.endm

.macro GET_VREG_XMMs _xmmreg _vreg
    movss VREG_ADDRESS(\_vreg), \_xmmreg
.endm
.macro GET_VREG_XMMd _xmmreg _vreg
    movsd VREG_ADDRESS(\_vreg), \_xmmreg
.endm
.macro SET_VREG_XMMs _xmmreg _vreg
    movss \_xmmreg, VREG_ADDRESS(\_vreg)
.endm
.macro SET_VREG_XMMd _xmmreg _vreg
    movsd \_xmmreg, VREG_ADDRESS(\_vreg)
.endm

// An assembly entry that has a OatQuickMethodHeader prefix.
.macro OAT_ENTRY name, end
    FUNCTION_TYPE(\name)
    ASM_HIDDEN SYMBOL(\name)
    .global SYMBOL(\name)
    .balign 16
    .long 0
    .long (SYMBOL(\end) - SYMBOL(\name))
SYMBOL(\name):
.endm

.macro ENTRY name
    .text
    ASM_HIDDEN SYMBOL(\name)
    .global SYMBOL(\name)
    FUNCTION_TYPE(\name)
SYMBOL(\name):
.endm

.macro END name
    SIZE(\name)
.endm

// Macro for defining entrypoints into runtime. We don't need to save registers
// (we're not holding references there), but there is no
// kDontSave runtime method. So just use the kSaveRefsOnly runtime method.
.macro NTERP_TRAMPOLINE name, helper
DEFINE_FUNCTION \name
  SETUP_SAVE_REFS_ONLY_FRAME
  call \helper
  RESTORE_SAVE_REFS_ONLY_FRAME
  RETURN_OR_DELIVER_PENDING_EXCEPTION
END_FUNCTION \name
.endm

.macro CLEAR_VOLATILE_MARKER reg
  andq MACRO_LITERAL(-2), \reg
.endm

.macro EXPORT_PC
    movq    rPC, -16(rREFS)
.endm


.macro BRANCH
    // Update method counter and do a suspend check if the branch is negative.
    testq rINSTq, rINSTq
    js 3f
2:
    leaq    (rPC, rINSTq, 2), rPC
    FETCH_INST
    GOTO_NEXT
3:
    movq (%rsp), %rdi
#if (NTERP_HOTNESS_MASK != 0xffff)
#error "Nterp x86_64 expects Nterp hotness mask to be 0xffff"
#endif
    addw $$1, ART_METHOD_HOTNESS_COUNT_OFFSET(%rdi)
    // If the counter overflows, handle this in the runtime.
    jo NterpHandleHotnessOverflow
    // Otherwise, do a suspend check.
    testl   $$(THREAD_SUSPEND_OR_CHECKPOINT_REQUEST), rSELF:THREAD_FLAGS_OFFSET
    jz      2b
    EXPORT_PC
    call    SYMBOL(art_quick_test_suspend)
    jmp 2b
.endm

// Setup the stack to start executing the method. Expects:
// - rdi to contain the ArtMethod
// - rbx, r10, r11 to be available.
//
// Outputs
// - rbx contains the dex registers size
// - r11 contains the old stack pointer.
.macro SETUP_STACK_FRAME code_item, refs, fp, cfi_refs
    // Fetch dex register size.
    movzwl CODE_ITEM_REGISTERS_SIZE_OFFSET(\code_item), %ebx
    // Fetch outs size.
    movzwq CODE_ITEM_OUTS_SIZE_OFFSET(\code_item), \refs

    // Compute required frame size for dex registers: ((2 * ebx) + refs)
    leaq (\refs, %rbx, 2), %r11
    salq $$2, %r11

    // Compute new stack pointer in r10: add 24 for saving the previous frame,
    // pc, and method being executed.
    leaq -24(%rsp), %r10
    subq %r11, %r10
    // Alignment
    // Note: There may be two pieces of alignment but there is no need to align
    // out args to `kPointerSize` separately before aligning to kStackAlignment.
    andq $$-16, %r10

    // Set reference and dex registers, align to pointer size for previous frame and dex pc.
    leaq 24 + 4(%r10, \refs, 4), \refs
    andq LITERAL(-__SIZEOF_POINTER__), \refs
    leaq (\refs, %rbx, 4), \fp

    // Now setup the stack pointer.
    movq %rsp, %r11
    CFI_DEF_CFA_REGISTER(r11)
    movq %r10, %rsp
    movq %r11, -8(\refs)
    CFI_DEFINE_CFA_DEREF(\cfi_refs, -8, (6 + 4 + 1) * 8)

    // Put nulls in reference frame.
    testl %ebx, %ebx
    je 2f
    movq \refs, %r10
1:
    movl $$0, (%r10)
    addq $$4, %r10
    cmpq %r10, \fp
    jne 1b
2:
    // Save the ArtMethod.
    movq %rdi, (%rsp)
.endm

// Puts the next floating point argument into the expected register,
// fetching values based on a non-range invoke.
// Uses rax as temporary.
//
// TODO: We could simplify a lot of code by loading the G argument into
// the "inst" register. Given that we enter the handler with "1(rPC)" in
// the rINST, we can just add rINST<<16 to the args and we don't even
// need to pass "arg_index" around.
.macro LOOP_OVER_SHORTY_LOADING_XMMS xmm_reg, inst, shorty, arg_index, finished
1: // LOOP
    movb (REG_VAR(shorty)), %al             // bl := *shorty
    addq MACRO_LITERAL(1), REG_VAR(shorty)  // shorty++
    cmpb MACRO_LITERAL(0), %al              // if (al == '\0') goto finished
    je VAR(finished)
    cmpb MACRO_LITERAL(68), %al             // if (al == 'D') goto FOUND_DOUBLE
    je 2f
    cmpb MACRO_LITERAL(70), %al             // if (al == 'F') goto FOUND_FLOAT
    je 3f
    shrq MACRO_LITERAL(4), REG_VAR(inst)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    //  Handle extra argument in arg array taken by a long.
    cmpb MACRO_LITERAL(74), %al   // if (al != 'J') goto LOOP
    jne 1b
    shrq MACRO_LITERAL(4), REG_VAR(inst)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    subq MACRO_LITERAL(8), %rsp
    movq REG_VAR(inst), %rax
    andq MACRO_LITERAL(0xf), %rax
    GET_VREG %eax, %rax
    movl %eax, (%rsp)
    shrq MACRO_LITERAL(4), REG_VAR(inst)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    cmpq MACRO_LITERAL(4), REG_VAR(arg_index)
    je 5f
    movq REG_VAR(inst), %rax
    andq MACRO_LITERAL(0xf), %rax
    shrq MACRO_LITERAL(4), REG_VAR(inst)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 6f
5:
    movzbl 1(rPC), %eax
    andq MACRO_LITERAL(0xf), %rax
6:
    GET_VREG %eax, %rax
    movl %eax, 4(%rsp)
    movsd (%rsp), REG_VAR(xmm_reg)
    addq MACRO_LITERAL(8), %rsp
    jmp 4f
3:  // FOUND_FLOAT
    cmpq MACRO_LITERAL(4), REG_VAR(arg_index)
    je 7f
    movq REG_VAR(inst), %rax
    andq MACRO_LITERAL(0xf), %rax
    shrq MACRO_LITERAL(4), REG_VAR(inst)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 8f
7:
    movzbl 1(rPC), %eax
    andq MACRO_LITERAL(0xf), %rax
8:
    GET_VREG_XMMs REG_VAR(xmm_reg), %rax
4:
.endm

// Puts the next int/long/object argument in the expected register,
// fetching values based on a non-range invoke.
// Uses rax as temporary.
.macro LOOP_OVER_SHORTY_LOADING_GPRS gpr_reg64, gpr_reg32, inst, shorty, arg_index, finished
1: // LOOP
    movb (REG_VAR(shorty)), %al   // bl := *shorty
    addq MACRO_LITERAL(1), REG_VAR(shorty)  // shorty++
    cmpb MACRO_LITERAL(0), %al    // if (al == '\0') goto finished
    je  VAR(finished)
    cmpb MACRO_LITERAL(74), %al   // if (al == 'J') goto FOUND_LONG
    je 2f
    cmpb MACRO_LITERAL(70), %al   // if (al == 'F') goto SKIP_FLOAT
    je 3f
    cmpb MACRO_LITERAL(68), %al   // if (al == 'D') goto SKIP_DOUBLE
    je 4f
    cmpq MACRO_LITERAL(4), REG_VAR(arg_index)
    je 7f
    movq REG_VAR(inst), %rax
    andq MACRO_LITERAL(0xf), %rax
    shrq MACRO_LITERAL(4), REG_VAR(inst)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 8f
7:
    movzbl 1(rPC), %eax
    andq MACRO_LITERAL(0xf), %rax
8:
    GET_VREG REG_VAR(gpr_reg32), %rax
    jmp 5f
2:  // FOUND_LONG
    subq MACRO_LITERAL(8), %rsp
    movq REG_VAR(inst), %rax
    andq MACRO_LITERAL(0xf), %rax
    GET_VREG %eax, %rax
    movl %eax, (%rsp)
    shrq MACRO_LITERAL(4), REG_VAR(inst)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    cmpq MACRO_LITERAL(4), REG_VAR(arg_index)
    je 9f
    movq REG_VAR(inst), %rax
    andq MACRO_LITERAL(0xf), %rax
    shrq MACRO_LITERAL(4), REG_VAR(inst)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 10f
9:
    movzbl 1(rPC), %eax
    andq MACRO_LITERAL(0xf), %rax
10:
    GET_VREG %eax, %rax
    movl %eax, 4(%rsp)
    movq (%rsp), REG_VAR(gpr_reg64)
    addq MACRO_LITERAL(8), %rsp
    jmp 5f
3:  // SKIP_FLOAT
    shrq MACRO_LITERAL(4), REG_VAR(inst)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 1b
4:  // SKIP_DOUBLE
    shrq MACRO_LITERAL(4), REG_VAR(inst)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    cmpq MACRO_LITERAL(4), REG_VAR(arg_index)
    je 1b
    shrq MACRO_LITERAL(4), REG_VAR(inst)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 1b
5:
.endm

// Puts the next floating point argument into the expected register,
// fetching values based on a range invoke.
// Uses rax as temporary.
.macro LOOP_RANGE_OVER_SHORTY_LOADING_XMMS xmm_reg, shorty, arg_index, stack_index, finished
1: // LOOP
    movb (REG_VAR(shorty)), %al             // bl := *shorty
    addq MACRO_LITERAL(1), REG_VAR(shorty)  // shorty++
    cmpb MACRO_LITERAL(0), %al              // if (al == '\0') goto finished
    je VAR(finished)
    cmpb MACRO_LITERAL(68), %al             // if (al == 'D') goto FOUND_DOUBLE
    je 2f
    cmpb MACRO_LITERAL(70), %al             // if (al == 'F') goto FOUND_FLOAT
    je 3f
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    addq MACRO_LITERAL(1), REG_VAR(stack_index)
    //  Handle extra argument in arg array taken by a long.
    cmpb MACRO_LITERAL(74), %al   // if (al != 'J') goto LOOP
    jne 1b
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    addq MACRO_LITERAL(1), REG_VAR(stack_index)
    jmp 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    GET_VREG_XMMd REG_VAR(xmm_reg), REG_VAR(arg_index)
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    addq MACRO_LITERAL(2), REG_VAR(stack_index)
    jmp 4f
3:  // FOUND_FLOAT
    GET_VREG_XMMs REG_VAR(xmm_reg), REG_VAR(arg_index)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    addq MACRO_LITERAL(1), REG_VAR(stack_index)
4:
.endm

// Puts the next floating point argument into the expected stack slot,
// fetching values based on a range invoke.
// Uses rax as temporary.
//
// TODO: We could just copy all the vregs to the stack slots in a simple loop
// (or REP MOVSD) without looking at the shorty at all. (We could also drop
// the "stack_index" from the macros for loading registers.) We could also do
// that conditionally if argument word count > 6; otherwise we know that all
// args fit into registers.
.macro LOOP_RANGE_OVER_FPs shorty, arg_index, stack_index, finished
1: // LOOP
    movb (REG_VAR(shorty)), %al             // bl := *shorty
    addq MACRO_LITERAL(1), REG_VAR(shorty)  // shorty++
    cmpb MACRO_LITERAL(0), %al              // if (al == '\0') goto finished
    je VAR(finished)
    cmpb MACRO_LITERAL(68), %al             // if (al == 'D') goto FOUND_DOUBLE
    je 2f
    cmpb MACRO_LITERAL(70), %al             // if (al == 'F') goto FOUND_FLOAT
    je 3f
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    addq MACRO_LITERAL(1), REG_VAR(stack_index)
    //  Handle extra argument in arg array taken by a long.
    cmpb MACRO_LITERAL(74), %al   // if (al != 'J') goto LOOP
    jne 1b
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    addq MACRO_LITERAL(1), REG_VAR(stack_index)
    jmp 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    movq (rFP, REG_VAR(arg_index), 4), %rax
    movq %rax, 8(%rsp, REG_VAR(stack_index), 4)
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    addq MACRO_LITERAL(2), REG_VAR(stack_index)
    jmp 1b
3:  // FOUND_FLOAT
    movl (rFP, REG_VAR(arg_index), 4), %eax
    movl %eax, 8(%rsp, REG_VAR(stack_index), 4)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    addq MACRO_LITERAL(1), REG_VAR(stack_index)
    jmp 1b
.endm

// Puts the next int/long/object argument in the expected register,
// fetching values based on a range invoke.
// Uses rax as temporary.
.macro LOOP_RANGE_OVER_SHORTY_LOADING_GPRS gpr_reg64, gpr_reg32, shorty, arg_index, stack_index, finished
1: // LOOP
    movb (REG_VAR(shorty)), %al             // bl := *shorty
    addq MACRO_LITERAL(1), REG_VAR(shorty)  // shorty++
    cmpb MACRO_LITERAL(0), %al    // if (al == '\0') goto finished
    je  VAR(finished)
    cmpb MACRO_LITERAL(74), %al   // if (al == 'J') goto FOUND_LONG
    je 2f
    cmpb MACRO_LITERAL(70), %al   // if (al == 'F') goto SKIP_FLOAT
    je 3f
    cmpb MACRO_LITERAL(68), %al   // if (al == 'D') goto SKIP_DOUBLE
    je 4f
    movl       (rFP, REG_VAR(arg_index), 4), REG_VAR(gpr_reg32)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    addq MACRO_LITERAL(1), REG_VAR(stack_index)
    jmp 5f
2:  // FOUND_LONG
    movq (rFP, REG_VAR(arg_index), 4), REG_VAR(gpr_reg64)
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    addq MACRO_LITERAL(2), REG_VAR(stack_index)
    jmp 5f
3:  // SKIP_FLOAT
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    addq MACRO_LITERAL(1), REG_VAR(stack_index)
    jmp 1b
4:  // SKIP_DOUBLE
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    addq MACRO_LITERAL(2), REG_VAR(stack_index)
    jmp 1b
5:
.endm

// Puts the next int/long/object argument in the expected stack slot,
// fetching values based on a range invoke.
// Uses rax as temporary.
.macro LOOP_RANGE_OVER_INTs shorty, arg_index, stack_index, finished
1: // LOOP
    movb (REG_VAR(shorty)), %al             // al := *shorty
    addq MACRO_LITERAL(1), REG_VAR(shorty)  // shorty++
    cmpb MACRO_LITERAL(0), %al    // if (al == '\0') goto finished
    je  VAR(finished)
    cmpb MACRO_LITERAL(74), %al   // if (al == 'J') goto FOUND_LONG
    je 2f
    cmpb MACRO_LITERAL(70), %al   // if (al == 'F') goto SKIP_FLOAT
    je 3f
    cmpb MACRO_LITERAL(68), %al   // if (al == 'D') goto SKIP_DOUBLE
    je 4f
    movl (rFP, REG_VAR(arg_index), 4), %eax
    movl %eax, 8(%rsp, REG_VAR(stack_index), 4)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    addq MACRO_LITERAL(1), REG_VAR(stack_index)
    jmp 1b
2:  // FOUND_LONG
    movq (rFP, REG_VAR(arg_index), 4), %rax
    movq %rax, 8(%rsp, REG_VAR(stack_index), 4)
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    addq MACRO_LITERAL(2), REG_VAR(stack_index)
    jmp 1b
3:  // SKIP_FLOAT
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    addq MACRO_LITERAL(1), REG_VAR(stack_index)
    jmp 1b
4:  // SKIP_DOUBLE
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    addq MACRO_LITERAL(2), REG_VAR(stack_index)
    jmp 1b
.endm

// Puts the next floating point parameter passed in physical register
// in the expected dex register array entry.
// Uses rax as temporary.
.macro LOOP_OVER_SHORTY_STORING_XMMS xmm_reg, shorty, arg_index, fp, finished
1: // LOOP
    movb (REG_VAR(shorty)), %al             // al := *shorty
    addq MACRO_LITERAL(1), REG_VAR(shorty)  // shorty++
    cmpb MACRO_LITERAL(0), %al              // if (al == '\0') goto finished
    je VAR(finished)
    cmpb MACRO_LITERAL(68), %al             // if (al == 'D') goto FOUND_DOUBLE
    je 2f
    cmpb MACRO_LITERAL(70), %al             // if (al == 'F') goto FOUND_FLOAT
    je 3f
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    //  Handle extra argument in arg array taken by a long.
    cmpb MACRO_LITERAL(74), %al   // if (al != 'J') goto LOOP
    jne 1b
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    movsd REG_VAR(xmm_reg),(REG_VAR(fp), REG_VAR(arg_index), 4)
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    jmp 4f
3:  // FOUND_FLOAT
    movss REG_VAR(xmm_reg), (REG_VAR(fp), REG_VAR(arg_index), 4)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
4:
.endm

// Puts the next int/long/object parameter passed in physical register
// in the expected dex register array entry, and in case of object in the
// expected reference array entry.
// Uses rax as temporary.
.macro LOOP_OVER_SHORTY_STORING_GPRS gpr_reg64, gpr_reg32, shorty, arg_index, regs, refs, finished
1: // LOOP
    movb (REG_VAR(shorty)), %al             // bl := *shorty
    addq MACRO_LITERAL(1), REG_VAR(shorty)  // shorty++
    cmpb MACRO_LITERAL(0), %al    // if (al == '\0') goto finished
    je  VAR(finished)
    cmpb MACRO_LITERAL(74), %al   // if (al == 'J') goto FOUND_LONG
    je 2f
    cmpb MACRO_LITERAL(70), %al   // if (al == 'F') goto SKIP_FLOAT
    je 3f
    cmpb MACRO_LITERAL(68), %al   // if (al == 'D') goto SKIP_DOUBLE
    je 4f
    movl REG_VAR(gpr_reg32), (REG_VAR(regs), REG_VAR(arg_index), 4)
    cmpb MACRO_LITERAL(76), %al   // if (al != 'L') goto NOT_REFERENCE
    jne 6f
    movl REG_VAR(gpr_reg32), (REG_VAR(refs), REG_VAR(arg_index), 4)
6:  // NOT_REFERENCE
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 5f
2:  // FOUND_LONG
    movq REG_VAR(gpr_reg64), (REG_VAR(regs), REG_VAR(arg_index), 4)
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    jmp 5f
3:  // SKIP_FLOAT
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 1b
4:  // SKIP_DOUBLE
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    jmp 1b
5:
.endm

// Puts the next floating point parameter passed in stack
// in the expected dex register array entry.
// Uses rax as temporary.
//
// TODO: Or we could just spill regs to the reserved slots in the caller's
// frame and copy all regs in a simple loop. This time, however, we would
// need to look at the shorty anyway to look for the references.
// (The trade-off is different for passing arguments and receiving them.)
.macro LOOP_OVER_FPs shorty, arg_index, regs, stack_ptr, finished
1: // LOOP
    movb (REG_VAR(shorty)), %al             // bl := *shorty
    addq MACRO_LITERAL(1), REG_VAR(shorty)  // shorty++
    cmpb MACRO_LITERAL(0), %al              // if (al == '\0') goto finished
    je VAR(finished)
    cmpb MACRO_LITERAL(68), %al             // if (al == 'D') goto FOUND_DOUBLE
    je 2f
    cmpb MACRO_LITERAL(70), %al             // if (al == 'F') goto FOUND_FLOAT
    je 3f
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    //  Handle extra argument in arg array taken by a long.
    cmpb MACRO_LITERAL(74), %al   // if (al != 'J') goto LOOP
    jne 1b
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    movq OFFSET_TO_FIRST_ARGUMENT_IN_STACK(REG_VAR(stack_ptr), REG_VAR(arg_index), 4), %rax
    movq %rax, (REG_VAR(regs), REG_VAR(arg_index), 4)
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    jmp 1b
3:  // FOUND_FLOAT
    movl OFFSET_TO_FIRST_ARGUMENT_IN_STACK(REG_VAR(stack_ptr), REG_VAR(arg_index), 4), %eax
    movl %eax, (REG_VAR(regs), REG_VAR(arg_index), 4)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 1b
.endm

// Puts the next int/long/object parameter passed in stack
// in the expected dex register array entry, and in case of object in the
// expected reference array entry.
// Uses rax as temporary.
.macro LOOP_OVER_INTs shorty, arg_index, regs, refs, stack_ptr, finished
1: // LOOP
    movb (REG_VAR(shorty)), %al             // bl := *shorty
    addq MACRO_LITERAL(1), REG_VAR(shorty)  // shorty++
    cmpb MACRO_LITERAL(0), %al    // if (al == '\0') goto finished
    je  VAR(finished)
    cmpb MACRO_LITERAL(74), %al   // if (al == 'J') goto FOUND_LONG
    je 2f
    cmpb MACRO_LITERAL(76), %al   // if (al == 'L') goto FOUND_REFERENCE
    je 6f
    cmpb MACRO_LITERAL(70), %al   // if (al == 'F') goto SKIP_FLOAT
    je 3f
    cmpb MACRO_LITERAL(68), %al   // if (al == 'D') goto SKIP_DOUBLE
    je 4f
    movl OFFSET_TO_FIRST_ARGUMENT_IN_STACK(REG_VAR(stack_ptr), REG_VAR(arg_index), 4), %eax
    movl %eax, (REG_VAR(regs), REG_VAR(arg_index), 4)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 1b
6:  // FOUND_REFERENCE
    movl OFFSET_TO_FIRST_ARGUMENT_IN_STACK(REG_VAR(stack_ptr), REG_VAR(arg_index), 4), %eax
    movl %eax, (REG_VAR(regs), REG_VAR(arg_index), 4)
    movl %eax, (REG_VAR(refs), REG_VAR(arg_index), 4)
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 1b
2:  // FOUND_LONG
    movq OFFSET_TO_FIRST_ARGUMENT_IN_STACK(REG_VAR(stack_ptr), REG_VAR(arg_index), 4), %rax
    movq %rax, (REG_VAR(regs), REG_VAR(arg_index), 4)
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    jmp 1b
3:  // SKIP_FLOAT
    addq MACRO_LITERAL(1), REG_VAR(arg_index)
    jmp 1b
4:  // SKIP_DOUBLE
    addq MACRO_LITERAL(2), REG_VAR(arg_index)
    jmp 1b
.endm

// Increase method hotness and do suspend check before starting executing the method.
.macro START_EXECUTING_INSTRUCTIONS
   movq (%rsp), %rdi
#if (NTERP_HOTNESS_MASK != 0xffff)
#error "Nterp x86_64 expects Nterp hotness mask to be 0xffff"
#endif
   addw $$1, ART_METHOD_HOTNESS_COUNT_OFFSET(%rdi)
   jo 2f
   testl $$(THREAD_SUSPEND_OR_CHECKPOINT_REQUEST), rSELF:THREAD_FLAGS_OFFSET
   jz 1f
   EXPORT_PC
   call SYMBOL(art_quick_test_suspend)
1:
   FETCH_INST
   GOTO_NEXT
2:
   movq $$0, %rsi
   movq rFP, %rdx
   call nterp_hot_method
   jmp 1b
.endm

.macro SPILL_ALL_CALLEE_SAVES
    PUSH r15
    PUSH r14
    PUSH r13
    PUSH r12
    PUSH rbp
    PUSH rbx
    SETUP_FP_CALLEE_SAVE_FRAME
.endm

.macro RESTORE_ALL_CALLEE_SAVES
    RESTORE_FP_CALLEE_SAVE_FRAME
    POP rbx
    POP rbp
    POP r12
    POP r13
    POP r14
    POP r15
.endm

// Helper to setup the stack after doing a nterp to nterp call. This will setup:
// - rNEW_FP: the new pointer to dex registers
// - rNEW_REFS: the new pointer to references
// - rPC: the new PC pointer to execute
// - edi: number of arguments
// - ecx: first dex register
.macro SETUP_STACK_FOR_INVOKE
   // We do the same stack overflow check as the compiler. See CanMethodUseNterp
   // in how we limit the maximum nterp frame size.
   testq %rax, -STACK_OVERFLOW_RESERVED_BYTES(%rsp)

   // Spill all callee saves to have a consistent stack frame whether we
   // are called by compiled code or nterp.
   SPILL_ALL_CALLEE_SAVES

   // Setup the frame.
   SETUP_STACK_FRAME %rax, rNEW_REFS, rNEW_FP, CFI_NEW_REFS
   // Make r11 point to the top of the dex register array.
   leaq (rNEW_FP, %rbx, 4), %r11

   // Fetch instruction information before replacing rPC.
   movzbl 1(rPC), %edi
   movzwl 4(rPC), %ecx

   // Set the dex pc pointer.
   leaq CODE_ITEM_INSNS_OFFSET(%rax), rPC
   CFI_DEFINE_DEX_PC_WITH_OFFSET(CFI_TMP, CFI_DEX, 0)
.endm

// Setup arguments based on a non-range nterp to nterp call, and start executing
// the method. We expect:
// - rNEW_FP: the new pointer to dex registers
// - rNEW_REFS: the new pointer to references
// - rPC: the new PC pointer to execute
// - edi: number of arguments
// - ecx: first dex register
// - r11: top of dex register array
// - esi: receiver if non-static.
.macro SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=0
   // Now all temporary registers (except r11 containing top of registers array)
   // are available, copy the parameters.
   // /* op vA, vB, {vC...vG} */
   movl %edi, %eax
   shrl $$4, %eax # Number of arguments
   jz 6f  # shl sets the Z flag
   movq MACRO_LITERAL(-1), %r10
   cmpl MACRO_LITERAL(2), %eax
   jl 1f
   je 2f
   cmpl MACRO_LITERAL(4), %eax
   jl 3f
   je 4f

  // We use a decrementing r10 to store references relative
  // to rNEW_FP and dex registers relative to r11.
  //
  // TODO: We could set up r10 as the number of registers (this can be an additional output from
  // SETUP_STACK_FOR_INVOKE) and then just decrement it by one before copying each arg to
  // (rNEW_FP, r10, 4) and (rNEW_REFS, r10, 4).
  // Maybe even introduce macros NEW_VREG_ADDRESS/NEW_VREG_REF_ADDRESS.
5:
   andq        MACRO_LITERAL(15), %rdi
   GET_VREG_OBJECT %edx, %rdi
   movl        %edx, (rNEW_FP, %r10, 4)
   GET_VREG    %edx, %rdi
   movl        %edx, (%r11, %r10, 4)
   subq        MACRO_LITERAL(1), %r10
4:
   movl        %ecx, %eax
   shrl        MACRO_LITERAL(12), %eax
   GET_VREG_OBJECT %edx, %rax
   movl        %edx, (rNEW_FP, %r10, 4)
   GET_VREG    %edx, %rax
   movl        %edx, (%r11, %r10, 4)
   subq        MACRO_LITERAL(1), %r10
3:
   movl        %ecx, %eax
   shrl        MACRO_LITERAL(8), %eax
   andl        MACRO_LITERAL(0xf), %eax
   GET_VREG_OBJECT %edx, %rax
   movl        %edx, (rNEW_FP, %r10, 4)
   GET_VREG    %edx, %rax
   movl        %edx, (%r11, %r10, 4)
   subq        MACRO_LITERAL(1), %r10
2:
   movl        %ecx, %eax
   shrl        MACRO_LITERAL(4), %eax
   andl        MACRO_LITERAL(0xf), %eax
   GET_VREG_OBJECT %edx, %rax
   movl        %edx, (rNEW_FP, %r10, 4)
   GET_VREG    %edx, %rax
   movl        %edx, (%r11, %r10, 4)
   subq        MACRO_LITERAL(1), %r10
1:
   .if \is_string_init
   // Ignore the first argument
   .elseif \is_static
   movl        %ecx, %eax
   andq        MACRO_LITERAL(0x000f), %rax
   GET_VREG_OBJECT %edx, %rax
   movl        %edx, (rNEW_FP, %r10, 4)
   GET_VREG    %edx, %rax
   movl        %edx, (%r11, %r10, 4)
   .else
   movl        %esi, (rNEW_FP, %r10, 4)
   movl        %esi, (%r11, %r10, 4)
   .endif

6:
   // Start executing the method.
   movq rNEW_FP, rFP
   movq rNEW_REFS, rREFS
   CFI_DEFINE_CFA_DEREF(CFI_REFS, -8, (6 + 4 + 1) * 8)
   START_EXECUTING_INSTRUCTIONS
.endm

// Setup arguments based on a range nterp to nterp call, and start executing
// the method.
.macro SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=0
   // edi is number of arguments
   // ecx is first register
   movq MACRO_LITERAL(-4), %r10
   .if \is_string_init
   // Ignore the first argument
   subl $$1, %edi
   addl $$1, %ecx
   .elseif !\is_static
   subl $$1, %edi
   addl $$1, %ecx
   .endif

   testl %edi, %edi
   je 2f
   leaq  (rREFS, %rcx, 4), %rax  # pointer to first argument in reference array
   leaq  (%rax, %rdi, 4), %rax   # pointer to last argument in reference array
   leaq  (rFP, %rcx, 4), %rcx    # pointer to first argument in register array
   leaq  (%rcx, %rdi, 4), %rdi   # pointer to last argument in register array
   // TODO: Same comment for copying arguments as in SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE.
1:
   movl  -4(%rax), %edx
   movl  %edx, (rNEW_FP, %r10, 1)
   movl  -4(%rdi), %edx
   movl  %edx, (%r11, %r10, 1)
   subq  MACRO_LITERAL(4), %r10
   subq  MACRO_LITERAL(4), %rax
   subq  MACRO_LITERAL(4), %rdi
   cmpq  %rcx, %rdi
   jne 1b

2:
   .if \is_string_init
   // Ignore first argument
   .elseif !\is_static
   movl        %esi, (rNEW_FP, %r10, 1)
   movl        %esi, (%r11, %r10, 1)
   .endif
   movq rNEW_FP, rFP
   movq rNEW_REFS, rREFS
   CFI_DEFINE_CFA_DEREF(CFI_REFS, -8, (6 + 4 + 1) * 8)
   START_EXECUTING_INSTRUCTIONS
.endm

.macro GET_SHORTY dest, is_interface, is_polymorphic, is_custom
   push %rdi
   push %rsi
   .if \is_polymorphic
   movq 16(%rsp), %rdi
   movq rPC, %rsi
   call SYMBOL(NterpGetShortyFromInvokePolymorphic)
   .elseif \is_custom
   movq 16(%rsp), %rdi
   movq rPC, %rsi
   call SYMBOL(NterpGetShortyFromInvokeCustom)
   .elseif \is_interface
   movq 16(%rsp), %rdi
   movzwl 2(rPC), %esi
   call SYMBOL(NterpGetShortyFromMethodId)
   .else
   call SYMBOL(NterpGetShorty)
   .endif
   pop %rsi
   pop %rdi
   movq %rax, \dest
.endm

.macro DO_ENTRY_POINT_CHECK call_compiled_code
   // On entry, the method is %rdi, the instance is %rsi
   leaq ExecuteNterpImpl(%rip), %rax
   cmpq %rax, ART_METHOD_QUICK_CODE_OFFSET_64(%rdi)
   jne  VAR(call_compiled_code)

   // TODO: Get code item in a better way and remove below
   push %rdi
   push %rsi
   call SYMBOL(NterpGetCodeItem)
   pop %rsi
   pop %rdi
   // TODO: Get code item in a better way and remove above
.endm

// Uses r9 and r10 as temporary
.macro UPDATE_REGISTERS_FOR_STRING_INIT old_value, new_value
   movq rREFS, %r9
   movq rFP, %r10
1:
   cmpl (%r9), \old_value
   jne 2f
   movl \new_value, (%r9)
   movl \new_value, (%r10)
2:
   addq $$4, %r9
   addq $$4, %r10
   cmpq %r9, rFP
   jne 1b
.endm

.macro COMMON_INVOKE_NON_RANGE is_static=0, is_interface=0, suffix="", is_string_init=0, is_polymorphic=0, is_custom=0
   .if \is_polymorphic
   // We always go to compiled code for polymorphic calls.
   .elseif \is_custom
   // We always go to compiled code for custom calls.
   .else
     DO_ENTRY_POINT_CHECK .Lcall_compiled_code_\suffix
     .if \is_string_init
     call nterp_to_nterp_string_init_non_range
     .elseif \is_static
     call nterp_to_nterp_static_non_range
     .else
     call nterp_to_nterp_instance_non_range
     .endif
     jmp .Ldone_return_\suffix
   .endif

.Lcall_compiled_code_\suffix:
   GET_SHORTY rINSTq, \is_interface, \is_polymorphic, \is_custom
   // From this point:
   // - rISNTq contains shorty (in callee-save to switch over return value after call).
   // - rdi contains method
   // - rsi contains 'this' pointer for instance method.
   leaq 1(rINSTq), %r9  // shorty + 1  ; ie skip return arg character
   movzwl 4(rPC), %r11d // arguments
   .if \is_string_init
   shrq MACRO_LITERAL(4), %r11
   movq $$1, %r10       // ignore first argument
   .elseif \is_static
   movq $$0, %r10       // arg_index
   .else
   shrq MACRO_LITERAL(4), %r11
   movq $$1, %r10       // arg_index
   .endif
   LOOP_OVER_SHORTY_LOADING_XMMS xmm0, r11, r9, r10, .Lxmm_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_XMMS xmm1, r11, r9, r10, .Lxmm_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_XMMS xmm2, r11, r9, r10, .Lxmm_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_XMMS xmm3, r11, r9, r10, .Lxmm_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_XMMS xmm4, r11, r9, r10, .Lxmm_setup_finished_\suffix
.Lxmm_setup_finished_\suffix:
   leaq 1(rINSTq), %r9  // shorty + 1  ; ie skip return arg character
   movzwl 4(rPC), %r11d // arguments
   .if \is_string_init
   movq $$1, %r10       // ignore first argument
   shrq MACRO_LITERAL(4), %r11
   LOOP_OVER_SHORTY_LOADING_GPRS rsi, esi, r11, r9, r10, .Lgpr_setup_finished_\suffix
   .elseif \is_static
   movq $$0, %r10       // arg_index
   LOOP_OVER_SHORTY_LOADING_GPRS rsi, esi, r11, r9, r10, .Lgpr_setup_finished_\suffix
   .else
   shrq MACRO_LITERAL(4), %r11
   movq $$1, %r10       // arg_index
   .endif
   LOOP_OVER_SHORTY_LOADING_GPRS rdx, edx, r11, r9, r10, .Lgpr_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_GPRS rcx, ecx, r11, r9, r10, .Lgpr_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_GPRS r8, r8d, r11, r9, r10, .Lgpr_setup_finished_\suffix
   LOOP_OVER_SHORTY_LOADING_GPRS r9, r9d, r11, r9, r10, .Lgpr_setup_finished_\suffix
.Lgpr_setup_finished_\suffix:
   .if \is_polymorphic
   call SYMBOL(art_quick_invoke_polymorphic)
   .elseif \is_custom
   call SYMBOL(art_quick_invoke_custom)
   .else
      .if \is_interface
      movzwl 2(rPC), %eax
      .endif
      call *ART_METHOD_QUICK_CODE_OFFSET_64(%rdi) // Call the method.
   .endif
   cmpb LITERAL(68), (rINSTq)       // Test if result type char == 'D'.
   je .Lreturn_double_\suffix
   cmpb LITERAL(70), (rINSTq)       // Test if result type char == 'F'.
   jne .Ldone_return_\suffix
.Lreturn_float_\suffix:
   movd %xmm0, %eax
   jmp .Ldone_return_\suffix
.Lreturn_double_\suffix:
   movq %xmm0, %rax
.Ldone_return_\suffix:
   /* resume execution of caller */
   .if \is_string_init
   movzwl 4(rPC), %r11d // arguments
   andq $$0xf, %r11
   GET_VREG %esi, %r11
   UPDATE_REGISTERS_FOR_STRING_INIT %esi, %eax
   .endif

   .if \is_polymorphic
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 4
   .else
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 3
   .endif
.endm

.macro COMMON_INVOKE_RANGE is_static=0, is_interface=0, suffix="", is_string_init=0, is_polymorphic=0, is_custom=0
   .if \is_polymorphic
   // We always go to compiled code for polymorphic calls.
   .elseif \is_custom
   // We always go to compiled code for custom calls.
   .else
     DO_ENTRY_POINT_CHECK .Lcall_compiled_code_range_\suffix
     .if \is_string_init
     call nterp_to_nterp_string_init_range
     .elseif \is_static
     call nterp_to_nterp_static_range
     .else
     call nterp_to_nterp_instance_range
     .endif
     jmp .Ldone_return_range_\suffix
   .endif

.Lcall_compiled_code_range_\suffix:
   GET_SHORTY rINSTq, \is_interface, \is_polymorphic, \is_custom
   // From this point:
   // - rINSTq contains shorty (in callee-save to switch over return value after call).
   // - rdi contains method
   // - rsi contains 'this' pointer for instance method.
   leaq 1(rINSTq), %r9  // shorty + 1  ; ie skip return arg character
   movzwl 4(rPC), %r10d // arg start index
   .if \is_string_init
   addq $$1, %r10       // arg start index
   movq $$1, %rbp       // index in stack
   .elseif \is_static
   movq $$0, %rbp       // index in stack
   .else
   addq $$1, %r10       // arg start index
   movq $$1, %rbp       // index in stack
   .endif
   LOOP_RANGE_OVER_SHORTY_LOADING_XMMS xmm0, r9, r10, rbp, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_XMMS xmm1, r9, r10, rbp, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_XMMS xmm2, r9, r10, rbp, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_XMMS xmm3, r9, r10, rbp, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_XMMS xmm4, r9, r10, rbp, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_XMMS xmm5, r9, r10, rbp, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_XMMS xmm6, r9, r10, rbp, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_XMMS xmm7, r9, r10, rbp, .Lxmm_setup_finished_range_\suffix
   LOOP_RANGE_OVER_FPs r9, r10, rbp, .Lxmm_setup_finished_range_\suffix
.Lxmm_setup_finished_range_\suffix:
   leaq 1(%rbx), %r11  // shorty + 1  ; ie skip return arg character
   movzwl 4(rPC), %r10d // arg start index
   .if \is_string_init
   addq $$1, %r10       // arg start index
   movq $$1, %rbp       // index in stack
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS rsi, esi, r11, r10, rbp, .Lgpr_setup_finished_\suffix
   .elseif \is_static
   movq $$0, %rbp // index in stack
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS rsi, esi, r11, r10, rbp, .Lgpr_setup_finished_\suffix
   .else
   addq $$1, %r10       // arg start index
   movq $$1, %rbp // index in stack
   .endif
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS rdx, edx, r11, r10, rbp, .Lgpr_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS rcx, ecx, r11, r10, rbp, .Lgpr_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS r8, r8d, r11, r10, rbp, .Lgpr_setup_finished_range_\suffix
   LOOP_RANGE_OVER_SHORTY_LOADING_GPRS r9, r9d, r11, r10, rbp, .Lgpr_setup_finished_range_\suffix
   LOOP_RANGE_OVER_INTs r11, r10, rbp, .Lgpr_setup_finished_range_\suffix

.Lgpr_setup_finished_range_\suffix:
   .if \is_polymorphic
   call SYMBOL(art_quick_invoke_polymorphic)
   .elseif \is_custom
   call SYMBOL(art_quick_invoke_custom)
   .else
     .if \is_interface
     movzwl 2(rPC), %eax
     .endif
     call *ART_METHOD_QUICK_CODE_OFFSET_64(%rdi) // Call the method.
   .endif
   cmpb LITERAL(68), (%rbx)       // Test if result type char == 'D'.
   je .Lreturn_range_double_\suffix
   cmpb LITERAL(70), (%rbx)       // Test if result type char == 'F'.
   je .Lreturn_range_float_\suffix
   /* resume execution of caller */
.Ldone_return_range_\suffix:
   .if \is_string_init
   movzwl 4(rPC), %r11d // arguments
   GET_VREG %esi, %r11
   UPDATE_REGISTERS_FOR_STRING_INIT %esi, %eax
   .endif

   .if \is_polymorphic
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 4
   .else
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 3
   .endif
.Lreturn_range_double_\suffix:
    movq %xmm0, %rax
    jmp .Ldone_return_range_\suffix
.Lreturn_range_float_\suffix:
    movd %xmm0, %eax
    jmp .Ldone_return_range_\suffix
.endm

// Fetch some information from the thread cache.
// Uses rax, rdx, rcx as temporaries.
.macro FETCH_FROM_THREAD_CACHE dest_reg, slow_path
   movq rSELF:THREAD_SELF_OFFSET, %rax
   movq rPC, %rdx
   salq MACRO_LITERAL(THREAD_INTERPRETER_CACHE_SIZE_SHIFT), %rdx
   andq MACRO_LITERAL(THREAD_INTERPRETER_CACHE_SIZE_MASK), %rdx
   cmpq THREAD_INTERPRETER_CACHE_OFFSET(%rax, %rdx, 1), rPC
   jne \slow_path
   movq __SIZEOF_POINTER__+THREAD_INTERPRETER_CACHE_OFFSET(%rax, %rdx, 1), \dest_reg
.endm

// Helper for static field get.
.macro OP_SGET load="movl", wide="0"
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rax, 2f
1:
   movl ART_FIELD_OFFSET_OFFSET(%rax), %edx
   movl ART_FIELD_DECLARING_CLASS_OFFSET(%rax), %eax
   cmpq $$0, rSELF:THREAD_READ_BARRIER_MARK_REG00_OFFSET
   jne 3f
4:
   .if \wide
   movq (%eax,%edx,1), %rax
   SET_WIDE_VREG %rax, rINSTq              # fp[A] <- value
   .else
   \load (%eax, %edx, 1), %eax
   SET_VREG %eax, rINSTq            # fp[A] <- value
   .endif
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   EXPORT_PC
   call nterp_get_static_field
   // Clear the marker that we put for volatile fields. The x86 memory
   // model doesn't require a barrier.
   andq $$-2, %rax
   jmp 1b
3:
   call art_quick_read_barrier_mark_reg00
   jmp 4b
.endm

// Helper for static field put.
.macro OP_SPUT rINST_reg="rINST", store="movl", wide="0":
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rax, 2f
1:
   movl ART_FIELD_OFFSET_OFFSET(%rax), %edx
   movl ART_FIELD_DECLARING_CLASS_OFFSET(%rax), %eax
   cmpq $$0, rSELF:THREAD_READ_BARRIER_MARK_REG00_OFFSET
   jne 3f
4:
   .if \wide
   GET_WIDE_VREG rINSTq, rINSTq           # rINST <- v[A]
   .else
   GET_VREG rINST, rINSTq                  # rINST <- v[A]
   .endif
   \store    \rINST_reg, (%rax,%rdx,1)
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   EXPORT_PC
   call nterp_get_static_field
   testq MACRO_LITERAL(1), %rax
   je 1b
   // Clear the marker that we put for volatile fields. The x86 memory
   // model doesn't require a barrier.
   CLEAR_VOLATILE_MARKER %rax
   movl ART_FIELD_OFFSET_OFFSET(%rax), %edx
   movl ART_FIELD_DECLARING_CLASS_OFFSET(%rax), %eax
   cmpq $$0, rSELF:THREAD_READ_BARRIER_MARK_REG00_OFFSET
   jne 6f
5:
   .if \wide
   GET_WIDE_VREG rINSTq, rINSTq           # rINST <- v[A]
   .else
   GET_VREG rINST, rINSTq                  # rINST <- v[A]
   .endif
   \store    \rINST_reg, (%rax,%rdx,1)
   lock addl $$0, (%rsp)
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
3:
   call art_quick_read_barrier_mark_reg00
   jmp 4b
6:
   call art_quick_read_barrier_mark_reg00
   jmp 5b
.endm


.macro OP_IPUT_INTERNAL rINST_reg="rINST", store="movl", wide="0":
   movzbq  rINSTbl, %rcx                   # rcx <- BA
   sarl    $$4, %ecx                       # ecx <- B
   GET_VREG %ecx, %rcx                     # vB (object we're operating on)
   testl   %ecx, %ecx                      # is object null?
   je      common_errNullObject
   andb    $$0xf, rINSTbl                  # rINST <- A
   .if \wide
   GET_WIDE_VREG rINSTq, rINSTq              # rax<- fp[A]/fp[A+1]
   .else
   GET_VREG rINST, rINSTq                  # rINST <- v[A]
   .endif
   \store \rINST_reg, (%rcx,%rax,1)
.endm

// Helper for instance field put.
.macro OP_IPUT rINST_reg="rINST", store="movl", wide="0":
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rax, 2f
1:
   OP_IPUT_INTERNAL \rINST_reg, \store, \wide
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   EXPORT_PC
   call nterp_get_instance_field_offset
   testl %eax, %eax
   jns 1b
   negl %eax
   OP_IPUT_INTERNAL \rINST_reg, \store, \wide
   lock addl $$0, (%rsp)
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
.endm

// Helper for instance field get.
.macro OP_IGET load="movl", wide="0"
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rax, 2f
1:
   movl    rINST, %ecx                     # rcx <- BA
   sarl    $$4, %ecx                       # ecx <- B
   GET_VREG %ecx, %rcx                     # vB (object we're operating on)
   testl   %ecx, %ecx                      # is object null?
   je      common_errNullObject
   andb    $$0xf,rINSTbl                   # rINST <- A
   .if \wide
   movq (%rcx,%rax,1), %rax
   SET_WIDE_VREG %rax, rINSTq              # fp[A] <- value
   .else
   \load (%rcx,%rax,1), %eax
   SET_VREG %eax, rINSTq                   # fp[A] <- value
   .endif
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   EXPORT_PC
   call nterp_get_instance_field_offset
   testl %eax, %eax
   jns 1b
   negl %eax
   jmp 1b
.endm

%def entry():
/*
 * ArtMethod entry point.
 *
 * On entry:
 *  rdi   ArtMethod* callee
 *  rest  method parameters
 */

OAT_ENTRY ExecuteNterpImpl, EndExecuteNterpImpl
    .cfi_startproc
    .cfi_def_cfa rsp, 8
    testq %rax, -STACK_OVERFLOW_RESERVED_BYTES(%rsp)
    /* Spill callee save regs */
    SPILL_ALL_CALLEE_SAVES

    // TODO: Get shorty in a better way and remove below
    PUSH rdi
    PUSH rsi
    PUSH rdx
    PUSH rcx
    PUSH r8
    PUSH r9

    // Save xmm registers + alignment.
    subq MACRO_LITERAL(8 * 8 + 8), %rsp
    CFI_ADJUST_CFA_OFFSET(8 * 8 + 8)
    movq %xmm0, 0(%rsp)
    movq %xmm1, 8(%rsp)
    movq %xmm2, 16(%rsp)
    movq %xmm3, 24(%rsp)
    movq %xmm4, 32(%rsp)
    movq %xmm5, 40(%rsp)
    movq %xmm6, 48(%rsp)
    movq %xmm7, 56(%rsp)

    // Save method in callee-save rbx.
    movq %rdi, %rbx
    call SYMBOL(NterpGetShorty)
    // Save shorty in callee-save rbp.
    movq %rax, %rbp
    movq %rbx, %rdi
    call SYMBOL(NterpGetCodeItem)
    movq %rax, rPC

    // Restore xmm registers + alignment.
    movq 0(%rsp), %xmm0
    movq 8(%rsp), %xmm1
    movq 16(%rsp), %xmm2
    movq 24(%rsp), %xmm3
    movq 32(%rsp), %xmm4
    movq 40(%rsp), %xmm5
    movq 48(%rsp), %xmm6
    movq 56(%rsp), %xmm7
    addq MACRO_LITERAL(8 * 8 + 8), %rsp
    CFI_ADJUST_CFA_OFFSET(-8 * 8 - 8)

    POP r9
    POP r8
    POP rcx
    POP rdx
    POP rsi
    POP rdi
    // TODO: Get shorty in a better way and remove above

    // Setup the stack for executing the method.
    SETUP_STACK_FRAME rPC, rREFS, rFP, CFI_REFS

    // Setup the parameters
    movzwl CODE_ITEM_INS_SIZE_OFFSET(rPC), %r14d
    testl %r14d, %r14d
    je .Lxmm_setup_finished

    subq %r14, %rbx
    salq $$2, %rbx // rbx is now the offset for inputs into the registers array.

    testl $$ART_METHOD_IS_STATIC_FLAG, ART_METHOD_ACCESS_FLAGS_OFFSET(%rdi)

    // Available: rdi, r10, r14
    // Note the leaq below don't change the flags.
    leaq 1(%rbp), %r10  // shorty + 1  ; ie skip return arg character
    leaq (rFP, %rbx, 1), %rdi
    leaq (rREFS, %rbx, 1), %rbx
    jne .Lhandle_static_method
    movl %esi, (%rdi)
    movl %esi, (%rbx)
    addq $$4, %rdi
    addq $$4, %rbx
    addq $$4, %r11
    movq $$0, %r14
    jmp .Lcontinue_setup_gprs
.Lhandle_static_method:
    movq $$0, %r14
    LOOP_OVER_SHORTY_STORING_GPRS rsi, esi, r10, r14, rdi, rbx, .Lgpr_setup_finished
.Lcontinue_setup_gprs:
    LOOP_OVER_SHORTY_STORING_GPRS rdx, edx, r10, r14, rdi, rbx, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS rcx, ecx, r10, r14, rdi, rbx, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS r8, r8d, r10, r14, rdi, rbx, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS r9, r9d, r10, r14, rdi, rbx, .Lgpr_setup_finished
    LOOP_OVER_INTs r10, r14, rdi, rbx, r11, .Lgpr_setup_finished
.Lgpr_setup_finished:
    leaq 1(%rbp), %r10  // shorty + 1  ; ie skip return arg character
    movq $$0, %r14 // reset counter
    LOOP_OVER_SHORTY_STORING_XMMS xmm0, r10, r14, rdi, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_XMMS xmm1, r10, r14, rdi, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_XMMS xmm2, r10, r14, rdi, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_XMMS xmm3, r10, r14, rdi, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_XMMS xmm4, r10, r14, rdi, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_XMMS xmm5, r10, r14, rdi, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_XMMS xmm6, r10, r14, rdi, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_XMMS xmm7, r10, r14, rdi, .Lxmm_setup_finished
    LOOP_OVER_FPs r10, r14, rdi, r11, .Lxmm_setup_finished
.Lxmm_setup_finished:
    // Set the dex pc pointer.
    addq $$CODE_ITEM_INSNS_OFFSET, rPC
    CFI_DEFINE_DEX_PC_WITH_OFFSET(CFI_TMP, CFI_DEX, 0)

    // Set rIBASE
    leaq artNterpAsmInstructionStart(%rip), rIBASE
    /* start executing the instruction at rPC */
    START_EXECUTING_INSTRUCTIONS
    /* NOTE: no fallthrough */
    // cfi info continues, and covers the whole nterp implementation.
    END ExecuteNterpImpl

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
    call art_quick_throw_div_zero

common_errArrayIndex:
    EXPORT_PC
    movl MIRROR_ARRAY_LENGTH_OFFSET(%edi), %eax
    movl %esi, %edi
    movl %eax, %esi
    call art_quick_throw_array_bounds

common_errNullObject:
    EXPORT_PC
    call art_quick_throw_null_pointer_exception

NterpCommonInvokeStatic:
    COMMON_INVOKE_NON_RANGE is_static=1, is_interface=0, suffix="invokeStatic"

NterpCommonInvokeStaticRange:
    COMMON_INVOKE_RANGE is_static=1, is_interface=0, suffix="invokeStatic"

NterpCommonInvokeInstance:
    COMMON_INVOKE_NON_RANGE is_static=0, is_interface=0, suffix="invokeInstance"

NterpCommonInvokeInstanceRange:
    COMMON_INVOKE_RANGE is_static=0, is_interface=0, suffix="invokeInstance"

NterpCommonInvokeInterface:
    COMMON_INVOKE_NON_RANGE is_static=0, is_interface=1, suffix="invokeInterface"

NterpCommonInvokeInterfaceRange:
    COMMON_INVOKE_RANGE is_static=0, is_interface=1, suffix="invokeInterface"

NterpCommonInvokePolymorphic:
    COMMON_INVOKE_NON_RANGE is_static=0, is_interface=0, is_string_init=0, is_polymorphic=1, suffix="invokePolymorphic"

NterpCommonInvokePolymorphicRange:
    COMMON_INVOKE_RANGE is_static=0, is_interface=0, is_polymorphic=1, suffix="invokePolymorphic"

NterpCommonInvokeCustom:
    COMMON_INVOKE_NON_RANGE is_static=1, is_interface=0, is_string_init=0, is_polymorphic=0, is_custom=1, suffix="invokeCustom"

NterpCommonInvokeCustomRange:
    COMMON_INVOKE_RANGE is_static=1, is_interface=0, is_polymorphic=0, is_custom=1, suffix="invokeCustom"

NterpHandleStringInit:
   COMMON_INVOKE_NON_RANGE is_static=0, is_interface=0, is_string_init=1, suffix="stringInit"

NterpHandleStringInitRange:
   COMMON_INVOKE_RANGE is_static=0, is_interface=0, is_string_init=1, suffix="stringInit"

NterpNewInstance:
   EXPORT_PC
   // Fast-path which gets the class from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rdi, 2f
   cmpq $$0, rSELF:THREAD_READ_BARRIER_MARK_REG00_OFFSET
   jne 3f
4:
   callq *rSELF:THREAD_ALLOC_OBJECT_ENTRYPOINT_OFFSET
1:
   SET_VREG_OBJECT %eax, rINSTq            # fp[A] <- value
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   call nterp_get_class_or_allocate_object
   jmp 1b
3:
   // 07 is %rdi
   call art_quick_read_barrier_mark_reg07
   jmp 4b

NterpNewArray:
   /* new-array vA, vB, class@CCCC */
   EXPORT_PC
   // Fast-path which gets the class from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rdi, 2f
   cmpq $$0, rSELF:THREAD_READ_BARRIER_MARK_REG00_OFFSET
   jne 3f
1:
   movzbl  rINSTbl,%esi
   sarl    $$4,%esi                          # esi<- B
   GET_VREG %esi %rsi                        # esi<- vB (array length)
   andb    $$0xf,rINSTbl                     # rINST<- A
   callq *rSELF:THREAD_ALLOC_ARRAY_ENTRYPOINT_OFFSET
   SET_VREG_OBJECT %eax, rINSTq            # fp[A] <- value
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   call nterp_get_class_or_allocate_object
   movq %rax, %rdi
   jmp 1b
3:
   // 07 is %rdi
   call art_quick_read_barrier_mark_reg07
   jmp 1b

NterpPutObjectInstanceField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rax, 2f
1:
   movzbq  rINSTbl, %rcx                   # rcx <- BA
   sarl    $$4, %ecx                       # ecx <- B
   GET_VREG %ecx, %rcx                     # vB (object we're operating on)
   testl   %ecx, %ecx                      # is object null?
   je      common_errNullObject
   andb    $$0xf, rINSTbl                  # rINST <- A
   GET_VREG rINST, rINSTq                  # rINST <- v[A]
   movl rINST, (%rcx,%rax,1)
   testl rINST, rINST
   je 4f
   movq rSELF:THREAD_CARD_TABLE_OFFSET, %rax
   shrq $$CARD_TABLE_CARD_SHIFT, %rcx
   movb %al, (%rax, %rcx, 1)
4:
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
   EXPORT_PC
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   EXPORT_PC
   call nterp_get_instance_field_offset
   testl %eax, %eax
   jns 1b
   negl %eax
   movzbq  rINSTbl, %rcx                   # rcx <- BA
   sarl    $$4, %ecx                       # ecx <- B
   GET_VREG %ecx, %rcx                     # vB (object we're operating on)
   testl   %ecx, %ecx                      # is object null?
   je      common_errNullObject
   andb    $$0xf, rINSTbl                  # rINST <- A
   GET_VREG rINST, rINSTq                  # rINST <- v[A]
   movl rINST, (%rcx,%rax,1)
   testl rINST, rINST
   je 5f
   movq rSELF:THREAD_CARD_TABLE_OFFSET, %rax
   shrq $$CARD_TABLE_CARD_SHIFT, %rcx
   movb %al, (%rcx, %rax, 1)
5:
   lock addl $$0, (%rsp)
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

NterpGetObjectInstanceField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rax, 2f
1:
   movl    rINST, %ecx                     # rcx <- BA
   sarl    $$4, %ecx                       # ecx <- B
   GET_VREG %ecx, %rcx                     # vB (object we're operating on)
   testl   %ecx, %ecx                      # is object null?
   je      common_errNullObject
   testb $$READ_BARRIER_TEST_VALUE, GRAY_BYTE_OFFSET(%ecx)
   movl (%rcx,%rax,1), %eax
   jnz 3f
4:
   andb    $$0xf,rINSTbl                   # rINST <- A
   SET_VREG_OBJECT %eax, rINSTq            # fp[A] <- value
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
   EXPORT_PC
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   EXPORT_PC
   call nterp_get_instance_field_offset
   testl %eax, %eax
   jns 1b
   // For volatile fields, we return a negative offset. Remove the sign
   // and no need for any barrier thanks to the memory model.
   negl %eax
   jmp 1b
3:
   // reg00 is eax
   call art_quick_read_barrier_mark_reg00
   jmp 4b

NterpPutObjectStaticField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rax, 2f
1:
   movl ART_FIELD_OFFSET_OFFSET(%rax), %edx
   movl ART_FIELD_DECLARING_CLASS_OFFSET(%rax), %eax
   cmpq $$0, rSELF:THREAD_READ_BARRIER_MARK_REG00_OFFSET
   jne 3f
5:
   GET_VREG %ecx, rINSTq
   movl %ecx, (%eax, %edx, 1)
   testl %ecx, %ecx
   je 4f
   movq rSELF:THREAD_CARD_TABLE_OFFSET, %rcx
   shrq $$CARD_TABLE_CARD_SHIFT, %rax
   movb %cl, (%rax, %rcx, 1)
4:
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   EXPORT_PC
   call nterp_get_static_field
   testq MACRO_LITERAL(1), %rax
   je 1b
   CLEAR_VOLATILE_MARKER %rax
   movl ART_FIELD_OFFSET_OFFSET(%rax), %edx
   movl ART_FIELD_DECLARING_CLASS_OFFSET(%rax), %eax
   cmpq $$0, rSELF:THREAD_READ_BARRIER_MARK_REG00_OFFSET
   jne 7f
6:
   movzbl rINSTbl, %ecx
   GET_VREG %ecx, %rcx
   movl %ecx, (%eax, %edx, 1)
   testl %ecx, %ecx
   je 8f
   movq rSELF:THREAD_CARD_TABLE_OFFSET, %rcx
   shrq $$CARD_TABLE_CARD_SHIFT, %rax
   movb %cl, (%rax, %rcx, 1)
8:
   lock addl $$0, (%rsp)
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
3:
   call art_quick_read_barrier_mark_reg00
   jmp 5b
7:
   call art_quick_read_barrier_mark_reg00
   jmp 6b

NterpGetObjectStaticField:
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rax, 2f
1:
   movl ART_FIELD_OFFSET_OFFSET(%rax), %edx
   movl ART_FIELD_DECLARING_CLASS_OFFSET(%rax), %eax
   cmpq $$0, rSELF:THREAD_READ_BARRIER_MARK_REG00_OFFSET
   jne 5f
6:
   testb $$READ_BARRIER_TEST_VALUE, GRAY_BYTE_OFFSET(%eax)
   movl (%eax, %edx, 1), %eax
   jnz 3f
4:
   SET_VREG_OBJECT %eax, rINSTq            # fp[A] <- value
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   EXPORT_PC
   call nterp_get_static_field
   andq $$-2, %rax
   jmp 1b
3:
   call art_quick_read_barrier_mark_reg00
   jmp 4b
5:
   call art_quick_read_barrier_mark_reg00
   jmp 6b

NterpGetBooleanStaticField:
  OP_SGET load="movsbl", wide=0

NterpGetByteStaticField:
  OP_SGET load="movsbl", wide=0

NterpGetCharStaticField:
  OP_SGET load="movzwl", wide=0

NterpGetShortStaticField:
  OP_SGET load="movswl", wide=0

NterpGetWideStaticField:
  OP_SGET load="movq", wide=1

NterpGetIntStaticField:
  OP_SGET load="movl", wide=0

NterpPutStaticField:
  OP_SPUT rINST_reg=rINST, store="movl", wide=0

NterpPutBooleanStaticField:
NterpPutByteStaticField:
  OP_SPUT rINST_reg=rINSTbl, store="movb", wide=0

NterpPutCharStaticField:
NterpPutShortStaticField:
  OP_SPUT rINST_reg=rINSTw, store="movw", wide=0

NterpPutWideStaticField:
  OP_SPUT rINST_reg=rINSTq, store="movq", wide=1

NterpPutInstanceField:
  OP_IPUT rINST_reg=rINST, store="movl", wide=0

NterpPutBooleanInstanceField:
NterpPutByteInstanceField:
  OP_IPUT rINST_reg=rINSTbl, store="movb", wide=0

NterpPutCharInstanceField:
NterpPutShortInstanceField:
  OP_IPUT rINST_reg=rINSTw, store="movw", wide=0

NterpPutWideInstanceField:
  OP_IPUT rINST_reg=rINSTq, store="movq", wide=1

NterpGetBooleanInstanceField:
  OP_IGET load="movzbl", wide=0

NterpGetByteInstanceField:
  OP_IGET load="movsbl", wide=0

NterpGetCharInstanceField:
  OP_IGET load="movzwl", wide=0

NterpGetShortInstanceField:
  OP_IGET load="movswl", wide=0

NterpGetWideInstanceField:
  OP_IGET load="movq", wide=1

NterpGetInstanceField:
  OP_IGET load="movl", wide=0

NterpInstanceOf:
    /* instance-of vA, vB, class@CCCC */
   // Fast-path which gets the class from thread-local cache.
   EXPORT_PC
   FETCH_FROM_THREAD_CACHE %rsi, 2f
   cmpq $$0, rSELF:THREAD_READ_BARRIER_MARK_REG00_OFFSET
   jne 5f
1:
   movzbl  rINSTbl,%edi
   sarl    $$4,%edi                          # edi<- B
   GET_VREG %edi %rdi                        # edi<- vB (object)
   andb    $$0xf,rINSTbl                     # rINST<- A
   testl %edi, %edi
   je 3f
   call art_quick_instance_of
   SET_VREG %eax, rINSTq            # fp[A] <- value
4:
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
3:
   SET_VREG %edi, rINSTq            # fp[A] <-0
   jmp 4b
2:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   call nterp_get_class_or_allocate_object
   movq %rax, %rsi
   jmp 1b
5:
   // 06 is %rsi
   call art_quick_read_barrier_mark_reg06
   jmp 1b

NterpCheckCast:
   // Fast-path which gets the class from thread-local cache.
   EXPORT_PC
   FETCH_FROM_THREAD_CACHE %rsi, 3f
   cmpq $$0, rSELF:THREAD_READ_BARRIER_MARK_REG00_OFFSET
   jne 4f
1:
   GET_VREG %edi, rINSTq
   testl %edi, %edi
   je 2f
   call art_quick_check_instance_of
2:
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
3:
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   call nterp_get_class_or_allocate_object
   movq %rax, %rsi
   jmp 1b
4:
   // 06 is %rsi
   call art_quick_read_barrier_mark_reg06
   jmp 1b

NterpHandleHotnessOverflow:
    leaq (rPC, rINSTq, 2), %rsi
    movq rFP, %rdx
    call nterp_hot_method
    testq %rax, %rax
    jne 1f
    leaq    (rPC, rINSTq, 2), rPC
    FETCH_INST
    GOTO_NEXT
1:
    // Drop the current frame.
    movq -8(rREFS), %rsp
    CFI_DEF_CFA(rsp, CALLEE_SAVES_SIZE)

    // Setup the new frame
    movq OSR_DATA_FRAME_SIZE(%rax), %rcx
    // Given stack size contains all callee saved registers, remove them.
    subq $$CALLEE_SAVES_SIZE, %rcx

    // Remember CFA.
    movq %rsp, %rbp
    CFI_DEF_CFA_REGISTER(rbp)

    subq %rcx, %rsp
    movq %rsp, %rdi               // rdi := beginning of stack
    leaq OSR_DATA_MEMORY(%rax), %rsi  // rsi := memory to copy
    rep movsb                     // while (rcx--) { *rdi++ = *rsi++ }

    // Fetch the native PC to jump to and save it in a callee-save register.
    movq OSR_DATA_NATIVE_PC(%rax), %rbx

    // Free the memory holding OSR Data.
    movq %rax, %rdi
    call free

    // Jump to the compiled code.
    jmp *%rbx

NterpHandleInvokeInterfaceOnObjectMethodRange:
   // First argument is the 'this' pointer.
   movzwl 4(rPC), %r11d // arguments
   movl (rFP, %r11, 4), %esi
   // Note: if esi is null, this will be handled by our SIGSEGV handler.
   movl MIRROR_OBJECT_CLASS_OFFSET(%esi), %edx
   movq MIRROR_CLASS_VTABLE_OFFSET_64(%edx, %eax, 8), %rdi
   jmp NterpCommonInvokeInstanceRange

NterpHandleInvokeInterfaceOnObjectMethod:
   // First argument is the 'this' pointer.
   movzwl 4(rPC), %r11d // arguments
   andq MACRO_LITERAL(0xf), %r11
   movl (rFP, %r11, 4), %esi
   // Note: if esi is null, this will be handled by our SIGSEGV handler.
   movl MIRROR_OBJECT_CLASS_OFFSET(%esi), %edx
   movq MIRROR_CLASS_VTABLE_OFFSET_64(%edx, %eax, 8), %rdi
   jmp NterpCommonInvokeInstance

// This is the logical end of ExecuteNterpImpl, where the frame info applies.
// EndExecuteNterpImpl includes the methods below as we want the runtime to
// see them as part of the Nterp PCs.
.cfi_endproc

nterp_to_nterp_static_non_range:
    .cfi_startproc
    .cfi_def_cfa rsp, 8
    SETUP_STACK_FOR_INVOKE
    SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=1, is_string_init=0
    .cfi_endproc

nterp_to_nterp_string_init_non_range:
    .cfi_startproc
    .cfi_def_cfa rsp, 8
    SETUP_STACK_FOR_INVOKE
    SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=1
    .cfi_endproc

nterp_to_nterp_instance_non_range:
    .cfi_startproc
    .cfi_def_cfa rsp, 8
    SETUP_STACK_FOR_INVOKE
    SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=0
    .cfi_endproc

nterp_to_nterp_static_range:
    .cfi_startproc
    .cfi_def_cfa rsp, 8
    SETUP_STACK_FOR_INVOKE
    SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=1
    .cfi_endproc

nterp_to_nterp_instance_range:
    .cfi_startproc
    .cfi_def_cfa rsp, 8
    SETUP_STACK_FOR_INVOKE
    SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=0
    .cfi_endproc

nterp_to_nterp_string_init_range:
    .cfi_startproc
    .cfi_def_cfa rsp, 8
    SETUP_STACK_FOR_INVOKE
    SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=0, is_string_init=1
    .cfi_endproc

// This is the end of PCs contained by the OatQuickMethodHeader created for the interpreter
// entry point.
    FUNCTION_TYPE(EndExecuteNterpImpl)
    ASM_HIDDEN SYMBOL(EndExecuteNterpImpl)
    .global SYMBOL(EndExecuteNterpImpl)
SYMBOL(EndExecuteNterpImpl):

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

    FUNCTION_TYPE(artNterpAsmInstructionEnd)
    ASM_HIDDEN SYMBOL(artNterpAsmInstructionEnd)
    .global SYMBOL(artNterpAsmInstructionEnd)
SYMBOL(artNterpAsmInstructionEnd):
    // artNterpAsmInstructionEnd is used as landing pad for exception handling.
    FETCH_INST
    GOTO_NEXT

%def instruction_start():

    FUNCTION_TYPE(artNterpAsmInstructionStart)
    ASM_HIDDEN SYMBOL(artNterpAsmInstructionStart)
    .global SYMBOL(artNterpAsmInstructionStart)
SYMBOL(artNterpAsmInstructionStart) = .L_op_nop
    .text

%def opcode_start():
    ENTRY nterp_${opcode}
%def opcode_end():
    END nterp_${opcode}
%def helper_start(name):
    ENTRY ${name}
%def helper_end(name):
    END ${name}
