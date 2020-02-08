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

#include "dalvik_system_DexFile.h"

#include <memory>

#include "class_loader_context.h"
#include "native_util.h"
#include "nativehelper/jni_macros.h"
#include "well_known_classes.h"

namespace art {

static bool append_string(JNIEnv* env, jobjectArray array, uint32_t& i, const std::string& string) {
  ScopedLocalRef<jstring> jstring(env, env->NewStringUTF(string.c_str()));
  if (jstring.get() == nullptr) {
    DCHECK(env->ExceptionCheck());
    return false;
  }
  env->SetObjectArrayElement(array, i++, jstring.get());
  return true;
}

static jobjectArray BaseDexClassLoader_computeClassLoaderContextsNative(JNIEnv* env,
                                                                        jobject class_loader) {
  CHECK(class_loader != nullptr);
  std::map<std::string, std::string> contextMap =
      ClassLoaderContext::EncodeClassPathContextsForClassLoader(class_loader);
  jobjectArray result = env->NewObjectArray(2 * contextMap.size(),
                                            WellKnownClasses::java_lang_String,
                                            nullptr);
  if (result == nullptr) {
    DCHECK(env->ExceptionCheck());
    return nullptr;
  }
  uint32_t i = 0;
  for (const auto& classpath_to_context : contextMap) {
    const std::string& classpath = classpath_to_context.first;
    const std::string& context = classpath_to_context.second;
    if (!append_string(env, result, i, classpath) || !append_string(env, result, i, context)) {
      return nullptr;
    }
  }
  return result;
}

static JNINativeMethod gMethods[] = {
  NATIVE_METHOD(BaseDexClassLoader, computeClassLoaderContextsNative,
                "()[Ljava/lang/String;"),
};

void register_dalvik_system_BaseDexClassLoader(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("dalvik/system/BaseDexClassLoader");
}

}  // namespace art
