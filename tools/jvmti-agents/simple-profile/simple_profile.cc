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
//

#include <android-base/logging.h>
#include <fcntl.h>
#include <jni.h>
#include <jvmti.h>

#include <atomic>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "android-base/unique_fd.h"
#include "nativehelper/scoped_local_ref.h"

namespace simple_profile {

static constexpr jint kArtTiVersion = JVMTI_VERSION_1_2 | 0x40000000;

#define CHECK_JVMTI(a) CHECK_EQ(JVMTI_ERROR_NONE, a)

struct DataDefinition {
  std::string_view class_name;
  std::string_view method_name;
  std::string_view method_descriptor;
  uint64_t count;
};

std::ostream& operator<<(std::ostream& os, const DataDefinition& dd) {
  return os << "{\"class_name\":\"" << dd.class_name << "\",\"method_name\":\"" << dd.method_name
            << "\",\"method_descriptor\":\"" << dd.method_descriptor << "\",\"count\":" << dd.count
            << "}";
}

class SimpleProfileData {
 public:
  SimpleProfileData(
      jvmtiEnv* env, std::string out_fd_name, int fd, bool dump_on_shutdown, bool dump_on_main_stop)
      : dump_id_(0),
        out_fd_name_(out_fd_name),
        out_fd_(fd),
        shutdown_(false),
        dump_on_shutdown_(dump_on_shutdown || dump_on_main_stop),
        dump_on_main_stop_(dump_on_main_stop) {
    CHECK_JVMTI(env->CreateRawMonitor("simple_profile_mon", &mon_));
    method_counts_.reserve(10000);
  }

  void Dump(jvmtiEnv* jvmti);
  void Enter(jvmtiEnv* jvmti, JNIEnv* env, jmethodID meth);

  void RunDumpLoop(jvmtiEnv* jvmti, JNIEnv* env);

  static SimpleProfileData* GetProfileData(jvmtiEnv* env) {
    void* data;
    CHECK_JVMTI(env->GetEnvironmentLocalStorage(&data));
    return static_cast<SimpleProfileData*>(data);
  }

  void FinishInitialization(jvmtiEnv* jvmti, JNIEnv* jni, jthread cur);
  void Shutdown(jvmtiEnv* jvmti, JNIEnv* jni);

 private:
  void DoDump(jvmtiEnv* jvmti, JNIEnv* jni, std::unordered_map<jmethodID, uint64_t> copy);

  jlong dump_id_;
  jrawMonitorID mon_;
  std::string out_fd_name_;
  int out_fd_;
  std::unordered_map<jmethodID, uint64_t> method_counts_;
  bool shutdown_;
  bool dump_on_shutdown_;
  bool dump_on_main_stop_;
};

struct ScopedJvmtiMonitor {
 public:
  ScopedJvmtiMonitor(jvmtiEnv* env, jrawMonitorID mon) : jvmti_(env), mon_(mon) {
    CHECK_JVMTI(jvmti_->RawMonitorEnter(mon_));
  }

  ~ScopedJvmtiMonitor() {
    CHECK_JVMTI(jvmti_->RawMonitorExit(mon_));
  }

  void Notify() {
    CHECK_JVMTI(jvmti_->RawMonitorNotifyAll(mon_));
  }

  void Wait() {
    CHECK_JVMTI(jvmti_->RawMonitorWait(mon_, 0));
  }

 private:
  jvmtiEnv* jvmti_;
  jrawMonitorID mon_;
};

void SimpleProfileData::Enter(jvmtiEnv* jvmti, JNIEnv* env, jmethodID meth) {
  ScopedJvmtiMonitor sjm(jvmti, mon_);
  // Keep all classes from being unloaded to allow us to know we can get the method info later.
  jclass tmp;
  CHECK_JVMTI(jvmti->GetMethodDeclaringClass(meth, &tmp));
  ScopedLocalRef<jclass> klass(env, tmp);
  jlong tag;
  CHECK_JVMTI(jvmti->GetTag(klass.get(), &tag));
  if (tag == 0) {
    CHECK_JVMTI(jvmti->SetTag(klass.get(), 1u));
    env->NewGlobalRef(klass.get());
  }
  method_counts_.insert({ meth, 0u }).first->second++;
}

void SimpleProfileData::Dump(jvmtiEnv* jvmti) {
  ScopedJvmtiMonitor sjm(jvmti, mon_);
  dump_id_++;
  sjm.Notify();
}

void SimpleProfileData::RunDumpLoop(jvmtiEnv* jvmti, JNIEnv* env) {
  jlong current_id = 0;
  do {
    std::unordered_map<jmethodID, uint64_t> copy;
    {
      ScopedJvmtiMonitor sjm(jvmti, mon_);
      while (!shutdown_ && current_id == dump_id_) {
        sjm.Wait();
      }
      if (shutdown_) {
        break;
      }
      current_id = dump_id_;
      copy = method_counts_;
    }
    DoDump(jvmti, env, std::move(copy));
  } while (true);
}

void SimpleProfileData::Shutdown(jvmtiEnv* jvmti, JNIEnv* jni) {
  std::unordered_map<jmethodID, uint64_t> copy;
  {
    ScopedJvmtiMonitor sjm(jvmti, mon_);
    if (shutdown_) {
      return;
    }
    shutdown_ = true;
    copy = method_counts_;
    sjm.Notify();
  }
  if (dump_on_shutdown_) {
    DoDump(jvmti, jni, std::move(copy));
  }
}

void SimpleProfileData::FinishInitialization(jvmtiEnv* jvmti, JNIEnv* env, jthread cur) {
  // Finish up startup.
  // Create a Thread object.
  std::string name = std::string("profile dump Thread: ") + this->out_fd_name_;
  ScopedLocalRef<jobject> thread_name(env, env->NewStringUTF(name.c_str()));
  CHECK_NE(thread_name.get(), nullptr);

  ScopedLocalRef<jclass> thread_klass(env, env->FindClass("java/lang/Thread"));
  CHECK_NE(thread_klass.get(), nullptr);
  ScopedLocalRef<jobject> thread(env, env->AllocObject(thread_klass.get()));
  CHECK_NE(thread.get(), nullptr);
  jmethodID initID = env->GetMethodID(thread_klass.get(), "<init>", "(Ljava/lang/String;)V");
  jmethodID setDaemonId = env->GetMethodID(thread_klass.get(), "setDaemon", "(Z)V");
  CHECK_NE(initID, nullptr);
  CHECK_NE(setDaemonId, nullptr);
  env->CallNonvirtualVoidMethod(thread.get(), thread_klass.get(), initID, thread_name.get());
  env->CallVoidMethod(thread.get(), setDaemonId, JNI_TRUE);
  CHECK(!env->ExceptionCheck());

  CHECK_JVMTI(jvmti->RunAgentThread(
      thread.get(),
      [](jvmtiEnv* jvmti, JNIEnv* jni, void* unused_data ATTRIBUTE_UNUSED) {
        SimpleProfileData* data = SimpleProfileData::GetProfileData(jvmti);
        data->RunDumpLoop(jvmti, jni);
      },
      nullptr,
      JVMTI_THREAD_NORM_PRIORITY));

  CHECK_JVMTI(jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr));
  CHECK_JVMTI(
      jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_DATA_DUMP_REQUEST, nullptr));
  if (dump_on_main_stop_) {
    CHECK_JVMTI(jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_THREAD_END, cur));
  }
  CHECK_JVMTI(jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, nullptr));
}

class ScopedClassInfo {
 public:
  ScopedClassInfo(jvmtiEnv* jvmti_env, jclass c)
      : jvmti_env_(jvmti_env), class_(c), name_(nullptr), generic_(nullptr) {}

  ~ScopedClassInfo() {
    if (class_ != nullptr) {
      jvmti_env_->Deallocate(reinterpret_cast<unsigned char*>(name_));
      jvmti_env_->Deallocate(reinterpret_cast<unsigned char*>(generic_));
    }
  }

  bool Init() {
    if (class_ == nullptr) {
      name_ = const_cast<char*>("<NONE>");
      generic_ = const_cast<char*>("<NONE>");
      return true;
    } else {
      return jvmti_env_->GetClassSignature(class_, &name_, &generic_) == JVMTI_ERROR_NONE;
    }
  }

  jclass GetClass() const {
    return class_;
  }
  const char* GetName() const {
    return name_;
  }
  // Generic type parameters, whatever is in the <> for a class
  const char* GetGeneric() const {
    return generic_;
  }

 private:
  jvmtiEnv* jvmti_env_;
  jclass class_;
  char* name_;
  char* generic_;
};

class ScopedMethodInfo {
 public:
  ScopedMethodInfo(jvmtiEnv* jvmti_env, JNIEnv* env, jmethodID method)
      : jvmti_env_(jvmti_env),
        env_(env),
        method_(method),
        declaring_class_(nullptr),
        class_info_(nullptr),
        name_(nullptr),
        signature_(nullptr),
        generic_(nullptr) {}

  ~ScopedMethodInfo() {
    env_->DeleteLocalRef(declaring_class_);
    jvmti_env_->Deallocate(reinterpret_cast<unsigned char*>(name_));
    jvmti_env_->Deallocate(reinterpret_cast<unsigned char*>(signature_));
    jvmti_env_->Deallocate(reinterpret_cast<unsigned char*>(generic_));
  }

  bool Init() {
    if (jvmti_env_->GetMethodDeclaringClass(method_, &declaring_class_) != JVMTI_ERROR_NONE) {
      LOG(INFO) << "No decl";
      return false;
    }
    class_info_.reset(new ScopedClassInfo(jvmti_env_, declaring_class_));
    return class_info_->Init() &&
           (jvmti_env_->GetMethodName(method_, &name_, &signature_, &generic_) == JVMTI_ERROR_NONE);
  }

  const ScopedClassInfo& GetDeclaringClassInfo() const {
    return *class_info_;
  }

  jclass GetDeclaringClass() const {
    return declaring_class_;
  }

  const char* GetName() const {
    return name_;
  }

  const char* GetSignature() const {
    return signature_;
  }

  const char* GetGeneric() const {
    return generic_;
  }

 private:
  jvmtiEnv* jvmti_env_;
  JNIEnv* env_;
  jmethodID method_;
  jclass declaring_class_;
  std::unique_ptr<ScopedClassInfo> class_info_;
  char* name_;
  char* signature_;
  char* generic_;

  friend std::ostream& operator<<(std::ostream& os, ScopedMethodInfo const& method);
};

std::ostream& operator<<(std::ostream& os, const ScopedMethodInfo* method) {
  return os << *method;
}

std::ostream& operator<<(std::ostream& os, ScopedMethodInfo const& method) {
  return os << method.GetDeclaringClassInfo().GetName() << "->" << method.GetName()
            << method.GetSignature();
}

void SimpleProfileData::DoDump(jvmtiEnv* jvmti,
                               JNIEnv* jni,
                               std::unordered_map<jmethodID, uint64_t> copy) {
  std::ostringstream oss;
  oss << "[";
  bool is_first = true;
  for (auto [meth, cnt] : copy) {
    ScopedMethodInfo smi(jvmti, jni, meth);
    if (!smi.Init()) {
      continue;
    }
    if (!is_first) {
      oss << "," << std::endl;
    }
    is_first = false;
    oss << DataDefinition {
      .class_name = smi.GetDeclaringClassInfo().GetName(),
      .method_name = smi.GetName(),
      .method_descriptor = smi.GetSignature(),
      .count = cnt,
    };
  }
  oss << "]";
  CHECK_GE(TEMP_FAILURE_RETRY(write(out_fd_, oss.str().c_str(), oss.str().size())), 0)
      << strerror(errno) << out_fd_ << " " << out_fd_name_;
  fsync(out_fd_);
}

static void DataDumpCb(jvmtiEnv* jvmti_env) {
  SimpleProfileData* data = SimpleProfileData::GetProfileData(jvmti_env);
  data->Dump(jvmti_env);
}

static void MethodEntryCB(jvmtiEnv* jvmti_env,
                          JNIEnv* env,
                          jthread thread ATTRIBUTE_UNUSED,
                          jmethodID method) {
  SimpleProfileData* data = SimpleProfileData::GetProfileData(jvmti_env);
  data->Enter(jvmti_env, env, method);
}

static void VMInitCB(jvmtiEnv* jvmti, JNIEnv* env, jthread thr) {
  SimpleProfileData* data = SimpleProfileData::GetProfileData(jvmti);
  data->FinishInitialization(jvmti, env, thr);
}
static void VMDeathCB(jvmtiEnv* jvmti, JNIEnv* env) {
  SimpleProfileData* data = SimpleProfileData::GetProfileData(jvmti);
  data->Shutdown(jvmti, env);
}

// Fills targets with the breakpoints to add.
// Lname/of/Klass;->methodName(Lsig/of/Method)Lreturn/Type;@location,<...>
static bool ParseArgs(const std::string& start_options,
                      /*out*/ std::string* fd_name,
                      /*out*/ int* fd,
                      /*out*/ bool* dump_on_shutdown,
                      /*out*/ bool* dump_on_main_stop) {
  std::istringstream iss(start_options);
  std::string item;
  *dump_on_main_stop = false;
  *dump_on_shutdown = false;
  bool has_fd = false;
  while (std::getline(iss, item, ',')) {
    if (item == "dump_on_shutdown") {
      *dump_on_shutdown = true;
    } else if (item == "dump_on_main_stop") {
      *dump_on_main_stop = true;
    } else if (has_fd) {
      LOG(ERROR) << "Too many args!";
      return false;
    } else {
      has_fd = true;
      *fd_name = item;
      *fd = TEMP_FAILURE_RETRY(open(fd_name->c_str(), O_WRONLY | O_CLOEXEC | O_CREAT, 00666));
      CHECK_GE(*fd, 0) << strerror(errno);
    }
  }
  return has_fd;
}

enum class StartType {
  OnAttach,
  OnLoad,
};

static jint SetupJvmtiEnv(JavaVM* vm, jvmtiEnv** jvmti) {
  jint res = 0;
  res = vm->GetEnv(reinterpret_cast<void**>(jvmti), JVMTI_VERSION_1_1);

  if (res != JNI_OK || *jvmti == nullptr) {
    LOG(ERROR) << "Unable to access JVMTI, error code " << res;
    return vm->GetEnv(reinterpret_cast<void**>(jvmti), kArtTiVersion);
  }
  return res;
}

static jint AgentStart(StartType start,
                       JavaVM* vm,
                       const char* options,
                       void* reserved ATTRIBUTE_UNUSED) {
  if (options == nullptr) {
    options = "";
  }
  jvmtiEnv* jvmti = nullptr;
  jvmtiError error = JVMTI_ERROR_NONE;
  {
    jint res = 0;
    res = SetupJvmtiEnv(vm, &jvmti);

    if (res != JNI_OK || jvmti == nullptr) {
      LOG(ERROR) << "Unable to access JVMTI, error code " << res;
      return JNI_ERR;
    }
  }

  int fd;
  std::string fd_name;
  bool dump_on_shutdown;
  bool dump_on_main_stop;
  if (!ParseArgs(options,
                 /*out*/ &fd_name,
                 /*out*/ &fd,
                 /*out*/ &dump_on_shutdown,
                 /*out*/ &dump_on_main_stop)) {
    LOG(ERROR) << "failed to get output file from " << options << "!";
    return JNI_ERR;
  }

  void* data_mem = nullptr;
  error = jvmti->Allocate(sizeof(SimpleProfileData), reinterpret_cast<unsigned char**>(&data_mem));
  if (error != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to alloc memory for breakpoint target data";
    return JNI_ERR;
  }

  SimpleProfileData* data =
      new (data_mem) SimpleProfileData(jvmti, fd_name, fd, dump_on_shutdown, dump_on_main_stop);
  error = jvmti->SetEnvironmentLocalStorage(data);
  if (error != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to set local storage";
    return JNI_ERR;
  }

  jvmtiCapabilities caps {};
  caps.can_generate_method_entry_events = JNI_TRUE;
  caps.can_tag_objects = JNI_TRUE;
  error = jvmti->AddCapabilities(&caps);
  if (error != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to set caps";
    return JNI_ERR;
  }

  jvmtiEventCallbacks callbacks {};
  callbacks.MethodEntry = &MethodEntryCB;
  callbacks.VMInit = &VMInitCB;
  callbacks.DataDumpRequest = &DataDumpCb;
  callbacks.VMDeath = &VMDeathCB;
  callbacks.ThreadEnd = [](jvmtiEnv* env, JNIEnv* jni, jthread thr ATTRIBUTE_UNUSED) {
    VMDeathCB(env, jni);
  };

  error = jvmti->SetEventCallbacks(&callbacks, static_cast<jint>(sizeof(callbacks)));

  if (error != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to set event callbacks.";
    return JNI_ERR;
  }

  if (start == StartType::OnAttach) {
    JNIEnv* env = nullptr;
    jint res = 0;
    res = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_2);
    if (res != JNI_OK || env == nullptr) {
      LOG(ERROR) << "Unable to get jnienv";
      return JNI_ERR;
    }
    jthread temp;
    ScopedLocalRef<jthread> cur(env, nullptr);
    CHECK_JVMTI(jvmti->GetCurrentThread(&temp));
    cur.reset(temp);
    VMInitCB(jvmti, env, cur.get());
  } else {
    error = jvmti->SetEventNotificationMode(
        JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, nullptr /* all threads */);
    if (error != JVMTI_ERROR_NONE) {
      LOG(ERROR) << "Unable to set event vminit";
      return JNI_ERR;
    }
  }
  return JNI_OK;
}

// Late attachment (e.g. 'am attach-agent').
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char* options, void* reserved) {
  return AgentStart(StartType::OnAttach, vm, options, reserved);
}

// Early attachment
extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* jvm, char* options, void* reserved) {
  return AgentStart(StartType::OnLoad, jvm, options, reserved);
}

}  // namespace simple_profile
