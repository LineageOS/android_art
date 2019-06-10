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
namespace Test1969ForceEarlyReturnVoid {

extern "C" JNIEXPORT
void JNICALL Java_art_Test1969_00024NativeCalledObject_calledFunction(
    JNIEnv* env, jobject thiz) {
  jclass klass = env->GetObjectClass(thiz);
  jfieldID cnt = env->GetFieldID(klass, "cnt", "I");
  env->SetIntField(thiz, cnt, env->GetIntField(thiz, cnt) + 1);
  env->SetIntField(thiz, cnt, env->GetIntField(thiz, cnt) + 1);
  void *data;
  if (JvmtiErrorToException(env,
                            jvmti_env,
                            jvmti_env->GetThreadLocalStorage(/* thread */ nullptr,
                                                             reinterpret_cast<void**>(&data)))) {
    return;
  }
  if (data != nullptr) {
    art::common_suspend_event::PerformSuspension(jvmti_env, env);
  }
  return;
}

extern "C" JNIEXPORT
void JNICALL Java_art_Test1969_00024NativeCallerObject_run(
    JNIEnv* env, jobject thiz) {
  env->PushLocalFrame(1);
  jclass klass = env->GetObjectClass(thiz);
  jmethodID called = env->GetMethodID(klass, "calledFunction", "()V");
  env->CallVoidMethod(thiz, called);
  env->PopLocalFrame(nullptr);
}

extern "C" JNIEXPORT
jboolean JNICALL Java_art_Test1969_isClassLoaded(JNIEnv* env, jclass, jstring name) {
  ScopedUtfChars chr(env, name);
  if (env->ExceptionCheck()) {
    return false;
  }
  jint cnt = 0;
  jclass* klasses = nullptr;
  if (JvmtiErrorToException(env, jvmti_env, jvmti_env->GetLoadedClasses(&cnt, &klasses))) {
    return false;
  }
  bool res = false;
  for (jint i = 0; !res && i < cnt; i++) {
    char* sig;
    if (JvmtiErrorToException(env,
                              jvmti_env,
                              jvmti_env->GetClassSignature(klasses[i], &sig, nullptr))) {
      return false;
    }
    res = (strcmp(sig, chr.c_str()) == 0);
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(sig));
  }
  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(klasses));
  return res;
}

}  // namespace Test1969ForceEarlyReturnVoid
}  // namespace art

