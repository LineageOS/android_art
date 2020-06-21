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
    bne    .Llookup_stub_fast_or_critical_native
    blx    artFindNativeMethod
    b      .Llookup_stub_continue
.Llookup_stub_fast_or_critical_native:
    blx    artFindNativeMethodRunnable
.Llookup_stub_continue:
    mov    r12, r0                        @ save result in r12

    add    sp, #12                        @ restore stack pointer
    .cfi_adjust_cfa_offset -12
    cbz    r0, 1f                         @ is method code null?
    pop    {r0, r1, r2, r3, lr}           @ restore regs
    .cfi_adjust_cfa_offset -20
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

    // Reserve space for a SaveRefsAndArgs managed frame, either for the actual runtime
    // method or for a GenericJNI frame which is similar but has a native method and a tag.
    // Do this eagerly, so that we can use these registers as temps without the need to
    // save and restore them multiple times.
    INCREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS

    // Save args, the hidden arg and caller PC. No CFI needed for args and the hidden arg.
    push   {r0, r1, r2, r3, r4, lr}
    .cfi_adjust_cfa_offset 24
    .cfi_rel_offset lr, 20

    // Call artCriticalNativeFrameSize(method, caller_pc)
    mov    r0, r4  // r0 := method (from hidden arg)
    mov    r1, lr  // r1 := caller_pc
    bl     artCriticalNativeFrameSize

    // Prepare the return address for managed stack walk of the SaveRefsAndArgs frame.
    // If we're coming from JNI stub with tail call, it is LR. If we're coming from
    // JNI stub that saved the return address, it will be the last value we copy below.
    // If we're coming directly from compiled code, it is LR, set further down.
    ldr    lr, [sp, #20]

    // Move the stack args if any.
    add    r4, sp, #24
    cbz    r0, .Lcritical_skip_copy_args
.Lcritical_copy_args_loop:
    ldrd   ip, lr, [r4, #FRAME_SIZE_SAVE_REFS_AND_ARGS]
    subs   r0, r0, #8
    strd   ip, lr, [r4], #8
    bne    .Lcritical_copy_args_loop
.Lcritical_skip_copy_args:
    // The managed frame address is now in R4. This is conveniently a callee-save in native ABI.

    // Restore args.
    pop    {r0, r1, r2, r3}
    .cfi_adjust_cfa_offset -16

    // Spill registers for the SaveRefsAndArgs frame above the stack args.
    // Note that the runtime shall not examine the args here, otherwise we would have to
    // move them in registers and stack to account for the difference between managed and
    // native ABIs.
    add    ip, r4, #FRAME_SIZE_SAVE_REFS_AND_ARGS - 40
    stmia  ip, {r1-r3, r5-r8, r10-r11, lr}  // LR: Save return address for tail call from JNI stub.
    // (If there were any stack args, we're storing the value that's already there.
    // For direct calls from compiled managed code, we shall overwrite this below.)
    // Skip args r1-r3.
    CFI_EXPRESSION_BREG 5, 4, FRAME_SIZE_SAVE_REFS_AND_ARGS - 28
    CFI_EXPRESSION_BREG 6, 4, FRAME_SIZE_SAVE_REFS_AND_ARGS - 24
    CFI_EXPRESSION_BREG 7, 4, FRAME_SIZE_SAVE_REFS_AND_ARGS - 20
    CFI_EXPRESSION_BREG 8, 4, FRAME_SIZE_SAVE_REFS_AND_ARGS - 16
    CFI_EXPRESSION_BREG 10, 4, FRAME_SIZE_SAVE_REFS_AND_ARGS - 12
    CFI_EXPRESSION_BREG 11, 4, FRAME_SIZE_SAVE_REFS_AND_ARGS - 8
    // The saved return PC for managed stack walk is not necessarily our LR.
    // Skip managed FP args as these are native ABI caller-saves and not args.

    // Restore the hidden arg to r1 and caller PC.
    pop    {r1, lr}
    .cfi_adjust_cfa_offset -8
    .cfi_restore lr

    // Save our return PC in the padding.
    str   lr, [r4, #__SIZEOF_POINTER__]
    CFI_EXPRESSION_BREG 14, 4, __SIZEOF_POINTER__

    ldr    ip, [r1, #ART_METHOD_ACCESS_FLAGS_OFFSET]  // Load access flags.
    add    r2, r4, #1             // Prepare managed SP tagged for a GenericJNI frame.
    tst    ip, #ACCESS_FLAGS_METHOD_IS_NATIVE
    bne    .Lcritical_skip_prepare_runtime_method

    // When coming from a compiled method, the return PC for managed stack walk is LR.
    // (When coming from a compiled stub, the correct return PC is already stored above.)
    str    lr, [r4, #(FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__)]

    // Replace the target method with the SaveRefsAndArgs runtime method.
    RUNTIME_CURRENT1 r1
    ldr    r1, [r1, #RUNTIME_SAVE_REFS_AND_ARGS_METHOD_OFFSET]

    mov    r2, r4                 // Prepare untagged managed SP for the runtime method.

.Lcritical_skip_prepare_runtime_method:
    // Store the method on the bottom of the managed frame.
    str    r1, [r4]

    // Place (maybe tagged) managed SP in Thread::Current()->top_quick_frame.
    str    r2, [rSELF, #THREAD_TOP_QUICK_FRAME_OFFSET]

    // Preserve the native arg register r0 in callee-save register r10 which was saved above.
    mov    r10, r0

    // Call artFindNativeMethodRunnable()
    mov    r0, rSELF   // pass Thread::Current()
    bl     artFindNativeMethodRunnable

    // Store result in scratch reg.
    mov    ip, r0

    // Restore the native arg register r0.
    mov    r0, r10

    // Restore the frame. We shall not need the method anymore.
    add    r1, r4, #FRAME_SIZE_SAVE_REFS_AND_ARGS - 40
    ldmia  r1, {r1-r3, r5-r8, r10-r11}
    .cfi_restore r5
    .cfi_restore r6
    .cfi_restore r7
    .cfi_restore r8
    .cfi_restore r10
    .cfi_restore r11

    REFRESH_MARKING_REGISTER

    // Check for exception before moving args back to keep the return PC for managed stack walk.
    cmp    ip, #0
    beq    .Lcritical_deliver_exception

    .cfi_remember_state

    // Restore our return PC.
    ldr    lr, [r4, #__SIZEOF_POINTER__]
    .cfi_restore lr

    // Move stack args to their original place.
    cmp    sp, r4
    beq    .Lcritical_skip_copy_args_back
    push   {r0, r1, r2, r3}
    .cfi_adjust_cfa_offset 16
    add    r0, sp, #16
    sub    r0, r4, r0
.Lcritical_copy_args_loop_back:
    ldrd   r2, r3, [r4, #-8]!
    subs   r0, r0, #8
    strd   r2, r3, [r4, #FRAME_SIZE_SAVE_REFS_AND_ARGS]
    bne    .Lcritical_copy_args_loop_back
    pop    {r0, r1, r2, r3}
    .cfi_adjust_cfa_offset -16
.Lcritical_skip_copy_args_back:

    // Remove the frame reservation.
    DECREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS

    // Do the tail call.
    bx     ip
    .cfi_restore_state
    .cfi_def_cfa sp, FRAME_SIZE_SAVE_REFS_AND_ARGS

.Lcritical_deliver_exception:
    // The exception delivery checks that rSELF was saved but the SaveRefsAndArgs
    // frame does not save it, so we cannot use the existing SaveRefsAndArgs frame.
    // That's why we checked for exception after restoring registers from it.
    // We need to build a SaveAllCalleeSaves frame instead. Args are irrelevant at this
    // point but keep the area allocated for stack args to keep CFA definition simple.
#if FRAME_SIZE_SAVE_REFS_AND_ARGS != FRAME_SIZE_SAVE_ALL_CALLEE_SAVES
#  error "Expected FRAME_SIZE_SAVE_REFS_AND_ARGS == FRAME_SIZE_SAVE_ALL_CALLEE_SAVES"
    // Otherwise we would need to adjust SP and R4 and move our return PC which is at [R4, #4].
    // (Luckily, both SaveRefsAndArgs and SaveAllCalleeSaves frames have padding there.)
#endif

    // Spill registers for the SaveAllCalleeSaves frame above the stack args area.
    add    ip, r4, #FRAME_SIZE_SAVE_ALL_CALLEE_SAVES - 32
    stmia  ip, {r5-r11}  // Keep the caller PC for managed stack walk.
    CFI_EXPRESSION_BREG 5, 4, FRAME_SIZE_SAVE_ALL_CALLEE_SAVES - 32
    CFI_EXPRESSION_BREG 6, 4, FRAME_SIZE_SAVE_ALL_CALLEE_SAVES - 28
    CFI_EXPRESSION_BREG 7, 4, FRAME_SIZE_SAVE_ALL_CALLEE_SAVES - 24
    CFI_EXPRESSION_BREG 8, 4, FRAME_SIZE_SAVE_ALL_CALLEE_SAVES - 20
    CFI_EXPRESSION_BREG 9, 4, FRAME_SIZE_SAVE_ALL_CALLEE_SAVES - 16
    CFI_EXPRESSION_BREG 10, 4, FRAME_SIZE_SAVE_ALL_CALLEE_SAVES - 12
    CFI_EXPRESSION_BREG 11, 4, FRAME_SIZE_SAVE_ALL_CALLEE_SAVES - 8
    // Skip R4, it is callee-save in managed ABI.
    add    ip, r4, #12
    vstmia ip, {s16-s31}

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveAllCalleeSaves] to the managed frame.
    RUNTIME_CURRENT2 ip
    ldr   ip, [ip, #RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET]
    str   ip, [r4]

    // Place the managed frame SP in Thread::Current()->top_quick_frame.
    str   r4, [rSELF, #THREAD_TOP_QUICK_FRAME_OFFSET]

    DELIVER_PENDING_EXCEPTION_FRAME_READY
END art_jni_dlsym_lookup_critical_stub
