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

#include "oat_file_manager.h"

#include <memory>
#include <queue>
#include <vector>
#include <sys/stat.h>

#include "android-base/file.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"

#include "art_field-inl.h"
#include "base/bit_vector-inl.h"
#include "base/file_utils.h"
#include "base/logging.h"  // For VLOG.
#include "base/mutex-inl.h"
#include "base/sdk_version.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "class_linker.h"
#include "class_loader_context.h"
#include "dex/art_dex_file_loader.h"
#include "dex/dex_file-inl.h"
#include "dex/dex_file_loader.h"
#include "dex/dex_file_tracking_registrar.h"
#include "gc/scoped_gc_critical_section.h"
#include "gc/space/image_space.h"
#include "handle_scope-inl.h"
#include "jit/jit.h"
#include "jni/java_vm_ext.h"
#include "jni/jni_internal.h"
#include "mirror/class_loader.h"
#include "mirror/object-inl.h"
#include "oat_file.h"
#include "oat_file_assistant.h"
#include "obj_ptr-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"
#include "thread_list.h"
#include "thread_pool.h"
#include "vdex_file.h"
#include "verifier/verifier_deps.h"
#include "well_known_classes.h"

namespace art {

using android::base::StringPrintf;

// If true, we attempt to load the application image if it exists.
static constexpr bool kEnableAppImage = true;

const OatFile* OatFileManager::RegisterOatFile(std::unique_ptr<const OatFile> oat_file) {
  // Use class_linker vlog to match the log for dex file registration.
  VLOG(class_linker) << "Registered oat file " << oat_file->GetLocation();
  PaletteHooks* hooks = nullptr;
  if (PaletteGetHooks(&hooks) == PALETTE_STATUS_OK) {
    hooks->NotifyOatFileLoaded(oat_file->GetLocation().c_str());
  }

  WriterMutexLock mu(Thread::Current(), *Locks::oat_file_manager_lock_);
  CHECK(!only_use_system_oat_files_ ||
        LocationIsOnSystem(oat_file->GetLocation().c_str()) ||
        !oat_file->IsExecutable())
      << "Registering a non /system oat file: " << oat_file->GetLocation();
  DCHECK(oat_file != nullptr);
  if (kIsDebugBuild) {
    CHECK(oat_files_.find(oat_file) == oat_files_.end());
    for (const std::unique_ptr<const OatFile>& existing : oat_files_) {
      CHECK_NE(oat_file.get(), existing.get()) << oat_file->GetLocation();
      // Check that we don't have an oat file with the same address. Copies of the same oat file
      // should be loaded at different addresses.
      CHECK_NE(oat_file->Begin(), existing->Begin()) << "Oat file already mapped at that location";
    }
  }
  const OatFile* ret = oat_file.get();
  oat_files_.insert(std::move(oat_file));
  return ret;
}

void OatFileManager::UnRegisterAndDeleteOatFile(const OatFile* oat_file) {
  WriterMutexLock mu(Thread::Current(), *Locks::oat_file_manager_lock_);
  DCHECK(oat_file != nullptr);
  std::unique_ptr<const OatFile> compare(oat_file);
  auto it = oat_files_.find(compare);
  CHECK(it != oat_files_.end());
  oat_files_.erase(it);
  compare.release();  // NOLINT b/117926937
}

const OatFile* OatFileManager::FindOpenedOatFileFromDexLocation(
    const std::string& dex_base_location) const {
  ReaderMutexLock mu(Thread::Current(), *Locks::oat_file_manager_lock_);
  for (const std::unique_ptr<const OatFile>& oat_file : oat_files_) {
    const std::vector<const OatDexFile*>& oat_dex_files = oat_file->GetOatDexFiles();
    for (const OatDexFile* oat_dex_file : oat_dex_files) {
      if (DexFileLoader::GetBaseLocation(oat_dex_file->GetDexFileLocation()) == dex_base_location) {
        return oat_file.get();
      }
    }
  }
  return nullptr;
}

const OatFile* OatFileManager::FindOpenedOatFileFromOatLocation(const std::string& oat_location)
    const {
  ReaderMutexLock mu(Thread::Current(), *Locks::oat_file_manager_lock_);
  return FindOpenedOatFileFromOatLocationLocked(oat_location);
}

const OatFile* OatFileManager::FindOpenedOatFileFromOatLocationLocked(
    const std::string& oat_location) const {
  for (const std::unique_ptr<const OatFile>& oat_file : oat_files_) {
    if (oat_file->GetLocation() == oat_location) {
      return oat_file.get();
    }
  }
  return nullptr;
}

std::vector<const OatFile*> OatFileManager::GetBootOatFiles() const {
  std::vector<gc::space::ImageSpace*> image_spaces =
      Runtime::Current()->GetHeap()->GetBootImageSpaces();
  std::vector<const OatFile*> oat_files;
  oat_files.reserve(image_spaces.size());
  for (gc::space::ImageSpace* image_space : image_spaces) {
    oat_files.push_back(image_space->GetOatFile());
  }
  return oat_files;
}

bool OatFileManager::GetPrimaryOatFileInfo(std::string* compilation_reason,
                                           CompilerFilter::Filter* compiler_filter) const {
  ReaderMutexLock mu(Thread::Current(), *Locks::oat_file_manager_lock_);
  std::vector<const OatFile*> boot_oat_files = GetBootOatFiles();
  if (!boot_oat_files.empty()) {
    for (const std::unique_ptr<const OatFile>& oat_file : oat_files_) {
      if (std::find(boot_oat_files.begin(), boot_oat_files.end(), oat_file.get()) ==
          boot_oat_files.end()) {
        const char* reason = oat_file->GetCompilationReason();
        if (reason != nullptr) {
          *compilation_reason = reason;
        }
        *compiler_filter = oat_file->GetCompilerFilter();
        return true;
      }
    }
  }
  return false;
}

OatFileManager::OatFileManager()
    : only_use_system_oat_files_(false) {}

OatFileManager::~OatFileManager() {
  // Explicitly clear oat_files_ since the OatFile destructor calls back into OatFileManager for
  // UnRegisterOatFileLocation.
  oat_files_.clear();
}

std::vector<const OatFile*> OatFileManager::RegisterImageOatFiles(
    const std::vector<gc::space::ImageSpace*>& spaces) {
  std::vector<const OatFile*> oat_files;
  oat_files.reserve(spaces.size());
  for (gc::space::ImageSpace* space : spaces) {
    oat_files.push_back(RegisterOatFile(space->ReleaseOatFile()));
  }
  return oat_files;
}

static bool ClassLoaderContextMatches(
    const OatFile* oat_file,
    const ClassLoaderContext* context,
    /*out*/ std::string* error_msg) {
  DCHECK(oat_file != nullptr);
  DCHECK(error_msg != nullptr);
  DCHECK(context != nullptr);

  if (oat_file->IsBackedByVdexOnly()) {
    // Only a vdex file, we don't depend on the class loader context.
    return true;
  }

  if (!CompilerFilter::IsVerificationEnabled(oat_file->GetCompilerFilter())) {
    // If verification is not enabled we don't need to check if class loader context matches
    // as the oat file is either extracted or assumed verified.
    return true;
  }

  // If the oat file loading context matches the context used during compilation then we accept
  // the oat file without addition checks
  ClassLoaderContext::VerificationResult result = context->VerifyClassLoaderContextMatch(
      oat_file->GetClassLoaderContext(),
      /*verify_names=*/ true,
      /*verify_checksums=*/ true);
  switch (result) {
    case ClassLoaderContext::VerificationResult::kMismatch:
      return false;
    case ClassLoaderContext::VerificationResult::kVerifies:
      return true;
  }
  LOG(FATAL) << "Unreachable";
}

bool OatFileManager::ShouldLoadAppImage(const OatFile* source_oat_file) const {
  Runtime* const runtime = Runtime::Current();
  return kEnableAppImage && (!runtime->IsJavaDebuggable() || source_oat_file->IsDebuggable());
}

std::vector<std::unique_ptr<const DexFile>> OatFileManager::OpenDexFilesFromOat(
    const char* dex_location,
    jobject class_loader,
    jobjectArray dex_elements,
    const OatFile** out_oat_file,
    std::vector<std::string>* error_msgs) {
  ScopedTrace trace(StringPrintf("%s(%s)", __FUNCTION__, dex_location));
  CHECK(dex_location != nullptr);
  CHECK(error_msgs != nullptr);

  // Verify we aren't holding the mutator lock, which could starve GC when
  // hitting the disk.
  Thread* const self = Thread::Current();
  Locks::mutator_lock_->AssertNotHeld(self);
  Runtime* const runtime = Runtime::Current();

  std::vector<std::unique_ptr<const DexFile>> dex_files;

  // If the class_loader is null there's not much we can do. This happens if a dex files is loaded
  // directly with DexFile APIs instead of using class loaders.
  if (class_loader == nullptr) {
    LOG(WARNING) << "Opening an oat file without a class loader. "
                 << "Are you using the deprecated DexFile APIs?";
  } else {
    std::unique_ptr<ClassLoaderContext> context(
        ClassLoaderContext::CreateContextForClassLoader(class_loader, dex_elements));

    OatFileAssistant oat_file_assistant(dex_location,
                                        kRuntimeISA,
                                        runtime->GetOatFilesExecutable(),
                                        only_use_system_oat_files_);

    // Get the current optimization status for trace debugging.
    // Implementation detail note: GetOptimizationStatus will select the same
    // oat file as GetBestOatFile used below, and in doing so it already pre-populates
    // some OatFileAssistant internal fields.
    std::string odex_location;
    std::string compilation_filter;
    std::string compilation_reason;
    std::string odex_status;
    oat_file_assistant.GetOptimizationStatus(
        &odex_location,
        &compilation_filter,
        &compilation_reason,
        &odex_status);

    ScopedTrace odex_loading(StringPrintf(
        "location=%s status=%s filter=%s reason=%s",
        odex_location.c_str(),
        odex_status.c_str(),
        compilation_filter.c_str(),
        compilation_reason.c_str()));

    // Proceed with oat file loading.
    std::unique_ptr<const OatFile> oat_file(oat_file_assistant.GetBestOatFile().release());
    VLOG(oat) << "OatFileAssistant(" << dex_location << ").GetBestOatFile()="
              << (oat_file != nullptr ? oat_file->GetLocation() : "")
              << " (executable=" << (oat_file != nullptr ? oat_file->IsExecutable() : false) << ")";

    CHECK(oat_file == nullptr || odex_location == oat_file->GetLocation())
        << "OatFileAssistant non-determinism in choosing best oat files. "
        << "optimization-status-location=" << odex_location
        << " best_oat_file-location=" << oat_file->GetLocation();

    const OatFile* source_oat_file = nullptr;
    std::string error_msg;
    bool class_loader_context_matches = false;
    bool check_context = oat_file != nullptr && context != nullptr;
    if (check_context) {
        class_loader_context_matches =
            ClassLoaderContextMatches(oat_file.get(),
                                      context.get(),
                                      /*out*/ &error_msg);
    }
    ScopedTrace context_results(StringPrintf(
        "check_context=%s contex-ok=%s",
        check_context ? "true" : "false",
        class_loader_context_matches ? "true" : "false"));

    if (class_loader_context_matches) {
      // Load the dex files from the oat file.
      bool added_image_space = false;
      if (oat_file->IsExecutable()) {
        ScopedTrace app_image_timing("AppImage:Loading");

        // We need to throw away the image space if we are debuggable but the oat-file source of the
        // image is not otherwise we might get classes with inlined methods or other such things.
        std::unique_ptr<gc::space::ImageSpace> image_space;
        if (ShouldLoadAppImage(oat_file.get())) {
          image_space = oat_file_assistant.OpenImageSpace(oat_file.get());
        }
        if (image_space != nullptr) {
          ScopedObjectAccess soa(self);
          StackHandleScope<1> hs(self);
          Handle<mirror::ClassLoader> h_loader(
              hs.NewHandle(soa.Decode<mirror::ClassLoader>(class_loader)));
          // Can not load app image without class loader.
          if (h_loader != nullptr) {
            std::string temp_error_msg;
            // Add image space has a race condition since other threads could be reading from the
            // spaces array.
            {
              ScopedThreadSuspension sts(self, kSuspended);
              gc::ScopedGCCriticalSection gcs(self,
                                              gc::kGcCauseAddRemoveAppImageSpace,
                                              gc::kCollectorTypeAddRemoveAppImageSpace);
              ScopedSuspendAll ssa("Add image space");
              runtime->GetHeap()->AddSpace(image_space.get());
            }
            {
              ScopedTrace image_space_timing("Adding image space");
              added_image_space = runtime->GetClassLinker()->AddImageSpace(image_space.get(),
                                                                           h_loader,
                                                                           /*out*/&dex_files,
                                                                           /*out*/&temp_error_msg);
            }
            if (added_image_space) {
              // Successfully added image space to heap, release the map so that it does not get
              // freed.
              image_space.release();  // NOLINT b/117926937

              // Register for tracking.
              for (const auto& dex_file : dex_files) {
                dex::tracking::RegisterDexFile(dex_file.get());
              }
            } else {
              LOG(INFO) << "Failed to add image file " << temp_error_msg;
              dex_files.clear();
              {
                ScopedThreadSuspension sts(self, kSuspended);
                gc::ScopedGCCriticalSection gcs(self,
                                                gc::kGcCauseAddRemoveAppImageSpace,
                                                gc::kCollectorTypeAddRemoveAppImageSpace);
                ScopedSuspendAll ssa("Remove image space");
                runtime->GetHeap()->RemoveSpace(image_space.get());
              }
              // Non-fatal, don't update error_msg.
            }
          }
        }
      }
      if (!added_image_space) {
        DCHECK(dex_files.empty());

        if (oat_file->RequiresImage()) {
          VLOG(oat) << "Loading "
                    << oat_file->GetLocation()
                    << "non-executable as it requires an image which we failed to load";
          // file as non-executable.
          OatFileAssistant nonexecutable_oat_file_assistant(dex_location,
                                                            kRuntimeISA,
                                                            /*load_executable=*/false,
                                                            only_use_system_oat_files_);
          oat_file.reset(nonexecutable_oat_file_assistant.GetBestOatFile().release());
        }

        dex_files = oat_file_assistant.LoadDexFiles(*oat_file.get(), dex_location);

        // Register for tracking.
        for (const auto& dex_file : dex_files) {
          dex::tracking::RegisterDexFile(dex_file.get());
        }
      }
      if (dex_files.empty()) {
        ScopedTrace failed_to_open_dex_files("FailedToOpenDexFilesFromOat");
        error_msgs->push_back("Failed to open dex files from " + oat_file->GetLocation());
      } else {
        // Opened dex files from an oat file, madvise them to their loaded state.
         for (const std::unique_ptr<const DexFile>& dex_file : dex_files) {
           OatDexFile::MadviseDexFile(*dex_file, MadviseState::kMadviseStateAtLoad);
         }
      }

      VLOG(class_linker) << "Registering " << oat_file->GetLocation();
      source_oat_file = RegisterOatFile(std::move(oat_file));
      *out_oat_file = source_oat_file;
    } else if (!error_msg.empty()) {
      LOG(WARNING) << error_msg;
    }

    // Verify if any of the dex files being loaded is already in the class path.
    // If so, report an error with the current stack trace.
    // Most likely the developer didn't intend to do this because it will waste
    // performance and memory.
    if (context != nullptr && !class_loader_context_matches) {
      std::set<const DexFile*> already_exists_in_classpath =
          context->CheckForDuplicateDexFiles(MakeNonOwningPointerVector(dex_files));
      if (!already_exists_in_classpath.empty()) {
        ScopedTrace duplicate_dex_files("DuplicateDexFilesInContext");
        auto duplicate_it = already_exists_in_classpath.begin();
        std::string duplicates = (*duplicate_it)->GetLocation();
        for (duplicate_it++ ; duplicate_it != already_exists_in_classpath.end(); duplicate_it++) {
          duplicates += "," + (*duplicate_it)->GetLocation();
        }

        std::ostringstream out;
        out << "Trying to load dex files which is already loaded in the same ClassLoader "
            << "hierarchy.\n"
            << "This is a strong indication of bad ClassLoader construct which leads to poor "
            << "performance and wastes memory.\n"
            << "The list of duplicate dex files is: " << duplicates << "\n"
            << "The current class loader context is: "
            << context->EncodeContextForOatFile("") << "\n"
            << "Java stack trace:\n";

        {
          ScopedObjectAccess soa(self);
          self->DumpJavaStack(out);
        }

        // We log this as an ERROR to stress the fact that this is most likely unintended.
        // Note that ART cannot do anything about it. It is up to the app to fix their logic.
        // Here we are trying to give a heads up on why the app might have performance issues.
        LOG(ERROR) << out.str();
      }
    }
  }

  // If we arrive here with an empty dex files list, it means we fail to load
  // it/them through an .oat file.
  if (dex_files.empty()) {
    std::string error_msg;
    static constexpr bool kVerifyChecksum = true;
    const ArtDexFileLoader dex_file_loader;
    if (!dex_file_loader.Open(dex_location,
                              dex_location,
                              Runtime::Current()->IsVerificationEnabled(),
                              kVerifyChecksum,
                              /*out*/ &error_msg,
                              &dex_files)) {
      ScopedTrace fail_to_open_dex_from_apk("FailedToOpenDexFilesFromApk");
      LOG(WARNING) << error_msg;
      error_msgs->push_back("Failed to open dex files from " + std::string(dex_location)
                            + " because: " + error_msg);
    }
  }

  if (Runtime::Current()->GetJit() != nullptr) {
    Runtime::Current()->GetJit()->RegisterDexFiles(dex_files, class_loader);
  }

  return dex_files;
}

static std::vector<const DexFile::Header*> GetDexFileHeaders(const std::vector<MemMap>& maps) {
  std::vector<const DexFile::Header*> headers;
  headers.reserve(maps.size());
  for (const MemMap& map : maps) {
    DCHECK(map.IsValid());
    headers.push_back(reinterpret_cast<const DexFile::Header*>(map.Begin()));
  }
  return headers;
}

std::vector<std::unique_ptr<const DexFile>> OatFileManager::OpenDexFilesFromOat(
    std::vector<MemMap>&& dex_mem_maps,
    jobject class_loader,
    jobjectArray dex_elements,
    const OatFile** out_oat_file,
    std::vector<std::string>* error_msgs) {
  std::vector<std::unique_ptr<const DexFile>> dex_files = OpenDexFilesFromOat_Impl(
      std::move(dex_mem_maps),
      class_loader,
      dex_elements,
      out_oat_file,
      error_msgs);

  if (error_msgs->empty()) {
    // Remove write permission from DexFile pages. We do this at the end because
    // OatFile assigns OatDexFile pointer in the DexFile objects.
    for (std::unique_ptr<const DexFile>& dex_file : dex_files) {
      if (!dex_file->DisableWrite()) {
        error_msgs->push_back("Failed to make dex file " + dex_file->GetLocation() + " read-only");
      }
    }
  }

  if (!error_msgs->empty()) {
    return std::vector<std::unique_ptr<const DexFile>>();
  }

  return dex_files;
}

std::vector<std::unique_ptr<const DexFile>> OatFileManager::OpenDexFilesFromOat_Impl(
    std::vector<MemMap>&& dex_mem_maps,
    jobject class_loader,
    jobjectArray dex_elements,
    const OatFile** out_oat_file,
    std::vector<std::string>* error_msgs) {
  ScopedTrace trace(__FUNCTION__);
  std::string error_msg;
  DCHECK(error_msgs != nullptr);

  // Extract dex file headers from `dex_mem_maps`.
  const std::vector<const DexFile::Header*> dex_headers = GetDexFileHeaders(dex_mem_maps);

  // Determine dex/vdex locations and the combined location checksum.
  std::string dex_location;
  std::string vdex_path;
  bool has_vdex = OatFileAssistant::AnonymousDexVdexLocation(dex_headers,
                                                             kRuntimeISA,
                                                             &dex_location,
                                                             &vdex_path);

  // Attempt to open an existing vdex and check dex file checksums match.
  std::unique_ptr<VdexFile> vdex_file = nullptr;
  if (has_vdex && OS::FileExists(vdex_path.c_str())) {
    vdex_file = VdexFile::Open(vdex_path,
                               /* writable= */ false,
                               /* low_4gb= */ false,
                               /* unquicken= */ false,
                               &error_msg);
    if (vdex_file == nullptr) {
      LOG(WARNING) << "Failed to open vdex " << vdex_path << ": " << error_msg;
    } else if (!vdex_file->MatchesDexFileChecksums(dex_headers)) {
      LOG(WARNING) << "Failed to open vdex " << vdex_path << ": dex file checksum mismatch";
      vdex_file.reset(nullptr);
    }
  }

  // Load dex files. Skip structural dex file verification if vdex was found
  // and dex checksums matched.
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  for (size_t i = 0; i < dex_mem_maps.size(); ++i) {
    static constexpr bool kVerifyChecksum = true;
    const ArtDexFileLoader dex_file_loader;
    std::unique_ptr<const DexFile> dex_file(dex_file_loader.Open(
        DexFileLoader::GetMultiDexLocation(i, dex_location.c_str()),
        dex_headers[i]->checksum_,
        std::move(dex_mem_maps[i]),
        /* verify= */ (vdex_file == nullptr) && Runtime::Current()->IsVerificationEnabled(),
        kVerifyChecksum,
        &error_msg));
    if (dex_file != nullptr) {
      dex::tracking::RegisterDexFile(dex_file.get());  // Register for tracking.
      dex_files.push_back(std::move(dex_file));
    } else {
      error_msgs->push_back("Failed to open dex files from memory: " + error_msg);
    }
  }

  // Check if we should proceed to creating an OatFile instance backed by the vdex.
  // We need: (a) an existing vdex, (b) class loader (can be null if invoked via reflection),
  // and (c) no errors during dex file loading.
  if (vdex_file == nullptr || class_loader == nullptr || !error_msgs->empty()) {
    return dex_files;
  }

  // Attempt to create a class loader context, check OpenDexFiles succeeds (prerequisite
  // for using the context later).
  std::unique_ptr<ClassLoaderContext> context = ClassLoaderContext::CreateContextForClassLoader(
      class_loader,
      dex_elements);
  if (context == nullptr) {
    LOG(ERROR) << "Could not create class loader context for " << vdex_path;
    return dex_files;
  }
  DCHECK(context->OpenDexFiles())
      << "Context created from already opened dex files should not attempt to open again";

  // Initialize an OatFile instance backed by the loaded vdex.
  std::unique_ptr<OatFile> oat_file(OatFile::OpenFromVdex(MakeNonOwningPointerVector(dex_files),
                                                          std::move(vdex_file),
                                                          dex_location));
  if (oat_file != nullptr) {
    VLOG(class_linker) << "Registering " << oat_file->GetLocation();
    *out_oat_file = RegisterOatFile(std::move(oat_file));
  }
  return dex_files;
}

// Check how many vdex files exist in the same directory as the vdex file we are about
// to write. If more than or equal to kAnonymousVdexCacheSize, unlink the least
// recently used one(s) (according to stat-reported atime).
static bool UnlinkLeastRecentlyUsedVdexIfNeeded(const std::string& vdex_path_to_add,
                                                std::string* error_msg) {
  std::string basename = android::base::Basename(vdex_path_to_add);
  if (!OatFileAssistant::IsAnonymousVdexBasename(basename)) {
    // File is not for in memory dex files.
    return true;
  }

  if (OS::FileExists(vdex_path_to_add.c_str())) {
    // File already exists and will be overwritten.
    // This will not change the number of entries in the cache.
    return true;
  }

  auto last_slash = vdex_path_to_add.rfind('/');
  CHECK(last_slash != std::string::npos);
  std::string vdex_dir = vdex_path_to_add.substr(0, last_slash + 1);

  if (!OS::DirectoryExists(vdex_dir.c_str())) {
    // Folder does not exist yet. Cache has zero entries.
    return true;
  }

  std::vector<std::pair<time_t, std::string>> cache;

  DIR* c_dir = opendir(vdex_dir.c_str());
  if (c_dir == nullptr) {
    *error_msg = "Unable to open " + vdex_dir + " to delete unused vdex files";
    return false;
  }
  for (struct dirent* de = readdir(c_dir); de != nullptr; de = readdir(c_dir)) {
    if (de->d_type != DT_REG) {
      continue;
    }
    basename = de->d_name;
    if (!OatFileAssistant::IsAnonymousVdexBasename(basename)) {
      continue;
    }
    std::string fullname = vdex_dir + basename;

    struct stat s;
    int rc = TEMP_FAILURE_RETRY(stat(fullname.c_str(), &s));
    if (rc == -1) {
      *error_msg = "Failed to stat() anonymous vdex file " + fullname;
      return false;
    }

    cache.push_back(std::make_pair(s.st_atime, fullname));
  }
  CHECK_EQ(0, closedir(c_dir)) << "Unable to close directory.";

  if (cache.size() < OatFileManager::kAnonymousVdexCacheSize) {
    return true;
  }

  std::sort(cache.begin(),
            cache.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  for (size_t i = OatFileManager::kAnonymousVdexCacheSize - 1; i < cache.size(); ++i) {
    if (unlink(cache[i].second.c_str()) != 0) {
      *error_msg = "Could not unlink anonymous vdex file " + cache[i].second;
      return false;
    }
  }

  return true;
}

class BackgroundVerificationTask final : public Task {
 public:
  BackgroundVerificationTask(const std::vector<const DexFile*>& dex_files,
                             jobject class_loader,
                             const std::string& vdex_path)
      : dex_files_(dex_files),
        vdex_path_(vdex_path) {
    Thread* const self = Thread::Current();
    ScopedObjectAccess soa(self);
    // Create a global ref for `class_loader` because it will be accessed from a different thread.
    class_loader_ = soa.Vm()->AddGlobalRef(self, soa.Decode<mirror::ClassLoader>(class_loader));
    CHECK(class_loader_ != nullptr);
  }

  ~BackgroundVerificationTask() {
    Thread* const self = Thread::Current();
    ScopedObjectAccess soa(self);
    soa.Vm()->DeleteGlobalRef(self, class_loader_);
  }

  void Run(Thread* self) override {
    std::string error_msg;
    ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
    verifier::VerifierDeps verifier_deps(dex_files_);

    // Iterate over all classes and verify them.
    for (const DexFile* dex_file : dex_files_) {
      for (uint32_t cdef_idx = 0; cdef_idx < dex_file->NumClassDefs(); cdef_idx++) {
        const dex::ClassDef& class_def = dex_file->GetClassDef(cdef_idx);

        // Take handles inside the loop. The background verification is low priority
        // and we want to minimize the risk of blocking anyone else.
        ScopedObjectAccess soa(self);
        StackHandleScope<2> hs(self);
        Handle<mirror::ClassLoader> h_loader(hs.NewHandle(
            soa.Decode<mirror::ClassLoader>(class_loader_)));
        Handle<mirror::Class> h_class(hs.NewHandle<mirror::Class>(class_linker->FindClass(
            self,
            dex_file->GetClassDescriptor(class_def),
            h_loader)));

        if (h_class == nullptr) {
          CHECK(self->IsExceptionPending());
          self->ClearException();
          continue;
        }

        if (&h_class->GetDexFile() != dex_file) {
          // There is a different class in the class path or a parent class loader
          // with the same descriptor. This `h_class` is not resolvable, skip it.
          continue;
        }

        CHECK(h_class->IsResolved()) << h_class->PrettyDescriptor();
        class_linker->VerifyClass(self, &verifier_deps, h_class);
        if (h_class->IsErroneous()) {
          // ClassLinker::VerifyClass throws, which isn't useful here.
          CHECK(soa.Self()->IsExceptionPending());
          soa.Self()->ClearException();
        }

        CHECK(h_class->IsVerified() || h_class->IsErroneous())
            << h_class->PrettyDescriptor() << ": state=" << h_class->GetStatus();

        if (h_class->IsVerified()) {
          verifier_deps.RecordClassVerified(*dex_file, class_def);
        }
      }
    }

    // Delete old vdex files if there are too many in the folder.
    if (!UnlinkLeastRecentlyUsedVdexIfNeeded(vdex_path_, &error_msg)) {
      LOG(ERROR) << "Could not unlink old vdex files " << vdex_path_ << ": " << error_msg;
      return;
    }

    // Construct a vdex file and write `verifier_deps` into it.
    if (!VdexFile::WriteToDisk(vdex_path_,
                               dex_files_,
                               verifier_deps,
                               &error_msg)) {
      LOG(ERROR) << "Could not write anonymous vdex " << vdex_path_ << ": " << error_msg;
      return;
    }
  }

  void Finalize() override {
    delete this;
  }

 private:
  const std::vector<const DexFile*> dex_files_;
  jobject class_loader_;
  const std::string vdex_path_;

  DISALLOW_COPY_AND_ASSIGN(BackgroundVerificationTask);
};

void OatFileManager::RunBackgroundVerification(const std::vector<const DexFile*>& dex_files,
                                               jobject class_loader) {
  Runtime* const runtime = Runtime::Current();
  Thread* const self = Thread::Current();

  if (runtime->IsJavaDebuggable()) {
    // Threads created by ThreadPool ("runtime threads") are not allowed to load
    // classes when debuggable to match class-initialization semantics
    // expectations. Do not verify in the background.
    return;
  }

  if (!IsSdkVersionSetAndAtLeast(runtime->GetTargetSdkVersion(), SdkVersion::kQ)) {
    // Do not run for legacy apps as they may depend on the previous class loader behaviour.
    return;
  }

  if (runtime->IsShuttingDown(self)) {
    // Not allowed to create new threads during runtime shutdown.
    return;
  }

  if (dex_files.size() < 1) {
    // Nothing to verify.
    return;
  }

  std::string dex_location = dex_files[0]->GetLocation();
  const std::string& data_dir = Runtime::Current()->GetProcessDataDirectory();
  if (!android::base::StartsWith(dex_location, data_dir)) {
    // For now, we only run background verification for secondary dex files.
    // Running it for primary or split APKs could have some undesirable
    // side-effects, like overloading the device on app startup.
    return;
  }

  std::string error_msg;
  std::string odex_filename;
  if (!OatFileAssistant::DexLocationToOdexFilename(dex_location,
                                                   kRuntimeISA,
                                                   &odex_filename,
                                                   &error_msg)) {
    LOG(WARNING) << "Could not get odex filename for " << dex_location << ": " << error_msg;
    return;
  }

  {
    WriterMutexLock mu(self, *Locks::oat_file_manager_lock_);
    if (verification_thread_pool_ == nullptr) {
      verification_thread_pool_.reset(
          new ThreadPool("Verification thread pool", /* num_threads= */ 1));
      verification_thread_pool_->StartWorkers(self);
    }
  }
  verification_thread_pool_->AddTask(self, new BackgroundVerificationTask(
      dex_files,
      class_loader,
      GetVdexFilename(odex_filename)));
}

void OatFileManager::WaitForWorkersToBeCreated() {
  DCHECK(!Runtime::Current()->IsShuttingDown(Thread::Current()))
      << "Cannot create new threads during runtime shutdown";
  if (verification_thread_pool_ != nullptr) {
    verification_thread_pool_->WaitForWorkersToBeCreated();
  }
}

void OatFileManager::DeleteThreadPool() {
  verification_thread_pool_.reset(nullptr);
}

void OatFileManager::WaitForBackgroundVerificationTasks() {
  if (verification_thread_pool_ != nullptr) {
    Thread* const self = Thread::Current();
    verification_thread_pool_->WaitForWorkersToBeCreated();
    verification_thread_pool_->Wait(self, /* do_work= */ true, /* may_hold_locks= */ false);
  }
}

void OatFileManager::SetOnlyUseSystemOatFiles() {
  ReaderMutexLock mu(Thread::Current(), *Locks::oat_file_manager_lock_);
  // Make sure all files that were loaded up to this point are on /system.
  // Skip the image files as they can encode locations that don't exist (eg not
  // containing the arch in the path, or for JIT zygote /nonx/existent).
  std::vector<const OatFile*> boot_vector = GetBootOatFiles();
  std::unordered_set<const OatFile*> boot_set(boot_vector.begin(), boot_vector.end());

  for (const std::unique_ptr<const OatFile>& oat_file : oat_files_) {
    if (boot_set.find(oat_file.get()) == boot_set.end()) {
      if (!LocationIsOnSystem(oat_file->GetLocation().c_str())) {
        // When the file is not on system, we check whether the oat file has any
        // AOT or DEX code. It is a fatal error if it has.
        if (CompilerFilter::IsAotCompilationEnabled(oat_file->GetCompilerFilter()) ||
            oat_file->ContainsDexCode()) {
          LOG(FATAL) << "Executing untrusted code from " << oat_file->GetLocation();
        }
      }
    }
  }
  only_use_system_oat_files_ = true;
}

void OatFileManager::DumpForSigQuit(std::ostream& os) {
  ReaderMutexLock mu(Thread::Current(), *Locks::oat_file_manager_lock_);
  std::vector<const OatFile*> boot_oat_files = GetBootOatFiles();
  for (const std::unique_ptr<const OatFile>& oat_file : oat_files_) {
    if (ContainsElement(boot_oat_files, oat_file.get())) {
      continue;
    }
    os << oat_file->GetLocation() << ": " << oat_file->GetCompilerFilter() << "\n";
  }
}

}  // namespace art
