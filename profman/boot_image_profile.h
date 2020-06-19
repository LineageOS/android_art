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

#ifndef ART_PROFMAN_BOOT_IMAGE_PROFILE_H_
#define ART_PROFMAN_BOOT_IMAGE_PROFILE_H_

#include <limits>
#include <memory>
#include <vector>
#include <set>

#include "base/safe_map.h"
#include "dex/dex_file.h"

namespace art {

class ProfileCompilationInfo;

struct BootImageOptions {
 public:
  // Threshold for preloaded. The threshold specifies, as percentage
  // of maximum number or aggregations, how many different profiles need to have the class
  // before it gets added to the list of preloaded classes.
  uint32_t preloaded_class_threshold = 10;

  // Threshold for classes that may be dirty or clean. The threshold specifies, as percentage
  // of maximum number or aggregations, how many different profiles need to have the class
  // before it gets added to the boot profile.
  uint32_t image_class_threshold = 10;

  // Threshold for classes that are likely to remain clean. The threshold specifies, as percentage
  // of maximum number or aggregations, how many different profiles need to have the class
  // before it gets added to the boot profile.
  uint32_t image_class_clean_threshold = 5;

  // Threshold for including a method in the profile. The threshold specifies, as percentage
  // of maximum number or aggregations, how many different profiles need to have the method
  // before it gets added to the boot profile.
  uint32_t method_threshold = 10;

  // Whether or not we should upgrade the startup methods to hot.
  bool upgrade_startup_to_hot = true;

  // A special set of thresholds (classes and methods) that apply if a method/class is being used
  // by a special package. This can be used to lower the thresholds for methods used by important
  // packages (e.g. system server of system ui) or packages which have special needs (e.g. camera
  // needs more hardware methods).
  SafeMap<std::string, uint32_t> special_packages_thresholds;

  // Whether or not to append package use list to each profile element.
  // Should be use only for debugging as it will add additional elements to the text output
  // that are not compatible with the default profile format.
  bool append_package_use_list = false;

  // The set of classes that should not be preloaded in Zygote
  std::set<std::string> preloaded_classes_blacklist;
};

// Generate a boot image profile according to the specified options.
// Boot classpaths dex files are identified by the given vector and the output is
// written to the two specified paths. The paths will be open with O_CREAT | O_WRONLY.
// Returns true if the generation was successful, false otherwise.
bool GenerateBootImageProfile(
    const std::vector<std::unique_ptr<const DexFile>>& dex_files,
    const ProfileCompilationInfo& profile,
    const BootImageOptions& options,
    const std::string& boot_profile_out_path,
    const std::string& preloaded_classes_out_path);

}  // namespace art

#endif  // ART_PROFMAN_BOOT_IMAGE_PROFILE_H_
