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

#include <stdio.h>

#include <vector>

#include "android-base/logging.h"
#include "android-base/macros.h"
#include "jni.h"
#include "jvmti.h"

// Test infrastructure
#include "jvmti_helper.h"
#include "scoped_local_ref.h"
#include "test_env.h"

namespace art {
namespace Test2009StructuralLocalRef {

extern "C" JNIEXPORT jstring JNICALL Java_art_Test2009_NativeLocalCallStatic(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jobject obj, jobject thnk) {
  jclass obj_klass = env->GetObjectClass(obj);
  jmethodID run_meth = env->GetMethodID(env->FindClass("java/lang/Runnable"), "run", "()V");
  env->CallVoidMethod(thnk, run_meth);
  jmethodID new_method =
      env->GetStaticMethodID(obj_klass, "getGreetingStatic", "()Ljava/lang/String;");
  if (env->ExceptionCheck()) {
    return nullptr;
  } else {
    return reinterpret_cast<jstring>(env->CallStaticObjectMethod(obj_klass, new_method));
  }
}

extern "C" JNIEXPORT jstring JNICALL Java_art_Test2009_NativeLocalCallVirtual(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jobject obj, jobject thnk) {
  jclass obj_klass = env->GetObjectClass(obj);
  jmethodID run_meth = env->GetMethodID(env->FindClass("java/lang/Runnable"), "run", "()V");
  env->CallVoidMethod(thnk, run_meth);
  jmethodID new_method = env->GetMethodID(obj_klass, "getGreeting", "()Ljava/lang/String;");
  if (env->ExceptionCheck()) {
    return nullptr;
  } else {
    return reinterpret_cast<jstring>(env->CallObjectMethod(obj, new_method));
  }
}
extern "C" JNIEXPORT jstring JNICALL Java_art_Test2009_NativeLocalGetIField(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jobject obj, jobject thnk) {
  jclass obj_klass = env->GetObjectClass(obj);
  jmethodID run_meth = env->GetMethodID(env->FindClass("java/lang/Runnable"), "run", "()V");
  env->CallVoidMethod(thnk, run_meth);
  jfieldID new_field = env->GetFieldID(obj_klass, "greeting", "Ljava/lang/String;");
  if (env->ExceptionCheck()) {
    return nullptr;
  } else {
    env->SetObjectField(obj, new_field, env->NewStringUTF("VirtualString"));
    return reinterpret_cast<jstring>(env->GetObjectField(obj, new_field));
  }
}
extern "C" JNIEXPORT jstring JNICALL Java_art_Test2009_NativeLocalGetSField(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jobject obj, jobject thnk) {
  jclass obj_klass = env->GetObjectClass(obj);
  jmethodID run_meth = env->GetMethodID(env->FindClass("java/lang/Runnable"), "run", "()V");
  env->CallVoidMethod(thnk, run_meth);
  jfieldID new_field = env->GetStaticFieldID(obj_klass, "static_greeting", "Ljava/lang/String;");
  if (env->ExceptionCheck()) {
    return nullptr;
  } else {
    env->SetStaticObjectField(obj_klass, new_field, env->NewStringUTF("StaticString"));
    return reinterpret_cast<jstring>(env->GetStaticObjectField(obj_klass, new_field));
  }
}

}  // namespace Test2009StructuralLocalRef
}  // namespace art
