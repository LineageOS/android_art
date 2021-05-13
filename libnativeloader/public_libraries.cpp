/*
 * Copyright (C) 2019 The Android Open Source Project
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

#define LOG_TAG "nativeloader"

#include "public_libraries.h"

#include <dirent.h>

#include <algorithm>
#include <map>
#include <memory>
#include <regex>
#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/result.h>
#include <android-base/strings.h>
#include <log/log.h>

#if defined(ART_TARGET_ANDROID)
#include <android/sysprop/VndkProperties.sysprop.h>
#endif

#include "utils.h"

namespace android::nativeloader {

using android::base::ErrnoError;
using android::base::Result;
using internal::ConfigEntry;
using internal::ParseConfig;
using internal::ParseApexLibrariesConfig;
using std::literals::string_literals::operator""s;

namespace {

constexpr const char* kDefaultPublicLibrariesFile = "/etc/public.libraries.txt";
constexpr const char* kExtendedPublicLibrariesFilePrefix = "public.libraries-";
constexpr const char* kExtendedPublicLibrariesFileSuffix = ".txt";
constexpr const char* kApexLibrariesConfigFile = "/linkerconfig/apex.libraries.config.txt";
constexpr const char* kVendorPublicLibrariesFile = "/vendor/etc/public.libraries.txt";
constexpr const char* kLlndkLibrariesFile = "/apex/com.android.vndk.v{}/etc/llndk.libraries.{}.txt";
constexpr const char* kVndkLibrariesFile = "/apex/com.android.vndk.v{}/etc/vndksp.libraries.{}.txt";


// TODO(b/130388701): do we need this?
std::string root_dir() {
  static const char* android_root_env = getenv("ANDROID_ROOT");
  return android_root_env != nullptr ? android_root_env : "/system";
}

std::string vndk_version_str(bool use_product_vndk) {
  if (use_product_vndk) {
    static std::string product_vndk_version = get_vndk_version(true);
    return product_vndk_version;
  } else {
    static std::string vendor_vndk_version = get_vndk_version(false);
    return vendor_vndk_version;
  }
}

// insert vndk version in every {} placeholder
void InsertVndkVersionStr(std::string* file_name, bool use_product_vndk) {
  CHECK(file_name != nullptr);
  auto version = vndk_version_str(use_product_vndk);
  size_t pos = file_name->find("{}");
  while (pos != std::string::npos) {
    file_name->replace(pos, 2, version);
    pos = file_name->find("{}", pos + version.size());
  }
}

const std::function<Result<bool>(const struct ConfigEntry&)> always_true =
    [](const struct ConfigEntry&) -> Result<bool> { return true; };

Result<std::vector<std::string>> ReadConfig(
    const std::string& configFile,
    const std::function<Result<bool>(const ConfigEntry& /* entry */)>& filter_fn) {
  std::string file_content;
  if (!base::ReadFileToString(configFile, &file_content)) {
    return ErrnoError();
  }
  Result<std::vector<std::string>> result = ParseConfig(file_content, filter_fn);
  if (!result.ok()) {
    return Errorf("Cannot parse {}: {}", configFile, result.error().message());
  }
  return result;
}

void ReadExtensionLibraries(const char* dirname, std::vector<std::string>* sonames) {
  std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(dirname), closedir);
  if (dir != nullptr) {
    // Failing to opening the dir is not an error, which can happen in
    // webview_zygote.
    while (struct dirent* ent = readdir(dir.get())) {
      if (ent->d_type != DT_REG && ent->d_type != DT_LNK) {
        continue;
      }
      const std::string filename(ent->d_name);
      std::string_view fn = filename;
      if (android::base::ConsumePrefix(&fn, kExtendedPublicLibrariesFilePrefix) &&
          android::base::ConsumeSuffix(&fn, kExtendedPublicLibrariesFileSuffix)) {
        const std::string company_name(fn);
        const std::string config_file_path = dirname + "/"s + filename;
        LOG_ALWAYS_FATAL_IF(
            company_name.empty(),
            "Error extracting company name from public native library list file path \"%s\"",
            config_file_path.c_str());

        auto ret = ReadConfig(
            config_file_path, [&company_name](const struct ConfigEntry& entry) -> Result<bool> {
              if (android::base::StartsWith(entry.soname, "lib") &&
                  android::base::EndsWith(entry.soname, "." + company_name + ".so")) {
                return true;
              } else {
                return Errorf("Library name \"{}\" does not end with the company name {}.",
                              entry.soname, company_name);
              }
            });
        if (ret.ok()) {
          sonames->insert(sonames->end(), ret->begin(), ret->end());
        } else {
          LOG_ALWAYS_FATAL("Error reading public native library list from \"%s\": %s",
                           config_file_path.c_str(), ret.error().message().c_str());
        }
      }
    }
  }
}

static std::string InitDefaultPublicLibraries(bool for_preload) {
  std::string config_file = root_dir() + kDefaultPublicLibrariesFile;
  auto sonames =
      ReadConfig(config_file, [&for_preload](const struct ConfigEntry& entry) -> Result<bool> {
        if (for_preload) {
          return !entry.nopreload;
        } else {
          return true;
        }
      });
  if (!sonames.ok()) {
    LOG_ALWAYS_FATAL("Error reading public native library list from \"%s\": %s",
                     config_file.c_str(), sonames.error().message().c_str());
    return "";
  }

  // If this is for preloading libs, don't remove the libs from APEXes.
  if (for_preload) {
    return android::base::Join(*sonames, ':');
  }

  // Remove the public libs provided by apexes because these libs are available
  // from apex namespaces.
  for (const auto& p : apex_public_libraries()) {
    auto public_libs = base::Split(p.second, ":");
    sonames->erase(std::remove_if(sonames->begin(), sonames->end(), [&public_libs](const std::string& v) {
      return std::find(public_libs.begin(), public_libs.end(), v) != public_libs.end();
    }), sonames->end());
  }
  return android::base::Join(*sonames, ':');
}

static std::string InitVendorPublicLibraries() {
  // This file is optional, quietly ignore if the file does not exist.
  auto sonames = ReadConfig(kVendorPublicLibrariesFile, always_true);
  if (!sonames.ok()) {
    return "";
  }
  return android::base::Join(*sonames, ':');
}

// read /system/etc/public.libraries-<companyname>.txt,
// /system_ext/etc/public.libraries-<companyname>.txt and
// /product/etc/public.libraries-<companyname>.txt which contain partner defined
// system libs that are exposed to apps. The libs in the txt files must be
// named as lib<name>.<companyname>.so.
static std::string InitExtendedPublicLibraries() {
  std::vector<std::string> sonames;
  ReadExtensionLibraries("/system/etc", &sonames);
  ReadExtensionLibraries("/system_ext/etc", &sonames);
  ReadExtensionLibraries("/product/etc", &sonames);
  return android::base::Join(sonames, ':');
}

static std::string InitLlndkLibrariesVendor() {
  std::string config_file = kLlndkLibrariesFile;
  InsertVndkVersionStr(&config_file, false);
  auto sonames = ReadConfig(config_file, always_true);
  if (!sonames.ok()) {
    LOG_ALWAYS_FATAL("%s: %s", config_file.c_str(), sonames.error().message().c_str());
    return "";
  }
  return android::base::Join(*sonames, ':');
}

static std::string InitLlndkLibrariesProduct() {
  if (!is_product_vndk_version_defined()) {
    return "";
  }
  std::string config_file = kLlndkLibrariesFile;
  InsertVndkVersionStr(&config_file, true);
  auto sonames = ReadConfig(config_file, always_true);
  if (!sonames.ok()) {
    LOG_ALWAYS_FATAL("%s: %s", config_file.c_str(), sonames.error().message().c_str());
    return "";
  }
  return android::base::Join(*sonames, ':');
}

static std::string InitVndkspLibrariesVendor() {
  std::string config_file = kVndkLibrariesFile;
  InsertVndkVersionStr(&config_file, false);
  auto sonames = ReadConfig(config_file, always_true);
  if (!sonames.ok()) {
    LOG_ALWAYS_FATAL("%s", sonames.error().message().c_str());
    return "";
  }
  return android::base::Join(*sonames, ':');
}

static std::string InitVndkspLibrariesProduct() {
  if (!is_product_vndk_version_defined()) {
    return "";
  }
  std::string config_file = kVndkLibrariesFile;
  InsertVndkVersionStr(&config_file, true);
  auto sonames = ReadConfig(config_file, always_true);
  if (!sonames.ok()) {
    LOG_ALWAYS_FATAL("%s", sonames.error().message().c_str());
    return "";
  }
  return android::base::Join(*sonames, ':');
}

static std::map<std::string, std::string> InitApexLibraries(const std::string& tag) {
  std::string file_content;
  if (!base::ReadFileToString(kApexLibrariesConfigFile, &file_content)) {
    // config is optional
    return {};
  }
  auto config = ParseApexLibrariesConfig(file_content, tag);
  if (!config.ok()) {
    LOG_ALWAYS_FATAL("%s: %s", kApexLibrariesConfigFile, config.error().message().c_str());
    return {};
  }
  return *config;
}

struct ApexLibrariesConfigLine {
  std::string tag;
  std::string apex_namespace;
  std::string library_list;
};

const std::regex kApexNamespaceRegex("[0-9a-zA-Z_]+");
const std::regex kLibraryListRegex("[0-9a-zA-Z.:@+_-]+");

Result<ApexLibrariesConfigLine> ParseApexLibrariesConfigLine(const std::string& line) {
  std::vector<std::string> tokens = base::Split(line, " ");
  if (tokens.size() != 3) {
    return Errorf("Malformed line \"{}\"", line);
  }
  if (tokens[0] != "jni" && tokens[0] != "public") {
    return Errorf("Invalid tag \"{}\"", line);
  }
  if (!std::regex_match(tokens[1], kApexNamespaceRegex)) {
    return Errorf("Invalid apex_namespace \"{}\"", line);
  }
  if (!std::regex_match(tokens[2], kLibraryListRegex)) {
    return Errorf("Invalid library_list \"{}\"", line);
  }
  return ApexLibrariesConfigLine{std::move(tokens[0]), std::move(tokens[1]), std::move(tokens[2])};
}

}  // namespace

const std::string& preloadable_public_libraries() {
  static std::string list = InitDefaultPublicLibraries(/*for_preload*/ true);
  return list;
}

const std::string& default_public_libraries() {
  static std::string list = InitDefaultPublicLibraries(/*for_preload*/ false);
  return list;
}

const std::string& vendor_public_libraries() {
  static std::string list = InitVendorPublicLibraries();
  return list;
}

const std::string& extended_public_libraries() {
  static std::string list = InitExtendedPublicLibraries();
  return list;
}

const std::string& llndk_libraries_product() {
  static std::string list = InitLlndkLibrariesProduct();
  return list;
}

const std::string& llndk_libraries_vendor() {
  static std::string list = InitLlndkLibrariesVendor();
  return list;
}

const std::string& vndksp_libraries_product() {
  static std::string list = InitVndkspLibrariesProduct();
  return list;
}

const std::string& vndksp_libraries_vendor() {
  static std::string list = InitVndkspLibrariesVendor();
  return list;
}

const std::string& apex_jni_libraries(const std::string& apex_ns_name) {
  static std::map<std::string, std::string> jni_libraries = InitApexLibraries("jni");
  return jni_libraries[apex_ns_name];
}

const std::map<std::string, std::string>& apex_public_libraries() {
  static std::map<std::string, std::string> public_libraries = InitApexLibraries("public");
  return public_libraries;
}

bool is_product_vndk_version_defined() {
#if defined(ART_TARGET_ANDROID)
  return android::sysprop::VndkProperties::product_vndk_version().has_value();
#else
  return false;
#endif
}

std::string get_vndk_version(bool is_product_vndk) {
#if defined(ART_TARGET_ANDROID)
  if (is_product_vndk) {
    return android::sysprop::VndkProperties::product_vndk_version().value_or("");
  }
  return android::sysprop::VndkProperties::vendor_vndk_version().value_or("");
#else
  if (is_product_vndk) {
    return android::base::GetProperty("ro.product.vndk.version", "");
  }
  return android::base::GetProperty("ro.vndk.version", "");
#endif
}

namespace internal {
// Exported for testing
Result<std::vector<std::string>> ParseConfig(
    const std::string& file_content,
    const std::function<Result<bool>(const ConfigEntry& /* entry */)>& filter_fn) {
  std::vector<std::string> lines = base::Split(file_content, "\n");

  std::vector<std::string> sonames;
  for (auto& line : lines) {
    auto trimmed_line = base::Trim(line);
    if (trimmed_line[0] == '#' || trimmed_line.empty()) {
      continue;
    }

    std::vector<std::string> tokens = android::base::Split(trimmed_line, " ");
    if (tokens.size() < 1 || tokens.size() > 3) {
      return Errorf("Malformed line \"{}\"", line);
    }
    struct ConfigEntry entry = {.soname = "", .nopreload = false, .bitness = ALL};
    size_t i = tokens.size();
    while (i-- > 0) {
      if (tokens[i] == "nopreload") {
        entry.nopreload = true;
      } else if (tokens[i] == "32" || tokens[i] == "64") {
        if (entry.bitness != ALL) {
          return Errorf("Malformed line \"{}\": bitness can be specified only once", line);
        }
        entry.bitness = tokens[i] == "32" ? ONLY_32 : ONLY_64;
      } else {
        if (i != 0) {
          return Errorf("Malformed line \"{}\"", line);
        }
        entry.soname = tokens[i];
      }
    }

    // skip 32-bit lib on 64-bit process and vice versa
#if defined(__LP64__)
    if (entry.bitness == ONLY_32) continue;
#else
    if (entry.bitness == ONLY_64) continue;
#endif

    Result<bool> ret = filter_fn(entry);
    if (!ret.ok()) {
      return ret.error();
    }
    if (*ret) {
      // filter_fn has returned true.
      sonames.push_back(entry.soname);
    }
  }
  return sonames;
}

// Parses apex.libraries.config.txt file generated by linkerconfig which looks like
//   system/linkerconfig/testdata/golden_output/stages/apex.libraries.config.txt
// and returns mapping of <apex namespace> to <library list> which matches <tag>.
//
// The file is line-based and each line consists of "<tag> <apex namespace> <library list>".
//
// <tag> explains what <library list> is. (e.g "jni", "public")
// <library list> is colon-separated list of library names. (e.g "libfoo.so:libbar.so")
//
// If <tag> is "jni", <library list> is the list of JNI libraries exposed by <apex namespace>.
// If <tag> is "public", <library list> is the list of public libraries exposed by <apex namespace>.
// Public libraries are the libs listed in /system/etc/public.libraries.txt.
Result<std::map<std::string, std::string>> ParseApexLibrariesConfig(const std::string& file_content, const std::string& tag) {
  std::map<std::string, std::string> entries;
  std::vector<std::string> lines = base::Split(file_content, "\n");
  for (auto& line : lines) {
    auto trimmed_line = base::Trim(line);
    if (trimmed_line[0] == '#' || trimmed_line.empty()) {
      continue;
    }
    auto config_line = ParseApexLibrariesConfigLine(trimmed_line);
    if (!config_line.ok()) {
      return config_line.error();
    }
    if (config_line->tag != tag) {
      continue;
    }
    entries[config_line->apex_namespace] = config_line->library_list;
  }
  return entries;
}

}  // namespace internal

}  // namespace android::nativeloader
