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

#ifndef ART_LIBPROFILE_PROFILE_PROFILE_TEST_HELPER_H_
#define ART_LIBPROFILE_PROFILE_PROFILE_TEST_HELPER_H_

#include <memory>
#include <vector>

#include "dex/test_dex_file_builder.h"
#include "profile/profile_compilation_info.h"

namespace art {

class ProfileTestHelper {
 public:
  ProfileTestHelper() = default;

  using Hotness = ProfileCompilationInfo::MethodHotness;
  using ProfileInlineCache = ProfileMethodInfo::ProfileInlineCache;
  using ProfileSampleAnnotation = ProfileCompilationInfo::ProfileSampleAnnotation;
  using ProfileIndexType = ProfileCompilationInfo::ProfileIndexType;

  static bool AddMethod(
      ProfileCompilationInfo* info,
      const DexFile* dex,
      uint16_t method_idx,
      const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) {
    return AddMethod(info, dex, method_idx, Hotness::kFlagHot, annotation);
  }

  static bool AddMethod(
      ProfileCompilationInfo* info,
      const DexFile* dex,
      uint16_t method_idx,
      Hotness::Flag flags,
      const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) {
    return info->AddMethod(
        ProfileMethodInfo(MethodReference(dex, method_idx)), flags, annotation);
  }

  static bool AddMethod(
      ProfileCompilationInfo* info,
      const DexFile* dex,
      uint16_t method_idx,
      const std::vector<ProfileInlineCache>& inline_caches,
      const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) {
    return AddMethod(info, dex, method_idx, inline_caches, Hotness::kFlagHot, annotation);
  }

  static bool AddMethod(
      ProfileCompilationInfo* info,
      const DexFile* dex,
      uint16_t method_idx,
      const std::vector<ProfileInlineCache>& inline_caches,
      Hotness::Flag flags,
      const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) {
    return info->AddMethod(
        ProfileMethodInfo(MethodReference(dex, method_idx), inline_caches), flags, annotation);
  }

  static bool AddClass(ProfileCompilationInfo* info,
                       const DexFile* dex,
                       dex::TypeIndex type_index,
                       const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) {
    std::vector<dex::TypeIndex> classes = {type_index};
    return info->AddClassesForDex(dex, classes.begin(), classes.end(), annotation);
  }

  static bool ProfileIndexMatchesDexFile(const ProfileCompilationInfo& info,
                                         ProfileIndexType profile_index,
                                         const DexFile* dex_file) {
    DCHECK(dex_file != nullptr);
    std::array<const DexFile*, 1u> dex_files{dex_file};
    return dex_file == info.FindDexFileForProfileIndex(profile_index, dex_files);
  }

  // Compare different representations of inline caches for equality.
  static bool EqualInlineCaches(const std::vector<ProfileMethodInfo::ProfileInlineCache>& expected,
                                const ProfileCompilationInfo::MethodHotness& actual_hotness,
                                const ProfileCompilationInfo& info) {
    CHECK(actual_hotness.IsHot());
    CHECK(actual_hotness.GetInlineCacheMap() != nullptr);
    const ProfileCompilationInfo::InlineCacheMap& actual = *actual_hotness.GetInlineCacheMap();
    if (expected.size() != actual.size()) {
      return false;
    }
    // The `expected` data should be sorted by dex pc.
    CHECK(std::is_sorted(expected.begin(),
                         expected.end(),
                         [](auto&& lhs, auto&& rhs) { return lhs.dex_pc < rhs.dex_pc; }));
    // The `actual` data is a map sorted by dex pc, so we can just iterate over both.
    auto expected_it = expected.begin();
    for (auto it = actual.begin(), end = actual.end(); it != end; ++it, ++expected_it) {
      uint32_t dex_pc = it->first;
      const ProfileCompilationInfo::DexPcData& dex_pc_data = it->second;
      if (dex_pc != expected_it->dex_pc) {
        return false;
      }
      if (dex_pc_data.is_missing_types != expected_it->is_missing_types) {
        return false;
      } else if (dex_pc_data.is_missing_types) {
        continue;  // The classes do not matter if we're missing some types.
      }
      // The `expected_it->is_megamorphic` is not initialized. Check the number of classes.
      bool expected_is_megamorphic =
          (expected_it->classes.size() >= ProfileCompilationInfo::kIndividualInlineCacheSize);
      if (dex_pc_data.is_megamorphic != expected_is_megamorphic) {
        return false;
      } else if (dex_pc_data.is_megamorphic) {
        continue;  // The classes do not matter if the inline cache is megamorphic.
      }
      if (dex_pc_data.classes.size() != expected_it->classes.size()) {
        return false;
      }
      for (const ProfileCompilationInfo::ClassReference& class_ref : dex_pc_data.classes) {
        if (std::none_of(expected_it->classes.begin(),
                         expected_it->classes.end(),
                         [&](const TypeReference& type_ref) {
                           return (class_ref.type_index == type_ref.TypeIndex()) &&
                                  ProfileIndexMatchesDexFile(info,
                                                             class_ref.dex_profile_index,
                                                             type_ref.dex_file);
                         })) {
          return false;
        }
      }
    }
    return true;
  }

 protected:
  static constexpr size_t kNumSharedTypes = 10u;

  const DexFile* BuildDex(const std::string& location,
                          uint32_t location_checksum,
                          const std::string& class_descriptor,
                          size_t num_method_ids,
                          size_t num_class_ids = kNumSharedTypes + 1u) {
    TestDexFileBuilder builder;
    for (size_t shared_type_index = 0; shared_type_index != kNumSharedTypes; ++shared_type_index) {
      builder.AddType("LSharedType" + std::to_string(shared_type_index) + ";");
    }
    builder.AddType(class_descriptor);
    for (size_t i = kNumSharedTypes + 1u; i < num_class_ids; ++i) {
      builder.AddType("LFiller" + std::to_string(i) + ";");
    }
    for (size_t method_index = 0; method_index != num_method_ids; ++method_index) {
      // Keep the number of protos and names low even for the maximum number of methods.
      size_t return_type_index = method_index % kNumSharedTypes;
      size_t arg_type_index = (method_index / kNumSharedTypes) % kNumSharedTypes;
      size_t method_name_index = (method_index / kNumSharedTypes) / kNumSharedTypes;
      std::string return_type = "LSharedType" + std::to_string(return_type_index) + ";";
      std::string arg_type = "LSharedType" + std::to_string(arg_type_index) + ";";
      std::string signature = "(" + arg_type + ")" + return_type;
      builder.AddMethod(class_descriptor, signature, "m" + std::to_string(method_name_index));
    }
    storage.push_back(builder.Build(location, location_checksum));
    return storage.back().get();
  }

  std::vector<std::unique_ptr<const DexFile>> storage;
};

}  // namespace art

#endif  // ART_LIBPROFILE_PROFILE_PROFILE_TEST_HELPER_H_
