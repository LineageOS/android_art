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

.class public LWeirdLoop;

.super Ljava/lang/Object;

.method public static weirdLoop()I
    .registers 3
    invoke-static {}, LMain;->$noinline$magicValue()I
    move-result v0
    const-string v1, "weirdLoop"
    invoke-static {v1}, LMain;->isInInterpreter(Ljava/lang/String;)Z
    move-result v2
    if-eqz v2, :end
    goto :mid

    :top
    invoke-static {}, LMain;->$noinline$magicValue()I
    move-result v0
    :mid
    invoke-static {v1}, LMain;->isInOsrCode(Ljava/lang/String;)Z
    move-result v2
    if-eqz v2, :top
    :end
    return v0
.end method

