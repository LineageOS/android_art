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

#include <queue>
#include <vector>

#include "jvmti.h"

// Test infrastructure
#include "jvmti_helper.h"
#include "nativehelper/scoped_local_ref.h"
#include "nativehelper/scoped_primitive_array.h"
#include "test_env.h"

namespace art {
namespace Test1940DdmExt {

using DdmHandleChunk = jvmtiError(*)(jvmtiEnv* env,
                                     jint type_in,
                                     jint len_in,
                                     const jbyte* data_in,
                                     jint* type_out,
                                     jint* len_data_out,
                                     jbyte** data_out);

struct DdmCallbackData {
 public:
  DdmCallbackData(jint type, jint size, jbyte* data) : type_(type), data_(data, data + size) {}
  jint type_;
  std::vector<jbyte> data_;
};
struct DdmsTrackingData {
  DdmHandleChunk send_ddm_chunk;
  jrawMonitorID callback_mon;
  std::queue<DdmCallbackData> callbacks_received;
};

template <typename T>
static void Dealloc(T* t) {
  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(t));
}

template <typename T, typename ...Rest>
static void Dealloc(T* t, Rest... rs) {
  Dealloc(t);
  Dealloc(rs...);
}

extern "C" JNIEXPORT jobject JNICALL Java_art_Test1940_processChunk(JNIEnv* env,
                                                                    jclass,
                                                                    jobject chunk) {
  DdmsTrackingData* data = nullptr;
  if (JvmtiErrorToException(
      env, jvmti_env, jvmti_env->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)))) {
    return nullptr;
  }
  CHECK(chunk != nullptr);
  CHECK(data != nullptr);
  CHECK(data->send_ddm_chunk != nullptr);
  ScopedLocalRef<jclass> chunk_class(env, env->FindClass("org/apache/harmony/dalvik/ddmc/Chunk"));
  if (env->ExceptionCheck()) {
    return nullptr;
  }
  jfieldID type_field_id = env->GetFieldID(chunk_class.get(), "type", "I");
  jfieldID offset_field_id = env->GetFieldID(chunk_class.get(), "offset", "I");
  jfieldID length_field_id = env->GetFieldID(chunk_class.get(), "length", "I");
  jfieldID data_field_id = env->GetFieldID(chunk_class.get(), "data", "[B");
  jint type = env->GetIntField(chunk, type_field_id);
  jint off = env->GetIntField(chunk, offset_field_id);
  jint len = env->GetIntField(chunk, length_field_id);
  ScopedLocalRef<jbyteArray> chunk_buf(
      env, reinterpret_cast<jbyteArray>(env->GetObjectField(chunk, data_field_id)));
  if (env->ExceptionCheck()) {
    return nullptr;
  }
  ScopedByteArrayRO byte_data(env, chunk_buf.get());
  jint out_type;
  jint out_size;
  jbyte* out_data;
  if (JvmtiErrorToException(env, jvmti_env, data->send_ddm_chunk(jvmti_env,
                                                                 type,
                                                                 len,
                                                                 &byte_data[off],
                                                                 /*out*/&out_type,
                                                                 /*out*/&out_size,
                                                                 /*out*/&out_data))) {
    return nullptr;
  } else {
    ScopedLocalRef<jbyteArray> chunk_data(env, env->NewByteArray(out_size));
    env->SetByteArrayRegion(chunk_data.get(), 0, out_size, out_data);
    Dealloc(out_data);
    ScopedLocalRef<jobject> res(env, env->NewObject(chunk_class.get(),
                                                    env->GetMethodID(chunk_class.get(),
                                                                     "<init>",
                                                                     "(I[BII)V"),
                                                    out_type,
                                                    chunk_data.get(),
                                                    0,
                                                    out_size));
    return res.release();
  }
}

static void DeallocParams(jvmtiParamInfo* params, jint n_params) {
  for (jint i = 0; i < n_params; i++) {
    Dealloc(params[i].name);
  }
}

extern "C" JNIEXPORT void JNICALL Java_art_Test1940_publishListen(JNIEnv* env,
                                                                  jclass test_klass,
                                                                  jobject publish) {
  jmethodID publish_method = env->FromReflectedMethod(publish);
  DdmsTrackingData* data = nullptr;
  if (JvmtiErrorToException(
          env, jvmti_env, jvmti_env->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)))) {
    return;
  }
  std::vector<DdmCallbackData> callbacks;
  while (true) {
    if (JvmtiErrorToException(env, jvmti_env, jvmti_env->RawMonitorEnter(data->callback_mon))) {
      return;
    }
    while (data->callbacks_received.empty()) {
      if (JvmtiErrorToException(env, jvmti_env, jvmti_env->RawMonitorWait(data->callback_mon, 0))) {
        CHECK_EQ(JVMTI_ERROR_NONE, jvmti_env->RawMonitorExit(data->callback_mon));
        return;
      }
    }
    while (!data->callbacks_received.empty()) {
      callbacks.emplace_back(std::move(data->callbacks_received.front()));
      data->callbacks_received.pop();
    }
    if (JvmtiErrorToException(env, jvmti_env, jvmti_env->RawMonitorExit(data->callback_mon))) {
      return;
    }
    for (auto cb : callbacks) {
      ScopedLocalRef<jbyteArray> res(env, env->NewByteArray(cb.data_.size()));
      env->SetByteArrayRegion(res.get(), 0, cb.data_.size(), cb.data_.data());
      env->CallStaticVoidMethod(test_klass, publish_method, cb.type_, res.get());
    }
    callbacks.clear();
  }
}

static void JNICALL PublishCB(jvmtiEnv* jvmti, jint type, jint size, jbyte* bytes) {
  DdmsTrackingData* data = nullptr;
  CHECK_EQ(JVMTI_ERROR_NONE, jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)));
  CHECK_EQ(JVMTI_ERROR_NONE, jvmti->RawMonitorEnter(data->callback_mon));
  data->callbacks_received.emplace(type, size, bytes);
  CHECK_EQ(JVMTI_ERROR_NONE, jvmti->RawMonitorNotifyAll(data->callback_mon));
  CHECK_EQ(JVMTI_ERROR_NONE, jvmti->RawMonitorExit(data->callback_mon));
}

extern "C" JNIEXPORT void JNICALL Java_art_Test1940_initializeTest(JNIEnv* env, jclass) {
  void* old_data = nullptr;
  if (JvmtiErrorToException(env, jvmti_env, jvmti_env->GetEnvironmentLocalStorage(&old_data))) {
    return;
  } else if (old_data != nullptr) {
    ScopedLocalRef<jclass> rt_exception(env, env->FindClass("java/lang/RuntimeException"));
    env->ThrowNew(rt_exception.get(), "Environment already has local storage set!");
    return;
  }
  void* mem = nullptr;
  if (JvmtiErrorToException(env,
                            jvmti_env,
                            jvmti_env->Allocate(sizeof(DdmsTrackingData),
                                                reinterpret_cast<unsigned char**>(&mem)))) {
    return;
  }
  DdmsTrackingData* data = new (mem) DdmsTrackingData{};
  if (JvmtiErrorToException(
          env, jvmti_env, jvmti_env->CreateRawMonitor("callback-mon", &data->callback_mon))) {
    return;
  }
  // Get the extensions.
  jint n_ext = 0;
  jvmtiExtensionFunctionInfo* infos = nullptr;
  if (JvmtiErrorToException(env, jvmti_env, jvmti_env->GetExtensionFunctions(&n_ext, &infos))) {
    return;
  }
  for (jint i = 0; i < n_ext; i++) {
    jvmtiExtensionFunctionInfo* cur_info = &infos[i];
    if (strcmp("com.android.art.internal.ddm.process_chunk", cur_info->id) == 0) {
      data->send_ddm_chunk = reinterpret_cast<DdmHandleChunk>(cur_info->func);
    }
    // Cleanup the cur_info
    DeallocParams(cur_info->params, cur_info->param_count);
    Dealloc(cur_info->id, cur_info->short_description, cur_info->params, cur_info->errors);
  }
  // Cleanup the array.
  Dealloc(infos);
  if (data->send_ddm_chunk == nullptr) {
    ScopedLocalRef<jclass> rt_exception(env, env->FindClass("java/lang/RuntimeException"));
    env->ThrowNew(rt_exception.get(), "Unable to find memory tracking extensions.");
    return;
  }
  if (JvmtiErrorToException(env, jvmti_env, jvmti_env->SetEnvironmentLocalStorage(data))) {
    return;
  }

  jint event_index = -1;
  bool found_event = false;
  jvmtiExtensionEventInfo* events = nullptr;
  if (JvmtiErrorToException(env, jvmti_env, jvmti_env->GetExtensionEvents(&n_ext, &events))) {
    return;
  }
  for (jint i = 0; i < n_ext; i++) {
    jvmtiExtensionEventInfo* cur_info = &events[i];
    if (strcmp("com.android.art.internal.ddm.publish_chunk_safe", cur_info->id) == 0) {
      found_event = true;
      event_index = cur_info->extension_event_index;
    }
    // Cleanup the cur_info
    DeallocParams(cur_info->params, cur_info->param_count);
    Dealloc(cur_info->id, cur_info->short_description, cur_info->params);
  }
  // Cleanup the array.
  Dealloc(events);
  if (!found_event) {
    ScopedLocalRef<jclass> rt_exception(env, env->FindClass("java/lang/RuntimeException"));
    env->ThrowNew(rt_exception.get(), "Unable to find ddms extension event.");
    return;
  }
  JvmtiErrorToException(env,
                        jvmti_env,
                        jvmti_env->SetExtensionEventCallback(
                            event_index, reinterpret_cast<jvmtiExtensionEvent>(PublishCB)));
  return;
}

}  // namespace Test1940DdmExt
}  // namespace art
