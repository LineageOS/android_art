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
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable for @FastNative.
    ldr    ip, [r0, #THREAD_TOP_QUICK_FRAME_OFFSET]   // uintptr_t tagged_quick_frame
    bic    ip, #1                                     // ArtMethod** sp
    ldr    ip, [ip]                                   // ArtMethod* method
    ldr    ip, [ip, #ART_METHOD_ACCESS_FLAGS_OFFSET]  // uint32_t access_flags
    tst    ip, #ACCESS_FLAGS_METHOD_IS_FAST_NATIVE
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
