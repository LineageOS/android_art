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

.class LOtherClass;
.super LSuperClass;

## CHECK-START: void OtherClass.$noinline$foo() register (after)
# TODO(ngeoffray): We should emit HInvokeStaticDirect
## CHECK-DAG:     InvokeUnresolved method_name:OtherClass.$noinline$foo
## CHECK-DAG:     InvokeStaticOrDirect dex_file_index:<<Index2:\d+>> method_name:SuperSuperClass.$noinline$foo
## CHECK-DAG:     InvokeStaticOrDirect dex_file_index:<<Index3:\d+>> method_name:SuperSuperClass.$noinline$foo
.method public $noinline$foo()V
.registers 1
    invoke-super {p0}, LOtherClass;->$noinline$foo()V
    invoke-super {p0}, LSuperClass;->$noinline$foo()V
    invoke-super {p0}, LSuperSuperClass;->$noinline$foo()V
    return-void
.end method
