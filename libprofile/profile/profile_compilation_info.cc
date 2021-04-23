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
#include "dex/dex_file_loader.h"

namespace art {

const uint8_t ProfileCompilationInfo::kProfileMagic[] = { 'p', 'r', 'o', '\0' };
// Last profile version: merge profiles directly from the file without creating
// profile_compilation_info object. All the profile line headers are now placed together
// before corresponding method_encodings and class_ids.
const uint8_t ProfileCompilationInfo::kProfileVersion[] = { '0', '1', '0', '\0' };
const uint8_t ProfileCompilationInfo::kProfileVersionForBootImage[] = { '0', '1', '2', '\0' };

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

static constexpr uint16_t kMaxDexFileKeyLength = PATH_MAX;

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

  /**
   * Read bytes from this source.
   * Reading will advance the current source position so subsequent
   * invocations will read from the las position.
   */
  ProfileLoadStatus Read(uint8_t* buffer,
                         size_t byte_count,
                         const std::string& debug_stage,
                         std::string* error);

  /** Return true if the source has 0 data. */
  bool HasEmptyContent() const;

  /** Return true if all the information from this source has been read. */
  bool HasConsumedAllData() const;

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
class ProfileCompilationInfo::SafeBuffer {
 public:
  explicit SafeBuffer(size_t size)
      : storage_(new uint8_t[size]),
        ptr_current_(storage_.get()),
        ptr_end_(ptr_current_ + size) {}

  // Reads the content of the descriptor at the current position.
  ProfileLoadStatus Fill(ProfileSource& source,
                         const std::string& debug_stage,
                         /*out*/std::string* error) {
    size_t byte_count = (ptr_end_ - ptr_current_) * sizeof(*ptr_current_);
    uint8_t* buffer = ptr_current_;
    return source.Read(buffer, byte_count, debug_stage, error);
  }

  // Reads an uint value and advances the current pointer.
  template <typename T>
  bool ReadUintAndAdvance(/*out*/ T* value) {
    static_assert(std::is_unsigned<T>::value, "Type is not unsigned");
    if (sizeof(T) > CountUnreadBytes()) {
      return false;
    }
    *value = 0;
    for (size_t i = 0; i < sizeof(T); i++) {
      *value += ptr_current_[i] << (i * kBitsPerByte);
    }
    ptr_current_ += sizeof(T);
    return true;
  }

  // Compares the given data with the content at the current pointer.
  // If the contents are equal it advances the current pointer by data_size.
  bool CompareAndAdvance(const uint8_t* data, size_t data_size) {
    if (data_size > CountUnreadBytes()) {
      return false;
    }
    if (memcmp(ptr_current_, data, data_size) == 0) {
      ptr_current_ += data_size;
      return true;
    }
    return false;
  }

  // Advances current pointer by data_size.
  void Advance(size_t data_size) {
    DCHECK_LE(data_size, CountUnreadBytes());
    ptr_current_ += data_size;
  }

  // Returns the count of unread bytes.
  size_t CountUnreadBytes() {
    DCHECK_LE(static_cast<void*>(ptr_current_), static_cast<void*>(ptr_end_));
    return (ptr_end_ - ptr_current_) * sizeof(*ptr_current_);
  }

  // Returns the current pointer.
  const uint8_t* GetCurrentPtr() {
    return ptr_current_;
  }

  // Get the underlying raw buffer.
  uint8_t* Get() {
    return storage_.get();
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
      profile_key_map_(std::less<const std::string_view>(),
                       allocator_.Adapter(kArenaAllocProfile)) {
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

void ProfileCompilationInfo::DexPcData::AddClass(uint16_t dex_profile_idx,
                                                 const dex::TypeIndex& type_idx) {
  if (is_megamorphic || is_missing_types) {
    return;
  }

  // Perform an explicit lookup for the type instead of directly emplacing the
  // element. We do this because emplace() allocates the node before doing the
  // lookup and if it then finds an identical element, it shall deallocate the
  // node. For Arena allocations, that's essentially a leak.
  ClassReference ref(dex_profile_idx, type_idx);
  auto it = classes.find(ref);
  if (it != classes.end()) {
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
  classes.insert(ref);
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
      ((status == ProfileLoadStatus::kVersionMismatch) ||
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
static bool WriteBuffer(int fd, const uint8_t* buffer, size_t byte_count) {
  while (byte_count > 0) {
    int bytes_written = TEMP_FAILURE_RETRY(write(fd, buffer, byte_count));
    if (bytes_written == -1) {
      return false;
    }
    byte_count -= bytes_written;  // Reduce the number of remaining bytes.
    buffer += bytes_written;  // Move the buffer forward.
  }
  return true;
}

// Add the string bytes to the buffer.
static void AddStringToBuffer(std::vector<uint8_t>* buffer, const std::string& value) {
  buffer->insert(buffer->end(), value.begin(), value.end());
}

// Insert each byte, from low to high into the buffer.
template <typename T>
static void AddUintToBuffer(std::vector<uint8_t>* buffer, T value) {
  for (size_t i = 0; i < sizeof(T); i++) {
    buffer->push_back((value >> (i * kBitsPerByte)) & 0xff);
  }
}

static constexpr size_t kLineHeaderSize =
    2 * sizeof(uint16_t) +  // class_set.size + dex_location.size
    3 * sizeof(uint32_t);   // method_map.size + checksum + num_method_ids

/**
 * Serialization format:
 * [profile_header, zipped[[profile_line_header1, profile_line_header2...],[profile_line_data1,
 *    profile_line_data2...]]
 * profile_header:
 *   magic,version,number_of_dex_files,uncompressed_size_of_zipped_data,compressed_data_size
 * profile_line_header:
 *   profile_key,number_of_classes,methods_region_size,dex_location_checksum,num_method_ids
 * profile_line_data:
 *   method_encoding_1,method_encoding_2...,class_id1,class_id2...,method_flags bitmap,
 * The method_encoding is:
 *    method_id,number_of_inline_caches,inline_cache1,inline_cache2...
 * The inline_cache is:
 *    dex_pc,[M|dex_map_size], dex_profile_index,class_id1,class_id2...,dex_profile_index2,...
 *    dex_map_size is the number of dex_indeces that follows.
 *       Classes are grouped per their dex files and the line
 *       `dex_profile_index,class_id1,class_id2...,dex_profile_index2,...` encodes the
 *       mapping from `dex_profile_index` to the set of classes `class_id1,class_id2...`
 *    M stands for megamorphic or missing types and it's encoded as either
 *    the byte kIsMegamorphicEncoding or kIsMissingTypesEncoding.
 *    When present, there will be no class ids following.
 **/
bool ProfileCompilationInfo::Save(int fd) {
  uint64_t start = NanoTime();
  ScopedTrace trace(__PRETTY_FUNCTION__);
  DCHECK_GE(fd, 0);

  // Use a vector wrapper to avoid keeping track of offsets when we add elements.
  std::vector<uint8_t> buffer;
  if (!WriteBuffer(fd, kProfileMagic, sizeof(kProfileMagic))) {
    return false;
  }
  if (!WriteBuffer(fd, version_, sizeof(version_))) {
    return false;
  }

  DCHECK_LE(info_.size(), MaxProfileIndex());
  WriteProfileIndex(&buffer, static_cast<ProfileIndexType>(info_.size()));

  uint32_t required_capacity = 0;
  for (const std::unique_ptr<DexFileData>& dex_data_ptr : info_) {
    const DexFileData& dex_data = *dex_data_ptr;
    uint32_t methods_region_size = GetMethodsRegionSize(dex_data);
    required_capacity += kLineHeaderSize +
        dex_data.profile_key.size() +
        sizeof(uint16_t) * dex_data.class_set.size() +
        methods_region_size +
        dex_data.bitmap_storage.size();
  }
  // Allow large profiles for non target builds for the case where we are merging many profiles
  // to generate a boot image profile.
  VLOG(profiler) << "Required capacity: " << required_capacity << " bytes.";
  if (required_capacity > GetSizeErrorThresholdBytes()) {
    LOG(ERROR) << "Profile data size exceeds "
               << GetSizeErrorThresholdBytes()
               << " bytes. Profile will not be written to disk."
               << " It requires " << required_capacity << " bytes.";
    return false;
  }
  AddUintToBuffer(&buffer, required_capacity);
  if (!WriteBuffer(fd, buffer.data(), buffer.size())) {
    return false;
  }
  // Make sure that the buffer has enough capacity to avoid repeated resizings
  // while we add data.
  buffer.reserve(required_capacity);
  buffer.clear();

  // Dex files must be written in the order of their profile index. This
  // avoids writing the index in the output file and simplifies the parsing logic.
  // Write profile line headers.
  for (const std::unique_ptr<DexFileData>& dex_data_ptr : info_) {
    const DexFileData& dex_data = *dex_data_ptr;

    if (dex_data.profile_key.size() >= kMaxDexFileKeyLength) {
      LOG(WARNING) << "DexFileKey exceeds allocated limit";
      return false;
    }

    uint32_t methods_region_size = GetMethodsRegionSize(dex_data);

    DCHECK_LE(dex_data.profile_key.size(), std::numeric_limits<uint16_t>::max());
    DCHECK_LE(dex_data.class_set.size(), std::numeric_limits<uint16_t>::max());
    // Write profile line header.
    AddUintToBuffer(&buffer, static_cast<uint16_t>(dex_data.profile_key.size()));
    AddUintToBuffer(&buffer, static_cast<uint16_t>(dex_data.class_set.size()));
    AddUintToBuffer(&buffer, methods_region_size);  // uint32_t
    AddUintToBuffer(&buffer, dex_data.checksum);  // uint32_t
    AddUintToBuffer(&buffer, dex_data.num_method_ids);  // uint32_t

    AddStringToBuffer(&buffer, dex_data.profile_key);
  }

  for (const std::unique_ptr<DexFileData>& dex_data_ptr : info_) {
    const DexFileData& dex_data = *dex_data_ptr;

    // Note that we allow dex files without any methods or classes, so that
    // inline caches can refer valid dex files.

    uint16_t last_method_index = 0;
    for (const auto& method_it : dex_data.method_map) {
      // Store the difference between the method indices. The SafeMap is ordered by
      // method_id, so the difference will always be non negative.
      DCHECK_GE(method_it.first, last_method_index);
      uint16_t diff_with_last_method_index = method_it.first - last_method_index;
      last_method_index = method_it.first;
      AddUintToBuffer(&buffer, diff_with_last_method_index);
      AddInlineCacheToBuffer(&buffer, method_it.second);
    }

    uint16_t last_class_index = 0;
    for (const auto& class_id : dex_data.class_set) {
      // Store the difference between the class indices. The set is ordered by
      // class_id, so the difference will always be non negative.
      DCHECK_GE(class_id.index_, last_class_index);
      uint16_t diff_with_last_class_index = class_id.index_ - last_class_index;
      last_class_index = class_id.index_;
      AddUintToBuffer(&buffer, diff_with_last_class_index);
    }

    buffer.insert(buffer.end(),
                  dex_data.bitmap_storage.begin(),
                  dex_data.bitmap_storage.end());
  }

  ArrayRef<const uint8_t> in_buffer(buffer.data(), required_capacity);
  uint32_t output_size = 0;
  std::unique_ptr<uint8_t[]> compressed_buffer = DeflateBuffer(in_buffer, &output_size);

  if (output_size > GetSizeWarningThresholdBytes()) {
    LOG(WARNING) << "Profile data size exceeds "
        << GetSizeWarningThresholdBytes()
        << " It has " << output_size << " bytes";
  }

  buffer.clear();
  AddUintToBuffer(&buffer, output_size);

  if (!WriteBuffer(fd, buffer.data(), buffer.size())) {
    return false;
  }
  if (!WriteBuffer(fd, compressed_buffer.get(), output_size)) {
    return false;
  }
  uint64_t total_time = NanoTime() - start;
  VLOG(profiler) << "Compressed from "
                 << std::to_string(required_capacity)
                 << " to "
                 << std::to_string(output_size);
  VLOG(profiler) << "Time to save profile: " << std::to_string(total_time);
  return true;
}

void ProfileCompilationInfo::AddInlineCacheToBuffer(std::vector<uint8_t>* buffer,
                                                    const InlineCacheMap& inline_cache_map) {
  // Add inline cache map size.
  AddUintToBuffer(buffer, static_cast<uint16_t>(inline_cache_map.size()));
  if (inline_cache_map.size() == 0) {
    return;
  }
  for (const auto& inline_cache_it : inline_cache_map) {
    uint16_t dex_pc = inline_cache_it.first;
    const DexPcData dex_pc_data = inline_cache_it.second;
    const ClassSet& classes = dex_pc_data.classes;

    // Add the dex pc.
    AddUintToBuffer(buffer, dex_pc);

    // Add the megamorphic/missing_types encoding if needed and continue.
    // In either cases we don't add any classes to the profiles and so there's
    // no point to continue.
    // TODO(calin): in case we miss types there is still value to add the
    // rest of the classes. They can be added without bumping the profile version.
    if (dex_pc_data.is_missing_types) {
      DCHECK(!dex_pc_data.is_megamorphic);  // at this point the megamorphic flag should not be set.
      DCHECK_EQ(classes.size(), 0u);
      AddUintToBuffer(buffer, kIsMissingTypesEncoding);
      continue;
    } else if (dex_pc_data.is_megamorphic) {
      DCHECK_EQ(classes.size(), 0u);
      AddUintToBuffer(buffer, kIsMegamorphicEncoding);
      continue;
    }

    DCHECK_LT(classes.size(), ProfileCompilationInfo::kIndividualInlineCacheSize);
    DCHECK_NE(classes.size(), 0u) << "InlineCache contains a dex_pc with 0 classes";

    SafeMap<ProfileIndexType, std::vector<dex::TypeIndex>> dex_to_classes_map;
    // Group the classes by dex. We expect that most of the classes will come from
    // the same dex, so this will be more efficient than encoding the dex index
    // for each class reference.
    GroupClassesByDex(classes, &dex_to_classes_map);
    // Add the dex map size.
    AddUintToBuffer(buffer, static_cast<uint8_t>(dex_to_classes_map.size()));
    for (const auto& dex_it : dex_to_classes_map) {
      ProfileIndexType dex_profile_index = dex_it.first;
      const std::vector<dex::TypeIndex>& dex_classes = dex_it.second;
      // Add the dex profile index.
      WriteProfileIndex(buffer, dex_profile_index);
      // Add the the number of classes for each dex profile index.
      AddUintToBuffer(buffer, static_cast<uint8_t>(dex_classes.size()));
      for (size_t i = 0; i < dex_classes.size(); i++) {
        // Add the type index of the classes.
        AddUintToBuffer(buffer, dex_classes[i].index_);
      }
    }
  }
}

uint32_t ProfileCompilationInfo::GetMethodsRegionSize(const DexFileData& dex_data) {
  // ((uint16_t)method index + (uint16_t)inline cache size) * number of methods
  uint32_t size = 2 * sizeof(uint16_t) * dex_data.method_map.size();
  for (const auto& method_it : dex_data.method_map) {
    const InlineCacheMap& inline_cache = method_it.second;
    size += sizeof(uint16_t) * inline_cache.size();  // dex_pc
    for (const auto& inline_cache_it : inline_cache) {
      const ClassSet& classes = inline_cache_it.second.classes;
      SafeMap<ProfileIndexType, std::vector<dex::TypeIndex>> dex_to_classes_map;
      GroupClassesByDex(classes, &dex_to_classes_map);
      size += sizeof(uint8_t);  // dex_to_classes_map size
      for (const auto& dex_it : dex_to_classes_map) {
        size += SizeOfProfileIndexType();  // dex profile index
        size += sizeof(uint8_t);  // number of classes
        const std::vector<dex::TypeIndex>& dex_classes = dex_it.second;
        size += sizeof(uint16_t) * dex_classes.size();  // the actual classes
      }
    }
  }
  return size;
}

void ProfileCompilationInfo::GroupClassesByDex(
    const ClassSet& classes,
    /*out*/SafeMap<ProfileIndexType, std::vector<dex::TypeIndex>>* dex_to_classes_map) {
  for (const auto& classes_it : classes) {
    auto dex_it = dex_to_classes_map->FindOrAdd(classes_it.dex_profile_index);
    dex_it->second.push_back(classes_it.type_index);
  }
}

ProfileCompilationInfo::DexFileData* ProfileCompilationInfo::GetOrAddDexFileData(
    const std::string& profile_key,
    uint32_t checksum,
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
      if (kIsDebugBuild) {
        LOG(ERROR) << "Exceeded the maximum number of dex file. Something went wrong";
      }
      return nullptr;
    }
    ProfileIndexType new_profile_index = dchecked_integral_cast<ProfileIndexType>(info_.size());
    std::unique_ptr<DexFileData> dex_file_data(new (&allocator_) DexFileData(
        &allocator_,
        profile_key,
        checksum,
        new_profile_index,
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

  if (num_method_ids != result->num_method_ids) {
    // This should not happen... added to help investigating b/65812889.
    LOG(ERROR) << "num_method_ids mismatch for dex " << profile_key
        << ", expected=" << num_method_ids
        << ", actual=" << result->num_method_ids;
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
      DexFileData* class_dex_data = GetOrAddDexFileData(class_ref.dex_file, annotation);
      if (class_dex_data == nullptr) {  // checksum mismatch
        return false;
      }
      DexPcData* dex_pc_data = FindOrAddDexPc(inline_cache, cache.dex_pc);
      if (dex_pc_data->is_missing_types || dex_pc_data->is_megamorphic) {
        // Don't bother adding classes if we are missing types or already megamorphic.
        break;
      }
      dex_pc_data->AddClass(class_dex_data->profile_index, class_ref.TypeIndex());
    }
  }
  return true;
}

#define READ_UINT(type, buffer, dest, error)            \
  do {                                                  \
    if (!(buffer).ReadUintAndAdvance<type>(&(dest))) {  \
      *(error) = "Could not read "#dest;                \
      return false;                                     \
    }                                                   \
  }                                                     \
  while (false)

bool ProfileCompilationInfo::ReadInlineCache(
    SafeBuffer& buffer,
    ProfileIndexType number_of_dex_files,
    const SafeMap<ProfileIndexType, ProfileIndexType>& dex_profile_index_remap,
    /*out*/ InlineCacheMap* inline_cache,
    /*out*/ std::string* error) {
  uint16_t inline_cache_size;
  READ_UINT(uint16_t, buffer, inline_cache_size, error);
  for (; inline_cache_size > 0; inline_cache_size--) {
    uint16_t dex_pc;
    uint8_t dex_to_classes_map_size;
    READ_UINT(uint16_t, buffer, dex_pc, error);
    READ_UINT(uint8_t, buffer, dex_to_classes_map_size, error);
    DexPcData* dex_pc_data = FindOrAddDexPc(inline_cache, dex_pc);
    if (dex_to_classes_map_size == kIsMissingTypesEncoding) {
      dex_pc_data->SetIsMissingTypes();
      continue;
    }
    if (dex_to_classes_map_size == kIsMegamorphicEncoding) {
      dex_pc_data->SetIsMegamorphic();
      continue;
    }
    for (; dex_to_classes_map_size > 0; dex_to_classes_map_size--) {
      ProfileIndexType dex_profile_index;
      uint8_t dex_classes_size;
      if (!ReadProfileIndex(buffer, &dex_profile_index)) {
        *error = "Cannot read profile index";
        return false;
      }
      READ_UINT(uint8_t, buffer, dex_classes_size, error);
      if (dex_profile_index >= number_of_dex_files) {
        *error = "dex_profile_index out of bounds ";
        *error += std::to_string(dex_profile_index) + " " + std::to_string(number_of_dex_files);
        return false;
      }
      for (; dex_classes_size > 0; dex_classes_size--) {
        uint16_t type_index;
        READ_UINT(uint16_t, buffer, type_index, error);
        auto it = dex_profile_index_remap.find(dex_profile_index);
        if (it == dex_profile_index_remap.end()) {
          // If we don't have an index that's because the dex file was filtered out when loading.
          // Set missing types on the dex pc data.
          dex_pc_data->SetIsMissingTypes();
        } else {
          dex_pc_data->AddClass(it->second, dex::TypeIndex(type_index));
        }
      }
    }
  }
  return true;
}

bool ProfileCompilationInfo::ReadMethods(
    SafeBuffer& buffer,
    ProfileIndexType number_of_dex_files,
    const ProfileLineHeader& line_header,
    const SafeMap<ProfileIndexType, ProfileIndexType>& dex_profile_index_remap,
    /*out*/std::string* error) {
  uint32_t unread_bytes_before_operation = buffer.CountUnreadBytes();
  if (unread_bytes_before_operation < line_header.method_region_size_bytes) {
    *error += "Profile EOF reached prematurely for ReadMethod";
    return false;
  }
  size_t expected_unread_bytes_after_operation = buffer.CountUnreadBytes()
      - line_header.method_region_size_bytes;
  uint16_t last_method_index = 0;
  while (buffer.CountUnreadBytes() > expected_unread_bytes_after_operation) {
    DexFileData* const data = GetOrAddDexFileData(line_header.profile_key,
                                                  line_header.checksum,
                                                  line_header.num_method_ids);
    uint16_t diff_with_last_method_index;
    READ_UINT(uint16_t, buffer, diff_with_last_method_index, error);
    uint16_t method_index = last_method_index + diff_with_last_method_index;
    last_method_index = method_index;
    InlineCacheMap* inline_cache = data->FindOrAddHotMethod(method_index);
    if (inline_cache == nullptr) {
      return false;
    }
    if (!ReadInlineCache(buffer,
                         number_of_dex_files,
                         dex_profile_index_remap,
                         inline_cache,
                         error)) {
      return false;
    }
  }
  uint32_t total_bytes_read = unread_bytes_before_operation - buffer.CountUnreadBytes();
  if (total_bytes_read != line_header.method_region_size_bytes) {
    *error += "Profile data inconsistent for ReadMethods";
    return false;
  }
  return true;
}

bool ProfileCompilationInfo::ReadClasses(SafeBuffer& buffer,
                                         const ProfileLineHeader& line_header,
                                         /*out*/std::string* error) {
  size_t unread_bytes_before_op = buffer.CountUnreadBytes();
  if (unread_bytes_before_op < line_header.class_set_size) {
    *error += "Profile EOF reached prematurely for ReadClasses";
    return false;
  }

  uint16_t last_class_index = 0;
  for (uint16_t i = 0; i < line_header.class_set_size; i++) {
    uint16_t diff_with_last_class_index;
    READ_UINT(uint16_t, buffer, diff_with_last_class_index, error);
    uint16_t type_index = last_class_index + diff_with_last_class_index;
    last_class_index = type_index;

    DexFileData* const data = GetOrAddDexFileData(line_header.profile_key,
                                                  line_header.checksum,
                                                  line_header.num_method_ids);
    if (data == nullptr) {
       return false;
    }
    data->class_set.insert(dex::TypeIndex(type_index));
  }
  size_t total_bytes_read = unread_bytes_before_op - buffer.CountUnreadBytes();
  uint32_t expected_bytes_read = line_header.class_set_size * sizeof(uint16_t);
  if (total_bytes_read != expected_bytes_read) {
    *error += "Profile data inconsistent for ReadClasses";
    return false;
  }
  return true;
}

// Tests for EOF by trying to read 1 byte from the descriptor.
// Returns:
//   0 if the descriptor is at the EOF,
//  -1 if there was an IO error
//   1 if the descriptor has more content to read
static int testEOF(int fd) {
  uint8_t buffer[1];
  return TEMP_FAILURE_RETRY(read(fd, buffer, 1));
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::ReadProfileHeader(
      ProfileSource& source,
      /*out*/ProfileIndexType* number_of_dex_files,
      /*out*/uint32_t* uncompressed_data_size,
      /*out*/uint32_t* compressed_data_size,
      /*out*/std::string* error) {
  // Read magic and version
  const size_t kMagicVersionSize =
    sizeof(kProfileMagic) +
    kProfileVersionSize;
  SafeBuffer safe_buffer_version(kMagicVersionSize);

  ProfileLoadStatus status = safe_buffer_version.Fill(source, "ReadProfileHeaderVersion", error);
  if (status != ProfileLoadStatus::kSuccess) {
    return status;
  }

  if (!safe_buffer_version.CompareAndAdvance(kProfileMagic, sizeof(kProfileMagic))) {
    *error = "Profile missing magic";
    return ProfileLoadStatus::kVersionMismatch;
  }
  if (safe_buffer_version.CountUnreadBytes() < kProfileVersionSize) {
     *error = "Cannot read profile version";
     return ProfileLoadStatus::kBadData;
  }
  memcpy(version_, safe_buffer_version.GetCurrentPtr(), kProfileVersionSize);
  if ((memcmp(version_, kProfileVersion, kProfileVersionSize) != 0) &&
      (memcmp(version_, kProfileVersionForBootImage, kProfileVersionSize) != 0)) {
    *error = "Profile version mismatch";
    return ProfileLoadStatus::kVersionMismatch;
  }

  const size_t kProfileHeaderDataSize =
    SizeOfProfileIndexType() +  // number of dex files
    sizeof(uint32_t) +  // size of uncompressed profile data
    sizeof(uint32_t);  // size of compressed profile data
  SafeBuffer safe_buffer_header_data(kProfileHeaderDataSize);

  status = safe_buffer_header_data.Fill(source, "ReadProfileHeaderData", error);
  if (status != ProfileLoadStatus::kSuccess) {
    return status;
  }

  if (!ReadProfileIndex(safe_buffer_header_data, number_of_dex_files)) {
    *error = "Cannot read the number of dex files";
    return ProfileLoadStatus::kBadData;
  }
  if (!safe_buffer_header_data.ReadUintAndAdvance<uint32_t>(uncompressed_data_size)) {
    *error = "Cannot read the size of uncompressed data";
    return ProfileLoadStatus::kBadData;
  }
  if (!safe_buffer_header_data.ReadUintAndAdvance<uint32_t>(compressed_data_size)) {
    *error = "Cannot read the size of compressed data";
    return ProfileLoadStatus::kBadData;
  }
  return ProfileLoadStatus::kSuccess;
}

bool ProfileCompilationInfo::ReadProfileLineHeaderElements(SafeBuffer& buffer,
                                                           /*out*/uint16_t* profile_key_size,
                                                           /*out*/ProfileLineHeader* line_header,
                                                           /*out*/std::string* error) {
  READ_UINT(uint16_t, buffer, *profile_key_size, error);
  READ_UINT(uint16_t, buffer, line_header->class_set_size, error);
  READ_UINT(uint32_t, buffer, line_header->method_region_size_bytes, error);
  READ_UINT(uint32_t, buffer, line_header->checksum, error);
  READ_UINT(uint32_t, buffer, line_header->num_method_ids, error);
  return true;
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::ReadProfileLineHeader(
    SafeBuffer& buffer,
    /*out*/ProfileLineHeader* line_header,
    /*out*/std::string* error) {
  if (buffer.CountUnreadBytes() < kLineHeaderSize) {
    *error += "Profile EOF reached prematurely for ReadProfileLineHeader";
    return ProfileLoadStatus::kBadData;
  }

  uint16_t profile_key_size;
  if (!ReadProfileLineHeaderElements(buffer, &profile_key_size, line_header, error)) {
    return ProfileLoadStatus::kBadData;
  }

  if (profile_key_size == 0 || profile_key_size > kMaxDexFileKeyLength) {
    *error = "ProfileKey has an invalid size: " +
        std::to_string(static_cast<uint32_t>(profile_key_size));
    return ProfileLoadStatus::kBadData;
  }

  if (buffer.CountUnreadBytes() < profile_key_size) {
    *error += "Profile EOF reached prematurely for ReadProfileHeaderDexLocation";
    return ProfileLoadStatus::kBadData;
  }
  const uint8_t* base_ptr = buffer.GetCurrentPtr();
  line_header->profile_key.assign(
      reinterpret_cast<const char*>(base_ptr), profile_key_size);
  buffer.Advance(profile_key_size);
  return ProfileLoadStatus::kSuccess;
}

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::ReadProfileLine(
      SafeBuffer& buffer,
      ProfileIndexType number_of_dex_files,
      const ProfileLineHeader& line_header,
      const SafeMap<ProfileIndexType, ProfileIndexType>& dex_profile_index_remap,
      bool merge_classes,
      /*out*/std::string* error) {
  DexFileData* data = GetOrAddDexFileData(line_header.profile_key,
                                          line_header.checksum,
                                          line_header.num_method_ids);
  if (data == nullptr) {
    *error = "Error when reading profile file line header: checksum mismatch for "
        + line_header.profile_key;
    return ProfileLoadStatus::kBadData;
  }

  if (!ReadMethods(buffer, number_of_dex_files, line_header, dex_profile_index_remap, error)) {
    return ProfileLoadStatus::kBadData;
  }

  if (merge_classes) {
    if (!ReadClasses(buffer, line_header, error)) {
      return ProfileLoadStatus::kBadData;
    }
  }

  // Read method bitmap.
  const size_t bytes = data->bitmap_storage.size();
  if (buffer.CountUnreadBytes() < bytes) {
    *error += "Profile EOF reached prematurely for method bitmap";
    return ProfileLoadStatus::kBadData;
  }
  const uint8_t* base_ptr = buffer.GetCurrentPtr();
  std::copy_n(base_ptr, bytes, data->bitmap_storage.data());
  buffer.Advance(bytes);

  return ProfileLoadStatus::kSuccess;
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

    if (dex_data->num_method_ids != dex_file->NumMethodIds()) {
      LOG(ERROR) << "Number of method ids in dex file and profile don't match."
                 << "dex location " << dex_location << " NumMethodId in DexFile"
                 << dex_file->NumMethodIds() << ", NumMethodId in profile"
                 << dex_data->num_method_ids;
      return false;
    }

    // Verify method_encoding.
    for (const auto& method_it : dex_data->method_map) {
      size_t method_id = (size_t)(method_it.first);
      if (method_id >= dex_file->NumMethodIds()) {
        LOG(ERROR) << "Invalid method id in profile file. dex location="
                   << dex_location << " method_id=" << method_id << " NumMethodIds="
                   << dex_file->NumMethodIds();
        return false;
      }

      // Verify class indices of inline caches.
      const InlineCacheMap &inline_cache_map = method_it.second;
      for (const auto& inline_cache_it : inline_cache_map) {
        const DexPcData dex_pc_data = inline_cache_it.second;
        if (dex_pc_data.is_missing_types || dex_pc_data.is_megamorphic) {
          // No class indices to verify.
          continue;
        }

        const ClassSet &classes = dex_pc_data.classes;
        SafeMap<ProfileIndexType, std::vector<dex::TypeIndex>> dex_to_classes_map;
        // Group the classes by dex. We expect that most of the classes will come from
        // the same dex, so this will be more efficient than encoding the dex index
        // for each class reference.
        GroupClassesByDex(classes, &dex_to_classes_map);
        for (const auto &dex_it : dex_to_classes_map) {
          ProfileIndexType dex_profile_index = dex_it.first;
          const auto dex_file_inline_cache_it = key_to_dex_file.find(
              info_[dex_profile_index]->profile_key);
          if (dex_file_inline_cache_it == key_to_dex_file.end()) {
            // It is okay if profile contains data for additional dex files.
            continue;
          }
          const DexFile *dex_file_for_inline_cache_check = dex_file_inline_cache_it->second;
          const std::vector<dex::TypeIndex> &dex_classes = dex_it.second;
          for (size_t i = 0; i < dex_classes.size(); i++) {
            if (dex_classes[i].index_ >= dex_file_for_inline_cache_check->NumTypeIds()) {
              LOG(ERROR) << "Invalid inline cache in profile file. dex location="
                  << dex_location << " method_id=" << method_id
                  << " dex_profile_index="
                  << static_cast<uint16_t >(dex_profile_index) << " type_index="
                  << dex_classes[i].index_
                  << " NumTypeIds="
                  << dex_file_for_inline_cache_check->NumTypeIds();
              return false;
            }
          }
        }
      }
    }
    // Verify class_ids.
    for (const auto& class_id : dex_data->class_set) {
      if (class_id.index_ >= dex_file->NumTypeIds()) {
        LOG(ERROR) << "Invalid class id in profile file. dex_file location "
                   << dex_location << " class_id=" << class_id.index_ << " NumClassIds="
                   << dex_file->NumClassDefs();
        return false;
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

ProfileCompilationInfo::ProfileLoadStatus ProfileCompilationInfo::ProfileSource::Read(
    uint8_t* buffer,
    size_t byte_count,
    const std::string& debug_stage,
    std::string* error) {
  if (IsMemMap()) {
    if (mem_map_cur_ + byte_count > mem_map_.Size()) {
      return ProfileLoadStatus::kBadData;
    }
    for (size_t i = 0; i < byte_count; i++) {
      buffer[i] = *(mem_map_.Begin() + mem_map_cur_);
      mem_map_cur_++;
    }
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
      buffer += bytes_read;
    }
  }
  return ProfileLoadStatus::kSuccess;
}

bool ProfileCompilationInfo::ProfileSource::HasConsumedAllData() const {
  return IsMemMap()
      ? (!mem_map_.IsValid() || mem_map_cur_ == mem_map_.Size())
      : (testEOF(fd_) == 0);
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

  // Read profile header: magic + version + number_of_dex_files.
  ProfileIndexType number_of_dex_files;
  uint32_t uncompressed_data_size;
  uint32_t compressed_data_size;
  status = ReadProfileHeader(*source,
                             &number_of_dex_files,
                             &uncompressed_data_size,
                             &compressed_data_size,
                             error);

  if (status != ProfileLoadStatus::kSuccess) {
    return status;
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

  std::unique_ptr<uint8_t[]> compressed_data(new uint8_t[compressed_data_size]);
  status = source->Read(compressed_data.get(), compressed_data_size, "ReadContent", error);
  if (status != ProfileLoadStatus::kSuccess) {
    *error += "Unable to read compressed profile data";
    return status;
  }

  if (!source->HasConsumedAllData()) {
    *error += "Unexpected data in the profile file.";
    return ProfileLoadStatus::kBadData;
  }

  SafeBuffer uncompressed_data(uncompressed_data_size);

  ArrayRef<const uint8_t> in_buffer(compressed_data.get(), compressed_data_size);
  ArrayRef<uint8_t> out_buffer(uncompressed_data.Get(), uncompressed_data_size);
  int ret = InflateBuffer(in_buffer, out_buffer);

  if (ret != Z_STREAM_END) {
    *error += "Error reading uncompressed profile data";
    return ProfileLoadStatus::kBadData;
  }

  std::vector<ProfileLineHeader> profile_line_headers;
  // Read profile line headers.
  for (ProfileIndexType k = 0; k < number_of_dex_files; k++) {
    ProfileLineHeader line_header;

    // First, read the line header to get the amount of data we need to read.
    status = ReadProfileLineHeader(uncompressed_data, &line_header, error);
    if (status != ProfileLoadStatus::kSuccess) {
      return status;
    }
    profile_line_headers.push_back(line_header);
  }

  SafeMap<ProfileIndexType, ProfileIndexType> dex_profile_index_remap;
  if (!RemapProfileIndex(profile_line_headers, filter_fn, &dex_profile_index_remap)) {
    return ProfileLoadStatus::kBadData;
  }

  for (ProfileIndexType k = 0; k < number_of_dex_files; k++) {
    if (!filter_fn(profile_line_headers[k].profile_key, profile_line_headers[k].checksum)) {
      // We have to skip the line. Advanced the current pointer of the buffer.
      size_t profile_line_size =
           profile_line_headers[k].class_set_size * sizeof(uint16_t) +
           profile_line_headers[k].method_region_size_bytes +
           DexFileData::ComputeBitmapStorage(IsForBootImage(),
              profile_line_headers[k].num_method_ids);
      uncompressed_data.Advance(profile_line_size);
    } else {
      // Now read the actual profile line.
      status = ReadProfileLine(uncompressed_data,
                               number_of_dex_files,
                               profile_line_headers[k],
                               dex_profile_index_remap,
                               merge_classes,
                               error);
      if (status != ProfileLoadStatus::kSuccess) {
        return status;
      }
    }
  }

  // Check that we read everything and that profiles don't contain junk data.
  if (uncompressed_data.CountUnreadBytes() > 0) {
    *error = "Unexpected content in the profile file: " +
        std::to_string(uncompressed_data.CountUnreadBytes()) + " extra bytes";
    return ProfileLoadStatus::kBadData;
  } else {
    return ProfileLoadStatus::kSuccess;
  }
}

bool ProfileCompilationInfo::RemapProfileIndex(
    const std::vector<ProfileLineHeader>& profile_line_headers,
    const ProfileLoadFilterFn& filter_fn,
    /*out*/SafeMap<ProfileIndexType, ProfileIndexType>* dex_profile_index_remap) {
  // First verify that all checksums match. This will avoid adding garbage to
  // the current profile info.
  // Note that the number of elements should be very small, so this should not
  // be a performance issue.
  for (const ProfileLineHeader& other_profile_line_header : profile_line_headers) {
    if (!filter_fn(other_profile_line_header.profile_key, other_profile_line_header.checksum)) {
      continue;
    }
    // verify_checksum is false because we want to differentiate between a missing dex data and
    // a mismatched checksum.
    const DexFileData* dex_data = FindDexData(other_profile_line_header.profile_key,
                                              /* checksum= */ 0u,
                                              /* verify_checksum= */ false);
    if ((dex_data != nullptr) && (dex_data->checksum != other_profile_line_header.checksum)) {
      LOG(WARNING) << "Checksum mismatch for dex " << other_profile_line_header.profile_key;
      return false;
    }
  }
  // All checksums match. Import the data.
  uint32_t num_dex_files = static_cast<uint32_t>(profile_line_headers.size());
  for (uint32_t i = 0; i < num_dex_files; i++) {
    if (!filter_fn(profile_line_headers[i].profile_key, profile_line_headers[i].checksum)) {
      continue;
    }
    const DexFileData* dex_data = GetOrAddDexFileData(profile_line_headers[i].profile_key,
                                                      profile_line_headers[i].checksum,
                                                      profile_line_headers[i].num_method_ids);
    if (dex_data == nullptr) {
      return false;  // Could happen if we exceed the number of allowed dex files.
    }
    dex_profile_index_remap->Put(i, dex_data->profile_index);
  }
  return true;
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
  // This will make sure that the ClassReferences  will point to the correct dex file.
  SafeMap<ProfileIndexType, ProfileIndexType> dex_profile_index_remap;
  for (const std::unique_ptr<DexFileData>& other_dex_data : other.info_) {
    const DexFileData* dex_data = GetOrAddDexFileData(other_dex_data->profile_key,
                                                      other_dex_data->checksum,
                                                      other_dex_data->num_method_ids);
    if (dex_data == nullptr) {
      return false;  // Could happen if we exceed the number of allowed dex files.
    }
    dex_profile_index_remap.Put(other_dex_data->profile_index, dex_data->profile_index);
  }

  // Merge the actual profile data.
  for (const std::unique_ptr<DexFileData>& other_dex_data : other.info_) {
    DexFileData* dex_data = const_cast<DexFileData*>(FindDexData(other_dex_data->profile_key,
                                                                 other_dex_data->checksum));
    DCHECK(dex_data != nullptr);

    // Merge the classes.
    if (merge_classes) {
      dex_data->class_set.insert(other_dex_data->class_set.begin(),
                                 other_dex_data->class_set.end());
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
        const ClassSet& other_class_set = other_ic_it.second.classes;
        DexPcData* dex_pc_data = FindOrAddDexPc(inline_cache, other_dex_pc);
        if (other_ic_it.second.is_missing_types) {
          dex_pc_data->SetIsMissingTypes();
        } else if (other_ic_it.second.is_megamorphic) {
          dex_pc_data->SetIsMegamorphic();
        } else {
          for (const auto& class_it : other_class_set) {
            dex_pc_data->AddClass(dex_profile_index_remap.Get(
                class_it.dex_profile_index), class_it.type_index);
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
          for (const ClassReference& class_ref : inline_cache_it.second.classes) {
            os << "(" << static_cast<uint32_t>(class_ref.dex_profile_index)
               << "," << class_ref.type_index.index_ << ")";
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
    for (const auto class_it : dex_data->class_set) {
      if (dex_file != nullptr) {
        os << "\n\t\t" << dex_file->PrettyType(class_it);
      } else {
        os << class_it.index_ << ",";
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
  const uint16_t max_method = std::numeric_limits<uint16_t>::max();
  const uint16_t max_classes = std::numeric_limits<uint16_t>::max();
  uint16_t number_of_methods = max_method * method_percentage / 100;
  uint16_t number_of_classes = max_classes * class_percentage / 100;

  std::srand(random_seed);

  // Make sure we generate more samples with a low index value.
  // This makes it more likely to hit valid method/class indices in small apps.
  const uint16_t kFavorFirstN = 10000;
  const uint16_t kFavorSplit = 2;

  for (uint16_t i = 0; i < number_of_dex_files; i++) {
    std::string dex_location = DexFileLoader::GetMultiDexLocation(i, base_dex_location.c_str());
    std::string profile_key = info.GetProfileDexFileBaseKey(dex_location);

    DexFileData* const data = info.GetOrAddDexFileData(profile_key, /*checksum=*/ 0, max_method);
    for (uint16_t m = 0; m < number_of_methods; m++) {
      uint16_t method_idx = rand() % max_method;
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
          profile_key, checksum, dex_file->NumMethodIds());
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
  DCHECK_EQ(info_.empty(), profile_key_map_.empty());
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
  if (index >= num_method_ids) {
    LOG(ERROR) << "Invalid method index " << index << ". num_method_ids=" << num_method_ids;
    return false;
  }

  SetMethodHotness(index, flags);

  if ((flags & MethodHotness::kFlagHot) != 0) {
    ProfileCompilationInfo::InlineCacheMap* result = FindOrAddHotMethod(index);
    DCHECK(result != nullptr);
  }
  return true;
}

void ProfileCompilationInfo::DexFileData::SetMethodHotness(size_t index,
                                                           MethodHotness::Flag flags) {
  DCHECK_LT(index, num_method_ids);
  uint32_t lastFlag = is_for_boot_image
      ? MethodHotness::kFlagLastBoot
      : MethodHotness::kFlagLastRegular;
  for (uint32_t flag = MethodHotness::kFlagFirst; flag <= lastFlag; flag = flag << 1) {
    if (flag == MethodHotness::kFlagHot) {
      // There's no bit for hotness in the bitmap.
      // We store the hotness by recording the method in the method list.
      continue;
    }
    if ((flags & flag) != 0) {
      method_bitmap.StoreBit(MethodFlagBitmapIndex(
          static_cast<MethodHotness::Flag>(flag), index), /*value=*/ true);
    }
  }
}

ProfileCompilationInfo::MethodHotness ProfileCompilationInfo::DexFileData::GetHotnessInfo(
    uint32_t dex_method_index) const {
  MethodHotness ret;
  uint32_t lastFlag = is_for_boot_image
      ? MethodHotness::kFlagLastBoot
      : MethodHotness::kFlagLastRegular;
  for (uint32_t flag = MethodHotness::kFlagFirst; flag <= lastFlag; flag = flag << 1) {
    if (flag == MethodHotness::kFlagHot) {
      continue;
    }
    if (method_bitmap.LoadBit(MethodFlagBitmapIndex(
          static_cast<MethodHotness::Flag>(flag), dex_method_index))) {
      ret.AddFlag(static_cast<MethodHotness::Flag>(flag));
    }
  }
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

ProfileCompilationInfo::DexPcData*
ProfileCompilationInfo::FindOrAddDexPc(InlineCacheMap* inline_cache, uint32_t dex_pc) {
  return &(inline_cache->FindOrAdd(dex_pc, DexPcData(&allocator_))->second);
}

HashSet<std::string> ProfileCompilationInfo::GetClassDescriptors(
    const std::vector<const DexFile*>& dex_files,
    const ProfileSampleAnnotation& annotation) {
  HashSet<std::string> ret;
  for (const DexFile* dex_file : dex_files) {
    const DexFileData* data = FindDexDataUsingAnnotations(dex_file, annotation);
    if (data != nullptr) {
      for (dex::TypeIndex type_idx : data->class_set) {
        if (!dex_file->IsTypeIndexValid(type_idx)) {
          // Something went bad. The profile is probably corrupted. Abort and return an emtpy set.
          LOG(WARNING) << "Corrupted profile: invalid type index "
              << type_idx.index_ << " in dex " << dex_file->GetLocation();
          return HashSet<std::string>();
        }
        const dex::TypeId& type_id = dex_file->GetTypeId(type_idx);
        ret.insert(dex_file->GetTypeDescriptor(type_id));
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
      if (dex_data->checksum == dex_file->GetLocationChecksum()
          && dex_data->num_method_ids == dex_file->NumMethodIds()) {
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

bool ProfileCompilationInfo::DexFileData::ContainsClass(const dex::TypeIndex type_index) const {
  return class_set.find(type_index) != class_set.end();
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
         << ",num_method_ids=" << dumper.GetNumMethodIds()
         << "]";
  return stream;
}

void ProfileCompilationInfo::WriteProfileIndex(
    std::vector<uint8_t>* buffer, ProfileIndexType value) const {
  if (IsForBootImage()) {
    AddUintToBuffer(buffer, value);
  } else {
    AddUintToBuffer(buffer, static_cast<ProfileIndexTypeRegular>(value));
  }
}

bool ProfileCompilationInfo::ReadProfileIndex(
    SafeBuffer& safe_buffer, ProfileIndexType* value) const {
  if (IsForBootImage()) {
    return safe_buffer.ReadUintAndAdvance<ProfileIndexType>(value);
  } else {
    ProfileIndexTypeRegular out;
    bool result = safe_buffer.ReadUintAndAdvance<ProfileIndexTypeRegular>(&out);
    *value = out;
    return result;
  }
}

ProfileCompilationInfo::ProfileIndexType ProfileCompilationInfo::MaxProfileIndex() const {
  return IsForBootImage()
      ? std::numeric_limits<ProfileIndexType>::max()
      : std::numeric_limits<ProfileIndexTypeRegular>::max();
}

uint32_t ProfileCompilationInfo::SizeOfProfileIndexType() const {
  return IsForBootImage()
    ? sizeof(ProfileIndexType)
    : sizeof(ProfileIndexTypeRegular);
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
