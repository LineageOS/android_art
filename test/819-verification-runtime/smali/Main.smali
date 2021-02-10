# Copyright 2021 The Android Open Source Project
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

.class public LMain;

.super Ljava/lang/Object;

.method public static main([Ljava/lang/String;)V
  .registers 1
  invoke-static {}, LMain;->softFail()I
  return-void
.end method

.method public static softFail()I
  .registers 1
  sget v0, LMain;->test:I
  if-eqz v0, :Lzero
  # BecausenonExistentMethod does not exist, the verifier will
  # consider this instructions as always throwing, and will not
  # look at the return-void below.
  invoke-static {}, LMain;->nonExistentMethod()V
  # Normally, this should hard-fail the verification, but it is
  # skipped due to the throwing instruction above.
  return-void
:Lzero
  return v0
.end method

.field public static test:I
