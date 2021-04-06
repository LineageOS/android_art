/*
 * Copyright (C) 2021 The Android Open Source Project
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

// Typically GCC outputs functions like these into any object file that needs a PIC base register,
// and one of the copies for each register is used. Clang doesn't use these functions, but
// SETUP_GOT_NOSAVE in asm_support_x86.S calls this one.

    .section .text.__x86.get_pc_thunk.bx,"axG",@progbits,__x86.get_pc_thunk.bx,comdat
    .globl __x86.get_pc_thunk.bx
    .hidden __x86.get_pc_thunk.bx
    .p2align 4
    .type __x86.get_pc_thunk.bx,@function
__x86.get_pc_thunk.bx:
    .cfi_startproc
    movl (%esp), %ebx
    ret
    .cfi_endproc
