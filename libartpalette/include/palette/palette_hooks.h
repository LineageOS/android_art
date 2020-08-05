/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef ART_LIBARTPALETTE_INCLUDE_PALETTE_PALETTE_HOOKS_H_
#define ART_LIBARTPALETTE_INCLUDE_PALETTE_PALETTE_HOOKS_H_

#include "palette_types.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Functions provided by the Palette Hooks object, called by ART.
typedef struct paletteHooksInterface_ {
  // TODO: add functions. Currently only one field to ensure the struct has a
  // size.
  void* empty;
} paletteHooksInterface;

struct PaletteHooks {
  const struct paletteHooksInterface_* functions;
#ifdef __cplusplus
  // TODO: Add member functions.
#endif
};

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // ART_LIBARTPALETTE_INCLUDE_PALETTE_PALETTE_HOOKS_H_
