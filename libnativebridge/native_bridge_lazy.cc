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

#include "nativebridge/native_bridge.h"
#define LOG_TAG "nativebridge"

#include <dlfcn.h>
#include <errno.h>
#include <string.h>

#include <log/log.h>

namespace android {

namespace {

void* GetLibHandle() {
  static void* handle = dlopen("libnativebridge.so", RTLD_NOW);
  LOG_FATAL_IF(handle == nullptr, "Failed to load libnativebridge.so: %s", dlerror());
  return handle;
}

template <typename FuncPtr>
FuncPtr GetFuncPtr(const char* function_name) {
  auto f = reinterpret_cast<FuncPtr>(dlsym(GetLibHandle(), function_name));
  LOG_FATAL_IF(f == nullptr, "Failed to get address of %s: %s", function_name, dlerror());
  return f;
}

#define GET_FUNC_PTR(name) GetFuncPtr<decltype(&(name))>(#name)

}  // namespace

bool NeedsNativeBridge(const char* instruction_set) {
  static auto f = GET_FUNC_PTR(NeedsNativeBridge);
  return f(instruction_set);
}

bool PreInitializeNativeBridge(const char* app_data_dir, const char* instruction_set) {
  static auto f = GET_FUNC_PTR(PreInitializeNativeBridge);
  return f(app_data_dir, instruction_set);
}

bool NativeBridgeAvailable() {
  static auto f = GET_FUNC_PTR(NativeBridgeAvailable);
  return f();
}

bool NativeBridgeInitialized() {
  static auto f = GET_FUNC_PTR(NativeBridgeInitialized);
  return f();
}

void* NativeBridgeGetTrampoline(void* handle, const char* name, const char* shorty, uint32_t len) {
  static auto f = GET_FUNC_PTR(NativeBridgeGetTrampoline);
  return f(handle, name, shorty, len);
}

const char* NativeBridgeGetError() {
  static auto f = GET_FUNC_PTR(NativeBridgeGetError);
  return f();
}

#undef GET_FUNC_PTR

}  // namespace android
