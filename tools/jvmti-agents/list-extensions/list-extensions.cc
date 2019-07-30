// Copyright (C) 2019 The Android Open Source Project
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
//

#include <android-base/logging.h>

#include <jni.h>
#include <jvmti.h>
#include <string>

namespace listextensions {

namespace {

// Special art ti-version number. We will use this as a fallback if we cannot get a regular JVMTI
// env.
constexpr jint kArtTiVersion = JVMTI_VERSION_1_2 | 0x40000000;

template <typename T> void Dealloc(jvmtiEnv* env, T* t) {
  env->Deallocate(reinterpret_cast<unsigned char*>(t));
}

template <typename T, typename... Rest> void Dealloc(jvmtiEnv* env, T* t, Rest... rs) {
  Dealloc(env, t);
  Dealloc(env, rs...);
}

void DeallocParams(jvmtiEnv* env, jvmtiParamInfo* params, jint n_params) {
  for (jint i = 0; i < n_params; i++) {
    Dealloc(env, params[i].name);
  }
}

std::ostream& operator<<(std::ostream& os, const jvmtiParamInfo& param) {
  os << param.name << " (";
#define CASE(type, name)       \
  case JVMTI_##type##_##name:  \
    os << #name;               \
    break
  switch (param.kind) {
    CASE(KIND, IN);
    CASE(KIND, IN_PTR);
    CASE(KIND, IN_BUF);
    CASE(KIND, ALLOC_BUF);
    CASE(KIND, ALLOC_ALLOC_BUF);
    CASE(KIND, OUT);
    CASE(KIND, OUT_BUF);
  }
  os << ", ";
  switch (param.base_type) {
    CASE(TYPE, JBYTE);
    CASE(TYPE, JCHAR);
    CASE(TYPE, JSHORT);
    CASE(TYPE, JINT);
    CASE(TYPE, JLONG);
    CASE(TYPE, JFLOAT);
    CASE(TYPE, JDOUBLE);
    CASE(TYPE, JBOOLEAN);
    CASE(TYPE, JOBJECT);
    CASE(TYPE, JTHREAD);
    CASE(TYPE, JCLASS);
    CASE(TYPE, JVALUE);
    CASE(TYPE, JFIELDID);
    CASE(TYPE, JMETHODID);
    CASE(TYPE, CCHAR);
    CASE(TYPE, CVOID);
    CASE(TYPE, JNIENV);
  }
#undef CASE
  os << ")";
  return os;
}

jint SetupJvmtiEnv(JavaVM* vm) {
  jint res = 0;
  jvmtiEnv* env = nullptr;
  res = vm->GetEnv(reinterpret_cast<void**>(&env), JVMTI_VERSION_1_1);

  if (res != JNI_OK || env == nullptr) {
    LOG(ERROR) << "Unable to access JVMTI, error code " << res;
    res = vm->GetEnv(reinterpret_cast<void**>(&env), kArtTiVersion);
    if (res != JNI_OK) {
      return res;
    }
  }

  // Get the extensions.
  jint n_ext = 0;
  jvmtiExtensionFunctionInfo* infos = nullptr;
  if (env->GetExtensionFunctions(&n_ext, &infos) != JVMTI_ERROR_NONE) {
    return JNI_ERR;
  }
  LOG(INFO) << "Found " << n_ext << " extension functions";
  for (jint i = 0; i < n_ext; i++) {
    const jvmtiExtensionFunctionInfo& info = infos[i];
    LOG(INFO) << info.id;
    LOG(INFO) << "\tdesc: " << info.short_description;
    LOG(INFO) << "\targuments: (count: " << info.param_count << ")";
    for (jint j = 0; j < info.param_count; j++) {
      const jvmtiParamInfo& param = info.params[j];
      LOG(INFO) << "\t\t" << param;
    }
    LOG(INFO) << "\tErrors: (count: " << info.error_count << ")";
    for (jint j = 0; j < info.error_count; j++) {
      char* name;
      CHECK_EQ(JVMTI_ERROR_NONE, env->GetErrorName(info.errors[j], &name));
      LOG(INFO) << "\t\t" << name;
      Dealloc(env, name);
    }
    DeallocParams(env, info.params, info.param_count);
    Dealloc(env, info.short_description, info.id, info.errors, info.params);
  }
  // Cleanup the array.
  Dealloc(env, infos);
  jvmtiExtensionEventInfo* events = nullptr;
  if (env->GetExtensionEvents(&n_ext, &events) != JVMTI_ERROR_NONE) {
    return JNI_ERR;
  }
  LOG(INFO) << "Found " << n_ext << " extension events";
  for (jint i = 0; i < n_ext; i++) {
    const jvmtiExtensionEventInfo& info = events[i];
    LOG(INFO) << info.id;
    LOG(INFO) << "\tindex: " << info.extension_event_index;
    LOG(INFO) << "\tdesc: " << info.short_description;
    LOG(INFO) << "\tevent arguments: (count: " << info.param_count << ")";
    for (jint j = 0; j < info.param_count; j++) {
      const jvmtiParamInfo& param = info.params[j];
      LOG(INFO) << "\t\t" << param;
    }
    DeallocParams(env, info.params, info.param_count);
    Dealloc(env, info.short_description, info.id, info.params);
  }
  // Cleanup the array.
  Dealloc(env, events);
  env->DisposeEnvironment();
  return JNI_OK;
}

jint AgentStart(JavaVM* vm, char* options ATTRIBUTE_UNUSED, void* reserved ATTRIBUTE_UNUSED) {
  if (SetupJvmtiEnv(vm) != JNI_OK) {
    LOG(ERROR) << "Could not get JVMTI env or ArtTiEnv!";
    return JNI_ERR;
  }
  return JNI_OK;
}

}  // namespace

// Late attachment (e.g. 'am attach-agent').
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char* options, void* reserved) {
  return AgentStart(vm, options, reserved);
}

// Early attachment
extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* jvm, char* options, void* reserved) {
  return AgentStart(jvm, options, reserved);
}

}  // namespace listextensions
