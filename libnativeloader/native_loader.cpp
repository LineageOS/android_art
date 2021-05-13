/*
 * Copyright (C) 2015 The Android Open Source Project
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

#define LOG_TAG "nativeloader"

#include "nativeloader/native_loader.h"

#include <dlfcn.h>
#include <sys/types.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/macros.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/thread_annotations.h>
#include <nativebridge/native_bridge.h>
#include <nativehelper/scoped_utf_chars.h>

#ifdef ART_TARGET_ANDROID
#include <log/log.h>
#include "library_namespaces.h"
#include "nativeloader/dlext_namespaces.h"
#endif

namespace android {

namespace {

#if defined(ART_TARGET_ANDROID)

// NATIVELOADER_DEFAULT_NAMESPACE_LIBS is an environment variable that can be
// used when ro.debuggable is true to list extra libraries (separated by ":")
// that libnativeloader will load from the default namespace. The libraries
// must be listed without paths, and then LD_LIBRARY_PATH is typically set to the
// directories to load them from. The libraries will be available in all
// classloader namespaces, and also in the fallback namespace used when no
// classloader is given.
//
// kNativeloaderExtraLibs is the name of that fallback namespace.
//
// NATIVELOADER_DEFAULT_NAMESPACE_LIBS is intended to be used for testing only,
// and in particular in the ART run tests that are executed through dalvikvm in
// the APEX. In that case the default namespace links to the ART namespace
// (com_android_art) for all libraries, which means this can be used to load
// test libraries that depend on ART internal libraries.
constexpr const char* kNativeloaderExtraLibs = "nativeloader-extra-libs";

bool Debuggable() {
  static bool debuggable = android::base::GetBoolProperty("ro.debuggable", false);
  return debuggable;
}

using android::nativeloader::LibraryNamespaces;

std::mutex g_namespaces_mutex;
LibraryNamespaces* g_namespaces = new LibraryNamespaces;
NativeLoaderNamespace* g_nativeloader_extra_libs_namespace = nullptr;

android_namespace_t* FindExportedNamespace(const char* caller_location) {
  auto name = nativeloader::FindApexNamespaceName(caller_location);
  if (name.ok()) {
    android_namespace_t* boot_namespace = android_get_exported_namespace(name->c_str());
    LOG_ALWAYS_FATAL_IF((boot_namespace == nullptr),
                        "Error finding namespace of apex: no namespace called %s", name->c_str());
    return boot_namespace;
  }
  return nullptr;
}

Result<void> CreateNativeloaderDefaultNamespaceLibsLink(NativeLoaderNamespace& ns)
    REQUIRES(g_namespaces_mutex) {
  const char* links = getenv("NATIVELOADER_DEFAULT_NAMESPACE_LIBS");
  if (links == nullptr || *links == 0) {
    return {};
  }
  // Pass nullptr to Link() to create a link to the default namespace without
  // requiring it to be visible.
  return ns.Link(nullptr, links);
}

Result<NativeLoaderNamespace*> GetNativeloaderExtraLibsNamespace() REQUIRES(g_namespaces_mutex) {
  if (g_nativeloader_extra_libs_namespace != nullptr) {
    return g_nativeloader_extra_libs_namespace;
  }

  Result<NativeLoaderNamespace> ns =
      NativeLoaderNamespace::Create(kNativeloaderExtraLibs,
                                    /*search_paths=*/"",
                                    /*permitted_paths=*/"",
                                    /*parent=*/nullptr,
                                    /*is_shared=*/false,
                                    /*is_exempt_list_enabled=*/false,
                                    /*also_used_as_anonymous=*/false);
  if (!ns.ok()) {
    return ns.error();
  }
  g_nativeloader_extra_libs_namespace = new NativeLoaderNamespace(std::move(ns.value()));
  Result<void> linked =
      CreateNativeloaderDefaultNamespaceLibsLink(*g_nativeloader_extra_libs_namespace);
  if (!linked.ok()) {
    return linked.error();
  }
  return g_nativeloader_extra_libs_namespace;
}

// If the given path matches a library in NATIVELOADER_DEFAULT_NAMESPACE_LIBS
// then load it in the nativeloader-extra-libs namespace, otherwise return
// nullptr without error. This is only enabled if the ro.debuggable is true.
Result<void*> TryLoadNativeloaderExtraLib(const char* path) {
  if (!Debuggable()) {
    return nullptr;
  }
  const char* links = getenv("NATIVELOADER_DEFAULT_NAMESPACE_LIBS");
  if (links == nullptr || *links == 0) {
    return nullptr;
  }
  std::vector<std::string> lib_list = base::Split(links, ":");
  if (std::find(lib_list.begin(), lib_list.end(), path) == lib_list.end()) {
    return nullptr;
  }

  std::lock_guard<std::mutex> guard(g_namespaces_mutex);
  Result<NativeLoaderNamespace*> ns = GetNativeloaderExtraLibsNamespace();
  if (!ns.ok()) {
    return ns.error();
  }
  return ns.value()->Load(path);
}

Result<NativeLoaderNamespace*> CreateClassLoaderNamespaceLocked(JNIEnv* env,
                                                                int32_t target_sdk_version,
                                                                jobject class_loader,
                                                                bool is_shared,
                                                                jstring dex_path,
                                                                jstring library_path,
                                                                jstring permitted_path,
                                                                jstring uses_library_list)
    REQUIRES(g_namespaces_mutex) {
  Result<NativeLoaderNamespace*> ns = g_namespaces->Create(env,
                                                           target_sdk_version,
                                                           class_loader,
                                                           is_shared,
                                                           dex_path,
                                                           library_path,
                                                           permitted_path,
                                                           uses_library_list);
  if (!ns.ok()) {
    return ns;
  }
  if (Debuggable()) {
    Result<void> linked = CreateNativeloaderDefaultNamespaceLibsLink(*ns.value());
    if (!linked.ok()) {
      return linked.error();
    }
  }
  return ns;
}

#endif  // #if defined(ART_TARGET_ANDROID)

}  // namespace

void InitializeNativeLoader() {
#if defined(ART_TARGET_ANDROID)
  std::lock_guard<std::mutex> guard(g_namespaces_mutex);
  g_namespaces->Initialize();
#endif
}

void ResetNativeLoader() {
#if defined(ART_TARGET_ANDROID)
  std::lock_guard<std::mutex> guard(g_namespaces_mutex);
  g_namespaces->Reset();
  delete(g_nativeloader_extra_libs_namespace);
  g_nativeloader_extra_libs_namespace = nullptr;
#endif
}

jstring CreateClassLoaderNamespace(JNIEnv* env, int32_t target_sdk_version, jobject class_loader,
                                   bool is_shared, jstring dex_path, jstring library_path,
                                   jstring permitted_path, jstring uses_library_list) {
#if defined(ART_TARGET_ANDROID)
  std::lock_guard<std::mutex> guard(g_namespaces_mutex);
  Result<NativeLoaderNamespace*> ns = CreateClassLoaderNamespaceLocked(env,
                                                                       target_sdk_version,
                                                                       class_loader,
                                                                       is_shared,
                                                                       dex_path,
                                                                       library_path,
                                                                       permitted_path,
                                                                       uses_library_list);
  if (!ns.ok()) {
    return env->NewStringUTF(ns.error().message().c_str());
  }
#else
  UNUSED(env, target_sdk_version, class_loader, is_shared, dex_path, library_path, permitted_path,
         uses_library_list);
#endif
  return nullptr;
}

void* OpenNativeLibrary(JNIEnv* env, int32_t target_sdk_version, const char* path,
                        jobject class_loader, const char* caller_location, jstring library_path,
                        bool* needs_native_bridge, char** error_msg) {
#if defined(ART_TARGET_ANDROID)
  UNUSED(target_sdk_version);

  if (class_loader == nullptr) {
    *needs_native_bridge = false;
    if (caller_location != nullptr) {
      android_namespace_t* boot_namespace = FindExportedNamespace(caller_location);
      if (boot_namespace != nullptr) {
        const android_dlextinfo dlextinfo = {
            .flags = ANDROID_DLEXT_USE_NAMESPACE,
            .library_namespace = boot_namespace,
        };
        void* handle = android_dlopen_ext(path, RTLD_NOW, &dlextinfo);
        if (handle == nullptr) {
          *error_msg = strdup(dlerror());
        }
        return handle;
      }
    }

    // Check if the library is in NATIVELOADER_DEFAULT_NAMESPACE_LIBS and should
    // be loaded from the kNativeloaderExtraLibs namespace.
    {
      Result<void*> handle = TryLoadNativeloaderExtraLib(path);
      if (!handle.ok()) {
        *error_msg = strdup(handle.error().message().c_str());
        return nullptr;
      }
      if (handle.value() != nullptr) {
        return handle.value();
      }
    }

    // Fall back to the system namespace. This happens for preloaded JNI
    // libraries in the zygote.
    // TODO(b/185833744): Investigate if this should fall back to the app main
    // namespace (aka anonymous namespace) instead.
    void* handle = OpenSystemLibrary(path, RTLD_NOW);
    if (handle == nullptr) {
      *error_msg = strdup(dlerror());
    }
    return handle;
  }

  std::lock_guard<std::mutex> guard(g_namespaces_mutex);
  NativeLoaderNamespace* ns;

  if ((ns = g_namespaces->FindNamespaceByClassLoader(env, class_loader)) == nullptr) {
    // This is the case where the classloader was not created by ApplicationLoaders
    // In this case we create an isolated not-shared namespace for it.
    Result<NativeLoaderNamespace*> isolated_ns =
        CreateClassLoaderNamespaceLocked(env,
                                         target_sdk_version,
                                         class_loader,
                                         /*is_shared=*/false,
                                         /*dex_path=*/nullptr,
                                         library_path,
                                         /*permitted_path=*/nullptr,
                                         /*uses_library_list=*/nullptr);
    if (!isolated_ns.ok()) {
      *error_msg = strdup(isolated_ns.error().message().c_str());
      return nullptr;
    } else {
      ns = *isolated_ns;
    }
  }

  return OpenNativeLibraryInNamespace(ns, path, needs_native_bridge, error_msg);
#else
  UNUSED(env, target_sdk_version, class_loader, caller_location);

  // Do some best effort to emulate library-path support. It will not
  // work for dependencies.
  //
  // Note: null has a special meaning and must be preserved.
  std::string c_library_path;  // Empty string by default.
  if (library_path != nullptr && path != nullptr && path[0] != '/') {
    ScopedUtfChars library_path_utf_chars(env, library_path);
    c_library_path = library_path_utf_chars.c_str();
  }

  std::vector<std::string> library_paths = base::Split(c_library_path, ":");

  for (const std::string& lib_path : library_paths) {
    *needs_native_bridge = false;
    const char* path_arg;
    std::string complete_path;
    if (path == nullptr) {
      // Preserve null.
      path_arg = nullptr;
    } else {
      complete_path = lib_path;
      if (!complete_path.empty()) {
        complete_path.append("/");
      }
      complete_path.append(path);
      path_arg = complete_path.c_str();
    }
    void* handle = dlopen(path_arg, RTLD_NOW);
    if (handle != nullptr) {
      return handle;
    }
    if (NativeBridgeIsSupported(path_arg)) {
      *needs_native_bridge = true;
      handle = NativeBridgeLoadLibrary(path_arg, RTLD_NOW);
      if (handle != nullptr) {
        return handle;
      }
      *error_msg = strdup(NativeBridgeGetError());
    } else {
      *error_msg = strdup(dlerror());
    }
  }
  return nullptr;
#endif
}

bool CloseNativeLibrary(void* handle, const bool needs_native_bridge, char** error_msg) {
  bool success;
  if (needs_native_bridge) {
    success = (NativeBridgeUnloadLibrary(handle) == 0);
    if (!success) {
      *error_msg = strdup(NativeBridgeGetError());
    }
  } else {
    success = (dlclose(handle) == 0);
    if (!success) {
      *error_msg = strdup(dlerror());
    }
  }

  return success;
}

void NativeLoaderFreeErrorMessage(char* msg) {
  // The error messages get allocated through strdup, so we must call free on them.
  free(msg);
}

#if defined(ART_TARGET_ANDROID)
void* OpenNativeLibraryInNamespace(NativeLoaderNamespace* ns, const char* path,
                                   bool* needs_native_bridge, char** error_msg) {
  auto handle = ns->Load(path);
  if (!handle.ok() && error_msg != nullptr) {
    *error_msg = strdup(handle.error().message().c_str());
  }
  if (needs_native_bridge != nullptr) {
    *needs_native_bridge = ns->IsBridged();
  }
  return handle.ok() ? *handle : nullptr;
}

// native_bridge_namespaces are not supported for callers of this function.
// This function will return nullptr in the case when application is running
// on native bridge.
android_namespace_t* FindNamespaceByClassLoader(JNIEnv* env, jobject class_loader) {
  std::lock_guard<std::mutex> guard(g_namespaces_mutex);
  NativeLoaderNamespace* ns = g_namespaces->FindNamespaceByClassLoader(env, class_loader);
  if (ns != nullptr && !ns->IsBridged()) {
    return ns->ToRawAndroidNamespace();
  }
  return nullptr;
}

NativeLoaderNamespace* FindNativeLoaderNamespaceByClassLoader(JNIEnv* env, jobject class_loader) {
  std::lock_guard<std::mutex> guard(g_namespaces_mutex);
  return g_namespaces->FindNamespaceByClassLoader(env, class_loader);
}

void LinkNativeLoaderNamespaceToExportedNamespaceLibrary(struct NativeLoaderNamespace* ns,
                                                         const char* exported_ns_name,
                                                         const char* library_name,
                                                         char** error_msg) {
  Result<NativeLoaderNamespace> exported_ns =
      NativeLoaderNamespace::GetExportedNamespace(exported_ns_name, ns->IsBridged());
  if (!exported_ns.ok()) {
    *error_msg = strdup(exported_ns.error().message().c_str());
    return;
  }

  Result<void> linked = ns->Link(&exported_ns.value(), std::string(library_name));
  if (!linked.ok()) {
    *error_msg = strdup(linked.error().message().c_str());
  }
}

#endif  // ART_TARGET_ANDROID

};  // namespace android
