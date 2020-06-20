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

#include "boot_image_profile.h"

#include <memory>
#include <set>

#include "android-base/file.h"
#include "base/unix_file/fd_file.h"
#include "dex/class_accessor-inl.h"
#include "dex/descriptors_names.h"
#include "dex/dex_file-inl.h"
#include "dex/method_reference.h"
#include "dex/type_reference.h"
#include "profile/profile_compilation_info.h"

namespace art {

using Hotness = ProfileCompilationInfo::MethodHotness;

static const std::string kMethodSep = "->";  // NOLINT [runtime/string] [4]
static const std::string kPackageUseDelim = "@";  // NOLINT [runtime/string] [4]
static constexpr char kMethodFlagStringHot = 'H';
static constexpr char kMethodFlagStringStartup = 'S';
static constexpr char kMethodFlagStringPostStartup = 'P';

// Returns the type descriptor of the given reference.
static std::string GetTypeDescriptor(const TypeReference& ref) {
  const dex::TypeId& type_id = ref.dex_file->GetTypeId(ref.TypeIndex());
  return ref.dex_file->GetTypeDescriptor(type_id);
}

// Returns the method representation used in the text format of the boot image profile.
static std::string BootImageRepresentation(const MethodReference& ref) {
  const DexFile* dex_file = ref.dex_file;
  const dex::MethodId& id = ref.GetMethodId();
  std::string signature_string(dex_file->GetMethodSignature(id).ToString());
  std::string type_string(dex_file->GetTypeDescriptor(dex_file->GetTypeId(id.class_idx_)));
  std::string method_name(dex_file->GetMethodName(id));
  return type_string +
        kMethodSep +
        method_name +
        signature_string;
}

// Returns the class representation used in the text format of the boot image profile.
static std::string BootImageRepresentation(const TypeReference& ref) {
  return GetTypeDescriptor(ref);
}

// Returns the class representation used in preloaded classes.
static std::string PreloadedClassesRepresentation(const TypeReference& ref) {
  std::string descriptor = GetTypeDescriptor(ref);
  return DescriptorToDot(descriptor.c_str());
}

// Formats the list of packages from the item metadata as a debug string.
static std::string GetPackageUseString(const FlattenProfileData::ItemMetadata& metadata) {
  std::string result;
  for (const auto& it : metadata.GetAnnotations()) {
    result += it.GetOriginPackageName() + ",";
  }

  return metadata.GetAnnotations().empty()
      ? result
      : result.substr(0, result.size() - 1);
}

// Converts a method representation to its final profile format.
static std::string MethodToProfileFormat(
    const std::string& method,
    const FlattenProfileData::ItemMetadata& metadata,
    bool output_package_use) {
  std::string flags_string;
  if (metadata.HasFlagSet(Hotness::kFlagHot)) {
    flags_string += kMethodFlagStringHot;
  }
  if (metadata.HasFlagSet(Hotness::kFlagStartup)) {
    flags_string += kMethodFlagStringStartup;
  }
  if (metadata.HasFlagSet(Hotness::kFlagPostStartup)) {
    flags_string += kMethodFlagStringPostStartup;
  }
  std::string extra;
  if (output_package_use) {
    extra = kPackageUseDelim + GetPackageUseString(metadata);
  }

  return flags_string + method + extra;
}

// Converts a class representation to its final profile or preloaded classes format.
static std::string ClassToProfileFormat(
    const std::string& classString,
    const FlattenProfileData::ItemMetadata& metadata,
    bool output_package_use) {
  std::string extra;
  if (output_package_use) {
    extra = kPackageUseDelim + GetPackageUseString(metadata);
  }

  return classString + extra;
}

// Tries to asses if the given type reference is a clean class.
static bool MaybeIsClassClean(const TypeReference& ref) {
  const dex::ClassDef* class_def = ref.dex_file->FindClassDef(ref.TypeIndex());
  if (class_def == nullptr) {
    return false;
  }

  ClassAccessor accessor(*ref.dex_file, *class_def);
  for (auto& it : accessor.GetStaticFields()) {
    if (!it.IsFinal()) {
      // Not final static field will probably dirty the class.
      return false;
    }
  }
  for (auto& it : accessor.GetMethods()) {
    uint32_t flags = it.GetAccessFlags();
    if ((flags & kAccNative) != 0) {
      // Native method will get dirtied.
      return false;
    }
    if ((flags & kAccConstructor) != 0 && (flags & kAccStatic) != 0) {
      // Class initializer, may get dirtied (not sure).
      return false;
    }
  }

  return true;
}

// Returns true iff the item should be included in the profile.
// (i.e. it passes the given aggregation thresholds)
static bool IncludeItemInProfile(uint32_t max_aggregation_count,
                                 uint32_t item_threshold,
                                 const FlattenProfileData::ItemMetadata& metadata,
                                 const BootImageOptions& options) {
  CHECK_NE(max_aggregation_count, 0u);
  float item_percent = metadata.GetAnnotations().size() / static_cast<float>(max_aggregation_count);
  for (const auto& annotIt : metadata.GetAnnotations()) {
    const auto&thresholdIt =
        options.special_packages_thresholds.find(annotIt.GetOriginPackageName());
    if (thresholdIt != options.special_packages_thresholds.end()) {
      if (item_percent >= (thresholdIt->second / 100.f)) {
        return true;
      }
    }
  }
  return item_percent >= (item_threshold / 100.f);
}

// Returns true iff a method with the given metada should be included in the profile.
static bool IncludeMethodInProfile(uint32_t max_aggregation_count,
                                   const FlattenProfileData::ItemMetadata& metadata,
                                   const BootImageOptions& options) {
  return IncludeItemInProfile(max_aggregation_count, options.method_threshold, metadata, options);
}

// Returns true iff a class with the given metada should be included in the profile.
static bool IncludeClassInProfile(const TypeReference& type_ref,
                                  uint32_t max_aggregation_count,
                                  const FlattenProfileData::ItemMetadata& metadata,
                                  const BootImageOptions& options) {
  uint32_t threshold = MaybeIsClassClean(type_ref)
      ? options.image_class_clean_threshold
      : options.image_class_threshold;
  return IncludeItemInProfile(max_aggregation_count, threshold, metadata, options);
}

// Returns true iff a class with the given metada should be included in the list of
// prelaoded classes.
static bool IncludeInPreloadedClasses(const std::string& class_name,
                                      uint32_t max_aggregation_count,
                                      const FlattenProfileData::ItemMetadata& metadata,
                                      const BootImageOptions& options) {
  bool blacklisted = options.preloaded_classes_blacklist.find(class_name) !=
      options.preloaded_classes_blacklist.end();
  return !blacklisted && IncludeItemInProfile(
      max_aggregation_count, options.preloaded_class_threshold, metadata, options);
}

bool GenerateBootImageProfile(
    const std::vector<std::unique_ptr<const DexFile>>& dex_files,
    const std::vector<std::string>& profile_files,
    const BootImageOptions& options,
    const std::string& boot_profile_out_path,
    const std::string& preloaded_classes_out_path) {
  if (boot_profile_out_path.empty()) {
    LOG(ERROR) << "No output file specified";
    return false;
  }

  bool generate_preloaded_classes = !preloaded_classes_out_path.empty();

  std::unique_ptr<FlattenProfileData> flattend_data(new FlattenProfileData());
  for (const std::string& profile_file : profile_files) {
    ProfileCompilationInfo profile;
    if (!profile.Load(profile_file, /*clear_if_invalid=*/ false)) {
      LOG(ERROR) << "Profile is not a valid: " << profile_file;
      return false;
    }
    std::unique_ptr<FlattenProfileData> currentData = profile.ExtractProfileData(dex_files);
    flattend_data->MergeData(*currentData);
  }

  // We want the output sorted by the method/class name.
  // So we use an intermediate map for that.
  // There's no attempt to optimize this as it's not part of any critical path,
  // and mostly executed on hosts.
  SafeMap<std::string, FlattenProfileData::ItemMetadata> profile_methods;
  SafeMap<std::string, FlattenProfileData::ItemMetadata> profile_classes;
  SafeMap<std::string, FlattenProfileData::ItemMetadata> preloaded_classes;

  for (const auto& it : flattend_data->GetMethodData()) {
    if (IncludeMethodInProfile(flattend_data->GetMaxAggregationForMethods(), it.second, options)) {
      FlattenProfileData::ItemMetadata metadata(it.second);
      if (options.upgrade_startup_to_hot
          && ((metadata.GetFlags() & Hotness::Flag::kFlagStartup) != 0)) {
        metadata.AddFlag(Hotness::Flag::kFlagHot);
      }
      profile_methods.Put(BootImageRepresentation(it.first), metadata);
    }
  }

  for (const auto& it : flattend_data->GetClassData()) {
    const TypeReference& type_ref = it.first;
    const FlattenProfileData::ItemMetadata& metadata = it.second;
    if (IncludeClassInProfile(type_ref,
            flattend_data->GetMaxAggregationForClasses(),
            metadata,
            options)) {
      profile_classes.Put(BootImageRepresentation(it.first), it.second);
    }
    std::string preloaded_class_representation = PreloadedClassesRepresentation(it.first);
    if (generate_preloaded_classes && IncludeInPreloadedClasses(
            preloaded_class_representation,
            flattend_data->GetMaxAggregationForClasses(),
            metadata,
            options)) {
      preloaded_classes.Put(preloaded_class_representation, it.second);
    }
  }

  // Create the output content
  std::string profile_content;
  std::string preloaded_content;
  for (const auto& it : profile_classes) {
    profile_content += ClassToProfileFormat(it.first, it.second, options.append_package_use_list)
        + "\n";
  }
  for (const auto& it : profile_methods) {
    profile_content += MethodToProfileFormat(it.first, it.second, options.append_package_use_list)
        + "\n";
  }

  if (generate_preloaded_classes) {
    for (const auto& it : preloaded_classes) {
      preloaded_content +=
          ClassToProfileFormat(it.first, it.second, options.append_package_use_list) + "\n";
    }
  }

  return android::base::WriteStringToFile(profile_content, boot_profile_out_path)
      && (!generate_preloaded_classes
          || android::base::WriteStringToFile(preloaded_content, preloaded_classes_out_path));
}

}  // namespace art
