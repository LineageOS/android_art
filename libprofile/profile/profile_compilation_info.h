/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef ART_LIBPROFILE_PROFILE_PROFILE_COMPILATION_INFO_H_
#define ART_LIBPROFILE_PROFILE_PROFILE_COMPILATION_INFO_H_

#include <array>
#include <list>
#include <set>
#include <string_view>
#include <vector>

#include "base/arena_containers.h"
#include "base/arena_object.h"
#include "base/array_ref.h"
#include "base/atomic.h"
#include "base/bit_memory_region.h"
#include "base/hash_map.h"
#include "base/hash_set.h"
#include "base/malloc_arena_pool.h"
#include "base/mem_map.h"
#include "base/safe_map.h"
#include "dex/dex_file.h"
#include "dex/dex_file_types.h"
#include "dex/method_reference.h"
#include "dex/type_reference.h"

namespace art {

/**
 *  Convenient class to pass around profile information (including inline caches)
 *  without the need to hold GC-able objects.
 */
struct ProfileMethodInfo {
  struct ProfileInlineCache {
    ProfileInlineCache(uint32_t pc,
                       bool missing_types,
                       const std::vector<TypeReference>& profile_classes,
                       // Only used by profman for creating profiles from text
                       bool megamorphic = false)
        : dex_pc(pc),
          is_missing_types(missing_types),
          classes(profile_classes),
          is_megamorphic(megamorphic) {}

    const uint32_t dex_pc;
    const bool is_missing_types;
    // TODO: Replace `TypeReference` with `dex::TypeIndex` and allow artificial
    // type indexes for types without a `dex::TypeId` in any dex file processed
    // by the profman. See `ProfileCompilationInfo::FindOrCreateTypeIndex()`.
    const std::vector<TypeReference> classes;
    const bool is_megamorphic;
  };

  explicit ProfileMethodInfo(MethodReference reference) : ref(reference) {}

  ProfileMethodInfo(MethodReference reference, const std::vector<ProfileInlineCache>& caches)
      : ref(reference),
        inline_caches(caches) {}

  MethodReference ref;
  std::vector<ProfileInlineCache> inline_caches;
};

class FlattenProfileData;

/**
 * Profile information in a format suitable to be queried by the compiler and
 * performing profile guided compilation.
 * It is a serialize-friendly format based on information collected by the
 * interpreter (ProfileInfo).
 * Currently it stores only the hot compiled methods.
 */
class ProfileCompilationInfo {
 public:
  static const uint8_t kProfileMagic[];
  static const uint8_t kProfileVersion[];
  static const uint8_t kProfileVersionForBootImage[];
  static const char kDexMetadataProfileEntry[];

  static constexpr size_t kProfileVersionSize = 4;
  static constexpr uint8_t kIndividualInlineCacheSize = 5;

  // Data structures for encoding the offline representation of inline caches.
  // This is exposed as public in order to make it available to dex2oat compilations
  // (see compiler/optimizing/inliner.cc).

  // The type used to manipulate the profile index of dex files.
  // It sets an upper limit to how many dex files a given profile can record.
  using ProfileIndexType = uint16_t;

  // Encodes a class reference in the profile.
  // The owning dex file is encoded as the index (dex_profile_index) it has in the
  // profile rather than as a full reference (location, checksum).
  // This avoids excessive string copying when managing the profile data.
  // The dex_profile_index is an index in the `DexFileData::profile_index` (internal use)
  // and a matching dex file can found with `FindDexFileForProfileIndex()`.
  // Note that the dex_profile_index is not necessary the multidex index.
  // We cannot rely on the actual multidex index because a single profile may store
  // data from multiple splits. This means that a profile may contain a classes2.dex from split-A
  // and one from split-B.
  struct ClassReference : public ValueObject {
    ClassReference(ProfileIndexType dex_profile_idx, const dex::TypeIndex type_idx) :
      dex_profile_index(dex_profile_idx), type_index(type_idx) {}

    bool operator==(const ClassReference& other) const {
      return dex_profile_index == other.dex_profile_index && type_index == other.type_index;
    }
    bool operator<(const ClassReference& other) const {
      return dex_profile_index == other.dex_profile_index
          ? type_index < other.type_index
          : dex_profile_index < other.dex_profile_index;
    }

    ProfileIndexType dex_profile_index;  // the index of the owning dex in the profile info
    dex::TypeIndex type_index;  // the type index of the class
  };

  // Encodes the actual inline cache for a given dex pc (whether or not the receiver is
  // megamorphic and its possible types).
  // If the receiver is megamorphic or is missing types the set of classes will be empty.
  struct DexPcData : public ArenaObject<kArenaAllocProfile> {
    explicit DexPcData(ArenaAllocator* allocator)
        : DexPcData(allocator->Adapter(kArenaAllocProfile)) {}
    explicit DexPcData(const ArenaAllocatorAdapter<void>& allocator)
        : is_missing_types(false),
          is_megamorphic(false),
          classes(std::less<dex::TypeIndex>(), allocator) {}
    void AddClass(const dex::TypeIndex& type_idx);
    void SetIsMegamorphic() {
      if (is_missing_types) return;
      is_megamorphic = true;
      classes.clear();
    }
    void SetIsMissingTypes() {
      is_megamorphic = false;
      is_missing_types = true;
      classes.clear();
    }
    bool operator==(const DexPcData& other) const {
      return is_megamorphic == other.is_megamorphic &&
          is_missing_types == other.is_missing_types &&
          classes == other.classes;
    }

    // Not all runtime types can be encoded in the profile. For example if the receiver
    // type is in a dex file which is not tracked for profiling its type cannot be
    // encoded. When types are missing this field will be set to true.
    bool is_missing_types;
    bool is_megamorphic;
    ArenaSet<dex::TypeIndex> classes;
  };

  // The inline cache map: DexPc -> DexPcData.
  using InlineCacheMap = ArenaSafeMap<uint16_t, DexPcData>;

  // Maps a method dex index to its inline cache.
  using MethodMap = ArenaSafeMap<uint16_t, InlineCacheMap>;

  // Profile method hotness information for a single method. Also includes a pointer to the inline
  // cache map.
  class MethodHotness {
   public:
    enum Flag {
      // Marker flag used to simplify iterations.
      kFlagFirst = 1 << 0,
      // The method is profile-hot (this is implementation specific, e.g. equivalent to JIT-warm)
      kFlagHot = 1 << 0,
      // Executed during the app startup as determined by the runtime.
      kFlagStartup = 1 << 1,
      // Executed after app startup as determined by the runtime.
      kFlagPostStartup = 1 << 2,
      // Marker flag used to simplify iterations.
      kFlagLastRegular = 1 << 2,
      // Executed by a 32bit process.
      kFlag32bit = 1 << 3,
      // Executed by a 64bit process.
      kFlag64bit = 1 << 4,
      // Executed on sensitive thread (e.g. UI).
      kFlagSensitiveThread = 1 << 5,
      // Executed during the app startup as determined by the framework (equivalent to am start).
      kFlagAmStartup = 1 << 6,
      // Executed after the app startup as determined by the framework (equivalent to am start).
      kFlagAmPostStartup = 1 << 7,
      // Executed during system boot.
      kFlagBoot = 1 << 8,
      // Executed after the system has booted.
      kFlagPostBoot = 1 << 9,

      // The startup bins captured the relative order of when a method become hot. There are 6
      // total bins supported and each hot method will have at least one bit set. If the profile was
      // merged multiple times more than one bit may be set as a given method may become hot at
      // various times during subsequent executions.
      // The granularity of the bins is unspecified (i.e. the runtime is free to change the
      // values it uses - this may be 100ms, 200ms etc...).
      kFlagStartupBin = 1 << 10,
      kFlagStartupMaxBin = 1 << 15,
      // Marker flag used to simplify iterations.
      kFlagLastBoot = 1 << 15,
    };

    bool IsHot() const {
      return (flags_ & kFlagHot) != 0;
    }

    bool IsStartup() const {
      return (flags_ & kFlagStartup) != 0;
    }

    bool IsPostStartup() const {
      return (flags_ & kFlagPostStartup) != 0;
    }

    void AddFlag(Flag flag) {
      flags_ |= flag;
    }

    uint32_t GetFlags() const {
      return flags_;
    }

    bool HasFlagSet(MethodHotness::Flag flag) {
      return (flags_ & flag ) != 0;
    }

    bool IsInProfile() const {
      return flags_ != 0;
    }

    const InlineCacheMap* GetInlineCacheMap() const {
      return inline_cache_map_;
    }

   private:
    const InlineCacheMap* inline_cache_map_ = nullptr;
    uint32_t flags_ = 0;

    void SetInlineCacheMap(const InlineCacheMap* info) {
      inline_cache_map_ = info;
    }

    friend class ProfileCompilationInfo;
  };

  // Encapsulates metadata that can be associated with the methods and classes added to the profile.
  // The additional metadata is serialized in the profile and becomes part of the profile key
  // representation. It can be used to differentiate the samples that are added to the profile
  // based on the supported criteria (e.g. keep track of which app generated what sample when
  // constructing a boot profile.).
  class ProfileSampleAnnotation {
   public:
    explicit ProfileSampleAnnotation(const std::string& package_name) :
        origin_package_name_(package_name) {}

    const std::string& GetOriginPackageName() const { return origin_package_name_; }

    bool operator==(const ProfileSampleAnnotation& other) const {
      return origin_package_name_ == other.origin_package_name_;
    }

    bool operator<(const ProfileSampleAnnotation& other) const {
      return origin_package_name_ < other.origin_package_name_;
    }

    // A convenient empty annotation object that can be used to denote that no annotation should
    // be associated with the profile samples.
    static const ProfileSampleAnnotation kNone;

   private:
    // The name of the package that generated the samples.
    const std::string origin_package_name_;
  };

  // Helper class for printing referenced dex file information to a stream.
  struct DexReferenceDumper;

  // Public methods to create, extend or query the profile.
  ProfileCompilationInfo();
  explicit ProfileCompilationInfo(bool for_boot_image);
  explicit ProfileCompilationInfo(ArenaPool* arena_pool);
  ProfileCompilationInfo(ArenaPool* arena_pool, bool for_boot_image);

  ~ProfileCompilationInfo();

  // Add the given methods to the current profile object.
  //
  // Note: if an annotation is provided, the methods/classes will be associated with the group
  // (dex_file, sample_annotation). Each group keeps its unique set of methods/classes.
  bool AddMethods(const std::vector<ProfileMethodInfo>& methods,
                  MethodHotness::Flag flags,
                  const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone);

  // Find a type index in the `dex_file` if there is a `TypeId` for it. Otherwise,
  // find or insert the descriptor in "extra descriptors" and return an artificial
  // type index beyond `dex_file.NumTypeIds()`. This fails if the artificial index
  // would be kDexNoIndex16 (0xffffu) or higher, returning an invalid type index.
  // The returned type index can be used, if valid, for `AddClass()` or (TODO) as
  // a type index for inline caches.
  dex::TypeIndex FindOrCreateTypeIndex(const DexFile& dex_file, TypeReference class_ref);
  dex::TypeIndex FindOrCreateTypeIndex(const DexFile& dex_file, const char* descriptor);

  // Add a class with the specified `type_index` to the profile. The `type_index`
  // can be either a normal index for a `TypeId` in the dex file, or an artificial
  // type index created by `FindOrCreateTypeIndex()`.
  // Returns `true` on success, `false` on failure.
  bool AddClass(const DexFile& dex_file,
                dex::TypeIndex type_index,
                const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone);

  // Add a class with the specified `descriptor` to the profile.
  // Returns `true` on success, `false` on failure.
  bool AddClass(const DexFile& dex_file,
                const char* descriptor,
                const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone);
  bool AddClass(const DexFile& dex_file,
                const std::string& descriptor,
                const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) {
    return AddClass(dex_file, descriptor.c_str(), annotation);
  }
  bool AddClass(const DexFile& dex_file,
                std::string_view descriptor,
                const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) {
    return AddClass(dex_file, std::string(descriptor).c_str(), annotation);
  }

  // Add multiple type ids for classes in a single dex file. Iterator is for type_ids not
  // class_defs.
  //
  // Note: see AddMethods docs for the handling of annotations.
  template <class Iterator>
  bool AddClassesForDex(
      const DexFile* dex_file,
      Iterator index_begin,
      Iterator index_end,
      const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) {
    DexFileData* data = GetOrAddDexFileData(dex_file, annotation);
    if (data == nullptr) {
      return false;
    }
    data->class_set.insert(index_begin, index_end);
    return true;
  }

  // Add a method to the profile using its online representation (containing runtime structures).
  //
  // Note: see AddMethods docs for the handling of annotations.
  bool AddMethod(const ProfileMethodInfo& pmi,
                 MethodHotness::Flag flags,
                 const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone);

  // Bulk add sampled methods and/or hot methods for a single dex, fast since it only has one
  // GetOrAddDexFileData call.
  //
  // Note: see AddMethods docs for the handling of annotations.
  template <class Iterator>
  bool AddMethodsForDex(
      MethodHotness::Flag flags,
      const DexFile* dex_file,
      Iterator index_begin,
      Iterator index_end,
      const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) {
    DexFileData* data = GetOrAddDexFileData(dex_file, annotation);
    if (data == nullptr) {
      return false;
    }
    for (Iterator it = index_begin; it != index_end; ++it) {
      DCHECK_LT(*it, data->num_method_ids);
      if (!data->AddMethod(flags, *it)) {
        return false;
      }
    }
    return true;
  }

  // Load or Merge profile information from the given file descriptor.
  // If the current profile is non-empty the load will fail.
  // If merge_classes is set to false, classes will not be merged/loaded.
  // If filter_fn is present, it will be used to filter out profile data belonging
  // to dex file which do not comply with the filter
  // (i.e. for which filter_fn(dex_location, dex_checksum) is false).
  using ProfileLoadFilterFn = std::function<bool(const std::string&, uint32_t)>;
  // Profile filter method which accepts all dex locations.
  // This is convenient to use when we need to accept all locations without repeating the same
  // lambda.
  static bool ProfileFilterFnAcceptAll(const std::string& dex_location, uint32_t checksum);

  bool Load(
      int fd,
      bool merge_classes = true,
      const ProfileLoadFilterFn& filter_fn = ProfileFilterFnAcceptAll);

  // Verify integrity of the profile file with the provided dex files.
  // If there exists a DexData object which maps to a dex_file, then it verifies that:
  // - The checksums of the DexData and dex_file are equals.
  // - No method id exceeds NumMethodIds corresponding to the dex_file.
  // - No class id exceeds NumTypeIds corresponding to the dex_file.
  // - For every inline_caches, class_ids does not exceed NumTypeIds corresponding to
  //   the dex_file they are in.
  bool VerifyProfileData(const std::vector<const DexFile*>& dex_files);

  // Load profile information from the given file
  // If the current profile is non-empty the load will fail.
  // If clear_if_invalid is true and the file is invalid the method clears the
  // the file and returns true.
  bool Load(const std::string& filename, bool clear_if_invalid);

  // Merge the data from another ProfileCompilationInfo into the current object. Only merges
  // classes if merge_classes is true. This is used for creating the boot profile since
  // we don't want all of the classes to be image classes.
  bool MergeWith(const ProfileCompilationInfo& info, bool merge_classes = true);

  // Merge profile information from the given file descriptor.
  bool MergeWith(const std::string& filename);

  // Save the profile data to the given file descriptor.
  bool Save(int fd);

  // Save the current profile into the given file. The file will be cleared before saving.
  bool Save(const std::string& filename, uint64_t* bytes_written);

  // Return the number of dex files referenced in the profile.
  size_t GetNumberOfDexFiles() const {
    return info_.size();
  }

  // Return the number of methods that were profiled.
  uint32_t GetNumberOfMethods() const;

  // Return the number of resolved classes that were profiled.
  uint32_t GetNumberOfResolvedClasses() const;

  // Returns the profile method info for a given method reference.
  //
  // Note that if the profile was built with annotations, the same dex file may be
  // represented multiple times in the profile (due to different annotation associated with it).
  // If so, and if no annotation is passed to this method, then only the first dex file is searched.
  //
  // Implementation details: It is suitable to pass kNone for regular profile guided compilation
  // because during compilation we generally don't care about annotations. The metadata is
  // useful for boot profiles which need the extra information.
  MethodHotness GetMethodHotness(
      const MethodReference& method_ref,
      const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) const;

  // Return true if the class's type is present in the profiling info.
  //
  // Note: see GetMethodHotness docs for the handling of annotations.
  bool ContainsClass(
      const DexFile& dex_file,
      dex::TypeIndex type_idx,
      const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) const;

  // Return the dex file for the given `profile_index`, or null if none of the provided
  // dex files has a matching checksum and a location with the same base key.
  template <typename Container>
  const DexFile* FindDexFileForProfileIndex(ProfileIndexType profile_index,
                                            const Container& dex_files) const {
    static_assert(std::is_same_v<typename Container::value_type, const DexFile*> ||
                  std::is_same_v<typename Container::value_type, std::unique_ptr<const DexFile>>);
    DCHECK_LE(profile_index, info_.size());
    const DexFileData* dex_file_data = info_[profile_index].get();
    DCHECK(dex_file_data != nullptr);
    uint32_t dex_checksum = dex_file_data->checksum;
    std::string_view base_key = GetBaseKeyViewFromAugmentedKey(dex_file_data->profile_key);
    for (const auto& dex_file : dex_files) {
      if (dex_checksum == dex_file->GetLocationChecksum() &&
          base_key == GetProfileDexFileBaseKeyView(dex_file->GetLocation())) {
        return std::addressof(*dex_file);
      }
    }
    return nullptr;
  }

  DexReferenceDumper DumpDexReference(ProfileIndexType profile_index) const;

  // Dump all the loaded profile info into a string and returns it.
  // If dex_files is not empty then the method indices will be resolved to their
  // names.
  // This is intended for testing and debugging.
  std::string DumpInfo(const std::vector<const DexFile*>& dex_files,
                       bool print_full_dex_location = true) const;

  // Return the classes and methods for a given dex file through out args. The out args are the set
  // of class as well as the methods and their associated inline caches. Returns true if the dex
  // file is register and has a matching checksum, false otherwise.
  //
  // Note: see GetMethodHotness docs for the handling of annotations.
  bool GetClassesAndMethods(
      const DexFile& dex_file,
      /*out*/std::set<dex::TypeIndex>* class_set,
      /*out*/std::set<uint16_t>* hot_method_set,
      /*out*/std::set<uint16_t>* startup_method_set,
      /*out*/std::set<uint16_t>* post_startup_method_method_set,
      const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone) const;

  // Returns true iff both profiles have the same version.
  bool SameVersion(const ProfileCompilationInfo& other) const;

  // Perform an equality test with the `other` profile information.
  bool Equals(const ProfileCompilationInfo& other);

  // Return the base profile key associated with the given dex location. The base profile key
  // is solely constructed based on the dex location (as opposed to the one produced by
  // GetProfileDexFileAugmentedKey which may include additional metadata like the origin
  // package name)
  static std::string GetProfileDexFileBaseKey(const std::string& dex_location);

  // Returns a base key without the annotation information.
  static std::string GetBaseKeyFromAugmentedKey(const std::string& profile_key);

  // Returns the annotations from an augmented key.
  // If the key is a base key it return ProfileSampleAnnotation::kNone.
  static ProfileSampleAnnotation GetAnnotationFromKey(const std::string& augmented_key);

  // Generate a test profile which will contain a percentage of the total maximum
  // number of methods and classes (method_ratio and class_ratio).
  static bool GenerateTestProfile(int fd,
                                  uint16_t number_of_dex_files,
                                  uint16_t method_ratio,
                                  uint16_t class_ratio,
                                  uint32_t random_seed);

  // Generate a test profile which will randomly contain classes and methods from
  // the provided list of dex files.
  static bool GenerateTestProfile(int fd,
                                  std::vector<std::unique_ptr<const DexFile>>& dex_files,
                                  uint16_t method_percentage,
                                  uint16_t class_percentage,
                                  uint32_t random_seed);

  ArenaAllocator* GetAllocator() { return &allocator_; }

  // Return all of the class descriptors in the profile for a set of dex files.
  // Note: see GetMethodHotness docs for the handling of annotations..
  HashSet<std::string> GetClassDescriptors(
      const std::vector<const DexFile*>& dex_files,
      const ProfileSampleAnnotation& annotation = ProfileSampleAnnotation::kNone);

  // Return true if the fd points to a profile file.
  bool IsProfileFile(int fd);

  // Update the profile keys corresponding to the given dex files based on their current paths.
  // This method allows fix-ups in the profile for dex files that might have been renamed.
  // The new profile key will be constructed based on the current dex location.
  //
  // The matching [profile key <-> dex_file] is done based on the dex checksum and the number of
  // methods ids. If neither is a match then the profile key is not updated.
  //
  // If the new profile key would collide with an existing key (for a different dex)
  // the method returns false. Otherwise it returns true.
  bool UpdateProfileKeys(const std::vector<std::unique_ptr<const DexFile>>& dex_files);

  // Checks if the profile is empty.
  bool IsEmpty() const;

  // Clears all the data from the profile.
  void ClearData();

  // Clears all the data from the profile and adjust the object version.
  void ClearDataAndAdjustVersion(bool for_boot_image);

  // Prepare the profile to store aggregation counters.
  // This will change the profile version and allocate extra storage for the counters.
  // It allocates 2 bytes for every possible method and class, so do not use in performance
  // critical code which needs to be memory efficient.
  void PrepareForAggregationCounters();

  // Returns true if the profile is configured to store aggregation counters.
  bool IsForBootImage() const;

  // Get type descriptor for a valid type index, whether a normal type index
  // referencing a `dex::TypeId` in the dex file, or an artificial type index
  // referencing an "extra descriptor".
  const char* GetTypeDescriptor(const DexFile* dex_file, dex::TypeIndex type_index) const {
    DCHECK(type_index.IsValid());
    uint32_t num_type_ids = dex_file->NumTypeIds();
    if (type_index.index_ < num_type_ids) {
      return dex_file->StringByTypeIdx(type_index);
    } else {
      return extra_descriptors_[type_index.index_ - num_type_ids].c_str();
    }
  }

  // Return the version of this profile.
  const uint8_t* GetVersion() const;

  // Extracts the data that the profile has on the given dex files:
  //  - for each method and class, a list of the corresponding annotations and flags
  //  - the maximum number of aggregations for classes and classes across dex files with different
  //    annotations (essentially this sums up how many different packages used the corresponding
  //    method). This information is reconstructible from the other two pieces of info, but it's
  //    convenient to have it precomputed.
  std::unique_ptr<FlattenProfileData> ExtractProfileData(
      const std::vector<std::unique_ptr<const DexFile>>& dex_files) const;

 private:
  // Helper classes.
  class FileHeader;
  class FileSectionInfo;
  enum class FileSectionType : uint32_t;
  enum class ProfileLoadStatus : uint32_t;
  class ProfileSource;
  class SafeBuffer;

  // Extra descriptors are used to reference classes with `TypeIndex` between the dex
  // file's `NumTypeIds()` and the `DexFile::kDexNoIndex16`. The range of usable
  // extra descriptor indexes is therefore also limited by `DexFile::kDexNoIndex16`.
  using ExtraDescriptorIndex = uint16_t;
  static constexpr ExtraDescriptorIndex kMaxExtraDescriptors = DexFile::kDexNoIndex16;

  class ExtraDescriptorIndexEmpty {
   public:
    void MakeEmpty(ExtraDescriptorIndex& index) const {
      index = kMaxExtraDescriptors;
    }
    bool IsEmpty(const ExtraDescriptorIndex& index) const {
      return index == kMaxExtraDescriptors;
    }
  };

  class ExtraDescriptorHash {
   public:
    explicit ExtraDescriptorHash(const dchecked_vector<std::string>* extra_descriptors)
        : extra_descriptors_(extra_descriptors) {}

    size_t operator()(const ExtraDescriptorIndex& index) const {
      std::string_view str = (*extra_descriptors_)[index];
      return (*this)(str);
    }

    size_t operator()(std::string_view str) const {
      return DataHash()(str);
    }

   private:
    const dchecked_vector<std::string>* extra_descriptors_;
  };

  class ExtraDescriptorEquals {
   public:
    explicit ExtraDescriptorEquals(const dchecked_vector<std::string>* extra_descriptors)
        : extra_descriptors_(extra_descriptors) {}

    size_t operator()(const ExtraDescriptorIndex& lhs, const ExtraDescriptorIndex& rhs) const {
      DCHECK_EQ(lhs == rhs, (*this)(lhs, (*extra_descriptors_)[rhs]));
      return lhs == rhs;
    }

    size_t operator()(const ExtraDescriptorIndex& lhs, std::string_view rhs_str) const {
      std::string_view lhs_str = (*extra_descriptors_)[lhs];
      return lhs_str == rhs_str;
    }

   private:
    const dchecked_vector<std::string>* extra_descriptors_;
  };

  using ExtraDescriptorHashSet = HashSet<ExtraDescriptorIndex,
                                         ExtraDescriptorIndexEmpty,
                                         ExtraDescriptorHash,
                                         ExtraDescriptorEquals>;

  // Internal representation of the profile information belonging to a dex file.
  // Note that we could do without the profile_index (the index of the dex file
  // in the profile) field in this struct because we can infer it from
  // `profile_key_map_` and `info_`. However, it makes the profiles logic much
  // simpler if we have the profile index here as well.
  struct DexFileData : public DeletableArenaObject<kArenaAllocProfile> {
    DexFileData(ArenaAllocator* allocator,
                const std::string& key,
                uint32_t location_checksum,
                uint16_t index,
                uint32_t num_types,
                uint32_t num_methods,
                bool for_boot_image)
        : allocator_(allocator),
          profile_key(key),
          profile_index(index),
          checksum(location_checksum),
          method_map(std::less<uint16_t>(), allocator->Adapter(kArenaAllocProfile)),
          class_set(std::less<dex::TypeIndex>(), allocator->Adapter(kArenaAllocProfile)),
          num_type_ids(num_types),
          num_method_ids(num_methods),
          bitmap_storage(allocator->Adapter(kArenaAllocProfile)),
          is_for_boot_image(for_boot_image) {
      bitmap_storage.resize(ComputeBitmapStorage(is_for_boot_image, num_method_ids));
      if (!bitmap_storage.empty()) {
        method_bitmap =
            BitMemoryRegion(MemoryRegion(
                &bitmap_storage[0],
                bitmap_storage.size()),
                0,
                ComputeBitmapBits(is_for_boot_image, num_method_ids));
      }
    }

    static size_t ComputeBitmapBits(bool is_for_boot_image, uint32_t num_method_ids) {
      size_t flag_bitmap_index = FlagBitmapIndex(is_for_boot_image
          ? MethodHotness::kFlagLastBoot
          : MethodHotness::kFlagLastRegular);
      return num_method_ids * (flag_bitmap_index + 1);
    }
    static size_t ComputeBitmapStorage(bool is_for_boot_image, uint32_t num_method_ids) {
      return RoundUp(ComputeBitmapBits(is_for_boot_image, num_method_ids), kBitsPerByte) /
          kBitsPerByte;
    }

    bool operator==(const DexFileData& other) const {
      return checksum == other.checksum &&
          num_method_ids == other.num_method_ids &&
          method_map == other.method_map &&
          class_set == other.class_set &&
          (BitMemoryRegion::Compare(method_bitmap, other.method_bitmap) == 0);
    }

    // Mark a method as executed at least once.
    bool AddMethod(MethodHotness::Flag flags, size_t index);

    void MergeBitmap(const DexFileData& other) {
      DCHECK_EQ(bitmap_storage.size(), other.bitmap_storage.size());
      for (size_t i = 0; i < bitmap_storage.size(); ++i) {
        bitmap_storage[i] |= other.bitmap_storage[i];
      }
    }

    void SetMethodHotness(size_t index, MethodHotness::Flag flags);
    MethodHotness GetHotnessInfo(uint32_t dex_method_index) const;

    bool ContainsClass(dex::TypeIndex type_index) const;

    uint32_t ClassesDataSize() const;
    void WriteClasses(SafeBuffer& buffer) const;
    ProfileLoadStatus ReadClasses(
        SafeBuffer& buffer,
        const dchecked_vector<ExtraDescriptorIndex>& extra_descriptors_remap,
        std::string* error);
    static ProfileLoadStatus SkipClasses(SafeBuffer& buffer, std::string* error);

    uint32_t MethodsDataSize(/*out*/ uint16_t* method_flags = nullptr,
                             /*out*/ size_t* saved_bitmap_bit_size = nullptr) const;
    void WriteMethods(SafeBuffer& buffer) const;
    ProfileLoadStatus ReadMethods(
        SafeBuffer& buffer,
        const dchecked_vector<ExtraDescriptorIndex>& extra_descriptors_remap,
        std::string* error);
    static ProfileLoadStatus SkipMethods(SafeBuffer& buffer, std::string* error);

    // The allocator used to allocate new inline cache maps.
    ArenaAllocator* const allocator_;
    // The profile key this data belongs to.
    std::string profile_key;
    // The profile index of this dex file (matches ClassReference#dex_profile_index).
    ProfileIndexType profile_index;
    // The dex checksum.
    uint32_t checksum;
    // The methods' profile information.
    MethodMap method_map;
    // The classes which have been profiled. Note that these don't necessarily include
    // all the classes that can be found in the inline caches reference.
    ArenaSet<dex::TypeIndex> class_set;
    // Find the inline caches of the the given method index. Add an empty entry if
    // no previous data is found.
    InlineCacheMap* FindOrAddHotMethod(uint16_t method_index);
    // Num type ids.
    uint32_t num_type_ids;
    // Num method ids.
    uint32_t num_method_ids;
    ArenaVector<uint8_t> bitmap_storage;
    BitMemoryRegion method_bitmap;
    bool is_for_boot_image;

   private:
    template <typename Fn>
    void ForMethodBitmapHotnessFlags(Fn fn) const;

    static void WriteClassSet(SafeBuffer& buffer, const ArenaSet<dex::TypeIndex>& class_set);
    size_t MethodFlagBitmapIndex(MethodHotness::Flag flag, size_t method_index) const;
    static size_t FlagBitmapIndex(MethodHotness::Flag flag);

    uint16_t GetUsedBitmapFlags() const;
  };

  // Return the profile data for the given profile key or null if the dex location
  // already exists but has a different checksum
  DexFileData* GetOrAddDexFileData(const std::string& profile_key,
                                   uint32_t checksum,
                                   uint32_t num_type_ids,
                                   uint32_t num_method_ids);

  DexFileData* GetOrAddDexFileData(const DexFile* dex_file,
                                   const ProfileSampleAnnotation& annotation) {
    return GetOrAddDexFileData(GetProfileDexFileAugmentedKey(dex_file->GetLocation(), annotation),
                               dex_file->GetLocationChecksum(),
                               dex_file->NumTypeIds(),
                               dex_file->NumMethodIds());
  }

  // Return the dex data associated with the given profile key or null if the profile
  // doesn't contain the key.
  const DexFileData* FindDexData(const std::string& profile_key,
                                 uint32_t checksum,
                                 bool verify_checksum = true) const;
  // Same as FindDexData but performs the searching using the given annotation:
  //   - If the annotation is kNone then the search ignores it and only looks at the base keys.
  //     In this case only the first matching dex is searched.
  //   - If the annotation is not kNone, the augmented key is constructed and used to invoke
  //     the regular FindDexData.
  const DexFileData* FindDexDataUsingAnnotations(
      const DexFile* dex_file,
      const ProfileSampleAnnotation& annotation) const;

  // Same as FindDexDataUsingAnnotations but extracts the data for all annotations.
  void FindAllDexData(
      const DexFile* dex_file,
      /*out*/ std::vector<const ProfileCompilationInfo::DexFileData*>* result) const;

  // Add a new extra descriptor. Returns kMaxExtraDescriptors on failure.
  ExtraDescriptorIndex AddExtraDescriptor(std::string_view extra_descriptor);

  // Parsing functionality.

  ProfileLoadStatus OpenSource(int32_t fd,
                               /*out*/ std::unique_ptr<ProfileSource>* source,
                               /*out*/ std::string* error);

  ProfileLoadStatus ReadSectionData(ProfileSource& source,
                                    const FileSectionInfo& section_info,
                                    /*out*/ SafeBuffer* buffer,
                                    /*out*/ std::string* error);

  ProfileLoadStatus ReadDexFilesSection(
      ProfileSource& source,
      const FileSectionInfo& section_info,
      const ProfileLoadFilterFn& filter_fn,
      /*out*/ dchecked_vector<ProfileIndexType>* dex_profile_index_remap,
      /*out*/ std::string* error);

  ProfileLoadStatus ReadExtraDescriptorsSection(
      ProfileSource& source,
      const FileSectionInfo& section_info,
      /*out*/ dchecked_vector<ExtraDescriptorIndex>* extra_descriptors_remap,
      /*out*/ std::string* error);

  ProfileLoadStatus ReadClassesSection(
      ProfileSource& source,
      const FileSectionInfo& section_info,
      const dchecked_vector<ProfileIndexType>& dex_profile_index_remap,
      const dchecked_vector<ExtraDescriptorIndex>& extra_descriptors_remap,
      /*out*/ std::string* error);

  ProfileLoadStatus ReadMethodsSection(
      ProfileSource& source,
      const FileSectionInfo& section_info,
      const dchecked_vector<ProfileIndexType>& dex_profile_index_remap,
      const dchecked_vector<ExtraDescriptorIndex>& extra_descriptors_remap,
      /*out*/ std::string* error);

  // Entry point for profile loading functionality.
  ProfileLoadStatus LoadInternal(
      int32_t fd,
      std::string* error,
      bool merge_classes = true,
      const ProfileLoadFilterFn& filter_fn = ProfileFilterFnAcceptAll);

  // Find the data for the dex_pc in the inline cache. Adds an empty entry
  // if no previous data exists.
  static DexPcData* FindOrAddDexPc(InlineCacheMap* inline_cache, uint32_t dex_pc);

  // Initializes the profile version to the desired one.
  void InitProfileVersionInternal(const uint8_t version[]);

  // Returns the threshold size (in bytes) which will trigger save/load warnings.
  size_t GetSizeWarningThresholdBytes() const;
  // Returns the threshold size (in bytes) which will cause save/load failures.
  size_t GetSizeErrorThresholdBytes() const;

  // Implementation of `GetProfileDexFileBaseKey()` but returning a subview
  // referencing the same underlying data to avoid excessive heap allocations.
  static std::string_view GetProfileDexFileBaseKeyView(std::string_view dex_location);

  // Implementation of `GetBaseKeyFromAugmentedKey()` but returning a subview
  // referencing the same underlying data to avoid excessive heap allocations.
  static std::string_view GetBaseKeyViewFromAugmentedKey(std::string_view dex_location);

  // Returns the augmented profile key associated with the given dex location.
  // The return key will contain a serialized form of the information from the provided
  // annotation. If the annotation is ProfileSampleAnnotation::kNone then no extra info is
  // added to the key and this method is equivalent to GetProfileDexFileBaseKey.
  static std::string GetProfileDexFileAugmentedKey(const std::string& dex_location,
                                                   const ProfileSampleAnnotation& annotation);

  // Migrates the annotation from an augmented key to a base key.
  static std::string MigrateAnnotationInfo(const std::string& base_key,
                                           const std::string& augmented_key);

  // Returns the maximum value for the profile index.
  static constexpr ProfileIndexType MaxProfileIndex() {
    return std::numeric_limits<ProfileIndexType>::max();
  }

  friend class ProfileCompilationInfoTest;
  friend class CompilerDriverProfileTest;
  friend class ProfileAssistantTest;
  friend class Dex2oatLayoutTest;

  MallocArenaPool default_arena_pool_;
  ArenaAllocator allocator_;

  // Vector containing the actual profile info.
  // The vector index is the profile index of the dex data and
  // matched DexFileData::profile_index.
  ArenaVector<std::unique_ptr<DexFileData>> info_;

  // Cache mapping profile keys to profile index.
  // This is used to speed up searches since it avoids iterating
  // over the info_ vector when searching by profile key.
  // The backing storage for the `string_view` is the associated `DexFileData`.
  ArenaSafeMap<const std::string_view, ProfileIndexType> profile_key_map_;

  // Additional descriptors for referencing types not present in a dex files's `TypeId`s.
  dchecked_vector<std::string> extra_descriptors_;
  ExtraDescriptorHashSet extra_descriptors_indexes_;

  // The version of the profile.
  uint8_t version_[kProfileVersionSize];
};

/**
 * Flatten profile data that list all methods and type references together
 * with their metadata (such as flags or annotation list).
 */
class FlattenProfileData {
 public:
  class ItemMetadata {
   public:
    ItemMetadata();
    ItemMetadata(const ItemMetadata& other);

    uint16_t GetFlags() const {
      return flags_;
    }

    const std::list<ProfileCompilationInfo::ProfileSampleAnnotation>& GetAnnotations() const {
      return annotations_;
    }

    void AddFlag(ProfileCompilationInfo::MethodHotness::Flag flag) {
      flags_ |= flag;
    }

    bool HasFlagSet(ProfileCompilationInfo::MethodHotness::Flag flag) const {
      return (flags_ & flag) != 0;
    }

   private:
    // will be 0 for classes and MethodHotness::Flags for methods.
    uint16_t flags_;
    // This is a list that may contain duplicates after a merge operation.
    // It represents that a method was used multiple times across different devices.
    std::list<ProfileCompilationInfo::ProfileSampleAnnotation> annotations_;

    friend class ProfileCompilationInfo;
    friend class FlattenProfileData;
  };

  FlattenProfileData();

  const SafeMap<MethodReference, ItemMetadata>& GetMethodData() const {
    return method_metadata_;
  }

  const SafeMap<TypeReference, ItemMetadata>& GetClassData() const {
    return class_metadata_;
  }

  uint32_t GetMaxAggregationForMethods() const {
    return max_aggregation_for_methods_;
  }

  uint32_t GetMaxAggregationForClasses() const {
    return max_aggregation_for_classes_;
  }

  void MergeData(const FlattenProfileData& other);

 private:
  // Method data.
  SafeMap<MethodReference, ItemMetadata> method_metadata_;
  // Class data.
  SafeMap<TypeReference, ItemMetadata> class_metadata_;
  // Maximum aggregation counter for all methods.
  // This is essentially a cache equal to the max size of any method's annotation set.
  // It avoids the traversal of all the methods which can be quite expensive.
  uint32_t max_aggregation_for_methods_;
  // Maximum aggregation counter for all classes.
  // Simillar to max_aggregation_for_methods_.
  uint32_t max_aggregation_for_classes_;

  friend class ProfileCompilationInfo;
};

struct ProfileCompilationInfo::DexReferenceDumper {
  const std::string& GetProfileKey() {
    return dex_file_data->profile_key;
  }

  uint32_t GetDexChecksum() const {
    return dex_file_data->checksum;
  }

  uint32_t GetNumTypeIds() const {
    return dex_file_data->num_type_ids;
  }

  uint32_t GetNumMethodIds() const {
    return dex_file_data->num_method_ids;
  }

  const DexFileData* dex_file_data;
};

inline ProfileCompilationInfo::DexReferenceDumper ProfileCompilationInfo::DumpDexReference(
    ProfileIndexType profile_index) const {
  return DexReferenceDumper{info_[profile_index].get()};
}

std::ostream& operator<<(std::ostream& stream, ProfileCompilationInfo::DexReferenceDumper dumper);

}  // namespace art

#endif  // ART_LIBPROFILE_PROFILE_PROFILE_COMPILATION_INFO_H_
