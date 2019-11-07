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

#include <atomic>
#include <fstream>
#include <iostream>
#include <istream>
#include <iomanip>
#include <jni.h>
#include <jvmti.h>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sstream>
#include <vector>

namespace tifast {

namespace {

// Special art ti-version number. We will use this as a fallback if we cannot get a regular JVMTI
// env.
static constexpr jint kArtTiVersion = JVMTI_VERSION_1_2 | 0x40000000;

// jthread is a typedef of jobject so we use this to allow the templates to distinguish them.
struct jthreadContainer { jthread thread; };
// jlocation is a typedef of jlong so use this to distinguish the less common jlong.
struct jlongContainer { jlong val; };

static void DeleteLocalRef(JNIEnv* env, jobject obj) {
  if (obj != nullptr && env != nullptr) {
    env->DeleteLocalRef(obj);
  }
}

class ScopedThreadInfo {
 public:
  ScopedThreadInfo(jvmtiEnv* jvmtienv, JNIEnv* env, jthread thread)
      : jvmtienv_(jvmtienv), env_(env), free_name_(false) {
    if (thread == nullptr) {
      info_.name = const_cast<char*>("<NULLPTR>");
    } else if (jvmtienv->GetThreadInfo(thread, &info_) != JVMTI_ERROR_NONE) {
      info_.name = const_cast<char*>("<UNKNOWN THREAD>");
    } else {
      free_name_ = true;
    }
  }

  ~ScopedThreadInfo() {
    if (free_name_) {
      jvmtienv_->Deallocate(reinterpret_cast<unsigned char*>(info_.name));
    }
    DeleteLocalRef(env_, info_.thread_group);
    DeleteLocalRef(env_, info_.context_class_loader);
  }

  const char* GetName() const {
    return info_.name;
  }

 private:
  jvmtiEnv* jvmtienv_;
  JNIEnv* env_;
  bool free_name_;
  jvmtiThreadInfo info_{};
};

class ScopedClassInfo {
 public:
  ScopedClassInfo(jvmtiEnv* jvmtienv, jclass c) : jvmtienv_(jvmtienv), class_(c) {}

  ~ScopedClassInfo() {
    if (class_ != nullptr) {
      jvmtienv_->Deallocate(reinterpret_cast<unsigned char*>(name_));
      jvmtienv_->Deallocate(reinterpret_cast<unsigned char*>(generic_));
      jvmtienv_->Deallocate(reinterpret_cast<unsigned char*>(file_));
      jvmtienv_->Deallocate(reinterpret_cast<unsigned char*>(debug_ext_));
    }
  }

  bool Init(bool get_generic = true) {
    if (class_ == nullptr) {
      name_ = const_cast<char*>("<NONE>");
      generic_ = const_cast<char*>("<NONE>");
      return true;
    } else {
      jvmtiError ret1 = jvmtienv_->GetSourceFileName(class_, &file_);
      jvmtiError ret2 = jvmtienv_->GetSourceDebugExtension(class_, &debug_ext_);
      char** gen_ptr = &generic_;
      if (!get_generic) {
        generic_ = nullptr;
        gen_ptr = nullptr;
      }
      return jvmtienv_->GetClassSignature(class_, &name_, gen_ptr) == JVMTI_ERROR_NONE &&
          ret1 != JVMTI_ERROR_MUST_POSSESS_CAPABILITY &&
          ret1 != JVMTI_ERROR_INVALID_CLASS &&
          ret2 != JVMTI_ERROR_MUST_POSSESS_CAPABILITY &&
          ret2 != JVMTI_ERROR_INVALID_CLASS;
    }
  }

  jclass GetClass() const {
    return class_;
  }

  const char* GetName() const {
    return name_;
  }

  const char* GetGeneric() const {
    return generic_;
  }

  const char* GetSourceDebugExtension() const {
    if (debug_ext_ == nullptr) {
      return "<UNKNOWN_SOURCE_DEBUG_EXTENSION>";
    } else {
      return debug_ext_;
    }
  }
  const char* GetSourceFileName() const {
    if (file_ == nullptr) {
      return "<UNKNOWN_FILE>";
    } else {
      return file_;
    }
  }

 private:
  jvmtiEnv* jvmtienv_;
  jclass class_;
  char* name_ = nullptr;
  char* generic_ = nullptr;
  char* file_ = nullptr;
  char* debug_ext_ = nullptr;

  friend std::ostream& operator<<(std::ostream &os, ScopedClassInfo const& m);
};

class ScopedMethodInfo {
 public:
  ScopedMethodInfo(jvmtiEnv* jvmtienv, JNIEnv* env, jmethodID m)
      : jvmtienv_(jvmtienv), env_(env), method_(m) {}

  ~ScopedMethodInfo() {
    DeleteLocalRef(env_, declaring_class_);
    jvmtienv_->Deallocate(reinterpret_cast<unsigned char*>(name_));
    jvmtienv_->Deallocate(reinterpret_cast<unsigned char*>(signature_));
    jvmtienv_->Deallocate(reinterpret_cast<unsigned char*>(generic_));
  }

  bool Init(bool get_generic = true) {
    if (jvmtienv_->GetMethodDeclaringClass(method_, &declaring_class_) != JVMTI_ERROR_NONE) {
      return false;
    }
    class_info_.reset(new ScopedClassInfo(jvmtienv_, declaring_class_));
    jint nlines;
    jvmtiLineNumberEntry* lines;
    jvmtiError err = jvmtienv_->GetLineNumberTable(method_, &nlines, &lines);
    if (err == JVMTI_ERROR_NONE) {
      if (nlines > 0) {
        first_line_ = lines[0].line_number;
      }
      jvmtienv_->Deallocate(reinterpret_cast<unsigned char*>(lines));
    } else if (err != JVMTI_ERROR_ABSENT_INFORMATION &&
               err != JVMTI_ERROR_NATIVE_METHOD) {
      return false;
    }
    return class_info_->Init(get_generic) &&
        (jvmtienv_->GetMethodName(method_, &name_, &signature_, &generic_) == JVMTI_ERROR_NONE);
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

  jint GetFirstLine() const {
    return first_line_;
  }

 private:
  jvmtiEnv* jvmtienv_;
  JNIEnv* env_;
  jmethodID method_;
  jclass declaring_class_ = nullptr;
  std::unique_ptr<ScopedClassInfo> class_info_;
  char* name_ = nullptr;
  char* signature_ = nullptr;
  char* generic_ = nullptr;
  jint first_line_ = -1;
};

std::ostream& operator<<(std::ostream &os, ScopedClassInfo const& c) {
  const char* generic = c.GetGeneric();
  if (generic != nullptr) {
    return os << c.GetName() << "<" << generic << ">" << " file: " << c.GetSourceFileName();
  } else {
    return os << c.GetName() << " file: " << c.GetSourceFileName();
  }
}

class LockedStream {
 public:
  explicit LockedStream(const std::string& filepath) {
    stream_.open(filepath, std::ofstream::out);
    if (!stream_.is_open()) {
      LOG(ERROR) << "====== JVMTI FAILED TO OPEN LOG FILE";
    }
  }
  ~LockedStream() {
    stream_.close();
  }
  void Write(const std::string& str) {
    stream_ << str;
    stream_.flush();
  }
 private:
  std::ofstream stream_;
};

static LockedStream* stream = nullptr;

class UniqueStringTable {
 public:
  UniqueStringTable() = default;
  ~UniqueStringTable() = default;
  std::string Intern(const std::string& header, const std::string& key) {
    if (map_.find(key) == map_.end()) {
      map_[key] = next_index_;
      // Emit definition line.  E.g., =123,string
      stream->Write(header + std::to_string(next_index_) + "," + key + "\n");
      ++next_index_;
    }
    return std::to_string(map_[key]);
  }
 private:
  int32_t next_index_;
  std::map<std::string, int32_t> map_;
};

static UniqueStringTable* string_table = nullptr;

// Formatter for the thread, type, and size of an allocation.
static std::string formatAllocation(jvmtiEnv* jvmti,
                                    JNIEnv* jni,
                                    jthreadContainer thr,
                                    jclass klass,
                                    jlongContainer size) {
  ScopedThreadInfo sti(jvmti, jni, thr.thread);
  std::ostringstream allocation;
  allocation << "jthread[" << sti.GetName() << "]";
  ScopedClassInfo sci(jvmti, klass);
  if (sci.Init(/*get_generic=*/false)) {
    allocation << ", jclass[" << sci << "]";
  } else {
    allocation << ", jclass[TYPE UNKNOWN]";
  }
  allocation << ", size[" << size.val << ", hex: 0x" << std::hex << size.val << "]";
  return string_table->Intern("+", allocation.str());
}

// Formatter for a method entry on a call stack.
static std::string formatMethod(jvmtiEnv* jvmti, JNIEnv* jni, jmethodID method_id) {
  ScopedMethodInfo smi(jvmti, jni, method_id);
  std::string method;
  if (smi.Init(/*get_generic=*/false)) {
    method = std::string(smi.GetDeclaringClassInfo().GetName()) +
        "::" + smi.GetName() + smi.GetSignature();
  } else {
    method = "ERROR";
  }
  return string_table->Intern("+", method);
}

static int sampling_rate;
static int stack_depth_limit;

static void JNICALL logVMObjectAlloc(jvmtiEnv* jvmti,
                                     JNIEnv* jni,
                                     jthread thread,
                                     jobject obj ATTRIBUTE_UNUSED,
                                     jclass klass,
                                     jlong size) {
  // Sample only once out of sampling_rate tries, and prevent recursive allocation tracking,
  static thread_local int sample_countdown = sampling_rate;
  --sample_countdown;
  if (sample_countdown != 0) {
    return;
  }

  // Guard accesses to string table and emission.
  static std::mutex mutex;
  std::lock_guard<std::mutex> lg(mutex);

  std::string record =
      formatAllocation(jvmti,
                       jni,
                       jthreadContainer{.thread = thread},
                       klass,
                       jlongContainer{.val = size});

  std::unique_ptr<jvmtiFrameInfo[]> stack_frames(new jvmtiFrameInfo[stack_depth_limit]);
  jint stack_depth;
  jvmtiError err = jvmti->GetStackTrace(thread,
                                        0,
                                        stack_depth_limit,
                                        stack_frames.get(),
                                        &stack_depth);
  if (err == JVMTI_ERROR_NONE) {
    // Emit stack frames in order from deepest in the stack to most recent.
    // This simplifies post-collection processing.
    for (int i = stack_depth - 1; i >= 0; --i) {
      record += ";" + formatMethod(jvmti, jni, stack_frames[i].method);
    }
  }
  stream->Write(string_table->Intern("=", record) + "\n");

  sample_countdown = sampling_rate;
}

static jvmtiEventCallbacks kLogCallbacks {
  .VMObjectAlloc = logVMObjectAlloc,
};

static jint SetupJvmtiEnv(JavaVM* vm, jvmtiEnv** jvmti) {
  jint res = vm->GetEnv(reinterpret_cast<void**>(jvmti), JVMTI_VERSION_1_1);
  if (res != JNI_OK || *jvmti == nullptr) {
    LOG(ERROR) << "Unable to access JVMTI, error code " << res;
    return vm->GetEnv(reinterpret_cast<void**>(jvmti), kArtTiVersion);
  }
  return res;
}

}  // namespace

static jvmtiError SetupCapabilities(jvmtiEnv* jvmti) {
  jvmtiCapabilities caps{};
  caps.can_generate_vm_object_alloc_events = 1;
  caps.can_get_line_numbers = 1;
  caps.can_get_source_file_name = 1;
  caps.can_get_source_debug_extension = 1;
  return jvmti->AddCapabilities(&caps);
}

static bool ProcessOptions(std::string options) {
  std::string output_file_path;
  if (options.empty()) {
    static constexpr int kDefaultSamplingRate = 10;
    static constexpr int kDefaultStackDepthLimit = 50;
    static constexpr const char* kDefaultOutputFilePath = "/data/local/tmp/logstream.txt";

    sampling_rate = kDefaultSamplingRate;
    stack_depth_limit = kDefaultStackDepthLimit;
    output_file_path = kDefaultOutputFilePath;
  } else {
    // options string should contain "sampling_rate,stack_depth_limit,output_file_path".
    size_t comma_pos = options.find(',');
    if (comma_pos == std::string::npos) {
      return false;
    }
    sampling_rate = std::stoi(options.substr(0, comma_pos));
    options = options.substr(comma_pos + 1);
    comma_pos = options.find(',');
    if (comma_pos == std::string::npos) {
      return false;
    }
    stack_depth_limit = std::stoi(options.substr(0, comma_pos));
    output_file_path = options.substr(comma_pos + 1);
  }
  LOG(INFO) << "Starting allocation tracing: sampling_rate=" << sampling_rate
            << ", stack_depth_limit=" << stack_depth_limit
            << ", output_file_path=" << output_file_path;
  stream = new LockedStream(output_file_path);

  return true;
}

static jint AgentStart(JavaVM* vm,
                       char* options,
                       void* reserved ATTRIBUTE_UNUSED) {
  // Handle the sampling rate, depth limit, and output path, if set.
  if (!ProcessOptions(options)) {
    return JNI_ERR;
  }

  // Create the environment.
  jvmtiEnv* jvmti = nullptr;
  if (SetupJvmtiEnv(vm, &jvmti) != JNI_OK) {
    LOG(ERROR) << "Could not get JVMTI env or ArtTiEnv!";
    return JNI_ERR;
  }

  jvmtiError error = SetupCapabilities(jvmti);
  if (error != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to set caps";
    return JNI_ERR;
  }

  // Add callbacks and notification.
  error = jvmti->SetEventCallbacks(&kLogCallbacks, static_cast<jint>(sizeof(kLogCallbacks)));
  if (error != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to set event callbacks.";
    return JNI_ERR;
  }
  error = jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                          JVMTI_EVENT_VM_OBJECT_ALLOC,
                                          nullptr /* all threads */);
  if (error != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to enable event " << JVMTI_EVENT_VM_OBJECT_ALLOC;
    return JNI_ERR;
  }

  string_table = new UniqueStringTable();

  return JNI_OK;
}

// Late attachment (e.g. 'am attach-agent').
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *vm, char* options, void* reserved) {
  return AgentStart(vm, options, reserved);
}

// Early attachment
extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* jvm, char* options, void* reserved) {
  return AgentStart(jvm, options, reserved);
}

}  // namespace tifast

