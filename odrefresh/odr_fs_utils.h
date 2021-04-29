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

#ifndef ART_ODREFRESH_ODR_FS_UTILS_H_
#define ART_ODREFRESH_ODR_FS_UTILS_H_

#include <cstdint>
#include <iosfwd>

#include <android-base/macros.h>

namespace art {
namespace odrefresh {

// Cleans directory by removing all files and sub-directories under `dir_path`.
// Returns true on success, false otherwise.
WARN_UNUSED bool CleanDirectory(const std::string& dir_path);

// Create all directories on `absolute_dir_path`.
// Returns true on success, false otherwise.
WARN_UNUSED bool EnsureDirectoryExists(const std::string& absolute_dir_path);

// Get free space for filesystem containing `path`.
// Returns true on success, false otherwise.
WARN_UNUSED bool GetFreeSpace(const std::string& path, uint64_t* bytes);

// Gets space used under directory `dir_path`.
// Returns true on success, false otherwise.
WARN_UNUSED bool GetUsedSpace(const std::string& dir_path, uint64_t* bytes);

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_ODR_FS_UTILS_H_
