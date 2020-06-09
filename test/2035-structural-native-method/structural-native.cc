/*
 * Copyright (C) 2020 The Android Open Source Project
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
namespace Test2035StructuralNativeMethod {

jlong JNICALL TransformNativeMethod(JNIEnv* env ATTRIBUTE_UNUSED, jclass klass ATTRIBUTE_UNUSED) {
  return 42;
}

extern "C" JNIEXPORT void JNICALL Java_art_Test2035_LinkClassMethods(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jclass target) {
  JNINativeMethod meth{"getValue", "()J", reinterpret_cast<void*>(TransformNativeMethod)};
  env->RegisterNatives(target, &meth, 1);
}


}  // namespace Test2035StructuralNativeMethod
}  // namespace art
