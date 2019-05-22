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


#include <string>
#include <iostream>
#include <sstream>

#include "jvmti.h"

#include "base/logging.h"
#include "base/globals.h"
#include "base/memfd.h"

#ifdef __linux__
#include <sys/utsname.h>
#endif

namespace art {
namespace Test1963AddToDexClassLoaderInMemory {

extern "C" JNIEXPORT jboolean JNICALL Java_Main_hasWorkingMemfdCreate(JNIEnv*, jclass) {
  // We should always have a working version if we're on normal buildbots.
  if (!art::kIsTargetBuild) {
    return true;
  }
#ifdef __linux__
  struct utsname name;
  if (uname(&name) >= 0) {
    std::istringstream version(name.release);
    std::string major_str;
    std::string minor_str;
    std::getline(version, major_str, '.');
    std::getline(version, minor_str, '.');
    int major = std::stoi(major_str);
    int minor = std::stoi(minor_str);
    if (major >= 4 || (major == 3 && minor >= 17)) {
      // memfd_create syscall was added in 3.17
      return true;
    }
  }
#endif
  int res = memfd_create_compat("TEST THAT MEMFD CREATE WORKS", 0);
  if (res < 0) {
    PLOG(ERROR) << "Unable to call memfd_create_compat successfully!";
    return false;
  } else {
    close(res);
    return true;
  }
}

}  // namespace Test1963AddToDexClassLoaderInMemory
}  // namespace art
