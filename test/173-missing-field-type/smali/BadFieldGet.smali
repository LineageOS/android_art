#
# Copyright (C) 2021 The Android Open Source Project
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

.class public LBadFieldGet;
.super Ljava/lang/Object;

# These are bad fields since there is no class Widget in this test.
.field public static widget:LWidget;

.method public constructor <init>()V
    .registers 2
    invoke-direct {v1}, Ljava/lang/Object;-><init>()V
    return-void
.end method

.method public static loadStatic()Ljava/lang/Object;
    .registers 1
    # Put an object in the register used for sget-object, the bug in mterp checked it was
    # non-null.
    new-instance v0, LBadFieldGet;
    invoke-direct {v0}, LBadFieldGet;-><init>()V
    sget-object v0, LBadField;->widget:LWidget;
    return-object v0
.end method

.method public static forceAccessChecks()V
    .registers 0
    invoke-static {}, LMain;->privateMethod()V
    return-void
.end method
