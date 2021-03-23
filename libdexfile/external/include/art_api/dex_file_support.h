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

#ifndef ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_SUPPORT_H_
#define ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_SUPPORT_H_

// C++ wrapper for the dex file external API.

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <android-base/macros.h>
#include <android-base/mapped_file.h>

#include "art_api/dex_file_external.h"

namespace art_api {
namespace dex {

// Returns true if libdexfile_external.so is already loaded. Otherwise tries to
// load it and returns true if successful. Otherwise returns false and sets
// *error_msg. If false is returned then calling any function below may abort
// the process. Thread safe.
bool TryLoadLibdexfileExternal(std::string* error_msg);

// Loads the libdexfile_external.so library and sets up function pointers.
// Aborts with a fatal error on any error. For internal use by the classes
// below.
void LoadLibdexfileExternal();

using DexString = std::string;

struct MethodInfo {
  uint32_t offset = 0;  // Code offset relative to the start of the dex file header
  uint32_t len = 0;     // Code length
  DexString name;
};

inline bool operator==(const MethodInfo& s1, const MethodInfo& s2) {
  return s1.offset == s2.offset && s1.len == s2.len && s1.name == s2.name;
}

// External stable API to access ordinary dex files and CompactDex. This wraps
// the stable C ABI and handles instance ownership. Thread-compatible but not
// thread-safe.
class DexFile {
 public:
  DexFile(DexFile&& dex_file) noexcept {
    std::swap(ext_dex_file_, dex_file.ext_dex_file_);
    std::swap(map_, dex_file.map_);
  }

  explicit DexFile(std::unique_ptr<DexFile>& dex_file) noexcept {
    std::swap(ext_dex_file_, dex_file->ext_dex_file_);
    std::swap(map_, dex_file->map_);
    dex_file.reset();
  }

  virtual ~DexFile();

  // Interprets a chunk of memory as a dex file. As long as *size is too small,
  // returns nullptr, sets *size to a new size to try again with, and sets
  // *error_msg. That might happen repeatedly. Also returns nullptr
  // on error in which case *error_msg is set to a nonempty string.
  //
  // location is a string that describes the dex file, and is preferably its
  // path. It is mostly used to make error messages better, and may be "".
  //
  // The caller must retain the memory.
  static std::unique_ptr<DexFile> OpenFromMemory(const void* addr,
                                                 size_t* size,
                                                 const std::string& location,
                                                 /*out*/ std::string* error_msg);

  // mmaps the given file offset in the open fd and reads a dexfile from there.
  // Returns nullptr on error in which case *error_msg is set.
  //
  // location is a string that describes the dex file, and is preferably its
  // path. It is mostly used to make error messages better, and may be "".
  static std::unique_ptr<DexFile> OpenFromFd(int fd,
                                             off_t offset,
                                             const std::string& location,
                                             /*out*/ std::string* error_msg);

  // Given an offset relative to the start of the dex file header, if there is a
  // method whose instruction range includes that offset then calls the provided
  // callback with ExtDexFileMethodInfo* (which is live only during the callback).
  template<typename T /* lambda taking (ExtDexFileMethodInfo*) */>
  void GetMethodInfoForOffset(int64_t dex_offset, T& callback, uint32_t flags = 0) {
    auto cb = [](void* ctx, ExtDexFileMethodInfo* info) { (*reinterpret_cast<T*>(ctx))(info); };
    g_ExtDexFileGetMethodInfoForOffset(ext_dex_file_, dex_offset, flags, cb, &callback);
  }

  // Given an offset relative to the start of the dex file header, if there is a
  // method whose instruction range includes that offset then returns info about
  // it, otherwise returns a struct with offset == 0. MethodInfo.name receives
  // the full function signature if with_signature is set, otherwise it gets the
  // class and method name only.
  MethodInfo GetMethodInfoForOffset(int64_t dex_offset, bool with_signature);

  // Call the provided callback for all dex methods.
  template<typename T /* lambda taking (ExtDexFileMethodInfo*) */>
  void GetAllMethodInfos(T& callback, uint32_t flags = 0) {
    auto cb = [](void* ctx, ExtDexFileMethodInfo* info) { (*reinterpret_cast<T*>(ctx))(info); };
    g_ExtDexFileGetAllMethodInfos(ext_dex_file_, flags, cb, &callback);
  }

  // Returns info structs about all methods in the dex file. MethodInfo.name
  // receives the full function signature if with_signature is set, otherwise it
  // gets the class and method name only.
  std::vector<MethodInfo> GetAllMethodInfos(bool with_signature);

 private:
  static inline MethodInfo AbsorbMethodInfo(const ExtDexFileMethodInfo* info) {
    return {
      .offset = info->addr,
      .len = info->size,
      .name = std::string(info->name, info->name_size)
    };
  }

  friend bool TryLoadLibdexfileExternal(std::string* error_msg);
  explicit DexFile(ExtDexFile* ext_dex_file) : ext_dex_file_(ext_dex_file) {}
  ExtDexFile* ext_dex_file_ = nullptr;  // Owned instance. nullptr only in moved-from zombies.
  std::unique_ptr<android::base::MappedFile> map_;  // Owned map (if we allocated one).

  // These are initialized by TryLoadLibdexfileExternal.
  static decltype(ExtDexFileOpenFromMemory)* g_ExtDexFileOpenFromMemory;
  static decltype(ExtDexFileGetMethodInfoForOffset)* g_ExtDexFileGetMethodInfoForOffset;
  static decltype(ExtDexFileGetAllMethodInfos)* g_ExtDexFileGetAllMethodInfos;
  static decltype(ExtDexFileClose)* g_ExtDexFileClose;

  DISALLOW_COPY_AND_ASSIGN(DexFile);
};

}  // namespace dex
}  // namespace art_api

#endif  // ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_SUPPORT_H_
