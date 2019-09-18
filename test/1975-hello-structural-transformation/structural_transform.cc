
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
namespace Test1975StructuralTransform {

extern "C" JNIEXPORT void JNICALL Java_art_Test1975_readNativeFields(JNIEnv* env,
                                                                    jclass k,
                                                                    jclass f_class,
                                                                    jlongArray f) {
  jint len = env->GetArrayLength(f);
  for (jint i = 0; i < len; i++) {
    jlong fid_val;
    env->GetLongArrayRegion(f, i, 1, &fid_val);
    jfieldID fid = reinterpret_cast<jfieldID>(static_cast<intptr_t>(fid_val));
    // For this test everything is objects and static.
    jobject val = env->GetStaticObjectField(f_class, fid);
    env->CallStaticVoidMethod(
        k,
        env->GetStaticMethodID(
            k, "printNativeField", "(JLjava/lang/reflect/Field;Ljava/lang/Object;)V"),
        fid_val,
        env->ToReflectedField(f_class, fid, true),
        val);
    env->DeleteLocalRef(val);
  }
}

extern "C" JNIEXPORT jlongArray JNICALL Java_art_Test1975_getNativeFields(JNIEnv* env,
                                                                          jclass,
                                                                          jobjectArray f) {
  jint len = env->GetArrayLength(f);
  jlongArray arr = env->NewLongArray(len);
  for (jint i = 0; i < len; i++) {
    jfieldID fid = env->FromReflectedField(env->GetObjectArrayElement(f, i));
    jlong lfid = static_cast<jlong>(reinterpret_cast<intptr_t>(fid));
    env->SetLongArrayRegion(arr, i, 1, &lfid);
  }
  return arr;
}

}  // namespace Test1975StructuralTransform
}  // namespace art
