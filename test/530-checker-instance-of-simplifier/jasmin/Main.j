; Copyright (C) 2021 The Android Open Source Project
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
;      http://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.

.class public Main
.super java/lang/Object

; Just do simple check that we remove the instance-of. Well formedness
; checks will be done in gtests.
;; CHECK-START: int Main.$noinline$test(boolean) instruction_simplifier (before)
;; CHECK-DAG: LoadClass
;; CHECK-DAG: LoadClass
;; CHECK-DAG: InstanceOf
;
;; CHECK-START: int Main.$noinline$test(boolean) instruction_simplifier (after)
;; CHECK-DAG: LoadClass
;; CHECK-DAG: LoadClass
;
;; CHECK-START: int Main.$noinline$test(boolean) instruction_simplifier (after)
;; CHECK-NOT: InstanceOf

; public static int $noinline$test(boolean escape) {
;   Foo f = new Foo();
;   f.intField = 7
;   if (escape) {
;     if (f instanceof Bar) {
;       $noinline$escape(f);
;     }
;   }
;   return f.intField;
; }
.method public static $noinline$test(Z)I
  .limit stack 3
  new Foo
  ; Stack: [f]
  dup
  ; Stack: [f, f]
  invokespecial Foo/<init>()V
  ; Stack: [f]
  dup
  ; Stack: [f, f]
  ldc 7
  ; Stack: [f, f, 7]
  putfield Foo/intField I
  ; Stack: [f]
  iload_0
  ; Stack: [f, escape]
  ifeq finish
  ; Stack: [f]
  dup
  ; Stack: [f, f]
  ; NB Baz does not exist
  instanceof Baz
  ; Stack: [f, is_instance]
  ifeq finish
  ; Stack: [f]
  dup
  ; Stack: [f, f]
  invokestatic Main/$noinline$escape(Ljava/lang/Object;)V
  ; Stack: [f]
finish:   ; Stack: [f]
  getfield Foo/intField I
  ; Stack: [f.intField]
  ireturn
.end method

.method public static $noinline$escape(Ljava/lang/Object;)V
  .limit stack 0
  return
.end method

; public static void main(String[] args) {
;   PrintStream out = System.out;
;   int i = $noinline$test(false);
;   if (i != 7) {
;     out.print("FAIL! GOT ");
;     out.println(i);
;   }
; }
.method public static main([Ljava/lang/String;)V
  .limit stack 5
  ; Stack: []
  ; locals: [args]
  getstatic java/lang/System/out Ljava/io/PrintStream;
  ; Stack: [out]
  ; locals: [args]
  astore_0
  ; Stack: []
  ; locals: [out]
  bipush 0
  ; Stack: [0]
  ; locals: [out]
  invokestatic Main/$noinline$test(Z)I
  ; Stack: [res]
  ; locals: [out]
  dup
  ; Stack: [res, res]
  ; locals: [out]
  bipush 7
  ; Stack: [res, res, 7]
  ; locals: [out]
  if_icmpeq finish
  ; Stack: [res]
  ; locals: [out]
  aload_0
  ; Stack: [res, out]
  ; locals: [out]
  dup2
  ; Stack: [res, out, res, out]
  ; locals: [out]
  ldc "FAIL! GOT "
  ; Stack: [res, out, res, out, "FAIL! GOT "]
  ; locals: [out]
  invokevirtual java/io/PrintStream/print(Ljava/lang/String;)V
  ; Stack: [res, out, res]
  ; locals: [out]
  invokevirtual java/io/PrintStream/println(I)V
  ; Stack: [res]
  ; locals: [out]
finish:
  ; Stack: [res]
  ; locals: [out]
  return
.end method
