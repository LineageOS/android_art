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
#include "dex/dex_instruction-inl.h"
#include "dex/method_reference.h"
#include "entrypoints/entrypoint_utils-inl.h"
#include "jni/java_vm_ext.h"
#include "mirror/object-inl.h"
#include "oat_quick_method_header.h"
#include "scoped_thread_state_change-inl.h"
#include "stack_map.h"
#include "thread.h"

namespace art {

static inline uint32_t GetInvokeStaticMethodIndex(ArtMethod* caller, uint32_t dex_pc)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // Get the DexFile and method index.
  const Instruction& instruction = caller->DexInstructions().InstructionAt(dex_pc);
  DCHECK(instruction.Opcode() == Instruction::INVOKE_STATIC ||
         instruction.Opcode() == Instruction::INVOKE_STATIC_RANGE);
  uint32_t method_idx = (instruction.Opcode() == Instruction::INVOKE_STATIC)
      ? instruction.VRegB_35c()
      : instruction.VRegB_3rc();
  return method_idx;
}

// Used by the JNI dlsym stub to find the native method to invoke if none is registered.
extern "C" const void* artFindNativeMethodRunnable(Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  Locks::mutator_lock_->AssertSharedHeld(self);  // We come here as Runnable.
  uint32_t dex_pc;
  ArtMethod* method = self->GetCurrentMethod(&dex_pc);
  DCHECK(method != nullptr);
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();

  if (!method->IsNative()) {
    // We're coming from compiled managed code and the `method` we see here is the caller.
    // Resolve target @CriticalNative method for a direct call from compiled managed code.
    uint32_t method_idx = GetInvokeStaticMethodIndex(method, dex_pc);
    ArtMethod* target_method = class_linker->ResolveMethod<ClassLinker::ResolveMode::kNoChecks>(
        self, method_idx, method, kStatic);
    if (target_method == nullptr) {
      self->AssertPendingException();
      return nullptr;
    }
    DCHECK(target_method->IsCriticalNative());
    MaybeUpdateBssMethodEntry(target_method, MethodReference(method->GetDexFile(), method_idx));

    // These calls do not have an explicit class initialization check, so do the check now.
    // (When going through the stub or GenericJNI, the check was already done.)
    DCHECK(NeedsClinitCheckBeforeCall(target_method));
    ObjPtr<mirror::Class> declaring_class = target_method->GetDeclaringClass();
    if (UNLIKELY(!declaring_class->IsVisiblyInitialized())) {
      StackHandleScope<1> hs(self);
      Handle<mirror::Class> h_class(hs.NewHandle(declaring_class));
      if (!class_linker->EnsureInitialized(self, h_class, true, true)) {
        DCHECK(self->IsExceptionPending()) << method->PrettyMethod();
        return nullptr;
      }
    }

    // Replace the runtime method on the stack with the target method.
    DCHECK(!self->GetManagedStack()->GetTopQuickFrameTag());
    ArtMethod** sp = self->GetManagedStack()->GetTopQuickFrameKnownNotTagged();
    DCHECK(*sp == Runtime::Current()->GetCalleeSaveMethod(CalleeSaveType::kSaveRefsAndArgs));
    *sp = target_method;
    self->SetTopOfStackTagged(sp);  // Fake GenericJNI frame.

    // Continue with the target method.
    method = target_method;
  }
  DCHECK(method == self->GetCurrentMethod(/*dex_pc=*/ nullptr));

  // Check whether we already have a registered native code.
  // For @CriticalNative it may not be stored in the ArtMethod as a JNI entrypoint if the class
  // was not visibly initialized yet. Do this check also for @FastNative and normal native for
  // consistency; though success would mean that another thread raced to do this lookup.
  const void* native_code = class_linker->GetRegisteredNative(self, method);
  if (native_code != nullptr) {
    return native_code;
  }

  // Lookup symbol address for method, on failure we'll return null with an exception set,
  // otherwise we return the address of the method we found.
  JavaVMExt* vm = down_cast<JNIEnvExt*>(self->GetJniEnv())->GetVm();
  native_code = vm->FindCodeForNativeMethod(method);
  if (native_code == nullptr) {
    self->AssertPendingException();
    return nullptr;
  }

  // Register the code. This usually prevents future calls from coming to this function again.
  // We can still come here if the ClassLinker cannot set the entrypoint in the ArtMethod,
  // i.e. for @CriticalNative methods with the declaring class not visibly initialized.
  return class_linker->RegisterNative(self, method, native_code);
}

// Used by the JNI dlsym stub to find the native method to invoke if none is registered.
extern "C" const void* artFindNativeMethod(Thread* self) {
  DCHECK_EQ(self, Thread::Current());
  Locks::mutator_lock_->AssertNotHeld(self);  // We come here as Native.
  ScopedObjectAccess soa(self);
  return artFindNativeMethodRunnable(self);
}

extern "C" size_t artCriticalNativeFrameSize(ArtMethod* method, uintptr_t caller_pc)
    REQUIRES_SHARED(Locks::mutator_lock_)  {
  if (method->IsNative()) {
    // Get the method's shorty.
    DCHECK(method->IsCriticalNative());
    uint32_t shorty_len;
    const char* shorty = method->GetShorty(&shorty_len);

    // Return the platform-dependent stub frame size.
    switch (kRuntimeISA) {
      case InstructionSet::kArm:
      case InstructionSet::kThumb2:
        return arm::GetCriticalNativeStubFrameSize(shorty, shorty_len);
      case InstructionSet::kArm64:
        return arm64::GetCriticalNativeStubFrameSize(shorty, shorty_len);
      case InstructionSet::kX86:
        return x86::GetCriticalNativeStubFrameSize(shorty, shorty_len);
      case InstructionSet::kX86_64:
        return x86_64::GetCriticalNativeStubFrameSize(shorty, shorty_len);
      default:
        UNIMPLEMENTED(FATAL) << kRuntimeISA;
        UNREACHABLE();
    }
  } else {
    // We're coming from compiled managed code and the `method` we see here is the compiled
    // method that made the call. Get the actual caller (may be inlined) and dex pc.
    const OatQuickMethodHeader* current_code = method->GetOatQuickMethodHeader(caller_pc);
    DCHECK(current_code != nullptr);
    DCHECK(current_code->IsOptimized());
    uintptr_t native_pc_offset = current_code->NativeQuickPcOffset(caller_pc);
    CodeInfo code_info = CodeInfo::DecodeInlineInfoOnly(current_code);
    StackMap stack_map = code_info.GetStackMapForNativePcOffset(native_pc_offset);
    DCHECK(stack_map.IsValid());
    BitTableRange<InlineInfo> inline_infos = code_info.GetInlineInfosOf(stack_map);
    ArtMethod* caller =
        inline_infos.empty() ? method : GetResolvedMethod(method, code_info, inline_infos);
    uint32_t dex_pc = inline_infos.empty() ? stack_map.GetDexPc() : inline_infos.back().GetDexPc();

    // Get the callee shorty.
    const DexFile* dex_file = method->GetDexFile();
    uint32_t method_idx = GetInvokeStaticMethodIndex(caller, dex_pc);
    uint32_t shorty_len;
    const char* shorty = dex_file->GetMethodShorty(dex_file->GetMethodId(method_idx), &shorty_len);

    // Return the platform-dependent direct call frame size.
    switch (kRuntimeISA) {
      case InstructionSet::kArm:
      case InstructionSet::kThumb2:
        return arm::GetCriticalNativeDirectCallFrameSize(shorty, shorty_len);
      case InstructionSet::kArm64:
        return arm64::GetCriticalNativeDirectCallFrameSize(shorty, shorty_len);
      case InstructionSet::kX86:
        return x86::GetCriticalNativeDirectCallFrameSize(shorty, shorty_len);
      case InstructionSet::kX86_64:
        return x86_64::GetCriticalNativeDirectCallFrameSize(shorty, shorty_len);
      default:
        UNIMPLEMENTED(FATAL) << kRuntimeISA;
        UNREACHABLE();
    }
  }
}

}  // namespace art
