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

#ifndef ART_RUNTIME_ARCH_X86_64_ASM_SUPPORT_X86_64_S_
#define ART_RUNTIME_ARCH_X86_64_ASM_SUPPORT_X86_64_S_

#include "asm_support_x86_64.h"
#include "interpreter/cfi_asm_support.h"

// Regular gas(1) & current clang/llvm assembler support named macro parameters.
#define MACRO0(macro_name) .macro macro_name
#define MACRO1(macro_name, macro_arg1) .macro macro_name macro_arg1
#define MACRO2(macro_name, macro_arg1, macro_arg2) .macro macro_name macro_arg1, macro_arg2
#define MACRO3(macro_name, macro_arg1, macro_arg2, macro_arg3) .macro macro_name macro_arg1, macro_arg2, macro_arg3
#define MACRO4(macro_name, macro_arg1, macro_arg2, macro_arg3, macro_arg4) .macro macro_name macro_arg1, macro_arg2, macro_arg3, macro_arg4
#define END_MACRO .endm

#if defined(__clang__)
    // Clang/llvm does not support .altmacro. However, the clang/llvm preprocessor doesn't
    // separate the backslash and parameter by a space. Everything just works.
    #define RAW_VAR(name) \name
    #define VAR(name) \name
    #define CALLVAR(name) SYMBOL(\name)
    #define PLT_VAR(name) \name@PLT
    #define REG_VAR(name) %\name
    #define CALL_MACRO(name) \name
#else
    // Regular gas(1) uses \argument_name for macro arguments.
    // We need to turn on alternate macro syntax so we can use & instead or the preprocessor
    // will screw us by inserting a space between the \ and the name. Even in this mode there's
    // no special meaning to $, so literals are still just $x. The use of altmacro means % is a
    // special character meaning care needs to be taken when passing registers as macro
    // arguments.
    .altmacro
    #define RAW_VAR(name) name&
    #define VAR(name) name&
    #define CALLVAR(name) SYMBOL(name&)
    #define PLT_VAR(name) name&@PLT
    #define REG_VAR(name) %name
    #define CALL_MACRO(name) name&
#endif

#define LITERAL(value) $value
#if defined(__APPLE__)
    #define MACRO_LITERAL(value) $(value)
#else
    #define MACRO_LITERAL(value) $value
#endif

#if defined(__APPLE__)
    #define FUNCTION_TYPE(name)
    #define SIZE(name)
#else
    #define FUNCTION_TYPE(name) .type name, @function
    #define SIZE(name) .size name, .-name
#endif

    // CFI support.
#if !defined(__APPLE__)
    #define CFI_STARTPROC .cfi_startproc
    #define CFI_ENDPROC .cfi_endproc
    #define CFI_ADJUST_CFA_OFFSET(size) .cfi_adjust_cfa_offset size
    #define CFI_DEF_CFA(reg,size) .cfi_def_cfa reg,size
    #define CFI_DEF_CFA_REGISTER(reg) .cfi_def_cfa_register reg
    #define CFI_RESTORE(reg) .cfi_restore reg
    #define CFI_REL_OFFSET(reg,size) .cfi_rel_offset reg,size
    #define CFI_REGISTER(orig_reg, current_reg) .cfi_register orig_reg, current_reg
    #define CFI_REMEMBER_STATE .cfi_remember_state
    // The spec is not clear whether the CFA is part of the saved state and tools
    // differ in the behaviour, so explicitly set the CFA to avoid any ambiguity.
    // The restored CFA state should match the CFA state during CFI_REMEMBER_STATE.
    // `objdump -Wf libart.so | egrep "_cfa|_state"` is useful to audit the opcodes.
    #define CFI_RESTORE_STATE_AND_DEF_CFA(reg,off) .cfi_restore_state .cfi_def_cfa reg,off
    #define CFI_RESTORE_STATE .cfi_restore_state
#else
    // Mac OS' doesn't like cfi_* directives.
    #define CFI_STARTPROC
    #define CFI_ENDPROC
    #define CFI_ADJUST_CFA_OFFSET(size)
    #define CFI_DEF_CFA(reg,size)
    #define CFI_DEF_CFA_REGISTER(reg)
    #define CFI_RESTORE(reg)
    #define CFI_REL_OFFSET(reg,size)
    #define CFI_REGISTER(orig_reg, current_reg)
    #define CFI_REMEMBER_STATE
    #define CFI_RESTORE_STATE_AND_DEF_CFA(off)
    #define CFI_RESTORE_STATE
#endif

// The register numbers are a bit mixed up for x86-64.
#define CFI_REG_rax 0
#define CFI_REG_rcx 2
#define CFI_REG_rdx 1
#define CFI_REG_rbx 3
#define CFI_REG_rsp 7
#define CFI_REG_rbp 6
#define CFI_REG_rsi 4
#define CFI_REG_rdi 5
#define CFI_REG_r8 8
#define CFI_REG_r9 9
#define CFI_REG_r10 10
#define CFI_REG_r11 11
#define CFI_REG_r12 12
#define CFI_REG_r13 13
#define CFI_REG_r14 14
#define CFI_REG_r15 15
#define CFI_REG_rip 16

#define CFI_REG(reg) CFI_REG_##reg

MACRO3(CFI_EXPRESSION_BREG, n, b, offset)
    .if (-0x40 <= (\offset)) && ((\offset) < 0x40)
        CFI_EXPRESSION_BREG_1(\n, \b, \offset)
    .elseif (-0x2000 <= (\offset)) && ((\offset) < 0x2000)
        CFI_EXPRESSION_BREG_2(\n, \b, \offset)
    .else
        .error "Unsupported offset"
    .endif
END_MACRO

    // Symbols.
#if !defined(__APPLE__)
    #define SYMBOL(name) name
    #define PLT_SYMBOL(name) name ## @PLT
#else
    #define SYMBOL(name) _ ## name
    #define PLT_SYMBOL(name) _ ## name
#endif

// Directive to hide a function symbol.
#if defined(__APPLE__)
    #define ASM_HIDDEN .private_extern
#else
    #define ASM_HIDDEN .hidden
#endif

    /* Cache alignment for function entry */
MACRO0(ALIGN_FUNCTION_ENTRY)
    // ART-compiled functions have OatQuickMethodHeader but assembly funtions do not.
    // Prefix the assembly code with 0xFFs, which means there is no method header.
    .byte 0xFF, 0xFF, 0xFF, 0xFF
    // Cache alignment for function entry.
    .balign 16, 0xFF
END_MACRO

// TODO: we might need to use SYMBOL() here to add the underscore prefix
// for mac builds.
MACRO2(DEFINE_FUNCTION_CUSTOM_CFA, c_name, cfa_offset)
    FUNCTION_TYPE(SYMBOL(\c_name))
    ASM_HIDDEN CALLVAR(c_name)
    .globl CALLVAR(c_name)
    ALIGN_FUNCTION_ENTRY
CALLVAR(c_name):
    CFI_STARTPROC
    // Ensure we get a sane starting CFA.
    CFI_DEF_CFA(rsp, RAW_VAR(cfa_offset))
END_MACRO

MACRO1(DEFINE_FUNCTION, c_name)
    DEFINE_FUNCTION_CUSTOM_CFA RAW_VAR(c_name), __SIZEOF_POINTER__
END_MACRO

MACRO1(END_FUNCTION, c_name)
    CFI_ENDPROC
    SIZE(SYMBOL(\c_name))
END_MACRO

MACRO1(PUSH, reg)
    pushq REG_VAR(reg)
    CFI_ADJUST_CFA_OFFSET(8)
    CFI_REL_OFFSET(REG_VAR(reg), 0)
END_MACRO

MACRO1(POP, reg)
    popq REG_VAR(reg)
    CFI_ADJUST_CFA_OFFSET(-8)
    CFI_RESTORE(REG_VAR(reg))
END_MACRO

// Arguments do not need .cfi_rel_offset as they are caller-saved and
// therefore cannot hold caller's variables or unwinding data.
MACRO1(PUSH_ARG, reg)
    pushq REG_VAR(reg)
    CFI_ADJUST_CFA_OFFSET(8)
END_MACRO

MACRO1(POP_ARG, reg)
    popq REG_VAR(reg)
    CFI_ADJUST_CFA_OFFSET(-8)
END_MACRO

MACRO3(SAVE_REG_BASE, base, reg, offset)
    movq REG_VAR(reg), RAW_VAR(offset)(REG_VAR(base))
    CFI_REL_OFFSET(REG_VAR(reg), RAW_VAR(offset))
END_MACRO

MACRO3(RESTORE_REG_BASE, base, reg, offset)
    movq RAW_VAR(offset)(REG_VAR(base)), REG_VAR(reg)
    CFI_RESTORE(REG_VAR(reg))
END_MACRO

MACRO1(INCREASE_FRAME, frame_adjustment)
    subq MACRO_LITERAL(RAW_VAR(frame_adjustment)), %rsp
    CFI_ADJUST_CFA_OFFSET((RAW_VAR(frame_adjustment)))
END_MACRO

MACRO1(DECREASE_FRAME, frame_adjustment)
    addq MACRO_LITERAL(RAW_VAR(frame_adjustment)), %rsp
    CFI_ADJUST_CFA_OFFSET(-(RAW_VAR(frame_adjustment)))
END_MACRO

MACRO1(UNIMPLEMENTED,name)
    FUNCTION_TYPE(SYMBOL(\name))
    ASM_HIDDEN VAR(name)
    .globl VAR(name)
    ALIGN_FUNCTION_ENTRY
VAR(name):
    CFI_STARTPROC
    int3
    int3
    CFI_ENDPROC
    SIZE(SYMBOL(\name))
END_MACRO

MACRO0(UNREACHABLE)
    int3
END_MACRO

MACRO0(UNTESTED)
    int3
END_MACRO

MACRO1(LOAD_RUNTIME_INSTANCE, reg)
    movq _ZN3art7Runtime9instance_E@GOTPCREL(%rip), REG_VAR(reg)
    movq (REG_VAR(reg)), REG_VAR(reg)
END_MACRO

// Macros to poison (negate) the reference for heap poisoning.
MACRO1(POISON_HEAP_REF, rRef)
#ifdef USE_HEAP_POISONING
    negl REG_VAR(rRef)
#endif  // USE_HEAP_POISONING
END_MACRO

// Macros to unpoison (negate) the reference for heap poisoning.
MACRO1(UNPOISON_HEAP_REF, rRef)
#ifdef USE_HEAP_POISONING
    negl REG_VAR(rRef)
#endif  // USE_HEAP_POISONING
END_MACRO

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveRefsOnly)
     */
MACRO0(SETUP_SAVE_REFS_ONLY_FRAME)
#if defined(__APPLE__)
    int3
    int3
#else
    // R10 := Runtime::Current()
    LOAD_RUNTIME_INSTANCE r10
    // Save callee and GPR args, mixed together to agree with core spills bitmap.
    PUSH r15  // Callee save.
    PUSH r14  // Callee save.
    PUSH r13  // Callee save.
    PUSH r12  // Callee save.
    PUSH rbp  // Callee save.
    PUSH rbx  // Callee save.
    // Create space for FPR args, plus space for ArtMethod*.
    INCREASE_FRAME 8 + 4 * 8
    // Save FPRs.
    movq %xmm12, 8(%rsp)
    movq %xmm13, 16(%rsp)
    movq %xmm14, 24(%rsp)
    movq %xmm15, 32(%rsp)
    // R10 := ArtMethod* for refs only callee save frame method.
    movq RUNTIME_SAVE_REFS_ONLY_METHOD_OFFSET(%r10), %r10
    // Store ArtMethod* to bottom of stack.
    movq %r10, 0(%rsp)
    // Store rsp as the stop quick frame.
    movq %rsp, %gs:THREAD_TOP_QUICK_FRAME_OFFSET

    // Ugly compile-time check, but we only have the preprocessor.
    // Last +8: implicit return address pushed on stack when caller made call.
#if (FRAME_SIZE_SAVE_REFS_ONLY != 6 * 8 + 4 * 8 + 8 + 8)
#error "FRAME_SIZE_SAVE_REFS_ONLY(X86_64) size not as expected."
#endif
#endif  // __APPLE__
END_MACRO

MACRO0(RESTORE_SAVE_REFS_ONLY_FRAME)
    movq 8(%rsp), %xmm12
    movq 16(%rsp), %xmm13
    movq 24(%rsp), %xmm14
    movq 32(%rsp), %xmm15
    DECREASE_FRAME 8 + 4*8
    // TODO: optimize by not restoring callee-saves restored by the ABI
    POP rbx
    POP rbp
    POP r12
    POP r13
    POP r14
    POP r15
END_MACRO

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveRefsAndArgs), except for storing the method.
     */
MACRO0(SETUP_SAVE_REFS_AND_ARGS_FRAME_REGISTERS_ONLY)
    // Save callee and GPR args, mixed together to agree with core spills bitmap.
    PUSH r15      // Callee save.
    PUSH r14      // Callee save.
    PUSH r13      // Callee save.
    PUSH r12      // Callee save.
    PUSH_ARG r9   // Quick arg 5.
    PUSH_ARG r8   // Quick arg 4.
    PUSH_ARG rsi  // Quick arg 1.
    PUSH rbp      // Callee save.
    PUSH rbx      // Callee save.
    PUSH_ARG rdx  // Quick arg 2.
    PUSH_ARG rcx  // Quick arg 3.
    // Create space for FPR args and create 2 slots for ArtMethod*.
    INCREASE_FRAME 16 + 12 * 8
    // Save FPRs.
    movq %xmm0, 16(%rsp)
    movq %xmm1, 24(%rsp)
    movq %xmm2, 32(%rsp)
    movq %xmm3, 40(%rsp)
    movq %xmm4, 48(%rsp)
    movq %xmm5, 56(%rsp)
    movq %xmm6, 64(%rsp)
    movq %xmm7, 72(%rsp)
    movq %xmm12, 80(%rsp)
    movq %xmm13, 88(%rsp)
    movq %xmm14, 96(%rsp)
    movq %xmm15, 104(%rsp)

    // Ugly compile-time check, but we only have the preprocessor.
    // Last +8: implicit return address pushed on stack when caller made call.
#if (FRAME_SIZE_SAVE_REFS_AND_ARGS != 11 * 8 + 12 * 8 + 16 + 8)
#error "FRAME_SIZE_SAVE_REFS_AND_ARGS(X86_64) size not as expected."
#endif
END_MACRO

MACRO0(RESTORE_SAVE_REFS_AND_ARGS_FRAME)
    // Restore FPRs.
    movq 16(%rsp), %xmm0
    movq 24(%rsp), %xmm1
    movq 32(%rsp), %xmm2
    movq 40(%rsp), %xmm3
    movq 48(%rsp), %xmm4
    movq 56(%rsp), %xmm5
    movq 64(%rsp), %xmm6
    movq 72(%rsp), %xmm7
    movq 80(%rsp), %xmm12
    movq 88(%rsp), %xmm13
    movq 96(%rsp), %xmm14
    movq 104(%rsp), %xmm15
    DECREASE_FRAME 80 + 4 * 8
    // Restore callee and GPR args, mixed together to agree with core spills bitmap.
    POP_ARG rcx
    POP_ARG rdx
    POP rbx
    POP rbp
    POP_ARG rsi
    POP_ARG r8
    POP_ARG r9
    POP r12
    POP r13
    POP r14
    POP r15
END_MACRO

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveAllCalleeSaves)
     */
MACRO0(SETUP_SAVE_ALL_CALLEE_SAVES_FRAME)
#if defined(__APPLE__)
    int3
    int3
#else
    // R10 := Runtime::Current()
    LOAD_RUNTIME_INSTANCE r10
    // Save callee save registers to agree with core spills bitmap.
    PUSH r15  // Callee save.
    PUSH r14  // Callee save.
    PUSH r13  // Callee save.
    PUSH r12  // Callee save.
    PUSH rbp  // Callee save.
    PUSH rbx  // Callee save.
    // Create space for FPR args, plus space for ArtMethod*.
    INCREASE_FRAME 4 * 8 + 8
    // Save FPRs.
    movq %xmm12, 8(%rsp)
    movq %xmm13, 16(%rsp)
    movq %xmm14, 24(%rsp)
    movq %xmm15, 32(%rsp)
    // R10 := ArtMethod* for save all callee save frame method.
    movq RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET(%r10), %r10
    // Store ArtMethod* to bottom of stack.
    movq %r10, 0(%rsp)
    // Store rsp as the top quick frame.
    movq %rsp, %gs:THREAD_TOP_QUICK_FRAME_OFFSET

    // Ugly compile-time check, but we only have the preprocessor.
    // Last +8: implicit return address pushed on stack when caller made call.
#if (FRAME_SIZE_SAVE_ALL_CALLEE_SAVES != 6 * 8 + 4 * 8 + 8 + 8)
#error "FRAME_SIZE_SAVE_ALL_CALLEE_SAVES(X86_64) size not as expected."
#endif
#endif  // __APPLE__
END_MACRO

MACRO0(SETUP_FP_CALLEE_SAVE_FRAME)
    // Create space for ART FP callee-saved registers
    INCREASE_FRAME 4 * 8
    movq %xmm12, 0(%rsp)
    movq %xmm13, 8(%rsp)
    movq %xmm14, 16(%rsp)
    movq %xmm15, 24(%rsp)
END_MACRO

MACRO0(RESTORE_FP_CALLEE_SAVE_FRAME)
    // Restore ART FP callee-saved registers
    movq 0(%rsp), %xmm12
    movq 8(%rsp), %xmm13
    movq 16(%rsp), %xmm14
    movq 24(%rsp), %xmm15
    DECREASE_FRAME 4 * 8
END_MACRO

    /*
     * Macro that calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_ when the runtime method frame is ready.
     */
MACRO0(DELIVER_PENDING_EXCEPTION_FRAME_READY)
    // (Thread*) setup
    movq %gs:THREAD_SELF_OFFSET, %rdi
    call SYMBOL(artDeliverPendingExceptionFromCode)  // artDeliverPendingExceptionFromCode(Thread*)
    UNREACHABLE
END_MACRO
    /*
     * Macro that calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_.
     */
MACRO0(DELIVER_PENDING_EXCEPTION)
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME        // save callee saves for throw
    DELIVER_PENDING_EXCEPTION_FRAME_READY
END_MACRO

MACRO0(RETURN_OR_DELIVER_PENDING_EXCEPTION)
    movq %gs:THREAD_EXCEPTION_OFFSET, %rcx // get exception field
    testq %rcx, %rcx               // rcx == 0 ?
    jnz 1f                         // if rcx != 0 goto 1
    ret                            // return
1:                                 // deliver exception on current thread
    DELIVER_PENDING_EXCEPTION
END_MACRO

#endif  // ART_RUNTIME_ARCH_X86_64_ASM_SUPPORT_X86_64_S_
