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

// A dummy implementation of the native-bridge interface.

#include "nativebridge/native_bridge.h"

#include "NativeBridge6PreZygoteFork_lib.h"

// NativeBridgeCallbacks implementations
extern "C" bool native_bridge6_initialize(
                      const android::NativeBridgeRuntimeCallbacks* /* art_cbs */,
                      const char* /* app_code_cache_dir */,
                      const char* /* isa */) {
  return true;
}

extern "C" void* native_bridge6_loadLibrary(const char* /* libpath */, int /* flag */) {
  return nullptr;
}

extern "C" void* native_bridge6_getTrampoline(void* /* handle */, const char* /* name */,
                                             const char* /* shorty */, uint32_t /* len */) {
  return nullptr;
}

extern "C" bool native_bridge6_isSupported(const char* /* libpath */) {
  return false;
}

extern "C" const struct android::NativeBridgeRuntimeValues* native_bridge6_getAppEnv(
    const char* /* abi */) {
  return nullptr;
}

extern "C" bool native_bridge6_isCompatibleWith(uint32_t version) {
  // For testing, allow 1-6, but disallow 7+.
  return version <= 6;
}

extern "C" android::NativeBridgeSignalHandlerFn native_bridge6_getSignalHandler(int /* signal */) {
  return nullptr;
}

extern "C" int native_bridge6_unloadLibrary(void* /* handle */) {
  return 0;
}

extern "C" const char* native_bridge6_getError() {
  return nullptr;
}

extern "C" bool native_bridge6_isPathSupported(const char* /* path */) {
  return true;
}

extern "C" bool native_bridge6_initAnonymousNamespace(const char* /* public_ns_sonames */,
                                                      const char* /* anon_ns_library_path */) {
  return true;
}

extern "C" android::native_bridge_namespace_t*
native_bridge6_createNamespace(const char* /* name */,
                               const char* /* ld_library_path */,
                               const char* /* default_library_path */,
                               uint64_t /* type */,
                               const char* /* permitted_when_isolated_path */,
                               android::native_bridge_namespace_t* /* parent_ns */) {
  return nullptr;
}

extern "C" bool native_bridge6_linkNamespaces(android::native_bridge_namespace_t* /* from */,
                                              android::native_bridge_namespace_t* /* to */,
                                              const char* /* shared_libs_soname */) {
  return true;
}

extern "C" void* native_bridge6_loadLibraryExt(const char* /* libpath */,
                                               int /* flag */,
                                               android::native_bridge_namespace_t* /* ns */) {
  return nullptr;
}

extern "C" android::native_bridge_namespace_t* native_bridge6_getVendorNamespace() {
  return nullptr;
}

extern "C" android::native_bridge_namespace_t* native_bridge6_getExportedNamespace(const char* /* name */) {
  return nullptr;
}

extern "C" void native_bridge6_preZygoteFork() {
  android::SetPreZygoteForkDone();
}

android::NativeBridgeCallbacks NativeBridgeItf{
    // v1
    .version = 6,
    .initialize = &native_bridge6_initialize,
    .loadLibrary = &native_bridge6_loadLibrary,
    .getTrampoline = &native_bridge6_getTrampoline,
    .isSupported = &native_bridge6_isSupported,
    .getAppEnv = &native_bridge6_getAppEnv,
    // v2
    .isCompatibleWith = &native_bridge6_isCompatibleWith,
    .getSignalHandler = &native_bridge6_getSignalHandler,
    // v3
    .unloadLibrary = &native_bridge6_unloadLibrary,
    .getError = &native_bridge6_getError,
    .isPathSupported = &native_bridge6_isPathSupported,
    .initAnonymousNamespace = &native_bridge6_initAnonymousNamespace,
    .createNamespace = &native_bridge6_createNamespace,
    .linkNamespaces = &native_bridge6_linkNamespaces,
    .loadLibraryExt = &native_bridge6_loadLibraryExt,
    // v4
    &native_bridge6_getVendorNamespace,
    // v5
    &native_bridge6_getExportedNamespace,
    // v6
    &native_bridge6_preZygoteFork};
