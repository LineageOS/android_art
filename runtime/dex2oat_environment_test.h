/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef ART_RUNTIME_DEX2OAT_ENVIRONMENT_TEST_H_
#define ART_RUNTIME_DEX2OAT_ENVIRONMENT_TEST_H_

#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "base/file_utils.h"
#include "base/os.h"
#include "base/stl_util.h"
#include "base/utils.h"
#include "common_runtime_test.h"
#include "compiler_callbacks.h"
#include "dex/art_dex_file_loader.h"
#include "dex/dex_file_loader.h"
#include "exec_utils.h"
#include "gc/heap.h"
#include "gc/space/image_space.h"
#include "oat_file_assistant.h"
#include "runtime.h"

namespace art {

static constexpr bool kDebugArgs = false;

// Test class that provides some helpers to set a test up for compilation using dex2oat.
class Dex2oatEnvironmentTest : public CommonRuntimeTest {
 public:
  void SetUp() override {
    CommonRuntimeTest::SetUp();
    const ArtDexFileLoader dex_file_loader;

    // Create a scratch directory to work from.

    // Get the realpath of the android data. The oat dir should always point to real location
    // when generating oat files in dalvik-cache. This avoids complicating the unit tests
    // when matching the expected paths.
    UniqueCPtr<const char[]> android_data_real(realpath(android_data_.c_str(), nullptr));
    ASSERT_TRUE(android_data_real != nullptr)
      << "Could not get the realpath of the android data" << android_data_ << strerror(errno);

    scratch_dir_.assign(android_data_real.get());
    scratch_dir_ += "/Dex2oatEnvironmentTest";
    ASSERT_EQ(0, mkdir(scratch_dir_.c_str(), 0700));

    // Create a subdirectory in scratch for odex files.
    odex_oat_dir_ = scratch_dir_ + "/oat";
    ASSERT_EQ(0, mkdir(odex_oat_dir_.c_str(), 0700));

    odex_dir_ = odex_oat_dir_ + "/" + std::string(GetInstructionSetString(kRuntimeISA));
    ASSERT_EQ(0, mkdir(odex_dir_.c_str(), 0700));

    // Verify the environment is as we expect
    std::vector<uint32_t> checksums;
    std::string error_msg;
    ASSERT_TRUE(OS::FileExists(GetSystemImageFile().c_str()))
      << "Expected pre-compiled boot image to be at: " << GetSystemImageFile();
    ASSERT_TRUE(OS::FileExists(GetDexSrc1().c_str()))
      << "Expected dex file to be at: " << GetDexSrc1();
    ASSERT_TRUE(OS::FileExists(GetStrippedDexSrc1().c_str()))
      << "Expected stripped dex file to be at: " << GetStrippedDexSrc1();
    ASSERT_FALSE(
        dex_file_loader.GetMultiDexChecksums(GetStrippedDexSrc1().c_str(), &checksums, &error_msg))
      << "Expected stripped dex file to be stripped: " << GetStrippedDexSrc1();
    ASSERT_TRUE(OS::FileExists(GetDexSrc2().c_str()))
      << "Expected dex file to be at: " << GetDexSrc2();

    // GetMultiDexSrc2 should have the same primary dex checksum as
    // GetMultiDexSrc1, but a different secondary dex checksum.
    static constexpr bool kVerifyChecksum = true;
    std::vector<std::unique_ptr<const DexFile>> multi1;
    ASSERT_TRUE(dex_file_loader.Open(GetMultiDexSrc1().c_str(),
                                     GetMultiDexSrc1().c_str(),
                                     /* verify= */ true,
                                     kVerifyChecksum,
                                     &error_msg,
                                     &multi1)) << error_msg;
    ASSERT_GT(multi1.size(), 1u);

    std::vector<std::unique_ptr<const DexFile>> multi2;
    ASSERT_TRUE(dex_file_loader.Open(GetMultiDexSrc2().c_str(),
                                     GetMultiDexSrc2().c_str(),
                                     /* verify= */ true,
                                     kVerifyChecksum,
                                     &error_msg,
                                     &multi2)) << error_msg;
    ASSERT_GT(multi2.size(), 1u);

    ASSERT_EQ(multi1[0]->GetLocationChecksum(), multi2[0]->GetLocationChecksum());
    ASSERT_NE(multi1[1]->GetLocationChecksum(), multi2[1]->GetLocationChecksum());
  }

  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    // options->push_back(std::make_pair("-verbose:oat", nullptr));

    // Set up the image location.
    options->push_back(std::make_pair("-Ximage:" + GetImageLocation(),
          nullptr));
    // Make sure compilercallbacks are not set so that relocation will be
    // enabled.
    callbacks_.reset();
  }

  void TearDown() override {
    ClearDirectory(odex_dir_.c_str());
    ASSERT_EQ(0, rmdir(odex_dir_.c_str()));

    ClearDirectory(odex_oat_dir_.c_str());
    ASSERT_EQ(0, rmdir(odex_oat_dir_.c_str()));

    ClearDirectory(scratch_dir_.c_str());
    ASSERT_EQ(0, rmdir(scratch_dir_.c_str()));

    CommonRuntimeTest::TearDown();
  }

  static void Copy(const std::string& src, const std::string& dst) {
    std::ifstream  src_stream(src, std::ios::binary);
    std::ofstream  dst_stream(dst, std::ios::binary);

    dst_stream << src_stream.rdbuf();
  }

  std::string GetDexSrc1() const {
    return GetTestDexFileName("Main");
  }

  // Returns the path to a dex file equivalent to GetDexSrc1, but with the dex
  // file stripped.
  std::string GetStrippedDexSrc1() const {
    return GetTestDexFileName("MainStripped");
  }

  std::string GetMultiDexSrc1() const {
    return GetTestDexFileName("MultiDex");
  }

  // Returns the path to a multidex file equivalent to GetMultiDexSrc2, but
  // with the contents of the secondary dex file changed.
  std::string GetMultiDexSrc2() const {
    return GetTestDexFileName("MultiDexModifiedSecondary");
  }

  std::string GetDexSrc2() const {
    return GetTestDexFileName("Nested");
  }

  // Scratch directory, for dex and odex files (oat files will go in the
  // dalvik cache).
  const std::string& GetScratchDir() const {
    return scratch_dir_;
  }

  // Odex directory is the subdirectory in the scratch directory where odex
  // files should be located.
  const std::string& GetOdexDir() const {
    return odex_dir_;
  }

  int Dex2Oat(const std::vector<std::string>& dex2oat_args,
              std::string* output,
              std::string* error_msg) {
    std::vector<std::string> argv;
    if (!CommonRuntimeTest::StartDex2OatCommandLine(&argv, error_msg)) {
      ::testing::AssertionFailure() << "Could not start dex2oat cmd line " << *error_msg;
    }

    Runtime* runtime = Runtime::Current();
    if (!runtime->IsVerificationEnabled()) {
      argv.push_back("--compiler-filter=assume-verified");
    }

    if (runtime->MustRelocateIfPossible()) {
      argv.push_back("--runtime-arg");
      argv.push_back("-Xrelocate");
    } else {
      argv.push_back("--runtime-arg");
      argv.push_back("-Xnorelocate");
    }

    if (!kIsTargetBuild) {
      argv.push_back("--host");
    }

    argv.insert(argv.end(), dex2oat_args.begin(), dex2oat_args.end());

    // We must set --android-root.
    const char* android_root = getenv("ANDROID_ROOT");
    CHECK(android_root != nullptr);
    argv.push_back("--android-root=" + std::string(android_root));

    if (kDebugArgs) {
      std::string all_args;
      for (const std::string& arg : argv) {
        all_args += arg + " ";
      }
      LOG(ERROR) << all_args;
    }

    // We need dex2oat to actually log things.
    auto post_fork_fn = []() { return setenv("ANDROID_LOG_TAGS", "*:d", 1) == 0; };
    ForkAndExecResult res = ForkAndExec(argv, post_fork_fn, output);
    if (res.stage != ForkAndExecResult::kFinished) {
      *error_msg = strerror(errno);
      ::testing::AssertionFailure() << "Failed to finish dex2oat invocation: " << *error_msg;
    }

    if (!res.StandardSuccess()) {
      // We cannot use ASSERT_TRUE since the method returns an int and not void.
      ::testing::AssertionFailure() << "dex2oat fork/exec failed: " << *error_msg;
    }

    return res.status_code;
  }

 private:
  std::string scratch_dir_;
  std::string odex_oat_dir_;
  std::string odex_dir_;
};

}  // namespace art

#endif  // ART_RUNTIME_DEX2OAT_ENVIRONMENT_TEST_H_
