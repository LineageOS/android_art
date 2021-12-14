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

#include "palette/palette.h"

#include <map>
#include <mutex>
#include <stdbool.h>

#include <android-base/logging.h>
#include <android-base/macros.h>  // For ATTRIBUTE_UNUSED

#include "palette_system.h"

// Cached thread priority for testing. No thread priorities are ever affected.
static std::mutex g_tid_priority_map_mutex;
static std::map<int32_t, int32_t> g_tid_priority_map;

palette_status_t PaletteSchedSetPriority(int32_t tid, int32_t priority) {
  if (priority < art::palette::kMinManagedThreadPriority ||
      priority > art::palette::kMaxManagedThreadPriority) {
    return PALETTE_STATUS_INVALID_ARGUMENT;
  }
  std::lock_guard guard(g_tid_priority_map_mutex);
  g_tid_priority_map[tid] = priority;
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteSchedGetPriority(int32_t tid,
                                         /*out*/int32_t* priority) {
  std::lock_guard guard(g_tid_priority_map_mutex);
  if (g_tid_priority_map.find(tid) == g_tid_priority_map.end()) {
    g_tid_priority_map[tid] = art::palette::kNormalManagedThreadPriority;
  }
  *priority = g_tid_priority_map[tid];
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteWriteCrashThreadStacks(/*in*/ const char* stacks, size_t stacks_len) {
  LOG(INFO) << std::string_view(stacks, stacks_len);
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteTraceEnabled(/*out*/bool* enabled) {
  *enabled = false;
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteTraceBegin(const char* name ATTRIBUTE_UNUSED) {
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteTraceEnd() {
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteTraceIntegerValue(const char* name ATTRIBUTE_UNUSED,
                                          int32_t value ATTRIBUTE_UNUSED) {
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteAshmemCreateRegion(const char* name ATTRIBUTE_UNUSED,
                                           size_t size ATTRIBUTE_UNUSED,
                                           int* fd) {
  *fd = -1;
  return PALETTE_STATUS_NOT_SUPPORTED;
}

palette_status_t PaletteAshmemSetProtRegion(int fd ATTRIBUTE_UNUSED,
                                            int prot ATTRIBUTE_UNUSED) {
  return PALETTE_STATUS_NOT_SUPPORTED;
}

palette_status_t PaletteCreateOdrefreshStagingDirectory(const char** staging_dir) {
  *staging_dir = nullptr;
  return PALETTE_STATUS_NOT_SUPPORTED;
}

palette_status_t PaletteShouldReportDex2oatCompilation(bool* value) {
  *value = false;
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteNotifyStartDex2oatCompilation(int source_fd ATTRIBUTE_UNUSED,
                                                      int art_fd ATTRIBUTE_UNUSED,
                                                      int oat_fd ATTRIBUTE_UNUSED,
                                                      int vdex_fd ATTRIBUTE_UNUSED) {
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteNotifyEndDex2oatCompilation(int source_fd ATTRIBUTE_UNUSED,
                                                    int art_fd ATTRIBUTE_UNUSED,
                                                    int oat_fd ATTRIBUTE_UNUSED,
                                                    int vdex_fd ATTRIBUTE_UNUSED) {
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteNotifyDexFileLoaded(const char* path ATTRIBUTE_UNUSED) {
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteNotifyOatFileLoaded(const char* path ATTRIBUTE_UNUSED) {
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteShouldReportJniInvocations(bool* value) {
  *value = false;
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteNotifyBeginJniInvocation(JNIEnv* env ATTRIBUTE_UNUSED) {
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteNotifyEndJniInvocation(JNIEnv* env ATTRIBUTE_UNUSED) {
  return PALETTE_STATUS_OK;
}

palette_status_t PaletteReportLockContention(JNIEnv* env ATTRIBUTE_UNUSED,
                                             int32_t wait_ms ATTRIBUTE_UNUSED,
                                             const char* filename ATTRIBUTE_UNUSED,
                                             int32_t line_number ATTRIBUTE_UNUSED,
                                             const char* method_name ATTRIBUTE_UNUSED,
                                             const char* owner_filename ATTRIBUTE_UNUSED,
                                             int32_t owner_line_number ATTRIBUTE_UNUSED,
                                             const char* owner_method_name ATTRIBUTE_UNUSED,
                                             const char* proc_name ATTRIBUTE_UNUSED,
                                             const char* thread_name ATTRIBUTE_UNUSED) {
  return PALETTE_STATUS_OK;
}
