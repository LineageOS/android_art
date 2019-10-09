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

#ifndef ART_TOOLS_VERIDEX_API_LIST_FILTER_H_
#define ART_TOOLS_VERIDEX_API_LIST_FILTER_H_

#include <algorithm>
#include <android-base/strings.h>

#include "base/hiddenapi_flags.h"

namespace art {

class ApiListFilter {
 public:
  explicit ApiListFilter(const std::vector<std::string>& exclude_api_lists) {
    std::set<hiddenapi::ApiList> exclude_set;
    bool include_invalid_list = true;
    for (const std::string& name : exclude_api_lists) {
      if (name.empty()) {
        continue;
      }
      if (name == "invalid") {
        include_invalid_list = false;
        continue;
      }
      hiddenapi::ApiList list = hiddenapi::ApiList::FromName(name);
      if (!list.IsValid()) {
        LOG(ERROR) << "Unknown ApiList::Value " << name
                   << ". See valid values in art/libartbase/base/hiddenapi_flags.h.";
      }
      exclude_set.insert(list);
    }

    if (include_invalid_list) {
      lists_.push_back(hiddenapi::ApiList());
    }
    for (size_t i = 0; i < hiddenapi::ApiList::kValueCount; ++i) {
      hiddenapi::ApiList list = hiddenapi::ApiList(i);
      if (exclude_set.find(list) == exclude_set.end()) {
          lists_.push_back(list);
      }
    }
  }

  bool Matches(hiddenapi::ApiList list) const {
    for (const auto& it : lists_) {
      if (list.GetIntValue() == it.GetIntValue()) {
        return true;
      }
    }
    return false;
  }

 private:
  std::vector<hiddenapi::ApiList> lists_;
};

}  // namespace art

#endif  // ART_TOOLS_VERIDEX_API_LIST_FILTER_H_
