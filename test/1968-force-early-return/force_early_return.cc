/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include <inttypes.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "android-base/logging.h"
#include "android-base/stringprintf.h"

#include "jni.h"
#include "jvmti.h"
#include "scoped_local_ref.h"
#include "scoped_utf_chars.h"

// Test infrastructure
#include "jni_binder.h"
#include "jni_helper.h"
#include "jvmti_helper.h"
#include "test_env.h"
#include "ti_macros.h"

#include "suspend_event_helper.h"

namespace art {
namespace Test1968ForceEarlyReturn {

extern "C" JNIEXPORT
jobject JNICALL Java_art_Test1968_00024NativeCalledObject_calledFunction(
    JNIEnv* env, jobject thiz) {
  env->PushLocalFrame(4);
  jclass klass = env->GetObjectClass(thiz);
  jfieldID cnt = env->GetFieldID(klass, "cnt", "I");
  env->SetIntField(thiz, cnt, env->GetIntField(thiz, cnt) + 1);
  jclass int_container_klass = env->FindClass("art/Test1968$IntContainer");
  jmethodID int_cont_new = env->GetMethodID(int_container_klass, "<init>", "(I)V");
  jobject res = env->NewObject(int_container_klass, int_cont_new, env->GetIntField(thiz, cnt));
  env->SetIntField(thiz, cnt, env->GetIntField(thiz, cnt) + 1);
  void *data;
  if (JvmtiErrorToException(env,
                            jvmti_env,
                            jvmti_env->GetThreadLocalStorage(/* thread */ nullptr,
                                                             reinterpret_cast<void**>(&data)))) {
    env->PopLocalFrame(nullptr);
    return nullptr;
  }
  if (data != nullptr) {
    art::common_suspend_event::PerformSuspension(jvmti_env, env);
  }
  return env->PopLocalFrame(res);
}

extern "C" JNIEXPORT
void JNICALL Java_art_Test1968_00024NativeCallerObject_run(
    JNIEnv* env, jobject thiz) {
  env->PushLocalFrame(1);
  jclass klass = env->GetObjectClass(thiz);
  jfieldID ret = env->GetFieldID(klass, "returnValue", "Ljava/lang/Object;");
  jmethodID called = env->GetMethodID(klass, "calledFunction", "()Ljava/lang/Object;");
  env->SetObjectField(thiz, ret, env->CallObjectMethod(thiz, called));
  env->PopLocalFrame(nullptr);
}

}  // namespace Test1968ForceEarlyReturn
}  // namespace art

