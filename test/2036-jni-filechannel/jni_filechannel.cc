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

#include <iostream>
#include <jni.h>

extern "C" JNIEXPORT void Java_Main_checkBufferCapacity(JNIEnv* env,
                                                        jclass /*clazz*/,
                                                        jobject buffer,
                                                        jint expectedCapacity) {
  jlong capacity = env->GetDirectBufferCapacity(buffer);
  const char* status = (capacity == expectedCapacity) ? "PASS" : "FAIL";
  std::cout << "Expected " << expectedCapacity
            << " got " << capacity
            << " " << status << std::endl;
}
