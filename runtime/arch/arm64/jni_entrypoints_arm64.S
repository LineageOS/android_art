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
  // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable for @FastNative.
  ldr   xIP0, [x0, #THREAD_TOP_QUICK_FRAME_OFFSET]      // uintptr_t tagged_quick_frame
  bic   xIP0, xIP0, #1                                  // ArtMethod** sp
  ldr   xIP0, [xIP0]                                    // ArtMethod* method
  ldr   xIP0, [xIP0, #ART_METHOD_ACCESS_FLAGS_OFFSET]   // uint32_t access_flags
  tst   xIP0, #ACCESS_FLAGS_METHOD_IS_FAST_NATIVE
  b.ne  .Llookup_stub_fast_native
  bl    artFindNativeMethod
  b     .Llookup_stub_continue
.Llookup_stub_fast_native:
  bl    artFindNativeMethodRunnable
.Llookup_stub_continue:
  mov  x17, x0    // store result in scratch reg.

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
