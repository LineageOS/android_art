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

.class public final art_test/TestCases1965
.super java/lang/Object

.method public <init>()V
  .limit stack 1
  .limit locals 1
  0: aload_0
  1: invokespecial java/lang/Object/<init>()V
  4: return
.end method

; NB We limit locals 4 so that every method has space to fit a long/double in it.
.method public static NullObjectMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
  .limit stack 2
  .limit locals 4
  0: aconst_null
  1: astore_2
  2: aload_0
  3: sipush 2
  6: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  11: aload_1
  12: aload_2
  13: invokeinterface java/util/function/Consumer/accept(Ljava/lang/Object;)V 2
  18: return
.end method

.method public static ObjectMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
  .limit stack 2
  .limit locals 4
  0: ldc "TARGET_VALUE"
  2: astore_2
  3: aload_0
  4: sipush 2
  7: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  12: aload_1
  13: aload_2
  14: invokeinterface java/util/function/Consumer/accept(Ljava/lang/Object;)V 2
  19: return
.end method

.method public static BooleanMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
  .limit stack 2
  .limit locals 4
  0: iconst_0
  1: istore_2
  2: aload_0
  3: sipush 2
  6: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  11: aload_1
  12: iload_2
  13: invokestatic java/lang/Boolean/valueOf(Z)Ljava/lang/Boolean;
  16: invokeinterface java/util/function/Consumer/accept(Ljava/lang/Object;)V 2
  21: return
.end method

.method public static ByteMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
  .limit stack 2
  .limit locals 4
  0: bipush 8
  2: istore_2
  3: aload_0
  4: sipush 2
  7: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  12: aload_1
  13: iload_2
  14: invokestatic java/lang/Byte/valueOf(B)Ljava/lang/Byte;
  17: invokeinterface java/util/function/Consumer/accept(Ljava/lang/Object;)V 2
  22: return
.end method

.method public static CharMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
  .limit stack 2
  .limit locals 4
  0: bipush 113
  2: istore_2
  3: aload_0
  4: sipush 2
  7: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  12: aload_1
  13: iload_2
  14: invokestatic java/lang/Character/valueOf(C)Ljava/lang/Character;
  17: invokeinterface java/util/function/Consumer/accept(Ljava/lang/Object;)V 2
  22: return
.end method

.method public static ShortMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
  .limit stack 2
  .limit locals 4
  0: sipush 321
  3: istore_2
  4: aload_0
  5: sipush 2
  8: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  13: aload_1
  14: iload_2
  15: invokestatic java/lang/Short/valueOf(S)Ljava/lang/Short;
  18: invokeinterface java/util/function/Consumer/accept(Ljava/lang/Object;)V 2
  23: return
.end method

.method public static IntMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
  .limit stack 2
  .limit locals 4
  0: bipush 42
  2: istore_2
  3: aload_0
  4: sipush 2
  7: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  12: aload_1
  13: iload_2
  14: invokestatic java/lang/Integer/valueOf(I)Ljava/lang/Integer;
  17: invokeinterface java/util/function/Consumer/accept(Ljava/lang/Object;)V 2
  22: return
.end method

.method public static LongMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
  .limit stack 3
  .limit locals 4
  0: ldc2_w 9001
  3: lstore_2
  4: aload_0
  5: sipush 2
  8: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  13: aload_1
  14: lload_2
  15: invokestatic java/lang/Long/valueOf(J)Ljava/lang/Long;
  18: invokeinterface java/util/function/Consumer/accept(Ljava/lang/Object;)V 2
  23: return
.end method

.method public static FloatMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
  .limit stack 2
  .limit locals 4
  0: ldc 1.618
  2: fstore_2
  3: aload_0
  4: sipush 2
  7: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  12: aload_1
  13: fload_2
  14: invokestatic java/lang/Float/valueOf(F)Ljava/lang/Float;
  17: invokeinterface java/util/function/Consumer/accept(Ljava/lang/Object;)V 2
  22: return
.end method

.method public static DoubleMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
  .limit stack 3
  .limit locals 4
  0: ldc2_w 3.1415
  3: dstore_2
  4: aload_0
  5: sipush 2
  8: invokeinterface java/util/function/IntConsumer/accept(I)V 2
  13: aload_1
  14: dload_2
  15: invokestatic java/lang/Double/valueOf(D)Ljava/lang/Double;
  18: invokeinterface java/util/function/Consumer/accept(Ljava/lang/Object;)V 2
  23: return
.end method
