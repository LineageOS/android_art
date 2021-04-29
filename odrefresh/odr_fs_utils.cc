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

#include "odr_fs_utils.h"

#include <dirent.h>
#include <ftw.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <iosfwd>
#include <memory>
#include <ostream>
#include <queue>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/strings.h>
#include <base/os.h>

namespace art {
namespace odrefresh {

// Callback for use with nftw(3) to assist with clearing files and sub-directories.
// This method removes files and directories below the top-level directory passed to nftw().
static int NftwCleanUpCallback(const char* fpath,
                               const struct stat* sb ATTRIBUTE_UNUSED,
                               int typeflag,
                               struct FTW* ftwbuf) {
  switch (typeflag) {
    case FTW_F:
      return unlink(fpath);
    case FTW_DP:
      return (ftwbuf->level == 0) ? 0 : rmdir(fpath);
    default:
      return -1;
  }
}

WARN_UNUSED bool CleanDirectory(const std::string& dir_path) {
  if (!OS::DirectoryExists(dir_path.c_str())) {
    return true;
  }

  static constexpr int kMaxDescriptors = 4;  // Limit the need for nftw() to re-open descriptors.
  if (nftw(dir_path.c_str(), NftwCleanUpCallback, kMaxDescriptors, FTW_DEPTH | FTW_MOUNT) != 0) {
    LOG(ERROR) << "Failed to clean-up '" << dir_path << "'";
    return false;
  }
  return true;
}

WARN_UNUSED bool EnsureDirectoryExists(const std::string& absolute_path) {
  if (absolute_path.empty() || absolute_path[0] != '/') {
    LOG(ERROR) << "Path not absolute '" << absolute_path << "'";
    return false;
  }
  std::string path;
  for (const std::string& directory : android::base::Split(absolute_path, "/")) {
    path.append("/").append(directory);
    if (!OS::DirectoryExists(path.c_str())) {
      static constexpr mode_t kDirectoryMode = S_IRWXU | S_IRGRP | S_IXGRP| S_IROTH | S_IXOTH;
      if (mkdir(path.c_str(), kDirectoryMode) != 0) {
        PLOG(ERROR) << "Could not create directory: " << path;
        return false;
      }
    }
  }
  return true;
}

bool GetFreeSpace(const std::string& path, uint64_t* bytes) {
  struct statvfs sv;
  if (statvfs(path.c_str(), &sv) != 0) {
    PLOG(ERROR) << "statvfs '" << path << "'";
    return false;
  }
  *bytes = sv.f_bfree * sv.f_bsize;
  return true;
}

bool GetUsedSpace(const std::string& path, uint64_t* bytes) {
  static constexpr std::string_view kCurrentDirectory{"."};
  static constexpr std::string_view kParentDirectory{".."};
  static constexpr size_t kBytesPerBlock = 512;  // see manual page for stat(2).

  uint64_t file_bytes = 0;
  std::queue<std::string> unvisited;
  unvisited.push(path);
  while (!unvisited.empty()) {
    std::string current = unvisited.front();
    unvisited.pop();
    std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(current.c_str()), closedir);
    if (!dir) {
      continue;
    }
    for (auto entity = readdir(dir.get()); entity != nullptr; entity = readdir(dir.get())) {
      std::string_view name{entity->d_name};
      if (name == kCurrentDirectory || name == kParentDirectory) {
        continue;
      }
      std::string entity_name = current + "/" + entity->d_name;
      if (entity->d_type == DT_DIR) {
        unvisited.push(entity_name.c_str());
      } else if (entity->d_type == DT_REG) {
        struct stat sb;
        if (stat(entity_name.c_str(), &sb) != 0) {
          PLOG(ERROR) << "Failed to stat() file " << entity_name;
          continue;
        }
        file_bytes += sb.st_blocks * kBytesPerBlock;
      }
    }
  }
  *bytes = file_bytes;
  return true;
}

}  // namespace odrefresh
}  // namespace art
