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

#if defined(ART_TARGET_ANDROID)

#include <gtest/gtest.h>

#include "native_loader_test.h"
#include "nativehelper/scoped_utf_chars.h"
#include "nativeloader/native_loader.h"

namespace android {
namespace nativeloader {

using ::testing::StrEq;

// Only need to test that the trivial lazy lib wrappers call through to the real
// functions, but still have to mock things well enough to avoid null pointer
// dereferences.

class NativeLoaderLazyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock = std::make_unique<testing::NiceMock<MockPlatform>>(false);
    env = std::make_unique<JNIEnv>();
    env->functions = CreateJNINativeInterface();
  }

  void TearDown() override {
    // ResetNativeLoader isn't accessible through the lazy library, so we cannot
    // reset libnativeloader internal state. Hence be sure to not reuse the same
    // class loader/namespace names.
    delete env->functions;
    mock.reset();
  }

  void CallCreateClassLoaderNamespace(const char* class_loader) {
    ON_CALL(*mock, JniObject_getParent(StrEq(class_loader))).WillByDefault(Return(nullptr));
    EXPECT_CALL(*mock, mock_create_namespace)
        .WillOnce(Return(TO_MOCK_NAMESPACE(TO_ANDROID_NAMESPACE(class_loader))));
    ON_CALL(*mock, mock_link_namespaces).WillByDefault(Return(true));

    jstring err = CreateClassLoaderNamespace(env.get(),
                                             17,
                                             env.get()->NewStringUTF(class_loader),
                                             false,
                                             env.get()->NewStringUTF("/data/app/foo/classes.dex"),
                                             env.get()->NewStringUTF("/data/app/foo"),
                                             /*permitted_path=*/nullptr,
                                             /*uses_library_list=*/nullptr);
    EXPECT_EQ(err, nullptr) << "Error is: " << std::string(ScopedUtfChars(env.get(), err).c_str());
  }

  std::unique_ptr<JNIEnv> env;
};

TEST_F(NativeLoaderLazyTest, CreateClassLoaderNamespace) {
  CallCreateClassLoaderNamespace("my_classloader_1");
}

TEST_F(NativeLoaderLazyTest, OpenNativeLibrary) {
  bool needs_native_bridge;
  char* errmsg = nullptr;
  EXPECT_EQ(nullptr,
            OpenNativeLibrary(env.get(),
                              17,
                              "libnotfound.so",
                              env.get()->NewStringUTF("my_classloader"),
                              /*caller_location=*/nullptr,
                              /*library_path=*/nullptr,
                              &needs_native_bridge,
                              &errmsg));
  EXPECT_NE(nullptr, errmsg);
  NativeLoaderFreeErrorMessage(errmsg);
}

TEST_F(NativeLoaderLazyTest, CloseNativeLibrary) {
  char* errmsg = nullptr;
  EXPECT_FALSE(CloseNativeLibrary(nullptr, false, &errmsg));
  EXPECT_NE(nullptr, errmsg);
  NativeLoaderFreeErrorMessage(errmsg);
}

TEST_F(NativeLoaderLazyTest, OpenNativeLibraryInNamespace) {
  CallCreateClassLoaderNamespace("my_classloader_2");
  struct NativeLoaderNamespace* ns = FindNativeLoaderNamespaceByClassLoader(
      env.get(), env.get()->NewStringUTF("my_classloader_2"));
  ASSERT_NE(nullptr, ns);

  bool needs_native_bridge;
  char* errmsg = nullptr;
  EXPECT_FALSE(OpenNativeLibraryInNamespace(ns, "libnotfound.so", &needs_native_bridge, &errmsg));
  EXPECT_NE(nullptr, errmsg);
  NativeLoaderFreeErrorMessage(errmsg);
}

TEST_F(NativeLoaderLazyTest, FindNamespaceByClassLoader) {
  EXPECT_EQ(nullptr, FindNamespaceByClassLoader(env.get(), env.get()->NewStringUTF("namespace")));
}

TEST_F(NativeLoaderLazyTest, FindNativeLoaderNamespaceByClassLoader) {
  EXPECT_EQ(
      nullptr,
      FindNativeLoaderNamespaceByClassLoader(env.get(), env.get()->NewStringUTF("namespace")));
}

TEST_F(NativeLoaderLazyTest, NativeLoaderFreeErrorMessage) {
  NativeLoaderFreeErrorMessage(nullptr);
}

}  // namespace nativeloader
}  // namespace android

#endif  // defined(ART_TARGET_ANDROID)
