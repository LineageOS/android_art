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

#include "critical_native_abi_fixup_arm.h"

#include "art_method-inl.h"
#include "intrinsics.h"
#include "jni/jni_internal.h"
#include "nodes.h"
#include "scoped_thread_state_change-inl.h"
#include "well_known_classes.h"

namespace art {
namespace arm {

// Fix up FP arguments passed in core registers for call to @CriticalNative by inserting fake calls
// to Float.floatToRawIntBits() or Double.doubleToRawLongBits() to satisfy type consistency checks.
static void FixUpArguments(HInvokeStaticOrDirect* invoke) {
  DCHECK_EQ(invoke->GetCodePtrLocation(),
            HInvokeStaticOrDirect::CodePtrLocation::kCallCriticalNative);
  size_t reg = 0u;
  for (size_t i = 0, num_args = invoke->GetNumberOfArguments(); i != num_args; ++i) {
    HInstruction* input = invoke->InputAt(i);
    DataType::Type input_type = input->GetType();
    size_t next_reg = reg + 1u;
    if (DataType::Is64BitType(input_type)) {
      reg = RoundUp(reg, 2u);
      next_reg = reg + 2u;
    }
    if (reg == 4u) {
      break;  // Remaining arguments are passed on stack.
    }
    if (DataType::IsFloatingPointType(input_type)) {
      bool is_double = (input_type == DataType::Type::kFloat64);
      DataType::Type converted_type = is_double ? DataType::Type::kInt64 : DataType::Type::kInt32;
      jmethodID known_method = is_double ? WellKnownClasses::java_lang_Double_doubleToRawLongBits
                                         : WellKnownClasses::java_lang_Float_floatToRawIntBits;
      ArtMethod* resolved_method = jni::DecodeArtMethod(known_method);
      DCHECK(resolved_method != nullptr);
      MethodReference target_method(nullptr, 0);
      {
        ScopedObjectAccess soa(Thread::Current());
        target_method =
            MethodReference(resolved_method->GetDexFile(), resolved_method->GetDexMethodIndex());
      }
      // Use arbitrary dispatch info that does not require the method argument.
      HInvokeStaticOrDirect::DispatchInfo dispatch_info = {
          HInvokeStaticOrDirect::MethodLoadKind::kBssEntry,
          HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod,
          /*method_load_data=*/ 0u
      };
      HBasicBlock* block = invoke->GetBlock();
      ArenaAllocator* allocator = block->GetGraph()->GetAllocator();
      HInvokeStaticOrDirect* new_input = new (allocator) HInvokeStaticOrDirect(
          allocator,
          /*number_of_arguments=*/ 1u,
          converted_type,
          invoke->GetDexPc(),
          /*method_index=*/ dex::kDexNoIndex,
          resolved_method,
          dispatch_info,
          kStatic,
          target_method,
          HInvokeStaticOrDirect::ClinitCheckRequirement::kNone);
      // The intrinsic has no side effects and does not need environment or dex cache on ARM.
      new_input->SetSideEffects(SideEffects::None());
      IntrinsicOptimizations opt(new_input);
      opt.SetDoesNotNeedDexCache();
      opt.SetDoesNotNeedEnvironment();
      new_input->SetRawInputAt(0u, input);
      block->InsertInstructionBefore(new_input, invoke);
      invoke->ReplaceInput(new_input, i);
    }
    reg = next_reg;
  }
}

bool CriticalNativeAbiFixupArm::Run() {
  if (!graph_->HasDirectCriticalNativeCall()) {
    return false;
  }

  for (HBasicBlock* block : graph_->GetReversePostOrder()) {
    for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
      HInstruction* instruction = it.Current();
      if (instruction->IsInvokeStaticOrDirect() &&
          instruction->AsInvokeStaticOrDirect()->GetCodePtrLocation() ==
              HInvokeStaticOrDirect::CodePtrLocation::kCallCriticalNative) {
        FixUpArguments(instruction->AsInvokeStaticOrDirect());
      }
    }
  }
  return true;
}

}  // namespace arm
}  // namespace art
