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

#include <jni.h>
#include <sstream>
#include <stdio.h>

#include <android-base/logging.h>
#include <android-base/macros.h>

#include "jni/java_vm_ext.h"
#include "runtime.h"

namespace art {

extern "C" JNIEXPORT jlong JNICALL Java_Main_GetMethodId(JNIEnv* env,
                                                         jclass k ATTRIBUTE_UNUSED,
                                                         bool is_static,
                                                         jclass target,
                                                         jstring name,
                                                         jstring sig) {
  auto get_id = is_static ? env->functions->GetStaticMethodID : env->functions->GetMethodID;
  jboolean cpy;
  const char* cname = env->GetStringUTFChars(name, &cpy);
  const char* csig = env->GetStringUTFChars(sig, &cpy);
  jlong res = static_cast<jlong>(reinterpret_cast<intptr_t>(get_id(env, target, cname, csig)));
  env->ReleaseStringUTFChars(name, cname);
  env->ReleaseStringUTFChars(sig, csig);
  return res;
}

extern "C" JNIEXPORT jobject JNICALL Java_Main_GetJniType(JNIEnv* env, jclass k ATTRIBUTE_UNUSED) {
  std::ostringstream oss;
  oss << Runtime::Current()->GetJniIdType();
  return env->NewStringUTF(oss.str().c_str());
}

extern "C" JNIEXPORT void JNICALL Java_Main_SetToPointerIds(JNIEnv* env ATTRIBUTE_UNUSED,
                                                            jclass k ATTRIBUTE_UNUSED) {
  Runtime::Current()->SetJniIdType(JniIdType::kPointer);
}
extern "C" JNIEXPORT void JNICALL Java_Main_SetToIndexIds(JNIEnv* env ATTRIBUTE_UNUSED,
                                                          jclass k ATTRIBUTE_UNUSED) {
  Runtime::Current()->SetJniIdType(JniIdType::kIndices);
}

}  // namespace art
