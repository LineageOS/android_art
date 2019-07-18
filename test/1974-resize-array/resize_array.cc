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

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "android-base/logging.h"
#include "android-base/macros.h"
#include "android-base/stringprintf.h"

#include "jni.h"
#include "jvmti.h"
#include "scoped_local_ref.h"
#include "scoped_utf_chars.h"

// Test infrastructure
#include "jni_helper.h"
#include "jvmti_helper.h"
#include "test_env.h"
#include "ti_macros.h"

namespace art {
namespace Test1974ResizeArray {

using ChangeArraySize = jvmtiError (*)(jvmtiEnv* env, jobject arr, jint size);

template <typename T> static void Dealloc(T* t) {
  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(t));
}

template <typename T, typename... Rest> static void Dealloc(T* t, Rest... rs) {
  Dealloc(t);
  Dealloc(rs...);
}

static void DeallocParams(jvmtiParamInfo* params, jint n_params) {
  for (jint i = 0; i < n_params; i++) {
    Dealloc(params[i].name);
  }
}

static jvmtiExtensionFunction FindExtensionMethod(JNIEnv* env, const std::string& name) {
  jint n_ext;
  jvmtiExtensionFunctionInfo* infos;
  if (JvmtiErrorToException(env, jvmti_env, jvmti_env->GetExtensionFunctions(&n_ext, &infos))) {
    return nullptr;
  }
  jvmtiExtensionFunction res = nullptr;
  for (jint i = 0; i < n_ext; i++) {
    jvmtiExtensionFunctionInfo* cur_info = &infos[i];
    if (strcmp(name.c_str(), cur_info->id) == 0) {
      res = cur_info->func;
    }
    // Cleanup the cur_info
    DeallocParams(cur_info->params, cur_info->param_count);
    Dealloc(cur_info->id, cur_info->short_description, cur_info->params, cur_info->errors);
  }
  // Cleanup the array.
  Dealloc(infos);
  if (res == nullptr) {
    ScopedLocalRef<jclass> rt_exception(env, env->FindClass("java/lang/RuntimeException"));
    env->ThrowNew(rt_exception.get(), (name + " extensions not found").c_str());
    return nullptr;
  }
  return res;
}

extern "C" JNIEXPORT void JNICALL Java_art_Test1974_ResizeArray(JNIEnv* env,
                                                                jclass klass ATTRIBUTE_UNUSED,
                                                                jobject ref_gen,
                                                                jint new_size) {
  ChangeArraySize change_array_size = reinterpret_cast<ChangeArraySize>(
      FindExtensionMethod(env, "com.android.art.heap.change_array_size"));
  if (change_array_size == nullptr) {
    return;
  }
  jmethodID getArr = env->GetMethodID(
      env->FindClass("java/util/function/Supplier"), "get", "()Ljava/lang/Object;");
  jobject arr = env->CallObjectMethod(ref_gen, getArr);
  JvmtiErrorToException(env, jvmti_env, change_array_size(jvmti_env, arr, new_size));
}

extern "C" JNIEXPORT jobject JNICALL Java_art_Test1974_ReadJniRef(JNIEnv* env,
                                                                  jclass klass ATTRIBUTE_UNUSED,
                                                                  jlong r) {
  return env->NewLocalRef(reinterpret_cast<jobject>(static_cast<intptr_t>(r)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_art_Test1974_GetWeakGlobalJniRef(JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jobject r) {
  return static_cast<jlong>(reinterpret_cast<intptr_t>(env->NewWeakGlobalRef(r)));
}

extern "C" JNIEXPORT jlong JNICALL Java_art_Test1974_GetGlobalJniRef(JNIEnv* env,
                                                                     jclass klass ATTRIBUTE_UNUSED,
                                                                     jobject r) {
  return static_cast<jlong>(reinterpret_cast<intptr_t>(env->NewGlobalRef(r)));
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_art_Test1974_GetObjectsWithTag(JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jlong tag) {
  jsize cnt = 0;
  jobject* res = nullptr;
  if (JvmtiErrorToException(
          env, jvmti_env, jvmti_env->GetObjectsWithTags(1, &tag, &cnt, &res, nullptr))) {
    return nullptr;
  }
  jobjectArray ret = env->NewObjectArray(cnt, env->FindClass("java/lang/Object"), nullptr);
  if (ret == nullptr) {
    return nullptr;
  }
  for (jsize i = 0; i < cnt; i++) {
    env->SetObjectArrayElement(ret, i, res[i]);
  }
  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(res));
  return ret;
}

extern "C" JNIEXPORT void JNICALL Java_art_Test1974_runNativeTest(JNIEnv* env,
                                                                  jclass klass ATTRIBUTE_UNUSED,
                                                                  jobjectArray arr,
                                                                  jobject resize,
                                                                  jobject print,
                                                                  jobject check) {
  jmethodID run = env->GetMethodID(env->FindClass("java/lang/Runnable"), "run", "()V");
  jmethodID accept = env->GetMethodID(
      env->FindClass("java/util/function/Consumer"), "accept", "(Ljava/lang/Object;)V");
  env->CallVoidMethod(print, accept, arr);
  env->CallVoidMethod(resize, run);
  env->CallVoidMethod(print, accept, arr);
  env->CallVoidMethod(check, accept, arr);
}
}  // namespace Test1974ResizeArray
}  // namespace art
