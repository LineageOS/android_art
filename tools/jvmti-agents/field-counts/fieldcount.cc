// Copyright (C) 2018 The Android Open Source Project
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
#include <nativehelper/scoped_local_ref.h>

#include <atomic>
#include <iomanip>
#include <iostream>
#include <istream>
#include <jni.h>
#include <jvmti.h>
#include <memory>
#include <sstream>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace fieldnull {

#define CHECK_JVMTI(x) CHECK_EQ((x), JVMTI_ERROR_NONE)

// Special art ti-version number. We will use this as a fallback if we cannot get a regular JVMTI
// env.
static constexpr jint kArtTiVersion = JVMTI_VERSION_1_2 | 0x40000000;

static JavaVM* java_vm = nullptr;

// Field is "Lclass/name/here;.field_name:Lfield/type/here;"
static std::pair<jclass, jfieldID> SplitField(JNIEnv* env, const std::string& field_id) {
  CHECK_EQ(field_id[0], 'L');
  env->PushLocalFrame(1);
  std::istringstream is(field_id);
  std::string class_name;
  std::string field_name;
  std::string field_type;

  std::getline(is, class_name, '.');
  std::getline(is, field_name, ':');
  std::getline(is, field_type, '\0');

  jclass klass = reinterpret_cast<jclass>(
      env->NewGlobalRef(env->FindClass(class_name.substr(1, class_name.size() - 2).c_str())));
  CHECK(klass != nullptr) << class_name;
  jfieldID field = env->GetFieldID(klass, field_name.c_str(), field_type.c_str());
  CHECK(field != nullptr) << field_name;
  LOG(INFO) << "listing field " << field_id;
  env->PopLocalFrame(nullptr);
  return std::make_pair(klass, field);
}

static std::vector<std::pair<jclass, jfieldID>> GetRequestedFields(JNIEnv* env,
                                                                   const std::string& args) {
  std::vector<std::pair<jclass, jfieldID>> res;
  std::stringstream args_stream(args);
  std::string item;
  while (std::getline(args_stream, item, ',')) {
    if (item == "") {
      continue;
    }
    res.push_back(SplitField(env, item));
  }
  return res;
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

struct RequestList {
  std::vector<std::pair<jclass, jfieldID>> fields_;
};

static void DataDumpRequestCb(jvmtiEnv* jvmti) {
  JNIEnv* env = nullptr;
  CHECK_EQ(java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6), JNI_OK);
  LOG(INFO) << "Dumping counts of fields.";
  LOG(INFO) << "\t" << "Field name"
            << "\t" << "Type"
            << "\t" << "Count"
            << "\t" << "TotalSize";
  RequestList* list;
  CHECK_JVMTI(jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&list)));
  for (std::pair<jclass, jfieldID>& p : list->fields_) {
    jclass klass = p.first;
    jfieldID field = p.second;
    // Make sure all instances of the class are tagged with the klass ptr value. Since this is a
    // global ref it's guaranteed to be unique.
    CHECK_JVMTI(jvmti->IterateOverInstancesOfClass(
        p.first,
        // We need to do this to all objects every time since we might be looking for multiple
        // fields in classes that are subtypes of each other.
        JVMTI_HEAP_OBJECT_EITHER,
        /* class_tag, size, tag_ptr, user_data*/
        [](jlong, jlong, jlong* tag_ptr, void* klass) -> jvmtiIterationControl {
          *tag_ptr = static_cast<jlong>(reinterpret_cast<intptr_t>(klass));
          return JVMTI_ITERATION_CONTINUE;
        },
        klass));
    jobject* obj_list;
    jint obj_len;
    jlong tag = static_cast<jlong>(reinterpret_cast<intptr_t>(klass));
    CHECK_JVMTI(jvmti->GetObjectsWithTags(1, &tag, &obj_len, &obj_list, nullptr));

    std::unordered_map<std::string, size_t> class_sizes;
    std::unordered_map<std::string, size_t> class_counts;
    size_t total_size = 0;
    // Mark all the referenced objects with a single tag value, this way we can dedup them.
    jlong referenced_object_tag = static_cast<jlong>(reinterpret_cast<intptr_t>(klass) + 1);
    std::string null_class_name("<null>");
    class_counts[null_class_name] = 0;
    class_sizes[null_class_name] = 0;
    for (jint i = 0; i < obj_len; i++) {
      ScopedLocalRef<jobject> cur_thiz(env, obj_list[i]);
      ScopedLocalRef<jobject> obj(env, env->GetObjectField(cur_thiz.get(), field));
      std::string class_name(null_class_name);
      if (obj == nullptr) {
        class_counts[null_class_name]++;
      } else {
        CHECK_JVMTI(jvmti->SetTag(obj.get(), referenced_object_tag));
        jlong size = 0;
        if (obj.get() != nullptr) {
          char* class_name_tmp;
          ScopedLocalRef<jclass> obj_klass(env, env->GetObjectClass(obj.get()));
          CHECK_JVMTI(jvmti->GetClassSignature(obj_klass.get(), &class_name_tmp, nullptr));
          CHECK_JVMTI(jvmti->GetObjectSize(obj.get(), &size));
          class_name = class_name_tmp;
          CHECK_JVMTI(jvmti->Deallocate(reinterpret_cast<unsigned char*>(class_name_tmp)));
        }
        if (class_sizes.find(class_name) == class_counts.end()) {
          class_sizes[class_name] = 0;
          class_counts[class_name] = 0;
        }
        class_counts[class_name]++;
      }
    }
    jobject* ref_list;
    jint ref_len;
    CHECK_JVMTI(jvmti->GetObjectsWithTags(1, &referenced_object_tag, &ref_len, &ref_list, nullptr));
    for (jint i = 0; i < ref_len; i++) {
      ScopedLocalRef<jobject> obj(env, ref_list[i]);
      std::string class_name(null_class_name);
      jlong size = 0;
      if (obj.get() != nullptr) {
        char* class_name_tmp;
        ScopedLocalRef<jclass> obj_klass(env, env->GetObjectClass(obj.get()));
        CHECK_JVMTI(jvmti->GetClassSignature(obj_klass.get(), &class_name_tmp, nullptr));
        CHECK_JVMTI(jvmti->GetObjectSize(obj.get(), &size));
        class_name = class_name_tmp;
        CHECK_JVMTI(jvmti->Deallocate(reinterpret_cast<unsigned char*>(class_name_tmp)));
      }
      total_size += static_cast<size_t>(size);
      class_sizes[class_name] += static_cast<size_t>(size);
    }

    char* field_name;
    char* field_sig;
    char* field_class_name;
    CHECK_JVMTI(jvmti->GetFieldName(klass, field, &field_name, &field_sig, nullptr));
    CHECK_JVMTI(jvmti->GetClassSignature(klass, &field_class_name, nullptr));
    LOG(INFO) << "\t" << field_class_name << "." << field_name << ":" << field_sig
              << "\t" << "<ALL_TYPES>"
              << "\t" << obj_len
              << "\t" << total_size;
    for (auto sz : class_sizes) {
      size_t count = class_counts[sz.first];
      LOG(INFO) << "\t" << field_class_name << "." << field_name << ":" << field_sig
                << "\t" << sz.first
                << "\t" << count
                << "\t" << sz.second;
    }
    CHECK_JVMTI(jvmti->Deallocate(reinterpret_cast<unsigned char*>(field_name)));
    CHECK_JVMTI(jvmti->Deallocate(reinterpret_cast<unsigned char*>(field_sig)));
    CHECK_JVMTI(jvmti->Deallocate(reinterpret_cast<unsigned char*>(field_class_name)));
  }
}

static void VMDeathCb(jvmtiEnv* jvmti, JNIEnv* env ATTRIBUTE_UNUSED) {
  DataDumpRequestCb(jvmti);
  RequestList* list = nullptr;
  CHECK_JVMTI(jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&list)));
  delete list;
}

static void CreateFieldList(jvmtiEnv* jvmti, JNIEnv* env, const std::string& args) {
  RequestList* list = nullptr;
  CHECK_JVMTI(jvmti->Allocate(sizeof(*list), reinterpret_cast<unsigned char**>(&list)));
  new (list) RequestList{
    .fields_ = GetRequestedFields(env, args),
  };
  CHECK_JVMTI(jvmti->SetEnvironmentLocalStorage(list));
}

static void VMInitCb(jvmtiEnv* jvmti, JNIEnv* env, jobject thr ATTRIBUTE_UNUSED) {
  char* args = nullptr;
  CHECK_JVMTI(jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&args)));
  CHECK_JVMTI(jvmti->SetEnvironmentLocalStorage(nullptr));
  CreateFieldList(jvmti, env, args);
  CHECK_JVMTI(jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, nullptr));
  CHECK_JVMTI(
      jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_DATA_DUMP_REQUEST, nullptr));
  CHECK_JVMTI(jvmti->Deallocate(reinterpret_cast<unsigned char*>(args)));
}

static jint AgentStart(JavaVM* vm, char* options, bool is_onload) {
  android::base::InitLogging(/* argv= */ nullptr);
  java_vm = vm;
  jvmtiEnv* jvmti = nullptr;
  if (SetupJvmtiEnv(vm, &jvmti) != JNI_OK) {
    LOG(ERROR) << "Could not get JVMTI env or ArtTiEnv!";
    return JNI_ERR;
  }
  jvmtiCapabilities caps{
    .can_tag_objects = 1,
  };
  CHECK_JVMTI(jvmti->AddCapabilities(&caps));
  jvmtiEventCallbacks cb{
    .VMInit = VMInitCb,
    .VMDeath = VMDeathCb,
    .DataDumpRequest = DataDumpRequestCb,
  };
  CHECK_JVMTI(jvmti->SetEventCallbacks(&cb, sizeof(cb)));
  if (is_onload) {
    unsigned char* ptr = nullptr;
    CHECK_JVMTI(jvmti->Allocate(strlen(options) + 1, &ptr));
    strcpy(reinterpret_cast<char*>(ptr), options);
    CHECK_JVMTI(jvmti->SetEnvironmentLocalStorage(ptr));
    CHECK_JVMTI(jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, nullptr));
  } else {
    JNIEnv* env = nullptr;
    CHECK_EQ(vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6), JNI_OK);
    CreateFieldList(jvmti, env, options);
    CHECK_JVMTI(jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, nullptr));
    CHECK_JVMTI(
        jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_DATA_DUMP_REQUEST, nullptr));
  }
  return JNI_OK;
}

// Late attachment (e.g. 'am attach-agent').
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm,
                                                 char* options,
                                                 void* reserved ATTRIBUTE_UNUSED) {
  return AgentStart(vm, options, /*is_onload=*/false);
}

// Early attachment
extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* jvm,
                                               char* options,
                                               void* reserved ATTRIBUTE_UNUSED) {
  return AgentStart(jvm, options, /*is_onload=*/true);
}

}  // namespace fieldnull
