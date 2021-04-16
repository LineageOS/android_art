/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef ART_RUNTIME_VDEX_FILE_H_
#define ART_RUNTIME_VDEX_FILE_H_

#include <stdint.h>
#include <string>

#include "base/array_ref.h"
#include "base/macros.h"
#include "base/mem_map.h"
#include "base/os.h"
#include "class_status.h"
#include "dex/compact_offset_table.h"
#include "dex/dex_file.h"
#include "quicken_info.h"
#include "handle.h"

namespace art {

class ClassLoaderContext;
class Thread;

namespace mirror {
class Class;
}

namespace verifier {
class VerifierDeps;
}  // namespace verifier

// VDEX files contain extracted DEX files. The VdexFile class maps the file to
// memory and provides tools for accessing its individual sections.
//
// In the description below, D is the number of dex files.
//
// File format:
//   VdexFileHeader    fixed-length header
//   VdexSectionHeader[kNumberOfSections]
//
//   Checksum section
//     VdexChecksum[D]
//
//   Optionally:
//      DexSection
//          DEX[0]                array of the input DEX files
//          DEX[1]
//          ...
//          DEX[D-1]
//
//   VerifierDeps
//      4-byte alignment
//      uint32[D]                  DexFileDeps offsets for each dex file
//      DexFileDeps[D][]           verification dependencies
//        4-byte alignment
//        uint32[class_def_size]     TypeAssignability offsets (kNotVerifiedMarker for a class
//                                        that isn't verified)
//        uint32                     Offset of end of AssignabilityType sets
//        uint8[]                    AssignabilityType sets
//        4-byte alignment
//        uint32                     Number of strings
//        uint32[]                   String data offsets for each string
//        uint8[]                    String data


enum VdexSection : uint32_t {
  kChecksumSection = 0,
  kDexFileSection = 1,
  kVerifierDepsSection = 2,
  kNumberOfSections = 3,
};

class VdexFile {
 public:
  using VdexChecksum = uint32_t;

  struct VdexSectionHeader {
    VdexSection section_kind;
    uint32_t section_offset;
    uint32_t section_size;

    VdexSectionHeader(VdexSection kind, uint32_t offset, uint32_t size)
        : section_kind(kind), section_offset(offset), section_size(size) {}

    VdexSectionHeader() {}
  };

  struct VdexFileHeader {
   public:
    explicit VdexFileHeader(bool has_dex_section);

    const char* GetMagic() const { return reinterpret_cast<const char*>(magic_); }
    const char* GetVdexVersion() const {
      return reinterpret_cast<const char*>(vdex_version_);
    }
    uint32_t GetNumberOfSections() const {
      return number_of_sections_;
    }
    bool IsMagicValid() const;
    bool IsVdexVersionValid() const;
    bool IsValid() const {
      return IsMagicValid() && IsVdexVersionValid();
    }

    static constexpr uint8_t kVdexInvalidMagic[] = { 'w', 'd', 'e', 'x' };

   private:
    static constexpr uint8_t kVdexMagic[] = { 'v', 'd', 'e', 'x' };

    // The format version of the verifier deps header and the verifier deps.
    // Last update: Introduce vdex sections.
    static constexpr uint8_t kVdexVersion[] = { '0', '2', '7', '\0' };

    uint8_t magic_[4];
    uint8_t vdex_version_[4];
    uint32_t number_of_sections_;
  };

  const VdexSectionHeader& GetSectionHeaderAt(uint32_t index) const {
    DCHECK_LT(index, GetVdexFileHeader().GetNumberOfSections());
    return *reinterpret_cast<const VdexSectionHeader*>(
        Begin() + sizeof(VdexFileHeader) + index * sizeof(VdexSectionHeader));
  }

  const VdexSectionHeader& GetSectionHeader(VdexSection kind) const {
    return GetSectionHeaderAt(static_cast<uint32_t>(kind));
  }

  static size_t GetChecksumsOffset() {
    return sizeof(VdexFileHeader) +
        static_cast<size_t>(VdexSection::kNumberOfSections) * sizeof(VdexSectionHeader);
  }

  size_t GetComputedFileSize() const {
    const VdexFileHeader& header = GetVdexFileHeader();
    uint32_t size = sizeof(VdexFileHeader) +
        header.GetNumberOfSections() * sizeof(VdexSectionHeader);
    for (uint32_t i = 0; i < header.GetNumberOfSections(); ++i) {
      size = std::max(size,
                      GetSectionHeaderAt(i).section_offset + GetSectionHeaderAt(i).section_size);
    }
    return size;
  }

  bool IsDexSectionValid() const;

  bool HasDexSection() const {
    return GetSectionHeader(VdexSection::kDexFileSection).section_size != 0u;
  }
  uint32_t GetVerifierDepsSize() const {
    return GetSectionHeader(VdexSection::kVerifierDepsSection).section_size;
  }
  uint32_t GetNumberOfDexFiles() const {
    return GetSectionHeader(VdexSection::kChecksumSection).section_size / sizeof(VdexChecksum);
  }

  const VdexChecksum* GetDexChecksumsArray() const {
    return reinterpret_cast<const VdexChecksum*>(
        Begin() + GetSectionHeader(VdexSection::kChecksumSection).section_offset);
  }

  VdexChecksum GetDexChecksumAt(size_t idx) const {
    DCHECK_LT(idx, GetNumberOfDexFiles());
    return GetDexChecksumsArray()[idx];
  }

  // Note: The file is called "primary" to match the naming with profiles.
  static const constexpr char* kVdexNameInDmFile = "primary.vdex";

  explicit VdexFile(MemMap&& mmap) : mmap_(std::move(mmap)) {}

  // Returns nullptr if the vdex file cannot be opened or is not valid.
  // The mmap_* parameters can be left empty (nullptr/0/false) to allocate at random address.
  static std::unique_ptr<VdexFile> OpenAtAddress(uint8_t* mmap_addr,
                                                 size_t mmap_size,
                                                 bool mmap_reuse,
                                                 const std::string& vdex_filename,
                                                 bool writable,
                                                 bool low_4gb,
                                                 bool unquicken,
                                                 std::string* error_msg);

  // Returns nullptr if the vdex file cannot be opened or is not valid.
  // The mmap_* parameters can be left empty (nullptr/0/false) to allocate at random address.
  static std::unique_ptr<VdexFile> OpenAtAddress(uint8_t* mmap_addr,
                                                 size_t mmap_size,
                                                 bool mmap_reuse,
                                                 int file_fd,
                                                 size_t vdex_length,
                                                 const std::string& vdex_filename,
                                                 bool writable,
                                                 bool low_4gb,
                                                 bool unquicken,
                                                 std::string* error_msg);

  // Returns nullptr if the vdex file cannot be opened or is not valid.
  static std::unique_ptr<VdexFile> Open(const std::string& vdex_filename,
                                        bool writable,
                                        bool low_4gb,
                                        bool unquicken,
                                        std::string* error_msg) {
    return OpenAtAddress(nullptr,
                         0,
                         false,
                         vdex_filename,
                         writable,
                         low_4gb,
                         unquicken,
                         error_msg);
  }

  // Returns nullptr if the vdex file cannot be opened or is not valid.
  static std::unique_ptr<VdexFile> Open(int file_fd,
                                        size_t vdex_length,
                                        const std::string& vdex_filename,
                                        bool writable,
                                        bool low_4gb,
                                        bool unquicken,
                                        std::string* error_msg) {
    return OpenAtAddress(nullptr,
                         0,
                         false,
                         file_fd,
                         vdex_length,
                         vdex_filename,
                         writable,
                         low_4gb,
                         unquicken,
                         error_msg);
  }

  const uint8_t* Begin() const { return mmap_.Begin(); }
  const uint8_t* End() const { return mmap_.End(); }
  size_t Size() const { return mmap_.Size(); }

  const VdexFileHeader& GetVdexFileHeader() const {
    return *reinterpret_cast<const VdexFileHeader*>(Begin());
  }

  ArrayRef<const uint8_t> GetVerifierDepsData() const {
    return ArrayRef<const uint8_t>(
        Begin() + GetSectionHeader(VdexSection::kVerifierDepsSection).section_offset,
        GetSectionHeader(VdexSection::kVerifierDepsSection).section_size);
  }

  bool IsValid() const {
    return mmap_.Size() >= sizeof(VdexFileHeader) && GetVdexFileHeader().IsValid();
  }

  // This method is for iterating over the dex files in the vdex. If `cursor` is null,
  // the first dex file is returned. If `cursor` is not null, it must point to a dex
  // file and this method returns the next dex file if there is one, or null if there
  // is none.
  const uint8_t* GetNextDexFileData(const uint8_t* cursor, uint32_t dex_file_index) const;

  // Get the location checksum of the dex file number `dex_file_index`.
  uint32_t GetLocationChecksum(uint32_t dex_file_index) const {
    DCHECK_LT(dex_file_index, GetNumberOfDexFiles());
    return GetDexChecksumAt(dex_file_index);
  }

  // Open all the dex files contained in this vdex file.
  bool OpenAllDexFiles(std::vector<std::unique_ptr<const DexFile>>* dex_files,
                       std::string* error_msg) const;

  // Writes a vdex into `path` and returns true on success.
  // The vdex will not contain a dex section but will store checksums of `dex_files`,
  // encoded `verifier_deps`, as well as the current boot class path cheksum and
  // encoded `class_loader_context`.
  static bool WriteToDisk(const std::string& path,
                          const std::vector<const DexFile*>& dex_files,
                          const verifier::VerifierDeps& verifier_deps,
                          std::string* error_msg);

  // Returns true if the dex file checksums stored in the vdex header match
  // the checksums in `dex_headers`. Both the number of dex files and their
  // order must match too.
  bool MatchesDexFileChecksums(const std::vector<const DexFile::Header*>& dex_headers) const;

  ClassStatus ComputeClassStatus(Thread* self, Handle<mirror::Class> cls) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Return the name of the underlying `MemMap` of the vdex file, typically the
  // location on disk of the vdex file.
  const std::string& GetName() const {
    return mmap_.GetName();
  }

 private:
  bool ContainsDexFile(const DexFile& dex_file) const;

  const uint8_t* DexBegin() const {
    DCHECK(HasDexSection());
    return Begin() + GetSectionHeader(VdexSection::kDexFileSection).section_offset;
  }

  MemMap mmap_;

  DISALLOW_COPY_AND_ASSIGN(VdexFile);
};

}  // namespace art

#endif  // ART_RUNTIME_VDEX_FILE_H_
