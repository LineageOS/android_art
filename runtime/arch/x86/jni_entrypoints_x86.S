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

#include "asm_support_x86.S"

    /*
     * Jni dlsym lookup stub.
     */
DEFINE_FUNCTION art_jni_dlsym_lookup_stub
    INCREASE_FRAME 8              // Align stack.
    pushl %fs:THREAD_SELF_OFFSET  // Pass Thread::Current().
    CFI_ADJUST_CFA_OFFSET(4)
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable()
    // for @FastNative or @CriticalNative.
    movl (%esp), %eax                                // Thread* self
    movl THREAD_TOP_QUICK_FRAME_OFFSET(%eax), %eax   // uintptr_t tagged_quick_frame
    andl LITERAL(0xfffffffe), %eax                   // ArtMethod** sp
    movl (%eax), %eax                                // ArtMethod* method
    testl LITERAL(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE), \
          ART_METHOD_ACCESS_FLAGS_OFFSET(%eax)
    jne .Llookup_stub_fast_or_critical_native
    call SYMBOL(artFindNativeMethod)  // (Thread*)
    jmp .Llookup_stub_continue
.Llookup_stub_fast_or_critical_native:
    call SYMBOL(artFindNativeMethodRunnable)  // (Thread*)
.Llookup_stub_continue:
    DECREASE_FRAME 12             // Remove argument & padding.
    testl %eax, %eax              // Check if returned method code is null.
    jz .Lno_native_code_found     // If null, jump to return to handle.
    jmp *%eax                     // Otherwise, tail call to intended method.
.Lno_native_code_found:
    ret
END_FUNCTION art_jni_dlsym_lookup_stub

DEFINE_FUNCTION art_jni_dlsym_lookup_critical_stub
    // The hidden arg holding the tagged method (bit 0 set means GenericJNI) is eax.
    // For Generic JNI we already have a managed frame, so we reuse the art_jni_dlsym_lookup_stub.
    testl LITERAL(1), %eax
    jnz art_jni_dlsym_lookup_stub

    // Since the native call args are all on the stack, we can use the managed args
    // registers as scratch registers. So, EBX, EDX and ECX are available.

    // Load caller PC.
    movl (%esp), %ecx

    // Save the caller method from the hidden arg.
    PUSH_ARG eax

    // Call artCriticalNativeFrameSize(method, caller_pc).
    PUSH_ARG ecx                  // Pass caller PC.
    PUSH_ARG eax                  // Pass method.
    call SYMBOL(artCriticalNativeFrameSize)  // (method, caller_pc)
    DECREASE_FRAME 8              // Remove args.

    // Restore method register to EBX.
    POP_ARG ebx

    // Load caller PC to EDX and redefine return PC for CFI.
    movl (%esp), %edx
    CFI_REGISTER(%eip, %edx)

    // Reserve space for a SaveRefsAndArgs managed frame, either for the actual runtime
    // method or for a GenericJNI frame which is similar but has a native method and a tag.
    INCREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__

    // Calculate the number of DWORDs to move.
    movl %eax, %ecx
    shrl LITERAL(2), %ecx
    jecxz .Lcritical_skip_copy_args

    // Save EDI, ESI so that we can use them for moving stack args.
    PUSH edi
    PUSH esi

    // Move the stack args.
    leal 2 * __SIZEOF_POINTER__(%esp), %edi
    leal FRAME_SIZE_SAVE_REFS_AND_ARGS(%edi), %esi
    rep movsd

    // Restore EDI, ESI.
    POP esi
    POP edi

.Lcritical_skip_copy_args:
    // Calculate the base address of the managed frame.
    leal (%esp, %eax, 1), %eax

    leal 1(%eax), %ecx            // Prepare namaged SP tagged for a GenericJNI frame.
    testl LITERAL(ACCESS_FLAGS_METHOD_IS_NATIVE), ART_METHOD_ACCESS_FLAGS_OFFSET(%ebx)
    jnz .Lcritical_skip_prepare_runtime_method

    // Save the return PC for managed stack walk.
    // (When coming from a compiled stub, the correct return PC is already there.)
    movl %edx, FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__(%eax)

    // Replace the target method with the SaveRefsAndArgs runtime method.
    LOAD_RUNTIME_INSTANCE ecx, ebx
    movl RUNTIME_SAVE_REFS_AND_ARGS_METHOD_OFFSET(%ecx), %ebx

    movl %eax, %ecx               // Prepare untagged managed SP for the runtime method.

.Lcritical_skip_prepare_runtime_method:
    // Store the method on the bottom of the managed frame.
    movl %ebx, (%eax)

    // Move the managed frame address to native callee-save register EBX.
    movl %eax, %ebx

    // Spill registers for the SaveRefsAndArgs frame above the stack args.
    movl %edi, 56(%ebx)
    CFI_EXPRESSION_BREG CFI_REG(edi), CFI_REG(ebx), 56
    movl %esi, 52(%ebx)
    CFI_EXPRESSION_BREG CFI_REG(esi), CFI_REG(ebx), 52
    movl %ebp, 48(%ebx)
    CFI_EXPRESSION_BREG CFI_REG(ebp), CFI_REG(ebx), 48
    // Skip managed ABI args EBX, EDX, ECX and FPRs. The runtime shall not examine the
    // args in the managed frame. (We have already clobbered EBX, EDX, ECX anyway.)

    // Place (maybe tagged) managed SP in Thread::Current()->top_quick_frame.
    movl %ecx, %fs:THREAD_TOP_QUICK_FRAME_OFFSET

    // Save our return PC in a slot reserved for first FP arg in managed ABI.
    movl %edx, __SIZEOF_POINTER__(%ebx)
    CFI_EXPRESSION_BREG CFI_REG(eip), CFI_REG(ebx), __SIZEOF_POINTER__

    // Call artFindNativeMethodRunnable()
    INCREASE_FRAME 12             // Align stack.
    pushl %fs:THREAD_SELF_OFFSET  // pass Thread::Current()
    CFI_ADJUST_CFA_OFFSET(4)
    call SYMBOL(artFindNativeMethodRunnable)  // (Thread*)
    addl LITERAL(16), %esp
    CFI_ADJUST_CFA_OFFSET(-16)

    // Check for exception.
    test %eax, %eax
    jz .Lcritical_deliver_exception

    CFI_REMEMBER_STATE

    // Remember our return PC in EDX.
    movl __SIZEOF_POINTER__(%ebx), %edx
    CFI_REGISTER(%eip, %edx)

    // Restore callee-save registers from the frame. We shall not need the method anymore.
    movl 48(%ebx), %ebp
    CFI_RESTORE(%ebp)
    movl 52(%ebx), %esi
    CFI_RESTORE(%esi)
    movl 56(%ebx), %edi
    CFI_RESTORE(%edi)

    // Calculate the number of DWORDs to move.
    movl %ebx, %ecx
    subl %esp, %ecx
    shrl LITERAL(2), %ecx
    jecxz .Lcritical_skip_copy_args_back

    // Save EDI, ESI so that we can use them for moving stack args.
    PUSH edi
    PUSH esi

    // Move stack args to their original place.
    leal -__SIZEOF_POINTER__(%ebx), %esi
    leal FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__(%ebx), %edi
    std
    rep movsd
    cld

    // Restore EDI, ESI.
    POP esi
    POP edi

.Lcritical_skip_copy_args_back:
    // Remove the frame reservation.
    DECREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__

    // Store our return PC.
    movl %edx, (%esp)
    CFI_REL_OFFSET(%eip, 0)

    // Do the tail call.
    jmp *%eax
    CFI_RESTORE_STATE_AND_DEF_CFA(%esp, FRAME_SIZE_SAVE_REFS_AND_ARGS)

.Lcritical_deliver_exception:
    DELIVER_PENDING_EXCEPTION_FRAME_READY
END_FUNCTION art_jni_dlsym_lookup_critical_stub
