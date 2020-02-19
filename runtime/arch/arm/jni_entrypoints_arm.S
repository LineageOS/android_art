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

#include "asm_support_arm.S"

    /*
     * Jni dlsym lookup stub.
     */
    .extern artFindNativeMethod
    .extern artFindNativeMethodRunnable
ENTRY art_jni_dlsym_lookup_stub
    push   {r0, r1, r2, r3, lr}           @ spill regs
    .cfi_adjust_cfa_offset 20
    .cfi_rel_offset r0, 0
    .cfi_rel_offset r1, 4
    .cfi_rel_offset r2, 8
    .cfi_rel_offset r3, 12
    .cfi_rel_offset lr, 16
    sub    sp, #12                        @ pad stack pointer to align frame
    .cfi_adjust_cfa_offset 12

    mov    r0, rSELF                      @ pass Thread::Current()
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable()
    // for @FastNative or @CriticalNative.
    ldr    ip, [r0, #THREAD_TOP_QUICK_FRAME_OFFSET]   // uintptr_t tagged_quick_frame
    bic    ip, #1                                     // ArtMethod** sp
    ldr    ip, [ip]                                   // ArtMethod* method
    ldr    ip, [ip, #ART_METHOD_ACCESS_FLAGS_OFFSET]  // uint32_t access_flags
    tst    ip, #(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE)
    bne    .Llookup_stub_fast_native
    blx    artFindNativeMethod
    b      .Llookup_stub_continue
.Llookup_stub_fast_native:
    blx    artFindNativeMethodRunnable
.Llookup_stub_continue:
    mov    r12, r0                        @ save result in r12

    add    sp, #12                        @ restore stack pointer
    .cfi_adjust_cfa_offset -12
    cbz    r0, 1f                         @ is method code null?
    pop    {r0, r1, r2, r3, lr}           @ restore regs
    .cfi_adjust_cfa_offset -20
    .cfi_restore r0
    .cfi_restore r1
    .cfi_restore r2
    .cfi_restore r3
    .cfi_restore lr
    bx     r12                            @ if non-null, tail call to method's code
1:
    pop    {r0, r1, r2, r3, pc}           @ restore regs and return to caller to handle exception
END art_jni_dlsym_lookup_stub

ENTRY art_jni_dlsym_lookup_critical_stub
    // The hidden arg holding the tagged method (bit 0 set means GenericJNI) is r4.
    // For Generic JNI we already have a managed frame, so we reuse the art_jni_dlsym_lookup_stub.
    tst    r4, #1
    bne art_jni_dlsym_lookup_stub

    // We need to create a GenericJNI managed frame above the stack args.

    // GenericJNI frame is similar to SaveRegsAndArgs frame with the native method
    // instead of runtime method saved at the bottom. Note that the runtime shall
    // not examine the args here, otherwise we would have to move them in registers
    // and stack to account for the difference between managed and native ABIs.
    SETUP_SAVE_REFS_AND_ARGS_FRAME_REGISTERS_ONLY
    // Save the hidden arg as method pointer, r0 in the padding.
    // (x0 is an arg in native ABI but not considered an arg in managed ABI.)
    strd   r4, r0, [sp]

    // Call artCriticalNativeOutArgsSize(method)
    mov    r0, r4  // r0 := method (from hidden arg)
    bl     artCriticalNativeOutArgsSize

    // Check if we have any stack args.
    cbnz   r0, .Lcritical_has_stack_args

    // Without stack args, the frame is fully constructed.
    // Place tagged managed sp in Thread::Current()->top_quick_frame.
    mov    ip, sp
    orr    ip, #1  // Tag as GenericJNI frame.
    str    ip, [rSELF, #THREAD_TOP_QUICK_FRAME_OFFSET]

    // Call artFindNativeMethodRunnable()
    mov    r0, rSELF   // pass Thread::Current()
    bl     artFindNativeMethodRunnable

    // Store result in scratch reg.
    mov    ip, r0

    // Restore frame.
    .cfi_remember_state
    ldrd   r4, r0, [sp]
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    REFRESH_MARKING_REGISTER

    // Check for exception.
    cmp    ip, #0
    beq    .Lcritical_deliver_exception

    // Do the tail call.
    bx     ip
    .cfi_restore_state
    .cfi_def_cfa_offset FRAME_SIZE_SAVE_REFS_AND_ARGS

.Lcritical_has_stack_args:
    // Move the out args size to a scratch register.
    mov    ip, r0

    // Restore register args as we're about to move stack args.
    ldrd   r4, r0, [sp]
    RESTORE_SAVE_REFS_AND_ARGS_FRAME

    // Reserve space for SaveRefsAndArgs frame.
    sub sp, #FRAME_SIZE_SAVE_REFS_AND_ARGS
    .cfi_adjust_cfa_offset FRAME_SIZE_SAVE_REFS_AND_ARGS

    // Save arg regs so that we can use them as temporaries.
    push   {r0-r3}
    .cfi_adjust_cfa_offset 16

    // Move out args. For simplicity include the return address at the end.
    add    r0, sp, #16   // Destination.
    add    ip, r0, ip    // Destination end.
1:
    ldrd   r2, r3, [r0, #FRAME_SIZE_SAVE_REFS_AND_ARGS]
    strd   r2, r3, [r0], #8
    cmp    r0, ip
    bne    1b

    // Save our LR, load caller's LR and redefine CFI to take ownership of the JNI stub frame.
    str    lr, [ip, #-__SIZEOF_POINTER__]
    mov    lr, r3  // The last moved value from the loop above.
    .cfi_def_cfa ip, FRAME_SIZE_SAVE_REFS_AND_ARGS

    // Restore arg regs.
    pop    {r0-r3}  // No `.cfi_adjust_cfa_offset`, CFA register is currently ip, not sp.

    // Re-create the SaveRefsAndArgs frame above the args.
    strd   r4, r0, [ip]  // r0 in the padding as before.
    add    r4, ip, FRAME_SIZE_SAVE_REFS_AND_ARGS - 40
    stmia  r4, {r1-r3, r5-r8, r10-r11, lr}   @ 10 words of callee saves and args.
    .cfi_rel_offset r1, FRAME_SIZE_SAVE_REFS_AND_ARGS - 40 + 0
    .cfi_rel_offset r2, FRAME_SIZE_SAVE_REFS_AND_ARGS - 40 + 4
    .cfi_rel_offset r3, FRAME_SIZE_SAVE_REFS_AND_ARGS - 40 + 8
    .cfi_rel_offset r5, FRAME_SIZE_SAVE_REFS_AND_ARGS - 40 + 12
    .cfi_rel_offset r6, FRAME_SIZE_SAVE_REFS_AND_ARGS - 40 + 16
    .cfi_rel_offset r7, FRAME_SIZE_SAVE_REFS_AND_ARGS - 40 + 20
    .cfi_rel_offset r8, FRAME_SIZE_SAVE_REFS_AND_ARGS - 40 + 24
    .cfi_rel_offset r10, FRAME_SIZE_SAVE_REFS_AND_ARGS - 40 + 28
    .cfi_rel_offset r11, FRAME_SIZE_SAVE_REFS_AND_ARGS - 40 + 32
    .cfi_rel_offset lr, FRAME_SIZE_SAVE_REFS_AND_ARGS - 40 + 36
    vstmdb r4!, {s0-s15}                     @ 16 words of float args.

    // Move the frame register to a callee-save register.
    mov    r11, ip
    .cfi_def_cfa_register r11

    // Place tagged managed sp in Thread::Current()->top_quick_frame.
    orr    ip, r11, #1  // Tag as GenericJNI frame.
    str    ip, [rSELF, #THREAD_TOP_QUICK_FRAME_OFFSET]

    // Call artFindNativeMethodRunnable()
    mov    r0, rSELF   // pass Thread::Current()
    bl     artFindNativeMethodRunnable

    // Store result in scratch reg.
    mov    ip, r0

    // Restore the frame. We shall not need the method anymore, so use r4 as scratch register.
    mov    r4, r11
    .cfi_def_cfa_register r4
    ldr    r0, [r4, #4]
    add    r11, r4, #(FRAME_SIZE_SAVE_REFS_AND_ARGS - 40 - 64)
    vldmia r11!, {s0-s15}                    @ 16 words of float args.
    ldmia  r11, {r1-r3, r5-r8, r10-r11, lr}  @ 10 words of callee saves and args.
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
    REFRESH_MARKING_REGISTER

    // Check for exception.
    cmp    ip, #0
    beq    3f

    // Save arg regs so that we can use them as temporaries.
    push   {r0-r3}  // No `.cfi_adjust_cfa_offset`, CFA register is currently r4, not sp.

    // Move stack args to their original place.
    mov    r0, r4
    add    r1, sp, #16
2:
    ldrd   r2, r3, [r0, #-8]!
    strd   r2, r3, [r0, #FRAME_SIZE_SAVE_REFS_AND_ARGS]
    cmp    r1, r0
    bne    2b

    // Replace original return address with caller's return address.
    ldr    r1, [r4, #(FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__)]
    str    lr, [r4, #(FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__)]

    // Restore LR and redefine CFI to release ownership of the JNI stub frame.
    .cfi_remember_state
    mov    lr, r1
    .cfi_def_cfa sp, FRAME_SIZE_SAVE_REFS_AND_ARGS + 16

    // Restore args
    pop    {r0-r3}
    .cfi_adjust_cfa_offset -16

    // Remove the frame reservation.
    add    sp, #FRAME_SIZE_SAVE_REFS_AND_ARGS
    .cfi_adjust_cfa_offset -FRAME_SIZE_SAVE_REFS_AND_ARGS

    // Do the tail call.
    bx     ip
    .cfi_restore_state
    .cfi_def_cfa x4, FRAME_SIZE_SAVE_REFS_AND_ARGS

3:
    // Drop stack args and the SaveRefsAndArgs reservation.
    mov    sp, r4
    add    sp, #FRAME_SIZE_SAVE_REFS_AND_ARGS
    .cfi_def_cfa sp, 0

.Lcritical_deliver_exception:
    // When delivering exception, we check that rSELF was saved but the SaveRefsAndArgs frame does
    // not save it, so we cannot use DELIVER_PENDING_EXCEPTION_FRAME_READY with the above frames.
    DELIVER_PENDING_EXCEPTION
END art_jni_dlsym_lookup_critical_stub
