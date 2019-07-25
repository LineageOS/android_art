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

#ifndef ART_LIBPROFILE_PROFILE_PROFILE_BOOT_INFO_H_
#define ART_LIBPROFILE_PROFILE_PROFILE_BOOT_INFO_H_

#include <vector>

#include "base/value_object.h"

namespace art {

class DexFile;

/**
 * Abstraction over a list of methods representing the boot profile
 * of an application. The order in the list is the order in which the methods
 * should be compiled.
 *
 * TODO: This is currently implemented as a separate profile to
 * ProfileCompilationInfo to enable fast experiments, but we are likely to
 * incorporate it in ProfileCompilationInfo once we settle on an automated way
 * to generate such a boot profile.
 */
class ProfileBootInfo : public ValueObject {
 public:
  // Add the given method located in the given dex file in the profile.
  void Add(const DexFile* dex_file, uint32_t method_index);

  // Save this profile boot info into the `fd` file descriptor.
  bool Save(int fd) const;

  // Load the profile listing from `fd` into this profile boot info. Note that
  // the profile boot info will store internally the dex files being passed.
  bool Load(int fd, const std::vector<const DexFile*>& dex_files);

  const std::vector<const DexFile*>& GetDexFiles() const {
    return dex_files_;
  }

  const std::vector<std::pair<uint32_t, uint32_t>>& GetMethods() const {
    return methods_;
  }

  bool IsEmpty() const { return dex_files_.empty() && methods_.empty(); }

 private:
  // List of dex files this boot profile info covers.
  std::vector<const DexFile*> dex_files_;

  // List of pair of <dex file index, method_id> methods to be compiled in
  // order.
  std::vector<std::pair<uint32_t, uint32_t>> methods_;
};

}  // namespace art

#endif  // ART_LIBPROFILE_PROFILE_PROFILE_BOOT_INFO_H_
