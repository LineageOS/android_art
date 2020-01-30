/*
 * Copyright 2020 The Android Open Source Project
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

#ifndef ART_LIBARTBASE_BASE_ENDIAN_UTILS_H_
#define ART_LIBARTBASE_BASE_ENDIAN_UTILS_H_

#include <stdint.h>
#include <endian.h>
#include <vector>

namespace art {

template<typename T>
inline void AppendBytes(std::vector<uint8_t>& bytes, T data) {
  size_t size = bytes.size();
  bytes.resize(size + sizeof(T));
  memcpy(bytes.data() + size, &data, sizeof(T));
}

inline void Append1BE(std::vector<uint8_t>& bytes, uint8_t value) {
  bytes.push_back(value);
}

inline void Append2BE(std::vector<uint8_t>& bytes, uint16_t value) {
  AppendBytes<uint16_t>(bytes, htobe16(value));
}

inline void Append4BE(std::vector<uint8_t>& bytes, uint32_t value) {
  AppendBytes<uint32_t>(bytes, htobe32(value));
}

inline void Append8BE(std::vector<uint8_t>& bytes, uint64_t value) {
  AppendBytes<uint64_t>(bytes, htobe64(value));
}

inline void AppendUtf16BE(std::vector<uint8_t>& bytes, const uint16_t* chars, size_t char_count) {
  Append4BE(bytes, char_count);
  for (size_t i = 0; i < char_count; ++i) {
    Append2BE(bytes, chars[i]);
  }
}

inline void AppendUtf16CompressedBE(std::vector<uint8_t>& bytes,
                                    const uint8_t* chars,
                                    size_t char_count) {
  Append4BE(bytes, char_count);
  for (size_t i = 0; i < char_count; ++i) {
    Append2BE(bytes, static_cast<uint16_t>(chars[i]));
  }
}

template <typename T>
inline void SetBytes(uint8_t* buf, T val) {
  memcpy(buf, &val, sizeof(T));
}

inline void Set1(uint8_t* buf, uint8_t val) {
  *buf = val;
}

inline void Set2BE(uint8_t* buf, uint16_t val) {
  SetBytes<uint16_t>(buf, htobe16(val));
}

inline void Set4BE(uint8_t* buf, uint32_t val) {
  SetBytes<uint32_t>(buf, htobe32(val));
}

inline void Set8BE(uint8_t* buf, uint64_t val) {
  SetBytes<uint64_t>(buf, htobe64(val));
}

inline void Write1BE(uint8_t** dst, uint8_t value) {
  Set1(*dst, value);
  *dst += sizeof(value);
}

inline void Write2BE(uint8_t** dst, uint16_t value) {
  Set2BE(*dst, value);
  *dst += sizeof(value);
}

inline void Write4BE(uint8_t** dst, uint32_t value) {
  Set4BE(*dst, value);
  *dst += sizeof(value);
}

inline void Write8BE(uint8_t** dst, uint64_t value) {
  Set8BE(*dst, value);
  *dst += sizeof(value);
}

}  // namespace art

#endif  // ART_LIBARTBASE_BASE_ENDIAN_UTILS_H_
