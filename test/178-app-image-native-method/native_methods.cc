/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "jni.h"

namespace art {

static inline bool VerifyManyParameters(
    jint i1, jlong l1, jfloat f1, jdouble d1,
    jint i2, jlong l2, jfloat f2, jdouble d2,
    jint i3, jlong l3, jfloat f3, jdouble d3,
    jint i4, jlong l4, jfloat f4, jdouble d4,
    jint i5, jlong l5, jfloat f5, jdouble d5,
    jint i6, jlong l6, jfloat f6, jdouble d6,
    jint i7, jlong l7, jfloat f7, jdouble d7,
    jint i8, jlong l8, jfloat f8, jdouble d8) {
  return
      (i1 == 11) && (l1 == 12) && (f1 == 13.0) && (d1 == 14.0) &&
      (i2 == 21) && (l2 == 22) && (f2 == 23.0) && (d2 == 24.0) &&
      (i3 == 31) && (l3 == 32) && (f3 == 33.0) && (d3 == 34.0) &&
      (i4 == 41) && (l4 == 42) && (f4 == 43.0) && (d4 == 44.0) &&
      (i5 == 51) && (l5 == 52) && (f5 == 53.0) && (d5 == 54.0) &&
      (i6 == 61) && (l6 == 62) && (f6 == 63.0) && (d6 == 64.0) &&
      (i7 == 71) && (l7 == 72) && (f7 == 73.0) && (d7 == 74.0) &&
      (i8 == 81) && (l8 == 82) && (f8 == 83.0) && (d8 == 84.0);
}

extern "C" JNIEXPORT jint JNICALL Java_Test_nativeMethodVoid(JNIEnv*, jclass) {
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_Test_nativeMethod(JNIEnv*, jclass, jint i) {
  return i;
}

extern "C" JNIEXPORT jint JNICALL Java_Test_nativeMethodWithManyParameters(
    JNIEnv*, jclass,
    jint i1, jlong l1, jfloat f1, jdouble d1,
    jint i2, jlong l2, jfloat f2, jdouble d2,
    jint i3, jlong l3, jfloat f3, jdouble d3,
    jint i4, jlong l4, jfloat f4, jdouble d4,
    jint i5, jlong l5, jfloat f5, jdouble d5,
    jint i6, jlong l6, jfloat f6, jdouble d6,
    jint i7, jlong l7, jfloat f7, jdouble d7,
    jint i8, jlong l8, jfloat f8, jdouble d8) {
  bool ok = VerifyManyParameters(
      i1, l1, f1, d1,
      i2, l2, f2, d2,
      i3, l3, f3, d3,
      i4, l4, f4, d4,
      i5, l5, f5, d5,
      i6, l6, f6, d6,
      i7, l7, f7, d7,
      i8, l8, f8, d8);
  return ok ? 42 : -1;
}

extern "C" JNIEXPORT jint JNICALL Java_TestFast_nativeMethodVoid(JNIEnv*, jclass) {
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_TestFast_nativeMethod(JNIEnv*, jclass, jint i) {
  return i;
}

extern "C" JNIEXPORT jint JNICALL Java_TestFast_nativeMethodWithManyParameters(
    JNIEnv*, jclass,
    jint i1, jlong l1, jfloat f1, jdouble d1,
    jint i2, jlong l2, jfloat f2, jdouble d2,
    jint i3, jlong l3, jfloat f3, jdouble d3,
    jint i4, jlong l4, jfloat f4, jdouble d4,
    jint i5, jlong l5, jfloat f5, jdouble d5,
    jint i6, jlong l6, jfloat f6, jdouble d6,
    jint i7, jlong l7, jfloat f7, jdouble d7,
    jint i8, jlong l8, jfloat f8, jdouble d8) {
  bool ok = VerifyManyParameters(
      i1, l1, f1, d1,
      i2, l2, f2, d2,
      i3, l3, f3, d3,
      i4, l4, f4, d4,
      i5, l5, f5, d5,
      i6, l6, f6, d6,
      i7, l7, f7, d7,
      i8, l8, f8, d8);
  return ok ? 42 : -1;
}

extern "C" JNIEXPORT jint JNICALL Java_TestCritical_nativeMethodVoid() {
  return 42;
}

extern "C" JNIEXPORT jint JNICALL Java_TestCritical_nativeMethod(jint i) {
  return i;
}

extern "C" JNIEXPORT jint JNICALL Java_TestCritical_nativeMethodWithManyParameters(
    jint i1, jlong l1, jfloat f1, jdouble d1,
    jint i2, jlong l2, jfloat f2, jdouble d2,
    jint i3, jlong l3, jfloat f3, jdouble d3,
    jint i4, jlong l4, jfloat f4, jdouble d4,
    jint i5, jlong l5, jfloat f5, jdouble d5,
    jint i6, jlong l6, jfloat f6, jdouble d6,
    jint i7, jlong l7, jfloat f7, jdouble d7,
    jint i8, jlong l8, jfloat f8, jdouble d8) {
  bool ok = VerifyManyParameters(
      i1, l1, f1, d1,
      i2, l2, f2, d2,
      i3, l3, f3, d3,
      i4, l4, f4, d4,
      i5, l5, f5, d5,
      i6, l6, f6, d6,
      i7, l7, f7, d7,
      i8, l8, f8, d8);
  return ok ? 42 : -1;
}

}  // namespace art
