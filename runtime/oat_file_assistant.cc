/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "oat_file_assistant.h"

#include <sstream>

#include <sys/stat.h>
#include "zlib.h"

#include "android-base/file.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"

#include "base/compiler_filter.h"
#include "base/file_utils.h"
#include "base/logging.h"  // For VLOG.
#include "base/macros.h"
#include "base/os.h"
#include "base/stl_util.h"
#include "base/string_view_cpp20.h"
#include "base/systrace.h"
#include "base/utils.h"
#include "class_linker.h"
#include "class_loader_context.h"
#include "dex/art_dex_file_loader.h"
#include "dex/dex_file_loader.h"
#include "exec_utils.h"
#include "gc/heap.h"
#include "gc/space/image_space.h"
#include "image.h"
#include "oat.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "vdex_file.h"

namespace art {

using android::base::StringPrintf;

static constexpr const char* kAnonymousDexPrefix = "Anonymous-DexFile@";
static constexpr const char* kVdexExtension = ".vdex";

std::ostream& operator << (std::ostream& stream, const OatFileAssistant::OatStatus status) {
  switch (status) {
    case OatFileAssistant::kOatCannotOpen:
      stream << "kOatCannotOpen";
      break;
    case OatFileAssistant::kOatDexOutOfDate:
      stream << "kOatDexOutOfDate";
      break;
    case OatFileAssistant::kOatBootImageOutOfDate:
      stream << "kOatBootImageOutOfDate";
      break;
    case OatFileAssistant::kOatUpToDate:
      stream << "kOatUpToDate";
      break;
    case OatFileAssistant::kOatContextOutOfDate:
      stream << "kOaContextOutOfDate";
      break;
  }

  return stream;
}

OatFileAssistant::OatFileAssistant(const char* dex_location,
                                   const InstructionSet isa,
                                   ClassLoaderContext* context,
                                   bool load_executable,
                                   bool only_load_system_executable)
    : OatFileAssistant(dex_location,
                       isa,
                       context,
                       load_executable,
                       only_load_system_executable,
                       /*vdex_fd=*/ -1,
                       /*oat_fd=*/ -1,
                       /*zip_fd=*/ -1) {}


OatFileAssistant::OatFileAssistant(const char* dex_location,
                                   const InstructionSet isa,
                                   ClassLoaderContext* context,
                                   bool load_executable,
                                   bool only_load_system_executable,
                                   int vdex_fd,
                                   int oat_fd,
                                   int zip_fd)
    : context_(context),
      isa_(isa),
      load_executable_(load_executable),
      only_load_system_executable_(only_load_system_executable),
      odex_(this, /*is_oat_location=*/ false),
      oat_(this, /*is_oat_location=*/ true),
      vdex_for_odex_(this, /*is_oat_location=*/ false),
      vdex_for_oat_(this, /*is_oat_location=*/ true),
      zip_fd_(zip_fd) {
  CHECK(dex_location != nullptr) << "OatFileAssistant: null dex location";
  CHECK(!load_executable || context != nullptr) << "Loading executable without a context";

  if (zip_fd < 0) {
    CHECK_LE(oat_fd, 0) << "zip_fd must be provided with valid oat_fd. zip_fd=" << zip_fd
      << " oat_fd=" << oat_fd;
    CHECK_LE(vdex_fd, 0) << "zip_fd must be provided with valid vdex_fd. zip_fd=" << zip_fd
      << " vdex_fd=" << vdex_fd;;
    CHECK(!UseFdToReadFiles());
  } else {
    CHECK(UseFdToReadFiles());
  }

  dex_location_.assign(dex_location);

  if (load_executable_ && isa != kRuntimeISA) {
    LOG(WARNING) << "OatFileAssistant: Load executable specified, "
      << "but isa is not kRuntimeISA. Will not attempt to load executable.";
    load_executable_ = false;
  }

  // Get the odex filename.
  std::string error_msg;
  std::string odex_file_name;
  if (DexLocationToOdexFilename(dex_location_, isa_, &odex_file_name, &error_msg)) {
    odex_.Reset(odex_file_name, UseFdToReadFiles(), zip_fd, vdex_fd, oat_fd);
    std::string vdex_file_name = GetVdexFilename(odex_file_name);
    // We dup FDs as the odex_ will claim ownership.
    vdex_for_odex_.Reset(vdex_file_name,
                         UseFdToReadFiles(),
                         DupCloexec(zip_fd),
                         DupCloexec(vdex_fd),
                         DupCloexec(oat_fd));
  } else {
    LOG(WARNING) << "Failed to determine odex file name: " << error_msg;
  }

  if (!UseFdToReadFiles()) {
    // Get the oat filename.
    std::string oat_file_name;
    if (DexLocationToOatFilename(dex_location_, isa_, &oat_file_name, &error_msg)) {
      oat_.Reset(oat_file_name, /*use_fd=*/ false);
      std::string vdex_file_name = GetVdexFilename(oat_file_name);
      vdex_for_oat_.Reset(vdex_file_name, UseFdToReadFiles(), zip_fd, vdex_fd, oat_fd);
    } else {
      LOG(WARNING) << "Failed to determine oat file name for dex location "
                   << dex_location_ << ": " << error_msg;
    }
  }

  // Check if the dex directory is writable.
  // This will be needed in most uses of OatFileAssistant and so it's OK to
  // compute it eagerly. (the only use which will not make use of it is
  // OatFileAssistant::GetStatusDump())
  size_t pos = dex_location_.rfind('/');
  if (pos == std::string::npos) {
    LOG(WARNING) << "Failed to determine dex file parent directory: " << dex_location_;
  } else if (!UseFdToReadFiles()) {
    // We cannot test for parent access when using file descriptors. That's ok
    // because in this case we will always pick the odex file anyway.
    std::string parent = dex_location_.substr(0, pos);
    if (access(parent.c_str(), W_OK) == 0) {
      dex_parent_writable_ = true;
    } else {
      VLOG(oat) << "Dex parent of " << dex_location_ << " is not writable: " << strerror(errno);
    }
  }
}

bool OatFileAssistant::UseFdToReadFiles() {
  return zip_fd_ >= 0;
}

bool OatFileAssistant::IsInBootClassPath() {
  // Note: We check the current boot class path, regardless of the ISA
  // specified by the user. This is okay, because the boot class path should
  // be the same for all ISAs.
  // TODO: Can we verify the boot class path is the same for all ISAs?
  Runtime* runtime = Runtime::Current();
  ClassLinker* class_linker = runtime->GetClassLinker();
  const auto& boot_class_path = class_linker->GetBootClassPath();
  for (size_t i = 0; i < boot_class_path.size(); i++) {
    if (boot_class_path[i]->GetLocation() == dex_location_) {
      VLOG(oat) << "Dex location " << dex_location_ << " is in boot class path";
      return true;
    }
  }
  return false;
}

int OatFileAssistant::GetDexOptNeeded(CompilerFilter::Filter target,
                                      bool profile_changed,
                                      bool downgrade) {
  OatFileInfo& info = GetBestInfo();
  DexOptNeeded dexopt_needed = info.GetDexOptNeeded(target,
                                                    profile_changed,
                                                    downgrade);
  if (info.IsOatLocation() || dexopt_needed == kDex2OatFromScratch) {
    return dexopt_needed;
  }
  return -dexopt_needed;
}

bool OatFileAssistant::IsUpToDate() {
  return GetBestInfo().Status() == kOatUpToDate;
}

std::unique_ptr<OatFile> OatFileAssistant::GetBestOatFile() {
  return GetBestInfo().ReleaseFileForUse();
}

std::string OatFileAssistant::GetStatusDump() {
  std::ostringstream status;
  bool oat_file_exists = false;
  bool odex_file_exists = false;
  if (oat_.Status() != kOatCannotOpen) {
    // If we can open the file, Filename should not return null.
    CHECK(oat_.Filename() != nullptr);

    oat_file_exists = true;
    status << *oat_.Filename() << "[status=" << oat_.Status() << ", ";
    const OatFile* file = oat_.GetFile();
    if (file == nullptr) {
      // If the file is null even though the status is not kOatCannotOpen, it
      // means we must have a vdex file with no corresponding oat file. In
      // this case we cannot determine the compilation filter. Indicate that
      // we have only the vdex file instead.
      status << "vdex-only";
    } else {
      status << "compilation_filter=" << CompilerFilter::NameOfFilter(file->GetCompilerFilter());
    }
  }

  if (odex_.Status() != kOatCannotOpen) {
    // If we can open the file, Filename should not return null.
    CHECK(odex_.Filename() != nullptr);

    odex_file_exists = true;
    if (oat_file_exists) {
      status << "] ";
    }
    status << *odex_.Filename() << "[status=" << odex_.Status() << ", ";
    const OatFile* file = odex_.GetFile();
    if (file == nullptr) {
      status << "vdex-only";
    } else {
      status << "compilation_filter=" << CompilerFilter::NameOfFilter(file->GetCompilerFilter());
    }
  }

  if (!oat_file_exists && !odex_file_exists) {
    status << "invalid[";
  }

  status << "]";
  return status.str();
}

std::vector<std::unique_ptr<const DexFile>> OatFileAssistant::LoadDexFiles(
    const OatFile &oat_file, const char *dex_location) {
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  if (LoadDexFiles(oat_file, dex_location, &dex_files)) {
    return dex_files;
  } else {
    return std::vector<std::unique_ptr<const DexFile>>();
  }
}

bool OatFileAssistant::LoadDexFiles(
    const OatFile &oat_file,
    const std::string& dex_location,
    std::vector<std::unique_ptr<const DexFile>>* out_dex_files) {
  // Load the main dex file.
  std::string error_msg;
  const OatDexFile* oat_dex_file = oat_file.GetOatDexFile(
      dex_location.c_str(), nullptr, &error_msg);
  if (oat_dex_file == nullptr) {
    LOG(WARNING) << error_msg;
    return false;
  }

  std::unique_ptr<const DexFile> dex_file = oat_dex_file->OpenDexFile(&error_msg);
  if (dex_file.get() == nullptr) {
    LOG(WARNING) << "Failed to open dex file from oat dex file: " << error_msg;
    return false;
  }
  out_dex_files->push_back(std::move(dex_file));

  // Load the rest of the multidex entries
  for (size_t i = 1;; i++) {
    std::string multidex_dex_location = DexFileLoader::GetMultiDexLocation(i, dex_location.c_str());
    oat_dex_file = oat_file.GetOatDexFile(multidex_dex_location.c_str(), nullptr);
    if (oat_dex_file == nullptr) {
      // There are no more multidex entries to load.
      break;
    }

    dex_file = oat_dex_file->OpenDexFile(&error_msg);
    if (dex_file.get() == nullptr) {
      LOG(WARNING) << "Failed to open dex file from oat dex file: " << error_msg;
      return false;
    }
    out_dex_files->push_back(std::move(dex_file));
  }
  return true;
}

bool OatFileAssistant::HasDexFiles() {
  ScopedTrace trace("HasDexFiles");
  // Ensure GetRequiredDexChecksums has been run so that
  // has_original_dex_files_ is initialized. We don't care about the result of
  // GetRequiredDexChecksums.
  GetRequiredDexChecksums();
  return has_original_dex_files_;
}

OatFileAssistant::OatStatus OatFileAssistant::OdexFileStatus() {
  return odex_.Status();
}

OatFileAssistant::OatStatus OatFileAssistant::OatFileStatus() {
  return oat_.Status();
}

bool OatFileAssistant::DexChecksumUpToDate(const VdexFile& file, std::string* error_msg) {
  ScopedTrace trace("DexChecksumUpToDate(vdex)");
  const std::vector<uint32_t>* required_dex_checksums = GetRequiredDexChecksums();
  if (required_dex_checksums == nullptr) {
    LOG(WARNING) << "Required dex checksums not found. Assuming dex checksums are up to date.";
    return true;
  }

  uint32_t number_of_dex_files = file.GetNumberOfDexFiles();
  if (required_dex_checksums->size() != number_of_dex_files) {
    *error_msg = StringPrintf("expected %zu dex files but found %u",
                              required_dex_checksums->size(),
                              number_of_dex_files);
    return false;
  }

  for (uint32_t i = 0; i < number_of_dex_files; i++) {
    uint32_t expected_checksum = (*required_dex_checksums)[i];
    uint32_t actual_checksum = file.GetLocationChecksum(i);
    if (expected_checksum != actual_checksum) {
      std::string dex = DexFileLoader::GetMultiDexLocation(i, dex_location_.c_str());
      *error_msg = StringPrintf("Dex checksum does not match for dex: %s."
                                "Expected: %u, actual: %u",
                                dex.c_str(),
                                expected_checksum,
                                actual_checksum);
      return false;
    }
  }

  return true;
}

bool OatFileAssistant::DexChecksumUpToDate(const OatFile& file, std::string* error_msg) {
  ScopedTrace trace("DexChecksumUpToDate(oat)");
  const std::vector<uint32_t>* required_dex_checksums = GetRequiredDexChecksums();
  if (required_dex_checksums == nullptr) {
    LOG(WARNING) << "Required dex checksums not found. Assuming dex checksums are up to date.";
    return true;
  }

  uint32_t number_of_dex_files = file.GetOatHeader().GetDexFileCount();
  if (required_dex_checksums->size() != number_of_dex_files) {
    *error_msg = StringPrintf("expected %zu dex files but found %u",
                              required_dex_checksums->size(),
                              number_of_dex_files);
    return false;
  }

  for (uint32_t i = 0; i < number_of_dex_files; i++) {
    std::string dex = DexFileLoader::GetMultiDexLocation(i, dex_location_.c_str());
    uint32_t expected_checksum = (*required_dex_checksums)[i];
    const OatDexFile* oat_dex_file = file.GetOatDexFile(dex.c_str(), nullptr);
    if (oat_dex_file == nullptr) {
      *error_msg = StringPrintf("failed to find %s in %s", dex.c_str(), file.GetLocation().c_str());
      return false;
    }
    uint32_t actual_checksum = oat_dex_file->GetDexFileLocationChecksum();
    if (expected_checksum != actual_checksum) {
      VLOG(oat) << "Dex checksum does not match for dex: " << dex
        << ". Expected: " << expected_checksum
        << ", Actual: " << actual_checksum;
      return false;
    }
  }
  return true;
}

static bool ValidateApexVersions(const OatFile& oat_file) {
  const char* oat_apex_versions =
      oat_file.GetOatHeader().GetStoreValueByKey(OatHeader::kApexVersionsKey);
  if (oat_apex_versions == nullptr) {
    return false;
  }
  // Some dex files get compiled with a subset of the boot classpath (for
  // example currently system server is compiled with DEX2OAT_BOOTCLASSPATH).
  // For such cases, the oat apex versions will be a prefix of the runtime apex
  // versions.
  return android::base::StartsWith(Runtime::Current()->GetApexVersions(), oat_apex_versions);
}

OatFileAssistant::OatStatus OatFileAssistant::GivenOatFileStatus(const OatFile& file) {
  // Verify the ART_USE_READ_BARRIER state.
  // TODO: Don't fully reject files due to read barrier state. If they contain
  // compiled code and are otherwise okay, we should return something like
  // kOatRelocationOutOfDate. If they don't contain compiled code, the read
  // barrier state doesn't matter.
  const bool is_cc = file.GetOatHeader().IsConcurrentCopying();
  constexpr bool kRuntimeIsCC = kUseReadBarrier;
  if (is_cc != kRuntimeIsCC) {
    return kOatCannotOpen;
  }

  // Verify the dex checksum.
  std::string error_msg;
  VdexFile* vdex = file.GetVdexFile();
  if (!DexChecksumUpToDate(*vdex, &error_msg)) {
    LOG(ERROR) << error_msg;
    return kOatDexOutOfDate;
  }

  CompilerFilter::Filter current_compiler_filter = file.GetCompilerFilter();

  // Verify the image checksum
  if (file.IsBackedByVdexOnly()) {
    VLOG(oat) << "Image checksum test skipped for vdex file " << file.GetLocation();
  } else if (CompilerFilter::DependsOnImageChecksum(current_compiler_filter)) {
    if (!ValidateBootClassPathChecksums(file)) {
      VLOG(oat) << "Oat image checksum does not match image checksum.";
      return kOatBootImageOutOfDate;
    }
    if (!ValidateApexVersions(file)) {
      VLOG(oat) << "Apex versions do not match.";
      return kOatBootImageOutOfDate;
    }
  } else {
    VLOG(oat) << "Image checksum test skipped for compiler filter " << current_compiler_filter;
  }

  // zip_file_only_contains_uncompressed_dex_ is only set during fetching the dex checksums.
  DCHECK(required_dex_checksums_attempted_);
  if (only_load_system_executable_ &&
      !LocationIsOnSystem(file.GetLocation().c_str()) &&
      file.ContainsDexCode() &&
      zip_file_only_contains_uncompressed_dex_) {
    LOG(ERROR) << "Not loading "
               << dex_location_
               << ": oat file has dex code, but APK has uncompressed dex code";
    return kOatDexOutOfDate;
  }

  if (!ClassLoaderContextIsOkay(file)) {
    return kOatContextOutOfDate;
  }

  return kOatUpToDate;
}

bool OatFileAssistant::AnonymousDexVdexLocation(const std::vector<const DexFile::Header*>& headers,
                                                InstructionSet isa,
                                                /* out */ std::string* dex_location,
                                                /* out */ std::string* vdex_filename) {
  uint32_t checksum = adler32(0L, Z_NULL, 0);
  for (const DexFile::Header* header : headers) {
    checksum = adler32_combine(checksum,
                               header->checksum_,
                               header->file_size_ - DexFile::kNumNonChecksumBytes);
  }

  const std::string& data_dir = Runtime::Current()->GetProcessDataDirectory();
  if (data_dir.empty() || Runtime::Current()->IsZygote()) {
    *dex_location = StringPrintf("%s%u", kAnonymousDexPrefix, checksum);
    return false;
  }
  *dex_location = StringPrintf("%s/%s%u.jar", data_dir.c_str(), kAnonymousDexPrefix, checksum);

  std::string odex_filename;
  std::string error_msg;
  if (!DexLocationToOdexFilename(*dex_location, isa, &odex_filename, &error_msg)) {
    LOG(WARNING) << "Could not get odex filename for " << *dex_location << ": " << error_msg;
    return false;
  }

  *vdex_filename = GetVdexFilename(odex_filename);
  return true;
}

bool OatFileAssistant::IsAnonymousVdexBasename(const std::string& basename) {
  DCHECK(basename.find('/') == std::string::npos);
  // `basename` must have format: <kAnonymousDexPrefix><checksum><kVdexExtension>
  if (basename.size() < strlen(kAnonymousDexPrefix) + strlen(kVdexExtension) + 1 ||
      !android::base::StartsWith(basename.c_str(), kAnonymousDexPrefix) ||
      !android::base::EndsWith(basename, kVdexExtension)) {
    return false;
  }
  // Check that all characters between the prefix and extension are decimal digits.
  for (size_t i = strlen(kAnonymousDexPrefix); i < basename.size() - strlen(kVdexExtension); ++i) {
    if (!std::isdigit(basename[i])) {
      return false;
    }
  }
  return true;
}

bool OatFileAssistant::DexLocationToOdexFilename(const std::string& location,
                                                 InstructionSet isa,
                                                 std::string* odex_filename,
                                                 std::string* error_msg) {
  CHECK(odex_filename != nullptr);
  CHECK(error_msg != nullptr);

  // The odex file name is formed by replacing the dex_location extension with
  // .odex and inserting an oat/<isa> directory. For example:
  //   location = /foo/bar/baz.jar
  //   odex_location = /foo/bar/oat/<isa>/baz.odex

  // Find the directory portion of the dex location and add the oat/<isa>
  // directory.
  size_t pos = location.rfind('/');
  if (pos == std::string::npos) {
    *error_msg = "Dex location " + location + " has no directory.";
    return false;
  }
  std::string dir = location.substr(0, pos+1);
  // Add the oat directory.
  dir += "oat";

  // Add the isa directory
  dir += "/" + std::string(GetInstructionSetString(isa));

  // Get the base part of the file without the extension.
  std::string file = location.substr(pos+1);
  pos = file.rfind('.');
  if (pos == std::string::npos) {
    *error_msg = "Dex location " + location + " has no extension.";
    return false;
  }
  std::string base = file.substr(0, pos);

  *odex_filename = dir + "/" + base + ".odex";
  return true;
}

bool OatFileAssistant::DexLocationToOatFilename(const std::string& location,
                                                InstructionSet isa,
                                                std::string* oat_filename,
                                                std::string* error_msg) {
  CHECK(oat_filename != nullptr);
  CHECK(error_msg != nullptr);

  // Check if `location` could have an oat file in the ART APEX data directory. If so, and the
  // file exists, use it.
  const std::string apex_data_file = GetApexDataOdexFilename(location, isa);
  if (!apex_data_file.empty()) {
    if (OS::FileExists(apex_data_file.c_str(), /*check_file_type=*/true)) {
      *oat_filename = apex_data_file;
      return true;
    } else if (errno != ENOENT) {
      PLOG(ERROR) << "Could not check odex file " << apex_data_file;
    }
  }

  // If ANDROID_DATA is not set, return false instead of aborting.
  // This can occur for preopt when using a class loader context.
  if (GetAndroidDataSafe(error_msg).empty()) {
    *error_msg = "GetAndroidDataSafe failed: " + *error_msg;
    return false;
  }

  std::string dalvik_cache;
  bool have_android_data = false;
  bool dalvik_cache_exists = false;
  bool is_global_cache = false;
  GetDalvikCache(GetInstructionSetString(isa),
                  /*create_if_absent=*/ true,
                  &dalvik_cache,
                  &have_android_data,
                  &dalvik_cache_exists,
                  &is_global_cache);
  if (!dalvik_cache_exists) {
    *error_msg = "Dalvik cache directory does not exist";
    return false;
  }

  // TODO: The oat file assistant should be the definitive place for
  // determining the oat file name from the dex location, not
  // GetDalvikCacheFilename.
  return GetDalvikCacheFilename(location.c_str(), dalvik_cache.c_str(), oat_filename, error_msg);
}

const std::vector<uint32_t>* OatFileAssistant::GetRequiredDexChecksums() {
  if (!required_dex_checksums_attempted_) {
    required_dex_checksums_attempted_ = true;
    required_dex_checksums_found_ = false;
    cached_required_dex_checksums_.clear();
    std::string error_msg;
    const ArtDexFileLoader dex_file_loader;
    std::vector<std::string> dex_locations_ignored;
    if (dex_file_loader.GetMultiDexChecksums(dex_location_.c_str(),
                                             &cached_required_dex_checksums_,
                                             &dex_locations_ignored,
                                             &error_msg,
                                             zip_fd_,
                                             &zip_file_only_contains_uncompressed_dex_)) {
      required_dex_checksums_found_ = true;
      has_original_dex_files_ = true;
    } else {
      // The only valid case here is for APKs without dex files.
      required_dex_checksums_found_ = false;
      has_original_dex_files_ = false;
      VLOG(oat) << "Could not get required checksum: " << error_msg;
    }
  }
  return required_dex_checksums_found_ ? &cached_required_dex_checksums_ : nullptr;
}

bool OatFileAssistant::ValidateBootClassPathChecksums(const OatFile& oat_file) {
  // Get the checksums and the BCP from the oat file.
  const char* oat_boot_class_path_checksums =
      oat_file.GetOatHeader().GetStoreValueByKey(OatHeader::kBootClassPathChecksumsKey);
  const char* oat_boot_class_path =
      oat_file.GetOatHeader().GetStoreValueByKey(OatHeader::kBootClassPathKey);
  if (oat_boot_class_path_checksums == nullptr || oat_boot_class_path == nullptr) {
    return false;
  }
  std::string_view oat_boot_class_path_checksums_view(oat_boot_class_path_checksums);
  std::string_view oat_boot_class_path_view(oat_boot_class_path);
  if (oat_boot_class_path_view == cached_boot_class_path_ &&
      oat_boot_class_path_checksums_view == cached_boot_class_path_checksums_) {
    return true;
  }

  Runtime* runtime = Runtime::Current();
  std::string error_msg;
  bool result = gc::space::ImageSpace::VerifyBootClassPathChecksums(
      oat_boot_class_path_checksums_view,
      oat_boot_class_path_view,
      runtime->GetImageLocation(),
      ArrayRef<const std::string>(runtime->GetBootClassPathLocations()),
      ArrayRef<const std::string>(runtime->GetBootClassPath()),
      isa_,
      &error_msg);
  if (!result) {
    VLOG(oat) << "Failed to verify checksums of oat file " << oat_file.GetLocation()
        << " error: " << error_msg;
    return false;
  }

  // This checksum has been validated, so save it.
  cached_boot_class_path_ = oat_boot_class_path_view;
  cached_boot_class_path_checksums_ = oat_boot_class_path_checksums_view;
  return true;
}

OatFileAssistant::OatFileInfo& OatFileAssistant::GetBestInfo() {
  ScopedTrace trace("GetBestInfo");
  // TODO(calin): Document the side effects of class loading when
  // running dalvikvm command line.
  if (dex_parent_writable_ || UseFdToReadFiles()) {
    // If the parent of the dex file is writable it means that we can
    // create the odex file. In this case we unconditionally pick the odex
    // as the best oat file. This corresponds to the regular use case when
    // apps gets installed or when they load private, secondary dex file.
    // For apps on the system partition the odex location will not be
    // writable and thus the oat location might be more up to date.

    // If the odex is not useable, and we have a useable vdex, return the vdex
    // instead.
    if (!odex_.IsUseable() && vdex_for_odex_.IsUseable()) {
      return vdex_for_odex_;
    }
    return odex_;
  }

  // We cannot write to the odex location. This must be a system app.

  // If the oat location is useable take it.
  if (oat_.IsUseable()) {
    return oat_;
  }

  // The oat file is not useable but the odex file might be up to date.
  // This is an indication that we are dealing with an up to date prebuilt
  // (that doesn't need relocation).
  if (odex_.IsUseable()) {
    return odex_;
  }

  // Look for a useable vdex file.
  if (vdex_for_oat_.IsUseable()) {
    return vdex_for_oat_;
  }
  if (vdex_for_odex_.IsUseable()) {
    return vdex_for_odex_;
  }

  // We got into the worst situation here:
  // - the oat location is not useable
  // - the prebuild odex location is not up to date
  // - the vdex-only file is not useable
  // - and we don't have the original dex file anymore (stripped).
  // Pick the odex if it exists, or the oat if not.
  return (odex_.Status() == kOatCannotOpen) ? oat_ : odex_;
}

std::unique_ptr<gc::space::ImageSpace> OatFileAssistant::OpenImageSpace(const OatFile* oat_file) {
  DCHECK(oat_file != nullptr);
  std::string art_file = ReplaceFileExtension(oat_file->GetLocation(), "art");
  if (art_file.empty()) {
    return nullptr;
  }
  std::string error_msg;
  ScopedObjectAccess soa(Thread::Current());
  std::unique_ptr<gc::space::ImageSpace> ret =
      gc::space::ImageSpace::CreateFromAppImage(art_file.c_str(), oat_file, &error_msg);
  if (ret == nullptr && (VLOG_IS_ON(image) || OS::FileExists(art_file.c_str()))) {
    LOG(INFO) << "Failed to open app image " << art_file.c_str() << " " << error_msg;
  }
  return ret;
}

OatFileAssistant::OatFileInfo::OatFileInfo(OatFileAssistant* oat_file_assistant,
                                           bool is_oat_location)
  : oat_file_assistant_(oat_file_assistant), is_oat_location_(is_oat_location)
{}

bool OatFileAssistant::OatFileInfo::IsOatLocation() {
  return is_oat_location_;
}

const std::string* OatFileAssistant::OatFileInfo::Filename() {
  return filename_provided_ ? &filename_ : nullptr;
}

bool OatFileAssistant::OatFileInfo::IsUseable() {
  ScopedTrace trace("IsUseable");
  switch (Status()) {
    case kOatCannotOpen:
    case kOatDexOutOfDate:
    case kOatContextOutOfDate:
    case kOatBootImageOutOfDate: return false;

    case kOatUpToDate: return true;
  }
  UNREACHABLE();
}

OatFileAssistant::OatStatus OatFileAssistant::OatFileInfo::Status() {
  ScopedTrace trace("Status");
  if (!status_attempted_) {
    status_attempted_ = true;
    const OatFile* file = GetFile();
    if (file == nullptr) {
      status_ = kOatCannotOpen;
    } else {
      status_ = oat_file_assistant_->GivenOatFileStatus(*file);
      VLOG(oat) << file->GetLocation() << " is " << status_
          << " with filter " << file->GetCompilerFilter();
    }
  }
  return status_;
}

OatFileAssistant::DexOptNeeded OatFileAssistant::OatFileInfo::GetDexOptNeeded(
    CompilerFilter::Filter target,
    bool profile_changed,
    bool downgrade) {

  if (IsUseable()) {
    return CompilerFilterIsOkay(target, profile_changed, downgrade)
        ? kNoDexOptNeeded
        : kDex2OatForFilter;
  }

  if (Status() == kOatBootImageOutOfDate) {
    return kDex2OatForBootImage;
  }

  if (oat_file_assistant_->HasDexFiles()) {
    return kDex2OatFromScratch;
  } else {
    // No dex file, there is nothing we need to do.
    return kNoDexOptNeeded;
  }
}

const OatFile* OatFileAssistant::OatFileInfo::GetFile() {
  CHECK(!file_released_) << "GetFile called after oat file released.";
  if (load_attempted_) {
    return file_.get();
  }
  load_attempted_ = true;
  if (!filename_provided_) {
    return nullptr;
  }

  std::string error_msg;
  bool executable = oat_file_assistant_->load_executable_;
  if (android::base::EndsWith(filename_, kVdexExtension)) {
    executable = false;
    // Check to see if there is a vdex file we can make use of.
    std::unique_ptr<VdexFile> vdex;
    if (use_fd_) {
      if (vdex_fd_ >= 0) {
        struct stat s;
        int rc = TEMP_FAILURE_RETRY(fstat(vdex_fd_, &s));
        if (rc == -1) {
          error_msg = StringPrintf("Failed getting length of the vdex file %s.", strerror(errno));
        } else {
          vdex = VdexFile::Open(vdex_fd_,
                                s.st_size,
                                filename_,
                                /*writable=*/ false,
                                /*low_4gb=*/ false,
                                /*unquicken=*/ false,
                                &error_msg);
        }
      }
    } else {
      vdex = VdexFile::Open(filename_,
                            /*writable=*/ false,
                            /*low_4gb=*/ false,
                            /*unquicken=*/ false,
                            &error_msg);
    }
    if (vdex == nullptr) {
      VLOG(oat) << "unable to open vdex file " << filename_ << ": " << error_msg;
    } else {
      file_.reset(OatFile::OpenFromVdex(zip_fd_,
                                        std::move(vdex),
                                        oat_file_assistant_->dex_location_,
                                        &error_msg));
    }
  } else {
    if (executable && oat_file_assistant_->only_load_system_executable_) {
      executable = LocationIsOnSystem(filename_.c_str());
    }
    VLOG(oat) << "Loading " << filename_ << " with executable: " << executable;
    if (use_fd_) {
      if (oat_fd_ >= 0 && vdex_fd_ >= 0) {
        ArrayRef<const std::string> dex_locations(&oat_file_assistant_->dex_location_,
                                                  /*size=*/ 1u);
        file_.reset(OatFile::Open(zip_fd_,
                                  vdex_fd_,
                                  oat_fd_,
                                  filename_.c_str(),
                                  executable,
                                  /*low_4gb=*/ false,
                                  dex_locations,
                                  /*reservation=*/ nullptr,
                                  &error_msg));
      }
    } else {
      file_.reset(OatFile::Open(/*zip_fd=*/ -1,
                                filename_.c_str(),
                                filename_.c_str(),
                                executable,
                                /*low_4gb=*/ false,
                                oat_file_assistant_->dex_location_,
                                &error_msg));
    }
  }
  if (file_.get() == nullptr) {
    VLOG(oat) << "OatFileAssistant test for existing oat file "
              << filename_
              << ": " << error_msg;
  } else {
    VLOG(oat) << "Successfully loaded " << filename_ << " with executable: " << executable;
  }
  return file_.get();
}

bool OatFileAssistant::OatFileInfo::CompilerFilterIsOkay(
    CompilerFilter::Filter target, bool profile_changed, bool downgrade) {
  const OatFile* file = GetFile();
  if (file == nullptr) {
    return false;
  }

  CompilerFilter::Filter current = file->GetCompilerFilter();
  if (profile_changed && CompilerFilter::DependsOnProfile(current)) {
    VLOG(oat) << "Compiler filter not okay because Profile changed";
    return false;
  }
  return downgrade ? !CompilerFilter::IsBetter(current, target) :
    CompilerFilter::IsAsGoodAs(current, target);
}

bool OatFileAssistant::ClassLoaderContextIsOkay(const OatFile& oat_file) const {
  if (oat_file.IsBackedByVdexOnly()) {
    // Only a vdex file, we don't depend on the class loader context.
    return true;
  }

  if (!CompilerFilter::IsVerificationEnabled(oat_file.GetCompilerFilter())) {
    // If verification is not enabled we don't need to verify the class loader context and we
    // assume it's ok.
    return true;
  }

  if (context_ == nullptr) {
    // When no class loader context is provided (which happens for deprecated
    // DexFile APIs), just assume it is OK.
    return true;
  }

  ClassLoaderContext::VerificationResult matches = context_->VerifyClassLoaderContextMatch(
      oat_file.GetClassLoaderContext(),
      /*verify_names=*/ true,
      /*verify_checksums=*/ true);
  if (matches == ClassLoaderContext::VerificationResult::kMismatch) {
    VLOG(oat) << "ClassLoaderContext check failed. Context was "
              << oat_file.GetClassLoaderContext()
              << ". The expected context is "
              << context_->EncodeContextForOatFile(android::base::Dirname(dex_location_));
    return false;
  }
  return true;
}

bool OatFileAssistant::OatFileInfo::IsExecutable() {
  const OatFile* file = GetFile();
  return (file != nullptr && file->IsExecutable());
}

void OatFileAssistant::OatFileInfo::Reset() {
  load_attempted_ = false;
  file_.reset();
  status_attempted_ = false;
}

void OatFileAssistant::OatFileInfo::Reset(const std::string& filename,
                                          bool use_fd,
                                          int zip_fd,
                                          int vdex_fd,
                                          int oat_fd) {
  filename_provided_ = true;
  filename_ = filename;
  use_fd_ = use_fd;
  zip_fd_ = zip_fd;
  vdex_fd_ = vdex_fd;
  oat_fd_ = oat_fd;
  Reset();
}

std::unique_ptr<OatFile> OatFileAssistant::OatFileInfo::ReleaseFile() {
  file_released_ = true;
  return std::move(file_);
}

std::unique_ptr<OatFile> OatFileAssistant::OatFileInfo::ReleaseFileForUse() {
  ScopedTrace trace("ReleaseFileForUse");
  if (Status() == kOatUpToDate) {
    return ReleaseFile();
  }

  return std::unique_ptr<OatFile>();
}

// TODO(calin): we could provide a more refined status here
// (e.g. run from uncompressed apk, run with vdex but not oat etc). It will allow us to
// track more experiments but adds extra complexity.
void OatFileAssistant::GetOptimizationStatus(
    const std::string& filename,
    InstructionSet isa,
    std::string* out_compilation_filter,
    std::string* out_compilation_reason) {
  // It may not be possible to load an oat file executable (e.g., selinux restrictions). Load
  // non-executable and check the status manually.
  OatFileAssistant oat_file_assistant(filename.c_str(),
                                      isa,
                                      /* context= */ nullptr,
                                      /*load_executable=*/ false);
  std::string out_odex_location;  // unused
  std::string out_odex_status;  // unused
  oat_file_assistant.GetOptimizationStatus(
      &out_odex_location,
      out_compilation_filter,
      out_compilation_reason,
      &out_odex_status);
}

void OatFileAssistant::GetOptimizationStatus(
    std::string* out_odex_location,
    std::string* out_compilation_filter,
    std::string* out_compilation_reason,
    std::string* out_odex_status) {
  OatFileInfo& oat_file_info = GetBestInfo();
  const OatFile* oat_file = GetBestInfo().GetFile();

  if (oat_file == nullptr) {
    *out_odex_location = "error";
    *out_compilation_filter = "run-from-apk";
    *out_compilation_reason = "unknown";
    // This mostly happens when we cannot open the oat file.
    // Note that it's different than kOatCannotOpen.
    // TODO: The design of getting the BestInfo is not ideal,
    // as it's not very clear what's the difference between
    // a nullptr and kOatcannotOpen. The logic should be revised
    // and improved.
    *out_odex_status = "io-error-no-oat";
    return;
  }

  *out_odex_location = oat_file->GetLocation();
  OatStatus status = oat_file_info.Status();
  const char* reason = oat_file->GetCompilationReason();
  *out_compilation_reason = reason == nullptr ? "unknown" : reason;
  switch (status) {
    case kOatUpToDate:
      *out_compilation_filter = CompilerFilter::NameOfFilter(oat_file->GetCompilerFilter());
      *out_odex_status = "up-to-date";
      return;

    case kOatCannotOpen:  // This should never happen, but be robust.
      *out_compilation_filter = "error";
      *out_compilation_reason = "error";
      // This mostly happens when we cannot open the vdex file,
      // or the file is corrupt.
      *out_odex_status = "io-error-or-corruption";
      return;

    case kOatBootImageOutOfDate:
      *out_compilation_filter = "run-from-apk-fallback";
      *out_odex_status = "boot-image-more-recent";
      return;

    case kOatContextOutOfDate:
      *out_compilation_filter = "run-from-apk-fallback";
      *out_odex_status = "context-mismatch";
      return;

    case kOatDexOutOfDate:
      *out_compilation_filter = "run-from-apk-fallback";
      *out_odex_status = "apk-more-recent";
      return;
  }
  LOG(FATAL) << "Unreachable";
  UNREACHABLE();
}

}  // namespace art
