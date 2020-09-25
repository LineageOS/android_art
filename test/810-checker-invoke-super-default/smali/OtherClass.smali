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
.super LOtherAbstractClass;

## CHECK-START: void OtherClass.$noinline$foo() register (after)
# We check that the method_index is 0, as that's the method index for the interface method.
# The copied method would have a different method index.
## CHECK-DAG:     InvokeStaticOrDirect method_name:Itf.$noinline$foo method_index:0
## CHECK-DAG:     InvokeStaticOrDirect method_name:Itf.$noinline$foo method_index:0
## CHECK-DAG:     InvokeStaticOrDirect method_name:Itf.$noinline$foo method_index:0
.method public $noinline$foo()V
.registers 1
    invoke-super {p0}, LOtherClass;->$noinline$foo()V
    invoke-super {p0}, LOtherAbstractClass;->$noinline$foo()V
    invoke-super {p0}, LItf;->$noinline$foo()V
    return-void
.end method
