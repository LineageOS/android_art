# Copyright (C) 2019 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

.class public final Lart_test/TestCases1965;
.super Ljava/lang/Object;


# direct methods
.method public constructor <init>()V
    .registers 1
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    return-void
.end method

.method public static BooleanMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
    .registers 4
    const/4 v0, 0x0
    # Slot for value.
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Ljava/lang/Boolean;->valueOf(Z)Ljava/lang/Boolean;
    move-result-object v1
    invoke-interface {p1, v1}, Ljava/util/function/Consumer;->accept(Ljava/lang/Object;)V
    return-void
.end method

.method public static ByteMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
    .registers 4
    const/16 v0, 0x8
    # Slot for value.
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Ljava/lang/Byte;->valueOf(B)Ljava/lang/Byte;
    move-result-object v1
    invoke-interface {p1, v1}, Ljava/util/function/Consumer;->accept(Ljava/lang/Object;)V
    return-void
.end method

.method public static CharMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
    .registers 4
    const/16 v0, 0x71
    # Slot for value
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Ljava/lang/Character;->valueOf(C)Ljava/lang/Character;
    move-result-object v1
    invoke-interface {p1, v1}, Ljava/util/function/Consumer;->accept(Ljava/lang/Object;)V
    return-void
.end method

.method public static DoubleMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
    .registers 5
    const-wide v0, 0x400921cac083126fL    # 3.1415
    # Slot for value
    const/16 v2, 0x0
    invoke-interface {p0, v2}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0, v1}, Ljava/lang/Double;->valueOf(D)Ljava/lang/Double;
    move-result-object v2
    invoke-interface {p1, v2}, Ljava/util/function/Consumer;->accept(Ljava/lang/Object;)V
    return-void
.end method

.method public static FloatMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
    .registers 4
    const v0, 0x3fcf1aa0    # 1.618f
    # Slot for value
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Ljava/lang/Float;->valueOf(F)Ljava/lang/Float;
    move-result-object v1
    invoke-interface {p1, v1}, Ljava/util/function/Consumer;->accept(Ljava/lang/Object;)V
    return-void
.end method

.method public static IntMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
    .registers 4
    const/16 v0, 0x2a
    # Slot for value
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Ljava/lang/Integer;->valueOf(I)Ljava/lang/Integer;
    move-result-object v1
    invoke-interface {p1, v1}, Ljava/util/function/Consumer;->accept(Ljava/lang/Object;)V
    return-void
.end method

.method public static LongMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
    .registers 5
    const-wide/16 v0, 0x2329
    # Slot for value
    const/16 v2, 0x0
    invoke-interface {p0, v2}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0, v1}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;
    move-result-object v2
    invoke-interface {p1, v2}, Ljava/util/function/Consumer;->accept(Ljava/lang/Object;)V
    return-void
.end method

.method public static NullObjectMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
    .registers 4
    const/4 v0, 0x0
    # Slot for value
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-interface {p1, v0}, Ljava/util/function/Consumer;->accept(Ljava/lang/Object;)V
    return-void
.end method

.method public static ObjectMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
    .registers 4
    const-string v0, "TARGET_VALUE"
    # Slot for value
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-interface {p1, v0}, Ljava/util/function/Consumer;->accept(Ljava/lang/Object;)V
    return-void
.end method

.method public static ShortMethod(Ljava/util/function/IntConsumer;Ljava/util/function/Consumer;)V
    .registers 4
    const/16 v0, 0x141
    # slot for value
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Ljava/lang/Short;->valueOf(S)Ljava/lang/Short;
    move-result-object v1
    invoke-interface {p1, v1}, Ljava/util/function/Consumer;->accept(Ljava/lang/Object;)V
    return-void
.end method
