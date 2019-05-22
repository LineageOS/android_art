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


.class public Lart_test/TestCases1966;
.super Ljava/lang/Object;

# direct methods
.method public constructor <init>()V
    .registers 1
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    return-void
.end method

.method public static CastExactMethod(Ljava/util/function/IntConsumer;)V
    .registers 3
    const-string v0, "ObjectMethod"
    invoke-static {v0}, Lart/Test1966$TestClass1;->create(Ljava/lang/String;)Ljava/lang/Object;
    move-result-object v0
    check-cast v0, Lart/Test1966$TestClass1;
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Lart/Test1966;->reportValue(Ljava/lang/Object;)V
    return-void
.end method

.method public static CastInterfaceMethod(Ljava/util/function/IntConsumer;)V
    .registers 3
    const-string v0, "ObjectMethod"
    invoke-static {v0}, Lart/Test1966$TestClass1;->create(Ljava/lang/String;)Ljava/lang/Object;
    move-result-object v0
    check-cast v0, Lart/Test1966$TestClass1;
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Lart/Test1966;->reportValue(Ljava/lang/Object;)V
    return-void
.end method

.method public static ExactClassMethod(Ljava/util/function/IntConsumer;)V
    .registers 3
    const-string v0, "SpecificClassMethod"
    invoke-static {v0}, Lart/Test1966$TestClass1;->createExact(Ljava/lang/String;)Lart/Test1966$TestClass1;
    move-result-object v0
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Lart/Test1966;->reportValue(Ljava/lang/Object;)V
    return-void
.end method

.method public static InterfaceMethod(Ljava/util/function/IntConsumer;)V
    .registers 3
    const-string v0, "InterfaceMethod"
    invoke-static {v0}, Lart/Test1966$TestClass1;->createInterface(Ljava/lang/String;)Lart/Test1966$TestInterface;
    move-result-object v0
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Lart/Test1966;->reportValue(Ljava/lang/Object;)V
    return-void
.end method

.method public static ObjectMethod(Ljava/util/function/IntConsumer;)V
    .registers 3
    const-string v0, "ObjectMethod"
    invoke-static {v0}, Lart/Test1966$TestClass1;->create(Ljava/lang/String;)Ljava/lang/Object;
    move-result-object v0
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Lart/Test1966;->reportValue(Ljava/lang/Object;)V
    return-void
.end method

.method public static PrimitiveMethod(Ljava/util/function/IntConsumer;)V
    .registers 3
    const/16 v0, 0x2a
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Ljava/lang/Integer;->valueOf(I)Ljava/lang/Integer;
    move-result-object p0
    invoke-static {p0}, Lart/Test1966;->reportValue(Ljava/lang/Object;)V
    return-void
.end method

.method public static NullMethod(Ljava/util/function/IntConsumer;)V
    .registers 3
    const/4 v0, 0x0
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Lart/Test1966;->reportValue(Ljava/lang/Object;)V
    return-void
.end method

.method public static CastInterfaceNullMethod(Ljava/util/function/IntConsumer;)V
    .registers 3
    const/4 v0, 0x0
    check-cast v0, Lart/Test1966$TestInterface;
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Lart/Test1966;->reportValue(Ljava/lang/Object;)V
    return-void
.end method

.method public static CastExactNullMethod(Ljava/util/function/IntConsumer;)V
    .registers 3
    const/4 v0, 0x0
    check-cast v0, Lart/Test1966$TestClass1;
    const/16 v1, 0x0
    invoke-interface {p0, v1}, Ljava/util/function/IntConsumer;->accept(I)V
    invoke-static {v0}, Lart/Test1966;->reportValue(Ljava/lang/Object;)V
    return-void
.end method