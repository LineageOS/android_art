/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "file_utils.h"

#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <unistd.h>

// We need dladdr.
#if !defined(__APPLE__) && !defined(_WIN32)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#define DEFINED_GNU_SOURCE
#endif
#include <dlfcn.h>
#include <libgen.h>
#ifdef DEFINED_GNU_SOURCE
#undef _GNU_SOURCE
#undef DEFINED_GNU_SOURCE
#endif
#endif

#include <memory>
#include <sstream>

#include "android-base/file.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"

#include "base/bit_utils.h"
#include "base/globals.h"
#include "base/os.h"
#include "base/stl_util.h"
#include "base/unix_file/fd_file.h"

#if defined(__APPLE__)
#include <crt_externs.h>
#include <sys/syscall.h>
#include "AvailabilityMacros.h"  // For MAC_OS_X_VERSION_MAX_ALLOWED
#endif

#if defined(__linux__)
#include <linux/unistd.h>
#endif

namespace art {

using android::base::StringPrintf;

static constexpr const char* kClassesDex = "classes.dex";
static constexpr const char* kAndroidRootEnvVar = "ANDROID_ROOT";
static constexpr const char* kAndroidRootDefaultPath = "/system";
static constexpr const char* kAndroidSystemExtRootEnvVar = "ANDROID_SYSTEM_EXT";
static constexpr const char* kAndroidSystemExtRootDefaultPath = "/system_ext";
static constexpr const char* kAndroidDataEnvVar = "ANDROID_DATA";
static constexpr const char* kAndroidDataDefaultPath = "/data";
static constexpr const char* kAndroidArtRootEnvVar = "ANDROID_ART_ROOT";
static constexpr const char* kAndroidConscryptRootEnvVar = "ANDROID_CONSCRYPT_ROOT";
static constexpr const char* kAndroidI18nRootEnvVar = "ANDROID_I18N_ROOT";
static constexpr const char* kApexDefaultPath = "/apex/";
static constexpr const char* kArtApexDataEnvVar = "ART_APEX_DATA";

// Get the "root" directory containing the "lib" directory where this instance
// of the libartbase library (which contains `GetRootContainingLibartbase`) is
// located:
// - on host this "root" is normally the Android Root (e.g. something like
//   "$ANDROID_BUILD_TOP/out/host/linux-x86/");
// - on target this "root" is normally the ART Root ("/apex/com.android.art").
// Return the empty string if that directory cannot be found or if this code is
// run on Windows or macOS.
static std::string GetRootContainingLibartbase() {
#if !defined( _WIN32) && !defined(__APPLE__)
  // Check where libartbase is from, and derive from there.
  Dl_info info;
  if (dladdr(reinterpret_cast<const void*>(&GetRootContainingLibartbase), /* out */ &info) != 0) {
    // Make a duplicate of the fname so dirname can modify it.
    UniqueCPtr<char> fname(strdup(info.dli_fname));

    char* dir1 = dirname(fname.get());  // This is the lib directory.
    char* dir2 = dirname(dir1);         // This is the "root" directory.
    if (OS::DirectoryExists(dir2)) {
      std::string tmp = dir2;  // Make a copy here so that fname can be released.
      return tmp;
    }
  }
#endif
  return "";
}

std::string GetAndroidRootSafe(std::string* error_msg) {
#ifdef _WIN32
  UNUSED(kAndroidRootEnvVar, kAndroidRootDefaultPath, GetRootContainingLibartbase);
  *error_msg = "GetAndroidRootSafe unsupported for Windows.";
  return "";
#else
  // Prefer ANDROID_ROOT if it's set.
  const char* android_root_from_env = getenv(kAndroidRootEnvVar);
  if (android_root_from_env != nullptr) {
    if (!OS::DirectoryExists(android_root_from_env)) {
      *error_msg =
          StringPrintf("Failed to find %s directory %s", kAndroidRootEnvVar, android_root_from_env);
      return "";
    }
    return android_root_from_env;
  }

  // On host, libartbase is currently installed in "$ANDROID_ROOT/lib"
  // (e.g. something like "$ANDROID_BUILD_TOP/out/host/linux-x86/lib". Use this
  // information to infer the location of the Android Root (on host only).
  //
  // Note that this could change in the future, if we decided to install ART
  // artifacts in a different location, e.g. within an "ART APEX" directory.
  if (!kIsTargetBuild) {
    std::string root_containing_libartbase = GetRootContainingLibartbase();
    if (!root_containing_libartbase.empty()) {
      return root_containing_libartbase;
    }
  }

  // Try the default path.
  if (!OS::DirectoryExists(kAndroidRootDefaultPath)) {
    *error_msg =
        StringPrintf("Failed to find default Android Root directory %s", kAndroidRootDefaultPath);
    return "";
  }
  return kAndroidRootDefaultPath;
#endif
}

std::string GetAndroidRoot() {
  std::string error_msg;
  std::string ret = GetAndroidRootSafe(&error_msg);
  if (ret.empty()) {
    LOG(FATAL) << error_msg;
    UNREACHABLE();
  }
  return ret;
}


static const char* GetAndroidDirSafe(const char* env_var,
                                     const char* default_dir,
                                     bool must_exist,
                                     std::string* error_msg) {
  const char* android_dir = getenv(env_var);
  if (android_dir == nullptr) {
    if (!must_exist || OS::DirectoryExists(default_dir)) {
      android_dir = default_dir;
    } else {
      *error_msg = StringPrintf("%s not set and %s does not exist", env_var, default_dir);
      return nullptr;
    }
  }
  if (must_exist && !OS::DirectoryExists(android_dir)) {
    *error_msg = StringPrintf("Failed to find directory %s", android_dir);
    return nullptr;
  }
  return android_dir;
}

static const char* GetAndroidDir(const char* env_var,
                                 const char* default_dir,
                                 bool must_exist = true) {
  std::string error_msg;
  const char* dir = GetAndroidDirSafe(env_var, default_dir, must_exist, &error_msg);
  if (dir != nullptr) {
    return dir;
  } else {
    LOG(FATAL) << error_msg;
    UNREACHABLE();
  }
}

static std::string GetArtRootSafe(bool must_exist, /*out*/ std::string* error_msg) {
#ifdef _WIN32
  UNUSED(kAndroidArtRootEnvVar, kAndroidArtApexDefaultPath, GetRootContainingLibartbase);
  UNUSED(must_exist);
  *error_msg = "GetArtRootSafe unsupported for Windows.";
  return "";
#else
  // Prefer ANDROID_ART_ROOT if it's set.
  const char* android_art_root_from_env = getenv(kAndroidArtRootEnvVar);
  if (android_art_root_from_env != nullptr) {
    if (must_exist && !OS::DirectoryExists(android_art_root_from_env)) {
      *error_msg = StringPrintf("Failed to find %s directory %s",
                                kAndroidArtRootEnvVar,
                                android_art_root_from_env);
      return "";
    }
    return android_art_root_from_env;
  }

  // On target, libartbase is normally installed in
  // "$ANDROID_ART_ROOT/lib(64)" (e.g. something like
  // "/apex/com.android.art/lib(64)". Use this information to infer the
  // location of the ART Root (on target only).
  if (kIsTargetBuild) {
    // *However*, a copy of libartbase may still be installed outside the
    // ART Root on some occasions, as ART target gtests install their binaries
    // and their dependencies under the Android Root, i.e. "/system" (see
    // b/129534335). For that reason, we cannot reliably use
    // `GetRootContainingLibartbase` to find the ART Root. (Note that this is
    // not really a problem in practice, as Android Q devices define
    // ANDROID_ART_ROOT in their default environment, and will instead use
    // the logic above anyway.)
    //
    // TODO(b/129534335): Re-enable this logic when the only instance of
    // libartbase on target is the one from the ART APEX.
    if ((false)) {
      std::string root_containing_libartbase = GetRootContainingLibartbase();
      if (!root_containing_libartbase.empty()) {
        return root_containing_libartbase;
      }
    }
  }

  // Try the default path.
  if (must_exist && !OS::DirectoryExists(kAndroidArtApexDefaultPath)) {
    *error_msg = StringPrintf("Failed to find default ART root directory %s",
                              kAndroidArtApexDefaultPath);
    return "";
  }
  return kAndroidArtApexDefaultPath;
#endif
}

std::string GetArtRootSafe(std::string* error_msg) {
  return GetArtRootSafe(/* must_exist= */ true, error_msg);
}

std::string GetArtRoot() {
  std::string error_msg;
  std::string ret = GetArtRootSafe(&error_msg);
  if (ret.empty()) {
    LOG(FATAL) << error_msg;
    UNREACHABLE();
  }
  return ret;
}

std::string GetArtBinDir() {
  // Environment variable `ANDROID_ART_ROOT` is defined as
  // `$ANDROID_HOST_OUT/com.android.art` on host. However, host ART binaries are
  // still installed in `$ANDROID_HOST_OUT/bin` (i.e. outside the ART Root). The
  // situation is cleaner on target, where `ANDROID_ART_ROOT` is
  // `$ANDROID_ROOT/apex/com.android.art` and ART binaries are installed in
  // `$ANDROID_ROOT/apex/com.android.art/bin`.
  std::string android_art_root = kIsTargetBuild ? GetArtRoot() : GetAndroidRoot();
  return android_art_root + "/bin";
}

std::string GetAndroidDataSafe(std::string* error_msg) {
  const char* android_dir = GetAndroidDirSafe(kAndroidDataEnvVar,
                                              kAndroidDataDefaultPath,
                                              /* must_exist= */ true,
                                              error_msg);
  return (android_dir != nullptr) ? android_dir : "";
}

std::string GetAndroidData() {
  return GetAndroidDir(kAndroidDataEnvVar, kAndroidDataDefaultPath);
}

std::string GetArtApexData() {
  return GetAndroidDir(kArtApexDataEnvVar, kArtApexDataDefaultPath, /*must_exist=*/false);
}

static std::string GetFirstBootClasspathExtensionJar(const std::string& android_root) {
  DCHECK(kIsTargetBuild);

  // This method finds the first non-APEX DEX file in the boot class path as defined by the
  // DEX2OATBOOTCLASSPATH environment variable. This corresponds to the first boot classpath
  // extension (see IMAGE SECTION documentation in image.h). When on-device signing is used the
  // boot class extensions are compiled together as a single image with a name derived from the
  // first extension. This first boot classpath extension is usually
  // '/system/framework/framework.jar'.
  //
  // DEX2OATBOOTCLASSPATH is generated at build time by in the init.environ.rc.in:
  //   ${ANDROID_BUILD_TOP}/system/core/rootdir/Android.mk
  // and initialized on Android by init in init.environ.rc:
  //   ${ANDROID_BUILD_TOP}/system/core/rootdir/init.environ.rc.in.
  // It is used by installd too.
  const char* bcp = getenv("DEX2OATBOOTCLASSPATH");
  const std::string kDefaultBcpExtensionJar = android_root + "/framework/framework.jar";
  if (bcp != nullptr) {
    for (std::string_view component : SplitString(bcp, ':')) {
      if (component.empty()) {
        continue;
      }
      if (!LocationIsOnApex(component)) {
        return std::string{component};
      }
    }
  }
  return kDefaultBcpExtensionJar;
}

std::string GetDefaultBootImageLocation(const std::string& android_root) {
  constexpr static const char* kJavalibBootArt = "javalib/boot.art";
  constexpr static const char* kEtcBootImageProf = "etc/boot-image.prof";

  // Boot image consists of two parts:
  //  - the primary boot image in the ART APEX (contains the Core Libraries)
  //  - the boot image extensions (contains framework libraries) on the system partition, or
  //    in the ART APEX data directory, if an update for the ART module has been been installed.
  if (kIsTargetBuild) {
    // If the ART APEX has been updated, the compiled boot image extension will be in the ART APEX
    // data directory (assuming there is space). Otherwise, for a factory installed ART APEX it is
    // under $ANDROID_ROOT/framework/.
    const std::string first_extension_jar{GetFirstBootClasspathExtensionJar(android_root)};
    const std::string boot_extension_image = GetApexDataBootImage(first_extension_jar);
    const std::string boot_extension_filename =
        GetSystemImageFilename(boot_extension_image.c_str(), kRuntimeISA);
    if (OS::FileExists(boot_extension_filename.c_str(), /*check_file_type=*/true)) {
      return StringPrintf("%s/%s:%s!%s/%s",
                          kAndroidArtApexDefaultPath,
                          kJavalibBootArt,
                          boot_extension_image.c_str(),
                          android_root.c_str(),
                          kEtcBootImageProf);
    } else if (errno == EACCES) {
      // Additional warning for potential SELinux misconfiguration.
      PLOG(ERROR) << "Default boot image check failed, could not stat: " << boot_extension_image;
    }
  }
  return StringPrintf("%s/%s:%s/framework/boot-framework.art!%s/%s",
                      kAndroidArtApexDefaultPath,
                      kJavalibBootArt,
                      android_root.c_str(),
                      android_root.c_str(),
                      kEtcBootImageProf);
}

std::string GetDefaultBootImageLocation(std::string* error_msg) {
  std::string android_root = GetAndroidRootSafe(error_msg);
  if (android_root.empty()) {
    return "";
  }
  return GetDefaultBootImageLocation(android_root);
}

static std::string GetDalvikCacheDirectory(std::string_view root_directory,
                                           std::string_view sub_directory = {}) {
  static constexpr std::string_view kDalvikCache = "dalvik-cache";
  std::stringstream oss;
  oss << root_directory << '/' << kDalvikCache;
  if (!sub_directory.empty()) {
    oss << '/' << sub_directory;
  }
  return oss.str();
}

void GetDalvikCache(const char* subdir, const bool create_if_absent, std::string* dalvik_cache,
                    bool* have_android_data, bool* dalvik_cache_exists, bool* is_global_cache) {
#ifdef _WIN32
  UNUSED(subdir);
  UNUSED(create_if_absent);
  UNUSED(dalvik_cache);
  UNUSED(have_android_data);
  UNUSED(dalvik_cache_exists);
  UNUSED(is_global_cache);
  LOG(FATAL) << "GetDalvikCache unsupported on Windows.";
#else
  CHECK(subdir != nullptr);
  std::string unused_error_msg;
  std::string android_data = GetAndroidDataSafe(&unused_error_msg);
  if (android_data.empty()) {
    *have_android_data = false;
    *dalvik_cache_exists = false;
    *is_global_cache = false;
    return;
  } else {
    *have_android_data = true;
  }
  const std::string dalvik_cache_root = GetDalvikCacheDirectory(android_data);
  *dalvik_cache = dalvik_cache_root + '/' + subdir;
  *dalvik_cache_exists = OS::DirectoryExists(dalvik_cache->c_str());
  *is_global_cache = (android_data == kAndroidDataDefaultPath);
  if (create_if_absent && !*dalvik_cache_exists && !*is_global_cache) {
    // Don't create the system's /data/dalvik-cache/... because it needs special permissions.
    *dalvik_cache_exists = ((mkdir(dalvik_cache_root.c_str(), 0700) == 0 || errno == EEXIST) &&
                            (mkdir(dalvik_cache->c_str(), 0700) == 0 || errno == EEXIST));
  }
#endif
}

bool GetDalvikCacheFilename(const char* location, const char* cache_location,
                            std::string* filename, std::string* error_msg) {
  if (location[0] != '/') {
    *error_msg = StringPrintf("Expected path in location to be absolute: %s", location);
    return false;
  }
  std::string cache_file(&location[1]);  // skip leading slash
  if (!android::base::EndsWith(location, ".dex") &&
      !android::base::EndsWith(location, ".art") &&
      !android::base::EndsWith(location, ".oat")) {
    cache_file += "/";
    cache_file += kClassesDex;
  }
  std::replace(cache_file.begin(), cache_file.end(), '/', '@');
  *filename = StringPrintf("%s/%s", cache_location, cache_file.c_str());
  return true;
}

static std::string GetApexDataDalvikCacheDirectory(InstructionSet isa) {
  if (isa != InstructionSet::kNone) {
    return GetDalvikCacheDirectory(GetArtApexData(), GetInstructionSetString(isa));
  }
  return GetDalvikCacheDirectory(GetArtApexData());
}

static std::string GetApexDataDalvikCacheFilename(std::string_view dex_location,
                                                  InstructionSet isa,
                                                  bool encode_location,
                                                  std::string_view file_extension) {
  if (LocationIsOnApex(dex_location)) {
    return {};
  }
  std::string apex_data_dalvik_cache = GetApexDataDalvikCacheDirectory(isa);
  if (encode_location) {
    // Arguments: "/system/framework/xyz.jar", "arm", true, "odex"
    // Result:
    // "/data/misc/apexdata/com.android.art/dalvik-cache/arm/system@framework@xyz.jar@classes.odex"
    std::string result, unused_error_msg;
    GetDalvikCacheFilename(std::string{dex_location}.c_str(),
                           apex_data_dalvik_cache.c_str(),
                           &result,
                           &unused_error_msg);
    return ReplaceFileExtension(result, file_extension);
  } else {
    // Arguments: "/system/framework/xyz.jar", "x86_64", false, "art"
    // Results: "/data/misc/apexdata/com.android.art/dalvik-cache/x86_64/boot-xyz.jar@classes.art"
    std::string basename = android::base::Basename(std::string{dex_location});
    return apex_data_dalvik_cache + "/boot-" + ReplaceFileExtension(basename, file_extension);
  }
}

std::string GetApexDataOatFilename(std::string_view location, InstructionSet isa) {
  return GetApexDataDalvikCacheFilename(location, isa, /*encode_location=*/false, "oat");
}

std::string GetApexDataOdexFilename(std::string_view location, InstructionSet isa) {
  return GetApexDataDalvikCacheFilename(location, isa, /*encode_location=*/true, "odex");
}

std::string GetApexDataBootImage(std::string_view dex_location) {
  return GetApexDataDalvikCacheFilename(dex_location,
                                        InstructionSet::kNone,
                                        /*encode_location=*/false,
                                        kArtImageExtension);
}

std::string GetApexDataImage(std::string_view dex_location) {
  return GetApexDataDalvikCacheFilename(dex_location,
                                        InstructionSet::kNone,
                                        /*encode_location=*/true,
                                        kArtImageExtension);
}

std::string GetApexDataDalvikCacheFilename(std::string_view dex_location,
                                           InstructionSet isa,
                                           std::string_view file_extension) {
  return GetApexDataDalvikCacheFilename(
      dex_location, isa, /*encode_location=*/true, file_extension);
}

std::string GetVdexFilename(const std::string& oat_location) {
  return ReplaceFileExtension(oat_location, "vdex");
}

static void InsertIsaDirectory(const InstructionSet isa, std::string* filename) {
  // in = /foo/bar/baz
  // out = /foo/bar/<isa>/baz
  size_t pos = filename->rfind('/');
  CHECK_NE(pos, std::string::npos) << *filename << " " << isa;
  filename->insert(pos, "/", 1);
  filename->insert(pos + 1, GetInstructionSetString(isa));
}

std::string GetSystemImageFilename(const char* location, const InstructionSet isa) {
  // location = /system/framework/boot.art
  // filename = /system/framework/<isa>/boot.art
  std::string filename(location);
  InsertIsaDirectory(isa, &filename);
  return filename;
}

std::string ReplaceFileExtension(std::string_view filename, std::string_view new_extension) {
  const size_t last_ext = filename.find_last_of("./");
  std::string result;
  if (last_ext == std::string::npos || filename[last_ext] != '.') {
    result.reserve(filename.size() + 1 + new_extension.size());
    result.append(filename).append(".").append(new_extension);
  } else {
    result.reserve(last_ext + 1 + new_extension.size());
    result.append(filename.substr(0, last_ext + 1)).append(new_extension);
  }
  return result;
}

bool LocationIsOnArtApexData(std::string_view location) {
  const std::string art_apex_data = GetArtApexData();
  return android::base::StartsWith(location, art_apex_data);
}

bool LocationIsOnArtModule(std::string_view full_path) {
  std::string unused_error_msg;
  std::string module_path = GetArtRootSafe(/* must_exist= */ kIsTargetBuild, &unused_error_msg);
  if (module_path.empty()) {
    return false;
  }
  return android::base::StartsWith(full_path, module_path);
}

static bool StartsWithSlash(const char* str) {
  DCHECK(str != nullptr);
  return str[0] == '/';
}

static bool EndsWithSlash(const char* str) {
  DCHECK(str != nullptr);
  size_t len = strlen(str);
  return len > 0 && str[len - 1] == '/';
}

// Returns true if `full_path` is located in folder either provided with `env_var`
// or in `default_path` otherwise. The caller may optionally provide a `subdir`
// which will be appended to the tested prefix.
// `default_path` and the value of environment variable `env_var`
// are expected to begin with a slash and not end with one. If this ever changes,
// the path-building logic should be updated.
static bool IsLocationOn(std::string_view full_path,
                         const char* env_var,
                         const char* default_path,
                         const char* subdir = nullptr) {
  std::string unused_error_msg;
  const char* path = GetAndroidDirSafe(env_var,
                                       default_path,
                                       /* must_exist= */ kIsTargetBuild,
                                       &unused_error_msg);
  if (path == nullptr) {
    return false;
  }

  // Build the path which we will check is a prefix of `full_path`. The prefix must
  // end with a slash, so that "/foo/bar" does not match "/foo/barz".
  DCHECK(StartsWithSlash(path)) << path;
  std::string path_prefix(path);
  if (!EndsWithSlash(path_prefix.c_str())) {
    path_prefix.append("/");
  }
  if (subdir != nullptr) {
    // If `subdir` is provided, we assume it is provided without a starting slash
    // but ending with one, e.g. "sub/dir/". `path_prefix` ends with a slash at
    // this point, so we simply append `subdir`.
    DCHECK(!StartsWithSlash(subdir) && EndsWithSlash(subdir)) << subdir;
    path_prefix.append(subdir);
  }

  return android::base::StartsWith(full_path, path_prefix);
}

bool LocationIsOnSystemFramework(std::string_view full_path) {
  return IsLocationOn(full_path,
                      kAndroidRootEnvVar,
                      kAndroidRootDefaultPath,
                      /* subdir= */ "framework/");
}

bool LocationIsOnSystemExtFramework(std::string_view full_path) {
  return IsLocationOn(full_path,
                      kAndroidSystemExtRootEnvVar,
                      kAndroidSystemExtRootDefaultPath,
                      /* subdir= */ "framework/") ||
      // When the 'system_ext' partition is not present, builds will create
      // '/system/system_ext' instead.
      IsLocationOn(full_path,
                   kAndroidRootEnvVar,
                   kAndroidRootDefaultPath,
                   /* subdir= */ "system_ext/framework/");
}

bool LocationIsOnConscryptModule(std::string_view full_path) {
  return IsLocationOn(
      full_path, kAndroidConscryptRootEnvVar, kAndroidConscryptApexDefaultPath);
}

bool LocationIsOnI18nModule(std::string_view full_path) {
  return IsLocationOn(
      full_path, kAndroidI18nRootEnvVar, kAndroidI18nApexDefaultPath);
}

bool LocationIsOnApex(std::string_view full_path) {
  return android::base::StartsWith(full_path, kApexDefaultPath);
}

bool LocationIsOnSystem(const char* path) {
#ifdef _WIN32
  UNUSED(path);
  LOG(FATAL) << "LocationIsOnSystem is unsupported on Windows.";
  return false;
#else
  UniqueCPtr<const char[]> full_path(realpath(path, nullptr));
  return full_path != nullptr &&
      android::base::StartsWith(full_path.get(), GetAndroidRoot().c_str());
#endif
}

bool ArtModuleRootDistinctFromAndroidRoot() {
  std::string error_msg;
  const char* android_root = GetAndroidDirSafe(kAndroidRootEnvVar,
                                               kAndroidRootDefaultPath,
                                               /* must_exist= */ kIsTargetBuild,
                                               &error_msg);
  const char* art_root = GetAndroidDirSafe(kAndroidArtRootEnvVar,
                                           kAndroidArtApexDefaultPath,
                                           /* must_exist= */ kIsTargetBuild,
                                           &error_msg);
  return (android_root != nullptr)
      && (art_root != nullptr)
      && (std::string_view(android_root) != std::string_view(art_root));
}

int DupCloexec(int fd) {
#if defined(__linux__)
  return fcntl(fd, F_DUPFD_CLOEXEC, 0);
#else
  return dup(fd);
#endif
}

}  // namespace art
