/*
 * Copyright 2019 The Android Open Source Project
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


#include "jni.h"

#include "class_linker.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"

namespace art {

extern "C" JNIEXPORT void JNICALL Java_Main_makeVisiblyInitialized(JNIEnv*, jclass) {
  Runtime::Current()->GetClassLinker()->MakeInitializedClassesVisiblyInitialized(
      Thread::Current(), /*wait=*/ true);
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_isVisiblyInitialized(JNIEnv*, jclass, jclass c) {
  ScopedObjectAccess soa(Thread::Current());
  ObjPtr<mirror::Class> klass = soa.Decode<mirror::Class>(c);
  return klass->IsVisiblyInitialized() ? JNI_TRUE : JNI_FALSE;
}

}  // namespace art
