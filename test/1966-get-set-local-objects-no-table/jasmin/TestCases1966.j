; Copyright (C) 2019 The Android Open Source Project
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

.class public art_test/TestCases1966
.super java/lang/Object
.inner class public static TestClass1 inner art/Test1966$TestClass1 outer art/Test1966
.inner interface public static abstract TestInterface inner art/Test1966$TestInterface outer art/Test1966

.method public <init>()V
  .limit stack 1
  .limit locals 1
  0: aload_0
  1: invokespecial java/lang/Object/<init>()V
  4: return
.end method

.method public static PrimitiveMethod(Ljava/util/function/IntConsumer;)V
  .limit stack 2
  .limit locals 2
  0: bipush 42
  2: istore_1
  3: aload_0
  4: sipush 1
  7: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  12: iload_1
  13: invokestatic java/lang/Integer/valueOf(I)Ljava/lang/Integer;
  16: invokestatic art/Test1966/reportValue(Ljava/lang/Object;)V
  19: return
.end method

.method public static CastInterfaceMethod(Ljava/util/function/IntConsumer;)V
  .limit stack 2
  .limit locals 3
  0: ldc "ObjectMethod"
  2: invokestatic art/Test1966$TestClass1/create(Ljava/lang/String;)Ljava/lang/Object;
  5: astore_1
  6: aload_1
  7: checkcast art/Test1966$TestClass1
  10: astore_2
  11: aload_0
  12: sipush 2
  15: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  20: aload_2
  21: invokestatic art/Test1966/reportValue(Ljava/lang/Object;)V
  24: return
.end method

.method public static CastExactMethod(Ljava/util/function/IntConsumer;)V
  .limit stack 2
  .limit locals 3
  0: ldc "ObjectMethod"
  2: invokestatic art/Test1966$TestClass1/create(Ljava/lang/String;)Ljava/lang/Object;
  5: astore_1
  6: aload_1
  7: checkcast art/Test1966$TestClass1
  10: astore_2
  11: aload_0
  12: sipush 2
  15: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  20: aload_2
  21: invokestatic art/Test1966/reportValue(Ljava/lang/Object;)V
  24: return
.end method

.method public static ObjectMethod(Ljava/util/function/IntConsumer;)V
  .limit stack 2
  .limit locals 2
  0: ldc "ObjectMethod"
  2: invokestatic art/Test1966$TestClass1/create(Ljava/lang/String;)Ljava/lang/Object;
  5: astore_1
  6: aload_0
  7: sipush 1
  10: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  15: aload_1
  16: invokestatic art/Test1966/reportValue(Ljava/lang/Object;)V
  19: return
.end method

.method public static InterfaceMethod(Ljava/util/function/IntConsumer;)V
  .limit stack 2
  .limit locals 2
  0: ldc "InterfaceMethod"
  2: invokestatic art/Test1966$TestClass1/createInterface(Ljava/lang/String;)Lart/Test1966$TestInterface;
  5: astore_1
  6: aload_0
  7: sipush 1
  10: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  15: aload_1
  16: invokestatic art/Test1966/reportValue(Ljava/lang/Object;)V
  19: return
.end method

.method public static ExactClassMethod(Ljava/util/function/IntConsumer;)V
  .limit stack 2
  .limit locals 2
  0: ldc "SpecificClassMethod"
  2: invokestatic art/Test1966$TestClass1/createExact(Ljava/lang/String;)Lart/Test1966$TestClass1;
  5: astore_1
  6: aload_0
  7: sipush 1
  10: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  15: aload_1
  16: invokestatic art/Test1966/reportValue(Ljava/lang/Object;)V
  19: return
.end method

.method public static CastExactNullMethod(Ljava/util/function/IntConsumer;)V
  .limit stack 2
  .limit locals 3
  0: aconst_null
  1: astore_1
  2: aload_1
  3: checkcast art/Test1966$TestClass1
  6: astore_2
  7: aload_0
  8: sipush 2
  11: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  16: aload_2
  17: invokestatic art/Test1966/reportValue(Ljava/lang/Object;)V
  20: return
.end method

.method public static CastInterfaceNullMethod(Ljava/util/function/IntConsumer;)V
  .limit stack 2
  .limit locals 3
  0: aconst_null
  1: astore_1
  2: aload_1
  3: checkcast art/Test1966$TestInterface
  6: astore_2
  7: aload_0
  8: sipush 2
  11: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  16: aload_2
  17: invokestatic art/Test1966/reportValue(Ljava/lang/Object;)V
  20: return
.end method

.method public static NullMethod(Ljava/util/function/IntConsumer;)V
  .limit stack 2
  .limit locals 2
  0: aconst_null
  1: astore_1
  2: aload_0
  3: sipush 1
  6: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  11: aload_1
  12: invokestatic art/Test1966/reportValue(Ljava/lang/Object;)V
  15: return
.end method
