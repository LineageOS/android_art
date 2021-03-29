/*
 * Copyright (C) 2021 The Android Open Source Project
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

#define LOG_TAG "ArtifactsSignedTest"

#include "jni.h"
#include <nativehelper/JNIHelp.h>
#include <nativehelper/ScopedUtfChars.h>

#include <android/log.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" JNIEXPORT jboolean JNICALL
Java_com_android_tests_odsign_ArtifactsSignedTest_hasFsverityNative(
    JNIEnv *env, jobject /*thiz*/, jstring filePath) {
  ScopedUtfChars path(env, filePath);

  struct statx out = {};
  if (statx(AT_FDCWD, path.c_str(), /*flags=*/ 0, STATX_ALL, &out) != 0) {
    ALOGE("statx failed at %s", path.c_str());
    return JNI_FALSE;
  }

  // Make sure we actually support ATTR_VERITY
  if ((out.stx_attributes_mask & STATX_ATTR_VERITY) == 0) {
    ALOGE("STATX_ATTR_VERITY not supported by kernel");
    return JNI_FALSE;
  }

  return (out.stx_attributes & STATX_ATTR_VERITY) != 0 ? JNI_TRUE : JNI_FALSE;
}
