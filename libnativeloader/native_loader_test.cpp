/*
 * Copyright (C) 2019 The Android Open Source Project
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

#if defined(ART_TARGET_ANDROID)

#include <dlfcn.h>
#include <memory>
#include <unordered_map>

#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <jni.h>

#include "native_loader_namespace.h"
#include "nativehelper/scoped_utf_chars.h"
#include "nativeloader/dlext_namespaces.h"
#include "nativeloader/native_loader.h"
#include "public_libraries.h"

namespace android {
namespace nativeloader {

using ::testing::Eq;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::_;
using internal::ConfigEntry;
using internal::ParseApexLibrariesConfig;
using internal::ParseConfig;

#if defined(__LP64__)
#define LIB_DIR "lib64"
#else
#define LIB_DIR "lib"
#endif

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

static void* const any_nonnull = reinterpret_cast<void*>(0x12345678);

// Custom matcher for comparing namespace handles
MATCHER_P(NsEq, other, "") {
  *result_listener << "comparing " << reinterpret_cast<const char*>(arg) << " and " << other;
  return strcmp(reinterpret_cast<const char*>(arg), reinterpret_cast<const char*>(other)) == 0;
}

/////////////////////////////////////////////////////////////////

// Test fixture
class NativeLoaderTest : public ::testing::TestWithParam<bool> {
 protected:
  bool IsBridged() { return GetParam(); }

  void SetUp() override {
    mock = std::make_unique<testing::NiceMock<MockPlatform>>(IsBridged());

    env = std::make_unique<JNIEnv>();
    env->functions = CreateJNINativeInterface();
  }

  void SetExpectations() {
    std::vector<std::string> default_public_libs =
        android::base::Split(preloadable_public_libraries(), ":");
    for (auto l : default_public_libs) {
      EXPECT_CALL(*mock,
                  mock_dlopen_ext(false, StrEq(l.c_str()), RTLD_NOW | RTLD_NODELETE, NotNull()))
          .WillOnce(Return(any_nonnull));
    }
  }

  void RunTest() { InitializeNativeLoader(); }

  void TearDown() override {
    ResetNativeLoader();
    delete env->functions;
    mock.reset();
  }

  std::unique_ptr<JNIEnv> env;
};

/////////////////////////////////////////////////////////////////

TEST_P(NativeLoaderTest, InitializeLoadsDefaultPublicLibraries) {
  SetExpectations();
  RunTest();
}

TEST_P(NativeLoaderTest, OpenNativeLibraryWithoutClassloaderInApex) {
  const char* test_lib_path = "libfoo.so";
  void* fake_handle = &fake_handle;  // Arbitrary non-null value
  EXPECT_CALL(*mock,
              mock_dlopen_ext(false, StrEq(test_lib_path), RTLD_NOW, NsEq("com_android_art")))
      .WillOnce(Return(fake_handle));

  bool needs_native_bridge = false;
  char* errmsg = nullptr;
  EXPECT_EQ(fake_handle,
            OpenNativeLibrary(env.get(),
                              /*target_sdk_version=*/17,
                              test_lib_path,
                              /*class_loader=*/nullptr,
                              /*caller_location=*/"/apex/com.android.art/javalib/myloadinglib.jar",
                              /*library_path=*/nullptr,
                              &needs_native_bridge,
                              &errmsg));
  // OpenNativeLibrary never uses nativebridge when there's no classloader. That
  // should maybe change.
  EXPECT_EQ(needs_native_bridge, false);
  EXPECT_EQ(errmsg, nullptr);
}

TEST_P(NativeLoaderTest, OpenNativeLibraryWithoutClassloaderInFramework) {
  const char* test_lib_path = "libfoo.so";
  void* fake_handle = &fake_handle;  // Arbitrary non-null value
  EXPECT_CALL(*mock, mock_dlopen_ext(false, StrEq(test_lib_path), RTLD_NOW, NsEq("system")))
      .WillOnce(Return(fake_handle));

  bool needs_native_bridge = false;
  char* errmsg = nullptr;
  EXPECT_EQ(fake_handle,
            OpenNativeLibrary(env.get(),
                              /*target_sdk_version=*/17,
                              test_lib_path,
                              /*class_loader=*/nullptr,
                              /*caller_location=*/"/system/framework/framework.jar!classes1.dex",
                              /*library_path=*/nullptr,
                              &needs_native_bridge,
                              &errmsg));
  // OpenNativeLibrary never uses nativebridge when there's no classloader. That
  // should maybe change.
  EXPECT_EQ(needs_native_bridge, false);
  EXPECT_EQ(errmsg, nullptr);
}

TEST_P(NativeLoaderTest, OpenNativeLibraryWithoutClassloaderAndCallerLocation) {
  const char* test_lib_path = "libfoo.so";
  void* fake_handle = &fake_handle;  // Arbitrary non-null value
  EXPECT_CALL(*mock, mock_dlopen_ext(false, StrEq(test_lib_path), RTLD_NOW, NsEq("system")))
      .WillOnce(Return(fake_handle));

  bool needs_native_bridge = false;
  char* errmsg = nullptr;
  EXPECT_EQ(fake_handle,
            OpenNativeLibrary(env.get(),
                              /*target_sdk_version=*/17,
                              test_lib_path,
                              /*class_loader=*/nullptr,
                              /*caller_location=*/nullptr,
                              /*library_path=*/nullptr,
                              &needs_native_bridge,
                              &errmsg));
  // OpenNativeLibrary never uses nativebridge when there's no classloader. That
  // should maybe change.
  EXPECT_EQ(needs_native_bridge, false);
  EXPECT_EQ(errmsg, nullptr);
}

INSTANTIATE_TEST_SUITE_P(NativeLoaderTests, NativeLoaderTest, testing::Bool());

/////////////////////////////////////////////////////////////////

class NativeLoaderTest_Create : public NativeLoaderTest {
 protected:
  // Test inputs (initialized to the default values). Overriding these
  // must be done before calling SetExpectations() and RunTest().
  uint32_t target_sdk_version = 29;
  std::string class_loader = "my_classloader";
  bool is_shared = false;
  std::string dex_path = "/data/app/foo/classes.dex";
  std::string library_path = "/data/app/foo/" LIB_DIR "/arm";
  std::string permitted_path = "/data/app/foo/" LIB_DIR;

  // expected output (.. for the default test inputs)
  std::string expected_namespace_name = "classloader-namespace";
  uint64_t expected_namespace_flags =
      ANDROID_NAMESPACE_TYPE_ISOLATED | ANDROID_NAMESPACE_TYPE_ALSO_USED_AS_ANONYMOUS;
  std::string expected_library_path = library_path;
  std::string expected_permitted_path = std::string("/data:/mnt/expand:") + permitted_path;
  std::string expected_parent_namespace = "system";
  bool expected_link_with_platform_ns = true;
  bool expected_link_with_art_ns = true;
  bool expected_link_with_i18n_ns = true;
  bool expected_link_with_sphal_ns = !vendor_public_libraries().empty();
  bool expected_link_with_vndk_ns = false;
  bool expected_link_with_vndk_product_ns = false;
  bool expected_link_with_default_ns = false;
  bool expected_link_with_neuralnetworks_ns = true;
  std::string expected_shared_libs_to_platform_ns = default_public_libraries();
  std::string expected_shared_libs_to_art_ns = apex_public_libraries().at("com_android_art");
  std::string expected_shared_libs_to_i18n_ns = apex_public_libraries().at("com_android_i18n");
  std::string expected_shared_libs_to_sphal_ns = vendor_public_libraries();
  std::string expected_shared_libs_to_vndk_ns = vndksp_libraries_vendor();
  std::string expected_shared_libs_to_vndk_product_ns = vndksp_libraries_product();
  std::string expected_shared_libs_to_default_ns = default_public_libraries();
  std::string expected_shared_libs_to_neuralnetworks_ns = apex_public_libraries().at("com_android_neuralnetworks");

  void SetExpectations() {
    NativeLoaderTest::SetExpectations();

    ON_CALL(*mock, JniObject_getParent(StrEq(class_loader))).WillByDefault(Return(nullptr));

    EXPECT_CALL(*mock, NativeBridgeIsPathSupported(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*mock, NativeBridgeInitialized()).Times(testing::AnyNumber());

    EXPECT_CALL(*mock, mock_create_namespace(
                           Eq(IsBridged()), StrEq(expected_namespace_name), nullptr,
                           StrEq(expected_library_path), expected_namespace_flags,
                           StrEq(expected_permitted_path), NsEq(expected_parent_namespace.c_str())))
        .WillOnce(Return(TO_MOCK_NAMESPACE(TO_ANDROID_NAMESPACE(dex_path.c_str()))));
    if (expected_link_with_platform_ns) {
      EXPECT_CALL(*mock, mock_link_namespaces(Eq(IsBridged()), _, NsEq("system"),
                                              StrEq(expected_shared_libs_to_platform_ns)))
          .WillOnce(Return(true));
    }
    if (expected_link_with_art_ns) {
      EXPECT_CALL(*mock, mock_link_namespaces(Eq(IsBridged()), _, NsEq("com_android_art"),
                                              StrEq(expected_shared_libs_to_art_ns)))
          .WillOnce(Return(true));
    }
    if (expected_link_with_i18n_ns) {
      EXPECT_CALL(*mock, mock_link_namespaces(Eq(IsBridged()), _, NsEq("com_android_i18n"),
                                              StrEq(expected_shared_libs_to_i18n_ns)))
          .WillOnce(Return(true));
    }
    if (expected_link_with_sphal_ns) {
      EXPECT_CALL(*mock, mock_link_namespaces(Eq(IsBridged()), _, NsEq("sphal"),
                                              StrEq(expected_shared_libs_to_sphal_ns)))
          .WillOnce(Return(true));
    }
    if (expected_link_with_vndk_ns) {
      EXPECT_CALL(*mock, mock_link_namespaces(Eq(IsBridged()), _, NsEq("vndk"),
                                              StrEq(expected_shared_libs_to_vndk_ns)))
          .WillOnce(Return(true));
    }
    if (expected_link_with_vndk_product_ns) {
      EXPECT_CALL(*mock, mock_link_namespaces(Eq(IsBridged()), _, NsEq("vndk_product"),
                                              StrEq(expected_shared_libs_to_vndk_product_ns)))
          .WillOnce(Return(true));
    }
    if (expected_link_with_default_ns) {
      EXPECT_CALL(*mock, mock_link_namespaces(Eq(IsBridged()), _, NsEq("default"),
                                              StrEq(expected_shared_libs_to_default_ns)))
          .WillOnce(Return(true));
    }
    if (expected_link_with_neuralnetworks_ns) {
      EXPECT_CALL(*mock, mock_link_namespaces(Eq(IsBridged()), _, NsEq("com_android_neuralnetworks"),
                                              StrEq(expected_shared_libs_to_neuralnetworks_ns)))
          .WillOnce(Return(true));
    }
  }

  void RunTest() {
    NativeLoaderTest::RunTest();

    jstring err = CreateClassLoaderNamespace(
        env(), target_sdk_version, env()->NewStringUTF(class_loader.c_str()), is_shared,
        env()->NewStringUTF(dex_path.c_str()), env()->NewStringUTF(library_path.c_str()),
        env()->NewStringUTF(permitted_path.c_str()), /*uses_library_list=*/ nullptr);

    // no error
    EXPECT_EQ(err, nullptr) << "Error is: " << std::string(ScopedUtfChars(env(), err).c_str());

    if (!IsBridged()) {
      struct android_namespace_t* ns =
          FindNamespaceByClassLoader(env(), env()->NewStringUTF(class_loader.c_str()));

      // The created namespace is for this apk
      EXPECT_EQ(dex_path.c_str(), reinterpret_cast<const char*>(ns));
    } else {
      struct NativeLoaderNamespace* ns =
          FindNativeLoaderNamespaceByClassLoader(env(), env()->NewStringUTF(class_loader.c_str()));

      // The created namespace is for the this apk
      EXPECT_STREQ(dex_path.c_str(),
                   reinterpret_cast<const char*>(ns->ToRawNativeBridgeNamespace()));
    }
  }

  JNIEnv* env() { return NativeLoaderTest::env.get(); }
};

TEST_P(NativeLoaderTest_Create, DownloadedApp) {
  SetExpectations();
  RunTest();
}

TEST_P(NativeLoaderTest_Create, BundledSystemApp) {
  dex_path = "/system/app/foo/foo.apk";
  is_shared = true;

  expected_namespace_name = "classloader-namespace-shared";
  expected_namespace_flags |= ANDROID_NAMESPACE_TYPE_SHARED;
  SetExpectations();
  RunTest();
}

TEST_P(NativeLoaderTest_Create, BundledVendorApp) {
  dex_path = "/vendor/app/foo/foo.apk";
  is_shared = true;

  expected_namespace_name = "classloader-namespace-shared";
  expected_namespace_flags |= ANDROID_NAMESPACE_TYPE_SHARED;
  SetExpectations();
  RunTest();
}

TEST_P(NativeLoaderTest_Create, UnbundledVendorApp) {
  dex_path = "/vendor/app/foo/foo.apk";
  is_shared = false;

  expected_namespace_name = "vendor-classloader-namespace";
  expected_library_path = expected_library_path + ":/vendor/" LIB_DIR;
  expected_permitted_path = expected_permitted_path + ":/vendor/" LIB_DIR;
  expected_shared_libs_to_platform_ns =
      expected_shared_libs_to_platform_ns + ":" + llndk_libraries_vendor();
  expected_link_with_vndk_ns = true;
  SetExpectations();
  RunTest();
}

TEST_P(NativeLoaderTest_Create, BundledProductApp) {
  dex_path = "/product/app/foo/foo.apk";
  is_shared = true;

  expected_namespace_name = "classloader-namespace-shared";
  expected_namespace_flags |= ANDROID_NAMESPACE_TYPE_SHARED;
  SetExpectations();
  RunTest();
}

TEST_P(NativeLoaderTest_Create, UnbundledProductApp) {
  dex_path = "/product/app/foo/foo.apk";
  is_shared = false;

  if (is_product_vndk_version_defined()) {
    expected_namespace_name = "vendor-classloader-namespace";
    expected_library_path = expected_library_path + ":/product/" LIB_DIR ":/system/product/" LIB_DIR;
    expected_permitted_path =
        expected_permitted_path + ":/product/" LIB_DIR ":/system/product/" LIB_DIR;
    expected_shared_libs_to_platform_ns =
        expected_shared_libs_to_platform_ns + ":" + llndk_libraries_product();
    expected_link_with_vndk_product_ns = true;
  }
  SetExpectations();
  RunTest();
}

TEST_P(NativeLoaderTest_Create, NamespaceForSharedLibIsNotUsedAsAnonymousNamespace) {
  if (IsBridged()) {
    // There is no shared lib in translated arch
    // TODO(jiyong): revisit this
    return;
  }
  // compared to apks, for java shared libs, library_path is empty; java shared
  // libs don't have their own native libs. They use platform's.
  library_path = "";
  expected_library_path = library_path;
  // no ALSO_USED_AS_ANONYMOUS
  expected_namespace_flags = ANDROID_NAMESPACE_TYPE_ISOLATED;
  SetExpectations();
  RunTest();
}

TEST_P(NativeLoaderTest_Create, TwoApks) {
  SetExpectations();
  const uint32_t second_app_target_sdk_version = 29;
  const std::string second_app_class_loader = "second_app_classloader";
  const bool second_app_is_shared = false;
  const std::string second_app_dex_path = "/data/app/bar/classes.dex";
  const std::string second_app_library_path = "/data/app/bar/" LIB_DIR "/arm";
  const std::string second_app_permitted_path = "/data/app/bar/" LIB_DIR;
  const std::string expected_second_app_permitted_path =
      std::string("/data:/mnt/expand:") + second_app_permitted_path;
  const std::string expected_second_app_parent_namespace = "classloader-namespace";
  // no ALSO_USED_AS_ANONYMOUS
  const uint64_t expected_second_namespace_flags = ANDROID_NAMESPACE_TYPE_ISOLATED;

  // The scenario is that second app is loaded by the first app.
  // So the first app's classloader (`classloader`) is parent of the second
  // app's classloader.
  ON_CALL(*mock, JniObject_getParent(StrEq(second_app_class_loader)))
      .WillByDefault(Return(class_loader.c_str()));

  // namespace for the second app is created. Its parent is set to the namespace
  // of the first app.
  EXPECT_CALL(*mock, mock_create_namespace(
                         Eq(IsBridged()), StrEq(expected_namespace_name), nullptr,
                         StrEq(second_app_library_path), expected_second_namespace_flags,
                         StrEq(expected_second_app_permitted_path), NsEq(dex_path.c_str())))
      .WillOnce(Return(TO_MOCK_NAMESPACE(TO_ANDROID_NAMESPACE(second_app_dex_path.c_str()))));
  EXPECT_CALL(*mock, mock_link_namespaces(Eq(IsBridged()), NsEq(second_app_dex_path.c_str()), _, _))
      .WillRepeatedly(Return(true));

  RunTest();
  jstring err = CreateClassLoaderNamespace(
      env(), second_app_target_sdk_version, env()->NewStringUTF(second_app_class_loader.c_str()),
      second_app_is_shared, env()->NewStringUTF(second_app_dex_path.c_str()),
      env()->NewStringUTF(second_app_library_path.c_str()),
      env()->NewStringUTF(second_app_permitted_path.c_str()), /*uses_library_list=*/ nullptr);

  // success
  EXPECT_EQ(err, nullptr) << "Error is: " << std::string(ScopedUtfChars(env(), err).c_str());

  if (!IsBridged()) {
    struct android_namespace_t* ns =
        FindNamespaceByClassLoader(env(), env()->NewStringUTF(second_app_class_loader.c_str()));

    // The created namespace is for the second apk
    EXPECT_EQ(second_app_dex_path.c_str(), reinterpret_cast<const char*>(ns));
  } else {
    struct NativeLoaderNamespace* ns = FindNativeLoaderNamespaceByClassLoader(
        env(), env()->NewStringUTF(second_app_class_loader.c_str()));

    // The created namespace is for the second apk
    EXPECT_STREQ(second_app_dex_path.c_str(),
                 reinterpret_cast<const char*>(ns->ToRawNativeBridgeNamespace()));
  }
}

INSTANTIATE_TEST_SUITE_P(NativeLoaderTests_Create, NativeLoaderTest_Create, testing::Bool());

const std::function<Result<bool>(const struct ConfigEntry&)> always_true =
    [](const struct ConfigEntry&) -> Result<bool> { return true; };

TEST(NativeLoaderConfigParser, NamesAndComments) {
  const char file_content[] = R"(
######

libA.so
#libB.so


      libC.so
libD.so
    #### libE.so
)";
  const std::vector<std::string> expected_result = {"libA.so", "libC.so", "libD.so"};
  Result<std::vector<std::string>> result = ParseConfig(file_content, always_true);
  ASSERT_RESULT_OK(result);
  ASSERT_EQ(expected_result, *result);
}

TEST(NativeLoaderConfigParser, WithBitness) {
  const char file_content[] = R"(
libA.so 32
libB.so 64
libC.so
)";
#if defined(__LP64__)
  const std::vector<std::string> expected_result = {"libB.so", "libC.so"};
#else
  const std::vector<std::string> expected_result = {"libA.so", "libC.so"};
#endif
  Result<std::vector<std::string>> result = ParseConfig(file_content, always_true);
  ASSERT_RESULT_OK(result);
  ASSERT_EQ(expected_result, *result);
}

TEST(NativeLoaderConfigParser, WithNoPreload) {
  const char file_content[] = R"(
libA.so nopreload
libB.so nopreload
libC.so
)";

  const std::vector<std::string> expected_result = {"libC.so"};
  Result<std::vector<std::string>> result =
      ParseConfig(file_content,
                  [](const struct ConfigEntry& entry) -> Result<bool> { return !entry.nopreload; });
  ASSERT_RESULT_OK(result);
  ASSERT_EQ(expected_result, *result);
}

TEST(NativeLoaderConfigParser, WithNoPreloadAndBitness) {
  const char file_content[] = R"(
libA.so nopreload 32
libB.so 64 nopreload
libC.so 32
libD.so 64
libE.so nopreload
)";

#if defined(__LP64__)
  const std::vector<std::string> expected_result = {"libD.so"};
#else
  const std::vector<std::string> expected_result = {"libC.so"};
#endif
  Result<std::vector<std::string>> result =
      ParseConfig(file_content,
                  [](const struct ConfigEntry& entry) -> Result<bool> { return !entry.nopreload; });
  ASSERT_RESULT_OK(result);
  ASSERT_EQ(expected_result, *result);
}

TEST(NativeLoaderConfigParser, RejectMalformed) {
  ASSERT_FALSE(ParseConfig("libA.so 32 64", always_true).ok());
  ASSERT_FALSE(ParseConfig("libA.so 32 32", always_true).ok());
  ASSERT_FALSE(ParseConfig("libA.so 32 nopreload 64", always_true).ok());
  ASSERT_FALSE(ParseConfig("32 libA.so nopreload", always_true).ok());
  ASSERT_FALSE(ParseConfig("nopreload libA.so 32", always_true).ok());
  ASSERT_FALSE(ParseConfig("libA.so nopreload # comment", always_true).ok());
}

TEST(NativeLoaderApexLibrariesConfigParser, BasicLoading) {
  const char file_content[] = R"(
# comment
jni com_android_foo libfoo.so
# Empty line is ignored

jni com_android_bar libbar.so:libbar2.so

  public com_android_bar libpublic.so
)";

  auto jni_libs = ParseApexLibrariesConfig(file_content, "jni");
  ASSERT_RESULT_OK(jni_libs);
  std::map<std::string, std::string> expected_jni_libs {
    {"com_android_foo", "libfoo.so"},
    {"com_android_bar", "libbar.so:libbar2.so"},
  };
  ASSERT_EQ(expected_jni_libs, *jni_libs);

  auto public_libs = ParseApexLibrariesConfig(file_content, "public");
  ASSERT_RESULT_OK(public_libs);
  std::map<std::string, std::string> expected_public_libs {
    {"com_android_bar", "libpublic.so"},
  };
  ASSERT_EQ(expected_public_libs, *public_libs);
}

TEST(NativeLoaderApexLibrariesConfigParser, RejectMalformedLine) {
  const char file_content[] = R"(
jni com_android_foo libfoo
# missing <library list>
jni com_android_bar
)";
  auto result = ParseApexLibrariesConfig(file_content, "jni");
  ASSERT_FALSE(result.ok());
  ASSERT_EQ("Malformed line \"jni com_android_bar\"", result.error().message());
}

TEST(NativeLoaderApexLibrariesConfigParser, RejectInvalidTag) {
  const char file_content[] = R"(
jni apex1 lib
public apex2 lib
# unknown tag
unknown com_android_foo libfoo
)";
  auto result = ParseApexLibrariesConfig(file_content, "jni");
  ASSERT_FALSE(result.ok());
  ASSERT_EQ("Invalid tag \"unknown com_android_foo libfoo\"", result.error().message());
}

TEST(NativeLoaderApexLibrariesConfigParser, RejectInvalidApexNamespace) {
  const char file_content[] = R"(
# apex linker namespace should be mangled ('.' -> '_')
jni com.android.foo lib
)";
  auto result = ParseApexLibrariesConfig(file_content, "jni");
  ASSERT_FALSE(result.ok());
  ASSERT_EQ("Invalid apex_namespace \"jni com.android.foo lib\"", result.error().message());
}

TEST(NativeLoaderApexLibrariesConfigParser, RejectInvalidLibraryList) {
  const char file_content[] = R"(
# library list is ":" separated list of filenames
jni com_android_foo lib64/libfoo.so
)";
  auto result = ParseApexLibrariesConfig(file_content, "jni");
  ASSERT_FALSE(result.ok());
  ASSERT_EQ("Invalid library_list \"jni com_android_foo lib64/libfoo.so\"", result.error().message());
}


}  // namespace nativeloader
}  // namespace android

#endif  // defined(ART_TARGET_ANDROID)
