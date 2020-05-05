/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <gtest/gtest.h>

#include "android-base/logging.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"

#include "base/stl_util.h"
#include "class_linker.h"
#include "dexopt_test.h"
#include "dex/utf.h"
#include "intern_table.h"
#include "noop_compiler_callbacks.h"
#include "oat_file.h"

namespace art {
namespace gc {
namespace space {

class ImageSpaceTest : public CommonRuntimeTest {
 protected:
  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    // Disable implicit dex2oat invocations when loading image spaces.
    options->emplace_back("-Xnoimage-dex2oat", nullptr);
    // Disable relocation.
    options->emplace_back("-Xnorelocate", nullptr);
  }

  std::string GetFilenameBase(const std::string& full_path) {
    size_t slash_pos = full_path.rfind('/');
    CHECK_NE(std::string::npos, slash_pos);
    size_t dot_pos = full_path.rfind('.');
    CHECK_NE(std::string::npos, dot_pos);
    CHECK_GT(dot_pos, slash_pos + 1u);
    return full_path.substr(slash_pos + 1u, dot_pos - (slash_pos + 1u));
  }
};

TEST_F(ImageSpaceTest, StringDeduplication) {
  const char* const kBaseNames[] = { "Extension1", "Extension2" };

  ScratchDir scratch;
  const std::string& scratch_dir = scratch.GetPath();
  std::string image_dir = scratch_dir + GetInstructionSetString(kRuntimeISA);
  int mkdir_result = mkdir(image_dir.c_str(), 0700);
  ASSERT_EQ(0, mkdir_result);

  // Prepare boot class path variables, exclude conscrypt which is not in the primary boot image.
  std::vector<std::string> bcp = GetLibCoreDexFileNames();
  std::vector<std::string> bcp_locations = GetLibCoreDexLocations();
  CHECK_EQ(bcp.size(), bcp_locations.size());
  ASSERT_NE(std::string::npos, bcp.back().find("conscrypt"));
  bcp.pop_back();
  bcp_locations.pop_back();
  std::string base_bcp_string = android::base::Join(bcp, ':');
  std::string base_bcp_locations_string = android::base::Join(bcp_locations, ':');
  std::string base_image_location = GetImageLocation();

  // Compile the two extensions independently.
  std::vector<std::string> extension_image_locations;
  for (const char* base_name : kBaseNames) {
    std::string jar_name = GetTestDexFileName(base_name);
    ArrayRef<const std::string> dex_files(&jar_name, /*size=*/ 1u);
    ScratchFile profile_file;
    GenerateProfile(dex_files, profile_file.GetFile());
    std::vector<std::string> extra_args = {
        "--profile-file=" + profile_file.GetFilename(),
        "--runtime-arg",
        "-Xbootclasspath:" + base_bcp_string + ':' + jar_name,
        "--runtime-arg",
        "-Xbootclasspath-locations:" + base_bcp_locations_string + ':' + jar_name,
        "--boot-image=" + base_image_location,
    };
    std::string prefix = GetFilenameBase(base_image_location);
    std::string error_msg;
    bool success = CompileBootImage(extra_args, image_dir + '/' + prefix, dex_files, &error_msg);
    ASSERT_TRUE(success) << error_msg;
    bcp.push_back(jar_name);
    bcp_locations.push_back(jar_name);
    extension_image_locations.push_back(
        scratch_dir + prefix + '-' + GetFilenameBase(jar_name) + ".art");
  }

  // Also compile the second extension as an app with app image.
  const char* app_base_name = kBaseNames[std::size(kBaseNames) - 1u];
  std::string app_jar_name = GetTestDexFileName(app_base_name);
  std::string app_odex_name = scratch_dir + app_base_name + ".odex";
  std::string app_image_name = scratch_dir + app_base_name + ".art";
  {
    ArrayRef<const std::string> dex_files(&app_jar_name, /*size=*/ 1u);
    ScratchFile profile_file;
    GenerateProfile(dex_files, profile_file.GetFile());
    std::vector<std::string> argv;
    std::string error_msg;
    bool success = StartDex2OatCommandLine(&argv, &error_msg, /*use_runtime_bcp_and_image=*/ false);
    ASSERT_TRUE(success) << error_msg;
    argv.insert(argv.end(), {
        "--profile-file=" + profile_file.GetFilename(),
        "--runtime-arg",
        "-Xbootclasspath:" + base_bcp_string,
        "--runtime-arg",
        "-Xbootclasspath-locations:" + base_bcp_locations_string,
        "--boot-image=" + base_image_location,
        "--dex-file=" + app_jar_name,
        "--dex-location=" + app_jar_name,
        "--oat-file=" + app_odex_name,
        "--app-image-file=" + app_image_name,
        "--initialize-app-image-classes=true",
    });
    success = RunDex2Oat(argv, &error_msg);
    ASSERT_TRUE(success) << error_msg;
  }

  std::string full_image_locations;
  std::vector<std::unique_ptr<gc::space::ImageSpace>> boot_image_spaces;
  MemMap extra_reservation;
  auto load_boot_image = [&]() REQUIRES_SHARED(Locks::mutator_lock_) {
    boot_image_spaces.clear();
    extra_reservation = MemMap::Invalid();
    return ImageSpace::LoadBootImage(bcp,
                                     bcp_locations,
                                     full_image_locations,
                                     kRuntimeISA,
                                     ImageSpaceLoadingOrder::kSystemFirst,
                                     /*relocate=*/ false,
                                     /*executable=*/ true,
                                     /*is_zygote=*/ false,
                                     /*extra_reservation_size=*/ 0u,
                                     &boot_image_spaces,
                                     &extra_reservation);
  };

  const char test_string[] = "SharedBootImageExtensionTestString";
  size_t test_string_length = std::size(test_string) - 1u;  // Equals UTF-16 length.
  uint32_t hash = ComputeUtf16HashFromModifiedUtf8(test_string, test_string_length);
  InternTable::Utf8String utf8_test_string(test_string_length, test_string, hash);
  auto contains_test_string = [utf8_test_string](ImageSpace* space)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    const ImageHeader& image_header = space->GetImageHeader();
    if (image_header.GetInternedStringsSection().Size() != 0u) {
      const uint8_t* data = space->Begin() + image_header.GetInternedStringsSection().Offset();
      size_t read_count;
      InternTable::UnorderedSet temp_set(data, /*make_copy_of_data=*/ false, &read_count);
      return temp_set.find(utf8_test_string) != temp_set.end();
    } else {
      return false;
    }
  };

  // Load extensions and test for the presence of the test string.
  ScopedObjectAccess soa(Thread::Current());
  ASSERT_EQ(2u, extension_image_locations.size());
  full_image_locations = base_image_location +
                             ImageSpace::kComponentSeparator + extension_image_locations[0] +
                             ImageSpace::kComponentSeparator + extension_image_locations[1];
  bool success = load_boot_image();
  ASSERT_TRUE(success);
  ASSERT_EQ(bcp.size(), boot_image_spaces.size());
  EXPECT_TRUE(contains_test_string(boot_image_spaces[boot_image_spaces.size() - 2u].get()));
  // The string in the second extension should be replaced and removed from interned string section.
  EXPECT_FALSE(contains_test_string(boot_image_spaces[boot_image_spaces.size() - 1u].get()));

  // Reload extensions in reverse order and test for the presence of the test string.
  std::swap(bcp[bcp.size() - 2u], bcp[bcp.size() - 1u]);
  std::swap(bcp_locations[bcp_locations.size() - 2u], bcp_locations[bcp_locations.size() - 1u]);
  full_image_locations = base_image_location +
                             ImageSpace::kComponentSeparator + extension_image_locations[1] +
                             ImageSpace::kComponentSeparator + extension_image_locations[0];
  success = load_boot_image();
  ASSERT_TRUE(success);
  ASSERT_EQ(bcp.size(), boot_image_spaces.size());
  EXPECT_TRUE(contains_test_string(boot_image_spaces[boot_image_spaces.size() - 2u].get()));
  // The string in the second extension should be replaced and removed from interned string section.
  EXPECT_FALSE(contains_test_string(boot_image_spaces[boot_image_spaces.size() - 1u].get()));

  // Reload the image without the second extension.
  bcp.erase(bcp.end() - 2u);
  bcp_locations.erase(bcp_locations.end() - 2u);
  full_image_locations =
      base_image_location + ImageSpace::kComponentSeparator + extension_image_locations[0];
  success = load_boot_image();
  ASSERT_TRUE(success);
  ASSERT_EQ(bcp.size(), boot_image_spaces.size());
  ASSERT_TRUE(contains_test_string(boot_image_spaces[boot_image_spaces.size() - 1u].get()));

  // Load the app odex file and app image.
  std::string error_msg;
  std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/ -1,
                                                   app_odex_name.c_str(),
                                                   app_odex_name.c_str(),
                                                   /*executable=*/ false,
                                                   /*low_4gb=*/ false,
                                                   app_jar_name,
                                                   &error_msg));
  ASSERT_TRUE(odex_file != nullptr) << error_msg;
  std::vector<ImageSpace*> non_owning_boot_image_spaces =
      MakeNonOwningPointerVector(boot_image_spaces);
  std::unique_ptr<ImageSpace> app_image_space = ImageSpace::CreateFromAppImage(
      app_image_name.c_str(),
      odex_file.get(),
      ArrayRef<ImageSpace* const>(non_owning_boot_image_spaces),
      &error_msg);
  ASSERT_TRUE(app_image_space != nullptr) << error_msg;

  // The string in the app image should be replaced and removed from interned string section.
  EXPECT_FALSE(contains_test_string(app_image_space.get()));
}

TEST_F(DexoptTest, ValidateOatFile) {
  std::string dex1 = GetScratchDir() + "/Dex1.jar";
  std::string multidex1 = GetScratchDir() + "/MultiDex1.jar";
  std::string dex2 = GetScratchDir() + "/Dex2.jar";
  std::string oat_location = GetScratchDir() + "/Oat.oat";

  Copy(GetDexSrc1(), dex1);
  Copy(GetMultiDexSrc1(), multidex1);
  Copy(GetDexSrc2(), dex2);

  std::string error_msg;
  std::vector<std::string> args;
  args.push_back("--dex-file=" + dex1);
  args.push_back("--dex-file=" + multidex1);
  args.push_back("--dex-file=" + dex2);
  args.push_back("--oat-file=" + oat_location);
  ASSERT_TRUE(Dex2Oat(args, &error_msg)) << error_msg;

  std::unique_ptr<OatFile> oat(OatFile::Open(/*zip_fd=*/ -1,
                                             oat_location.c_str(),
                                             oat_location.c_str(),
                                             /*executable=*/ false,
                                             /*low_4gb=*/ false,
                                             &error_msg));
  ASSERT_TRUE(oat != nullptr) << error_msg;

  {
    // Test opening the oat file also with explicit dex filenames.
    std::vector<std::string> dex_filenames{ dex1, multidex1, dex2 };
    std::unique_ptr<OatFile> oat2(OatFile::Open(/*zip_fd=*/ -1,
                                                oat_location.c_str(),
                                                oat_location.c_str(),
                                                /*executable=*/ false,
                                                /*low_4gb=*/ false,
                                                ArrayRef<const std::string>(dex_filenames),
                                                /*reservation=*/ nullptr,
                                                &error_msg));
    ASSERT_TRUE(oat2 != nullptr) << error_msg;
  }

  // Originally all the dex checksums should be up to date.
  EXPECT_TRUE(ImageSpace::ValidateOatFile(*oat, &error_msg)) << error_msg;

  // Invalidate the dex1 checksum.
  Copy(GetDexSrc2(), dex1);
  EXPECT_FALSE(ImageSpace::ValidateOatFile(*oat, &error_msg));

  // Restore the dex1 checksum.
  Copy(GetDexSrc1(), dex1);
  EXPECT_TRUE(ImageSpace::ValidateOatFile(*oat, &error_msg)) << error_msg;

  // Invalidate the non-main multidex checksum.
  Copy(GetMultiDexSrc2(), multidex1);
  EXPECT_FALSE(ImageSpace::ValidateOatFile(*oat, &error_msg));

  // Restore the multidex checksum.
  Copy(GetMultiDexSrc1(), multidex1);
  EXPECT_TRUE(ImageSpace::ValidateOatFile(*oat, &error_msg)) << error_msg;

  // Invalidate the dex2 checksum.
  Copy(GetDexSrc1(), dex2);
  EXPECT_FALSE(ImageSpace::ValidateOatFile(*oat, &error_msg));

  // restore the dex2 checksum.
  Copy(GetDexSrc2(), dex2);
  EXPECT_TRUE(ImageSpace::ValidateOatFile(*oat, &error_msg)) << error_msg;

  // Replace the multidex file with a non-multidex file.
  Copy(GetDexSrc1(), multidex1);
  EXPECT_FALSE(ImageSpace::ValidateOatFile(*oat, &error_msg));

  // Restore the multidex file
  Copy(GetMultiDexSrc1(), multidex1);
  EXPECT_TRUE(ImageSpace::ValidateOatFile(*oat, &error_msg)) << error_msg;

  // Replace dex1 with a multidex file.
  Copy(GetMultiDexSrc1(), dex1);
  EXPECT_FALSE(ImageSpace::ValidateOatFile(*oat, &error_msg));

  // Restore the dex1 file.
  Copy(GetDexSrc1(), dex1);
  EXPECT_TRUE(ImageSpace::ValidateOatFile(*oat, &error_msg)) << error_msg;

  // Remove the dex2 file.
  EXPECT_EQ(0, unlink(dex2.c_str()));
  EXPECT_FALSE(ImageSpace::ValidateOatFile(*oat, &error_msg));

  // Restore the dex2 file.
  Copy(GetDexSrc2(), dex2);
  EXPECT_TRUE(ImageSpace::ValidateOatFile(*oat, &error_msg)) << error_msg;

  // Remove the multidex file.
  EXPECT_EQ(0, unlink(multidex1.c_str()));
  EXPECT_FALSE(ImageSpace::ValidateOatFile(*oat, &error_msg));
}

TEST_F(DexoptTest, Checksums) {
  Runtime* runtime = Runtime::Current();
  ASSERT_TRUE(runtime != nullptr);
  ASSERT_FALSE(runtime->GetHeap()->GetBootImageSpaces().empty());

  std::vector<std::string> bcp = runtime->GetBootClassPath();
  std::vector<std::string> bcp_locations = runtime->GetBootClassPathLocations();
  std::vector<const DexFile*> dex_files = runtime->GetClassLinker()->GetBootClassPath();

  std::string error_msg;
  auto create_and_verify = [&]() {
    std::string checksums = gc::space::ImageSpace::GetBootClassPathChecksums(
        ArrayRef<gc::space::ImageSpace* const>(runtime->GetHeap()->GetBootImageSpaces()),
        ArrayRef<const DexFile* const>(dex_files));
    return gc::space::ImageSpace::VerifyBootClassPathChecksums(
        checksums,
        android::base::Join(bcp_locations, ':'),
        runtime->GetImageLocation(),
        ArrayRef<const std::string>(bcp_locations),
        ArrayRef<const std::string>(bcp),
        kRuntimeISA,
        gc::space::ImageSpaceLoadingOrder::kSystemFirst,
        &error_msg);
  };

  ASSERT_TRUE(create_and_verify()) << error_msg;

  std::vector<std::unique_ptr<const DexFile>> opened_dex_files;
  for (const std::string& src : { GetDexSrc1(), GetDexSrc2() }) {
    std::vector<std::unique_ptr<const DexFile>> new_dex_files;
    const ArtDexFileLoader dex_file_loader;
    ASSERT_TRUE(dex_file_loader.Open(src.c_str(),
                                     src,
                                     /*verify=*/ true,
                                     /*verify_checksum=*/ false,
                                     &error_msg,
                                     &new_dex_files))
        << error_msg;

    bcp.push_back(src);
    bcp_locations.push_back(src);
    for (std::unique_ptr<const DexFile>& df : new_dex_files) {
      dex_files.push_back(df.get());
      opened_dex_files.push_back(std::move(df));
    }

    ASSERT_TRUE(create_and_verify()) << error_msg;
  }
}

template <bool kImage, bool kRelocate, bool kImageDex2oat>
class ImageSpaceLoadingTest : public CommonRuntimeTest {
 protected:
  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    std::string image_location = GetCoreArtLocation();
    if (!kImage) {
      missing_image_base_ = std::make_unique<ScratchFile>();
      image_location = missing_image_base_->GetFilename() + ".art";
    }
    options->emplace_back(android::base::StringPrintf("-Ximage:%s", image_location.c_str()),
                          nullptr);
    options->emplace_back(kRelocate ? "-Xrelocate" : "-Xnorelocate", nullptr);
    options->emplace_back(kImageDex2oat ? "-Ximage-dex2oat" : "-Xnoimage-dex2oat", nullptr);

    // We want to test the relocation behavior of ImageSpace. As such, don't pretend we're a
    // compiler.
    callbacks_.reset();

    // Clear DEX2OATBOOTCLASSPATH environment variable used for boot image compilation.
    // We don't want that environment variable to affect the behavior of this test.
    CHECK(old_dex2oat_bcp_ == nullptr);
    const char* old_dex2oat_bcp = getenv("DEX2OATBOOTCLASSPATH");
    if (old_dex2oat_bcp != nullptr) {
      old_dex2oat_bcp_.reset(strdup(old_dex2oat_bcp));
      CHECK(old_dex2oat_bcp_ != nullptr);
      unsetenv("DEX2OATBOOTCLASSPATH");
    }
  }

  void TearDown() override {
    if (old_dex2oat_bcp_ != nullptr) {
      int result = setenv("DEX2OATBOOTCLASSPATH", old_dex2oat_bcp_.get(), /* replace */ 0);
      CHECK_EQ(result, 0);
      old_dex2oat_bcp_.reset();
    }
    missing_image_base_.reset();
  }

 private:
  std::unique_ptr<ScratchFile> missing_image_base_;
  UniqueCPtr<const char[]> old_dex2oat_bcp_;
};

using ImageSpaceDex2oatTest = ImageSpaceLoadingTest<false, true, true>;
TEST_F(ImageSpaceDex2oatTest, Test) {
  EXPECT_FALSE(Runtime::Current()->GetHeap()->GetBootImageSpaces().empty());
}

using ImageSpaceNoDex2oatTest = ImageSpaceLoadingTest<true, true, false>;
TEST_F(ImageSpaceNoDex2oatTest, Test) {
  EXPECT_FALSE(Runtime::Current()->GetHeap()->GetBootImageSpaces().empty());
}

using ImageSpaceNoRelocateNoDex2oatTest = ImageSpaceLoadingTest<true, false, false>;
TEST_F(ImageSpaceNoRelocateNoDex2oatTest, Test) {
  EXPECT_FALSE(Runtime::Current()->GetHeap()->GetBootImageSpaces().empty());
}

class NoAccessAndroidDataTest : public ImageSpaceLoadingTest<false, true, true> {
 protected:
  NoAccessAndroidDataTest() : quiet_(LogSeverity::FATAL) {}

  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    const char* android_data = getenv("ANDROID_DATA");
    CHECK(android_data != nullptr);
    old_android_data_ = android_data;
    bad_android_data_ = old_android_data_ + "/no-android-data";
    int result = setenv("ANDROID_DATA", bad_android_data_.c_str(), /* replace */ 1);
    CHECK_EQ(result, 0) << strerror(errno);
    result = mkdir(bad_android_data_.c_str(), /* mode */ 0700);
    CHECK_EQ(result, 0) << strerror(errno);
    // Create a regular file "dalvik_cache". GetDalvikCache() shall get EEXIST
    // when trying to create a directory with the same name and creating a
    // subdirectory for a particular architecture shall fail.
    bad_dalvik_cache_ = bad_android_data_ + "/dalvik-cache";
    int fd = creat(bad_dalvik_cache_.c_str(), /* mode */ 0);
    CHECK_NE(fd, -1) << strerror(errno);
    result = close(fd);
    CHECK_EQ(result, 0) << strerror(errno);
    ImageSpaceLoadingTest<false, true, true>::SetUpRuntimeOptions(options);
  }

  void TearDown() override {
    ImageSpaceLoadingTest<false, true, true>::TearDown();
    int result = unlink(bad_dalvik_cache_.c_str());
    CHECK_EQ(result, 0) << strerror(errno);
    result = rmdir(bad_android_data_.c_str());
    CHECK_EQ(result, 0) << strerror(errno);
    result = setenv("ANDROID_DATA", old_android_data_.c_str(), /* replace */ 1);
    CHECK_EQ(result, 0) << strerror(errno);
  }

 private:
  ScopedLogSeverity quiet_;
  std::string old_android_data_;
  std::string bad_android_data_;
  std::string bad_dalvik_cache_;
};

TEST_F(NoAccessAndroidDataTest, Test) {
  EXPECT_TRUE(Runtime::Current()->GetHeap()->GetBootImageSpaces().empty());
}

}  // namespace space
}  // namespace gc
}  // namespace art
