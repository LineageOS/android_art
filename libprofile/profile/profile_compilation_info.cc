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

#include "profile_compilation_info.h"

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "android-base/file.h"

#include "base/arena_allocator.h"
#include "base/bit_utils.h"
#include "base/dumpable.h"
#include "base/file_utils.h"
#include "base/logging.h"  // For VLOG.
#include "base/malloc_arena_pool.h"
#include "base/os.h"
#include "base/safe_map.h"
#include "base/scoped_flock.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/time_utils.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "base/zip_archive.h"
#include "dex/descriptors_names.h"
#include "dex/dex_file_loader.h"

namespace art {

const uint8_t ProfileCompilationInfo::kProfileMagic[] = { 'p', 'r', 'o', '\0' };
// Last profile version: New extensible profile format.
const uint8_t ProfileCompilationInfo::kProfileVersion[] = { '0', '1', '3', '\0' };
const uint8_t ProfileCompilationInfo::kProfileVersionForBootImage[] = { '0', '1', '4', '\0' };

static_assert(sizeof(ProfileCompilationInfo::kProfileVersion) == 4,
              "Invalid profile version size");
static_assert(sizeof(ProfileCompilationInfo::kProfileVersionForBootImage) == 4,
              "Invalid profile version size");

// The name of the profile entry in the dex metadata file.
// DO NOT CHANGE THIS! (it's similar to classes.dex in the apk files).
const char ProfileCompilationInfo::kDexMetadataProfileEntry[] = "primary.prof";

// A synthetic annotations that can be used to denote that no annotation should
// be associated with the profile samples. We use the empty string for the package name
// because that's an invalid package name and should never occur in practice.
const ProfileCompilationInfo::ProfileSampleAnnotation
  ProfileCompilationInfo::ProfileSampleAnnotation::kNone =
      ProfileCompilationInfo::ProfileSampleAnnotation("");

static constexpr char kSampleMetadataSeparator = ':';

// Note: This used to be PATH_MAX (usually 4096) but that seems excessive
// and we do not want to rely on that external constant anyway.
static constexpr uint16_t kMaxDexFileKeyLength = 1024;

// According to dex file specification, there can be more than 2^16 valid method indexes
// but bytecode uses only 16 bits, so higher method indexes are not very useful (though
// such methods could be reached through virtual or interface dispatch). Consequently,
// dex files with more than 2^16 method indexes are not really used and the profile file
// format does not support higher method indexes.
static constexpr uint32_t kMaxSupportedMethodIndex = 0xffffu;

// Debug flag to ignore checksums when testing if a method or a class is present in the profile.
// Used to facilitate testing profile guided compilation across a large number of apps
// using the same test profile.
static constexpr bool kDebugIgnoreChecksum = false;

static constexpr uint8_t kIsMissingTypesEncoding = 6;
static constexpr uint8_t kIsMegamorphicEncoding = 7;

static_assert(sizeof(ProfileCompilationInfo::kIndividualInlineCacheSize) == sizeof(uint8_t),
              "InlineCache::kIndividualInlineCacheSize does not have the expect type size");
static_assert(ProfileCompilationInfo::kIndividualInlineCacheSize < kIsMegamorphicEncoding,
              "InlineCache::kIndividualInlineCacheSize is larger than expected");
static_assert(ProfileCompilationInfo::kIndividualInlineCacheSize < kIsMissingTypesEncoding,
              "InlineCache::kIndividualInlineCacheSize is larger than expected");

static constexpr uint32_t kSizeWarningThresholdBytes = 500000U;
static constexpr uint32_t kSizeErrorThresholdBytes = 1500000U;

static constexpr uint32_t kSizeWarningThresholdBootBytes = 25000000U;
static constexpr uint32_t kSizeErrorThresholdBootBytes = 100000000U;

static bool ChecksumMatch(uint32_t dex_file_checksum, uint32_t checksum) {
  return kDebugIgnoreChecksum || dex_file_checksum == checksum;
}

namespace {

// Deflate the input buffer `in_buffer`. It returns a buffer of
// compressed data for the input buffer of `*compressed_data_size` size.
std::unique_ptr<uint8_t[]> DeflateBuffer(ArrayRef<const uint8_t> in_buffer,
                                         /*out*/ uint32_t* compressed_data_size) {
  z_stream strm;
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  int init_ret = deflateInit(&strm, 1);
  if (init_ret != Z_OK) {
    return nullptr;
  }

  uint32_t out_size = dchecked_integral_cast<uint32_t>(deflateBound(&strm, in_buffer.size()));

  std::unique_ptr<uint8_t[]> compressed_buffer(new uint8_t[out_size]);
  strm.avail_in = in_buffer.size();
  strm.next_in = const_cast<uint8_t*>(in_buffer.data());
  strm.avail_out = out_size;
  strm.next_out = &compressed_buffer[0];
  int ret = deflate(&strm, Z_FINISH);
  if (ret == Z_STREAM_ERROR) {
    return nullptr;
  }
  *compressed_data_size = out_size - strm.avail_out;

  int end_ret = deflateEnd(&strm);
  if (end_ret != Z_OK) {
    return nullptr;
  }

  return compressed_buffer;
}

// Inflate the data from `in_buffer` into `out_buffer`. The `out_buffer.size()`
// is the expected output size of the buffer. It returns Z_STREAM_END on success.
// On error, it returns Z_STREAM_ERROR if the compressed data is inconsistent
// and Z_DATA_ERROR if the stream ended prematurely or the stream has extra data.
int InflateBuffer(ArrayRef<const uint8_t> in_buffer, /*out*/ ArrayRef<uint8_t> out_buffer) {
  /* allocate inflate state */
  z_stream strm;
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = in_buffer.size();
  strm.next_in = const_cast<uint8_t*>(in_buffer.data());
  strm.avail_out = out_buffer.size();
  strm.next_out = out_buffer.data();

  int init_ret = inflateInit(&strm);
  if (init_ret != Z_OK) {
    return init_ret;
  }

  int ret = inflate(&strm, Z_NO_FLUSH);
  if (strm.avail_in != 0 || strm.avail_out != 0) {
    return Z_DATA_ERROR;
  }

  int end_ret = inflateEnd(&strm);
  if (end_ret != Z_OK) {
    return end_ret;
  }

  return ret;
}

}  // anonymous namespace

enum class ProfileCompilationInfo::ProfileLoadStatus : uint32_t {
  kSuccess,
  kIOError,
  kBadMagic,
  kVersionMismatch,
  kBadData,
  kMergeError,  // Merging failed. There are too many extra descriptors
                // or classes without TypeId referenced by a dex file.
};

enum class ProfileCompilationInfo::FileSectionType : uint32_t {
  // The values of section enumerators and data format for individual sections
  // must not be changed without changing the profile file version. New sections
  // can be added at the end and they shall be ignored by old versions of ART.

  // The list of the dex files included in the profile.
  // There must be exactly one dex file section and it must be first.
  kDexFiles = 0,

  // Extra descriptors for referencing classes that do not have a `dex::TypeId`
  // in the referencing dex file, such as classes from a different dex file
  // (even outside of the dex files in the profile) or array classes that were
  // used from other dex files or created through reflection.
  kExtraDescriptors = 1,

  // Classes included in the profile.
  kClasses = 2,

  // Methods included in the profile, their hotness flags and inline caches.
  kMethods = 3,

  // The number of known sections.
  kNumberOfSections = 4
};

class ProfileCompilationInfo::FileSectionInfo {
 public:
  // Constructor for reading from a `ProfileSource`. Data shall be filled from the source.
  FileSectionInfo() {}

  // Constructor for writing to a file.
  FileSectionInfo(FileSectionType type,
                  uint32_t file_offset,
                  uint32_t file_size,
                  uint32_t inflated_size)
      : type_(type),
        file_offset_(file_offset),
        file_size_(file_size),
        inflated_size_(inflated_size) {}

  void SetFileOffset(uint32_t file_offset) {
    DCHECK_EQ(file_offset_, 0u);
    DCHECK_NE(file_offset, 0u);
    file_offset_ = file_offset;
  }

  FileSectionType GetType() const {
    return type_;
  }

  uint32_t GetFileOffset() const {
    return file_offset_;
  }

  uint32_t GetFileSize() const {
    return file_size_;
  }

  uint32_t GetInflatedSize() const {
    return inflated_size_;
  }

  uint32_t GetMemSize() const {
    return inflated_size_ != 0u ? inflated_size_ : file_size_;
  }

 private:
  FileSectionType type_;
  uint32_t file_offset_;
  uint32_t file_size_;
  uint32_t inflated_size_;  // If 0, do not inflate and use data from file directly.
};

// The file header.
class ProfileCompilationInfo::FileHeader {
 public:
  // Constructor for reading from a `ProfileSource`. Data shall be filled from the source.
  FileHeader() {
    DCHECK(!IsValid());
  }

  // Constructor for writing to a file.
  FileHeader(const uint8_t* version, uint32_t file_section_count)
      : file_section_count_(file_section_count) {
    static_assert(sizeof(magic_) == sizeof(kProfileMagic));
    static_assert(sizeof(version_) == sizeof(kProfileVersion));
    static_assert(sizeof(version_) == sizeof(kProfileVersionForBootImage));
    memcpy(magic_, kProfileMagic, sizeof(kProfileMagic));
    memcpy(version_, version, sizeof(version_));
    DCHECK_LE(file_section_count, kMaxFileSectionCount);
    DCHECK(IsValid());
  }

  bool IsValid() const {
    return memcmp(magic_, kProfileMagic, sizeof(kProfileMagic)) == 0 &&
           (memcmp(version_, kProfileVersion, kProfileVersionSize) == 0 ||
            memcmp(version_, kProfileVersionForBootImage, kProfileVersionSize) == 0) &&
           file_section_count_ != 0u &&  // The dex files section is mandatory.
           file_section_count_ <= kMaxFileSectionCount;
  }

  const uint8_t* GetVersion() const {
    DCHECK(IsValid());
    return version_;
  }

  ProfileLoadStatus InvalidHeaderMessage(/*out*/ std::string* error_msg) const;

  uint32_t GetFileSectionCount() const {
    DCHECK(IsValid());
    return file_section_count_;
  }

 private:
  // The upper bound for file section count is used to ensure that there
  // shall be no arithmetic overflow when calculating size of the header
  // with section information.
  static const uint32_t kMaxFileSectionCount;

  uint8_t magic_[4] = {0, 0, 0, 0};
  uint8_t version_[4] = {0, 0, 0, 0};
  uint32_t file_section_count_ = 0u;
};

const uint32_t ProfileCompilationInfo::FileHeader::kMaxFileSectionCount =
    (std::numeric_limits<uint32_t>::max() - sizeof(FileHeader)) / sizeof(FileSectionInfo);

ProfileCompilationInfo::ProfileLoadStatus
ProfileCompilationInfo::FileHeader::InvalidHeaderMessage(/*out*/ std::string* error_msg) const {
  if (memcmp(magic_, kProfileMagic, sizeof(kProfileMagic)) != 0) {
    *error_msg = "Profile missing magic.";
    return ProfileLoadStatus::kBadMagic;
  }
  if (memcmp(version_, kProfileVersion, sizeof(kProfileVersion)) != 0 &&
      memcmp(version_, kProfileVersion, sizeof(kProfileVersionForBootImage)) != 0) {
    *error_msg = "Profile version mismatch.";
    return ProfileLoadStatus::kVersionMismatch;
  }
  if (file_section_count_ == 0u) {
    *error_msg = "Missing mandatory dex files section.";
    return ProfileLoadStatus::kBadData;
  }
  DCHECK_GT(file_section_count_, kMaxFileSectionCount);
  *error_msg ="Too many sections.";
  return ProfileLoadStatus::kBadData;
}

/**
 * Encapsulate the source of profile data for loading.
 * The source can be either a plain file or a zip file.
 * For zip files, the profile entry will be extracted to
 * the memory map.
 */
class ProfileCompilationInfo::ProfileSource {
 public:
  /**
   * Create a profile source for the given fd. The ownership of the fd
   * remains to the caller; as this class will not attempt to close it at any
   * point.
   */
  static ProfileSource* Create(int32_t fd) {
    DCHECK_GT(fd, -1);
    return new ProfileSource(fd, MemMap::Invalid());
  }

  /**
   * Create a profile source backed by a memory map. The map can be null in
   * which case it will the treated as an empty source.
   */
  static ProfileSource* Create(MemMap&& mem_map) {
    return new ProfileSource(/*fd*/ -1, std::move(mem_map));
  }

  // Seek to the given offset in the source.
  bool Seek(off_t offset);

  /**
   * Read bytes from this source.
   * Reading will advance the current source position so subsequent
   * invocations will read from the las position.
   */
  ProfileLoadStatus Read(void* buffer,
                         size_t byte_count,
                         const std::string& debug_stage,
                         std::string* error);

  /** Return true if the source has 0 data. */
  bool HasEmptyContent() const;

 private:
  ProfileSource(int32_t fd, MemMap&& mem_map)
      : fd_(fd), mem_map_(std::move(mem_map)), mem_map_cur_(0) {}

  bool IsMemMap() const {
    return fd_ == -1;
  }

  int32_t fd_;  // The fd is not owned by this class.
  MemMap mem_map_;
  size_t mem_map_cur_;  // Current position in the map to read from.
};

// A helper structure to make sure we don't read past our buffers in the loops.
// Also used for writing but the buffer should be pre-sized correctly for that, so we
// DCHECK() we do not write beyond the end, rather than returning `false` on failure.
class ProfileCompilationInfo::SafeBuffer {
 public:
  SafeBuffer()
      : storage_(nullptr),
        ptr_current_(nullptr),
        ptr_end_(nullptr) {}

  explicit SafeBuffer(size_t size)
      : storage_(new uint8_t[size]),
        ptr_current_(storage_.get()),
        ptr_end_(ptr_current_ + size) {}

  // Reads an uint value and advances the current pointer.
  template <typename T>
  bool ReadUintAndAdvance(/*out*/ T* value) {
    static_assert(std::is_unsigned<T>::value, "Type is not unsigned");
    if (sizeof(T) > GetAvailableBytes()) {
      return false;
    }
    *value = 0;
    for (size_t i = 0; i < sizeof(T); i++) {
      *value += ptr_current_[i] << (i * kBitsPerByte);
    }
    ptr_current_ += sizeof(T);
    return true;
  }

  // Reads a null-terminated string as `std::string_view` and advances the current pointer.
  bool ReadStringAndAdvance(/*out*/ std::string_view* value) {
    const void* null_char = memchr(GetCurrentPtr(), 0, GetAvailableBytes());
    if (null_char == nullptr) {
      return false;
    }
    size_t length = reinterpret_cast<const uint8_t*>(null_char) - GetCurrentPtr();
    *value = std::string_view(reinterpret_cast<const char*>(GetCurrentPtr()), length);
    Advance(length + 1u);
    return true;
  }

  // Compares the given data with the content at the current pointer.
  // If the contents are equal it advances the current pointer by data_size.
  bool CompareAndAdvance(const uint8_t* data, size_t data_size) {
    if (data_size > GetAvailableBytes()) {
      return false;
    }
    if (memcmp(ptr_current_, data, data_size) == 0) {
      ptr_current_ += data_size;
      return true;
    }
    return false;
  }

  void WriteAndAdvance(const void* data, size_t data_size) {
    DCHECK_LE(data_size, GetAvailableBytes());
    memcpy(ptr_current_, data, data_size);
    ptr_current_ += data_size;
  }

  template <typename T>
  void WriteUintAndAdvance(T value) {
    static_assert(std::is_integral_v<T>);
    WriteAndAdvance(&value, sizeof(value));
  }

  // Deflate a filled buffer. Replaces the internal buffer with a new one, also filled.
  bool Deflate() {
    DCHECK_EQ(GetAvailableBytes(), 0u);
    DCHECK_NE(Size(), 0u);
    ArrayRef<const uint8_t> in_buffer(Get(), Size());
    uint32_t output_size = 0;
    std::unique_ptr<uint8_t[]> compressed_buffer = DeflateBuffer(in_buffer, &output_size);
    if (compressed_buffer == nullptr) {
      return false;
    }
    storage_ = std::move(compressed_buffer);
    ptr_current_ = storage_.get() + output_size;
    ptr_end_ = ptr_current_;
    return true;
  }

  // Inflate an unread buffer. Replaces the internal buffer with a new one, also unread.
  bool Inflate(size_t uncompressed_data_size) {
    DCHECK(ptr_current_ == storage_.get());
    DCHECK_NE(Size(), 0u);
    ArrayRef<const uint8_t> in_buffer(Get(), Size());
    SafeBuffer uncompressed_buffer(uncompressed_data_size);
    ArrayRef<uint8_t> out_buffer(uncompressed_buffer.Get(), uncompressed_data_size);
    int ret = InflateBuffer(in_buffer, out_buffer);
    if (ret != Z_STREAM_END) {
      return false;
    }
    Swap(uncompressed_buffer);
    DCHECK(ptr_current_ == storage_.get());
    return true;
  }

  // Advances current pointer by data_size.
  void Advance(size_t data_size) {
    DCHECK_LE(data_size, GetAvailableBytes());
    ptr_current_ += data_size;
  }

  // Returns the count of unread bytes.
  size_t GetAvailableBytes() const {
    DCHECK_LE(static_cast<void*>(ptr_current_), static_cast<void*>(ptr_end_));
    return (ptr_end_ - ptr_current_) * sizeof(*ptr_current_);
  }

  // Returns the current pointer.
  uint8_t* GetCurrentPtr() {
    return ptr_current_;
  }

  // Get the underlying raw buffer.
  uint8_t* Get() {
    return storage_.get();
  }

  // Get the size of the raw buffer.
  size_t Size() const {
    return ptr_end_ - storage_.get();
  }

  void Swap(SafeBuffer& other) {
    std::swap(storage_, other.storage_);
    std::swap(ptr_current_, other.ptr_current_);
    std::swap(ptr_end_, other.ptr_end_);
  }

 private:
  std::unique_ptr<uint8_t[]> storage_;
  uint8_t* ptr_current_;
  uint8_t* ptr_end_;
};

ProfileCompilationInfo::ProfileCompilationInfo(ArenaPool* custom_arena_pool, bool for_boot_image)
    : default_arena_pool_(),
      allocator_(custom_arena_pool),
      info_(allocator_.Adapter(kArenaAllocProfile)),
      profile_key_map_(std::less<const std::string_view>(), allocator_.Adapter(kArenaAllocProfile)),
      extra_descriptors_(),
      extra_descriptors_indexes_(ExtraDescriptorHash(&extra_descriptors_),
                                 ExtraDescriptorEquals(&extra_descriptors_)) {
  memcpy(version_,
         for_boot_image ? kProfileVersionForBootImage : kProfileVersion,
         kProfileVersionSize);
}

ProfileCompilationInfo::ProfileCompilationInfo(ArenaPool* custom_arena_pool)
    : ProfileCompilationInfo(custom_arena_pool, /*for_boot_image=*/ false) { }

ProfileCompilationInfo::ProfileCompilationInfo()
    : ProfileCompilationInfo(/*for_boot_image=*/ false) { }

ProfileCompilationInfo::ProfileCompilationInfo(bool for_boot_image)
    : ProfileCompilationInfo(&default_arena_pool_, for_boot_image) { }

ProfileCompilationInfo::~ProfileCompilationInfo() {
  VLOG(profiler) << Dumpable<MemStats>(allocator_.GetMemStats());
}

void ProfileCompilationInfo::DexPcData::AddClass(const dex::TypeIndex& type_idx) {
  if (is_megamorphic || is_missing_types) {
    return;
  }

  // Perform an explicit lookup for the type instead of directly emplacing the
  // element. We do this because emplace() allocates the node before doing the
  // lookup and if it then finds an identical element, it shall deallocate the
  // node. For Arena allocations, that's essentially a leak.
  auto lb = classes.lower_bound(type_idx);
  if (lb != classes.end() && *lb == type_idx) {
    // The type index exists.
    return;
  }

  // Check if the adding the type will cause the cache to become megamorphic.
  if (classes.size() + 1 >= ProfileCompilationInfo::kIndividualInlineCacheSize) {
    is_megamorphic = true;
    classes.clear();
    return;
  }

  // The type does not exist and the inline cache will not be megamorphic.
  classes.emplace_hint(lb, type_idx);
}

// Transform the actual dex location into a key used to index the dex file in the profile.
// See ProfileCompilationInfo#GetProfileDexFileBaseKey as well.
std::string ProfileCompilationInfo::GetProfileDexFileAugmentedKey(
      const std::string& dex_location,
      const ProfileSampleAnnotation& annotation) {
  std::string base_key = GetProfileDexFileBaseKey(dex_location);
  return annotation == ProfileSampleAnnotation::kNone
      ? base_key
      : base_key + kSampleMetadataSeparator + annotation.GetOriginPackageName();;
}

// Transform the actual dex location into a base profile key (represented as relative paths).
// Note: this is OK because we don't store profiles of different apps into the same file.
// Apps with split apks don't cause trouble because each split has a different name and will not
// collide with other entries.
std::string_view ProfileCompilationInfo::GetProfileDexFileBaseKeyView(
    std::string_view dex_location) {
  DCHECK(!dex_location.empty());
  size_t last_sep_index = dex_location.find_last_of('/');
  if (last_sep_index == std::string::npos) {
    return dex_location;
  } else {
    DCHECK(last_sep_index < dex_location.size());
    return dex_location.substr(last_sep_index + 1);
  }
}

std::string ProfileCompilationInfo::GetProfileDexFileBaseKey(const std::string& dex_location) {
  // Note: Conversions between std::string and std::string_view.
  return std::string(GetProfileDexFileBaseKeyView(dex_location));
}

std::string_view ProfileCompilationInfo::GetBaseKeyViewFromAugmentedKey(
    std::string_view profile_key) {
  size_t pos = profile_key.rfind(kSampleMetadataSeparator);
  return (pos == std::string::npos) ? profile_key : profile_key.substr(0, pos);
}

std::string ProfileCompilationInfo::GetBaseKeyFromAugmentedKey(
    const std::string& profile_key) {
  // Note: Conversions between std::string and std::string_view.
  return std::string(GetBaseKeyViewFromAugmentedKey(profile_key));
}

std::string ProfileCompilationInfo::MigrateAnnotationInfo(
    const std::string& base_key,
    const std::string& augmented_key) {
  size_t pos = augmented_key.rfind(kSampleMetadataSeparator);
  return (pos == std::string::npos)
      ? base_key
      : base_key + augmented_key.substr(pos);
}

ProfileCompilationInfo::ProfileSampleAnnotation ProfileCompilationInfo::GetAnnotationFromKey(
     const std::string& augmented_key) {
  size_t pos = augmented_key.rfind(kSampleMetadataSeparator);
  return (pos == std::string::npos)
      ? ProfileSampleAnnotation::kNone
      : ProfileSampleAnnotation(augmented_key.substr(pos + 1));
}

bool ProfileCompilationInfo::AddMethods(const std::vector<ProfileMethodInfo>& methods,
                                        MethodHotness::Flag flags,
                                        const ProfileSampleAnnotation& annotation) {
  for (const ProfileMethodInfo& method : methods) {
    if (!AddMethod(method, flags, annotation)) {
      return false;
    }
  }
  return true;
}

dex::TypeIndex ProfileCompilationInfo::FindOrCreateTypeIndex(const DexFile& dex_file,
                                                             TypeReference class_ref) {
  DCHECK(class_ref.dex_file != nullptr);
  DCHECK_LT(class_ref.TypeIndex().index_, class_ref.dex_file->NumTypeIds());
  if (class_ref.dex_file == &dex_file) {
    // We can use the type index from the `class_ref` as it's a valid index in the `dex_file`.
    return class_ref.TypeIndex();
  }
  // Try to find a `TypeId` in the method's dex file.
  const char* descriptor = class_ref.dex_file->StringByTypeIdx(class_ref.TypeIndex());
  return FindOrCreateTypeIndex(dex_file, descriptor);
}

dex::TypeIndex ProfileCompilationInfo::FindOrCreateTypeIndex(const DexFile& dex_file,
                                                             const char* descriptor) {
  const dex::TypeId* type_id = dex_file.FindTypeId(descriptor);
  if (type_id != nullptr) {
    return dex_file.GetIndexForTypeId(*type_id);
  }
  // Try to find an existing extra descriptor.
  uint32_t num_type_ids = dex_file.NumTypeIds();
  uint32_t max_artificial_ids = DexFile::kDexNoIndex16 - num_type_ids;
  std::string_view descriptor_view(descriptor);
  auto it = extra_descriptors_indexes_.find(descriptor_view);
  if (it != extra_descriptors_indexes_.end()) {
    return (*it < max_artificial_ids) ? dex::TypeIndex(num_type_ids + *it) : dex::TypeIndex();
  }
  // Check if inserting the extra descriptor yields a valid artificial type index.
  if (UNLIKELY(extra_descriptors_.size() >= max_artificial_ids)) {
    return dex::TypeIndex();  // Invalid.
  }
  // Add the descriptor to extra descriptors and return the artificial type index.
  ExtraDescriptorIndex new_extra_descriptor_index = AddExtraDescriptor(descriptor_view);
  DCHECK_LT(new_extra_descriptor_index, max_artificial_ids);
  return dex::TypeIndex(num_type_ids + new_extra_descriptor_index);
}

bool ProfileCompilationInfo::AddClass(const DexFile& dex_file,
                                      dex::TypeIndex type_index,
                                      const ProfileSampleAnnotation& annotation) {
  DCHECK(type_index.IsValid());
  DCHECK(type_index.index_ <= dex_file.NumTypeIds() ||
         type_index.index_ - dex_file.NumTypeIds() < extra_descriptors_.size());
  DexFileData* const data = GetOrAddDexFileData(&dex_file, annotation);
  if (data == nullptr) {  // checksum mismatch
    return false;
  }
  data->class_set.insert(type_index);
  return true;
}

bool ProfileCompilationInfo::AddClass(const DexFile& dex_file,
                                      const char* descriptor,
                                      const ProfileSampleAnnotation& annotation) {
  DexFileData* const data = GetOrAddDexFileData(&dex_file, annotation);
  if (data == nullptr) {  // checksum mismatch
    return false;
  }
  dex::TypeIndex type_index = FindOrCreateTypeIndex(dex_file, descriptor);
  if (!type_index.IsValid()) {
    return false;
  }
  data->class_set.insert(type_index);
  return true;
}

bool ProfileCompilationInfo::MergeWith(const std::string& filename) {
  std::string error;
#ifdef _WIN32
  int flags = O_RDONLY;
#else
  int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC;
#endif
  ScopedFlock profile_file =
      LockedFile::Open(filename.c_str(), flags, /*block=*/false, &error);

  if (profile_file.get() == nullptr) {
    LOG(WARNING) << "Couldn't lock the profile file " << filename << ": " << error;
    return false;
  }

  int fd = profile_file->Fd();

  ProfileLoadStatus status = LoadInternal(fd, &error);
  if (status == ProfileLoadStatus::kSuccess) {
    return true;
  }

  LOG(WARNING) << "Could not load profile data from file " << filename << ": " << error;
  return false;
}

bool ProfileCompilationInfo::Load(const std::string& filename, bool clear_if_invalid) {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  std::string error;

  if (!IsEmpty()) {
    return false;
  }

#ifdef _WIN32
  int flags = O_RDWR;
#else
  int flags = O_RDWR | O_NOFOLLOW | O_CLOEXEC;
#endif
  // There's no need to fsync profile data right away. We get many chances
  // to write it again in case something goes wrong. We can rely on a simple
  // close(), no sync, and let to the kernel decide when to write to disk.
  ScopedFlock profile_file =
      LockedFile::Open(filename.c_str(), flags, /*block=*/false, &error);

  if (profile_file.get() == nullptr) {
    LOG(WARNING) << "Couldn't lock the profile file " << filename << ": " << error;
    return false;
  }

  int fd = profile_file->Fd();

  ProfileLoadStatus status = LoadInternal(fd, &error);
  if (status == ProfileLoadStatus::kSuccess) {
    return true;
  }

  if (clear_if_invalid &&
      ((status == ProfileLoadStatus::kBadMagic) ||
       (status == ProfileLoadStatus::kVersionMismatch) ||
       (status == ProfileLoadStatus::kBadData))) {
    LOG(WARNING) << "Clearing bad or obsolete profile data from file "
                 << filename << ": " << error;
    if (profile_file->ClearContent()) {
      return true;
    } else {
      PLOG(WARNING) << "Could not clear profile file: " << filename;
      return false;
    }
  }

  LOG(WARNING) << "Could not load profile data from file " << filename << ": " << error;
  return false;
}

bool ProfileCompilationInfo::Save(const std::string& filename, uint64_t* bytes_written) {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  std::string error;
#ifdef _WIN32
  int flags = O_WRONLY;
#else
  int flags = O_WRONLY | O_NOFOLLOW | O_CLOEXEC;
#endif
  // There's no need to fsync profile data right away. We get many chances
  // to write it again in case something goes wrong. We can rely on a simple
  // close(), no sync, and let to the kernel decide when to write to disk.
  ScopedFlock profile_file =
      LockedFile::Open(filename.c_str(), flags, /*block=*/false, &error);
  if (profile_file.get() == nullptr) {
    LOG(WARNING) << "Couldn't lock the profile file " << filename << ": " << error;
    return false;
  }

  int fd = profile_file->Fd();

  // We need to clear the data because we don't support appending to the profiles yet.
  if (!profile_file->ClearContent()) {
    PLOG(WARNING) << "Could not clear profile file: " << filename;
    return false;
  }

  // This doesn't need locking because we are trying to lock the file for exclusive
  // access and fail immediately if we can't.
  bool result = Save(fd);
  if (result) {
    int64_t size = OS::GetFileSizeBytes(filename.c_str());
    if (size != -1) {
      VLOG(profiler)
        << "Successfully saved profile info to " << filename << " Size: "
        << size;
      if (bytes_written != nullptr) {
        *bytes_written = static_cast<uint64_t>(size);
      }
    }
  } else {
    VLOG(profiler) << "Failed to save profile info to " << filename;
  }
  return result;
}

// Returns true if all the bytes were successfully written to the file descriptor.
static bool WriteBuffer(int fd, const void* buffer, size_t byte_count) {
  while (byte_count > 0) {
    int bytes_written = TEMP_FAILURE_RETRY(write(fd, buffer, byte_count));
    if (bytes_written == -1) {
      return false;
    }
    byte_count -= bytes_written;  // Reduce the number of remaining bytes.
    reinterpret_cast<const uint8_t*&>(buffer) += bytes_written;  // Move the buffer forward.
  }
  return true;
}

/**
 * Serialization format:
 *
 * The file starts with a header and section information:
 *   FileHeader
 *   FileSectionInfo[]
 * The first FileSectionInfo must be for the DexFiles section.
 *
 * The rest of the file is allowed to contain different sections in any order,
 * at arbitrary offsets, with any gaps betweeen them and each section can be
 * either plaintext or separately zipped. However, we're writing sections
 * without any gaps with the following order and compression:
 *   DexFiles - mandatory, plaintext
 *   ExtraDescriptors - optional, zipped
 *   Classes - optional, zipped
 *   Methods - optional, zipped
 *
 * DexFiles:
 *    number_of_dex_files
 *    (checksum,num_type_ids,num_method_ids,profile_key)[number_of_dex_files]
 * where `profile_key` is a NULL-terminated string.
 *
 * ExtraDescriptors:
 *    number_of_extra_descriptors
 *    (extra_descriptor)[number_of_extra_descriptors]
 * where `extra_descriptor` is a NULL-terminated string.
 *
 * Classes contains records for any number of dex files, each consisting of:
 *    profile_index
 *    number_of_classes
 *    type_index_diff[number_of_classes]
 * where instead of storing plain sorted type indexes, we store their differences
 * as smaller numbers are likely to compress better.
 *
 * Methods contains records for any number of dex files, each consisting of:
 *    profile_index
 *    following_data_size  // For easy skipping of remaining data when dex file is filtered out.
 *    method_flags
 *    bitmap_data
 *    method_encoding[]  // Until the size indicated by `following_data_size`.
 * where `bitmap_data` contains `num_method_ids` bits for each bit set in `method_flags` other
 * than "hot" (the size of `bitmap_data` is rounded up to whole bytes) and `method_encoding[]`
 * contains data for hot methods. The `method_encoding` is:
 *    method_index_diff
 *    number_of_inline_caches
 *    inline_cache_encoding[number_of_inline_caches]
 * where differences in method indexes are used for better compression,
 * and the `inline_cache_encoding` is
 *    dex_pc
 *    (M|dex_map_size)
 *    type_index_diff[dex_map_size]
 * where `M` stands for special encodings indicating missing types (kIsMissingTypesEncoding)
 * or memamorphic call (kIsMegamorphicEncoding) which both imply `dex_map_size == 0`.
 **/
bool ProfileCompilationInfo::Save(int fd) {
  uint64_t start = NanoTime();
  ScopedTrace trace(__PRETTY_FUNCTION__);
  DCHECK_GE(fd, 0);

  // Collect uncompressed section sizes.
  // Use `uint64_t` and assume this cannot overflow as we would have run out of memory.
  uint64_t extra_descriptors_section_size = 0u;
  if (!extra_descriptors_.empty()) {
    extra_descriptors_section_size += sizeof(uint16_t);  // Number of descriptors.
    for (const std::string& descriptor : extra_descriptors_) {
      extra_descriptors_section_size += descriptor.size() + 1u;  // Null-terminated string.
    }
  }
  uint64_t dex_files_section_size = sizeof(ProfileIndexType);  // Number of dex files.
  uint64_t classes_section_size = 0u;
  uint64_t methods_section_size = 0u;
  DCHECK_LE(info_.size(), MaxProfileIndex());
  for (const std::unique_ptr<DexFileData>& dex_data : info_) {
    if (dex_data->profile_key.size() > kMaxDexFileKeyLength) {
      LOG(WARNING) << "DexFileKey exceeds allocated limit";
      return false;
    }
    dex_files_section_size +=
        3 * sizeof(uint32_t) +  // Checksum, num_type_ids, num_method_ids.
        dex_data->profile_key.size() + 1u;  // Null-terminated key.
    classes_section_size += dex_data->ClassesDataSize();
    methods_section_size += dex_data->MethodsDataSize();
  }

  const uint32_t file_section_count =
      /* dex files */ 1u +
      /* extra descriptors */ (extra_descriptors_section_size != 0u ? 1u : 0u) +
      /* classes */ (classes_section_size != 0u ? 1u : 0u) +
      /* methods */ (methods_section_size != 0u ? 1u : 0u);
  uint64_t header_and_infos_size =
      sizeof(FileHeader) + file_section_count * sizeof(FileSectionInfo);

  // Check size limit. Allow large profiles for non target builds for the case
  // where we are merging many profiles to generate a boot image profile.
  uint64_t total_uncompressed_size =
      header_and_infos_size +
      dex_files_section_size +
      extra_descriptors_section_size +
      classes_section_size +
      methods_section_size;
  VLOG(profiler) << "Required capacity: " << total_uncompressed_size << " bytes.";
  if (total_uncompressed_size > GetSizeErrorThresholdBytes()) {
    LOG(ERROR) << "Profile data size exceeds "
               << GetSizeErrorThresholdBytes()
               << " bytes. Profile will not be written to disk."
               << " It requires " << total_uncompressed_size << " bytes.";
    return false;
  }

  // Start with an invalid file header and section infos.
  DCHECK_EQ(lseek(fd, 0, SEEK_CUR), 0);
  constexpr uint32_t kMaxNumberOfSections = enum_cast<uint32_t>(FileSectionType::kNumberOfSections);
  constexpr uint64_t kMaxHeaderAndInfosSize =
      sizeof(FileHeader) + kMaxNumberOfSections * sizeof(FileSectionInfo);
  DCHECK_LE(header_and_infos_size, kMaxHeaderAndInfosSize);
  std::array<uint8_t, kMaxHeaderAndInfosSize> placeholder;
  memset(placeholder.data(), 0, header_and_infos_size);
  if (!WriteBuffer(fd, placeholder.data(), header_and_infos_size)) {
    return false;
  }

  std::array<FileSectionInfo, kMaxNumberOfSections> section_infos;
  size_t section_index = 0u;
  uint32_t file_offset = header_and_infos_size;
  auto add_section_info = [&](FileSectionType type, uint32_t file_size, uint32_t inflated_size) {
    DCHECK_LT(section_index, section_infos.size());
    section_infos[section_index] = FileSectionInfo(type, file_offset, file_size, inflated_size);
    file_offset += file_size;
    section_index += 1u;
  };

  // Write the dex files section.
  {
    SafeBuffer buffer(dex_files_section_size);
    buffer.WriteUintAndAdvance(dchecked_integral_cast<ProfileIndexType>(info_.size()));
    for (const std::unique_ptr<DexFileData>& dex_data : info_) {
      buffer.WriteUintAndAdvance(dex_data->checksum);
      buffer.WriteUintAndAdvance(dex_data->num_type_ids);
      buffer.WriteUintAndAdvance(dex_data->num_method_ids);
      buffer.WriteAndAdvance(dex_data->profile_key.c_str(), dex_data->profile_key.size() + 1u);
    }
    DCHECK_EQ(buffer.GetAvailableBytes(), 0u);
    // Write the dex files section uncompressed.
    if (!WriteBuffer(fd, buffer.Get(), dex_files_section_size)) {
      return false;
    }
    add_section_info(FileSectionType::kDexFiles, dex_files_section_size, /*inflated_size=*/ 0u);
  }

  // Write the extra descriptors section.
  if (extra_descriptors_section_size != 0u) {
    SafeBuffer buffer(extra_descriptors_section_size);
    buffer.WriteUintAndAdvance(dchecked_integral_cast<uint16_t>(extra_descriptors_.size()));
    for (const std::string& descriptor : extra_descriptors_) {
      buffer.WriteAndAdvance(descriptor.c_str(), descriptor.size() + 1u);
    }
    if (!buffer.Deflate()) {
      return false;
    }
    if (!WriteBuffer(fd, buffer.Get(), buffer.Size())) {
      return false;
    }
    add_section_info(
        FileSectionType::kExtraDescriptors, buffer.Size(), extra_descriptors_section_size);
  }

  // Write the classes section.
  if (classes_section_size != 0u) {
    SafeBuffer buffer(classes_section_size);
    for (const std::unique_ptr<DexFileData>& dex_data : info_) {
      dex_data->WriteClasses(buffer);
    }
    if (!buffer.Deflate()) {
      return false;
    }
    if (!WriteBuffer(fd, buffer.Get(), buffer.Size())) {
      return false;
    }
    add_section_info(FileSectionType::kClasses, buffer.Size(), classes_section_size);
  }

  // Write the methods section.
  if (methods_section_size != 0u) {
    SafeBuffer buffer(methods_section_size);
    for (const std::unique_ptr<DexFileData>& dex_data : info_) {
      dex_data->WriteMethods(buffer);
    }
    if (!buffer.Deflate()) {
      return false;
    }
    if (!WriteBuffer(fd, buffer.Get(), buffer.Size())) {
      return false;
    }
    add_section_info(FileSectionType::kMethods, buffer.Size(), methods_section_size);
  }

  if (file_offset > GetSizeWarningThresholdBytes()) {
    LOG(WARNING) << "Profile data size exceeds "
        << GetSizeWarningThresholdBytes()
        << " It has " << file_offset << " bytes";
  }

  // Write section infos.
  if (lseek64(fd, sizeof(FileHeader), SEEK_SET) != sizeof(FileHeader)) {
    return false;
  }
  if (!WriteBuffer(fd, section_infos.data(), section_index * sizeof(FileSectionInfo))) {
    return false;
  }

  // Write header.
  FileHeader header(version_, section_index);
  if (lseek(fd, 0, SEEK_SET) != 0) {
    return false;
  }
  if (!WriteBuffer(fd, &header, sizeof(FileHeader))) {
    return false;
  }

  uint64_t total_time = NanoTime() - start;
  VLOG(profiler) << "Compressed from "
                 << std::to_string(total_uncompressed_size)
                 << " to "
                 << std::to_string(file_offset);
  VLOG(profiler) << "Time to save profile: " << std::to_string(total_time);
  return true;
}

ProfileCompilationInfo::DexFileData* ProfileCompilationInfo::GetOrAddDexFileData(
    const std::string& profile_key,
    uint32_t checksum,
    uint32_t num_type_ids,
    uint32_t num_method_ids) {
  DCHECK_EQ(profile_key_map_.size(), info_.size());
  auto profile_index_it = profile_key_map_.lower_bound(profile_key);
  if (profile_index_it == profile_key_map_.end() || profile_index_it->first != profile_key) {
    // We did not find the key. Create a new DexFileData if we did not reach the limit.
    DCHECK_LE(profile_key_map_.size(), MaxProfileIndex());
    if (profile_key_map_.size() == MaxProfileIndex()) {
      // Allow only a limited number dex files to be profiled. This allows us to save bytes
      // when encoding. For regular profiles this 2^8, and for boot profiles is 2^16
      // (well above what we expect for normal applications).
      LOG(ERROR) << "Exceeded the maximum number of dex file. Something went wrong";
      return nullptr;
    }
    ProfileIndexType new_profile_index = dchecked_integral_cast<ProfileIndexType>(info_.size());
    std::unique_ptr<DexFileData> dex_file_data(new (&allocator_) DexFileData(
        &allocator_,
        profile_key,
        checksum,
        new_profile_index,
        num_type_ids,
        num_method_ids,
        IsForBootImage()));
    // Record the new data in `profile_key_map_` and `info_`.
    std::string_view new_key(dex_file_data->profile_key);
    profile_index_it = profile_key_map_.PutBefore(profile_index_it, new_key, new_profile_index);
    info_.push_back(std::move(dex_file_data));
    DCHECK_EQ(profile_key_map_.size(), info_.size());
  }

  ProfileIndexType profile_index = profile_index_it->second;
  DexFileData* result = info_[profile_index].get();

  // Check that the checksum matches.
  // This may different if for example the dex file was updated and we had a record of the old one.
  if (result->checksum != checksum) {
    LOG(WARNING) << "Checksum mismatch for dex " << profile_key;
    return nullptr;
  }

  // DCHECK that profile info map key is consistent with the one stored in the dex file data.
  // This should always be the case since since the cache map is managed by ProfileCompilationInfo.
  DCHECK_EQ(profile_key, result->profile_key);
  DCHECK_EQ(profile_index, result->profile_index);

  if (num_type_ids != result->num_type_ids || num_method_ids != result->num_method_ids) {
    // This should not happen... added to help investigating b/65812889.
    LOG(ERROR) << "num_type_ids or num_method_ids mismatch for dex " << profile_key
        << ", types: expected=" << num_type_ids << " v. actual=" << result->num_type_ids
        << ", methods: expected=" << num_method_ids << " actual=" << result->num_method_ids;
    return nullptr;
  }

  return result;
}

const ProfileCompilationInfo::DexFileData* ProfileCompilationInfo::FindDexData(
      const std::string& profile_key,
      uint32_t checksum,
      bool verify_checksum) const {
  const auto profile_index_it = profile_key_map_.find(profile_key);
  if (profile_index_it == profile_key_map_.end()) {
    return nullptr;
  }

  ProfileIndexType profile_index = profile_index_it->second;
  const DexFileData* result = info_[profile_index].get();
  if (verify_checksum && !ChecksumMatch(result->checksum, checksum)) {
    return nullptr;
  }
  DCHECK_EQ(profile_key, result->profile_key);
  DCHECK_EQ(profile_index, result->profile_index);
  return result;
}

const ProfileCompilationInfo::DexFileData* ProfileCompilationInfo::FindDexDataUsingAnnotations(
      const DexFile* dex_file,
      const ProfileSampleAnnotation& annotation) const {
  if (annotation == ProfileSampleAnnotation::kNone) {
    std::string_view profile_key = GetProfileDexFileBaseKeyView(dex_file->GetLocation());
    for (const std::unique_ptr<DexFileData>& dex_data : info_) {
      if (profile_key == GetBaseKeyViewFromAugmentedKey(dex_data->profile_key)) {
        if (!ChecksumMatch(dex_data->checksum, dex_file->GetLocationChecksum())) {
          return nullptr;
        }
        return dex_data.get();
      }
    }
  } else {
    std::string profile_key = GetProfileDexFileAugmentedKey(dex_file->GetLocation(), annotation);
    return FindDexData(profile_key, dex_file->GetLocationChecksum());
  }

  return nullptr;
}

void ProfileCompilationInfo::FindAllDexData(
    const DexFile* dex_file,
    /*out*/ std::vector<const ProfileCompilationInfo::DexFileData*>* result) const {
  std::string_view profile_key = GetProfileDexFileBaseKeyView(dex_file->GetLocation());
  for (const std::unique_ptr<DexFileData>& dex_data : info_) {
    if (profile_key == GetBaseKeyViewFromAugmentedKey(dex_data->profile_key)) {
      if (ChecksumMatch(dex_data->checksum, dex_file->GetLocationChecksum())) {
        result->push_back(dex_data.get());
      }
    }
  }
}

ProfileCompilationInfo::ExtraDescriptorIndex ProfileCompilationInfo::AddExtraDescriptor(
    std::string_view extra_descriptor) {
  DCHECK(extra_descriptors_indexes_.find(extra_descriptor) == extra_descriptors_indexes_.end());
  ExtraDescriptorIndex new_extra_descriptor_index = extra_descriptors_.size();
  DCHECK_LE(new_extra_descriptor_index, kMaxExtraDescriptors);
  if (UNLIKELY(new_extra_descriptor_index == kMaxExtraDescriptors)) {
    return kMaxExtraDescriptors;  // Cannot add another extra descriptor.
  }
  // Add the extra descriptor and record the new index.
  extra_descriptors_.emplace_back(extra_descriptor);
  extra_descriptors_indexes_.insert(new_extra_descriptor_index);
  return new_extra_descriptor_index;
}

bool ProfileCompilationInfo::AddMethod(const ProfileMethodInfo& pmi,
                                       MethodHotness::Flag flags,
                                       const ProfileSampleAnnotation& annotation) {
  DexFileData* const data = GetOrAddDexFileData(pmi.ref.dex_file, annotation);
  if (data == nullptr) {  // checksum mismatch
    return false;
  }
  if (!data->AddMethod(flags, pmi.ref.index)) {
    return false;
  }
  if ((flags & MethodHotness::kFlagHot) == 0) {
    // The method is not hot, do not add inline caches.
    return true;
  }

  // Add inline caches.
  InlineCacheMap* inline_cache = data->FindOrAddHotMethod(pmi.ref.index);
  DCHECK(inline_cache != nullptr);

  for (const ProfileMethodInfo::ProfileInlineCache& cache : pmi.inline_caches) {
    if (cache.is_missing_types) {
      FindOrAddDexPc(inline_cache, cache.dex_pc)->SetIsMissingTypes();
      continue;
    }
    if  (cache.is_megamorphic) {
      FindOrAddDexPc(inline_cache, cache.dex_pc)->SetIsMegamorphic();
      continue;
    }
    for (const TypeReference& class_ref : cache.classes) {
      DexPcData* dex_pc_data = FindOrAddDexPc(inline_cache, cache.dex_pc);
      if (dex_pc_data->is_missing_types || dex_pc_data->is_megamorphic) {
        // Don't bother adding classes if we are missing types or already megamorphic.
        break;
      }
      dex::TypeIndex type_index = FindOrCreateTypeIndex(*pmi.ref.dex_file, class_ref);
      if (type_index.IsValid()) {
        dex_pc_data->AddClass(type_index);
      } else {
        // Could not create artificial type index.
        dex_pc_data->SetIsMissingTypes();
      }
    }
  }
  return true;
}

// TODO(calin): Fix this API. ProfileCompilationInfo::Load should be static and
// return a unique pointer to a ProfileCompilationInfo upon success.
bool ProfileCompilationInfo::Load(
    int fd, bool merge_classes, const ProfileLoadFilterFn& filter_fn) {
  std::string error;

  ProfileLoadStatus status = LoadInternal(fd, &error, merge_classes, filter_fn);

  if (status == ProfileLoadStatus::kSuccess) {
    return true;
  } else {
    LOG(WARNING) << "Error when reading profile: " << error;
    return false;
  }
}

bool ProfileCompilationInfo::VerifyProfileData(const std::vector<const DexFile*>& dex_files) {
  std::unordered_map<std::string_view, const DexFile*> key_to_dex_file;
  for (const DexFile* dex_file : dex_files) {
    key_to_dex_file.emplace(GetProfileDexFileBaseKeyView(dex_file->GetLocation()), dex_file);
  }
  for (const std::unique_ptr<DexFileData>& dex_data : info_) {
    // We need to remove any annotation from the key during verification.
    const auto it = key_to_dex_file.find(GetBaseKeyViewFromAugmentedKey(dex_data->profile_key));
    if (it == key_to_dex_file.end()) {
      // It is okay if profile contains data for additional dex files.
      continue;
    }
    const DexFile* dex_file = it->second;
    const std::string& dex_location = dex_file->GetLocation();
    if (!ChecksumMatch(dex_data->checksum, dex_file->GetLocationChecksum())) {
      LOG(ERROR) << "Dex checksum mismatch while verifying profile "
                 << "dex location " << dex_location << " (checksum="
                 << dex_file->GetLocationChecksum() << ", profile checksum="
                 << dex_data->checksum;
      return false;
    }

    if (dex_data->num_method_ids != dex_file->NumMethodIds() ||
        dex_data->num_type_ids != dex_file->NumTypeIds()) {
      LOG(ERROR) << "Number of type or method ids in dex file and profile don't match."
                 << "dex location " << dex_location
                 << " dex_file.NumTypeIds=" << dex_file->NumTypeIds()
                 << " .v dex_data.num_type_ids=" << dex_data->num_type_ids
                 << ", dex_file.NumMethodIds=" << dex_file->NumMethodIds()
                 << " v. dex_data.num_method_ids=" << dex_data->num_method_ids;
      return false;
    }

    // Class and method data should be valid. Verify only in debug builds.
    if (kIsDebugBuild) {
      // Verify method_encoding.
      for (const auto& method_it : dex_data->method_map) {
        CHECK_LT(method_it.first, dex_data->num_method_ids);

        // Verify class indices of inline caches.
        const InlineCacheMap &inline_cache_map = method_it.second;
        for (const auto& inline_cache_it : inline_cache_map) {
          const DexPcData& dex_pc_data = inline_cache_it.second;
          if (dex_pc_data.is_missing_types || dex_pc_data.is_megamorphic) {
            // No class indices to verify.
            CHECK(dex_pc_data.classes.empty());
            continue;
          }

          for (const dex::TypeIndex& type_index : dex_pc_data.classes) {
            if (type_index.index_ >= dex_data->num_type_ids) {
              CHECK_LT(type_index.index_ - dex_data->num_type_ids, extra_descriptors_.size());
            }
          }
        }
      }
      // Verify class_ids.
      for (const dex::TypeIndex& type_index : dex_data->class_set) {
        if (type_index.index_ >= dex_data->num_type_ids) {
          CHECK_LT(type_index.index_ - dex_data->num_type_ids, extra_descriptors_.size());
        }
      }
    }
  }
  return true;
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::OpenSource(
    int32_t fd,
    /*out*/ std::unique_ptr<ProfileSource>* source,
    /*out*/ std::string* error) {
  if (IsProfileFile(fd)) {
    source->reset(ProfileSource::Create(fd));
    return ProfileLoadStatus::kSuccess;
  } else {
    std::unique_ptr<ZipArchive> zip_archive(
        ZipArchive::OpenFromFd(DupCloexec(fd), "profile", error));
    if (zip_archive.get() == nullptr) {
      *error = "Could not open the profile zip archive";
      return ProfileLoadStatus::kBadData;
    }
    std::unique_ptr<ZipEntry> zip_entry(zip_archive->Find(kDexMetadataProfileEntry, error));
    if (zip_entry == nullptr) {
      // Allow archives without the profile entry. In this case, create an empty profile.
      // This gives more flexible when ure-using archives that may miss the entry.
      // (e.g. dex metadata files)
      LOG(WARNING) << "Could not find entry " << kDexMetadataProfileEntry
          << " in the zip archive. Creating an empty profile.";
      source->reset(ProfileSource::Create(MemMap::Invalid()));
      return ProfileLoadStatus::kSuccess;
    }
    if (zip_entry->GetUncompressedLength() == 0) {
      *error = "Empty profile entry in the zip archive.";
      return ProfileLoadStatus::kBadData;
    }

    // TODO(calin) pass along file names to assist with debugging.
    MemMap map = zip_entry->MapDirectlyOrExtract(
        kDexMetadataProfileEntry, "profile file", error, alignof(ProfileSource));

    if (map.IsValid()) {
      source->reset(ProfileSource::Create(std::move(map)));
      return ProfileLoadStatus::kSuccess;
    } else {
      return ProfileLoadStatus::kBadData;
    }
  }
}

bool ProfileCompilationInfo::ProfileSource::Seek(off_t offset) {
  DCHECK_GE(offset, 0);
  if (IsMemMap()) {
    if (offset > static_cast<int64_t>(mem_map_.Size())) {
      return false;
    }
    mem_map_cur_ = offset;
    return true;
  } else {
    if (lseek64(fd_, offset, SEEK_SET) != offset) {
      return false;
    }
    return true;
  }
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::ProfileSource::Read(
    void* buffer,
    size_t byte_count,
    const std::string& debug_stage,
    std::string* error) {
  if (IsMemMap()) {
    DCHECK_LE(mem_map_cur_, mem_map_.Size());
    if (byte_count > mem_map_.Size() - mem_map_cur_) {
      return ProfileLoadStatus::kBadData;
    }
    memcpy(buffer, mem_map_.Begin() + mem_map_cur_, byte_count);
    mem_map_cur_ += byte_count;
  } else {
    while (byte_count > 0) {
      int bytes_read = TEMP_FAILURE_RETRY(read(fd_, buffer, byte_count));;
      if (bytes_read == 0) {
        *error += "Profile EOF reached prematurely for " + debug_stage;
        return ProfileLoadStatus::kBadData;
      } else if (bytes_read < 0) {
        *error += "Profile IO error for " + debug_stage + strerror(errno);
        return ProfileLoadStatus::kIOError;
      }
      byte_count -= bytes_read;
      reinterpret_cast<uint8_t*&>(buffer) += bytes_read;
    }
  }
  return ProfileLoadStatus::kSuccess;
}


bool ProfileCompilationInfo::ProfileSource::HasEmptyContent() const {
  if (IsMemMap()) {
    return !mem_map_.IsValid() || mem_map_.Size() == 0;
  } else {
    struct stat stat_buffer;
    if (fstat(fd_, &stat_buffer) != 0) {
      return false;
    }
    return stat_buffer.st_size == 0;
  }
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::ReadSectionData(
    ProfileSource& source,
    const FileSectionInfo& section_info,
    /*out*/ SafeBuffer* buffer,
    /*out*/ std::string* error) {
  DCHECK_EQ(buffer->Size(), 0u);
  if (!source.Seek(section_info.GetFileOffset())) {
    *error = "Failed to seek to section data.";
    return ProfileLoadStatus::kIOError;
  }
  SafeBuffer temp_buffer(section_info.GetFileSize());
  ProfileLoadStatus status = source.Read(
      temp_buffer.GetCurrentPtr(), temp_buffer.GetAvailableBytes(), "ReadSectionData", error);
  if (status != ProfileLoadStatus::kSuccess) {
    return status;
  }
  if (section_info.GetInflatedSize() != 0u &&
      !temp_buffer.Inflate(section_info.GetInflatedSize())) {
    *error += "Error uncompressing section data.";
    return ProfileLoadStatus::kBadData;
  }
  buffer->Swap(temp_buffer);
  return ProfileLoadStatus::kSuccess;
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::ReadDexFilesSection(
    ProfileSource& source,
    const FileSectionInfo& section_info,
    const ProfileLoadFilterFn& filter_fn,
    /*out*/ dchecked_vector<ProfileIndexType>* dex_profile_index_remap,
    /*out*/ std::string* error) {
  DCHECK(section_info.GetType() == FileSectionType::kDexFiles);
  SafeBuffer buffer;
  ProfileLoadStatus status = ReadSectionData(source, section_info, &buffer, error);
  if (status != ProfileLoadStatus::kSuccess) {
    return status;
  }

  ProfileIndexType num_dex_files;
  if (!buffer.ReadUintAndAdvance(&num_dex_files)) {
    *error = "Error reading number of dex files.";
    return ProfileLoadStatus::kBadData;
  }
  if (num_dex_files >= MaxProfileIndex()) {
    *error = "Too many dex files.";
    return ProfileLoadStatus::kBadData;
  }

  DCHECK(dex_profile_index_remap->empty());
  for (ProfileIndexType i = 0u; i != num_dex_files; ++i) {
    uint32_t checksum, num_type_ids, num_method_ids;
    if (!buffer.ReadUintAndAdvance(&checksum) ||
        !buffer.ReadUintAndAdvance(&num_type_ids) ||
        !buffer.ReadUintAndAdvance(&num_method_ids)) {
      *error = "Error reading dex file data.";
      return ProfileLoadStatus::kBadData;
    }
    std::string_view profile_key_view;
    if (!buffer.ReadStringAndAdvance(&profile_key_view)) {
      *error += "Missing terminating null character for profile key.";
      return ProfileLoadStatus::kBadData;
    }
    if (profile_key_view.size() == 0u || profile_key_view.size() > kMaxDexFileKeyLength) {
      *error = "ProfileKey has an invalid size: " + std::to_string(profile_key_view.size());
      return ProfileLoadStatus::kBadData;
    }
    std::string profile_key(profile_key_view);
    if (!filter_fn(profile_key, checksum)) {
      // Do not load data for this key. Store invalid index to `dex_profile_index_remap`.
      VLOG(compiler) << "Profile: Filtered out " << profile_key << " 0x" << std::hex << checksum;
      dex_profile_index_remap->push_back(MaxProfileIndex());
      continue;
    }
    DexFileData* data = GetOrAddDexFileData(profile_key, checksum, num_type_ids, num_method_ids);
    if (data == nullptr) {
      if (UNLIKELY(profile_key_map_.size() == MaxProfileIndex()) &&
          profile_key_map_.find(profile_key) == profile_key_map_.end()) {
        *error = "Too many dex files.";
      } else {
        *error = "Checksum, NumTypeIds, or NumMethodIds mismatch for " + profile_key;
      }
      return ProfileLoadStatus::kBadData;
    }
    dex_profile_index_remap->push_back(data->profile_index);
  }
  if (buffer.GetAvailableBytes() != 0u) {
    *error = "Unexpected data at end of dex files section.";
    return ProfileLoadStatus::kBadData;
  }
  return ProfileLoadStatus::kSuccess;
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::ReadExtraDescriptorsSection(
    ProfileSource& source,
    const FileSectionInfo& section_info,
    /*out*/ dchecked_vector<ExtraDescriptorIndex>* extra_descriptors_remap,
    /*out*/ std::string* error) {
  DCHECK(section_info.GetType() == FileSectionType::kExtraDescriptors);
  SafeBuffer buffer;
  ProfileLoadStatus status = ReadSectionData(source, section_info, &buffer, error);
  if (status != ProfileLoadStatus::kSuccess) {
    return status;
  }

  uint16_t num_extra_descriptors;
  if (!buffer.ReadUintAndAdvance(&num_extra_descriptors)) {
    *error = "Error reading number of extra descriptors.";
    return ProfileLoadStatus::kBadData;
  }

  // Note: We allow multiple extra descriptors sections in a single profile file
  // but that can lead to `kMergeError` if there are too many extra descriptors.
  // Other sections can reference only extra descriptors from preceding sections.
  extra_descriptors_remap->reserve(
      std::min<size_t>(extra_descriptors_remap->size() + num_extra_descriptors,
                       std::numeric_limits<uint16_t>::max()));
  for (uint16_t i = 0; i != num_extra_descriptors; ++i) {
    const char* descriptor = reinterpret_cast<const char*>(buffer.GetCurrentPtr());
    std::string_view extra_descriptor;
    if (!buffer.ReadStringAndAdvance(&extra_descriptor)) {
      *error += "Missing terminating null character for extra descriptor.";
      return ProfileLoadStatus::kBadData;
    }
    if (!IsValidDescriptor(descriptor)) {
      *error += "Invalid extra descriptor.";
      return ProfileLoadStatus::kBadData;
    }
    // Try to match an existing extra descriptor.
    auto it = extra_descriptors_indexes_.find(extra_descriptor);
    if (it != extra_descriptors_indexes_.end()) {
      extra_descriptors_remap->push_back(*it);
      continue;
    }
    // Try to insert a new extra descriptor.
    ExtraDescriptorIndex extra_descriptor_index = AddExtraDescriptor(extra_descriptor);
    if (extra_descriptor_index == kMaxExtraDescriptors) {
      *error = "Too many extra descriptors.";
      return ProfileLoadStatus::kMergeError;
    }
    extra_descriptors_remap->push_back(extra_descriptor_index);
  }
  return ProfileLoadStatus::kSuccess;
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::ReadClassesSection(
    ProfileSource& source,
    const FileSectionInfo& section_info,
    const dchecked_vector<ProfileIndexType>& dex_profile_index_remap,
    const dchecked_vector<ExtraDescriptorIndex>& extra_descriptors_remap,
    /*out*/ std::string* error) {
  DCHECK(section_info.GetType() == FileSectionType::kClasses);
  SafeBuffer buffer;
  ProfileLoadStatus status = ReadSectionData(source, section_info, &buffer, error);
  if (status != ProfileLoadStatus::kSuccess) {
    return status;
  }

  while (buffer.GetAvailableBytes() != 0u) {
    ProfileIndexType profile_index;
    if (!buffer.ReadUintAndAdvance(&profile_index)) {
      *error = "Error profile index in classes section.";
      return ProfileLoadStatus::kBadData;
    }
    if (profile_index >= dex_profile_index_remap.size()) {
      *error = "Invalid profile index in classes section.";
      return ProfileLoadStatus::kBadData;
    }
    profile_index = dex_profile_index_remap[profile_index];
    if (profile_index == MaxProfileIndex()) {
      status = DexFileData::SkipClasses(buffer, error);
    } else {
      status = info_[profile_index]->ReadClasses(buffer, extra_descriptors_remap, error);
    }
    if (status != ProfileLoadStatus::kSuccess) {
      return status;
    }
  }
  return ProfileLoadStatus::kSuccess;
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::ReadMethodsSection(
    ProfileSource& source,
    const FileSectionInfo& section_info,
    const dchecked_vector<ProfileIndexType>& dex_profile_index_remap,
    const dchecked_vector<ExtraDescriptorIndex>& extra_descriptors_remap,
    /*out*/ std::string* error) {
  DCHECK(section_info.GetType() == FileSectionType::kMethods);
  SafeBuffer buffer;
  ProfileLoadStatus status = ReadSectionData(source, section_info, &buffer, error);
  if (status != ProfileLoadStatus::kSuccess) {
    return status;
  }

  while (buffer.GetAvailableBytes() != 0u) {
    ProfileIndexType profile_index;
    if (!buffer.ReadUintAndAdvance(&profile_index)) {
      *error = "Error profile index in methods section.";
      return ProfileLoadStatus::kBadData;
    }
    if (profile_index >= dex_profile_index_remap.size()) {
      *error = "Invalid profile index in methods section.";
      return ProfileLoadStatus::kBadData;
    }
    profile_index = dex_profile_index_remap[profile_index];
    if (profile_index == MaxProfileIndex()) {
      status = DexFileData::SkipMethods(buffer, error);
    } else {
      status = info_[profile_index]->ReadMethods(buffer, extra_descriptors_remap, error);
    }
    if (status != ProfileLoadStatus::kSuccess) {
      return status;
    }
  }
  return ProfileLoadStatus::kSuccess;
}

// TODO(calin): fail fast if the dex checksums don't match.
ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::LoadInternal(
    int32_t fd,
    std::string* error,
    bool merge_classes,
    const ProfileLoadFilterFn& filter_fn) {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  DCHECK_GE(fd, 0);

  std::unique_ptr<ProfileSource> source;
  ProfileLoadStatus status = OpenSource(fd, &source, error);
  if (status != ProfileLoadStatus::kSuccess) {
    return status;
  }

  // We allow empty profile files.
  // Profiles may be created by ActivityManager or installd before we manage to
  // process them in the runtime or profman.
  if (source->HasEmptyContent()) {
    return ProfileLoadStatus::kSuccess;
  }

  // Read file header.
  FileHeader header;
  status = source->Read(&header, sizeof(FileHeader), "ReadProfileHeader", error);
  if (status != ProfileLoadStatus::kSuccess) {
    return status;
  }
  if (!header.IsValid()) {
    return header.InvalidHeaderMessage(error);
  }
  if (memcmp(header.GetVersion(), version_, kProfileVersionSize) != 0) {
    *error = IsForBootImage() ? "Expected boot profile, got app profile."
                              : "Expected app profile, got boot profile.";
    return ProfileLoadStatus::kMergeError;
  }

  // Check if there are too many section infos.
  uint32_t section_count = header.GetFileSectionCount();
  uint32_t uncompressed_data_size = sizeof(FileHeader) + section_count * sizeof(FileSectionInfo);
  if (uncompressed_data_size > GetSizeErrorThresholdBytes()) {
    LOG(ERROR) << "Profile data size exceeds " << GetSizeErrorThresholdBytes()
               << " bytes. It has " << uncompressed_data_size << " bytes.";
    return ProfileLoadStatus::kBadData;
  }

  // Read section infos.
  dchecked_vector<FileSectionInfo> section_infos(section_count);
  status = source->Read(
      section_infos.data(), section_count * sizeof(FileSectionInfo), "ReadSectionInfos", error);
  if (status != ProfileLoadStatus::kSuccess) {
    return status;
  }

  // Finish uncompressed data size calculation.
  for (const FileSectionInfo& section_info : section_infos) {
    uint32_t mem_size = section_info.GetMemSize();
    if (UNLIKELY(mem_size > std::numeric_limits<uint32_t>::max() - uncompressed_data_size)) {
      *error = "Total memory size overflow.";
      return ProfileLoadStatus::kBadData;
    }
    uncompressed_data_size += mem_size;
  }

  // Allow large profiles for non target builds for the case where we are merging many profiles
  // to generate a boot image profile.
  if (uncompressed_data_size > GetSizeErrorThresholdBytes()) {
    LOG(ERROR) << "Profile data size exceeds "
               << GetSizeErrorThresholdBytes()
               << " bytes. It has " << uncompressed_data_size << " bytes.";
    return ProfileLoadStatus::kBadData;
  }
  if (uncompressed_data_size > GetSizeWarningThresholdBytes()) {
    LOG(WARNING) << "Profile data size exceeds "
                 << GetSizeWarningThresholdBytes()
                 << " bytes. It has " << uncompressed_data_size << " bytes.";
  }

  // Process the mandatory dex files section.
  DCHECK_NE(section_count, 0u);  // Checked by `header.IsValid()` above.
  const FileSectionInfo& dex_files_section_info = section_infos[0];
  if (dex_files_section_info.GetType() != FileSectionType::kDexFiles) {
    *error = "First section is not dex files section.";
    return ProfileLoadStatus::kBadData;
  }
  dchecked_vector<ProfileIndexType> dex_profile_index_remap;
  status = ReadDexFilesSection(
      *source, dex_files_section_info, filter_fn, &dex_profile_index_remap, error);
  if (status != ProfileLoadStatus::kSuccess) {
    DCHECK(!error->empty());
    return status;
  }

  // Process all other sections.
  dchecked_vector<ExtraDescriptorIndex> extra_descriptors_remap;
  for (uint32_t i = 1u; i != section_count; ++i) {
    const FileSectionInfo& section_info = section_infos[i];
    DCHECK(status == ProfileLoadStatus::kSuccess);
    switch (section_info.GetType()) {
      case FileSectionType::kDexFiles:
        *error = "Unsupported additional dex files section.";
        status = ProfileLoadStatus::kBadData;
        break;
      case FileSectionType::kExtraDescriptors:
        status = ReadExtraDescriptorsSection(
            *source, section_info, &extra_descriptors_remap, error);
        break;
      case FileSectionType::kClasses:
        // Skip if all dex files were filtered out.
        if (!info_.empty() && merge_classes) {
          status = ReadClassesSection(
              *source, section_info, dex_profile_index_remap, extra_descriptors_remap, error);
        }
        break;
      case FileSectionType::kMethods:
        // Skip if all dex files were filtered out.
        if (!info_.empty()) {
          status = ReadMethodsSection(
              *source, section_info, dex_profile_index_remap, extra_descriptors_remap, error);
        }
        break;
      default:
        // Unknown section. Skip it. New versions of ART are allowed
        // to add sections that shall be ignored by old versions.
        break;
    }
    if (status != ProfileLoadStatus::kSuccess) {
      DCHECK(!error->empty());
      return status;
    }
  }

  return ProfileLoadStatus::kSuccess;
}

bool ProfileCompilationInfo::MergeWith(const ProfileCompilationInfo& other,
                                       bool merge_classes) {
  if (!SameVersion(other)) {
    LOG(WARNING) << "Cannot merge different profile versions";
    return false;
  }

  // First verify that all checksums match. This will avoid adding garbage to
  // the current profile info.
  // Note that the number of elements should be very small, so this should not
  // be a performance issue.
  for (const std::unique_ptr<DexFileData>& other_dex_data : other.info_) {
    // verify_checksum is false because we want to differentiate between a missing dex data and
    // a mismatched checksum.
    const DexFileData* dex_data = FindDexData(other_dex_data->profile_key,
                                              /* checksum= */ 0u,
                                              /* verify_checksum= */ false);
    if ((dex_data != nullptr) && (dex_data->checksum != other_dex_data->checksum)) {
      LOG(WARNING) << "Checksum mismatch for dex " << other_dex_data->profile_key;
      return false;
    }
  }
  // All checksums match. Import the data.

  // The other profile might have a different indexing of dex files.
  // That is because each dex files gets a 'dex_profile_index' on a first come first served basis.
  // That means that the order in with the methods are added to the profile matters for the
  // actual indices.
  // The reason we cannot rely on the actual multidex index is that a single profile may store
  // data from multiple splits. This means that a profile may contain a classes2.dex from split-A
  // and one from split-B.

  // First, build a mapping from other_dex_profile_index to this_dex_profile_index.
  dchecked_vector<ProfileIndexType> dex_profile_index_remap;
  dex_profile_index_remap.reserve(other.info_.size());
  for (const std::unique_ptr<DexFileData>& other_dex_data : other.info_) {
    const DexFileData* dex_data = GetOrAddDexFileData(other_dex_data->profile_key,
                                                      other_dex_data->checksum,
                                                      other_dex_data->num_type_ids,
                                                      other_dex_data->num_method_ids);
    if (dex_data == nullptr) {
      // Could happen if we exceed the number of allowed dex files or there is
      // a mismatch in `num_type_ids` or `num_method_ids`.
      return false;
    }
    DCHECK_EQ(other_dex_data->profile_index, dex_profile_index_remap.size());
    dex_profile_index_remap.push_back(dex_data->profile_index);
  }

  // Then merge extra descriptors.
  dchecked_vector<ExtraDescriptorIndex> extra_descriptors_remap;
  extra_descriptors_remap.reserve(other.extra_descriptors_.size());
  for (const std::string& other_extra_descriptor : other.extra_descriptors_) {
    auto it = extra_descriptors_indexes_.find(std::string_view(other_extra_descriptor));
    if (it != extra_descriptors_indexes_.end()) {
      extra_descriptors_remap.push_back(*it);
    } else {
      ExtraDescriptorIndex extra_descriptor_index = AddExtraDescriptor(other_extra_descriptor);
      if (extra_descriptor_index == kMaxExtraDescriptors) {
        // Too many extra descriptors.
        return false;
      }
      extra_descriptors_remap.push_back(extra_descriptor_index);
    }
  }

  // Merge the actual profile data.
  for (const std::unique_ptr<DexFileData>& other_dex_data : other.info_) {
    DexFileData* dex_data = info_[dex_profile_index_remap[other_dex_data->profile_index]].get();
    DCHECK_EQ(dex_data, FindDexData(other_dex_data->profile_key, other_dex_data->checksum));

    // Merge the classes.
    uint32_t num_type_ids = dex_data->num_type_ids;
    DCHECK_EQ(num_type_ids, other_dex_data->num_type_ids);
    if (merge_classes) {
      // Classes are ordered by the `TypeIndex`, so we have the classes with a `TypeId`
      // in the dex file first, followed by classes using extra descriptors.
      auto it = other_dex_data->class_set.lower_bound(dex::TypeIndex(num_type_ids));
      dex_data->class_set.insert(other_dex_data->class_set.begin(), it);
      for (auto end = other_dex_data->class_set.end(); it != end; ++it) {
        ExtraDescriptorIndex new_extra_descriptor_index =
            extra_descriptors_remap[it->index_ - num_type_ids];
        if (new_extra_descriptor_index >= DexFile::kDexNoIndex16 - num_type_ids) {
          // Cannot represent the type with new extra descriptor index.
          return false;
        }
        dex_data->class_set.insert(dex::TypeIndex(num_type_ids + new_extra_descriptor_index));
      }
    }

    // Merge the methods and the inline caches.
    for (const auto& other_method_it : other_dex_data->method_map) {
      uint16_t other_method_index = other_method_it.first;
      InlineCacheMap* inline_cache = dex_data->FindOrAddHotMethod(other_method_index);
      if (inline_cache == nullptr) {
        return false;
      }
      const auto& other_inline_cache = other_method_it.second;
      for (const auto& other_ic_it : other_inline_cache) {
        uint16_t other_dex_pc = other_ic_it.first;
        const ArenaSet<dex::TypeIndex>& other_class_set = other_ic_it.second.classes;
        DexPcData* dex_pc_data = FindOrAddDexPc(inline_cache, other_dex_pc);
        if (other_ic_it.second.is_missing_types) {
          dex_pc_data->SetIsMissingTypes();
        } else if (other_ic_it.second.is_megamorphic) {
          dex_pc_data->SetIsMegamorphic();
        } else {
          for (dex::TypeIndex type_index : other_class_set) {
            if (type_index.index_ >= num_type_ids) {
              ExtraDescriptorIndex new_extra_descriptor_index =
                  extra_descriptors_remap[type_index.index_ - num_type_ids];
              if (new_extra_descriptor_index >= DexFile::kDexNoIndex16 - num_type_ids) {
                // Cannot represent the type with new extra descriptor index.
                return false;
              }
              type_index = dex::TypeIndex(num_type_ids + new_extra_descriptor_index);
            }
            dex_pc_data->AddClass(type_index);
          }
        }
      }
    }

    // Merge the method bitmaps.
    dex_data->MergeBitmap(*other_dex_data);
  }

  return true;
}

ProfileCompilationInfo::MethodHotness ProfileCompilationInfo::GetMethodHotness(
    const MethodReference& method_ref,
    const ProfileSampleAnnotation& annotation) const {
  const DexFileData* dex_data = FindDexDataUsingAnnotations(method_ref.dex_file, annotation);
  return dex_data != nullptr
      ? dex_data->GetHotnessInfo(method_ref.index)
      : MethodHotness();
}

bool ProfileCompilationInfo::ContainsClass(const DexFile& dex_file,
                                           dex::TypeIndex type_idx,
                                           const ProfileSampleAnnotation& annotation) const {
  const DexFileData* dex_data = FindDexDataUsingAnnotations(&dex_file, annotation);
  return (dex_data != nullptr) && dex_data->ContainsClass(type_idx);
}

uint32_t ProfileCompilationInfo::GetNumberOfMethods() const {
  uint32_t total = 0;
  for (const std::unique_ptr<DexFileData>& dex_data : info_) {
    total += dex_data->method_map.size();
  }
  return total;
}

uint32_t ProfileCompilationInfo::GetNumberOfResolvedClasses() const {
  uint32_t total = 0;
  for (const std::unique_ptr<DexFileData>& dex_data : info_) {
    total += dex_data->class_set.size();
  }
  return total;
}

std::string ProfileCompilationInfo::DumpInfo(const std::vector<const DexFile*>& dex_files,
                                             bool print_full_dex_location) const {
  std::ostringstream os;

  os << "ProfileInfo [";

  for (size_t k = 0; k <  kProfileVersionSize - 1; k++) {
    // Iterate to 'kProfileVersionSize - 1' because the version_ ends with '\0'
    // which we don't want to print.
    os << static_cast<char>(version_[k]);
  }
  os << "]\n";

  if (info_.empty()) {
    os << "-empty-";
    return os.str();
  }

  if (!extra_descriptors_.empty()) {
    os << "\nextra descriptors:";
    for (const std::string& str : extra_descriptors_) {
      os << "\n\t" << str;
    }
    os << "\n";
  }

  const std::string kFirstDexFileKeySubstitute = "!classes.dex";

  for (const std::unique_ptr<DexFileData>& dex_data : info_) {
    os << "\n";
    if (print_full_dex_location) {
      os << dex_data->profile_key;
    } else {
      // Replace the (empty) multidex suffix of the first key with a substitute for easier reading.
      std::string multidex_suffix = DexFileLoader::GetMultiDexSuffix(
          GetBaseKeyFromAugmentedKey(dex_data->profile_key));
      os << (multidex_suffix.empty() ? kFirstDexFileKeySubstitute : multidex_suffix);
    }
    os << " [index=" << static_cast<uint32_t>(dex_data->profile_index) << "]";
    os << " [checksum=" << std::hex << dex_data->checksum << "]" << std::dec;
    os << " [num_type_ids=" << dex_data->num_type_ids << "]";
    os << " [num_method_ids=" << dex_data->num_method_ids << "]";
    const DexFile* dex_file = nullptr;
    for (const DexFile* current : dex_files) {
      if (GetBaseKeyViewFromAugmentedKey(dex_data->profile_key) == current->GetLocation() &&
          dex_data->checksum == current->GetLocationChecksum()) {
        dex_file = current;
      }
    }
    os << "\n\thot methods: ";
    for (const auto& method_it : dex_data->method_map) {
      if (dex_file != nullptr) {
        os << "\n\t\t" << dex_file->PrettyMethod(method_it.first, true);
      } else {
        os << method_it.first;
      }

      os << "[";
      for (const auto& inline_cache_it : method_it.second) {
        os << "{" << std::hex << inline_cache_it.first << std::dec << ":";
        if (inline_cache_it.second.is_missing_types) {
          os << "MT";
        } else if (inline_cache_it.second.is_megamorphic) {
          os << "MM";
        } else {
          const char* separator = "";
          for (dex::TypeIndex type_index : inline_cache_it.second.classes) {
            os << separator << type_index.index_;
            separator = ",";
          }
        }
        os << "}";
      }
      os << "], ";
    }
    bool startup = true;
    while (true) {
      os << "\n\t" << (startup ? "startup methods: " : "post startup methods: ");
      for (uint32_t method_idx = 0; method_idx < dex_data->num_method_ids; ++method_idx) {
        MethodHotness hotness_info(dex_data->GetHotnessInfo(method_idx));
        if (startup ? hotness_info.IsStartup() : hotness_info.IsPostStartup()) {
          if (dex_file != nullptr) {
            os << "\n\t\t" << dex_file->PrettyMethod(method_idx, true);
          } else {
            os << method_idx << ", ";
          }
        }
      }
      if (startup == false) {
        break;
      }
      startup = false;
    }
    os << "\n\tclasses: ";
    for (dex::TypeIndex type_index : dex_data->class_set) {
      if (dex_file != nullptr) {
        os << "\n\t\t" << PrettyDescriptor(GetTypeDescriptor(dex_file, type_index));
      } else {
        os << type_index.index_ << ",";
      }
    }
  }
  return os.str();
}

bool ProfileCompilationInfo::GetClassesAndMethods(
    const DexFile& dex_file,
    /*out*/std::set<dex::TypeIndex>* class_set,
    /*out*/std::set<uint16_t>* hot_method_set,
    /*out*/std::set<uint16_t>* startup_method_set,
    /*out*/std::set<uint16_t>* post_startup_method_method_set,
    const ProfileSampleAnnotation& annotation) const {
  std::set<std::string> ret;
  const DexFileData* dex_data = FindDexDataUsingAnnotations(&dex_file, annotation);
  if (dex_data == nullptr) {
    return false;
  }
  for (const auto& it : dex_data->method_map) {
    hot_method_set->insert(it.first);
  }
  for (uint32_t method_idx = 0; method_idx < dex_data->num_method_ids; ++method_idx) {
    MethodHotness hotness = dex_data->GetHotnessInfo(method_idx);
    if (hotness.IsStartup()) {
      startup_method_set->insert(method_idx);
    }
    if (hotness.IsPostStartup()) {
      post_startup_method_method_set->insert(method_idx);
    }
  }
  for (const dex::TypeIndex& type_index : dex_data->class_set) {
    class_set->insert(type_index);
  }
  return true;
}

bool ProfileCompilationInfo::SameVersion(const ProfileCompilationInfo& other) const {
  return memcmp(version_, other.version_, kProfileVersionSize) == 0;
}

bool ProfileCompilationInfo::Equals(const ProfileCompilationInfo& other) {
  // No need to compare profile_key_map_. That's only a cache for fast search.
  // All the information is already in the info_ vector.
  if (!SameVersion(other)) {
    return false;
  }
  if (info_.size() != other.info_.size()) {
    return false;
  }
  for (size_t i = 0; i < info_.size(); i++) {
    const DexFileData& dex_data = *info_[i];
    const DexFileData& other_dex_data = *other.info_[i];
    if (!(dex_data == other_dex_data)) {
      return false;
    }
  }

  return true;
}

// Naive implementation to generate a random profile file suitable for testing.
bool ProfileCompilationInfo::GenerateTestProfile(int fd,
                                                 uint16_t number_of_dex_files,
                                                 uint16_t method_percentage,
                                                 uint16_t class_percentage,
                                                 uint32_t random_seed) {
  const std::string base_dex_location = "base.apk";
  ProfileCompilationInfo info;
  // The limits are defined by the dex specification.
  const uint16_t max_methods = std::numeric_limits<uint16_t>::max();
  const uint16_t max_classes = std::numeric_limits<uint16_t>::max();
  uint16_t number_of_methods = max_methods * method_percentage / 100;
  uint16_t number_of_classes = max_classes * class_percentage / 100;

  std::srand(random_seed);

  // Make sure we generate more samples with a low index value.
  // This makes it more likely to hit valid method/class indices in small apps.
  const uint16_t kFavorFirstN = 10000;
  const uint16_t kFavorSplit = 2;

  for (uint16_t i = 0; i < number_of_dex_files; i++) {
    std::string dex_location = DexFileLoader::GetMultiDexLocation(i, base_dex_location.c_str());
    std::string profile_key = info.GetProfileDexFileBaseKey(dex_location);

    DexFileData* const data =
        info.GetOrAddDexFileData(profile_key, /*checksum=*/ 0, max_classes, max_methods);
    for (uint16_t m = 0; m < number_of_methods; m++) {
      uint16_t method_idx = rand() % max_methods;
      if (m < (number_of_methods / kFavorSplit)) {
        method_idx %= kFavorFirstN;
      }
      // Alternate between startup and post startup.
      uint32_t flags = MethodHotness::kFlagHot;
      flags |= ((m & 1) != 0) ? MethodHotness::kFlagPostStartup : MethodHotness::kFlagStartup;
      data->AddMethod(static_cast<MethodHotness::Flag>(flags), method_idx);
    }

    for (uint16_t c = 0; c < number_of_classes; c++) {
      uint16_t type_idx = rand() % max_classes;
      if (c < (number_of_classes / kFavorSplit)) {
        type_idx %= kFavorFirstN;
      }
      data->class_set.insert(dex::TypeIndex(type_idx));
    }
  }
  return info.Save(fd);
}

// Naive implementation to generate a random profile file suitable for testing.
// Description of random selection:
// * Select a random starting point S.
// * For every index i, add (S+i) % (N - total number of methods/classes) to profile with the
//   probably of 1/(N - i - number of methods/classes needed to add in profile).
bool ProfileCompilationInfo::GenerateTestProfile(
    int fd,
    std::vector<std::unique_ptr<const DexFile>>& dex_files,
    uint16_t method_percentage,
    uint16_t class_percentage,
    uint32_t random_seed) {
  ProfileCompilationInfo info;
  std::default_random_engine rng(random_seed);
  auto create_shuffled_range = [&rng](uint32_t take, uint32_t out_of) {
    CHECK_LE(take, out_of);
    std::vector<uint32_t> vec(out_of);
    std::iota(vec.begin(), vec.end(), 0u);
    std::shuffle(vec.begin(), vec.end(), rng);
    vec.erase(vec.begin() + take, vec.end());
    std::sort(vec.begin(), vec.end());
    return vec;
  };
  for (std::unique_ptr<const DexFile>& dex_file : dex_files) {
    const std::string& profile_key = dex_file->GetLocation();
    uint32_t checksum = dex_file->GetLocationChecksum();

    uint32_t number_of_classes = dex_file->NumClassDefs();
    uint32_t classes_required_in_profile = (number_of_classes * class_percentage) / 100;

    DexFileData* const data = info.GetOrAddDexFileData(
          profile_key, checksum, dex_file->NumTypeIds(), dex_file->NumMethodIds());
    for (uint32_t class_index : create_shuffled_range(classes_required_in_profile,
                                                      number_of_classes)) {
      data->class_set.insert(dex_file->GetClassDef(class_index).class_idx_);
    }

    uint32_t number_of_methods = dex_file->NumMethodIds();
    uint32_t methods_required_in_profile = (number_of_methods * method_percentage) / 100;
    for (uint32_t method_index : create_shuffled_range(methods_required_in_profile,
                                                       number_of_methods)) {
      // Alternate between startup and post startup.
      uint32_t flags = MethodHotness::kFlagHot;
      flags |= ((method_index & 1) != 0)
                   ? MethodHotness::kFlagPostStartup
                   : MethodHotness::kFlagStartup;
      data->AddMethod(static_cast<MethodHotness::Flag>(flags), method_index);
    }
  }
  return info.Save(fd);
}

bool ProfileCompilationInfo::IsEmpty() const {
  DCHECK_EQ(info_.size(), profile_key_map_.size());
  return info_.empty();
}

ProfileCompilationInfo::InlineCacheMap*
ProfileCompilationInfo::DexFileData::FindOrAddHotMethod(uint16_t method_index) {
  if (method_index >= num_method_ids) {
    LOG(ERROR) << "Invalid method index " << method_index << ". num_method_ids=" << num_method_ids;
    return nullptr;
  }
  return &(method_map.FindOrAdd(
      method_index,
      InlineCacheMap(std::less<uint16_t>(), allocator_->Adapter(kArenaAllocProfile)))->second);
}

// Mark a method as executed at least once.
bool ProfileCompilationInfo::DexFileData::AddMethod(MethodHotness::Flag flags, size_t index) {
  if (index >= num_method_ids || index > kMaxSupportedMethodIndex) {
    LOG(ERROR) << "Invalid method index " << index << ". num_method_ids=" << num_method_ids
        << ", max: " << kMaxSupportedMethodIndex;
    return false;
  }

  SetMethodHotness(index, flags);

  if ((flags & MethodHotness::kFlagHot) != 0) {
    ProfileCompilationInfo::InlineCacheMap* result = FindOrAddHotMethod(index);
    DCHECK(result != nullptr);
  }
  return true;
}

template <typename Fn>
ALWAYS_INLINE void ProfileCompilationInfo::DexFileData::ForMethodBitmapHotnessFlags(Fn fn) const {
  uint32_t lastFlag = is_for_boot_image
      ? MethodHotness::kFlagLastBoot
      : MethodHotness::kFlagLastRegular;
  for (uint32_t flag = MethodHotness::kFlagFirst; flag <= lastFlag; flag = flag << 1) {
    if (flag == MethodHotness::kFlagHot) {
      // There's no bit for hotness in the bitmap.
      // We store the hotness by recording the method in the method list.
      continue;
    }
    fn(enum_cast<MethodHotness::Flag>(flag));
  }
}

void ProfileCompilationInfo::DexFileData::SetMethodHotness(size_t index,
                                                           MethodHotness::Flag flags) {
  DCHECK_LT(index, num_method_ids);
  ForMethodBitmapHotnessFlags([&](MethodHotness::Flag flag) {
    if ((flags & flag) != 0) {
      method_bitmap.StoreBit(MethodFlagBitmapIndex(
          static_cast<MethodHotness::Flag>(flag), index), /*value=*/ true);
    }
  });
}

ProfileCompilationInfo::MethodHotness ProfileCompilationInfo::DexFileData::GetHotnessInfo(
    uint32_t dex_method_index) const {
  MethodHotness ret;
  ForMethodBitmapHotnessFlags([&](MethodHotness::Flag flag) {
    if (method_bitmap.LoadBit(MethodFlagBitmapIndex(
          static_cast<MethodHotness::Flag>(flag), dex_method_index))) {
      ret.AddFlag(static_cast<MethodHotness::Flag>(flag));
    }
  });
  auto it = method_map.find(dex_method_index);
  if (it != method_map.end()) {
    ret.SetInlineCacheMap(&it->second);
    ret.AddFlag(MethodHotness::kFlagHot);
  }
  return ret;
}

// To simplify the implementation we use the MethodHotness flag values as indexes into the internal
// bitmap representation. As such, they should never change unless the profile version is updated
// and the implementation changed accordingly.
static_assert(ProfileCompilationInfo::MethodHotness::kFlagFirst == 1 << 0);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagHot == 1 << 0);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagStartup == 1 << 1);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagPostStartup == 1 << 2);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagLastRegular == 1 << 2);
static_assert(ProfileCompilationInfo::MethodHotness::kFlag32bit == 1 << 3);
static_assert(ProfileCompilationInfo::MethodHotness::kFlag64bit == 1 << 4);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagSensitiveThread == 1 << 5);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagAmStartup == 1 << 6);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagAmPostStartup == 1 << 7);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagBoot == 1 << 8);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagPostBoot == 1 << 9);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagStartupBin == 1 << 10);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagStartupMaxBin == 1 << 15);
static_assert(ProfileCompilationInfo::MethodHotness::kFlagLastBoot == 1 << 15);

size_t ProfileCompilationInfo::DexFileData::MethodFlagBitmapIndex(
      MethodHotness::Flag flag, size_t method_index) const {
  DCHECK_LT(method_index, num_method_ids);
  // The format is [startup bitmap][post startup bitmap][AmStartup][...]
  // This compresses better than ([startup bit][post startup bit])*
  return method_index + FlagBitmapIndex(flag) * num_method_ids;
}

size_t ProfileCompilationInfo::DexFileData::FlagBitmapIndex(MethodHotness::Flag flag) {
  DCHECK(flag != MethodHotness::kFlagHot);
  DCHECK(IsPowerOfTwo(static_cast<uint32_t>(flag)));
  // We arrange the method flags in order, starting with the startup flag.
  // The kFlagHot is not encoded in the bitmap and thus not expected as an
  // argument here. Since all the other flags start at 1 we have to subtract
  // one for the power of 2.
  return WhichPowerOf2(static_cast<uint32_t>(flag)) - 1;
}

uint16_t ProfileCompilationInfo::DexFileData::GetUsedBitmapFlags() const {
  uint32_t used_flags = 0u;
  ForMethodBitmapHotnessFlags([&](MethodHotness::Flag flag) {
    size_t index = FlagBitmapIndex(static_cast<MethodHotness::Flag>(flag));
    if (method_bitmap.HasSomeBitSet(index * num_method_ids, num_method_ids)) {
      used_flags |= flag;
    }
  });
  return dchecked_integral_cast<uint16_t>(used_flags);
}

ProfileCompilationInfo::DexPcData*
ProfileCompilationInfo::FindOrAddDexPc(InlineCacheMap* inline_cache, uint32_t dex_pc) {
  return &(inline_cache->FindOrAdd(dex_pc, DexPcData(inline_cache->get_allocator()))->second);
}

HashSet<std::string> ProfileCompilationInfo::GetClassDescriptors(
    const std::vector<const DexFile*>& dex_files,
    const ProfileSampleAnnotation& annotation) {
  HashSet<std::string> ret;
  for (const DexFile* dex_file : dex_files) {
    const DexFileData* data = FindDexDataUsingAnnotations(dex_file, annotation);
    if (data != nullptr) {
      for (dex::TypeIndex type_idx : data->class_set) {
        ret.insert(GetTypeDescriptor(dex_file, type_idx));
      }
    } else {
      VLOG(compiler) << "Failed to find profile data for " << dex_file->GetLocation();
    }
  }
  return ret;
}

bool ProfileCompilationInfo::IsProfileFile(int fd) {
  // First check if it's an empty file as we allow empty profile files.
  // Profiles may be created by ActivityManager or installd before we manage to
  // process them in the runtime or profman.
  struct stat stat_buffer;
  if (fstat(fd, &stat_buffer) != 0) {
    return false;
  }

  if (stat_buffer.st_size == 0) {
    return true;
  }

  // The files is not empty. Check if it contains the profile magic.
  size_t byte_count = sizeof(kProfileMagic);
  uint8_t buffer[sizeof(kProfileMagic)];
  if (!android::base::ReadFullyAtOffset(fd, buffer, byte_count, /*offset=*/ 0)) {
    return false;
  }

  // Reset the offset to prepare the file for reading.
  off_t rc =  TEMP_FAILURE_RETRY(lseek(fd, 0, SEEK_SET));
  if (rc == static_cast<off_t>(-1)) {
    PLOG(ERROR) << "Failed to reset the offset";
    return false;
  }

  return memcmp(buffer, kProfileMagic, byte_count) == 0;
}

bool ProfileCompilationInfo::UpdateProfileKeys(
      const std::vector<std::unique_ptr<const DexFile>>& dex_files) {
  for (const std::unique_ptr<const DexFile>& dex_file : dex_files) {
    for (const std::unique_ptr<DexFileData>& dex_data : info_) {
      if (dex_data->checksum == dex_file->GetLocationChecksum() &&
          dex_data->num_type_ids == dex_file->NumTypeIds() &&
          dex_data->num_method_ids == dex_file->NumMethodIds()) {
        std::string new_profile_key = GetProfileDexFileBaseKey(dex_file->GetLocation());
        std::string dex_data_base_key = GetBaseKeyFromAugmentedKey(dex_data->profile_key);
        if (dex_data_base_key != new_profile_key) {
          if (profile_key_map_.find(new_profile_key) != profile_key_map_.end()) {
            // We can't update the key if the new key belongs to a different dex file.
            LOG(ERROR) << "Cannot update profile key to " << new_profile_key
                << " because the new key belongs to another dex file.";
            return false;
          }
          profile_key_map_.erase(dex_data->profile_key);
          // Retain the annotation (if any) during the renaming by re-attaching the info
          // form the old key.
          dex_data->profile_key = MigrateAnnotationInfo(new_profile_key, dex_data->profile_key);
          profile_key_map_.Put(dex_data->profile_key, dex_data->profile_index);
        }
      }
    }
  }
  return true;
}

bool ProfileCompilationInfo::ProfileFilterFnAcceptAll(
    const std::string& dex_location ATTRIBUTE_UNUSED,
    uint32_t checksum ATTRIBUTE_UNUSED) {
  return true;
}

void ProfileCompilationInfo::ClearData() {
  profile_key_map_.clear();
  info_.clear();
  extra_descriptors_indexes_.clear();
  extra_descriptors_.clear();
}

void ProfileCompilationInfo::ClearDataAndAdjustVersion(bool for_boot_image) {
  ClearData();
  memcpy(version_,
         for_boot_image ? kProfileVersionForBootImage : kProfileVersion,
         kProfileVersionSize);
}

bool ProfileCompilationInfo::IsForBootImage() const {
  return memcmp(version_, kProfileVersionForBootImage, sizeof(kProfileVersionForBootImage)) == 0;
}

const uint8_t* ProfileCompilationInfo::GetVersion() const {
  return version_;
}

bool ProfileCompilationInfo::DexFileData::ContainsClass(dex::TypeIndex type_index) const {
  return class_set.find(type_index) != class_set.end();
}

uint32_t ProfileCompilationInfo::DexFileData::ClassesDataSize() const {
  return class_set.empty()
      ? 0u
      : sizeof(ProfileIndexType) +            // Which dex file.
        sizeof(uint16_t) +                    // Number of classes.
        sizeof(uint16_t) * class_set.size();  // Type index diffs.
}

void ProfileCompilationInfo::DexFileData::WriteClasses(SafeBuffer& buffer) const {
  if (class_set.empty()) {
    return;
  }
  buffer.WriteUintAndAdvance(profile_index);
  buffer.WriteUintAndAdvance(dchecked_integral_cast<uint16_t>(class_set.size()));
  WriteClassSet(buffer, class_set);
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::DexFileData::ReadClasses(
    SafeBuffer& buffer,
    const dchecked_vector<ExtraDescriptorIndex>& extra_descriptors_remap,
    std::string* error) {
  uint16_t classes_size;
  if (!buffer.ReadUintAndAdvance(&classes_size)) {
    *error = "Error reading classes size.";
    return ProfileLoadStatus::kBadData;
  }
  uint16_t num_valid_type_indexes = dchecked_integral_cast<uint16_t>(
      std::min<size_t>(num_type_ids + extra_descriptors_remap.size(), DexFile::kDexNoIndex16));
  uint16_t type_index = 0u;
  for (size_t i = 0; i != classes_size; ++i) {
    uint16_t type_index_diff;
    if (!buffer.ReadUintAndAdvance(&type_index_diff)) {
      *error = "Error reading class type index diff.";
      return ProfileLoadStatus::kBadData;
    }
    if (type_index_diff == 0u && i != 0u) {
      *error = "Duplicate type index.";
      return ProfileLoadStatus::kBadData;
    }
    if (type_index_diff >= num_valid_type_indexes - type_index) {
      *error = "Invalid type index.";
      return ProfileLoadStatus::kBadData;
    }
    type_index += type_index_diff;
    if (type_index >= num_type_ids) {
      uint32_t new_extra_descriptor_index = extra_descriptors_remap[type_index - num_type_ids];
      if (new_extra_descriptor_index >= DexFile::kDexNoIndex16 - num_type_ids) {
        *error = "Remapped type index out of range.";
        return ProfileLoadStatus::kMergeError;
      }
      type_index = num_type_ids + new_extra_descriptor_index;
    }
    class_set.insert(dex::TypeIndex(type_index));
  }
  return ProfileLoadStatus::kSuccess;
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::DexFileData::SkipClasses(
    SafeBuffer& buffer,
    std::string* error) {
  uint16_t classes_size;
  if (!buffer.ReadUintAndAdvance(&classes_size)) {
    *error = "Error reading classes size to skip.";
    return ProfileLoadStatus::kBadData;
  }
  size_t following_data_size = static_cast<size_t>(classes_size) * sizeof(uint16_t);
  if (following_data_size > buffer.GetAvailableBytes()) {
    *error = "Classes data size to skip exceeds remaining data.";
    return ProfileLoadStatus::kBadData;
  }
  buffer.Advance(following_data_size);
  return ProfileLoadStatus::kSuccess;
}

uint32_t ProfileCompilationInfo::DexFileData::MethodsDataSize(
    /*out*/ uint16_t* method_flags,
    /*out*/ size_t* saved_bitmap_bit_size) const {
  uint16_t local_method_flags = GetUsedBitmapFlags();
  size_t local_saved_bitmap_bit_size = POPCOUNT(local_method_flags) * num_method_ids;
  if (!method_map.empty()) {
    local_method_flags |= enum_cast<uint16_t>(MethodHotness::kFlagHot);
  }
  size_t size = 0u;
  if (local_method_flags != 0u) {
    size_t num_hot_methods = method_map.size();
    size_t num_dex_pc_entries = 0u;
    size_t num_class_entries = 0u;
    for (const auto& method_entry : method_map) {
      const InlineCacheMap& inline_cache_map = method_entry.second;
      num_dex_pc_entries += inline_cache_map.size();
      for (const auto& inline_cache_entry : inline_cache_map) {
        const DexPcData& dex_pc_data = inline_cache_entry.second;
        num_class_entries += dex_pc_data.classes.size();
      }
    }

    constexpr size_t kPerHotMethodSize =
        sizeof(uint16_t) +  // Method index diff.
        sizeof(uint16_t);   // Inline cache size.
    constexpr size_t kPerDexPcEntrySize =
        sizeof(uint16_t) +  // Dex PC.
        sizeof(uint8_t);    // Number of inline cache classes.
    constexpr size_t kPerClassEntrySize =
        sizeof(uint16_t);   // Type index diff.

    size_t saved_bitmap_byte_size = BitsToBytesRoundUp(local_saved_bitmap_bit_size);
    size = sizeof(ProfileIndexType) +                 // Which dex file.
           sizeof(uint32_t) +                         // Total size of following data.
           sizeof(uint16_t) +                         // Method flags.
           saved_bitmap_byte_size +                   // Bitmap data.
           num_hot_methods * kPerHotMethodSize +      // Data for hot methods.
           num_dex_pc_entries * kPerDexPcEntrySize +  // Data for dex pc entries.
           num_class_entries * kPerClassEntrySize;    // Data for inline cache class entries.
  }
  if (method_flags != nullptr) {
    *method_flags = local_method_flags;
  }
  if (saved_bitmap_bit_size != nullptr) {
    *saved_bitmap_bit_size = local_saved_bitmap_bit_size;
  }
  return size;
}

void ProfileCompilationInfo::DexFileData::WriteMethods(SafeBuffer& buffer) const {
  uint16_t method_flags;
  size_t saved_bitmap_bit_size;
  uint32_t methods_data_size = MethodsDataSize(&method_flags, &saved_bitmap_bit_size);
  if (methods_data_size == 0u) {
    return;  // No data to write.
  }
  DCHECK_GE(buffer.GetAvailableBytes(), methods_data_size);
  uint32_t expected_available_bytes_at_end = buffer.GetAvailableBytes() - methods_data_size;

  // Write the profile index.
  buffer.WriteUintAndAdvance(profile_index);
  // Write the total size of the following methods data (without the profile index
  // and the total size itself) for easy skipping when the dex file is filtered out.
  uint32_t following_data_size = methods_data_size - sizeof(ProfileIndexType) - sizeof(uint32_t);
  buffer.WriteUintAndAdvance(following_data_size);
  // Write the used method flags.
  buffer.WriteUintAndAdvance(method_flags);

  // Write the bitmap data.
  size_t saved_bitmap_byte_size = BitsToBytesRoundUp(saved_bitmap_bit_size);
  DCHECK_LE(saved_bitmap_byte_size, buffer.GetAvailableBytes());
  BitMemoryRegion saved_bitmap(buffer.GetCurrentPtr(), /*bit_start=*/ 0, saved_bitmap_bit_size);
  size_t saved_bitmap_index = 0u;
  ForMethodBitmapHotnessFlags([&](MethodHotness::Flag flag) {
    if ((method_flags & flag) != 0u) {
      size_t index = FlagBitmapIndex(static_cast<MethodHotness::Flag>(flag));
      BitMemoryRegion src = method_bitmap.Subregion(index * num_method_ids, num_method_ids);
      saved_bitmap.StoreBits(saved_bitmap_index * num_method_ids, src, num_method_ids);
      ++saved_bitmap_index;
    }
  });
  DCHECK_EQ(saved_bitmap_index * num_method_ids, saved_bitmap_bit_size);
  buffer.Advance(saved_bitmap_byte_size);

  uint16_t last_method_index = 0;
  for (const auto& method_entry : method_map) {
    uint16_t method_index = method_entry.first;
    const InlineCacheMap& inline_cache_map = method_entry.second;

    // Store the difference between the method indices for better compression.
    // The SafeMap is ordered by method_id, so the difference will always be non negative.
    DCHECK_GE(method_index, last_method_index);
    uint16_t diff_with_last_method_index = method_index - last_method_index;
    last_method_index = method_index;
    buffer.WriteUintAndAdvance(diff_with_last_method_index);

    // Add inline cache map size.
    buffer.WriteUintAndAdvance(dchecked_integral_cast<uint16_t>(inline_cache_map.size()));

    // Add inline cache entries.
    for (const auto& inline_cache_entry : inline_cache_map) {
      uint16_t dex_pc = inline_cache_entry.first;
      const DexPcData& dex_pc_data = inline_cache_entry.second;
      const ArenaSet<dex::TypeIndex>& classes = dex_pc_data.classes;

      // Add the dex pc.
      buffer.WriteUintAndAdvance(dex_pc);

      // Add the megamorphic/missing_types encoding if needed and continue.
      // In either cases we don't add any classes to the profiles and so there's
      // no point to continue.
      // TODO: in case we miss types there is still value to add the rest of the
      // classes. (This requires changing profile version or using a new section type.)
      if (dex_pc_data.is_missing_types) {
        // At this point the megamorphic flag should not be set.
        DCHECK(!dex_pc_data.is_megamorphic);
        DCHECK_EQ(classes.size(), 0u);
        buffer.WriteUintAndAdvance(kIsMissingTypesEncoding);
        continue;
      } else if (dex_pc_data.is_megamorphic) {
        DCHECK_EQ(classes.size(), 0u);
        buffer.WriteUintAndAdvance(kIsMegamorphicEncoding);
        continue;
      }

      DCHECK_LT(classes.size(), ProfileCompilationInfo::kIndividualInlineCacheSize);
      DCHECK_NE(classes.size(), 0u) << "InlineCache contains a dex_pc with 0 classes";

      // Add the number of classes for the dex PC.
      buffer.WriteUintAndAdvance(dchecked_integral_cast<uint8_t>(classes.size()));
      // Store the class set.
      WriteClassSet(buffer, classes);
    }
  }

  // Check if we've written the right number of bytes.
  DCHECK_EQ(buffer.GetAvailableBytes(), expected_available_bytes_at_end);
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::DexFileData::ReadMethods(
    SafeBuffer& buffer,
    const dchecked_vector<ExtraDescriptorIndex>& extra_descriptors_remap,
    std::string* error) {
  uint32_t following_data_size;
  if (!buffer.ReadUintAndAdvance(&following_data_size)) {
    *error = "Error reading methods data size.";
    return ProfileLoadStatus::kBadData;
  }
  if (following_data_size > buffer.GetAvailableBytes()) {
    *error = "Methods data size exceeds available data size.";
    return ProfileLoadStatus::kBadData;
  }
  uint32_t expected_available_bytes_at_end = buffer.GetAvailableBytes() - following_data_size;

  // Read method flags.
  uint16_t method_flags;
  if (!buffer.ReadUintAndAdvance(&method_flags)) {
    *error = "Error reading method flags.";
    return ProfileLoadStatus::kBadData;
  }
  if (!is_for_boot_image && method_flags >= (MethodHotness::kFlagLastRegular << 1)) {
    // The profile we're loading contains data for boot image.
    *error = "Method flags contain boot image profile flags for non-boot image profile.";
    return ProfileLoadStatus::kBadData;
  }

  // Read method bitmap.
  size_t saved_bitmap_bit_size = POPCOUNT(method_flags & ~MethodHotness::kFlagHot) * num_method_ids;
  size_t saved_bitmap_byte_size = BitsToBytesRoundUp(saved_bitmap_bit_size);
  if (sizeof(uint16_t) + saved_bitmap_byte_size > following_data_size) {
    *error = "Insufficient available data for method bitmap.";
    return ProfileLoadStatus::kBadData;
  }
  BitMemoryRegion saved_bitmap(buffer.GetCurrentPtr(), /*bit_start=*/ 0, saved_bitmap_bit_size);
  size_t saved_bitmap_index = 0u;
  ForMethodBitmapHotnessFlags([&](MethodHotness::Flag flag) {
    if ((method_flags & flag) != 0u) {
      size_t index = FlagBitmapIndex(static_cast<MethodHotness::Flag>(flag));
      BitMemoryRegion src =
          saved_bitmap.Subregion(saved_bitmap_index * num_method_ids, num_method_ids);
      method_bitmap.OrBits(index * num_method_ids, src, num_method_ids);
      ++saved_bitmap_index;
    }
  });
  buffer.Advance(saved_bitmap_byte_size);

  // Load hot methods.
  if ((method_flags & MethodHotness::kFlagHot) != 0u) {
    uint32_t num_valid_method_indexes =
        std::min<uint32_t>(kMaxSupportedMethodIndex + 1u, num_method_ids);
    uint16_t num_valid_type_indexes = dchecked_integral_cast<uint16_t>(
        std::min<size_t>(num_type_ids + extra_descriptors_remap.size(), DexFile::kDexNoIndex16));
    uint16_t method_index = 0;
    bool first_diff = true;
    while (buffer.GetAvailableBytes() > expected_available_bytes_at_end) {
      uint16_t diff_with_last_method_index;
      if (!buffer.ReadUintAndAdvance(&diff_with_last_method_index)) {
        *error = "Error reading method index diff.";
        return ProfileLoadStatus::kBadData;
      }
      if (diff_with_last_method_index == 0u && !first_diff) {
        *error = "Duplicate method index.";
        return ProfileLoadStatus::kBadData;
      }
      first_diff = false;
      if (diff_with_last_method_index >= num_valid_method_indexes - method_index) {
        *error = "Invalid method index.";
        return ProfileLoadStatus::kBadData;
      }
      method_index += diff_with_last_method_index;
      InlineCacheMap* inline_cache = FindOrAddHotMethod(method_index);
      DCHECK(inline_cache != nullptr);

      // Load inline cache map size.
      uint16_t inline_cache_size;
      if (!buffer.ReadUintAndAdvance(&inline_cache_size)) {
        *error = "Error reading inline cache size.";
        return ProfileLoadStatus::kBadData;
      }
      for (uint16_t ic_index = 0; ic_index != inline_cache_size; ++ic_index) {
        // Load dex pc.
        uint16_t dex_pc;
        if (!buffer.ReadUintAndAdvance(&dex_pc)) {
          *error = "Error reading inline cache dex pc.";
          return ProfileLoadStatus::kBadData;
        }
        DexPcData* dex_pc_data = FindOrAddDexPc(inline_cache, dex_pc);
        DCHECK(dex_pc_data != nullptr);

        // Load inline cache classes.
        uint8_t inline_cache_classes_size;
        if (!buffer.ReadUintAndAdvance(&inline_cache_classes_size)) {
          *error = "Error reading inline cache classes size.";
          return ProfileLoadStatus::kBadData;
        }
        if (inline_cache_classes_size == kIsMissingTypesEncoding) {
          dex_pc_data->SetIsMissingTypes();
        } else if (inline_cache_classes_size == kIsMegamorphicEncoding) {
          dex_pc_data->SetIsMegamorphic();
        } else if (inline_cache_classes_size >= kIndividualInlineCacheSize) {
          *error = "Inline cache size too large.";
          return ProfileLoadStatus::kBadData;
        } else {
          uint16_t type_index = 0u;
          for (size_t i = 0; i != inline_cache_classes_size; ++i) {
            uint16_t type_index_diff;
            if (!buffer.ReadUintAndAdvance(&type_index_diff)) {
              *error = "Error reading inline cache type index diff.";
              return ProfileLoadStatus::kBadData;
            }
            if (type_index_diff == 0u && i != 0u) {
              *error = "Duplicate inline cache type index.";
              return ProfileLoadStatus::kBadData;
            }
            if (type_index_diff >= num_valid_type_indexes - type_index) {
              *error = "Invalid inline cache type index.";
              return ProfileLoadStatus::kBadData;
            }
            type_index += type_index_diff;
            if (type_index >= num_type_ids) {
              ExtraDescriptorIndex new_extra_descriptor_index =
                  extra_descriptors_remap[type_index - num_type_ids];
              if (new_extra_descriptor_index >= DexFile::kDexNoIndex16 - num_type_ids) {
                *error = "Remapped inline cache type index out of range.";
                return ProfileLoadStatus::kMergeError;
              }
              type_index = num_type_ids + new_extra_descriptor_index;
            }
            dex_pc_data->AddClass(dex::TypeIndex(type_index));
          }
        }
      }
    }
  }

  if (buffer.GetAvailableBytes() != expected_available_bytes_at_end) {
    *error = "Methods data did not end at expected position.";
    return ProfileLoadStatus::kBadData;
  }

  return ProfileLoadStatus::kSuccess;
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::DexFileData::SkipMethods(
    SafeBuffer& buffer,
    std::string* error) {
  uint32_t following_data_size;
  if (!buffer.ReadUintAndAdvance(&following_data_size)) {
    *error = "Error reading methods data size to skip.";
    return ProfileLoadStatus::kBadData;
  }
  if (following_data_size > buffer.GetAvailableBytes()) {
    *error = "Methods data size to skip exceeds remaining data.";
    return ProfileLoadStatus::kBadData;
  }
  buffer.Advance(following_data_size);
  return ProfileLoadStatus::kSuccess;
}

void ProfileCompilationInfo::DexFileData::WriteClassSet(
    SafeBuffer& buffer,
    const ArenaSet<dex::TypeIndex>& class_set) {
  // Store the difference between the type indexes for better compression.
  uint16_t last_type_index = 0u;
  for (const dex::TypeIndex& type_index : class_set) {
    DCHECK_GE(type_index.index_, last_type_index);
    uint16_t diff_with_last_type_index = type_index.index_ - last_type_index;
    last_type_index = type_index.index_;
    buffer.WriteUintAndAdvance(diff_with_last_type_index);
  }
}

size_t ProfileCompilationInfo::GetSizeWarningThresholdBytes() const {
  return IsForBootImage() ?  kSizeWarningThresholdBootBytes : kSizeWarningThresholdBytes;
}

size_t ProfileCompilationInfo::GetSizeErrorThresholdBytes() const {
  return IsForBootImage() ?  kSizeErrorThresholdBootBytes : kSizeErrorThresholdBytes;
}

std::ostream& operator<<(std::ostream& stream,
                         ProfileCompilationInfo::DexReferenceDumper dumper) {
  stream << "[profile_key=" << dumper.GetProfileKey()
         << ",dex_checksum=" << std::hex << dumper.GetDexChecksum() << std::dec
         << ",num_type_ids=" << dumper.GetNumTypeIds()
         << ",num_method_ids=" << dumper.GetNumMethodIds()
         << "]";
  return stream;
}

FlattenProfileData::FlattenProfileData() :
    max_aggregation_for_methods_(0),
    max_aggregation_for_classes_(0) {
}

FlattenProfileData::ItemMetadata::ItemMetadata() :
    flags_(0) {
}

FlattenProfileData::ItemMetadata::ItemMetadata(const ItemMetadata& other) :
    flags_(other.flags_),
    annotations_(other.annotations_) {
}

std::unique_ptr<FlattenProfileData> ProfileCompilationInfo::ExtractProfileData(
    const std::vector<std::unique_ptr<const DexFile>>& dex_files) const {

  std::unique_ptr<FlattenProfileData> result(new FlattenProfileData());

  auto create_metadata_fn = []() { return FlattenProfileData::ItemMetadata(); };

  // Iterate through all the dex files, find the methods/classes associated with each of them,
  // and add them to the flatten result.
  for (const std::unique_ptr<const DexFile>& dex_file : dex_files) {
    // Find all the dex data for the given dex file.
    // We may have multiple dex data if the methods or classes were added using
    // different annotations.
    std::vector<const DexFileData*> all_dex_data;
    FindAllDexData(dex_file.get(), &all_dex_data);
    for (const DexFileData* dex_data : all_dex_data) {
      // Extract the annotation from the key as we want to store it in the flatten result.
      ProfileSampleAnnotation annotation = GetAnnotationFromKey(dex_data->profile_key);

      // Check which methods from the current dex files are in the profile.
      for (uint32_t method_idx = 0; method_idx < dex_data->num_method_ids; ++method_idx) {
        MethodHotness hotness = dex_data->GetHotnessInfo(method_idx);
        if (!hotness.IsInProfile()) {
          // Not in the profile, continue.
          continue;
        }
        // The method is in the profile, create metadata item for it and added to the result.
        MethodReference ref(dex_file.get(), method_idx);
        FlattenProfileData::ItemMetadata& metadata =
            result->method_metadata_.GetOrCreate(ref, create_metadata_fn);
        metadata.flags_ |= hotness.flags_;
        metadata.annotations_.push_back(annotation);
        // Update the max aggregation counter for methods.
        // This is essentially a cache, to avoid traversing all the methods just to find out
        // this value.
        result->max_aggregation_for_methods_ = std::max(
            result->max_aggregation_for_methods_,
            static_cast<uint32_t>(metadata.annotations_.size()));
      }

      // Check which classes from the current dex files are in the profile.
      for (const dex::TypeIndex& type_index : dex_data->class_set) {
        if (type_index.index_ >= dex_file->NumTypeIds()) {
          // Not a valid `dex::TypeIndex` for `TypeReference`.
          // TODO: Rewrite the API to use descriptors or the `ProfileCompilationInfo` directly
          // instead of the `FlattenProfileData` helper class.
          continue;
        }
        TypeReference ref(dex_file.get(), type_index);
        FlattenProfileData::ItemMetadata& metadata =
            result->class_metadata_.GetOrCreate(ref, create_metadata_fn);
        metadata.annotations_.push_back(annotation);
        // Update the max aggregation counter for classes.
        result->max_aggregation_for_classes_ = std::max(
            result->max_aggregation_for_classes_,
            static_cast<uint32_t>(metadata.annotations_.size()));
      }
    }
  }

  return result;
}

void FlattenProfileData::MergeData(const FlattenProfileData& other) {
  auto create_metadata_fn = []() { return FlattenProfileData::ItemMetadata(); };
  for (const auto& it : other.method_metadata_) {
    const MethodReference& otherRef = it.first;
    const FlattenProfileData::ItemMetadata otherData = it.second;
    const std::list<ProfileCompilationInfo::ProfileSampleAnnotation>& other_annotations =
        otherData.GetAnnotations();

    FlattenProfileData::ItemMetadata& metadata =
        method_metadata_.GetOrCreate(otherRef, create_metadata_fn);
    metadata.flags_ |= otherData.GetFlags();
    metadata.annotations_.insert(
        metadata.annotations_.end(), other_annotations.begin(), other_annotations.end());

    max_aggregation_for_methods_ = std::max(
          max_aggregation_for_methods_,
          static_cast<uint32_t>(metadata.annotations_.size()));
  }
  for (const auto& it : other.class_metadata_) {
    const TypeReference& otherRef = it.first;
    const FlattenProfileData::ItemMetadata otherData = it.second;
    const std::list<ProfileCompilationInfo::ProfileSampleAnnotation>& other_annotations =
        otherData.GetAnnotations();

    FlattenProfileData::ItemMetadata& metadata =
        class_metadata_.GetOrCreate(otherRef, create_metadata_fn);
    metadata.flags_ |= otherData.GetFlags();
    metadata.annotations_.insert(
        metadata.annotations_.end(), other_annotations.begin(), other_annotations.end());

    max_aggregation_for_classes_ = std::max(
          max_aggregation_for_classes_,
          static_cast<uint32_t>(metadata.annotations_.size()));
  }
}

}  // namespace art
