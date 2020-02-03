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

#include "hidden_api.h"
#include "hidden_api_jni.h"
#include "jni.h"
#include "runtime.h"

#include "nativehelper/JNIHelp.h"
#include "nativehelper/ScopedLocalRef.h"
#include "nativehelper/ScopedUtfChars.h"

class TestLibraryPathClassifier : public art::hiddenapi::JniLibraryPathClassifier {
 public:
  std::optional<art::hiddenapi::SharedObjectKind> Classify(const char* so_path) override {
    // so_path is the path to a shared object. We have the filename minus suffix (expected to be
    // .so).
    const char* last_separator = strrchr(so_path, '/');
    std::string_view filename = (last_separator != nullptr) ? last_separator + 1 : so_path;
    if (filename == so_name_) {
      return so_kind_;
    }
    return {};
  }

  void Configure(const char* so_file, art::hiddenapi::SharedObjectKind kind) {
    so_name_ = so_file;
    so_kind_ = kind;
  }

 private:
  std::string so_name_;
  art::hiddenapi::SharedObjectKind so_kind_ = art::hiddenapi::SharedObjectKind::kOther;
};

static TestLibraryPathClassifier* GetLibraryPathClassifier() {
  static TestLibraryPathClassifier g_classifier;
  return &g_classifier;
}

static void InstallLibraryPathClassifier(JNIEnv* env,
                                         jstring j_library_path,
                                         art::hiddenapi::SharedObjectKind kind) {
  ScopedUtfChars library_path(env, j_library_path);
  const char* last_separator = strrchr(library_path.c_str(), '/');
  const char* library_so = (last_separator != nullptr) ? last_separator + 1 : library_path.c_str();
  GetLibraryPathClassifier()->Configure(library_so, kind);
  art::hiddenapi::JniInitializeNativeCallerCheck(GetLibraryPathClassifier());
}

extern "C" JNIEXPORT void JNICALL Java_Main_treatAsArtModule(JNIEnv* env,
                                                             jclass /*klass*/,
                                                             jstring library_name) {
  InstallLibraryPathClassifier(env, library_name, art::hiddenapi::SharedObjectKind::kArtModule);
}

extern "C" JNIEXPORT void JNICALL Java_Main_treatAsOtherLibrary(JNIEnv* env,
                                                                jclass /*klass*/,
                                                                jstring library_name) {
  InstallLibraryPathClassifier(env, library_name, art::hiddenapi::SharedObjectKind::kOther);
}
