# Copyright (C) 2020 The Android Open Source Project
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


.class public Ljava/lang/Integer;
.super Ljava/lang/Number;

.field public static final BYTES:I

.method static constructor <clinit>()V
    .registers 0
    return-void
.end method

.method public constructor <init>(I)V
    .registers 2
    invoke-static {v0}, Ljava/lang/Integer;->throw()V
.end method

.method public static getInteger(Ljava/lang/String;)Ljava/lang/Integer;
    .registers 1
    invoke-static {v0}, Ljava/lang/Integer;->throw()V
.end method

.method public doubleValue()D
    .registers 1
    invoke-static {v0}, Ljava/lang/Integer;->throw()V
.end method

.method public static throw()V
.registers 2
    new-instance v0, Ljava/lang/RuntimeException;
    const-string v1, "This is an error message"
    invoke-direct {v0, v1}, Ljava/lang/RuntimeException;-><init>(Ljava/lang/String;)V
    throw v0
.end method
