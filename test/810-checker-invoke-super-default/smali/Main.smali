# Copyright 2020 The Android Open Source Project
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

.class LMain;
.super Ljava/lang/Object;

.method public static main([Ljava/lang/String;)V
.registers 1
    new-instance v0, LClass;
    invoke-direct {v0}, LClass;-><init>()V
    invoke-virtual {v0}, LClass;->$noinline$foo()V
    invoke-virtual {v0}, LClass;->$noinline$foo()V
    new-instance v0, LOtherClass;
    invoke-direct {v0}, LOtherClass;-><init>()V
    invoke-virtual {v0}, LOtherClass;->$noinline$foo()V
    invoke-virtual {v0}, LOtherClass;->$noinline$foo()V
    return-void
.end method
