#
# Copyright (C) 2019 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

.class public LNewInstance;
.super Ljava/lang/Object;

.method public static initRange(Ljava/lang/String;)Ljava/lang/String;
    .registers 2
    new-instance v0, Ljava/lang/String;
    move-object v1, p0
    invoke-direct/range {v0 .. v1}, Ljava/lang/String;-><init>(Ljava/lang/String;)V
    return-object v0
.end method

.method public static initRange([BIII)Ljava/lang/String;
    .registers 9
    new-instance v0, Ljava/lang/String;
    move-object v1, p0
    move v2, p1
    move v3, p2
    move v4, p3
    invoke-direct/range {v0 .. v4}, Ljava/lang/String;-><init>([BIII)V
    return-object v0
.end method

.method public static initRangeWithAlias(Ljava/lang/String;)Ljava/lang/String;
    .registers 19
    # Put the object in a register > 0xF, as the arm64 nterp implementation wrongly masked
    # that register with 0xF when handling String.<init> in an invoke-range.
    new-instance v16, Ljava/lang/String;
    move-object/from16 v1, v16
    move-object/16 v17, p0
    invoke-direct/range {v16 .. v17}, Ljava/lang/String;-><init>(Ljava/lang/String;)V
    return-object v1
.end method
