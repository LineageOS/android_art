/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef ART_RUNTIME_GC_SPACE_IMAGE_SPACE_FS_H_
#define ART_RUNTIME_GC_SPACE_IMAGE_SPACE_FS_H_

#include <dirent.h>
#include <dlfcn.h>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include "base/file_utils.h"
#include "base/logging.h"  // For VLOG.
#include "base/macros.h"
#include "base/os.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "runtime.h"
#include "runtime_globals.h"

namespace art {
namespace gc {
namespace space {

// This file contains helper code for ImageSpace. It has most of the file-system
// related code, including handling A/B OTA.

namespace impl {

// Delete the directory and its (regular or link) contents. If the recurse flag is true, delete
// sub-directories recursively.
static void DeleteDirectoryContents(const std::string& dir, bool recurse) {
  if (!OS::DirectoryExists(dir.c_str())) {
    return;
  }
  DIR* c_dir = opendir(dir.c_str());
  if (c_dir == nullptr) {
    PLOG(WARNING) << "Unable to open " << dir << " to delete it's contents";
    return;
  }

  for (struct dirent* de = readdir(c_dir); de != nullptr; de = readdir(c_dir)) {
    const char* name = de->d_name;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
      continue;
    }
    // We only want to delete regular files and symbolic links.
    std::string file = android::base::StringPrintf("%s/%s", dir.c_str(), name);
    if (de->d_type != DT_REG && de->d_type != DT_LNK) {
      if (de->d_type == DT_DIR) {
        if (recurse) {
          DeleteDirectoryContents(file, recurse);
          // Try to rmdir the directory.
          if (rmdir(file.c_str()) != 0) {
            PLOG(ERROR) << "Unable to rmdir " << file;
          }
        }
      } else {
        LOG(WARNING) << "Unexpected file type of " << std::hex << de->d_type << " encountered.";
      }
    } else {
      // Try to unlink the file.
      if (unlink(file.c_str()) != 0) {
        PLOG(ERROR) << "Unable to unlink " << file;
      }
    }
  }
  CHECK_EQ(0, closedir(c_dir)) << "Unable to close directory.";
}

}  // namespace impl


// We are relocating or generating the core image. We should get rid of everything. It is all
// out-of-date. We also don't really care if this fails since it is just a convenience.
// Adapted from prune_dex_cache(const char* subdir) in frameworks/native/cmds/installd/commands.c
// Note this should only be used during first boot.
static void PruneDalvikCache(InstructionSet isa) {
  CHECK_NE(isa, InstructionSet::kNone);
  // Prune the base /data/dalvik-cache.
  // Note: GetDalvikCache may return the empty string if the directory doesn't
  // exist. It is safe to pass "" to DeleteDirectoryContents, so this is okay.
  impl::DeleteDirectoryContents(GetDalvikCache("."), false);
  // Prune /data/dalvik-cache/<isa>.
  impl::DeleteDirectoryContents(GetDalvikCache(GetInstructionSetString(isa)), false);

  // Be defensive. There should be a runtime created here, but this may be called in a test.
  if (Runtime::Current() != nullptr) {
    Runtime::Current()->SetPrunedDalvikCache(true);
  }
}

}  // namespace space
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_SPACE_IMAGE_SPACE_FS_H_
