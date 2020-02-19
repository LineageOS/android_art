/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "asm_support_arm64.S"

    /*
     * Jni dlsym lookup stub.
     */
    .extern artFindNativeMethod
    .extern artFindNativeMethodRunnable

ENTRY art_jni_dlsym_lookup_stub
    // spill regs.
    stp   x29, x30, [sp, #-16]!
    .cfi_adjust_cfa_offset 16
    .cfi_rel_offset x29, 0
    .cfi_rel_offset x30, 8
    mov   x29, sp
    stp   d6, d7,   [sp, #-16]!
    .cfi_adjust_cfa_offset 16
    stp   d4, d5,   [sp, #-16]!
    .cfi_adjust_cfa_offset 16
    stp   d2, d3,   [sp, #-16]!
    .cfi_adjust_cfa_offset 16
    stp   d0, d1,   [sp, #-16]!
    .cfi_adjust_cfa_offset 16
    stp   x6, x7,   [sp, #-16]!
    .cfi_adjust_cfa_offset 16
    stp   x4, x5,   [sp, #-16]!
    .cfi_adjust_cfa_offset 16
    stp   x2, x3,   [sp, #-16]!
    .cfi_adjust_cfa_offset 16
    stp   x0, x1,   [sp, #-16]!
    .cfi_adjust_cfa_offset 16

    mov x0, xSELF   // pass Thread::Current()
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable()
    // for @FastNative or @CriticalNative.
    ldr   xIP0, [x0, #THREAD_TOP_QUICK_FRAME_OFFSET]      // uintptr_t tagged_quick_frame
    bic   xIP0, xIP0, #1                                  // ArtMethod** sp
    ldr   xIP0, [xIP0]                                    // ArtMethod* method
    ldr   xIP0, [xIP0, #ART_METHOD_ACCESS_FLAGS_OFFSET]   // uint32_t access_flags
    mov   xIP1, #(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE)
    tst   xIP0, xIP1
    b.ne  .Llookup_stub_fast_native
    bl    artFindNativeMethod
    b     .Llookup_stub_continue
    .Llookup_stub_fast_native:
    bl    artFindNativeMethodRunnable
.Llookup_stub_continue:
    mov   x17, x0    // store result in scratch reg.

    // load spill regs.
    ldp   x0, x1,   [sp], #16
    .cfi_adjust_cfa_offset -16
    ldp   x2, x3,   [sp], #16
    .cfi_adjust_cfa_offset -16
    ldp   x4, x5,   [sp], #16
    .cfi_adjust_cfa_offset -16
    ldp   x6, x7,   [sp], #16
    .cfi_adjust_cfa_offset -16
    ldp   d0, d1,   [sp], #16
    .cfi_adjust_cfa_offset -16
    ldp   d2, d3,   [sp], #16
    .cfi_adjust_cfa_offset -16
    ldp   d4, d5,   [sp], #16
    .cfi_adjust_cfa_offset -16
    ldp   d6, d7,   [sp], #16
    .cfi_adjust_cfa_offset -16
    ldp   x29, x30, [sp], #16
    .cfi_adjust_cfa_offset -16
    .cfi_restore x29
    .cfi_restore x30

    cbz   x17, 1f   // is method code null ?
    br    x17       // if non-null, tail call to method's code.

1:
    ret             // restore regs and return to caller to handle exception.
END art_jni_dlsym_lookup_stub

ENTRY art_jni_dlsym_lookup_critical_stub
    // The hidden arg holding the tagged method (bit 0 set means GenericJNI) is x15.
    // For Generic JNI we already have a managed frame, so we reuse the art_jni_dlsym_lookup_stub.
    tbnz  x15, #0, art_jni_dlsym_lookup_stub

    // We need to create a GenericJNI managed frame above the stack args.

    // GenericJNI frame is similar to SaveRegsAndArgs frame with the native method
    // instead of runtime method saved at the bottom. Note that the runtime shall
    // not examine the args here, otherwise we would have to move them in registers
    // and stack to account for the difference between managed and native ABIs.
    INCREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS
    SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL sp
    // Save the hidden arg as method pointer, x0 in the padding.
    // (x0 is an arg in native ABI but not considered an arg in managed ABI.)
    SAVE_TWO_REGS x15, x0, 0

    // Call artCriticalNativeOutArgsSize(method)
    mov   x0, x15  // x0 := method (from hidden arg)
    bl    artCriticalNativeOutArgsSize

    // Check if we have any stack args.
    cbnz  x0, .Lcritical_has_stack_args

    // Without stack args, the frame is fully constructed.
    // Place tagged managed sp in Thread::Current()->top_quick_frame.
    mov   xIP0, sp
    orr   xIP0, xIP0, #1  // Tag as GenericJNI frame.
    str   xIP0, [xSELF, #THREAD_TOP_QUICK_FRAME_OFFSET]

    // Call artFindNativeMethodRunnable()
    mov   x0, xSELF   // pass Thread::Current()
    bl    artFindNativeMethodRunnable

    // Store result in scratch reg.
    mov   xIP0, x0

    // Restore frame.
    .cfi_remember_state
    RESTORE_TWO_REGS x15, x0, 0
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    REFRESH_MARKING_REGISTER

    // Check for exception.
    cbz   xIP0, .Lcritical_deliver_exception

    // Do the tail call
    br    xIP0
    .cfi_restore_state
    .cfi_def_cfa_offset FRAME_SIZE_SAVE_REFS_AND_ARGS

.Lcritical_has_stack_args:
    // Move the out args size to a scratch register.
    mov   xIP0, x0

    // Restore register args as we're about to move stack args.
    RESTORE_TWO_REGS x15, x0, 0
    RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL sp

    // Move out args. For simplicity include the return address at the end.
    mov   x8, sp        // Destination.
    add   x9, sp, xIP0  // Destination end.
1:
    ldp   x10, x11, [x8, #FRAME_SIZE_SAVE_REFS_AND_ARGS]
    stp   x10, x11, [x8], #16
    cmp   x8, x9
    bne   1b

    // Save our LR, load caller's LR and redefine CFI to take ownership of the JNI stub frame.
    str   xLR, [x9, #-__SIZEOF_POINTER__]
    mov   xLR, x11  // The last moved value from the loop above.
    .cfi_def_cfa x9, FRAME_SIZE_SAVE_REFS_AND_ARGS

    // Re-create the SaveRefsAndArgs frame above the args.
    SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL x9
    SAVE_TWO_REGS_BASE x9, x15, x0, 0

    // Move the frame register to a callee-save register.
    mov   x29, x9
    .cfi_def_cfa_register x29

    // Place tagged managed sp in Thread::Current()->top_quick_frame.
    orr   xIP0, x29, #1  // Tag as GenericJNI frame.
    str   xIP0, [xSELF, #THREAD_TOP_QUICK_FRAME_OFFSET]

    // Call artFindNativeMethodRunnable()
    mov   x0, xSELF   // pass Thread::Current()
    bl    artFindNativeMethodRunnable

    // Store result in scratch reg.
    mov   xIP0, x0

    // Restore the frame.
    mov   x9, x29
    .cfi_def_cfa_register x9
    RESTORE_TWO_REGS_BASE x9, x15, x0, 0
    RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL x9
    REFRESH_MARKING_REGISTER

    // Check for exception.
    cbz   xIP0, 3f

    // Move stack args to their original place.
    mov   x8, x9
2:
    ldp   x10, x11, [x8, #-16]!
    stp   x10, x11, [x8, #FRAME_SIZE_SAVE_REFS_AND_ARGS]
    cmp   sp, x8
    bne   2b

    // Replace original return address with caller's return address.
    ldr   xIP1, [x9, #(FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__)]
    str   xLR, [x9, #(FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__)]

    // Restore LR and redefine CFI to release ownership of the JNI stub frame.
    .cfi_remember_state
    mov   xLR, xIP1
    .cfi_def_cfa sp, FRAME_SIZE_SAVE_REFS_AND_ARGS

    // Remove the frame reservation.
    DECREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS

    // Do the tail call.
    br    xIP0
    .cfi_restore_state
    .cfi_def_cfa x9, FRAME_SIZE_SAVE_REFS_AND_ARGS

3:
    // Drop stack args and the SaveRefsAndArgs reservation.
    mov   sp, x9
    add   sp, sp, #FRAME_SIZE_SAVE_REFS_AND_ARGS
    .cfi_def_cfa sp, 0

.Lcritical_deliver_exception:
    // When delivering exception, we check that xSELF was saved but the SaveRefsAndArgs frame does
    // not save it, so we cannot use DELIVER_PENDING_EXCEPTION_FRAME_READY with the above frames.
    DELIVER_PENDING_EXCEPTION
END art_jni_dlsym_lookup_critical_stub
