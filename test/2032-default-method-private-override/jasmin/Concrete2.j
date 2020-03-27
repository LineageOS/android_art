; Copyright (C) 2020 The Android Open Source Project
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
;      http://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.

.class public Concrete2
; Our superclass implements the IFace interface.
.super Concrete2Base

.method public <init>()V
  .limit stack 1
  .limit locals 1
  aload_0
  invokespecial Concrete2Base/<init>()V
  return
.end method

.method private static sayHi()V
  .limit stack 2
  .limit locals 2
  getstatic java/lang/System/out Ljava/io/PrintStream;
  ldc "Hello from a private method!"
  invokevirtual java/io/PrintStream/println(Ljava/lang/String;)V
  return
.end method
