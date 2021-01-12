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

#include <queue>
#include <vector>

#include "jvmti.h"

// Test infrastructure
#include "jvmti_helper.h"
#include "nativehelper/scoped_local_ref.h"
#include "nativehelper/scoped_primitive_array.h"
#include "test_env.h"

namespace art {
namespace Test2038HiddenApiExt {

template <typename T>
static void Dealloc(T* t) {
  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(t));
}

template <typename T, typename... Rest>
static void Dealloc(T* t, Rest... rs) {
  Dealloc(t);
  Dealloc(rs...);
}

static void DeallocParams(jvmtiParamInfo* params, jint n_params) {
  for (jint i = 0; i < n_params; i++) {
    Dealloc(params[i].name);
  }
}

static constexpr std::string_view kDisablePolicyName =
    "com.android.art.misc.disable_hidden_api_enforcement_policy";
static constexpr std::string_view kGetPolicyName =
    "com.android.art.misc.get_hidden_api_enforcement_policy";
static constexpr std::string_view kSetPolicyName =
    "com.android.art.misc.set_hidden_api_enforcement_policy";
using GetPolicy = jvmtiError (*)(jvmtiEnv*, jint*);
using SetPolicy = jvmtiError (*)(jvmtiEnv*, jint);
using DisablePolicy = jvmtiError (*)(jvmtiEnv*);

void* GetExtension(JNIEnv* env, const std::string_view& name) {
  // Get the extensions.
  jint n_ext = 0;
  void* result = nullptr;
  jvmtiExtensionFunctionInfo* infos = nullptr;
  if (JvmtiErrorToException(env, jvmti_env, jvmti_env->GetExtensionFunctions(&n_ext, &infos))) {
    return nullptr;
  }
  for (jint i = 0; i < n_ext; i++) {
    jvmtiExtensionFunctionInfo* cur_info = &infos[i];
    if (name == std::string_view(cur_info->id)) {
      result = reinterpret_cast<void*>(cur_info->func);
    }
    // Cleanup the cur_info
    DeallocParams(cur_info->params, cur_info->param_count);
    Dealloc(cur_info->id, cur_info->short_description, cur_info->params, cur_info->errors);
  }
  // Cleanup the array.
  Dealloc(infos);
  if (result == nullptr) {
    ScopedLocalRef<jclass> rt_exception(env, env->FindClass("java/lang/RuntimeException"));
    env->ThrowNew(rt_exception.get(), "Unable to find policy extensions.");
    return nullptr;
  }
  return result;
}

extern "C" JNIEXPORT jint JNICALL Java_Main_disablePolicy(JNIEnv* env, jclass) {
  jint res;
  GetPolicy get_policy = reinterpret_cast<GetPolicy>(GetExtension(env, kGetPolicyName));
  if (get_policy == nullptr) {
    return -1;
  }
  DisablePolicy disable_policy =
      reinterpret_cast<DisablePolicy>(GetExtension(env, kDisablePolicyName));
  if (disable_policy == nullptr) {
    return -1;
  }
  if (JvmtiErrorToException(env, jvmti_env, get_policy(jvmti_env, &res))) {
    return -1;
  }
  JvmtiErrorToException(env, jvmti_env, disable_policy(jvmti_env));
  return res;
}

extern "C" JNIEXPORT jint JNICALL Java_Main_setPolicy(JNIEnv* env, jclass, jint pol) {
  jint res;
  GetPolicy get_policy = reinterpret_cast<GetPolicy>(GetExtension(env, kGetPolicyName));
  if (get_policy == nullptr) {
    return -1;
  }
  SetPolicy set_policy = reinterpret_cast<SetPolicy>(GetExtension(env, kSetPolicyName));
  if (set_policy == nullptr) {
    return -1;
  }
  if (JvmtiErrorToException(env, jvmti_env, get_policy(jvmti_env, &res))) {
    return -1;
  }
  JvmtiErrorToException(env, jvmti_env, set_policy(jvmti_env, pol));
  return res;
}

}  // namespace Test2038HiddenApiExt
}  // namespace art
