/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include "common_art_test.h"

#include <cstdio>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <ftw.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>
#include "android-base/file.h"
#include "android-base/logging.h"
#include "nativehelper/scoped_local_ref.h"

#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "android-base/unique_fd.h"

#include "art_field-inl.h"
#include "base/file_utils.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/mem_map.h"
#include "base/mutex.h"
#include "base/os.h"
#include "base/runtime_debug.h"
#include "base/stl_util.h"
#include "base/string_view_cpp20.h"
#include "base/unix_file/fd_file.h"
#include "dex/art_dex_file_loader.h"
#include "dex/dex_file-inl.h"
#include "dex/dex_file_loader.h"
#include "dex/primitive.h"
#include "gtest/gtest.h"

namespace art {

using android::base::StringPrintf;

ScratchDir::ScratchDir(bool keep_files) : keep_files_(keep_files) {
  // ANDROID_DATA needs to be set
  CHECK_NE(static_cast<char*>(nullptr), getenv("ANDROID_DATA")) <<
      "Are you subclassing RuntimeTest?";
  path_ = getenv("ANDROID_DATA");
  path_ += "/tmp-XXXXXX";
  bool ok = (mkdtemp(&path_[0]) != nullptr);
  CHECK(ok) << strerror(errno) << " for " << path_;
  path_ += "/";
}

ScratchDir::~ScratchDir() {
  if (!keep_files_) {
    // Recursively delete the directory and all its content.
    nftw(path_.c_str(), [](const char* name, const struct stat*, int type, struct FTW *) {
      if (type == FTW_F) {
        unlink(name);
      } else if (type == FTW_DP) {
        rmdir(name);
      }
      return 0;
    }, 256 /* max open file descriptors */, FTW_DEPTH);
  }
}

ScratchFile::ScratchFile() {
  // ANDROID_DATA needs to be set
  CHECK_NE(static_cast<char*>(nullptr), getenv("ANDROID_DATA")) <<
      "Are you subclassing RuntimeTest?";
  filename_ = getenv("ANDROID_DATA");
  filename_ += "/TmpFile-XXXXXX";
  int fd = mkstemp(&filename_[0]);
  CHECK_NE(-1, fd) << strerror(errno) << " for " << filename_;
  file_.reset(new File(fd, GetFilename(), true));
}

ScratchFile::ScratchFile(const ScratchFile& other, const char* suffix)
    : ScratchFile(other.GetFilename() + suffix) {}

ScratchFile::ScratchFile(const std::string& filename) : filename_(filename) {
  int fd = open(filename_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
  CHECK_NE(-1, fd);
  file_.reset(new File(fd, GetFilename(), true));
}

ScratchFile::ScratchFile(File* file) {
  CHECK(file != nullptr);
  filename_ = file->GetPath();
  file_.reset(file);
}

ScratchFile::ScratchFile(ScratchFile&& other) noexcept {
  *this = std::move(other);
}

ScratchFile& ScratchFile::operator=(ScratchFile&& other) noexcept {
  if (GetFile() != other.GetFile()) {
    std::swap(filename_, other.filename_);
    std::swap(file_, other.file_);
  }
  return *this;
}

ScratchFile::~ScratchFile() {
  Unlink();
}

int ScratchFile::GetFd() const {
  return file_->Fd();
}

void ScratchFile::Close() {
  if (file_ != nullptr) {
    if (file_->FlushCloseOrErase() != 0) {
      PLOG(WARNING) << "Error closing scratch file.";
    }
    file_.reset();
  }
}

void ScratchFile::Unlink() {
  if (!OS::FileExists(filename_.c_str())) {
    return;
  }
  Close();
  int unlink_result = unlink(filename_.c_str());
  CHECK_EQ(0, unlink_result);
}

std::string CommonArtTestImpl::GetAndroidBuildTop() {
  CHECK(IsHost());
  std::string android_build_top;

  // Look at how we were invoked to find the expected directory.
  std::string argv;
  if (android::base::ReadFileToString("/proc/self/cmdline", &argv)) {
    // /proc/self/cmdline is the programs 'argv' with elements delimited by '\0'.
    std::filesystem::path path(argv.substr(0, argv.find('\0')));
    path = std::filesystem::absolute(path);
    // Walk up until we find the one of the well-known directories.
    for (; path.parent_path() != path; path = path.parent_path()) {
      // We are running tests from out/host/linux-x86 on developer machine.
      if (path.filename() == std::filesystem::path("linux-x86")) {
        android_build_top = path.parent_path().parent_path().parent_path();
        break;
      }
      // We are running tests from testcases (extracted from zip) on tradefed.
      // The first path is for remote runs and the second path for local runs.
      if (path.filename() == std::filesystem::path("testcases") ||
          StartsWith(path.filename().string(), "host_testcases")) {
        android_build_top = path.append("art_common");
        break;
      }
    }
  }
  CHECK(!android_build_top.empty());

  // Check that the expected directory matches the environment variable.
  const char* android_build_top_from_env = getenv("ANDROID_BUILD_TOP");
  android_build_top = std::filesystem::path(android_build_top).string();
  CHECK(!android_build_top.empty());
  if (android_build_top_from_env != nullptr) {
    if (std::filesystem::weakly_canonical(android_build_top).string() !=
        std::filesystem::weakly_canonical(android_build_top_from_env).string()) {
      LOG(WARNING) << "Execution path (" << argv << ") not below ANDROID_BUILD_TOP ("
                   << android_build_top_from_env << ")! Using env-var.";
      android_build_top = android_build_top_from_env;
    }
  } else {
    setenv("ANDROID_BUILD_TOP", android_build_top.c_str(), /*overwrite=*/0);
  }
  if (android_build_top.back() != '/') {
    android_build_top += '/';
  }
  return android_build_top;
}

std::string CommonArtTestImpl::GetAndroidHostOut() {
  CHECK(IsHost());

  // Check that the expected directory matches the environment variable.
  // ANDROID_HOST_OUT is set by envsetup or unset and is the full path to host binaries/libs
  const char* android_host_out_from_env = getenv("ANDROID_HOST_OUT");
  // OUT_DIR is a user-settable ENV_VAR that controls where soong puts build artifacts. It can
  // either be relative to ANDROID_BUILD_TOP or a concrete path.
  const char* android_out_dir = getenv("OUT_DIR");
  // Take account of OUT_DIR setting.
  if (android_out_dir == nullptr) {
    android_out_dir = "out";
  }
  std::string android_host_out;
  if (android_out_dir[0] == '/') {
    android_host_out = (std::filesystem::path(android_out_dir) / "host" / "linux-x86").string();
  } else {
    android_host_out =
        (std::filesystem::path(GetAndroidBuildTop()) / android_out_dir / "host" / "linux-x86")
            .string();
  }
  std::filesystem::path expected(android_host_out);
  if (android_host_out_from_env != nullptr) {
    std::filesystem::path from_env(std::filesystem::weakly_canonical(android_host_out_from_env));
    if (std::filesystem::weakly_canonical(expected).string() != from_env.string()) {
      LOG(WARNING) << "Execution path (" << expected << ") not below ANDROID_HOST_OUT ("
                   << from_env << ")! Using env-var.";
      expected = from_env;
    }
  } else {
    setenv("ANDROID_HOST_OUT", android_host_out.c_str(), /*overwrite=*/0);
  }
  return expected.string();
}

void CommonArtTestImpl::SetUpAndroidRootEnvVars() {
  if (IsHost()) {
    std::string android_host_out = GetAndroidHostOut();

    // Environment variable ANDROID_ROOT is set on the device, but not
    // necessarily on the host.
    const char* android_root_from_env = getenv("ANDROID_ROOT");
    if (android_root_from_env == nullptr) {
      // Use ANDROID_HOST_OUT for ANDROID_ROOT.
      setenv("ANDROID_ROOT", android_host_out.c_str(), 1);
      android_root_from_env = getenv("ANDROID_ROOT");
    }

    // Environment variable ANDROID_I18N_ROOT is set on the device, but not
    // necessarily on the host. It needs to be set so that various libraries
    // like libcore / icu4j / icu4c can find their data files.
    const char* android_i18n_root_from_env = getenv("ANDROID_I18N_ROOT");
    if (android_i18n_root_from_env == nullptr) {
      // Use ${ANDROID_I18N_OUT}/com.android.i18n for ANDROID_I18N_ROOT.
      std::string android_i18n_root = android_host_out.c_str();
      android_i18n_root += "/com.android.i18n";
      setenv("ANDROID_I18N_ROOT", android_i18n_root.c_str(), 1);
    }

    // Environment variable ANDROID_ART_ROOT is set on the device, but not
    // necessarily on the host. It needs to be set so that various libraries
    // like libcore / icu4j / icu4c can find their data files.
    const char* android_art_root_from_env = getenv("ANDROID_ART_ROOT");
    if (android_art_root_from_env == nullptr) {
      // Use ${ANDROID_HOST_OUT}/com.android.art for ANDROID_ART_ROOT.
      std::string android_art_root = android_host_out.c_str();
      android_art_root += "/com.android.art";
      setenv("ANDROID_ART_ROOT", android_art_root.c_str(), 1);
    }

    // Environment variable ANDROID_TZDATA_ROOT is set on the device, but not
    // necessarily on the host. It needs to be set so that various libraries
    // like libcore / icu4j / icu4c can find their data files.
    const char* android_tzdata_root_from_env = getenv("ANDROID_TZDATA_ROOT");
    if (android_tzdata_root_from_env == nullptr) {
      // Use ${ANDROID_HOST_OUT}/com.android.tzdata for ANDROID_TZDATA_ROOT.
      std::string android_tzdata_root = android_host_out.c_str();
      android_tzdata_root += "/com.android.tzdata";
      setenv("ANDROID_TZDATA_ROOT", android_tzdata_root.c_str(), 1);
    }

    setenv("LD_LIBRARY_PATH", ":", 0);  // Required by java.lang.System.<clinit>.
  }
}

void CommonArtTestImpl::SetUpAndroidDataDir(std::string& android_data) {
  // On target, Cannot use /mnt/sdcard because it is mounted noexec, so use subdir of dalvik-cache
  if (IsHost()) {
    const char* tmpdir = getenv("TMPDIR");
    if (tmpdir != nullptr && tmpdir[0] != 0) {
      android_data = tmpdir;
    } else {
      android_data = "/tmp";
    }
  } else {
    android_data = "/data/dalvik-cache";
  }
  android_data += "/art-data-XXXXXX";
  if (mkdtemp(&android_data[0]) == nullptr) {
    PLOG(FATAL) << "mkdtemp(\"" << &android_data[0] << "\") failed";
  }
  setenv("ANDROID_DATA", android_data.c_str(), 1);
}

void CommonArtTestImpl::SetUp() {
  // Some tests clear these and when running with --no_isolate this can cause
  // later tests to fail
  Locks::Init();
  MemMap::Init();
  SetUpAndroidRootEnvVars();
  SetUpAndroidDataDir(android_data_);

  // Re-use the data temporary directory for /system_ext tests
  android_system_ext_.append(android_data_.c_str());
  android_system_ext_.append("/system_ext");
  int mkdir_result = mkdir(android_system_ext_.c_str(), 0700);
  ASSERT_EQ(mkdir_result, 0);
  setenv("ANDROID_SYSTEM_EXT", android_system_ext_.c_str(), 1);

  std::string system_ext_framework = android_system_ext_ + "/framework";
  mkdir_result = mkdir(system_ext_framework.c_str(), 0700);
  ASSERT_EQ(mkdir_result, 0);

  dalvik_cache_.append(android_data_.c_str());
  dalvik_cache_.append("/dalvik-cache");
  mkdir_result = mkdir(dalvik_cache_.c_str(), 0700);
  ASSERT_EQ(mkdir_result, 0);

  static bool gSlowDebugTestFlag = false;
  RegisterRuntimeDebugFlag(&gSlowDebugTestFlag);
  SetRuntimeDebugFlagsEnabled(true);
  CHECK(gSlowDebugTestFlag);
}

void CommonArtTestImpl::TearDownAndroidDataDir(const std::string& android_data,
                                               bool fail_on_error) {
  if (fail_on_error) {
    ASSERT_EQ(rmdir(android_data.c_str()), 0);
  } else {
    rmdir(android_data.c_str());
  }
}

// Get prebuilt binary tool.
// The paths need to be updated when Android prebuilts update.
std::string CommonArtTestImpl::GetAndroidTool(const char* name, InstructionSet) {
#ifndef ART_CLANG_PATH
  UNUSED(name);
  LOG(FATAL) << "There are no prebuilt tools available.";
  UNREACHABLE();
#else
  std::string path = GetAndroidBuildTop() + ART_CLANG_PATH + "/bin/";
  CHECK(OS::DirectoryExists(path.c_str())) << path;
  path += name;
  CHECK(OS::FileExists(path.c_str())) << path;
  return path;
#endif
}

std::string CommonArtTestImpl::GetCoreArtLocation() {
  return GetCoreFileLocation("art");
}

std::string CommonArtTestImpl::GetCoreOatLocation() {
  return GetCoreFileLocation("oat");
}

std::unique_ptr<const DexFile> CommonArtTestImpl::LoadExpectSingleDexFile(const char* location) {
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  std::string error_msg;
  MemMap::Init();
  static constexpr bool kVerifyChecksum = true;
  const ArtDexFileLoader dex_file_loader;
  std::string filename(IsHost() ? GetAndroidBuildTop() + location : location);
  if (!dex_file_loader.Open(filename.c_str(),
                            std::string(location),
                            /* verify= */ true,
                            kVerifyChecksum,
                            &error_msg,
                            &dex_files)) {
    LOG(FATAL) << "Could not open .dex file '" << filename << "': " << error_msg << "\n";
    UNREACHABLE();
  }
  CHECK_EQ(1U, dex_files.size()) << "Expected only one dex file in " << filename;
  return std::move(dex_files[0]);
}

void CommonArtTestImpl::ClearDirectory(const char* dirpath, bool recursive) {
  ASSERT_TRUE(dirpath != nullptr);
  DIR* dir = opendir(dirpath);
  ASSERT_TRUE(dir != nullptr);
  dirent* e;
  struct stat s;
  while ((e = readdir(dir)) != nullptr) {
    if ((strcmp(e->d_name, ".") == 0) || (strcmp(e->d_name, "..") == 0)) {
      continue;
    }
    std::string filename(dirpath);
    filename.push_back('/');
    filename.append(e->d_name);
    int stat_result = lstat(filename.c_str(), &s);
    ASSERT_EQ(0, stat_result) << "unable to stat " << filename;
    if (S_ISDIR(s.st_mode)) {
      if (recursive) {
        ClearDirectory(filename.c_str());
        int rmdir_result = rmdir(filename.c_str());
        ASSERT_EQ(0, rmdir_result) << filename;
      }
    } else {
      int unlink_result = unlink(filename.c_str());
      ASSERT_EQ(0, unlink_result) << filename;
    }
  }
  closedir(dir);
}

void CommonArtTestImpl::TearDown() {
  const char* android_data = getenv("ANDROID_DATA");
  ASSERT_TRUE(android_data != nullptr);
  ClearDirectory(dalvik_cache_.c_str());
  int rmdir_cache_result = rmdir(dalvik_cache_.c_str());
  ASSERT_EQ(0, rmdir_cache_result);
  ClearDirectory(android_system_ext_.c_str(), true);
  rmdir_cache_result = rmdir(android_system_ext_.c_str());
  ASSERT_EQ(0, rmdir_cache_result);
  TearDownAndroidDataDir(android_data_, true);
  dalvik_cache_.clear();
  android_system_ext_.clear();
}

static std::string GetDexFileName(const std::string& jar_prefix, bool host) {
  std::string prefix(host ? GetAndroidRoot() : "");
  const char* apexPath = (jar_prefix == "conscrypt") ? kAndroidConscryptApexDefaultPath
    : (jar_prefix == "core-icu4j" ? kAndroidI18nApexDefaultPath
    : kAndroidArtApexDefaultPath);
  return StringPrintf("%s%s/javalib/%s.jar", prefix.c_str(), apexPath, jar_prefix.c_str());
}

std::vector<std::string> CommonArtTestImpl::GetLibCoreModuleNames() const {
  // Note: This must start with the CORE_IMG_JARS in Android.common_path.mk
  // because that's what we use for compiling the boot.art image.
  // It may contain additional modules from TEST_CORE_JARS.
  return {
      // CORE_IMG_JARS modules.
      "core-oj",
      "core-libart",
      "okhttp",
      "bouncycastle",
      "apache-xml",
      // Additional modules.
      "core-icu4j",
      "conscrypt",
  };
}

std::vector<std::string> CommonArtTestImpl::GetLibCoreDexFileNames(
    const std::vector<std::string>& modules) const {
  std::vector<std::string> result;
  result.reserve(modules.size());
  for (const std::string& module : modules) {
    result.push_back(GetDexFileName(module, IsHost()));
  }
  return result;
}

std::vector<std::string> CommonArtTestImpl::GetLibCoreDexFileNames() const {
  std::vector<std::string> modules = GetLibCoreModuleNames();
  return GetLibCoreDexFileNames(modules);
}

std::vector<std::string> CommonArtTestImpl::GetLibCoreDexLocations(
    const std::vector<std::string>& modules) const {
  std::vector<std::string> result = GetLibCoreDexFileNames(modules);
  if (IsHost()) {
    // Strip the ANDROID_BUILD_TOP directory including the directory separator '/'.
    std::string prefix = GetAndroidBuildTop();
    for (std::string& location : result) {
      CHECK_GT(location.size(), prefix.size());
      CHECK_EQ(location.compare(0u, prefix.size(), prefix), 0)
          << " prefix=" << prefix << " location=" << location;
      location.erase(0u, prefix.size());
    }
  }
  return result;
}

std::vector<std::string> CommonArtTestImpl::GetLibCoreDexLocations() const {
  std::vector<std::string> modules = GetLibCoreModuleNames();
  return GetLibCoreDexLocations(modules);
}

std::string CommonArtTestImpl::GetClassPathOption(const char* option,
                                                  const std::vector<std::string>& class_path) {
  return option + android::base::Join(class_path, ':');
}

// Check that for target builds we have ART_TARGET_NATIVETEST_DIR set.
#ifdef ART_TARGET
#ifndef ART_TARGET_NATIVETEST_DIR
#error "ART_TARGET_NATIVETEST_DIR not set."
#endif
// Wrap it as a string literal.
#define ART_TARGET_NATIVETEST_DIR_STRING STRINGIFY(ART_TARGET_NATIVETEST_DIR) "/"
#else
#define ART_TARGET_NATIVETEST_DIR_STRING ""
#endif

std::string CommonArtTestImpl::GetTestDexFileName(const char* name) const {
  CHECK(name != nullptr);
  // The needed jar files for gtest are located next to the gtest binary itself.
  std::string cmdline;
  bool result = android::base::ReadFileToString("/proc/self/cmdline", &cmdline);
  CHECK(result);
  UniqueCPtr<char[]> executable_path(realpath(cmdline.c_str(), nullptr));
  CHECK(executable_path != nullptr);
  std::string executable_dir = dirname(executable_path.get());
  for (auto ext : {".jar", ".dex"}) {
    std::string path = executable_dir + "/art-gtest-jars-" + name + ext;
    if (OS::FileExists(path.c_str())) {
      return path;
    }
  }
  LOG(FATAL) << "Test file " << name << " not found";
  UNREACHABLE();
}

std::vector<std::unique_ptr<const DexFile>> CommonArtTestImpl::OpenDexFiles(const char* filename) {
  static constexpr bool kVerify = true;
  static constexpr bool kVerifyChecksum = true;
  std::string error_msg;
  const ArtDexFileLoader dex_file_loader;
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  bool success = dex_file_loader.Open(filename,
                                      filename,
                                      kVerify,
                                      kVerifyChecksum,
                                      &error_msg,
                                      &dex_files);
  CHECK(success) << "Failed to open '" << filename << "': " << error_msg;
  for (auto& dex_file : dex_files) {
    CHECK_EQ(PROT_READ, dex_file->GetPermissions());
    CHECK(dex_file->IsReadOnly());
  }
  return dex_files;
}

std::unique_ptr<const DexFile> CommonArtTestImpl::OpenDexFile(const char* filename) {
  std::vector<std::unique_ptr<const DexFile>> dex_files(OpenDexFiles(filename));
  CHECK_EQ(dex_files.size(), 1u) << "Expected only one dex file";
  return std::move(dex_files[0]);
}

std::vector<std::unique_ptr<const DexFile>> CommonArtTestImpl::OpenTestDexFiles(
    const char* name) {
  return OpenDexFiles(GetTestDexFileName(name).c_str());
}

std::unique_ptr<const DexFile> CommonArtTestImpl::OpenTestDexFile(const char* name) {
  return OpenDexFile(GetTestDexFileName(name).c_str());
}

std::string CommonArtTestImpl::GetImageDirectory() {
  std::string path;
  if (IsHost()) {
    const char* host_dir = getenv("ANDROID_HOST_OUT");
    CHECK(host_dir != nullptr);
    path = std::string(host_dir) + "/apex/art_boot_images";
  } else {
    path = std::string(kAndroidArtApexDefaultPath);
  }
  return path + "/javalib";
}

std::string CommonArtTestImpl::GetCoreFileLocation(const char* suffix) {
  CHECK(suffix != nullptr);
  return GetImageDirectory() + "/boot." + suffix;
}

std::string CommonArtTestImpl::CreateClassPath(
    const std::vector<std::unique_ptr<const DexFile>>& dex_files) {
  CHECK(!dex_files.empty());
  std::string classpath = dex_files[0]->GetLocation();
  for (size_t i = 1; i < dex_files.size(); i++) {
    classpath += ":" + dex_files[i]->GetLocation();
  }
  return classpath;
}

std::string CommonArtTestImpl::CreateClassPathWithChecksums(
    const std::vector<std::unique_ptr<const DexFile>>& dex_files) {
  CHECK(!dex_files.empty());
  std::string classpath = dex_files[0]->GetLocation() + "*" +
      std::to_string(dex_files[0]->GetLocationChecksum());
  for (size_t i = 1; i < dex_files.size(); i++) {
    classpath += ":" + dex_files[i]->GetLocation() + "*" +
        std::to_string(dex_files[i]->GetLocationChecksum());
  }
  return classpath;
}

CommonArtTestImpl::ForkAndExecResult CommonArtTestImpl::ForkAndExec(
    const std::vector<std::string>& argv,
    const PostForkFn& post_fork,
    const OutputHandlerFn& handler) {
  ForkAndExecResult result;
  result.status_code = 0;
  result.stage = ForkAndExecResult::kLink;

  std::vector<const char*> c_args;
  for (const std::string& str : argv) {
    c_args.push_back(str.c_str());
  }
  c_args.push_back(nullptr);

  android::base::unique_fd link[2];
  {
    int link_fd[2];

    if (pipe(link_fd) == -1) {
      return result;
    }
    link[0].reset(link_fd[0]);
    link[1].reset(link_fd[1]);
  }

  result.stage = ForkAndExecResult::kFork;

  pid_t pid = fork();
  if (pid == -1) {
    return result;
  }

  if (pid == 0) {
    if (!post_fork()) {
      LOG(ERROR) << "Failed post-fork function";
      exit(1);
      UNREACHABLE();
    }

    // Redirect stdout and stderr.
    dup2(link[1].get(), STDOUT_FILENO);
    dup2(link[1].get(), STDERR_FILENO);

    link[0].reset();
    link[1].reset();

    execv(c_args[0], const_cast<char* const*>(c_args.data()));
    exit(1);
    UNREACHABLE();
  }

  result.stage = ForkAndExecResult::kWaitpid;
  link[1].reset();

  char buffer[128] = { 0 };
  ssize_t bytes_read = 0;
  while (TEMP_FAILURE_RETRY(bytes_read = read(link[0].get(), buffer, 128)) > 0) {
    handler(buffer, bytes_read);
  }
  handler(buffer, 0u);  // End with a virtual write of zero length to simplify clients.

  link[0].reset();

  if (waitpid(pid, &result.status_code, 0) == -1) {
    return result;
  }

  result.stage = ForkAndExecResult::kFinished;
  return result;
}

CommonArtTestImpl::ForkAndExecResult CommonArtTestImpl::ForkAndExec(
    const std::vector<std::string>& argv, const PostForkFn& post_fork, std::string* output) {
  auto string_collect_fn = [output](char* buf, size_t len) {
    *output += std::string(buf, len);
  };
  return ForkAndExec(argv, post_fork, string_collect_fn);
}

}  // namespace art
