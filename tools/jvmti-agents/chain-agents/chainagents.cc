// Copyright (C) 2017 The Android Open Source Project
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
#include <dlfcn.h>
#include <jni.h>
#include <jvmti.h>

#include <atomic>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chainagents {

static constexpr const char* kChainFile = "chain_agents.txt";
static constexpr const char* kOnLoad = "Agent_OnLoad";
static constexpr const char* kOnAttach = "Agent_OnAttach";
static constexpr const char* kOnUnload = "Agent_OnUnload";
using AgentLoadFunction = jint (*)(JavaVM*, const char*, void*);
using AgentUnloadFunction = jint (*)(JavaVM*);

// Global namespace. Shared by every usage of this wrapper unfortunately.
// We need to keep track of them to call Agent_OnUnload.
static std::mutex unload_mutex;

struct Unloader {
  AgentUnloadFunction unload;
};
static std::vector<Unloader> unload_functions;

enum class StartType {
  OnAttach,
  OnLoad,
};

static std::pair<std::string, std::string> Split(std::string source, char delim) {
  std::string first(source.substr(0, source.find(delim)));
  if (source.find(delim) == std::string::npos) {
    return std::pair(first, "");
  } else {
    return std::pair(first, source.substr(source.find(delim) + 1));
  }
}

static jint Load(StartType start,
                 JavaVM* vm,
                 void* reserved,
                 const std::pair<std::string, std::string>& lib_and_args,
                 /*out*/ std::string* err) {
  void* handle = dlopen(lib_and_args.first.c_str(), RTLD_LAZY);
  std::ostringstream oss;
  if (handle == nullptr) {
    oss << "Failed to dlopen due to " << dlerror();
    *err = oss.str();
    return JNI_ERR;
  }
  AgentLoadFunction alf = reinterpret_cast<AgentLoadFunction>(
      dlsym(handle, start == StartType::OnLoad ? kOnLoad : kOnAttach));
  if (alf == nullptr) {
    oss << "Failed to dlsym " << (start == StartType::OnLoad ? kOnLoad : kOnAttach) << " due to "
        << dlerror();
    *err = oss.str();
    return JNI_ERR;
  }
  jint res = alf(vm, lib_and_args.second.c_str(), reserved);
  if (res != JNI_OK) {
    *err = "load function failed!";
    return res;
  }
  AgentUnloadFunction auf = reinterpret_cast<AgentUnloadFunction>(dlsym(handle, kOnUnload));
  if (auf != nullptr) {
    unload_functions.push_back({ auf });
  }
  return JNI_OK;
}

static jint AgentStart(StartType start, JavaVM* vm, char* options, void* reserved) {
  std::string input_file(options);
  input_file = input_file + "/" + kChainFile;
  std::ifstream input(input_file);
  std::string line;
  std::lock_guard<std::mutex> mu(unload_mutex);
  while (std::getline(input, line)) {
    std::pair<std::string, std::string> lib_and_args(Split(line, '='));
    std::string err;
    jint new_res = Load(start, vm, reserved, lib_and_args, &err);
    if (new_res != JNI_OK) {
      PLOG(WARNING) << "Failed to load library " << lib_and_args.first
                    << " (arguments: " << lib_and_args.second << ") due to " << err;
    }
  }
  return JNI_OK;
}

// Late attachment (e.g. 'am attach-agent').
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char* options, void* reserved) {
  return AgentStart(StartType::OnAttach, vm, options, reserved);
}

// Early attachment
// (e.g. 'java
// -agentpath:/path/to/libwrapagentproperties.so=/path/to/propfile,/path/to/wrapped.so=[ops]').
extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* jvm, char* options, void* reserved) {
  return AgentStart(StartType::OnLoad, jvm, options, reserved);
}

extern "C" JNIEXPORT void JNICALL Agent_OnUnload(JavaVM* jvm) {
  std::lock_guard<std::mutex> lk(unload_mutex);
  for (const Unloader& u : unload_functions) {
    u.unload(jvm);
    // Don't dlclose since some agents expect to still have code loaded after this.
  }
  unload_functions.clear();
}

}  // namespace chainagents
