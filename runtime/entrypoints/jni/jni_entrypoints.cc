/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include <android-base/logging.h>

#include "arch/arm/jni_frame_arm.h"
#include "arch/arm64/jni_frame_arm64.h"
#include "arch/instruction_set.h"
#include "arch/x86/jni_frame_x86.h"
#include "arch/x86_64/jni_frame_x86_64.h"
#include "art_method-inl.h"
#include "entrypoints/entrypoint_utils.h"
#include "jni/java_vm_ext.h"
#include "mirror/object-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"

namespace art {

// Used by the JNI dlsym stub to find the native method to invoke if none is registered.
extern "C" const void* artFindNativeMethodRunnable(Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  Locks::mutator_lock_->AssertSharedHeld(self);  // We come here as Runnable.
  ArtMethod* method = self->GetCurrentMethod(nullptr);
  DCHECK(method != nullptr);

  // Lookup symbol address for method, on failure we'll return null with an exception set,
  // otherwise we return the address of the method we found.
  JavaVMExt* vm = down_cast<JNIEnvExt*>(self->GetJniEnv())->GetVm();
  void* native_code = vm->FindCodeForNativeMethod(method);
  if (native_code == nullptr) {
    self->AssertPendingException();
    return nullptr;
  }
  // Register so that future calls don't come here
  return method->RegisterNative(native_code);
}

// Used by the JNI dlsym stub to find the native method to invoke if none is registered.
extern "C" const void* artFindNativeMethod(Thread* self) {
  DCHECK_EQ(self, Thread::Current());
  Locks::mutator_lock_->AssertNotHeld(self);  // We come here as Native.
  ScopedObjectAccess soa(self);
  return artFindNativeMethodRunnable(self);
}

extern "C" size_t artCriticalNativeOutArgsSize(ArtMethod* method)
    REQUIRES_SHARED(Locks::mutator_lock_)  {
  uint32_t shorty_len;
  const char* shorty = method->GetShorty(&shorty_len);
  switch (kRuntimeISA) {
    case InstructionSet::kArm:
    case InstructionSet::kThumb2:
      return arm::GetCriticalNativeOutArgsSize(shorty, shorty_len);
    case InstructionSet::kArm64:
      return arm64::GetCriticalNativeOutArgsSize(shorty, shorty_len);
    case InstructionSet::kX86:
      return x86::GetCriticalNativeOutArgsSize(shorty, shorty_len);
    case InstructionSet::kX86_64:
      return x86_64::GetCriticalNativeOutArgsSize(shorty, shorty_len);
    default:
      UNIMPLEMENTED(FATAL) << kRuntimeISA;
      UNREACHABLE();
  }
}

}  // namespace art
