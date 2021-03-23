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

#ifndef ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_EXTERNAL_H_
#define ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_EXTERNAL_H_

// Dex file external API

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// This is the stable C ABI that backs art_api::dex below. Structs and functions
// may only be added here. C++ users should use dex_file_support.h instead.

struct ExtDexFileMethodInfo {
  size_t sizeof_struct;  // Size of this structure (to allow future extensions).
  uint32_t addr;  // Start of dex byte-code relative to the start of the dex file.
  uint32_t size;  // Size of the dex byte-code in bytes.
  const char* name;
  size_t name_size;
};

enum ExtDexFileError {
  kExtDexFileOk = 0,
  kExtDexFileError = 1,  // Unspecified error.
  kExtDexFileNotEnoughData = 2,
  kExtDexFileInvalidHeader = 3,
};

enum ExtDexFileMethodFlags {
  kExtDexFileWithSignature = 1,
};

struct ExtDexFile;

// Try to open a dex file in the given memory range.
// If the memory range is too small, larger suggest size is written to the argument.
int ExtDexFileOpenFromMemory(const void* addr,
                             /*inout*/ size_t* size,
                             const char* location,
                             /*out*/ struct ExtDexFile** ext_dex_file);

// Callback used to return information about a dex method.
typedef void ExtDexFileMethodInfoCallback(void* user_data,
                                          struct ExtDexFileMethodInfo* method_info);

// Find a single dex method based on the given dex offset.
int ExtDexFileGetMethodInfoForOffset(struct ExtDexFile* ext_dex_file,
                                     uint32_t dex_offset,
                                     uint32_t flags,
                                     ExtDexFileMethodInfoCallback* method_info_cb,
                                     void* user_data);

// Return all dex methods in the dex file.
void ExtDexFileGetAllMethodInfos(struct ExtDexFile* ext_dex_file,
                                 uint32_t flags,
                                 ExtDexFileMethodInfoCallback* method_info_cb,
                                 void* user_data);

// Release all associated memory.
void ExtDexFileClose(struct ExtDexFile* ext_dex_file);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // ART_LIBDEXFILE_EXTERNAL_INCLUDE_ART_API_DEX_FILE_EXTERNAL_H_
