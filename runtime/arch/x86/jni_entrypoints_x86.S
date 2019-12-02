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
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable for @FastNative.
    movl (%esp), %eax                                // Thread* self
    movl THREAD_TOP_QUICK_FRAME_OFFSET(%eax), %eax   // uintptr_t tagged_quick_frame
    andl LITERAL(0xfffffffe), %eax                   // ArtMethod** sp
    movl (%eax), %eax                                // ArtMethod* method
    testl LITERAL(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE), ART_METHOD_ACCESS_FLAGS_OFFSET(%eax)
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
