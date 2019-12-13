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

.class public LB146014745;
.super Ljava/lang/Object;

## CHECK-START: java.lang.String B146014745.$noinline$testAppend1(java.lang.String, int) instruction_simplifier (before)
## CHECK-NOT:                  StringBuilderAppend

## CHECK-START: java.lang.String B146014745.$noinline$testAppend1(java.lang.String, int) instruction_simplifier (after)
## CHECK:                      StringBuilderAppend

.method public static $noinline$testAppend1(Ljava/lang/String;I)Ljava/lang/String;
    .registers 4
# StringBuilder sb = new StringBuilder();
    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0}, Ljava/lang/StringBuilder;-><init>()V
# sb.append(s).append(i);
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    move-result-object v1
    invoke-virtual {v1, p1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;
# return sb.append(s).append(i).toString();
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    move-result-object v1
    invoke-virtual {v1, p1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;
    move-result-object v1
    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v1
    return-object v1
.end method

## CHECK-START: java.lang.String B146014745.$noinline$testAppend2(java.lang.String, int) instruction_simplifier (after)
## CHECK-NOT:                  StringBuilderAppend

.method public static $noinline$testAppend2(Ljava/lang/String;I)Ljava/lang/String;
    .registers 4
# StringBuilder sb = new StringBuilder();
    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0}, Ljava/lang/StringBuilder;-><init>()V
# String s2 = sb.append(s).append(i).toString();
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    move-result-object v1
    invoke-virtual {v1, p1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;
    move-result-object v1
    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v1
# return sb.append(s2).toString();
    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    move-result-object v1
    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v1
    return-object v1
.end method

## CHECK-START: java.lang.String B146014745.$noinline$testAppend3(java.lang.String, int) instruction_simplifier (after)
## CHECK-NOT:                  StringBuilderAppend

.method public static $noinline$testAppend3(Ljava/lang/String;I)Ljava/lang/String;
    .registers 5
# StringBuilder sb = new StringBuilder();
    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0}, Ljava/lang/StringBuilder;-><init>()V
# String s2 = sb.append(s).toString();
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    move-result-object v2
    invoke-virtual {v2}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v2
# return sb.append(i).append(s2).append(i);
    invoke-virtual {v0, p1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;
    move-result-object v1
    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    move-result-object v1
    invoke-virtual {v1, p1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;
# return sb.toString();
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v1
    return-object v1
.end method

# The following is a jasmin version.
# Unfortunately, this would be translated without the required move-result-object,
# instead using the register initialized by new-instance for subsequent calls.

#.class public B146014745
#.super java/lang/Object

#.method public static $noinline$testAppend1(Ljava/lang/String;I)Ljava/lang/String;
#    .limit stack 3
#    .limit locals 2
#; StringBuilder sb = new StringBuilder();
#    new java/lang/StringBuilder
#    dup
#    invokespecial java.lang.StringBuilder.<init>()V
#; sb.append(s).append(i);
#    dup
#    aload_0
#    invokevirtual java.lang.StringBuilder.append(Ljava/lang/String;)Ljava/lang/StringBuilder;
#    iload_1
#    invokevirtual java.lang.StringBuilder.append(I)Ljava/lang/StringBuilder;
#    pop
#; return sb.append(s).append(i).toString();
#    aload_0
#    invokevirtual java.lang.StringBuilder.append(Ljava/lang/String;)Ljava/lang/StringBuilder;
#    iload_1
#    invokevirtual java.lang.StringBuilder.append(I)Ljava/lang/StringBuilder;
#    invokevirtual java.lang.StringBuilder.toString()Ljava/lang/String;
#    areturn
#.end method

#.method public static $noinline$testAppend2(Ljava/lang/String;I)Ljava/lang/String;
#    .limit stack 3
#    .limit locals 2
#; StringBuilder sb = new StringBuilder();
#    new java/lang/StringBuilder
#    dup
#    invokespecial java.lang.StringBuilder.<init>()V
#; String s2 = sb.append(s).append(i).toString();
#    dup
#    aload_0
#    invokevirtual java.lang.StringBuilder.append(Ljava/lang/String;)Ljava/lang/StringBuilder;
#    iload_1
#    invokevirtual java.lang.StringBuilder.append(I)Ljava/lang/StringBuilder;
#    invokevirtual java.lang.StringBuilder.toString()Ljava/lang/String;
#; return sb.append(s2).toString();
#    invokevirtual java.lang.StringBuilder.append(Ljava/lang/String;)Ljava/lang/StringBuilder;
#    invokevirtual java.lang.StringBuilder.toString()Ljava/lang/String;
#    areturn
#.end method

#.method public static $noinline$testAppend3(Ljava/lang/String;I)Ljava/lang/String;
#    .limit stack 3
#    .limit locals 3
#; StringBuilder sb = new StringBuilder();
#    new java/lang/StringBuilder
#    dup
#    invokespecial java.lang.StringBuilder.<init>()V
#; String s2 = sb.append(s).toString();
#    dup
#    aload_0
#    invokevirtual java.lang.StringBuilder.append(Ljava/lang/String;)Ljava/lang/StringBuilder;
#    invokevirtual java.lang.StringBuilder.toString()Ljava/lang/String;
#    astore_2
#; return sb.append(i).append(s2).append(i);
#    iload_1
#    invokevirtual java.lang.StringBuilder.append(I)Ljava/lang/StringBuilder;
#    aload_2
#    invokevirtual java.lang.StringBuilder.append(Ljava/lang/String;)Ljava/lang/StringBuilder;
#    iload_1
#    invokevirtual java.lang.StringBuilder.append(I)Ljava/lang/StringBuilder;
#    invokevirtual java.lang.StringBuilder.toString()Ljava/lang/String;
#    areturn
#.end method
