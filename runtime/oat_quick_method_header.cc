/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "oat_quick_method_header.h"

#include "arch/instruction_set.h"
#include "art_method.h"
#include "dex/dex_file_types.h"
#include "interpreter/interpreter_mterp_impl.h"
#include "interpreter/mterp/mterp.h"
#include "nterp_helpers.h"
#include "scoped_thread_state_change-inl.h"
#include "stack_map.h"
#include "thread.h"

namespace art {

uint32_t OatQuickMethodHeader::ToDexPc(ArtMethod** frame,
                                       const uintptr_t pc,
                                       bool abort_on_failure) const {
  ArtMethod* method = *frame;
  const void* entry_point = GetEntryPoint();
  uint32_t sought_offset = pc - reinterpret_cast<uintptr_t>(entry_point);
  if (method->IsNative()) {
    return dex::kDexNoIndex;
  } else if (IsNterpMethodHeader()) {
    return NterpGetDexPC(frame);
  } else {
    DCHECK(IsOptimized());
    CodeInfo code_info = CodeInfo::DecodeInlineInfoOnly(this);
    StackMap stack_map = code_info.GetStackMapForNativePcOffset(sought_offset);
    if (stack_map.IsValid()) {
      return stack_map.GetDexPc();
    }
  }
  if (abort_on_failure) {
    LOG(FATAL) << "Failed to find Dex offset for PC offset "
           << reinterpret_cast<void*>(sought_offset)
           << "(PC " << reinterpret_cast<void*>(pc) << ", entry_point=" << entry_point
           << " current entry_point=" << method->GetEntryPointFromQuickCompiledCode()
           << ") in " << method->PrettyMethod();
  }
  return dex::kDexNoIndex;
}

uintptr_t OatQuickMethodHeader::ToNativeQuickPc(ArtMethod* method,
                                                const uint32_t dex_pc,
                                                bool is_for_catch_handler,
                                                bool abort_on_failure) const {
  const void* entry_point = GetEntryPoint();
  DCHECK(!method->IsNative());
  if (IsNterpMethodHeader()) {
    // This should only be called on an nterp frame for getting a catch handler.
    CHECK(is_for_catch_handler);
    return NterpGetCatchHandler();
  }
  DCHECK(IsOptimized());
  // Search for the dex-to-pc mapping in stack maps.
  CodeInfo code_info = CodeInfo::DecodeInlineInfoOnly(this);

  // All stack maps are stored in the same CodeItem section, safepoint stack
  // maps first, then catch stack maps. We use `is_for_catch_handler` to select
  // the order of iteration.
  StackMap stack_map =
      LIKELY(is_for_catch_handler) ? code_info.GetCatchStackMapForDexPc(dex_pc)
                                   : code_info.GetStackMapForDexPc(dex_pc);
  if (stack_map.IsValid()) {
    return reinterpret_cast<uintptr_t>(entry_point) +
           stack_map.GetNativePcOffset(kRuntimeISA);
  }
  if (abort_on_failure) {
    ScopedObjectAccess soa(Thread::Current());
    LOG(FATAL) << "Failed to find native offset for dex pc 0x" << std::hex << dex_pc
               << " in " << method->PrettyMethod();
  }
  return UINTPTR_MAX;
}

static inline OatQuickMethodHeader* GetNterpMethodHeader() {
  if (!interpreter::IsNterpSupported()) {
    return nullptr;
  }
  uintptr_t nterp_entrypoint = reinterpret_cast<uintptr_t>(interpreter::GetNterpEntryPoint());
  uintptr_t nterp_code_pointer = (kRuntimeISA == InstructionSet::kArm)
      // Remove the Thumb mode bit if present on ARM.
      ? nterp_entrypoint & ~static_cast<uintptr_t>(1)
      : nterp_entrypoint;
  return reinterpret_cast<OatQuickMethodHeader*>(nterp_code_pointer - sizeof(OatQuickMethodHeader));
}

OatQuickMethodHeader* OatQuickMethodHeader::NterpMethodHeader = GetNterpMethodHeader();

bool OatQuickMethodHeader::IsNterpMethodHeader() const {
  return interpreter::IsNterpSupported() ? (this == NterpMethodHeader) : false;
}

}  // namespace art
