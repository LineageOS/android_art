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

#include "jni.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Functions provided by the Palette Hooks object, called by ART.
typedef struct paletteHooksInterface_ {
  // Notify the Hooks object that dex2oat is starting compilation of the given
  // `source_fd` dex/apk/zip file, and will generate .art/.oat/.vdex files with
  // the given file descriptors.
  void (*NotifyStartDex2oatCompilation)(int source_fd, int art_fd, int oat_fd, int vdex_fd);

  // Notify the Hooks object that dex2oat has ended compilation of the given
  // `source_fd` dex/apk/zip file, and has written the contents into the given file descriptors.
  void (*NotifyEndDex2oatCompilation)(int source_fd, int art_fd, int oat_fd, int vdex_fd);

  // Notify the Hooks object that the runtime is loading a dex file.
  void (*NotifyDexFileLoaded)(const char* path);

  // Notify the Hooks object that the runtime is loading a .oat file.
  void (*NotifyOatFileLoaded)(const char* path);

  // Notify the Hooks object that a native call is starting.
  void (*NotifyBeginJniInvocation)(JNIEnv* env);


  // Notify the Hooks object that a native call is ending.
  void (*NotifyEndJniInvocation)(JNIEnv* env);
} paletteHooksInterface;

struct PaletteHooks {
  const struct paletteHooksInterface_* functions;
#ifdef __cplusplus
  void NotifyStartDex2oatCompilation(int source_fd, int art_fd, int oat_fd, int vdex_fd) {
    return functions->NotifyStartDex2oatCompilation(source_fd, art_fd, oat_fd, vdex_fd);
  }
  void NotifyEndDex2oatCompilation(int source_fd, int art_fd, int oat_fd, int vdex_fd) {
    return functions->NotifyEndDex2oatCompilation(source_fd, art_fd, oat_fd, vdex_fd);
  }
  void NotifyDexFileLoaded(const char* path) {
    return functions->NotifyDexFileLoaded(path);
  }
  void NotifyOatFileLoaded(const char* path) {
    return functions->NotifyOatFileLoaded(path);
  }
  void NotifyBeginJniInvocation(JNIEnv* env) {
    return functions->NotifyBeginJniInvocation(env);
  }
  void NotifyEndJniInvocation(JNIEnv* env) {
    return functions->NotifyEndJniInvocation(env);
  }
#endif
};

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // ART_LIBARTPALETTE_INCLUDE_PALETTE_PALETTE_HOOKS_H_
