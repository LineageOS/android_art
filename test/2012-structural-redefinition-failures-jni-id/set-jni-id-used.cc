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

#include <stdio.h>

#include <vector>

#include "android-base/logging.h"
#include "android-base/macros.h"
#include "handle_scope-inl.h"
#include "jni.h"
#include "jvmti.h"

// Test infrastructure
#include "jvmti_helper.h"
#include "mirror/class.h"
#include "mirror/class_ext.h"
#include "scoped_local_ref.h"
#include "scoped_thread_state_change-inl.h"
#include "test_env.h"

namespace art {
namespace Test2012SetJniIdUsed {

extern "C" JNIEXPORT void JNICALL Java_Main_SetPointerIdsUsed(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jclass target) {
  ScopedObjectAccess soa(env);
  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::Class> h(hs.NewHandle(soa.Decode<mirror::Class>(target)));
  ObjPtr<mirror::ClassExt> ext(h->EnsureExtDataPresent(h, soa.Self()));
  CHECK(!ext.IsNull());
  ext->SetIdsArraysForClassExtExtData(Runtime::Current()->GetJniIdManager()->GetPointerMarker());
}

}  // namespace Test2012SetJniIdUsed
}  // namespace art
