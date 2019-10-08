/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef ART_TOOLS_VERIDEX_HIDDEN_API_H_
#define ART_TOOLS_VERIDEX_HIDDEN_API_H_

#include "api_list_filter.h"

#include "base/hiddenapi_flags.h"
#include "dex/method_reference.h"

#include <map>
#include <ostream>
#include <string>

namespace art {

class DexFile;

enum class SignatureSource {
  UNKNOWN,
  BOOT,
  APP,
};

/**
 * Helper class for logging if a method/field is in a hidden API list.
 */
class HiddenApi {
 public:
  HiddenApi(const char* flags_file, const ApiListFilter& api_list_filter);

  hiddenapi::ApiList GetApiList(const std::string& name) const {
    auto it = api_list_.find(name);
    return (it == api_list_.end()) ? hiddenapi::ApiList() : it->second;
  }

  bool ShouldReport(const std::string& signature) const {
    return api_list_filter_.Matches(GetApiList(signature));
  }

  void AddSignatureSource(const std::string &signature, SignatureSource source) {
    const auto type = GetApiClassName(signature);
    auto it = source_.find(type);
    if (it == source_.end() || it->second == SignatureSource::UNKNOWN) {
      source_[type] = source;
    } else if (it->second != source) {
      LOG(WARNING) << type << "is present both in boot and in app.";
      if (source == SignatureSource::BOOT) {
        // Runtime resolves to boot type, so it takes precedence.
        it->second = source;
      }
    } else {
      // Already exists with the same source.
    }
  }

  SignatureSource GetSignatureSource(const std::string& signature) const {
    auto it = source_.find(GetApiClassName(signature));
    return (it == source_.end()) ? SignatureSource::UNKNOWN : it->second;
  }

  bool IsInBoot(const std::string& signature) const {
    return SignatureSource::BOOT == GetSignatureSource(signature);
  }

  static std::string GetApiMethodName(const DexFile& dex_file, uint32_t method_index);

  static std::string GetApiFieldName(const DexFile& dex_file, uint32_t field_index);

  static std::string GetApiMethodName(MethodReference ref) {
    return HiddenApi::GetApiMethodName(*ref.dex_file, ref.index);
  }

  static std::string ToInternalName(const std::string& str) {
    std::string val = str;
    std::replace(val.begin(), val.end(), '.', '/');
    return "L" + val + ";";
  }

 private:
  void AddSignatureToApiList(const std::string& signature, hiddenapi::ApiList membership);

  static std::string GetApiClassName(const std::string& signature) {
    size_t pos = signature.find("->");
    if (pos != std::string::npos) {
      return signature.substr(0, pos);
    }
    return signature;
  }

  const ApiListFilter& api_list_filter_;
  std::map<std::string, hiddenapi::ApiList> api_list_;
  std::map<std::string, SignatureSource> source_;
};

struct HiddenApiStats {
  uint32_t count = 0;
  uint32_t reflection_count = 0;
  uint32_t linking_count = 0;
  // Ensure enough space for kInvalid as well, and initialize all to zero
  uint32_t api_counts[hiddenapi::ApiList::kValueSize] = {};
};


}  // namespace art

#endif  // ART_TOOLS_VERIDEX_HIDDEN_API_H_
