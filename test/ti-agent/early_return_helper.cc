/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "common_helper.h"

#include "jni.h"
#include "jvmti.h"

#include "jvmti_helper.h"
#include "scoped_local_ref.h"
#include "test_env.h"

namespace art {
namespace common_early_return {

extern "C" JNIEXPORT void JNICALL Java_art_NonStandardExit_popFrame(
    JNIEnv* env, jclass k ATTRIBUTE_UNUSED, jthread thr) {
  JvmtiErrorToException(env, jvmti_env, jvmti_env->PopFrame(thr));
}

extern "C" JNIEXPORT void JNICALL Java_art_NonStandardExit_forceEarlyReturnFloat(
    JNIEnv* env, jclass k ATTRIBUTE_UNUSED, jthread thr, jfloat val) {
  JvmtiErrorToException(env, jvmti_env, jvmti_env->ForceEarlyReturnFloat(thr, val));
}

extern "C" JNIEXPORT void JNICALL Java_art_NonStandardExit_forceEarlyReturnDouble(
    JNIEnv* env, jclass k ATTRIBUTE_UNUSED, jthread thr, jdouble val) {
  JvmtiErrorToException(env, jvmti_env, jvmti_env->ForceEarlyReturnDouble(thr, val));
}

extern "C" JNIEXPORT void JNICALL Java_art_NonStandardExit_forceEarlyReturnLong(
    JNIEnv* env, jclass k ATTRIBUTE_UNUSED, jthread thr, jlong val) {
  JvmtiErrorToException(env, jvmti_env, jvmti_env->ForceEarlyReturnLong(thr, val));
}

extern "C" JNIEXPORT void JNICALL Java_art_NonStandardExit_forceEarlyReturnInt(
    JNIEnv* env, jclass k ATTRIBUTE_UNUSED, jthread thr, jint val) {
  JvmtiErrorToException(env, jvmti_env, jvmti_env->ForceEarlyReturnInt(thr, val));
}

extern "C" JNIEXPORT void JNICALL Java_art_NonStandardExit_forceEarlyReturnVoid(
    JNIEnv* env, jclass k ATTRIBUTE_UNUSED, jthread thr) {
  JvmtiErrorToException(env, jvmti_env, jvmti_env->ForceEarlyReturnVoid(thr));
}

extern "C" JNIEXPORT void JNICALL Java_art_NonStandardExit_forceEarlyReturnObject(
    JNIEnv* env, jclass k ATTRIBUTE_UNUSED, jthread thr, jobject val) {
  JvmtiErrorToException(env, jvmti_env, jvmti_env->ForceEarlyReturnObject(thr, val));
}

}  // namespace common_early_return
}  // namespace art
