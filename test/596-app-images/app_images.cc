/*
 * Copyright 2016 The Android Open Source Project
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

#include <pthread.h>

#include <cstdio>
#include <iostream>
#include <vector>

#include "jni.h"

#include "gc/heap.h"
#include "gc/space/image_space.h"
#include "gc/space/space-inl.h"
#include "image.h"
#include "mirror/class.h"
#include "nativehelper/scoped_utf_chars.h"
#include "oat_file.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"

namespace art {

namespace {

// Returns whether the extensionless basename of `location` is equal to name.
// E.g. check_name("/foo/bar/baz.odex", "baz") == true,
//      check_name("/foo/bar/baz.odex", "bar") == false
static bool check_name(const std::string& location, const std::string& name) {
  std::string loc_name = location;
  size_t idx = loc_name.rfind('/');
  if (idx != std::string::npos) {
    loc_name = loc_name.substr(idx + 1);
  }
  idx = loc_name.rfind('.');
  if (idx != std::string::npos) {
    loc_name = loc_name.substr(0, idx);
  }
  return loc_name == name;
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_checkAppImageLoaded(JNIEnv* env,
                                                                    jclass,
                                                                    jstring jimage_name) {
  ScopedUtfChars image_name(env, jimage_name);
  ScopedObjectAccess soa(Thread::Current());
  for (auto* space : Runtime::Current()->GetHeap()->GetContinuousSpaces()) {
    if (space->IsImageSpace()) {
      auto* image_space = space->AsImageSpace();
      const auto& image_header = image_space->GetImageHeader();
      // Check that this is an app image associated with the dex file named
      // `jname` by verifying the extensionless basename of the odex file
      // location is equal to `jname`.
      if (image_header.IsAppImage() &&
          check_name(image_space->GetOatFile()->GetLocation(), image_name.c_str())) {
        return JNI_TRUE;
      }
    }
  }
  return JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_checkAppImageContains(JNIEnv*, jclass, jclass c) {
  ScopedObjectAccess soa(Thread::Current());
  ObjPtr<mirror::Class> klass_ptr = soa.Decode<mirror::Class>(c);
  for (auto* space : Runtime::Current()->GetHeap()->GetContinuousSpaces()) {
    if (space->IsImageSpace()) {
      auto* image_space = space->AsImageSpace();
      const auto& image_header = image_space->GetImageHeader();
      if (image_header.IsAppImage()) {
        if (image_space->HasAddress(klass_ptr.Ptr())) {
          return JNI_TRUE;
        }
      }
    }
  }
  return JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_checkInitialized(JNIEnv*, jclass, jclass c) {
  ScopedObjectAccess soa(Thread::Current());
  ObjPtr<mirror::Class> klass_ptr = soa.Decode<mirror::Class>(c);
  return klass_ptr->IsInitialized();
}

}  // namespace

}  // namespace art
