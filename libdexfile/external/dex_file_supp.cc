/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "art_api/dex_file_support.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <mutex>
#include <sys/stat.h>

#include <android-base/mapped_file.h>
#include <android-base/stringprintf.h>

#ifndef STATIC_LIB
// Not used in the static lib, so avoid a dependency on this header in
// libdexfile_support_static.
#include <log/log.h>
#endif

namespace art_api {
namespace dex {

#define FOR_ALL_DLFUNCS(MACRO) \
  MACRO(DexFile, ExtDexFileOpenFromMemory) \
  MACRO(DexFile, ExtDexFileGetMethodInfoForOffset) \
  MACRO(DexFile, ExtDexFileGetAllMethodInfos) \
  MACRO(DexFile, ExtDexFileClose)

#ifdef STATIC_LIB
#define DEFINE_DLFUNC_PTR(CLASS, DLFUNC) decltype(DLFUNC)* CLASS::g_##DLFUNC = DLFUNC;
#else
#define DEFINE_DLFUNC_PTR(CLASS, DLFUNC) decltype(DLFUNC)* CLASS::g_##DLFUNC = nullptr;
#endif
FOR_ALL_DLFUNCS(DEFINE_DLFUNC_PTR)
#undef DEFINE_DLFUNC_PTR

bool TryLoadLibdexfileExternal([[maybe_unused]] std::string* err_msg) {
#if defined(STATIC_LIB)
  // Nothing to do here since all function pointers are initialised statically.
  return true;
#elif defined(NO_DEXFILE_SUPPORT)
  *err_msg = "Dex file support not available.";
  return false;
#else
  // Use a plain old mutex since we want to try again if loading fails (to set
  // err_msg, if nothing else).
  static std::mutex load_mutex;
  static bool is_loaded = false;
  std::lock_guard<std::mutex> lock(load_mutex);

  if (!is_loaded) {
    // Check which version is already loaded to avoid loading both debug and
    // release builds. We might also be backtracing from separate process, in
    // which case neither is loaded.
    const char* so_name = "libdexfiled.so";
    void* handle = dlopen(so_name, RTLD_NOLOAD | RTLD_NOW | RTLD_NODELETE);
    if (handle == nullptr) {
      so_name = "libdexfile.so";
      handle = dlopen(so_name, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
    }
    if (handle == nullptr) {
      *err_msg = dlerror();
      return false;
    }

#define RESOLVE_DLFUNC_PTR(CLASS, DLFUNC) \
    decltype(DLFUNC)* DLFUNC##_ptr = reinterpret_cast<decltype(DLFUNC)*>(dlsym(handle, #DLFUNC)); \
    if ((DLFUNC) == nullptr) { \
      *err_msg = dlerror(); \
      return false; \
    }
    FOR_ALL_DLFUNCS(RESOLVE_DLFUNC_PTR);
#undef RESOLVE_DLFUNC_PTR

#define SET_DLFUNC_PTR(CLASS, DLFUNC) CLASS::g_##DLFUNC = DLFUNC##_ptr;
    FOR_ALL_DLFUNCS(SET_DLFUNC_PTR);
#undef SET_DLFUNC_PTR

    is_loaded = true;
  }

  return is_loaded;
#endif  // !defined(NO_DEXFILE_SUPPORT) && !defined(STATIC_LIB)
}

void LoadLibdexfileExternal() {
#ifndef STATIC_LIB
  if (std::string err_msg; !TryLoadLibdexfileExternal(&err_msg)) {
    LOG_ALWAYS_FATAL("%s", err_msg.c_str());
  }
#endif
}

DexFile::~DexFile() { g_ExtDexFileClose(ext_dex_file_); }

std::unique_ptr<DexFile> DexFile::OpenFromMemory(const void* addr,
                                                 size_t* size,
                                                 const std::string& location,
                                                 /*out*/ std::string* error_msg) {
  if (UNLIKELY(g_ExtDexFileOpenFromMemory == nullptr)) {
    // Load libdexfile_external.so in this factory function, so instance
    // methods don't need to check this.
    LoadLibdexfileExternal();
  }
  ExtDexFile* ext_dex_file;
  int res = g_ExtDexFileOpenFromMemory(addr, size, location.c_str(), &ext_dex_file);
  switch (static_cast<ExtDexFileError>(res)) {
    case kExtDexFileOk:
      return std::unique_ptr<DexFile>(new DexFile(ext_dex_file));
    case kExtDexFileInvalidHeader:
      *error_msg = std::string("Invalid DexFile header ") + location;
      return nullptr;
    case kExtDexFileNotEnoughData:
      *error_msg = std::string("Not enough data");
      return nullptr;
    case kExtDexFileError:
      break;
  }
  *error_msg = std::string("Failed to open DexFile ") + location;
  return nullptr;
}

std::unique_ptr<DexFile> DexFile::OpenFromFd(int fd,
                                             off_t offset,
                                             const std::string& location,
                                             /*out*/ std::string* error_msg) {
  using android::base::StringPrintf;
  size_t length;
  {
    struct stat sbuf;
    std::memset(&sbuf, 0, sizeof(sbuf));
    if (fstat(fd, &sbuf) == -1) {
      *error_msg = StringPrintf("fstat '%s' failed: %s", location.c_str(), std::strerror(errno));
      return nullptr;
    }
    if (S_ISDIR(sbuf.st_mode)) {
      *error_msg = StringPrintf("Attempt to mmap directory '%s'", location.c_str());
      return nullptr;
    }
    length = sbuf.st_size;
  }

  if (static_cast<off_t>(length) < offset) {
    *error_msg = StringPrintf(
        "Offset %" PRId64 " too large for '%s' of size %zu",
        int64_t{offset},
        location.c_str(),
        length);
    return nullptr;
  }
  length -= offset;

  std::unique_ptr<android::base::MappedFile> map;
  map = android::base::MappedFile::FromFd(fd, offset, length, PROT_READ);
  if (map == nullptr) {
    *error_msg = StringPrintf("mmap '%s' failed: %s", location.c_str(), std::strerror(errno));
    return nullptr;
  }

  std::unique_ptr<DexFile> dex = OpenFromMemory(map->data(), &length, location, error_msg);
  if (dex != nullptr) {
    dex->map_ = std::move(map);  // Ensure the map gets freed with the dex file.
  }
  return dex;
}

MethodInfo DexFile::GetMethodInfoForOffset(int64_t dex_offset, bool with_signature) {
  MethodInfo res{};
  auto set_method = [&res](ExtDexFileMethodInfo* info) { res = AbsorbMethodInfo(info); };
  uint32_t flags = with_signature ? kExtDexFileWithSignature : 0;
  GetMethodInfoForOffset(dex_offset, set_method, flags);
  return res;
}

std::vector<MethodInfo> DexFile::GetAllMethodInfos(bool with_signature) {
  std::vector<MethodInfo> res;
  auto add_method = [&res](ExtDexFileMethodInfo* info) { res.push_back(AbsorbMethodInfo(info)); };
  uint32_t flags = with_signature ? kExtDexFileWithSignature : 0;
  GetAllMethodInfos(add_method, flags);
  return res;
}

}  // namespace dex
}  // namespace art_api
