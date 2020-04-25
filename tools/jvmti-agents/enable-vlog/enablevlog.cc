// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <android-base/logging.h>
#include <jni.h>
#include <jvmti.h>

#include <iostream>
#include <string_view>

namespace enablevlog {

namespace {

// Special art ti-version number. We will use this as a fallback if we cannot get a regular JVMTI
// env.
static constexpr jint kArtTiVersion = JVMTI_VERSION_1_2 | 0x40000000;

// The extension that lets us change the VLOG flags.
static constexpr std::string_view kSetVerboseExtensionName =
    "com.android.art.misc.set_verbose_flag_ext";

// Extension prototype
using SetVerboseFlagExt = jvmtiError (*)(jvmtiEnv*, const char*, jboolean);

template <typename T>
static inline jvmtiError Deallocate(jvmtiEnv* env, T* mem) {
  return env->Deallocate(reinterpret_cast<unsigned char*>(mem));
}

template <typename T>
void Dealloc(jvmtiEnv* env, T* t) {
  env->Deallocate(reinterpret_cast<unsigned char*>(t));
}

template <typename T, typename... Rest>
void Dealloc(jvmtiEnv* env, T* t, Rest... rs) {
  Dealloc(env, t);
  Dealloc(env, rs...);
}

void DeallocParams(jvmtiEnv* env, jvmtiParamInfo* params, jint n_params) {
  for (jint i = 0; i < n_params; i++) {
    Dealloc(env, params[i].name);
  }
}

template <typename T>
T GetExtensionFunction(jvmtiEnv* jvmti, const std::string_view& name) {
  jint n_ext = 0;
  void* res = nullptr;
  jvmtiExtensionFunctionInfo* infos = nullptr;
  if (jvmti->GetExtensionFunctions(&n_ext, &infos) != JVMTI_ERROR_NONE) {
    LOG(FATAL) << "Unable to get extensions";
  }
  for (jint i = 0; i < n_ext; i++) {
    const jvmtiExtensionFunctionInfo& info = infos[i];
    if (name == info.id) {
      res = reinterpret_cast<void*>(info.func);
    }
    DeallocParams(jvmti, info.params, info.param_count);
    Dealloc(jvmti, info.short_description, info.errors, info.id, info.params);
  }
  Dealloc(jvmti, infos);
  return reinterpret_cast<T>(res);
}

static jint SetupJvmtiEnv(JavaVM* vm, jvmtiEnv** jvmti) {
  jint res = 0;
  res = vm->GetEnv(reinterpret_cast<void**>(jvmti), JVMTI_VERSION_1_1);

  if (res != JNI_OK || *jvmti == nullptr) {
    LOG(ERROR) << "Unable to access JVMTI, error code " << res;
    return vm->GetEnv(reinterpret_cast<void**>(jvmti), kArtTiVersion);
  }
  return res;
}

}  // namespace

static jint AgentStart(JavaVM* vm, char* options, void* reserved ATTRIBUTE_UNUSED) {
  jvmtiEnv* jvmti = nullptr;
  if (SetupJvmtiEnv(vm, &jvmti) != JNI_OK) {
    LOG(ERROR) << "Could not get JVMTI env or ArtTiEnv!";
    return JNI_ERR;
  }
  SetVerboseFlagExt svfe = GetExtensionFunction<SetVerboseFlagExt>(jvmti, kSetVerboseExtensionName);
  if (svfe == nullptr) {
    LOG(ERROR) << "Could not find extension " << kSetVerboseExtensionName;
    return JNI_ERR;
  } else if (svfe(jvmti, options, true) != JVMTI_ERROR_NONE) {
    return JNI_ERR;
  } else {
    return JNI_OK;
  }
}

// Late attachment (e.g. 'am attach-agent').
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char* options, void* reserved) {
  return AgentStart(vm, options, reserved);
}

// Early attachment
extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* jvm, char* options, void* reserved) {
  return AgentStart(jvm, options, reserved);
}

}  // namespace enablevlog
