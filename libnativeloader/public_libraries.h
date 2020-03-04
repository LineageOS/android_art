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

#ifndef ART_LIBNATIVELOADER_PUBLIC_LIBRARIES_H_
#define ART_LIBNATIVELOADER_PUBLIC_LIBRARIES_H_

#include <algorithm>
#include <string>

#include <android-base/result.h>

namespace android::nativeloader {

using android::base::Result;

// These provide the list of libraries that are available to the namespace for apps.
// Not all of the libraries are available to apps. Depending on the context,
// e.g., if it is a vendor app or not, different set of libraries are made available.
const std::string& preloadable_public_libraries();
const std::string& default_public_libraries();
const std::string& art_public_libraries();
const std::string& statsd_public_libraries();
const std::string& vendor_public_libraries();
const std::string& extended_public_libraries();
const std::string& neuralnetworks_public_libraries();
const std::string& llndk_libraries_product();
const std::string& llndk_libraries_vendor();
const std::string& vndksp_libraries_product();
const std::string& vndksp_libraries_vendor();

// Returns true if libnativeloader is running on devices and the device has
// ro.product.vndk.version property. It returns false for host.
bool is_product_vndk_version_defined();

std::string get_vndk_version(bool is_product_vndk);

// These are exported for testing
namespace internal {

enum Bitness { ALL = 0, ONLY_32, ONLY_64 };

struct ConfigEntry {
  std::string soname;
  bool nopreload;
  Bitness bitness;
};

Result<std::vector<std::string>> ParseConfig(
    const std::string& file_content,
    const std::function<Result<bool>(const ConfigEntry& /* entry */)>& filter_fn);

}  // namespace internal

}  // namespace android::nativeloader

#endif  // ART_LIBNATIVELOADER_PUBLIC_LIBRARIES_H_
