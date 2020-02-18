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

.class public LTestCase;

.super Ljava/lang/Object;

.method public static test()Ljava/lang/String;
   .registers 2
   sget-object v0, LMain;->main:LItf;
   invoke-interface {v0}, LItf;->toString()Ljava/lang/String;
   move-result-object v0
   return-object v0
.end method
