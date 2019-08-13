
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
#include <mutex>
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
namespace Test1976StructuralTransformMethods {

extern "C" JNIEXPORT void JNICALL Java_art_Test1976_callNativeMethods(JNIEnv* env,
                                                                      jclass k,
                                                                      jclass m_class,
                                                                      jlongArray m) {
  jint len = env->GetArrayLength(m);
  for (jint i = 0; i < len; i++) {
    jlong mid_val;
    env->GetLongArrayRegion(m, i, 1, &mid_val);
    jmethodID mid = reinterpret_cast<jmethodID>(static_cast<intptr_t>(mid_val));
    // For this test everything is objects and static.
    env->CallStaticVoidMethod(
        k,
        env->GetStaticMethodID(
            k, "printRun", "(JLjava/lang/reflect/Method;)V"),
        mid_val,
        env->ToReflectedMethod(m_class, mid, true));
    env->CallStaticVoidMethod(m_class, mid);
  }
}

extern "C" JNIEXPORT jlongArray JNICALL Java_art_Test1976_getMethodIds(JNIEnv* env,
                                                                       jclass,
                                                                       jobjectArray m) {
  jint len = env->GetArrayLength(m);
  jlongArray arr = env->NewLongArray(len);
  for (jint i = 0; i < len; i++) {
    env->PushLocalFrame(1);
    jmethodID fid = env->FromReflectedMethod(env->GetObjectArrayElement(m, i));
    jlong lmid = static_cast<jlong>(reinterpret_cast<intptr_t>(fid));
    env->SetLongArrayRegion(arr, i, 1, &lmid);
    env->PopLocalFrame(nullptr);
  }
  return arr;
}

}  // namespace Test1976StructuralTransformMethods
}  // namespace art
