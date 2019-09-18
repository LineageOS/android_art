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

#ifndef ART_RUNTIME_MIRROR_VAR_HANDLE_INL_H_
#define ART_RUNTIME_MIRROR_VAR_HANDLE_INL_H_

#include "var_handle.h"

namespace art {
class ArtField;

namespace mirror {

template<typename Visitor>
inline void FieldVarHandle::VisitTarget(Visitor&& v) {
  ArtField* orig = GetField();
  ArtField* new_value = v(orig);
  if (orig != new_value) {
    SetField64</*kTransactionActive*/ false>(ArtFieldOffset(),
                                             reinterpret_cast<uintptr_t>(new_value));
  }
}

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_VAR_HANDLE_INL_H_
