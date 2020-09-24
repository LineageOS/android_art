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

#ifndef ART_COMPILER_OPTIMIZING_INTRINSICS_UTILS_H_
#define ART_COMPILER_OPTIMIZING_INTRINSICS_UTILS_H_

#include "base/casts.h"
#include "base/macros.h"
#include "code_generator.h"
#include "data_type-inl.h"
#include "dex/dex_file-inl.h"
#include "locations.h"
#include "mirror/var_handle.h"
#include "nodes.h"
#include "utils/assembler.h"
#include "utils/label.h"

namespace art {

// Default slow-path for fallback (calling the managed code to handle the intrinsic) in an
// intrinsified call. This will copy the arguments into the positions for a regular call.
//
// Note: The actual parameters are required to be in the locations given by the invoke's location
//       summary. If an intrinsic modifies those locations before a slowpath call, they must be
//       restored!
//
// Note: If an invoke wasn't sharpened, we will put down an invoke-virtual here. That's potentially
//       sub-optimal (compared to a direct pointer call), but this is a slow-path.

template <typename TDexCallingConvention,
          typename TSlowPathCode = SlowPathCode,
          typename TAssembler = Assembler>
class IntrinsicSlowPath : public TSlowPathCode {
 public:
  explicit IntrinsicSlowPath(HInvoke* invoke) : TSlowPathCode(invoke), invoke_(invoke) { }

  Location MoveArguments(CodeGenerator* codegen) {
    TDexCallingConvention calling_convention_visitor;
    IntrinsicVisitor::MoveArguments(invoke_, codegen, &calling_convention_visitor);
    return calling_convention_visitor.GetMethodLocation();
  }

  void EmitNativeCode(CodeGenerator* codegen) override {
    TAssembler* assembler = down_cast<TAssembler*>(codegen->GetAssembler());
    assembler->Bind(this->GetEntryLabel());

    this->SaveLiveRegisters(codegen, invoke_->GetLocations());

    Location method_loc = MoveArguments(codegen);

    if (invoke_->IsInvokeStaticOrDirect()) {
      HInvokeStaticOrDirect* invoke_static_or_direct = invoke_->AsInvokeStaticOrDirect();
      DCHECK_NE(invoke_static_or_direct->GetMethodLoadKind(), MethodLoadKind::kRecursive);
      DCHECK_NE(invoke_static_or_direct->GetCodePtrLocation(),
                CodePtrLocation::kCallCriticalNative);
      codegen->GenerateStaticOrDirectCall(invoke_static_or_direct, method_loc, this);
    } else if (invoke_->IsInvokeVirtual()) {
      codegen->GenerateVirtualCall(invoke_->AsInvokeVirtual(), method_loc, this);
    } else {
      DCHECK(invoke_->IsInvokePolymorphic());
      codegen->GenerateInvokePolymorphicCall(invoke_->AsInvokePolymorphic(), this);
    }

    // Copy the result back to the expected output.
    Location out = invoke_->GetLocations()->Out();
    if (out.IsValid()) {
      DCHECK(out.IsRegisterKind());  // TODO: Replace this when we support output in memory.
      // We want to double-check that we don't overwrite a live register with the return
      // value.
      // Note: For the possible kNoOutputOverlap case we can't simply remove the OUT register
      // from the GetLiveRegisters() - theoretically it might be needed after the return from
      // the slow path.
      DCHECK(!invoke_->GetLocations()->GetLiveRegisters()->OverlapsRegisters(out));
      codegen->MoveFromReturnRegister(out, invoke_->GetType());
    }

    this->RestoreLiveRegisters(codegen, invoke_->GetLocations());
    assembler->Jump(this->GetExitLabel());
  }

  const char* GetDescription() const override { return "IntrinsicSlowPath"; }

 private:
  // The instruction where this slow path is happening.
  HInvoke* const invoke_;

  DISALLOW_COPY_AND_ASSIGN(IntrinsicSlowPath);
};

static inline size_t GetExpectedVarHandleCoordinatesCount(HInvoke *invoke) {
  mirror::VarHandle::AccessModeTemplate access_mode_template =
      mirror::VarHandle::GetAccessModeTemplateByIntrinsic(invoke->GetIntrinsic());
  size_t var_type_count = mirror::VarHandle::GetNumberOfVarTypeParameters(access_mode_template);
  size_t accessor_argument_count = invoke->GetNumberOfArguments() - 1;

  return accessor_argument_count - var_type_count;
}

static inline DataType::Type GetDataTypeFromShorty(HInvoke* invoke, uint32_t index) {
  DCHECK(invoke->IsInvokePolymorphic());
  const DexFile& dex_file = invoke->GetBlock()->GetGraph()->GetDexFile();
  const char* shorty = dex_file.GetShorty(invoke->AsInvokePolymorphic()->GetProtoIndex());
  DCHECK_LT(index, strlen(shorty));

  return DataType::FromShorty(shorty[index]);
}

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_INTRINSICS_UTILS_H_
