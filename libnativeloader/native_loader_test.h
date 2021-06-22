/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef ART_LIBNATIVELOADER_NATIVE_LOADER_TEST_H_
#define ART_LIBNATIVELOADER_NATIVE_LOADER_TEST_H_

#include <string>
#include <unordered_map>

#include <android-base/stringprintf.h>
#include <gmock/gmock.h>
#include <jni.h>

#include "native_loader_namespace.h"
#include "nativeloader/dlext_namespaces.h"

namespace android {
namespace nativeloader {

using ::testing::Return;
using ::testing::_;

// gmock interface that represents interested platform APIs on libdl_android and libnativebridge
class Platform {
 public:
  virtual ~Platform() {}

  // These mock_* are the APIs semantically the same across libdl_android and libnativebridge.
  // Instead of having two set of mock APIs for the two, define only one set with an additional
  // argument 'bool bridged' to identify the context (i.e., called for libdl_android or
  // libnativebridge).
  typedef char* mock_namespace_handle;
  virtual bool mock_init_anonymous_namespace(bool bridged, const char* sonames,
                                             const char* search_paths) = 0;
  virtual mock_namespace_handle mock_create_namespace(
      bool bridged, const char* name, const char* ld_library_path, const char* default_library_path,
      uint64_t type, const char* permitted_when_isolated_path, mock_namespace_handle parent) = 0;
  virtual bool mock_link_namespaces(bool bridged, mock_namespace_handle from,
                                    mock_namespace_handle to, const char* sonames) = 0;
  virtual mock_namespace_handle mock_get_exported_namespace(bool bridged, const char* name) = 0;
  virtual void* mock_dlopen_ext(bool bridged, const char* filename, int flags,
                                mock_namespace_handle ns) = 0;

  // libnativebridge APIs for which libdl_android has no corresponding APIs
  virtual bool NativeBridgeInitialized() = 0;
  virtual const char* NativeBridgeGetError() = 0;
  virtual bool NativeBridgeIsPathSupported(const char*) = 0;
  virtual bool NativeBridgeIsSupported(const char*) = 0;

  // To mock "ClassLoader Object.getParent()"
  virtual const char* JniObject_getParent(const char*) = 0;
};

// The mock does not actually create a namespace object. But simply casts the pointer to the
// string for the namespace name as the handle to the namespace object.
#define TO_ANDROID_NAMESPACE(str) \
  reinterpret_cast<struct android_namespace_t*>(const_cast<char*>(str))

#define TO_BRIDGED_NAMESPACE(str) \
  reinterpret_cast<struct native_bridge_namespace_t*>(const_cast<char*>(str))

#define TO_MOCK_NAMESPACE(ns) reinterpret_cast<Platform::mock_namespace_handle>(ns)

// These represents built-in namespaces created by the linker according to ld.config.txt
static std::unordered_map<std::string, Platform::mock_namespace_handle> namespaces = {
#define NAMESPACE_ENTRY(ns) {ns, TO_MOCK_NAMESPACE(TO_ANDROID_NAMESPACE(ns))}
  NAMESPACE_ENTRY("com_android_i18n"),
  NAMESPACE_ENTRY("com_android_neuralnetworks"),
  NAMESPACE_ENTRY("com_android_art"),

  // TODO(b/191644631) This can be removed when the test becomes more test-friendly.
  // This is added so that the test can exercise the JNI lib related behavior.
  NAMESPACE_ENTRY("com_android_conscrypt"),

  NAMESPACE_ENTRY("default"),
  NAMESPACE_ENTRY("sphal"),
  NAMESPACE_ENTRY("system"),
  NAMESPACE_ENTRY("vndk"),
  NAMESPACE_ENTRY("vndk_product"),
#undef NAMESPACE_ENTRY
};

// The actual gmock object
class MockPlatform : public Platform {
 public:
  explicit MockPlatform(bool is_bridged) : is_bridged_(is_bridged) {
    ON_CALL(*this, NativeBridgeIsSupported(_)).WillByDefault(Return(is_bridged_));
    ON_CALL(*this, NativeBridgeIsPathSupported(_)).WillByDefault(Return(is_bridged_));
    ON_CALL(*this, mock_get_exported_namespace(_, _))
        .WillByDefault(testing::Invoke([](bool, const char* name) -> mock_namespace_handle {
          if (namespaces.find(name) != namespaces.end()) {
            return namespaces[name];
          }
          std::string msg = android::base::StringPrintf("(namespace %s not found)", name);
          // The strdup'ed string will leak, but the test is already failing if we get here.
          return TO_MOCK_NAMESPACE(TO_ANDROID_NAMESPACE(strdup(msg.c_str())));
        }));
  }

  // Mocking the common APIs
  MOCK_METHOD3(mock_init_anonymous_namespace, bool(bool, const char*, const char*));
  MOCK_METHOD7(mock_create_namespace,
               mock_namespace_handle(bool, const char*, const char*, const char*, uint64_t,
                                     const char*, mock_namespace_handle));
  MOCK_METHOD4(mock_link_namespaces,
               bool(bool, mock_namespace_handle, mock_namespace_handle, const char*));
  MOCK_METHOD2(mock_get_exported_namespace, mock_namespace_handle(bool, const char*));
  MOCK_METHOD4(mock_dlopen_ext, void*(bool, const char*, int, mock_namespace_handle));

  // Mocking libnativebridge APIs
  MOCK_METHOD0(NativeBridgeInitialized, bool());
  MOCK_METHOD0(NativeBridgeGetError, const char*());
  MOCK_METHOD1(NativeBridgeIsPathSupported, bool(const char*));
  MOCK_METHOD1(NativeBridgeIsSupported, bool(const char*));

  // Mocking "ClassLoader Object.getParent()"
  MOCK_METHOD1(JniObject_getParent, const char*(const char*));

 private:
  bool is_bridged_;
};

static std::unique_ptr<MockPlatform> mock;

// Provide C wrappers for the mock object. These symbols must be exported by ld
// to be able to override the real symbols in the shared libs.
extern "C" {

// libdl_android APIs

bool android_init_anonymous_namespace(const char* sonames, const char* search_path) {
  return mock->mock_init_anonymous_namespace(false, sonames, search_path);
}

struct android_namespace_t* android_create_namespace(const char* name, const char* ld_library_path,
                                                     const char* default_library_path,
                                                     uint64_t type,
                                                     const char* permitted_when_isolated_path,
                                                     struct android_namespace_t* parent) {
  return TO_ANDROID_NAMESPACE(
      mock->mock_create_namespace(false, name, ld_library_path, default_library_path, type,
                                  permitted_when_isolated_path, TO_MOCK_NAMESPACE(parent)));
}

bool android_link_namespaces(struct android_namespace_t* from, struct android_namespace_t* to,
                             const char* sonames) {
  return mock->mock_link_namespaces(false, TO_MOCK_NAMESPACE(from), TO_MOCK_NAMESPACE(to), sonames);
}

struct android_namespace_t* android_get_exported_namespace(const char* name) {
  return TO_ANDROID_NAMESPACE(mock->mock_get_exported_namespace(false, name));
}

void* android_dlopen_ext(const char* filename, int flags, const android_dlextinfo* info) {
  return mock->mock_dlopen_ext(false, filename, flags, TO_MOCK_NAMESPACE(info->library_namespace));
}

// libnativebridge APIs

bool NativeBridgeIsSupported(const char* libpath) {
  return mock->NativeBridgeIsSupported(libpath);
}

struct native_bridge_namespace_t* NativeBridgeGetExportedNamespace(const char* name) {
  return TO_BRIDGED_NAMESPACE(mock->mock_get_exported_namespace(true, name));
}

struct native_bridge_namespace_t* NativeBridgeCreateNamespace(
    const char* name, const char* ld_library_path, const char* default_library_path, uint64_t type,
    const char* permitted_when_isolated_path, struct native_bridge_namespace_t* parent) {
  return TO_BRIDGED_NAMESPACE(
      mock->mock_create_namespace(true, name, ld_library_path, default_library_path, type,
                                  permitted_when_isolated_path, TO_MOCK_NAMESPACE(parent)));
}

bool NativeBridgeLinkNamespaces(struct native_bridge_namespace_t* from,
                                struct native_bridge_namespace_t* to, const char* sonames) {
  return mock->mock_link_namespaces(true, TO_MOCK_NAMESPACE(from), TO_MOCK_NAMESPACE(to), sonames);
}

void* NativeBridgeLoadLibraryExt(const char* libpath, int flag,
                                 struct native_bridge_namespace_t* ns) {
  return mock->mock_dlopen_ext(true, libpath, flag, TO_MOCK_NAMESPACE(ns));
}

bool NativeBridgeInitialized() {
  return mock->NativeBridgeInitialized();
}

bool NativeBridgeInitAnonymousNamespace(const char* public_ns_sonames,
                                        const char* anon_ns_library_path) {
  return mock->mock_init_anonymous_namespace(true, public_ns_sonames, anon_ns_library_path);
}

const char* NativeBridgeGetError() {
  return mock->NativeBridgeGetError();
}

bool NativeBridgeIsPathSupported(const char* path) {
  return mock->NativeBridgeIsPathSupported(path);
}

}  // extern "C"

// A very simple JNI mock.
// jstring is a pointer to utf8 char array. We don't need utf16 char here.
// jobject, jclass, and jmethodID are also a pointer to utf8 char array
// Only a few JNI methods that are actually used in libnativeloader are mocked.
JNINativeInterface* CreateJNINativeInterface() {
  JNINativeInterface* inf = new JNINativeInterface();
  memset(inf, 0, sizeof(JNINativeInterface));

  inf->GetStringUTFChars = [](JNIEnv*, jstring s, jboolean*) -> const char* {
    return reinterpret_cast<const char*>(s);
  };

  inf->ReleaseStringUTFChars = [](JNIEnv*, jstring, const char*) -> void { return; };

  inf->NewStringUTF = [](JNIEnv*, const char* bytes) -> jstring {
    return reinterpret_cast<jstring>(const_cast<char*>(bytes));
  };

  inf->FindClass = [](JNIEnv*, const char* name) -> jclass {
    return reinterpret_cast<jclass>(const_cast<char*>(name));
  };

  inf->CallObjectMethodV = [](JNIEnv*, jobject obj, jmethodID mid, va_list) -> jobject {
    if (strcmp("getParent", reinterpret_cast<const char*>(mid)) == 0) {
      // JniObject_getParent can be a valid jobject or nullptr if there is
      // no parent classloader.
      const char* ret = mock->JniObject_getParent(reinterpret_cast<const char*>(obj));
      return reinterpret_cast<jobject>(const_cast<char*>(ret));
    }
    return nullptr;
  };

  inf->GetMethodID = [](JNIEnv*, jclass, const char* name, const char*) -> jmethodID {
    return reinterpret_cast<jmethodID>(const_cast<char*>(name));
  };

  inf->NewWeakGlobalRef = [](JNIEnv*, jobject obj) -> jobject { return obj; };

  inf->IsSameObject = [](JNIEnv*, jobject a, jobject b) -> jboolean {
    return strcmp(reinterpret_cast<const char*>(a), reinterpret_cast<const char*>(b)) == 0;
  };

  return inf;
}

}  // namespace nativeloader
}  // namespace android

#endif  // ART_LIBNATIVELOADER_NATIVE_LOADER_TEST_H_
