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
    subl LITERAL(8), %esp         // align stack
    CFI_ADJUST_CFA_OFFSET(8)
    pushl %fs:THREAD_SELF_OFFSET  // pass Thread::Current()
    CFI_ADJUST_CFA_OFFSET(4)
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable()
    // for @FastNative or @CriticalNative.
    movl (%esp), %eax                                // Thread* self
    movl THREAD_TOP_QUICK_FRAME_OFFSET(%eax), %eax   // uintptr_t tagged_quick_frame
    andl LITERAL(0xfffffffe), %eax                   // ArtMethod** sp
    movl (%eax), %eax                                // ArtMethod* method
    testl LITERAL(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE), \
          ART_METHOD_ACCESS_FLAGS_OFFSET(%eax)
    jne .Llookup_stub_fast_native
    call SYMBOL(artFindNativeMethod)  // (Thread*)
    jmp .Llookup_stub_continue
.Llookup_stub_fast_native:
    call SYMBOL(artFindNativeMethodRunnable)  // (Thread*)
.Llookup_stub_continue:
    addl LITERAL(12), %esp        // remove argument & padding
    CFI_ADJUST_CFA_OFFSET(-12)
    testl %eax, %eax              // check if returned method code is null
    jz .Lno_native_code_found     // if null, jump to return to handle
    jmp *%eax                     // otherwise, tail call to intended method
.Lno_native_code_found:
    ret
END_FUNCTION art_jni_dlsym_lookup_stub

DEFINE_FUNCTION art_jni_dlsym_lookup_critical_stub
    // The hidden arg holding the tagged method (bit 0 set means GenericJNI) is eax.
    // For Generic JNI we already have a managed frame, so we reuse the art_jni_dlsym_lookup_stub.
    testl LITERAL(1), %eax
    jnz art_jni_dlsym_lookup_stub

    // We need to create a GenericJNI managed frame above the stack args.

    // GenericJNI frame is similar to SaveRegsAndArgs frame with the native method
    // instead of runtime method saved at the bottom. Note that the runtime shall
    // not examine the args here, otherwise we would have to reload them from stack
    // to account for the difference between managed and native ABIs.
    SETUP_SAVE_REFS_AND_ARGS_FRAME_REGISTERS_ONLY
    pushl %eax  // Save the hidden arg as method pointer at the bottom of the stack.
    CFI_ADJUST_CFA_OFFSET(4)

    // Call artCriticalNativeOutArgsSize(method); method is conveniently at the bottom of the stack.
    call SYMBOL(artCriticalNativeOutArgsSize)

    // Check if we have any stack args other than return PC.
    cmp LITERAL(__SIZEOF_POINTER__), %eax
    jnz .Lcritical_has_stack_args

    // Without stack args, the frame is fully constructed.
    // Place tagged managed sp in Thread::Current()->top_quick_frame.
    leal 1(%esp), %eax  // Tag as GenericJNI frame.
    mov %eax, %fs:THREAD_TOP_QUICK_FRAME_OFFSET

    // Call artFindNativeMethodRunnable()
    subl LITERAL(12), %esp         // align stack
    CFI_ADJUST_CFA_OFFSET(12)
    pushl %fs:THREAD_SELF_OFFSET  // pass Thread::Current()
    CFI_ADJUST_CFA_OFFSET(4)
    call SYMBOL(artFindNativeMethodRunnable)  // (Thread*)
    addl LITERAL(16), %esp
    CFI_ADJUST_CFA_OFFSET(-16)

    // Check for exception.
    test %eax, %eax
    jz 1f

    // Restore frame and do the tail call.
    CFI_REMEMBER_STATE
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    jmp *%eax
    CFI_RESTORE_STATE_AND_DEF_CFA(%esp, FRAME_SIZE_SAVE_REFS_AND_ARGS)

1:
    DELIVER_PENDING_EXCEPTION_FRAME_READY

.Lcritical_has_stack_args:
    // As mentioned above, the runtime shall not examine the args in the managed frame
    // and since all args for the native call are on the stack, we can use the managed
    // args registers as scratch registers. So, EBX, EDX and ECX are available and we
    // do not need to restore xmm0-xmm3 either.

    // Restore registers as we're about to move stack args over the current SaveRefsAndArgs frame.
    movl (%esp), %edx   // Remember the method in EDX.
    movl 48(%esp), %ebp
    CFI_RESTORE(%ebp)
    movl 52(%esp), %esi
    CFI_RESTORE(%esi)
    movl 56(%esp), %edi
    CFI_RESTORE(%edi)

    // Calculate the address of the end of the move destination and redefine CFI to take
    // ownership of the JNI stub frame. EBX is conveniently callee-save in native ABI.
    leal 0(%esp, %eax, 1), %ebx
    CFI_DEF_CFA(%ebx, FRAME_SIZE_SAVE_REFS_AND_ARGS)

    // Calculate the number of DWORDs to move.
    shrl LITERAL(2), %eax
    leal -1(%eax), %ecx  // Do not move the return PC.

    // Load our return PC to EAX.
    movl FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__(%esp), %eax

    // Save EDI, ESI so that we can use them for moving stack args.
    pushl %edi  // No `CFI_ADJUST_CFA_OFFSET`, CFA register is currently EBX, not ESP.
    pushl %esi  // ditto

    // Mov the stack args.
    leal 2 * __SIZEOF_POINTER__(%esp), %edi
    leal FRAME_SIZE_SAVE_REFS_AND_ARGS(%edi), %esi
    rep movsd

    // Save our return PC.
    movl %eax, (%edi)

    // Restore EDI, ESI.
    popl %esi   // No `CFI_ADJUST_CFA_OFFSET`, CFA register is currently EBX, not ESP.
    popl %edi   // ditto

    // Re-create the SaveRefsAndArgs frame above the args.
    movl %edi, 56(%ebx)
    CFI_REL_OFFSET(%edi, 56)
    movl %esi, 52(%ebx)
    CFI_REL_OFFSET(%esi, 52)
    movl %ebp, 48(%ebx)
    CFI_REL_OFFSET(%ebp, 48)
    // Skip managed ABI args EBX, EDX, ECX and FPRs, see above.
    // (We have already clobbered EBX, EDX, ECX anyway).
    movl %edx, (%ebx)    // Save method pointer.

    // Place tagged managed sp in Thread::Current()->top_quick_frame.
    leal 1(%ebx), %eax  // Tag as GenericJNI frame.
    movl %eax, %fs:THREAD_TOP_QUICK_FRAME_OFFSET

    // Call artFindNativeMethodRunnable()
    subl LITERAL(12), %esp        // align stack, no `CFI_ADJUST_CFA_OFFSET`.
    pushl %fs:THREAD_SELF_OFFSET  // pass Thread::Current()
    call SYMBOL(artFindNativeMethodRunnable)  // (Thread*)
    addl LITERAL(16), %esp        // Pop args, no `CFI_ADJUST_CFA_OFFSET`.

    // Check for exception.
    test %eax, %eax
    jz 2f

    // Restore the frame. We shall not need the method anymore.
    CFI_REMEMBER_STATE
    movl 48(%ebx), %ebp
    CFI_RESTORE(%ebp)
    movl 52(%ebx), %esi
    CFI_RESTORE(%esi)
    movl 56(%ebx), %edi
    CFI_RESTORE(%edi)

    // Remember our return PC in EDX.
    movl -__SIZEOF_POINTER__(%ebx), %edx

    // Calculate the number of DWORDs to move.
    leal -__SIZEOF_POINTER__(%ebx), %ecx  // Do not move return PC.
    subl %esp, %ecx
    shrl LITERAL(2), %ecx

    // Save EDI, ESI so that we can use them for moving stack args.
    pushl %edi  // No `CFI_ADJUST_CFA_OFFSET`, CFA register is currently EBX, not ESP.
    pushl %esi  // ditto

    // Mov stack args to their original place.
    leal -2 * __SIZEOF_POINTER__(%ebx), %esi
    leal FRAME_SIZE_SAVE_REFS_AND_ARGS - 2 * __SIZEOF_POINTER__(%ebx), %edi
    std
    rep movsd
    cld

    // Store our return PC.
    movl %edx, (%edi)

    // Restore EDI, ESI.
    popl %esi   // No `CFI_ADJUST_CFA_OFFSET`, CFA register is currently EBX, not ESP.
    popl %edi   // ditto

    // Redefine CFI to release ownership of the JNI stub frame.
    CFI_DEF_CFA(%esp, FRAME_SIZE_SAVE_REFS_AND_ARGS)

    // Remove the frame reservation.
    addl LITERAL(FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__), %esp
    CFI_ADJUST_CFA_OFFSET(-FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__)

    // Do the tail call.
    jmp *%eax
    CFI_RESTORE_STATE_AND_DEF_CFA(%ebx, FRAME_SIZE_SAVE_REFS_AND_ARGS)

2:
    // Replicate DELIVER_PENDING_EXCEPTION_FRAME_READY without CFI_ADJUST_CFA_OFFSET,
    // CFA register is currently EBX, not ESP.

    // Outgoing argument set up
    subl MACRO_LITERAL(12), %esp               // alignment padding
    pushl %fs:THREAD_SELF_OFFSET               // pass Thread::Current()
    call SYMBOL(artDeliverPendingExceptionFromCode)  // artDeliverPendingExceptionFromCode(Thread*)
    UNREACHABLE
END_FUNCTION art_jni_dlsym_lookup_critical_stub
