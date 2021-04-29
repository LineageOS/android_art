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

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "android-base/stringprintf.h"
#include "base/bit_utils.h"
#include "base/common_art_test.h"
#include "base/os.h"
#include "base/unix_file/fd_file.h"
#include "odr_fs_utils.h"

namespace art {
namespace odrefresh {

class OdrFsUtilsTest : public CommonArtTest {};
namespace {

static bool CreateFile(const char* file_path, size_t bytes) {
  std::unique_ptr<File> fp(OS::CreateEmptyFile(file_path));
  if (!fp) {
    return false;
  }

  std::vector<char> buffer(bytes, 0xa5);
  if (!fp->WriteFully(buffer.data(), buffer.size())) {
    fp->Erase();
    return false;
  }

  if (fp->FlushClose() != 0) {
    fp->Erase();
    return false;
  }

  return true;
}

}  // namespace

TEST_F(OdrFsUtilsTest, CleanDirectory) {
  ScratchDir scratch_dir(/*keep_files=*/false);

  // Create some sub-directories and files
  const std::string dir_paths[] = {
      scratch_dir.GetPath() + "/a",
      scratch_dir.GetPath() + "/b",
      scratch_dir.GetPath() + "/b/c",
      scratch_dir.GetPath() + "/d"
  };
  for (const auto& dir_path : dir_paths) {
    ASSERT_EQ(0, mkdir(dir_path.c_str(), S_IRWXU));
  }

  const std::string file_paths[] = {
      scratch_dir.GetPath() + "/zero.txt",
      scratch_dir.GetPath() + "/a/one.txt",
      scratch_dir.GetPath() + "/b/two.txt",
      scratch_dir.GetPath() + "/b/c/three.txt",
      scratch_dir.GetPath() + "/b/c/four.txt",
  };
  for (const auto& file_path : file_paths) {
    ASSERT_TRUE(CreateFile(file_path.c_str(), 4096));
  }

  // Clean all files and sub-directories
  ASSERT_TRUE(CleanDirectory(scratch_dir.GetPath()));

  // Check nothing we created remains.
  for (const auto& dir_path : dir_paths) {
    ASSERT_FALSE(OS::DirectoryExists(dir_path.c_str()));
  }

  for (const auto& file_path : file_paths) {
    ASSERT_FALSE(OS::FileExists(file_path.c_str(), true));
  }
}

TEST_F(OdrFsUtilsTest, EnsureDirectoryExistsBadPath) {
  // Pick a path where not even a root test runner can write.
  ASSERT_FALSE(EnsureDirectoryExists("/proc/unlikely/to/be/writable"));
}

TEST_F(OdrFsUtilsTest, EnsureDirectoryExistsEmptyPath) {
  ASSERT_FALSE(EnsureDirectoryExists(""));
}

TEST_F(OdrFsUtilsTest, EnsureDirectoryExistsRelativePath) {
  ASSERT_FALSE(EnsureDirectoryExists("a/b/c"));
}

TEST_F(OdrFsUtilsTest, EnsureDirectoryExistsSubDirs) {
  ScratchDir scratch_dir(/*keep_files=*/false);

  const char* relative_sub_dirs[] = {"a", "b/c", "d/e/f/"};
  for (const char* relative_sub_dir : relative_sub_dirs) {
    ASSERT_TRUE(EnsureDirectoryExists(scratch_dir.GetPath() + "/" + relative_sub_dir));
  }
}

TEST_F(OdrFsUtilsTest, DISABLED_GetUsedSpace) {
  static constexpr size_t kFirstFileBytes = 1;
  static constexpr size_t kSecondFileBytes = 16111;
  static constexpr size_t kBytesPerBlock = 512;

  ScratchDir scratch_dir(/*keep_files=*/false);

  const std::string first_file_path = scratch_dir.GetPath() + "/1.dat";
  ASSERT_TRUE(CreateFile(first_file_path.c_str(), kFirstFileBytes));

  struct stat sb;
  ASSERT_EQ(0, stat(first_file_path.c_str(), &sb));
  ASSERT_EQ(kFirstFileBytes, static_cast<decltype(kFirstFileBytes)>(sb.st_size));

  uint64_t bytes_used = 0;
  ASSERT_TRUE(GetUsedSpace(scratch_dir.GetPath().c_str(), &bytes_used));

  const std::string second_file_path = scratch_dir.GetPath() + "/2.dat";
  ASSERT_TRUE(CreateFile(second_file_path.c_str(), kSecondFileBytes));

  ASSERT_TRUE(GetUsedSpace(scratch_dir.GetPath().c_str(), &bytes_used));
  uint64_t expected_bytes_used = RoundUp(kFirstFileBytes, sb.st_blocks * kBytesPerBlock) +
                                 RoundUp(kSecondFileBytes, sb.st_blocks * kBytesPerBlock);
  ASSERT_EQ(expected_bytes_used, bytes_used);

  const std::string sub_dir_path = scratch_dir.GetPath() + "/sub";
  ASSERT_TRUE(EnsureDirectoryExists(sub_dir_path));
  for (size_t i = 1; i < 32768; i *= 17) {
    const std::string path = android::base::StringPrintf("%s/%zu", sub_dir_path.c_str(), i);
    ASSERT_TRUE(CreateFile(path.c_str(), i));
    expected_bytes_used += RoundUp(i, sb.st_blocks * kBytesPerBlock);
    ASSERT_TRUE(GetUsedSpace(scratch_dir.GetPath().c_str(), &bytes_used));
    ASSERT_EQ(expected_bytes_used, bytes_used);
  }
}

TEST_F(OdrFsUtilsTest, GetUsedSpaceBadPath) {
  ScratchDir scratch_dir(/*keep_files=*/false);
  const std::string bad_path = scratch_dir.GetPath() + "/bad_path";
  uint64_t bytes_used = ~0ull;
  ASSERT_TRUE(GetUsedSpace(bad_path, &bytes_used));
  ASSERT_EQ(0ull, bytes_used);
}

}  // namespace odrefresh
}  // namespace art
