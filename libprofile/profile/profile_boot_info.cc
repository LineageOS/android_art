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

#include "profile_boot_info.h"

#include <unistd.h>

#include <vector>

#include "dex/dex_file.h"
#include "profile_helpers.h"


namespace art {

void ProfileBootInfo::Add(const DexFile* dex_file, uint32_t method_index) {
  auto it = std::find(dex_files_.begin(), dex_files_.end(), dex_file);
  uint32_t index = 0;
  if (it == dex_files_.end()) {
    index = dex_files_.size();
    dex_files_.push_back(dex_file);
  } else {
    index = std::distance(dex_files_.begin(), it);
  }
  methods_.push_back(std::make_pair(index, method_index));
}

bool ProfileBootInfo::Save(int fd) const {
  std::vector<uint8_t> buffer;
  // Store dex file locations.
  for (const DexFile* dex_file : dex_files_) {
    AddUintToBuffer(&buffer, static_cast<uint8_t>(dex_file->GetLocation().size()));
    AddStringToBuffer(&buffer, dex_file->GetLocation());
  }
  // Store marker between dex file locations and methods.
  AddUintToBuffer(&buffer, static_cast<uint8_t>(0));

  // Store pairs of <dex file index, method id>, in compilation order.
  for (const std::pair<uint32_t, uint32_t>& pair : methods_) {
    AddUintToBuffer(&buffer, pair.first);
    AddUintToBuffer(&buffer, pair.second);
  }
  if (!WriteBuffer(fd, buffer.data(), buffer.size())) {
    return false;
  }
  return true;
}

bool ProfileBootInfo::Load(int fd, const std::vector<const DexFile*>& dex_files) {
  // Read dex file locations.
  do {
    uint8_t string_length;
    int bytes_read = TEMP_FAILURE_RETRY(read(fd, &string_length, sizeof(uint8_t)));
    if (bytes_read < 0) {
      PLOG(ERROR) << "Unexpected error reading profile";
      return false;
    } else if (bytes_read == 0) {
      if (dex_files.empty()) {
        // If no dex files have been passed, that's expected.
        return true;
      } else {
        LOG(ERROR) << "Unexpected end of file for length";
        return false;
      }
    }
    if (string_length == 0) {
      break;
    }
    std::unique_ptr<char[]> data(new char[string_length]);
    bytes_read = TEMP_FAILURE_RETRY(read(fd, data.get(), string_length));
    if (bytes_read < 0) {
      PLOG(WARNING) << "Unexpected error reading profile";
      return false;
    } else if (bytes_read == 0) {
      LOG(ERROR) << "Unexpected end of file for name";
      return false;
    }
    // Map the location to an instance of dex file in `dex_files`.
    auto it = std::find_if(dex_files.begin(),
                           dex_files.end(),
                           [string_length, &data](const DexFile* file) {
      std::string dex_location = file->GetLocation();
      return dex_location.size() == string_length &&
          (strncmp(data.get(), dex_location.data(), string_length) == 0);
    });
    if (it != dex_files.end()) {
      dex_files_.push_back(*it);
    } else {
      LOG(ERROR) << "Couldn't find " << std::string(data.get(), string_length);
      return false;
    }
  } while (true);

  // Read methods.
  do {
    uint32_t dex_file_index;
    uint32_t method_id;
    int bytes_read = TEMP_FAILURE_RETRY(read(fd, &dex_file_index, sizeof(dex_file_index)));
    if (bytes_read <= 0) {
      break;
    }
    bytes_read = TEMP_FAILURE_RETRY(read(fd, &method_id, sizeof(method_id)));
    if (bytes_read <= 0) {
      LOG(ERROR) << "Didn't get a method id";
      return false;
    }
    methods_.push_back(std::make_pair(dex_file_index, method_id));
  } while (true);
  return true;
}

}  // namespace art
