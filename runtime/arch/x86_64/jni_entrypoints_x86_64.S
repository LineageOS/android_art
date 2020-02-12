/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include "asm_support_x86_64.S"

    /*
     * Jni dlsym lookup stub.
     */
DEFINE_FUNCTION art_jni_dlsym_lookup_stub
    // Save callee and GPR args.
    PUSH_ARG r9   // Arg.
    PUSH_ARG r8   // Arg.
    PUSH_ARG rdi  // Arg. (JniEnv for normal and @FastNative)
    PUSH_ARG rsi  // Arg.
    PUSH_ARG rdx  // Arg.
    PUSH_ARG rcx  // Arg.
    // Create space for FPR args, plus padding for alignment
    subq LITERAL(72), %rsp
    CFI_ADJUST_CFA_OFFSET(72)
    // Save FPRs.
    movq %xmm0, 0(%rsp)
    movq %xmm1, 8(%rsp)
    movq %xmm2, 16(%rsp)
    movq %xmm3, 24(%rsp)
    movq %xmm4, 32(%rsp)
    movq %xmm5, 40(%rsp)
    movq %xmm6, 48(%rsp)
    movq %xmm7, 56(%rsp)
    // prepare call
    movq %gs:THREAD_SELF_OFFSET, %rdi      // RDI := Thread::Current()
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable()
    // for @FastNative or @CriticalNative.
    movq THREAD_TOP_QUICK_FRAME_OFFSET(%rdi), %rax   // uintptr_t tagged_quick_frame
    andq LITERAL(0xfffffffffffffffe), %rax           // ArtMethod** sp
    movq (%rax), %rax                                // ArtMethod* method
    testl LITERAL(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE), \
          ART_METHOD_ACCESS_FLAGS_OFFSET(%rax)
    jne .Llookup_stub_fast_native
    call SYMBOL(artFindNativeMethod)  // (Thread*)
    jmp .Llookup_stub_continue
.Llookup_stub_fast_native:
    call SYMBOL(artFindNativeMethodRunnable)  // (Thread*)
.Llookup_stub_continue:
    // restore arguments
    movq 0(%rsp), %xmm0
    movq 8(%rsp), %xmm1
    movq 16(%rsp), %xmm2
    movq 24(%rsp), %xmm3
    movq 32(%rsp), %xmm4
    movq 40(%rsp), %xmm5
    movq 48(%rsp), %xmm6
    movq 56(%rsp), %xmm7
    addq LITERAL(72), %rsp
    CFI_ADJUST_CFA_OFFSET(-72)
    POP_ARG rcx  // Arg.
    POP_ARG rdx  // Arg.
    POP_ARG rsi  // Arg.
    POP_ARG rdi  // Arg. (JniEnv for normal and @FastNative)
    POP_ARG r8   // Arg.
    POP_ARG r9   // Arg.
    testq %rax, %rax              // check if returned method code is null
    jz .Lno_native_code_found     // if null, jump to return to handle
    jmp *%rax                     // otherwise, tail call to intended method
.Lno_native_code_found:
    ret
END_FUNCTION art_jni_dlsym_lookup_stub

DEFINE_FUNCTION art_jni_dlsym_lookup_critical_stub
    // The hidden arg holding the tagged method (bit 0 set means GenericJNI) is r11.
    // For Generic JNI we already have a managed frame, so we reuse the art_jni_dlsym_lookup_stub.
    testq LITERAL(1), %r11
    jnz art_jni_dlsym_lookup_stub

    // We need to create a GenericJNI managed frame above the stack args.

    // GenericJNI frame is similar to SaveRegsAndArgs frame with the native method
    // instead of runtime method saved at the bottom.

    // As we always have "stack args" on x86-64 (due to xmm12-xmm15 being callee-save
    // in managed ABI but caller-save in native ABI), do not create a proper frame yet
    // as we do on other architectures where it's useful for no stack args case.

    // Reserve space for the frame (return PC is on stack).
    subq MACRO_LITERAL(FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__), %rsp
    CFI_ADJUST_CFA_OFFSET(FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__)

    // Save GPR args.
    PUSH_ARG r9
    PUSH_ARG r8
    PUSH_ARG rdi
    PUSH_ARG rsi
    PUSH_ARG rdx
    PUSH_ARG rcx
    // Create space for FPR args.
    subq LITERAL(64), %rsp
    CFI_ADJUST_CFA_OFFSET(64)
    // Save FPRs.
    movq %xmm0, 0(%rsp)
    movq %xmm1, 8(%rsp)
    movq %xmm2, 16(%rsp)
    movq %xmm3, 24(%rsp)
    movq %xmm4, 32(%rsp)
    movq %xmm5, 40(%rsp)
    movq %xmm6, 48(%rsp)
    movq %xmm7, 56(%rsp)

    // Add alignment padding.
    subq MACRO_LITERAL(__SIZEOF_POINTER__), %rsp
    CFI_ADJUST_CFA_OFFSET(__SIZEOF_POINTER__)
    // Save hidden arg.
    PUSH_ARG r11

    // Call artCriticalNativeOutArgsSize(method).
    movq %r11, %rdi  // Pass the method from hidden arg.
    call SYMBOL(artCriticalNativeOutArgsSize)

    // Calculate the address of the end of the move destination and redefine CFI to take
    // ownership of the JNI stub frame.
    leaq 16 * __SIZEOF_POINTER__(%rsp, %rax, 1), %r10  // 16 QWORDs of registers saved above.
    CFI_DEF_CFA(%r10, FRAME_SIZE_SAVE_REFS_AND_ARGS)

    // Calculate the number of QWORDs to move.
    shrq LITERAL(3), %rax
    leaq -1(%rax), %rcx  // Do not move the return PC.

    // Load our return PC to EAX.
    movq FRAME_SIZE_SAVE_REFS_AND_ARGS + (16 - 1) * __SIZEOF_POINTER__(%rsp), %rax

    // Mov the stack args.
    leaq 16 * __SIZEOF_POINTER__(%rsp), %rdi
    leaq FRAME_SIZE_SAVE_REFS_AND_ARGS(%rdi), %rsi
    rep movsq

    // Save our return PC.
    movq %rax, (%rdi)

    // Pop the hidden arg and alignment padding.
    popq %r11    // No `.cfi_adjust_cfa_offset`, CFA register is currently R10, not RSP.
    addq MACRO_LITERAL(__SIZEOF_POINTER__), %rsp  // ditto

    // Fill the SaveRefsAndArgs frame above the args, without actual args. Note that
    // the runtime shall not examine the args here, otherwise we would have to move them in
    // registers and stack to account for the difference between managed and native ABIs.
    SAVE_REG_BASE r10, r15, 192
    SAVE_REG_BASE r10, r14, 184
    SAVE_REG_BASE r10, r13, 176
    SAVE_REG_BASE r10, r12, 168
    // Skip args r9, r8, rsi.
    SAVE_REG_BASE r10, rbp, 136
    SAVE_REG_BASE r10, rbx, 128
    // Skip args rdx, rcx.
    // Skip args xmm0-xmm7.
    // Copy managed callee-saves xmm12-xmm15 from out args to the managed frame as they
    // may theoretically store variables or unwinding data. (The compiled stub preserves
    // them but the artCriticalNativeOutArgsSize() call above may clobber them.)
    movq -5 * __SIZEOF_POINTER__(%r10), %xmm12
    movq -4 * __SIZEOF_POINTER__(%r10), %xmm13
    movq -3 * __SIZEOF_POINTER__(%r10), %xmm14
    movq -2 * __SIZEOF_POINTER__(%r10), %xmm15
    movq %xmm12, 80(%r10)
    movq %xmm13, 88(%r10)
    movq %xmm14, 96(%r10)
    movq %xmm15, 104(%r10)
    // Save the hidden arg as method pointer at the bottom of the stack.
    movq %r11, (%r10)

    // Move the frame register to a callee-save register.
    movq %r10, %rbp
    CFI_DEF_CFA_REGISTER(%rbp)

    // Place tagged managed sp in Thread::Current()->top_quick_frame.
    leaq 1(%rbp), %rax  // Tag as GenericJNI frame.
    movq %rax, %gs:THREAD_TOP_QUICK_FRAME_OFFSET

    // Call artFindNativeMethodRunnable()
    movq %gs:THREAD_SELF_OFFSET, %rdi  // pass Thread::Current()
    call SYMBOL(artFindNativeMethodRunnable)  // (Thread*)

    // Check for exception.
    test %rax, %rax
    jz 2f

    // Restore the frame. We shall not need the method anymore.
    .cfi_remember_state
    movq %rbp, %r10
    CFI_DEF_CFA_REGISTER(%r10)
    // Skip args xmm0-xmm7 and managed callee-saves xmm12-xmm15 (not needed for native call).
    // Skip args rdx, rcx.
    RESTORE_REG_BASE r10, rbx, 128
    RESTORE_REG_BASE r10, rbp, 136
    // Skip args r9, r8, rsi.
    RESTORE_REG_BASE r10, r12, 168
    RESTORE_REG_BASE r10, r13, 176
    RESTORE_REG_BASE r10, r14, 184
    RESTORE_REG_BASE r10, r15, 192

    // Remember our return PC in R11.
    movq -__SIZEOF_POINTER__(%r10), %r11

    // Calculate the number of DWORDs to move.
    leaq -(1 + 14) * __SIZEOF_POINTER__(%r10), %rcx  // Do not move return PC, 14 arg regs saved.
    subq %rsp, %rcx
    shrq LITERAL(3), %rcx

    // Mov stack args to their original place.
    leaq -2 * __SIZEOF_POINTER__(%r10), %rsi
    leaq FRAME_SIZE_SAVE_REFS_AND_ARGS - 2 * __SIZEOF_POINTER__(%r10), %rdi
    std
    rep movsq
    cld

    // Store our return PC.
    movq %r11, (%rdi)

    // Redefine CFI to release ownership of the JNI stub frame.
    CFI_DEF_CFA(%rsp, FRAME_SIZE_SAVE_REFS_AND_ARGS + 14 * __SIZEOF_POINTER__)

    // Restore args.
    movq 0(%rsp), %xmm0
    movq 8(%rsp), %xmm1
    movq 16(%rsp), %xmm2
    movq 24(%rsp), %xmm3
    movq 32(%rsp), %xmm4
    movq 40(%rsp), %xmm5
    movq 48(%rsp), %xmm6
    movq 56(%rsp), %xmm7
    addq LITERAL(64), %rsp
    CFI_ADJUST_CFA_OFFSET(-64)
    POP_ARG rcx
    POP_ARG rdx
    POP_ARG rsi
    POP_ARG rdi
    POP_ARG r8
    POP_ARG r9

    // Remove the frame reservation.
    addq LITERAL(FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__), %rsp
    CFI_ADJUST_CFA_OFFSET(-(FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__))

    // Do the tail call.
    jmp *%rax
    CFI_RESTORE_STATE_AND_DEF_CFA(%rbp, FRAME_SIZE_SAVE_REFS_AND_ARGS)

2:
    // Drop the args from the stack (the r11 and padding was already removed).
    addq LITERAL(14 * __SIZEOF_POINTER__), %rsp

    DELIVER_PENDING_EXCEPTION_FRAME_READY
END_FUNCTION art_jni_dlsym_lookup_critical_stub
