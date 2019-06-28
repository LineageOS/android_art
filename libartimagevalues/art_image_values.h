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

#ifndef ART_LIBARTIMAGEVALUES_ART_IMAGE_VALUES_H_
#define ART_LIBARTIMAGEVALUES_ART_IMAGE_VALUES_H_

#include <cstdint>

namespace android {
namespace art {
namespace imagevalues {

uint32_t GetImageBaseAddress();
int32_t GetImageMinBaseAddressDelta();
int32_t GetImageMaxBaseAddressDelta();

}  // namespace imagevalues
}  // namespace art
}  // namespace android

#endif  // ART_LIBARTIMAGEVALUES_ART_IMAGE_VALUES_H_
