/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <android-base/logging.h>

namespace art {

extern "C" JNIEXPORT jint JNICALL Java_TestCase_testNativeInternal(JNIEnv* env,
                                                                   jclass) {
  jclass cls = env->FindClass("InheritAbstract");
  CHECK(cls != nullptr);
  jmethodID constructor = env->GetMethodID(cls, "<init>", "()V");
  CHECK(constructor != nullptr);
  jmethodID method_id = env->GetMethodID(cls, "methodPublicSdkNotInAbstractParent", "()I");
  if (method_id == nullptr) {
    return -1;
  }
  jobject obj = env->NewObject(cls, constructor);
  return env->CallIntMethod(obj, method_id);
}

}  // namespace art
