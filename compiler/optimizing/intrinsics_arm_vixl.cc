/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "intrinsics_arm_vixl.h"

#include "arch/arm/callee_save_frame_arm.h"
#include "arch/arm/instruction_set_features_arm.h"
#include "art_method.h"
#include "code_generator_arm_vixl.h"
#include "common_arm.h"
#include "heap_poisoning.h"
#include "intrinsics.h"
#include "intrinsics_utils.h"
#include "lock_word.h"
#include "mirror/array-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/reference.h"
#include "mirror/string-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"

#include "aarch32/constants-aarch32.h"

namespace art {
namespace arm {

#define __ assembler->GetVIXLAssembler()->

using helpers::DRegisterFrom;
using helpers::HighRegisterFrom;
using helpers::InputDRegisterAt;
using helpers::InputRegisterAt;
using helpers::InputSRegisterAt;
using helpers::Int32ConstantFrom;
using helpers::LocationFrom;
using helpers::LowRegisterFrom;
using helpers::LowSRegisterFrom;
using helpers::HighSRegisterFrom;
using helpers::OutputDRegister;
using helpers::OutputRegister;
using helpers::RegisterFrom;
using helpers::SRegisterFrom;

using namespace vixl::aarch32;  // NOLINT(build/namespaces)

using vixl::ExactAssemblyScope;
using vixl::CodeBufferCheckScope;

ArmVIXLAssembler* IntrinsicCodeGeneratorARMVIXL::GetAssembler() {
  return codegen_->GetAssembler();
}

ArenaAllocator* IntrinsicCodeGeneratorARMVIXL::GetAllocator() {
  return codegen_->GetGraph()->GetAllocator();
}

using IntrinsicSlowPathARMVIXL = IntrinsicSlowPath<InvokeDexCallingConventionVisitorARMVIXL,
                                                   SlowPathCodeARMVIXL,
                                                   ArmVIXLAssembler>;

// Compute base address for the System.arraycopy intrinsic in `base`.
static void GenSystemArrayCopyBaseAddress(ArmVIXLAssembler* assembler,
                                          DataType::Type type,
                                          const vixl32::Register& array,
                                          const Location& pos,
                                          const vixl32::Register& base) {
  // This routine is only used by the SystemArrayCopy intrinsic at the
  // moment. We can allow DataType::Type::kReference as `type` to implement
  // the SystemArrayCopyChar intrinsic.
  DCHECK_EQ(type, DataType::Type::kReference);
  const int32_t element_size = DataType::Size(type);
  const uint32_t element_size_shift = DataType::SizeShift(type);
  const uint32_t data_offset = mirror::Array::DataOffset(element_size).Uint32Value();

  if (pos.IsConstant()) {
    int32_t constant = Int32ConstantFrom(pos);
    __ Add(base, array, element_size * constant + data_offset);
  } else {
    __ Add(base, array, Operand(RegisterFrom(pos), vixl32::LSL, element_size_shift));
    __ Add(base, base, data_offset);
  }
}

// Compute end address for the System.arraycopy intrinsic in `end`.
static void GenSystemArrayCopyEndAddress(ArmVIXLAssembler* assembler,
                                         DataType::Type type,
                                         const Location& copy_length,
                                         const vixl32::Register& base,
                                         const vixl32::Register& end) {
  // This routine is only used by the SystemArrayCopy intrinsic at the
  // moment. We can allow DataType::Type::kReference as `type` to implement
  // the SystemArrayCopyChar intrinsic.
  DCHECK_EQ(type, DataType::Type::kReference);
  const int32_t element_size = DataType::Size(type);
  const uint32_t element_size_shift = DataType::SizeShift(type);

  if (copy_length.IsConstant()) {
    int32_t constant = Int32ConstantFrom(copy_length);
    __ Add(end, base, element_size * constant);
  } else {
    __ Add(end, base, Operand(RegisterFrom(copy_length), vixl32::LSL, element_size_shift));
  }
}

// Slow path implementing the SystemArrayCopy intrinsic copy loop with read barriers.
class ReadBarrierSystemArrayCopySlowPathARMVIXL : public SlowPathCodeARMVIXL {
 public:
  explicit ReadBarrierSystemArrayCopySlowPathARMVIXL(HInstruction* instruction)
      : SlowPathCodeARMVIXL(instruction) {
    DCHECK(kEmitCompilerReadBarrier);
    DCHECK(kUseBakerReadBarrier);
  }

  void EmitNativeCode(CodeGenerator* codegen) override {
    CodeGeneratorARMVIXL* arm_codegen = down_cast<CodeGeneratorARMVIXL*>(codegen);
    ArmVIXLAssembler* assembler = arm_codegen->GetAssembler();
    LocationSummary* locations = instruction_->GetLocations();
    DCHECK(locations->CanCall());
    DCHECK(instruction_->IsInvokeStaticOrDirect())
        << "Unexpected instruction in read barrier arraycopy slow path: "
        << instruction_->DebugName();
    DCHECK(instruction_->GetLocations()->Intrinsified());
    DCHECK_EQ(instruction_->AsInvoke()->GetIntrinsic(), Intrinsics::kSystemArrayCopy);

    DataType::Type type = DataType::Type::kReference;
    const int32_t element_size = DataType::Size(type);

    vixl32::Register dest = InputRegisterAt(instruction_, 2);
    Location dest_pos = locations->InAt(3);
    vixl32::Register src_curr_addr = RegisterFrom(locations->GetTemp(0));
    vixl32::Register dst_curr_addr = RegisterFrom(locations->GetTemp(1));
    vixl32::Register src_stop_addr = RegisterFrom(locations->GetTemp(2));
    vixl32::Register tmp = RegisterFrom(locations->GetTemp(3));

    __ Bind(GetEntryLabel());
    // Compute the base destination address in `dst_curr_addr`.
    GenSystemArrayCopyBaseAddress(assembler, type, dest, dest_pos, dst_curr_addr);

    vixl32::Label loop;
    __ Bind(&loop);
    __ Ldr(tmp, MemOperand(src_curr_addr, element_size, PostIndex));
    assembler->MaybeUnpoisonHeapReference(tmp);
    // TODO: Inline the mark bit check before calling the runtime?
    // tmp = ReadBarrier::Mark(tmp);
    // No need to save live registers; it's taken care of by the
    // entrypoint. Also, there is no need to update the stack mask,
    // as this runtime call will not trigger a garbage collection.
    // (See ReadBarrierMarkSlowPathARM::EmitNativeCode for more
    // explanations.)
    DCHECK(!tmp.IsSP());
    DCHECK(!tmp.IsLR());
    DCHECK(!tmp.IsPC());
    // IP is used internally by the ReadBarrierMarkRegX entry point
    // as a temporary (and not preserved).  It thus cannot be used by
    // any live register in this slow path.
    DCHECK(!src_curr_addr.Is(ip));
    DCHECK(!dst_curr_addr.Is(ip));
    DCHECK(!src_stop_addr.Is(ip));
    DCHECK(!tmp.Is(ip));
    DCHECK(tmp.IsRegister()) << tmp;
    // TODO: Load the entrypoint once before the loop, instead of
    // loading it at every iteration.
    int32_t entry_point_offset =
        Thread::ReadBarrierMarkEntryPointsOffset<kArmPointerSize>(tmp.GetCode());
    // This runtime call does not require a stack map.
    arm_codegen->InvokeRuntimeWithoutRecordingPcInfo(entry_point_offset, instruction_, this);
    assembler->MaybePoisonHeapReference(tmp);
    __ Str(tmp, MemOperand(dst_curr_addr, element_size, PostIndex));
    __ Cmp(src_curr_addr, src_stop_addr);
    __ B(ne, &loop, /* is_far_target= */ false);
    __ B(GetExitLabel());
  }

  const char* GetDescription() const override {
    return "ReadBarrierSystemArrayCopySlowPathARMVIXL";
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ReadBarrierSystemArrayCopySlowPathARMVIXL);
};

IntrinsicLocationsBuilderARMVIXL::IntrinsicLocationsBuilderARMVIXL(CodeGeneratorARMVIXL* codegen)
    : allocator_(codegen->GetGraph()->GetAllocator()),
      codegen_(codegen),
      assembler_(codegen->GetAssembler()),
      features_(codegen->GetInstructionSetFeatures()) {}

bool IntrinsicLocationsBuilderARMVIXL::TryDispatch(HInvoke* invoke) {
  Dispatch(invoke);
  LocationSummary* res = invoke->GetLocations();
  if (res == nullptr) {
    return false;
  }
  return res->Intrinsified();
}

static void CreateFPToIntLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresRegister());
}

static void CreateIntToFPLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresFpuRegister());
}

static void MoveFPToInt(LocationSummary* locations, bool is64bit, ArmVIXLAssembler* assembler) {
  Location input = locations->InAt(0);
  Location output = locations->Out();
  if (is64bit) {
    __ Vmov(LowRegisterFrom(output), HighRegisterFrom(output), DRegisterFrom(input));
  } else {
    __ Vmov(RegisterFrom(output), SRegisterFrom(input));
  }
}

static void MoveIntToFP(LocationSummary* locations, bool is64bit, ArmVIXLAssembler* assembler) {
  Location input = locations->InAt(0);
  Location output = locations->Out();
  if (is64bit) {
    __ Vmov(DRegisterFrom(output), LowRegisterFrom(input), HighRegisterFrom(input));
  } else {
    __ Vmov(SRegisterFrom(output), RegisterFrom(input));
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitDoubleDoubleToRawLongBits(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}
void IntrinsicLocationsBuilderARMVIXL::VisitDoubleLongBitsToDouble(HInvoke* invoke) {
  CreateIntToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitDoubleDoubleToRawLongBits(HInvoke* invoke) {
  MoveFPToInt(invoke->GetLocations(), /* is64bit= */ true, GetAssembler());
}
void IntrinsicCodeGeneratorARMVIXL::VisitDoubleLongBitsToDouble(HInvoke* invoke) {
  MoveIntToFP(invoke->GetLocations(), /* is64bit= */ true, GetAssembler());
}

void IntrinsicLocationsBuilderARMVIXL::VisitFloatFloatToRawIntBits(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}
void IntrinsicLocationsBuilderARMVIXL::VisitFloatIntBitsToFloat(HInvoke* invoke) {
  CreateIntToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitFloatFloatToRawIntBits(HInvoke* invoke) {
  MoveFPToInt(invoke->GetLocations(), /* is64bit= */ false, GetAssembler());
}
void IntrinsicCodeGeneratorARMVIXL::VisitFloatIntBitsToFloat(HInvoke* invoke) {
  MoveIntToFP(invoke->GetLocations(), /* is64bit= */ false, GetAssembler());
}

static void CreateIntToIntLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

static void CreateIntIntToIntSlowPathCallLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  // Force kOutputOverlap; see comments in IntrinsicSlowPath::EmitNativeCode.
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
}

static void CreateLongToLongLocationsWithOverlap(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
}

static void CreateFPToFPLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
}

static void GenNumberOfLeadingZeros(HInvoke* invoke,
                                    DataType::Type type,
                                    CodeGeneratorARMVIXL* codegen) {
  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  Location in = locations->InAt(0);
  vixl32::Register out = RegisterFrom(locations->Out());

  DCHECK((type == DataType::Type::kInt32) || (type == DataType::Type::kInt64));

  if (type == DataType::Type::kInt64) {
    vixl32::Register in_reg_lo = LowRegisterFrom(in);
    vixl32::Register in_reg_hi = HighRegisterFrom(in);
    vixl32::Label end;
    vixl32::Label* final_label = codegen->GetFinalLabel(invoke, &end);
    __ Clz(out, in_reg_hi);
    __ CompareAndBranchIfNonZero(in_reg_hi, final_label, /* is_far_target= */ false);
    __ Clz(out, in_reg_lo);
    __ Add(out, out, 32);
    if (end.IsReferenced()) {
      __ Bind(&end);
    }
  } else {
    __ Clz(out, RegisterFrom(in));
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitIntegerNumberOfLeadingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitIntegerNumberOfLeadingZeros(HInvoke* invoke) {
  GenNumberOfLeadingZeros(invoke, DataType::Type::kInt32, codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  CreateLongToLongLocationsWithOverlap(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  GenNumberOfLeadingZeros(invoke, DataType::Type::kInt64, codegen_);
}

static void GenNumberOfTrailingZeros(HInvoke* invoke,
                                     DataType::Type type,
                                     CodeGeneratorARMVIXL* codegen) {
  DCHECK((type == DataType::Type::kInt32) || (type == DataType::Type::kInt64));

  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  vixl32::Register out = RegisterFrom(locations->Out());

  if (type == DataType::Type::kInt64) {
    vixl32::Register in_reg_lo = LowRegisterFrom(locations->InAt(0));
    vixl32::Register in_reg_hi = HighRegisterFrom(locations->InAt(0));
    vixl32::Label end;
    vixl32::Label* final_label = codegen->GetFinalLabel(invoke, &end);
    __ Rbit(out, in_reg_lo);
    __ Clz(out, out);
    __ CompareAndBranchIfNonZero(in_reg_lo, final_label, /* is_far_target= */ false);
    __ Rbit(out, in_reg_hi);
    __ Clz(out, out);
    __ Add(out, out, 32);
    if (end.IsReferenced()) {
      __ Bind(&end);
    }
  } else {
    vixl32::Register in = RegisterFrom(locations->InAt(0));
    __ Rbit(out, in);
    __ Clz(out, out);
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  GenNumberOfTrailingZeros(invoke, DataType::Type::kInt32, codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitLongNumberOfTrailingZeros(HInvoke* invoke) {
  CreateLongToLongLocationsWithOverlap(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitLongNumberOfTrailingZeros(HInvoke* invoke) {
  GenNumberOfTrailingZeros(invoke, DataType::Type::kInt64, codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathSqrt(HInvoke* invoke) {
  CreateFPToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathSqrt(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  __ Vsqrt(OutputDRegister(invoke), InputDRegisterAt(invoke, 0));
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathRint(HInvoke* invoke) {
  if (features_.HasARMv8AInstructions()) {
    CreateFPToFPLocations(allocator_, invoke);
  }
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathRint(HInvoke* invoke) {
  DCHECK(codegen_->GetInstructionSetFeatures().HasARMv8AInstructions());
  ArmVIXLAssembler* assembler = GetAssembler();
  __ Vrintn(F64, OutputDRegister(invoke), InputDRegisterAt(invoke, 0));
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathRoundFloat(HInvoke* invoke) {
  if (features_.HasARMv8AInstructions()) {
    LocationSummary* locations =
        new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
    locations->SetInAt(0, Location::RequiresFpuRegister());
    locations->SetOut(Location::RequiresRegister());
    locations->AddTemp(Location::RequiresFpuRegister());
  }
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathRoundFloat(HInvoke* invoke) {
  DCHECK(codegen_->GetInstructionSetFeatures().HasARMv8AInstructions());

  ArmVIXLAssembler* assembler = GetAssembler();
  vixl32::SRegister in_reg = InputSRegisterAt(invoke, 0);
  vixl32::Register out_reg = OutputRegister(invoke);
  vixl32::SRegister temp1 = LowSRegisterFrom(invoke->GetLocations()->GetTemp(0));
  vixl32::SRegister temp2 = HighSRegisterFrom(invoke->GetLocations()->GetTemp(0));
  vixl32::Label done;
  vixl32::Label* final_label = codegen_->GetFinalLabel(invoke, &done);

  // Round to nearest integer, ties away from zero.
  __ Vcvta(S32, F32, temp1, in_reg);
  __ Vmov(out_reg, temp1);

  // For positive, zero or NaN inputs, rounding is done.
  __ Cmp(out_reg, 0);
  __ B(ge, final_label, /* is_far_target= */ false);

  // Handle input < 0 cases.
  // If input is negative but not a tie, previous result (round to nearest) is valid.
  // If input is a negative tie, change rounding direction to positive infinity, out_reg += 1.
  __ Vrinta(F32, temp1, in_reg);
  __ Vmov(temp2, 0.5);
  __ Vsub(F32, temp1, in_reg, temp1);
  __ Vcmp(F32, temp1, temp2);
  __ Vmrs(RegisterOrAPSR_nzcv(kPcCode), FPSCR);
  {
    // Use ExactAssemblyScope here because we are using IT.
    ExactAssemblyScope it_scope(assembler->GetVIXLAssembler(),
                                2 * kMaxInstructionSizeInBytes,
                                CodeBufferCheckScope::kMaximumSize);
    __ it(eq);
    __ add(eq, out_reg, out_reg, 1);
  }

  if (done.IsReferenced()) {
    __ Bind(&done);
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitMemoryPeekByte(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMemoryPeekByte(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  // Ignore upper 4B of long address.
  __ Ldrsb(OutputRegister(invoke), MemOperand(LowRegisterFrom(invoke->GetLocations()->InAt(0))));
}

void IntrinsicLocationsBuilderARMVIXL::VisitMemoryPeekIntNative(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMemoryPeekIntNative(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  // Ignore upper 4B of long address.
  __ Ldr(OutputRegister(invoke), MemOperand(LowRegisterFrom(invoke->GetLocations()->InAt(0))));
}

void IntrinsicLocationsBuilderARMVIXL::VisitMemoryPeekLongNative(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMemoryPeekLongNative(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  // Ignore upper 4B of long address.
  vixl32::Register addr = LowRegisterFrom(invoke->GetLocations()->InAt(0));
  // Worst case: Control register bit SCTLR.A = 0. Then unaligned accesses throw a processor
  // exception. So we can't use ldrd as addr may be unaligned.
  vixl32::Register lo = LowRegisterFrom(invoke->GetLocations()->Out());
  vixl32::Register hi = HighRegisterFrom(invoke->GetLocations()->Out());
  if (addr.Is(lo)) {
    __ Ldr(hi, MemOperand(addr, 4));
    __ Ldr(lo, MemOperand(addr));
  } else {
    __ Ldr(lo, MemOperand(addr));
    __ Ldr(hi, MemOperand(addr, 4));
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitMemoryPeekShortNative(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMemoryPeekShortNative(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  // Ignore upper 4B of long address.
  __ Ldrsh(OutputRegister(invoke), MemOperand(LowRegisterFrom(invoke->GetLocations()->InAt(0))));
}

static void CreateIntIntToVoidLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
}

void IntrinsicLocationsBuilderARMVIXL::VisitMemoryPokeByte(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMemoryPokeByte(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  __ Strb(InputRegisterAt(invoke, 1), MemOperand(LowRegisterFrom(invoke->GetLocations()->InAt(0))));
}

void IntrinsicLocationsBuilderARMVIXL::VisitMemoryPokeIntNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMemoryPokeIntNative(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  __ Str(InputRegisterAt(invoke, 1), MemOperand(LowRegisterFrom(invoke->GetLocations()->InAt(0))));
}

void IntrinsicLocationsBuilderARMVIXL::VisitMemoryPokeLongNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMemoryPokeLongNative(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  // Ignore upper 4B of long address.
  vixl32::Register addr = LowRegisterFrom(invoke->GetLocations()->InAt(0));
  // Worst case: Control register bit SCTLR.A = 0. Then unaligned accesses throw a processor
  // exception. So we can't use ldrd as addr may be unaligned.
  __ Str(LowRegisterFrom(invoke->GetLocations()->InAt(1)), MemOperand(addr));
  __ Str(HighRegisterFrom(invoke->GetLocations()->InAt(1)), MemOperand(addr, 4));
}

void IntrinsicLocationsBuilderARMVIXL::VisitMemoryPokeShortNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMemoryPokeShortNative(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  __ Strh(InputRegisterAt(invoke, 1), MemOperand(LowRegisterFrom(invoke->GetLocations()->InAt(0))));
}

void IntrinsicLocationsBuilderARMVIXL::VisitThreadCurrentThread(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorARMVIXL::VisitThreadCurrentThread(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  __ Ldr(OutputRegister(invoke),
         MemOperand(tr, Thread::PeerOffset<kArmPointerSize>().Int32Value()));
}

void IntrinsicLocationsBuilderARMVIXL::VisitStringCompareTo(HInvoke* invoke) {
  // The inputs plus one temp.
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke,
                                       invoke->InputAt(1)->CanBeNull()
                                           ? LocationSummary::kCallOnSlowPath
                                           : LocationSummary::kNoCall,
                                       kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  // Need temporary registers for String compression's feature.
  if (mirror::kUseStringCompression) {
    locations->AddTemp(Location::RequiresRegister());
  }
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
}

// Forward declaration.
//
// ART build system imposes a size limit (deviceFrameSizeLimit) on the stack frames generated
// by the compiler for every C++ function, and if this function gets inlined in
// IntrinsicCodeGeneratorARMVIXL::VisitStringCompareTo, the limit will be exceeded, resulting in a
// build failure. That is the reason why NO_INLINE attribute is used.
static void NO_INLINE GenerateStringCompareToLoop(ArmVIXLAssembler* assembler,
                                                  HInvoke* invoke,
                                                  vixl32::Label* end,
                                                  vixl32::Label* different_compression);

void IntrinsicCodeGeneratorARMVIXL::VisitStringCompareTo(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  const vixl32::Register str = InputRegisterAt(invoke, 0);
  const vixl32::Register arg = InputRegisterAt(invoke, 1);
  const vixl32::Register out = OutputRegister(invoke);

  const vixl32::Register temp0 = RegisterFrom(locations->GetTemp(0));
  const vixl32::Register temp1 = RegisterFrom(locations->GetTemp(1));
  const vixl32::Register temp2 = RegisterFrom(locations->GetTemp(2));
  vixl32::Register temp3;
  if (mirror::kUseStringCompression) {
    temp3 = RegisterFrom(locations->GetTemp(3));
  }

  vixl32::Label end;
  vixl32::Label different_compression;

  // Get offsets of count and value fields within a string object.
  const int32_t count_offset = mirror::String::CountOffset().Int32Value();

  // Note that the null check must have been done earlier.
  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  // Take slow path and throw if input can be and is null.
  SlowPathCodeARMVIXL* slow_path = nullptr;
  const bool can_slow_path = invoke->InputAt(1)->CanBeNull();
  if (can_slow_path) {
    slow_path = new (codegen_->GetScopedAllocator()) IntrinsicSlowPathARMVIXL(invoke);
    codegen_->AddSlowPath(slow_path);
    __ CompareAndBranchIfZero(arg, slow_path->GetEntryLabel());
  }

  // Reference equality check, return 0 if same reference.
  __ Subs(out, str, arg);
  __ B(eq, &end);

  if (mirror::kUseStringCompression) {
    // Load `count` fields of this and argument strings.
    __ Ldr(temp3, MemOperand(str, count_offset));
    __ Ldr(temp2, MemOperand(arg, count_offset));
    // Extract lengths from the `count` fields.
    __ Lsr(temp0, temp3, 1u);
    __ Lsr(temp1, temp2, 1u);
  } else {
    // Load lengths of this and argument strings.
    __ Ldr(temp0, MemOperand(str, count_offset));
    __ Ldr(temp1, MemOperand(arg, count_offset));
  }
  // out = length diff.
  __ Subs(out, temp0, temp1);
  // temp0 = min(len(str), len(arg)).

  {
    ExactAssemblyScope aas(assembler->GetVIXLAssembler(),
                           2 * kMaxInstructionSizeInBytes,
                           CodeBufferCheckScope::kMaximumSize);

    __ it(gt);
    __ mov(gt, temp0, temp1);
  }

  // Shorter string is empty?
  // Note that mirror::kUseStringCompression==true introduces lots of instructions,
  // which makes &end label far away from this branch and makes it not 'CBZ-encodable'.
  __ CompareAndBranchIfZero(temp0, &end, mirror::kUseStringCompression);

  if (mirror::kUseStringCompression) {
    // Check if both strings using same compression style to use this comparison loop.
    __ Eors(temp2, temp2, temp3);
    __ Lsrs(temp2, temp2, 1u);
    __ B(cs, &different_compression);
    // For string compression, calculate the number of bytes to compare (not chars).
    // This could in theory exceed INT32_MAX, so treat temp0 as unsigned.
    __ Lsls(temp3, temp3, 31u);  // Extract purely the compression flag.

    ExactAssemblyScope aas(assembler->GetVIXLAssembler(),
                           2 * kMaxInstructionSizeInBytes,
                           CodeBufferCheckScope::kMaximumSize);

    __ it(ne);
    __ add(ne, temp0, temp0, temp0);
  }


  GenerateStringCompareToLoop(assembler, invoke, &end, &different_compression);

  __ Bind(&end);

  if (can_slow_path) {
    __ Bind(slow_path->GetExitLabel());
  }
}

static void GenerateStringCompareToLoop(ArmVIXLAssembler* assembler,
                                        HInvoke* invoke,
                                        vixl32::Label* end,
                                        vixl32::Label* different_compression) {
  LocationSummary* locations = invoke->GetLocations();

  const vixl32::Register str = InputRegisterAt(invoke, 0);
  const vixl32::Register arg = InputRegisterAt(invoke, 1);
  const vixl32::Register out = OutputRegister(invoke);

  const vixl32::Register temp0 = RegisterFrom(locations->GetTemp(0));
  const vixl32::Register temp1 = RegisterFrom(locations->GetTemp(1));
  const vixl32::Register temp2 = RegisterFrom(locations->GetTemp(2));
  vixl32::Register temp3;
  if (mirror::kUseStringCompression) {
    temp3 = RegisterFrom(locations->GetTemp(3));
  }

  vixl32::Label loop;
  vixl32::Label find_char_diff;

  const int32_t value_offset = mirror::String::ValueOffset().Int32Value();
  // Store offset of string value in preparation for comparison loop.
  __ Mov(temp1, value_offset);

  // Assertions that must hold in order to compare multiple characters at a time.
  CHECK_ALIGNED(value_offset, 8);
  static_assert(IsAligned<8>(kObjectAlignment),
                "String data must be 8-byte aligned for unrolled CompareTo loop.");

  const unsigned char_size = DataType::Size(DataType::Type::kUint16);
  DCHECK_EQ(char_size, 2u);

  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());

  vixl32::Label find_char_diff_2nd_cmp;
  // Unrolled loop comparing 4x16-bit chars per iteration (ok because of string data alignment).
  __ Bind(&loop);
  vixl32::Register temp_reg = temps.Acquire();
  __ Ldr(temp_reg, MemOperand(str, temp1));
  __ Ldr(temp2, MemOperand(arg, temp1));
  __ Cmp(temp_reg, temp2);
  __ B(ne, &find_char_diff, /* is_far_target= */ false);
  __ Add(temp1, temp1, char_size * 2);

  __ Ldr(temp_reg, MemOperand(str, temp1));
  __ Ldr(temp2, MemOperand(arg, temp1));
  __ Cmp(temp_reg, temp2);
  __ B(ne, &find_char_diff_2nd_cmp, /* is_far_target= */ false);
  __ Add(temp1, temp1, char_size * 2);
  // With string compression, we have compared 8 bytes, otherwise 4 chars.
  __ Subs(temp0, temp0, (mirror::kUseStringCompression ? 8 : 4));
  __ B(hi, &loop, /* is_far_target= */ false);
  __ B(end);

  __ Bind(&find_char_diff_2nd_cmp);
  if (mirror::kUseStringCompression) {
    __ Subs(temp0, temp0, 4);  // 4 bytes previously compared.
    __ B(ls, end, /* is_far_target= */ false);  // Was the second comparison fully beyond the end?
  } else {
    // Without string compression, we can start treating temp0 as signed
    // and rely on the signed comparison below.
    __ Sub(temp0, temp0, 2);
  }

  // Find the single character difference.
  __ Bind(&find_char_diff);
  // Get the bit position of the first character that differs.
  __ Eor(temp1, temp2, temp_reg);
  __ Rbit(temp1, temp1);
  __ Clz(temp1, temp1);

  // temp0 = number of characters remaining to compare.
  // (Without string compression, it could be < 1 if a difference is found by the second CMP
  // in the comparison loop, and after the end of the shorter string data).

  // Without string compression (temp1 >> 4) = character where difference occurs between the last
  // two words compared, in the interval [0,1].
  // (0 for low half-word different, 1 for high half-word different).
  // With string compression, (temp1 << 3) = byte where the difference occurs,
  // in the interval [0,3].

  // If temp0 <= (temp1 >> (kUseStringCompression ? 3 : 4)), the difference occurs outside
  // the remaining string data, so just return length diff (out).
  // The comparison is unsigned for string compression, otherwise signed.
  __ Cmp(temp0, Operand(temp1, vixl32::LSR, (mirror::kUseStringCompression ? 3 : 4)));
  __ B((mirror::kUseStringCompression ? ls : le), end, /* is_far_target= */ false);

  // Extract the characters and calculate the difference.
  if (mirror::kUseStringCompression) {
    // For compressed strings we need to clear 0x7 from temp1, for uncompressed we need to clear
    // 0xf. We also need to prepare the character extraction mask `uncompressed ? 0xffffu : 0xffu`.
    // The compression flag is now in the highest bit of temp3, so let's play some tricks.
    __ Orr(temp3, temp3, 0xffu << 23);                  // uncompressed ? 0xff800000u : 0x7ff80000u
    __ Bic(temp1, temp1, Operand(temp3, vixl32::LSR, 31 - 3));  // &= ~(uncompressed ? 0xfu : 0x7u)
    __ Asr(temp3, temp3, 7u);                           // uncompressed ? 0xffff0000u : 0xff0000u.
    __ Lsr(temp2, temp2, temp1);                        // Extract second character.
    __ Lsr(temp3, temp3, 16u);                          // uncompressed ? 0xffffu : 0xffu
    __ Lsr(out, temp_reg, temp1);                       // Extract first character.
    __ And(temp2, temp2, temp3);
    __ And(out, out, temp3);
  } else {
    __ Bic(temp1, temp1, 0xf);
    __ Lsr(temp2, temp2, temp1);
    __ Lsr(out, temp_reg, temp1);
    __ Movt(temp2, 0);
    __ Movt(out, 0);
  }

  __ Sub(out, out, temp2);
  temps.Release(temp_reg);

  if (mirror::kUseStringCompression) {
    __ B(end);
    __ Bind(different_compression);

    // Comparison for different compression style.
    const size_t c_char_size = DataType::Size(DataType::Type::kInt8);
    DCHECK_EQ(c_char_size, 1u);

    // We want to free up the temp3, currently holding `str.count`, for comparison.
    // So, we move it to the bottom bit of the iteration count `temp0` which we tnen
    // need to treat as unsigned. Start by freeing the bit with an ADD and continue
    // further down by a LSRS+SBC which will flip the meaning of the flag but allow
    // `subs temp0, #2; bhi different_compression_loop` to serve as the loop condition.
    __ Add(temp0, temp0, temp0);              // Unlike LSL, this ADD is always 16-bit.
    // `temp1` will hold the compressed data pointer, `temp2` the uncompressed data pointer.
    __ Mov(temp1, str);
    __ Mov(temp2, arg);
    __ Lsrs(temp3, temp3, 1u);                // Continue the move of the compression flag.
    {
      ExactAssemblyScope aas(assembler->GetVIXLAssembler(),
                             3 * kMaxInstructionSizeInBytes,
                             CodeBufferCheckScope::kMaximumSize);
      __ itt(cs);                             // Interleave with selection of temp1 and temp2.
      __ mov(cs, temp1, arg);                 // Preserves flags.
      __ mov(cs, temp2, str);                 // Preserves flags.
    }
    __ Sbc(temp0, temp0, 0);                  // Complete the move of the compression flag.

    // Adjust temp1 and temp2 from string pointers to data pointers.
    __ Add(temp1, temp1, value_offset);
    __ Add(temp2, temp2, value_offset);

    vixl32::Label different_compression_loop;
    vixl32::Label different_compression_diff;

    // Main loop for different compression.
    temp_reg = temps.Acquire();
    __ Bind(&different_compression_loop);
    __ Ldrb(temp_reg, MemOperand(temp1, c_char_size, PostIndex));
    __ Ldrh(temp3, MemOperand(temp2, char_size, PostIndex));
    __ Cmp(temp_reg, temp3);
    __ B(ne, &different_compression_diff, /* is_far_target= */ false);
    __ Subs(temp0, temp0, 2);
    __ B(hi, &different_compression_loop, /* is_far_target= */ false);
    __ B(end);

    // Calculate the difference.
    __ Bind(&different_compression_diff);
    __ Sub(out, temp_reg, temp3);
    temps.Release(temp_reg);
    // Flip the difference if the `arg` is compressed.
    // `temp0` contains inverted `str` compression flag, i.e the same as `arg` compression flag.
    __ Lsrs(temp0, temp0, 1u);
    static_assert(static_cast<uint32_t>(mirror::StringCompressionFlag::kCompressed) == 0u,
                  "Expecting 0=compressed, 1=uncompressed");

    ExactAssemblyScope aas(assembler->GetVIXLAssembler(),
                           2 * kMaxInstructionSizeInBytes,
                           CodeBufferCheckScope::kMaximumSize);
    __ it(cc);
    __ rsb(cc, out, out, 0);
  }
}

// The cut off for unrolling the loop in String.equals() intrinsic for const strings.
// The normal loop plus the pre-header is 9 instructions (18-26 bytes) without string compression
// and 12 instructions (24-32 bytes) with string compression. We can compare up to 4 bytes in 4
// instructions (LDR+LDR+CMP+BNE) and up to 8 bytes in 6 instructions (LDRD+LDRD+CMP+BNE+CMP+BNE).
// Allow up to 12 instructions (32 bytes) for the unrolled loop.
constexpr size_t kShortConstStringEqualsCutoffInBytes = 16;

static const char* GetConstString(HInstruction* candidate, uint32_t* utf16_length) {
  if (candidate->IsLoadString()) {
    HLoadString* load_string = candidate->AsLoadString();
    const DexFile& dex_file = load_string->GetDexFile();
    return dex_file.StringDataAndUtf16LengthByIdx(load_string->GetStringIndex(), utf16_length);
  }
  return nullptr;
}

void IntrinsicLocationsBuilderARMVIXL::VisitStringEquals(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  InvokeRuntimeCallingConventionARMVIXL calling_convention;
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());

  // Temporary registers to store lengths of strings and for calculations.
  // Using instruction cbz requires a low register, so explicitly set a temp to be R0.
  locations->AddTemp(LocationFrom(r0));

  // For the generic implementation and for long const strings we need an extra temporary.
  // We do not need it for short const strings, up to 4 bytes, see code generation below.
  uint32_t const_string_length = 0u;
  const char* const_string = GetConstString(invoke->InputAt(0), &const_string_length);
  if (const_string == nullptr) {
    const_string = GetConstString(invoke->InputAt(1), &const_string_length);
  }
  bool is_compressed =
      mirror::kUseStringCompression &&
      const_string != nullptr &&
      mirror::String::DexFileStringAllASCII(const_string, const_string_length);
  if (const_string == nullptr || const_string_length > (is_compressed ? 4u : 2u)) {
    locations->AddTemp(Location::RequiresRegister());
  }

  // TODO: If the String.equals() is used only for an immediately following HIf, we can
  // mark it as emitted-at-use-site and emit branches directly to the appropriate blocks.
  // Then we shall need an extra temporary register instead of the output register.
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorARMVIXL::VisitStringEquals(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  vixl32::Register str = InputRegisterAt(invoke, 0);
  vixl32::Register arg = InputRegisterAt(invoke, 1);
  vixl32::Register out = OutputRegister(invoke);

  vixl32::Register temp = RegisterFrom(locations->GetTemp(0));

  vixl32::Label loop;
  vixl32::Label end;
  vixl32::Label return_true;
  vixl32::Label return_false;
  vixl32::Label* final_label = codegen_->GetFinalLabel(invoke, &end);

  // Get offsets of count, value, and class fields within a string object.
  const uint32_t count_offset = mirror::String::CountOffset().Uint32Value();
  const uint32_t value_offset = mirror::String::ValueOffset().Uint32Value();
  const uint32_t class_offset = mirror::Object::ClassOffset().Uint32Value();

  // Note that the null check must have been done earlier.
  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  StringEqualsOptimizations optimizations(invoke);
  if (!optimizations.GetArgumentNotNull()) {
    // Check if input is null, return false if it is.
    __ CompareAndBranchIfZero(arg, &return_false, /* is_far_target= */ false);
  }

  // Reference equality check, return true if same reference.
  __ Cmp(str, arg);
  __ B(eq, &return_true, /* is_far_target= */ false);

  if (!optimizations.GetArgumentIsString()) {
    // Instanceof check for the argument by comparing class fields.
    // All string objects must have the same type since String cannot be subclassed.
    // Receiver must be a string object, so its class field is equal to all strings' class fields.
    // If the argument is a string object, its class field must be equal to receiver's class field.
    //
    // As the String class is expected to be non-movable, we can read the class
    // field from String.equals' arguments without read barriers.
    AssertNonMovableStringClass();
    // /* HeapReference<Class> */ temp = str->klass_
    __ Ldr(temp, MemOperand(str, class_offset));
    // /* HeapReference<Class> */ out = arg->klass_
    __ Ldr(out, MemOperand(arg, class_offset));
    // Also, because we use the previously loaded class references only in the
    // following comparison, we don't need to unpoison them.
    __ Cmp(temp, out);
    __ B(ne, &return_false, /* is_far_target= */ false);
  }

  // Check if one of the inputs is a const string. Do not special-case both strings
  // being const, such cases should be handled by constant folding if needed.
  uint32_t const_string_length = 0u;
  const char* const_string = GetConstString(invoke->InputAt(0), &const_string_length);
  if (const_string == nullptr) {
    const_string = GetConstString(invoke->InputAt(1), &const_string_length);
    if (const_string != nullptr) {
      std::swap(str, arg);  // Make sure the const string is in `str`.
    }
  }
  bool is_compressed =
      mirror::kUseStringCompression &&
      const_string != nullptr &&
      mirror::String::DexFileStringAllASCII(const_string, const_string_length);

  if (const_string != nullptr) {
    // Load `count` field of the argument string and check if it matches the const string.
    // Also compares the compression style, if differs return false.
    __ Ldr(temp, MemOperand(arg, count_offset));
    __ Cmp(temp, Operand(mirror::String::GetFlaggedCount(const_string_length, is_compressed)));
    __ B(ne, &return_false, /* is_far_target= */ false);
  } else {
    // Load `count` fields of this and argument strings.
    __ Ldr(temp, MemOperand(str, count_offset));
    __ Ldr(out, MemOperand(arg, count_offset));
    // Check if `count` fields are equal, return false if they're not.
    // Also compares the compression style, if differs return false.
    __ Cmp(temp, out);
    __ B(ne, &return_false, /* is_far_target= */ false);
  }

  // Assertions that must hold in order to compare strings 4 bytes at a time.
  // Ok to do this because strings are zero-padded to kObjectAlignment.
  DCHECK_ALIGNED(value_offset, 4);
  static_assert(IsAligned<4>(kObjectAlignment), "String data must be aligned for fast compare.");

  if (const_string != nullptr &&
      const_string_length <= (is_compressed ? kShortConstStringEqualsCutoffInBytes
                                            : kShortConstStringEqualsCutoffInBytes / 2u)) {
    // Load and compare the contents. Though we know the contents of the short const string
    // at compile time, materializing constants may be more code than loading from memory.
    int32_t offset = value_offset;
    size_t remaining_bytes =
        RoundUp(is_compressed ? const_string_length : const_string_length * 2u, 4u);
    while (remaining_bytes > sizeof(uint32_t)) {
      vixl32::Register temp1 = RegisterFrom(locations->GetTemp(1));
      UseScratchRegisterScope scratch_scope(assembler->GetVIXLAssembler());
      vixl32::Register temp2 = scratch_scope.Acquire();
      __ Ldrd(temp, temp1, MemOperand(str, offset));
      __ Ldrd(temp2, out, MemOperand(arg, offset));
      __ Cmp(temp, temp2);
      __ B(ne, &return_false, /* is_far_target= */ false);
      __ Cmp(temp1, out);
      __ B(ne, &return_false, /* is_far_target= */ false);
      offset += 2u * sizeof(uint32_t);
      remaining_bytes -= 2u * sizeof(uint32_t);
    }
    if (remaining_bytes != 0u) {
      __ Ldr(temp, MemOperand(str, offset));
      __ Ldr(out, MemOperand(arg, offset));
      __ Cmp(temp, out);
      __ B(ne, &return_false, /* is_far_target= */ false);
    }
  } else {
    // Return true if both strings are empty. Even with string compression `count == 0` means empty.
    static_assert(static_cast<uint32_t>(mirror::StringCompressionFlag::kCompressed) == 0u,
                  "Expecting 0=compressed, 1=uncompressed");
    __ CompareAndBranchIfZero(temp, &return_true, /* is_far_target= */ false);

    if (mirror::kUseStringCompression) {
      // For string compression, calculate the number of bytes to compare (not chars).
      // This could in theory exceed INT32_MAX, so treat temp as unsigned.
      __ Lsrs(temp, temp, 1u);                        // Extract length and check compression flag.
      ExactAssemblyScope aas(assembler->GetVIXLAssembler(),
                             2 * kMaxInstructionSizeInBytes,
                             CodeBufferCheckScope::kMaximumSize);
      __ it(cs);                                      // If uncompressed,
      __ add(cs, temp, temp, temp);                   //   double the byte count.
    }

    vixl32::Register temp1 = RegisterFrom(locations->GetTemp(1));
    UseScratchRegisterScope scratch_scope(assembler->GetVIXLAssembler());
    vixl32::Register temp2 = scratch_scope.Acquire();

    // Store offset of string value in preparation for comparison loop.
    __ Mov(temp1, value_offset);

    // Loop to compare strings 4 bytes at a time starting at the front of the string.
    __ Bind(&loop);
    __ Ldr(out, MemOperand(str, temp1));
    __ Ldr(temp2, MemOperand(arg, temp1));
    __ Add(temp1, temp1, Operand::From(sizeof(uint32_t)));
    __ Cmp(out, temp2);
    __ B(ne, &return_false, /* is_far_target= */ false);
    // With string compression, we have compared 4 bytes, otherwise 2 chars.
    __ Subs(temp, temp, mirror::kUseStringCompression ? 4 : 2);
    __ B(hi, &loop, /* is_far_target= */ false);
  }

  // Return true and exit the function.
  // If loop does not result in returning false, we return true.
  __ Bind(&return_true);
  __ Mov(out, 1);
  __ B(final_label);

  // Return false and exit the function.
  __ Bind(&return_false);
  __ Mov(out, 0);

  if (end.IsReferenced()) {
    __ Bind(&end);
  }
}

static void GenerateVisitStringIndexOf(HInvoke* invoke,
                                       ArmVIXLAssembler* assembler,
                                       CodeGeneratorARMVIXL* codegen,
                                       bool start_at_zero) {
  LocationSummary* locations = invoke->GetLocations();

  // Note that the null check must have been done earlier.
  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  // Check for code points > 0xFFFF. Either a slow-path check when we don't know statically,
  // or directly dispatch for a large constant, or omit slow-path for a small constant or a char.
  SlowPathCodeARMVIXL* slow_path = nullptr;
  HInstruction* code_point = invoke->InputAt(1);
  if (code_point->IsIntConstant()) {
    if (static_cast<uint32_t>(Int32ConstantFrom(code_point)) >
        std::numeric_limits<uint16_t>::max()) {
      // Always needs the slow-path. We could directly dispatch to it, but this case should be
      // rare, so for simplicity just put the full slow-path down and branch unconditionally.
      slow_path = new (codegen->GetScopedAllocator()) IntrinsicSlowPathARMVIXL(invoke);
      codegen->AddSlowPath(slow_path);
      __ B(slow_path->GetEntryLabel());
      __ Bind(slow_path->GetExitLabel());
      return;
    }
  } else if (code_point->GetType() != DataType::Type::kUint16) {
    vixl32::Register char_reg = InputRegisterAt(invoke, 1);
    // 0xffff is not modified immediate but 0x10000 is, so use `>= 0x10000` instead of `> 0xffff`.
    __ Cmp(char_reg, static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()) + 1);
    slow_path = new (codegen->GetScopedAllocator()) IntrinsicSlowPathARMVIXL(invoke);
    codegen->AddSlowPath(slow_path);
    __ B(hs, slow_path->GetEntryLabel());
  }

  if (start_at_zero) {
    vixl32::Register tmp_reg = RegisterFrom(locations->GetTemp(0));
    DCHECK(tmp_reg.Is(r2));
    // Start-index = 0.
    __ Mov(tmp_reg, 0);
  }

  codegen->InvokeRuntime(kQuickIndexOf, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickIndexOf, int32_t, void*, uint32_t, uint32_t>();

  if (slow_path != nullptr) {
    __ Bind(slow_path->GetExitLabel());
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitStringIndexOf(HInvoke* invoke) {
  LocationSummary* locations = new (allocator_) LocationSummary(
      invoke, LocationSummary::kCallOnMainAndSlowPath, kIntrinsified);
  // We have a hand-crafted assembly stub that follows the runtime calling convention. So it's
  // best to align the inputs accordingly.
  InvokeRuntimeCallingConventionARMVIXL calling_convention;
  locations->SetInAt(0, LocationFrom(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, LocationFrom(calling_convention.GetRegisterAt(1)));
  locations->SetOut(LocationFrom(r0));

  // Need to send start-index=0.
  locations->AddTemp(LocationFrom(calling_convention.GetRegisterAt(2)));
}

void IntrinsicCodeGeneratorARMVIXL::VisitStringIndexOf(HInvoke* invoke) {
  GenerateVisitStringIndexOf(invoke, GetAssembler(), codegen_, /* start_at_zero= */ true);
}

void IntrinsicLocationsBuilderARMVIXL::VisitStringIndexOfAfter(HInvoke* invoke) {
  LocationSummary* locations = new (allocator_) LocationSummary(
      invoke, LocationSummary::kCallOnMainAndSlowPath, kIntrinsified);
  // We have a hand-crafted assembly stub that follows the runtime calling convention. So it's
  // best to align the inputs accordingly.
  InvokeRuntimeCallingConventionARMVIXL calling_convention;
  locations->SetInAt(0, LocationFrom(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, LocationFrom(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, LocationFrom(calling_convention.GetRegisterAt(2)));
  locations->SetOut(LocationFrom(r0));
}

void IntrinsicCodeGeneratorARMVIXL::VisitStringIndexOfAfter(HInvoke* invoke) {
  GenerateVisitStringIndexOf(invoke, GetAssembler(), codegen_, /* start_at_zero= */ false);
}

void IntrinsicLocationsBuilderARMVIXL::VisitStringNewStringFromBytes(HInvoke* invoke) {
  LocationSummary* locations = new (allocator_) LocationSummary(
      invoke, LocationSummary::kCallOnMainAndSlowPath, kIntrinsified);
  InvokeRuntimeCallingConventionARMVIXL calling_convention;
  locations->SetInAt(0, LocationFrom(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, LocationFrom(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, LocationFrom(calling_convention.GetRegisterAt(2)));
  locations->SetInAt(3, LocationFrom(calling_convention.GetRegisterAt(3)));
  locations->SetOut(LocationFrom(r0));
}

void IntrinsicCodeGeneratorARMVIXL::VisitStringNewStringFromBytes(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  vixl32::Register byte_array = InputRegisterAt(invoke, 0);
  __ Cmp(byte_array, 0);
  SlowPathCodeARMVIXL* slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathARMVIXL(invoke);
  codegen_->AddSlowPath(slow_path);
  __ B(eq, slow_path->GetEntryLabel());

  codegen_->InvokeRuntime(kQuickAllocStringFromBytes, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickAllocStringFromBytes, void*, void*, int32_t, int32_t, int32_t>();
  __ Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderARMVIXL::VisitStringNewStringFromChars(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kCallOnMainOnly, kIntrinsified);
  InvokeRuntimeCallingConventionARMVIXL calling_convention;
  locations->SetInAt(0, LocationFrom(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, LocationFrom(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, LocationFrom(calling_convention.GetRegisterAt(2)));
  locations->SetOut(LocationFrom(r0));
}

void IntrinsicCodeGeneratorARMVIXL::VisitStringNewStringFromChars(HInvoke* invoke) {
  // No need to emit code checking whether `locations->InAt(2)` is a null
  // pointer, as callers of the native method
  //
  //   java.lang.StringFactory.newStringFromChars(int offset, int charCount, char[] data)
  //
  // all include a null check on `data` before calling that method.
  codegen_->InvokeRuntime(kQuickAllocStringFromChars, invoke, invoke->GetDexPc());
  CheckEntrypointTypes<kQuickAllocStringFromChars, void*, int32_t, int32_t, void*>();
}

void IntrinsicLocationsBuilderARMVIXL::VisitStringNewStringFromString(HInvoke* invoke) {
  LocationSummary* locations = new (allocator_) LocationSummary(
      invoke, LocationSummary::kCallOnMainAndSlowPath, kIntrinsified);
  InvokeRuntimeCallingConventionARMVIXL calling_convention;
  locations->SetInAt(0, LocationFrom(calling_convention.GetRegisterAt(0)));
  locations->SetOut(LocationFrom(r0));
}

void IntrinsicCodeGeneratorARMVIXL::VisitStringNewStringFromString(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  vixl32::Register string_to_copy = InputRegisterAt(invoke, 0);
  __ Cmp(string_to_copy, 0);
  SlowPathCodeARMVIXL* slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathARMVIXL(invoke);
  codegen_->AddSlowPath(slow_path);
  __ B(eq, slow_path->GetEntryLabel());

  codegen_->InvokeRuntime(kQuickAllocStringFromString, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickAllocStringFromString, void*, void*>();

  __ Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderARMVIXL::VisitSystemArrayCopy(HInvoke* invoke) {
  // The only read barrier implementation supporting the
  // SystemArrayCopy intrinsic is the Baker-style read barriers.
  if (kEmitCompilerReadBarrier && !kUseBakerReadBarrier) {
    return;
  }

  CodeGenerator::CreateSystemArrayCopyLocationSummary(invoke);
  LocationSummary* locations = invoke->GetLocations();
  if (locations == nullptr) {
    return;
  }

  HIntConstant* src_pos = invoke->InputAt(1)->AsIntConstant();
  HIntConstant* dest_pos = invoke->InputAt(3)->AsIntConstant();
  HIntConstant* length = invoke->InputAt(4)->AsIntConstant();

  if (src_pos != nullptr && !assembler_->ShifterOperandCanAlwaysHold(src_pos->GetValue())) {
    locations->SetInAt(1, Location::RequiresRegister());
  }
  if (dest_pos != nullptr && !assembler_->ShifterOperandCanAlwaysHold(dest_pos->GetValue())) {
    locations->SetInAt(3, Location::RequiresRegister());
  }
  if (length != nullptr && !assembler_->ShifterOperandCanAlwaysHold(length->GetValue())) {
    locations->SetInAt(4, Location::RequiresRegister());
  }
  if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
    // Temporary register IP cannot be used in
    // ReadBarrierSystemArrayCopySlowPathARM (because that register
    // is clobbered by ReadBarrierMarkRegX entry points). Get an extra
    // temporary register from the register allocator.
    locations->AddTemp(Location::RequiresRegister());
  }
}

static void CheckPosition(ArmVIXLAssembler* assembler,
                          Location pos,
                          vixl32::Register input,
                          Location length,
                          SlowPathCodeARMVIXL* slow_path,
                          vixl32::Register temp,
                          bool length_is_input_length = false) {
  // Where is the length in the Array?
  const uint32_t length_offset = mirror::Array::LengthOffset().Uint32Value();

  if (pos.IsConstant()) {
    int32_t pos_const = Int32ConstantFrom(pos);
    if (pos_const == 0) {
      if (!length_is_input_length) {
        // Check that length(input) >= length.
        __ Ldr(temp, MemOperand(input, length_offset));
        if (length.IsConstant()) {
          __ Cmp(temp, Int32ConstantFrom(length));
        } else {
          __ Cmp(temp, RegisterFrom(length));
        }
        __ B(lt, slow_path->GetEntryLabel());
      }
    } else {
      // Check that length(input) >= pos.
      __ Ldr(temp, MemOperand(input, length_offset));
      __ Subs(temp, temp, pos_const);
      __ B(lt, slow_path->GetEntryLabel());

      // Check that (length(input) - pos) >= length.
      if (length.IsConstant()) {
        __ Cmp(temp, Int32ConstantFrom(length));
      } else {
        __ Cmp(temp, RegisterFrom(length));
      }
      __ B(lt, slow_path->GetEntryLabel());
    }
  } else if (length_is_input_length) {
    // The only way the copy can succeed is if pos is zero.
    vixl32::Register pos_reg = RegisterFrom(pos);
    __ CompareAndBranchIfNonZero(pos_reg, slow_path->GetEntryLabel());
  } else {
    // Check that pos >= 0.
    vixl32::Register pos_reg = RegisterFrom(pos);
    __ Cmp(pos_reg, 0);
    __ B(lt, slow_path->GetEntryLabel());

    // Check that pos <= length(input).
    __ Ldr(temp, MemOperand(input, length_offset));
    __ Subs(temp, temp, pos_reg);
    __ B(lt, slow_path->GetEntryLabel());

    // Check that (length(input) - pos) >= length.
    if (length.IsConstant()) {
      __ Cmp(temp, Int32ConstantFrom(length));
    } else {
      __ Cmp(temp, RegisterFrom(length));
    }
    __ B(lt, slow_path->GetEntryLabel());
  }
}

void IntrinsicCodeGeneratorARMVIXL::VisitSystemArrayCopy(HInvoke* invoke) {
  // The only read barrier implementation supporting the
  // SystemArrayCopy intrinsic is the Baker-style read barriers.
  DCHECK(!kEmitCompilerReadBarrier || kUseBakerReadBarrier);

  ArmVIXLAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  uint32_t class_offset = mirror::Object::ClassOffset().Int32Value();
  uint32_t super_offset = mirror::Class::SuperClassOffset().Int32Value();
  uint32_t component_offset = mirror::Class::ComponentTypeOffset().Int32Value();
  uint32_t primitive_offset = mirror::Class::PrimitiveTypeOffset().Int32Value();
  uint32_t monitor_offset = mirror::Object::MonitorOffset().Int32Value();

  vixl32::Register src = InputRegisterAt(invoke, 0);
  Location src_pos = locations->InAt(1);
  vixl32::Register dest = InputRegisterAt(invoke, 2);
  Location dest_pos = locations->InAt(3);
  Location length = locations->InAt(4);
  Location temp1_loc = locations->GetTemp(0);
  vixl32::Register temp1 = RegisterFrom(temp1_loc);
  Location temp2_loc = locations->GetTemp(1);
  vixl32::Register temp2 = RegisterFrom(temp2_loc);
  Location temp3_loc = locations->GetTemp(2);
  vixl32::Register temp3 = RegisterFrom(temp3_loc);

  SlowPathCodeARMVIXL* intrinsic_slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathARMVIXL(invoke);
  codegen_->AddSlowPath(intrinsic_slow_path);

  vixl32::Label conditions_on_positions_validated;
  SystemArrayCopyOptimizations optimizations(invoke);

  // If source and destination are the same, we go to slow path if we need to do
  // forward copying.
  if (src_pos.IsConstant()) {
    int32_t src_pos_constant = Int32ConstantFrom(src_pos);
    if (dest_pos.IsConstant()) {
      int32_t dest_pos_constant = Int32ConstantFrom(dest_pos);
      if (optimizations.GetDestinationIsSource()) {
        // Checked when building locations.
        DCHECK_GE(src_pos_constant, dest_pos_constant);
      } else if (src_pos_constant < dest_pos_constant) {
        __ Cmp(src, dest);
        __ B(eq, intrinsic_slow_path->GetEntryLabel());
      }

      // Checked when building locations.
      DCHECK(!optimizations.GetDestinationIsSource()
             || (src_pos_constant >= Int32ConstantFrom(dest_pos)));
    } else {
      if (!optimizations.GetDestinationIsSource()) {
        __ Cmp(src, dest);
        __ B(ne, &conditions_on_positions_validated, /* is_far_target= */ false);
      }
      __ Cmp(RegisterFrom(dest_pos), src_pos_constant);
      __ B(gt, intrinsic_slow_path->GetEntryLabel());
    }
  } else {
    if (!optimizations.GetDestinationIsSource()) {
      __ Cmp(src, dest);
      __ B(ne, &conditions_on_positions_validated, /* is_far_target= */ false);
    }
    if (dest_pos.IsConstant()) {
      int32_t dest_pos_constant = Int32ConstantFrom(dest_pos);
      __ Cmp(RegisterFrom(src_pos), dest_pos_constant);
    } else {
      __ Cmp(RegisterFrom(src_pos), RegisterFrom(dest_pos));
    }
    __ B(lt, intrinsic_slow_path->GetEntryLabel());
  }

  __ Bind(&conditions_on_positions_validated);

  if (!optimizations.GetSourceIsNotNull()) {
    // Bail out if the source is null.
    __ CompareAndBranchIfZero(src, intrinsic_slow_path->GetEntryLabel());
  }

  if (!optimizations.GetDestinationIsNotNull() && !optimizations.GetDestinationIsSource()) {
    // Bail out if the destination is null.
    __ CompareAndBranchIfZero(dest, intrinsic_slow_path->GetEntryLabel());
  }

  // If the length is negative, bail out.
  // We have already checked in the LocationsBuilder for the constant case.
  if (!length.IsConstant() &&
      !optimizations.GetCountIsSourceLength() &&
      !optimizations.GetCountIsDestinationLength()) {
    __ Cmp(RegisterFrom(length), 0);
    __ B(lt, intrinsic_slow_path->GetEntryLabel());
  }

  // Validity checks: source.
  CheckPosition(assembler,
                src_pos,
                src,
                length,
                intrinsic_slow_path,
                temp1,
                optimizations.GetCountIsSourceLength());

  // Validity checks: dest.
  CheckPosition(assembler,
                dest_pos,
                dest,
                length,
                intrinsic_slow_path,
                temp1,
                optimizations.GetCountIsDestinationLength());

  if (!optimizations.GetDoesNotNeedTypeCheck()) {
    // Check whether all elements of the source array are assignable to the component
    // type of the destination array. We do two checks: the classes are the same,
    // or the destination is Object[]. If none of these checks succeed, we go to the
    // slow path.

    if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
      if (!optimizations.GetSourceIsNonPrimitiveArray()) {
        // /* HeapReference<Class> */ temp1 = src->klass_
        codegen_->GenerateFieldLoadWithBakerReadBarrier(
            invoke, temp1_loc, src, class_offset, temp2_loc, /* needs_null_check= */ false);
        // Bail out if the source is not a non primitive array.
        // /* HeapReference<Class> */ temp1 = temp1->component_type_
        codegen_->GenerateFieldLoadWithBakerReadBarrier(
            invoke, temp1_loc, temp1, component_offset, temp2_loc, /* needs_null_check= */ false);
        __ CompareAndBranchIfZero(temp1, intrinsic_slow_path->GetEntryLabel());
        // If heap poisoning is enabled, `temp1` has been unpoisoned
        // by the the previous call to GenerateFieldLoadWithBakerReadBarrier.
        // /* uint16_t */ temp1 = static_cast<uint16>(temp1->primitive_type_);
        __ Ldrh(temp1, MemOperand(temp1, primitive_offset));
        static_assert(Primitive::kPrimNot == 0, "Expected 0 for kPrimNot");
        __ CompareAndBranchIfNonZero(temp1, intrinsic_slow_path->GetEntryLabel());
      }

      // /* HeapReference<Class> */ temp1 = dest->klass_
      codegen_->GenerateFieldLoadWithBakerReadBarrier(
          invoke, temp1_loc, dest, class_offset, temp2_loc, /* needs_null_check= */ false);

      if (!optimizations.GetDestinationIsNonPrimitiveArray()) {
        // Bail out if the destination is not a non primitive array.
        //
        // Register `temp1` is not trashed by the read barrier emitted
        // by GenerateFieldLoadWithBakerReadBarrier below, as that
        // method produces a call to a ReadBarrierMarkRegX entry point,
        // which saves all potentially live registers, including
        // temporaries such a `temp1`.
        // /* HeapReference<Class> */ temp2 = temp1->component_type_
        codegen_->GenerateFieldLoadWithBakerReadBarrier(
            invoke, temp2_loc, temp1, component_offset, temp3_loc, /* needs_null_check= */ false);
        __ CompareAndBranchIfZero(temp2, intrinsic_slow_path->GetEntryLabel());
        // If heap poisoning is enabled, `temp2` has been unpoisoned
        // by the the previous call to GenerateFieldLoadWithBakerReadBarrier.
        // /* uint16_t */ temp2 = static_cast<uint16>(temp2->primitive_type_);
        __ Ldrh(temp2, MemOperand(temp2, primitive_offset));
        static_assert(Primitive::kPrimNot == 0, "Expected 0 for kPrimNot");
        __ CompareAndBranchIfNonZero(temp2, intrinsic_slow_path->GetEntryLabel());
      }

      // For the same reason given earlier, `temp1` is not trashed by the
      // read barrier emitted by GenerateFieldLoadWithBakerReadBarrier below.
      // /* HeapReference<Class> */ temp2 = src->klass_
      codegen_->GenerateFieldLoadWithBakerReadBarrier(
          invoke, temp2_loc, src, class_offset, temp3_loc, /* needs_null_check= */ false);
      // Note: if heap poisoning is on, we are comparing two unpoisoned references here.
      __ Cmp(temp1, temp2);

      if (optimizations.GetDestinationIsTypedObjectArray()) {
        vixl32::Label do_copy;
        __ B(eq, &do_copy, /* is_far_target= */ false);
        // /* HeapReference<Class> */ temp1 = temp1->component_type_
        codegen_->GenerateFieldLoadWithBakerReadBarrier(
            invoke, temp1_loc, temp1, component_offset, temp2_loc, /* needs_null_check= */ false);
        // /* HeapReference<Class> */ temp1 = temp1->super_class_
        // We do not need to emit a read barrier for the following
        // heap reference load, as `temp1` is only used in a
        // comparison with null below, and this reference is not
        // kept afterwards.
        __ Ldr(temp1, MemOperand(temp1, super_offset));
        __ CompareAndBranchIfNonZero(temp1, intrinsic_slow_path->GetEntryLabel());
        __ Bind(&do_copy);
      } else {
        __ B(ne, intrinsic_slow_path->GetEntryLabel());
      }
    } else {
      // Non read barrier code.

      // /* HeapReference<Class> */ temp1 = dest->klass_
      __ Ldr(temp1, MemOperand(dest, class_offset));
      // /* HeapReference<Class> */ temp2 = src->klass_
      __ Ldr(temp2, MemOperand(src, class_offset));
      bool did_unpoison = false;
      if (!optimizations.GetDestinationIsNonPrimitiveArray() ||
          !optimizations.GetSourceIsNonPrimitiveArray()) {
        // One or two of the references need to be unpoisoned. Unpoison them
        // both to make the identity check valid.
        assembler->MaybeUnpoisonHeapReference(temp1);
        assembler->MaybeUnpoisonHeapReference(temp2);
        did_unpoison = true;
      }

      if (!optimizations.GetDestinationIsNonPrimitiveArray()) {
        // Bail out if the destination is not a non primitive array.
        // /* HeapReference<Class> */ temp3 = temp1->component_type_
        __ Ldr(temp3, MemOperand(temp1, component_offset));
        __ CompareAndBranchIfZero(temp3, intrinsic_slow_path->GetEntryLabel());
        assembler->MaybeUnpoisonHeapReference(temp3);
        // /* uint16_t */ temp3 = static_cast<uint16>(temp3->primitive_type_);
        __ Ldrh(temp3, MemOperand(temp3, primitive_offset));
        static_assert(Primitive::kPrimNot == 0, "Expected 0 for kPrimNot");
        __ CompareAndBranchIfNonZero(temp3, intrinsic_slow_path->GetEntryLabel());
      }

      if (!optimizations.GetSourceIsNonPrimitiveArray()) {
        // Bail out if the source is not a non primitive array.
        // /* HeapReference<Class> */ temp3 = temp2->component_type_
        __ Ldr(temp3, MemOperand(temp2, component_offset));
        __ CompareAndBranchIfZero(temp3, intrinsic_slow_path->GetEntryLabel());
        assembler->MaybeUnpoisonHeapReference(temp3);
        // /* uint16_t */ temp3 = static_cast<uint16>(temp3->primitive_type_);
        __ Ldrh(temp3, MemOperand(temp3, primitive_offset));
        static_assert(Primitive::kPrimNot == 0, "Expected 0 for kPrimNot");
        __ CompareAndBranchIfNonZero(temp3, intrinsic_slow_path->GetEntryLabel());
      }

      __ Cmp(temp1, temp2);

      if (optimizations.GetDestinationIsTypedObjectArray()) {
        vixl32::Label do_copy;
        __ B(eq, &do_copy, /* is_far_target= */ false);
        if (!did_unpoison) {
          assembler->MaybeUnpoisonHeapReference(temp1);
        }
        // /* HeapReference<Class> */ temp1 = temp1->component_type_
        __ Ldr(temp1, MemOperand(temp1, component_offset));
        assembler->MaybeUnpoisonHeapReference(temp1);
        // /* HeapReference<Class> */ temp1 = temp1->super_class_
        __ Ldr(temp1, MemOperand(temp1, super_offset));
        // No need to unpoison the result, we're comparing against null.
        __ CompareAndBranchIfNonZero(temp1, intrinsic_slow_path->GetEntryLabel());
        __ Bind(&do_copy);
      } else {
        __ B(ne, intrinsic_slow_path->GetEntryLabel());
      }
    }
  } else if (!optimizations.GetSourceIsNonPrimitiveArray()) {
    DCHECK(optimizations.GetDestinationIsNonPrimitiveArray());
    // Bail out if the source is not a non primitive array.
    if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
      // /* HeapReference<Class> */ temp1 = src->klass_
      codegen_->GenerateFieldLoadWithBakerReadBarrier(
          invoke, temp1_loc, src, class_offset, temp2_loc, /* needs_null_check= */ false);
      // /* HeapReference<Class> */ temp3 = temp1->component_type_
      codegen_->GenerateFieldLoadWithBakerReadBarrier(
          invoke, temp3_loc, temp1, component_offset, temp2_loc, /* needs_null_check= */ false);
      __ CompareAndBranchIfZero(temp3, intrinsic_slow_path->GetEntryLabel());
      // If heap poisoning is enabled, `temp3` has been unpoisoned
      // by the the previous call to GenerateFieldLoadWithBakerReadBarrier.
    } else {
      // /* HeapReference<Class> */ temp1 = src->klass_
      __ Ldr(temp1, MemOperand(src, class_offset));
      assembler->MaybeUnpoisonHeapReference(temp1);
      // /* HeapReference<Class> */ temp3 = temp1->component_type_
      __ Ldr(temp3, MemOperand(temp1, component_offset));
      __ CompareAndBranchIfZero(temp3, intrinsic_slow_path->GetEntryLabel());
      assembler->MaybeUnpoisonHeapReference(temp3);
    }
    // /* uint16_t */ temp3 = static_cast<uint16>(temp3->primitive_type_);
    __ Ldrh(temp3, MemOperand(temp3, primitive_offset));
    static_assert(Primitive::kPrimNot == 0, "Expected 0 for kPrimNot");
    __ CompareAndBranchIfNonZero(temp3, intrinsic_slow_path->GetEntryLabel());
  }

  if (length.IsConstant() && Int32ConstantFrom(length) == 0) {
    // Null constant length: not need to emit the loop code at all.
  } else {
    vixl32::Label done;
    const DataType::Type type = DataType::Type::kReference;
    const int32_t element_size = DataType::Size(type);

    if (length.IsRegister()) {
      // Don't enter the copy loop if the length is null.
      __ CompareAndBranchIfZero(RegisterFrom(length), &done, /* is_far_target= */ false);
    }

    if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
      // TODO: Also convert this intrinsic to the IsGcMarking strategy?

      // SystemArrayCopy implementation for Baker read barriers (see
      // also CodeGeneratorARMVIXL::GenerateReferenceLoadWithBakerReadBarrier):
      //
      //   uint32_t rb_state = Lockword(src->monitor_).ReadBarrierState();
      //   lfence;  // Load fence or artificial data dependency to prevent load-load reordering
      //   bool is_gray = (rb_state == ReadBarrier::GrayState());
      //   if (is_gray) {
      //     // Slow-path copy.
      //     do {
      //       *dest_ptr++ = MaybePoison(ReadBarrier::Mark(MaybeUnpoison(*src_ptr++)));
      //     } while (src_ptr != end_ptr)
      //   } else {
      //     // Fast-path copy.
      //     do {
      //       *dest_ptr++ = *src_ptr++;
      //     } while (src_ptr != end_ptr)
      //   }

      // /* int32_t */ monitor = src->monitor_
      __ Ldr(temp2, MemOperand(src, monitor_offset));
      // /* LockWord */ lock_word = LockWord(monitor)
      static_assert(sizeof(LockWord) == sizeof(int32_t),
                    "art::LockWord and int32_t have different sizes.");

      // Introduce a dependency on the lock_word including the rb_state,
      // which shall prevent load-load reordering without using
      // a memory barrier (which would be more expensive).
      // `src` is unchanged by this operation, but its value now depends
      // on `temp2`.
      __ Add(src, src, Operand(temp2, vixl32::LSR, 32));

      // Compute the base source address in `temp1`.
      // Note that `temp1` (the base source address) is computed from
      // `src` (and `src_pos`) here, and thus honors the artificial
      // dependency of `src` on `temp2`.
      GenSystemArrayCopyBaseAddress(GetAssembler(), type, src, src_pos, temp1);
      // Compute the end source address in `temp3`.
      GenSystemArrayCopyEndAddress(GetAssembler(), type, length, temp1, temp3);
      // The base destination address is computed later, as `temp2` is
      // used for intermediate computations.

      // Slow path used to copy array when `src` is gray.
      // Note that the base destination address is computed in `temp2`
      // by the slow path code.
      SlowPathCodeARMVIXL* read_barrier_slow_path =
          new (codegen_->GetScopedAllocator()) ReadBarrierSystemArrayCopySlowPathARMVIXL(invoke);
      codegen_->AddSlowPath(read_barrier_slow_path);

      // Given the numeric representation, it's enough to check the low bit of the
      // rb_state. We do that by shifting the bit out of the lock word with LSRS
      // which can be a 16-bit instruction unlike the TST immediate.
      static_assert(ReadBarrier::NonGrayState() == 0, "Expecting non-gray to have value 0");
      static_assert(ReadBarrier::GrayState() == 1, "Expecting gray to have value 1");
      __ Lsrs(temp2, temp2, LockWord::kReadBarrierStateShift + 1);
      // Carry flag is the last bit shifted out by LSRS.
      __ B(cs, read_barrier_slow_path->GetEntryLabel());

      // Fast-path copy.
      // Compute the base destination address in `temp2`.
      GenSystemArrayCopyBaseAddress(GetAssembler(), type, dest, dest_pos, temp2);
      // Iterate over the arrays and do a raw copy of the objects. We don't need to
      // poison/unpoison.
      vixl32::Label loop;
      __ Bind(&loop);
      {
        UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
        const vixl32::Register temp_reg = temps.Acquire();
        __ Ldr(temp_reg, MemOperand(temp1, element_size, PostIndex));
        __ Str(temp_reg, MemOperand(temp2, element_size, PostIndex));
      }
      __ Cmp(temp1, temp3);
      __ B(ne, &loop, /* is_far_target= */ false);

      __ Bind(read_barrier_slow_path->GetExitLabel());
    } else {
      // Non read barrier code.
      // Compute the base source address in `temp1`.
      GenSystemArrayCopyBaseAddress(GetAssembler(), type, src, src_pos, temp1);
      // Compute the base destination address in `temp2`.
      GenSystemArrayCopyBaseAddress(GetAssembler(), type, dest, dest_pos, temp2);
      // Compute the end source address in `temp3`.
      GenSystemArrayCopyEndAddress(GetAssembler(), type, length, temp1, temp3);
      // Iterate over the arrays and do a raw copy of the objects. We don't need to
      // poison/unpoison.
      vixl32::Label loop;
      __ Bind(&loop);
      {
        UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
        const vixl32::Register temp_reg = temps.Acquire();
        __ Ldr(temp_reg, MemOperand(temp1, element_size, PostIndex));
        __ Str(temp_reg, MemOperand(temp2, element_size, PostIndex));
      }
      __ Cmp(temp1, temp3);
      __ B(ne, &loop, /* is_far_target= */ false);
    }
    __ Bind(&done);
  }

  // We only need one card marking on the destination array.
  codegen_->MarkGCCard(temp1, temp2, dest, NoReg, /* value_can_be_null= */ false);

  __ Bind(intrinsic_slow_path->GetExitLabel());
}

static void CreateFPToFPCallLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  // If the graph is debuggable, all callee-saved floating-point registers are blocked by
  // the code generator. Furthermore, the register allocator creates fixed live intervals
  // for all caller-saved registers because we are doing a function call. As a result, if
  // the input and output locations are unallocated, the register allocator runs out of
  // registers and fails; however, a debuggable graph is not the common case.
  if (invoke->GetBlock()->GetGraph()->IsDebuggable()) {
    return;
  }

  DCHECK_EQ(invoke->GetNumberOfArguments(), 1U);
  DCHECK_EQ(invoke->InputAt(0)->GetType(), DataType::Type::kFloat64);
  DCHECK_EQ(invoke->GetType(), DataType::Type::kFloat64);

  LocationSummary* const locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnMainOnly, kIntrinsified);
  const InvokeRuntimeCallingConventionARMVIXL calling_convention;

  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister());
  // Native code uses the soft float ABI.
  locations->AddTemp(LocationFrom(calling_convention.GetRegisterAt(0)));
  locations->AddTemp(LocationFrom(calling_convention.GetRegisterAt(1)));
}

static void CreateFPFPToFPCallLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  // If the graph is debuggable, all callee-saved floating-point registers are blocked by
  // the code generator. Furthermore, the register allocator creates fixed live intervals
  // for all caller-saved registers because we are doing a function call. As a result, if
  // the input and output locations are unallocated, the register allocator runs out of
  // registers and fails; however, a debuggable graph is not the common case.
  if (invoke->GetBlock()->GetGraph()->IsDebuggable()) {
    return;
  }

  DCHECK_EQ(invoke->GetNumberOfArguments(), 2U);
  DCHECK_EQ(invoke->InputAt(0)->GetType(), DataType::Type::kFloat64);
  DCHECK_EQ(invoke->InputAt(1)->GetType(), DataType::Type::kFloat64);
  DCHECK_EQ(invoke->GetType(), DataType::Type::kFloat64);

  LocationSummary* const locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnMainOnly, kIntrinsified);
  const InvokeRuntimeCallingConventionARMVIXL calling_convention;

  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetInAt(1, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister());
  // Native code uses the soft float ABI.
  locations->AddTemp(LocationFrom(calling_convention.GetRegisterAt(0)));
  locations->AddTemp(LocationFrom(calling_convention.GetRegisterAt(1)));
  locations->AddTemp(LocationFrom(calling_convention.GetRegisterAt(2)));
  locations->AddTemp(LocationFrom(calling_convention.GetRegisterAt(3)));
}

static void GenFPToFPCall(HInvoke* invoke,
                          ArmVIXLAssembler* assembler,
                          CodeGeneratorARMVIXL* codegen,
                          QuickEntrypointEnum entry) {
  LocationSummary* const locations = invoke->GetLocations();

  DCHECK_EQ(invoke->GetNumberOfArguments(), 1U);
  DCHECK(locations->WillCall() && locations->Intrinsified());

  // Native code uses the soft float ABI.
  __ Vmov(RegisterFrom(locations->GetTemp(0)),
          RegisterFrom(locations->GetTemp(1)),
          InputDRegisterAt(invoke, 0));
  codegen->InvokeRuntime(entry, invoke, invoke->GetDexPc());
  __ Vmov(OutputDRegister(invoke),
          RegisterFrom(locations->GetTemp(0)),
          RegisterFrom(locations->GetTemp(1)));
}

static void GenFPFPToFPCall(HInvoke* invoke,
                            ArmVIXLAssembler* assembler,
                            CodeGeneratorARMVIXL* codegen,
                            QuickEntrypointEnum entry) {
  LocationSummary* const locations = invoke->GetLocations();

  DCHECK_EQ(invoke->GetNumberOfArguments(), 2U);
  DCHECK(locations->WillCall() && locations->Intrinsified());

  // Native code uses the soft float ABI.
  __ Vmov(RegisterFrom(locations->GetTemp(0)),
          RegisterFrom(locations->GetTemp(1)),
          InputDRegisterAt(invoke, 0));
  __ Vmov(RegisterFrom(locations->GetTemp(2)),
          RegisterFrom(locations->GetTemp(3)),
          InputDRegisterAt(invoke, 1));
  codegen->InvokeRuntime(entry, invoke, invoke->GetDexPc());
  __ Vmov(OutputDRegister(invoke),
          RegisterFrom(locations->GetTemp(0)),
          RegisterFrom(locations->GetTemp(1)));
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathCos(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathCos(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickCos);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathSin(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathSin(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickSin);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathAcos(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathAcos(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickAcos);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathAsin(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathAsin(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickAsin);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathAtan(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathAtan(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickAtan);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathCbrt(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathCbrt(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickCbrt);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathCosh(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathCosh(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickCosh);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathExp(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathExp(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickExp);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathExpm1(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathExpm1(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickExpm1);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathLog(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathLog(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickLog);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathLog10(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathLog10(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickLog10);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathSinh(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathSinh(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickSinh);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathTan(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathTan(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickTan);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathTanh(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathTanh(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickTanh);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathAtan2(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathAtan2(HInvoke* invoke) {
  GenFPFPToFPCall(invoke, GetAssembler(), codegen_, kQuickAtan2);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathPow(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathPow(HInvoke* invoke) {
  GenFPFPToFPCall(invoke, GetAssembler(), codegen_, kQuickPow);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathHypot(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathHypot(HInvoke* invoke) {
  GenFPFPToFPCall(invoke, GetAssembler(), codegen_, kQuickHypot);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathNextAfter(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathNextAfter(HInvoke* invoke) {
  GenFPFPToFPCall(invoke, GetAssembler(), codegen_, kQuickNextAfter);
}

void IntrinsicLocationsBuilderARMVIXL::VisitIntegerReverse(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitIntegerReverse(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  __ Rbit(OutputRegister(invoke), InputRegisterAt(invoke, 0));
}

void IntrinsicLocationsBuilderARMVIXL::VisitLongReverse(HInvoke* invoke) {
  CreateLongToLongLocationsWithOverlap(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitLongReverse(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  vixl32::Register in_reg_lo  = LowRegisterFrom(locations->InAt(0));
  vixl32::Register in_reg_hi  = HighRegisterFrom(locations->InAt(0));
  vixl32::Register out_reg_lo = LowRegisterFrom(locations->Out());
  vixl32::Register out_reg_hi = HighRegisterFrom(locations->Out());

  __ Rbit(out_reg_lo, in_reg_hi);
  __ Rbit(out_reg_hi, in_reg_lo);
}

static void GenerateReverseBytesInPlaceForEachWord(ArmVIXLAssembler* assembler, Location pair) {
  DCHECK(pair.IsRegisterPair());
  __ Rev(LowRegisterFrom(pair), LowRegisterFrom(pair));
  __ Rev(HighRegisterFrom(pair), HighRegisterFrom(pair));
}

static void GenerateReverseBytes(ArmVIXLAssembler* assembler,
                                 DataType::Type type,
                                 Location in,
                                 Location out) {
  switch (type) {
    case DataType::Type::kUint16:
      __ Rev16(RegisterFrom(out), RegisterFrom(in));
      break;
    case DataType::Type::kInt16:
      __ Revsh(RegisterFrom(out), RegisterFrom(in));
      break;
    case DataType::Type::kInt32:
      __ Rev(RegisterFrom(out), RegisterFrom(in));
      break;
    case DataType::Type::kInt64:
      DCHECK(!LowRegisterFrom(out).Is(LowRegisterFrom(in)));
      __ Rev(LowRegisterFrom(out), HighRegisterFrom(in));
      __ Rev(HighRegisterFrom(out), LowRegisterFrom(in));
      break;
    case DataType::Type::kFloat32:
      __ Rev(RegisterFrom(in), RegisterFrom(in));  // Note: Clobbers `in`.
      __ Vmov(SRegisterFrom(out), RegisterFrom(in));
      break;
    case DataType::Type::kFloat64:
      GenerateReverseBytesInPlaceForEachWord(assembler, in);  // Note: Clobbers `in`.
      __ Vmov(DRegisterFrom(out), HighRegisterFrom(in), LowRegisterFrom(in));  // Swap high/low.
      break;
    default:
      LOG(FATAL) << "Unexpected type for reverse-bytes: " << type;
      UNREACHABLE();
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitIntegerReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitIntegerReverseBytes(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  GenerateReverseBytes(assembler, DataType::Type::kInt32, locations->InAt(0), locations->Out());
}

void IntrinsicLocationsBuilderARMVIXL::VisitLongReverseBytes(HInvoke* invoke) {
  CreateLongToLongLocationsWithOverlap(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitLongReverseBytes(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  GenerateReverseBytes(assembler, DataType::Type::kInt64, locations->InAt(0), locations->Out());
}

void IntrinsicLocationsBuilderARMVIXL::VisitShortReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitShortReverseBytes(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  GenerateReverseBytes(assembler, DataType::Type::kInt16, locations->InAt(0), locations->Out());
}

static void GenBitCount(HInvoke* instr, DataType::Type type, ArmVIXLAssembler* assembler) {
  DCHECK(DataType::IsIntOrLongType(type)) << type;
  DCHECK_EQ(instr->GetType(), DataType::Type::kInt32);
  DCHECK_EQ(DataType::Kind(instr->InputAt(0)->GetType()), type);

  bool is_long = type == DataType::Type::kInt64;
  LocationSummary* locations = instr->GetLocations();
  Location in = locations->InAt(0);
  vixl32::Register src_0 = is_long ? LowRegisterFrom(in) : RegisterFrom(in);
  vixl32::Register src_1 = is_long ? HighRegisterFrom(in) : src_0;
  vixl32::SRegister tmp_s = LowSRegisterFrom(locations->GetTemp(0));
  vixl32::DRegister tmp_d = DRegisterFrom(locations->GetTemp(0));
  vixl32::Register  out_r = OutputRegister(instr);

  // Move data from core register(s) to temp D-reg for bit count calculation, then move back.
  // According to Cortex A57 and A72 optimization guides, compared to transferring to full D-reg,
  // transferring data from core reg to upper or lower half of vfp D-reg requires extra latency,
  // That's why for integer bit count, we use 'vmov d0, r0, r0' instead of 'vmov d0[0], r0'.
  __ Vmov(tmp_d, src_1, src_0);     // Temp DReg |--src_1|--src_0|
  __ Vcnt(Untyped8, tmp_d, tmp_d);  // Temp DReg |c|c|c|c|c|c|c|c|
  __ Vpaddl(U8, tmp_d, tmp_d);      // Temp DReg |--c|--c|--c|--c|
  __ Vpaddl(U16, tmp_d, tmp_d);     // Temp DReg |------c|------c|
  if (is_long) {
    __ Vpaddl(U32, tmp_d, tmp_d);   // Temp DReg |--------------c|
  }
  __ Vmov(out_r, tmp_s);
}

void IntrinsicLocationsBuilderARMVIXL::VisitIntegerBitCount(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
  invoke->GetLocations()->AddTemp(Location::RequiresFpuRegister());
}

void IntrinsicCodeGeneratorARMVIXL::VisitIntegerBitCount(HInvoke* invoke) {
  GenBitCount(invoke, DataType::Type::kInt32, GetAssembler());
}

void IntrinsicLocationsBuilderARMVIXL::VisitLongBitCount(HInvoke* invoke) {
  VisitIntegerBitCount(invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitLongBitCount(HInvoke* invoke) {
  GenBitCount(invoke, DataType::Type::kInt64, GetAssembler());
}

static void GenHighestOneBit(HInvoke* invoke,
                             DataType::Type type,
                             CodeGeneratorARMVIXL* codegen) {
  DCHECK(DataType::IsIntOrLongType(type));

  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  const vixl32::Register temp = temps.Acquire();

  if (type == DataType::Type::kInt64) {
    LocationSummary* locations = invoke->GetLocations();
    Location in = locations->InAt(0);
    Location out = locations->Out();

    vixl32::Register in_reg_lo = LowRegisterFrom(in);
    vixl32::Register in_reg_hi = HighRegisterFrom(in);
    vixl32::Register out_reg_lo = LowRegisterFrom(out);
    vixl32::Register out_reg_hi = HighRegisterFrom(out);

    __ Mov(temp, 0x80000000);  // Modified immediate.
    __ Clz(out_reg_lo, in_reg_lo);
    __ Clz(out_reg_hi, in_reg_hi);
    __ Lsr(out_reg_lo, temp, out_reg_lo);
    __ Lsrs(out_reg_hi, temp, out_reg_hi);

    // Discard result for lowest 32 bits if highest 32 bits are not zero.
    // Since IT blocks longer than a 16-bit instruction are deprecated by ARMv8,
    // we check that the output is in a low register, so that a 16-bit MOV
    // encoding can be used. If output is in a high register, then we generate
    // 4 more bytes of code to avoid a branch.
    Operand mov_src(0);
    if (!out_reg_lo.IsLow()) {
      __ Mov(LeaveFlags, temp, 0);
      mov_src = Operand(temp);
    }
    ExactAssemblyScope it_scope(codegen->GetVIXLAssembler(),
                                  2 * vixl32::k16BitT32InstructionSizeInBytes,
                                  CodeBufferCheckScope::kExactSize);
    __ it(ne);
    __ mov(ne, out_reg_lo, mov_src);
  } else {
    vixl32::Register out = OutputRegister(invoke);
    vixl32::Register in = InputRegisterAt(invoke, 0);

    __ Mov(temp, 0x80000000);  // Modified immediate.
    __ Clz(out, in);
    __ Lsr(out, temp, out);
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitIntegerHighestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitIntegerHighestOneBit(HInvoke* invoke) {
  GenHighestOneBit(invoke, DataType::Type::kInt32, codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitLongHighestOneBit(HInvoke* invoke) {
  CreateLongToLongLocationsWithOverlap(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitLongHighestOneBit(HInvoke* invoke) {
  GenHighestOneBit(invoke, DataType::Type::kInt64, codegen_);
}

static void GenLowestOneBit(HInvoke* invoke,
                            DataType::Type type,
                            CodeGeneratorARMVIXL* codegen) {
  DCHECK(DataType::IsIntOrLongType(type));

  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  const vixl32::Register temp = temps.Acquire();

  if (type == DataType::Type::kInt64) {
    LocationSummary* locations = invoke->GetLocations();
    Location in = locations->InAt(0);
    Location out = locations->Out();

    vixl32::Register in_reg_lo = LowRegisterFrom(in);
    vixl32::Register in_reg_hi = HighRegisterFrom(in);
    vixl32::Register out_reg_lo = LowRegisterFrom(out);
    vixl32::Register out_reg_hi = HighRegisterFrom(out);

    __ Rsb(out_reg_hi, in_reg_hi, 0);
    __ Rsb(out_reg_lo, in_reg_lo, 0);
    __ And(out_reg_hi, out_reg_hi, in_reg_hi);
    // The result of this operation is 0 iff in_reg_lo is 0
    __ Ands(out_reg_lo, out_reg_lo, in_reg_lo);

    // Discard result for highest 32 bits if lowest 32 bits are not zero.
    // Since IT blocks longer than a 16-bit instruction are deprecated by ARMv8,
    // we check that the output is in a low register, so that a 16-bit MOV
    // encoding can be used. If output is in a high register, then we generate
    // 4 more bytes of code to avoid a branch.
    Operand mov_src(0);
    if (!out_reg_lo.IsLow()) {
      __ Mov(LeaveFlags, temp, 0);
      mov_src = Operand(temp);
    }
    ExactAssemblyScope it_scope(codegen->GetVIXLAssembler(),
                                  2 * vixl32::k16BitT32InstructionSizeInBytes,
                                  CodeBufferCheckScope::kExactSize);
    __ it(ne);
    __ mov(ne, out_reg_hi, mov_src);
  } else {
    vixl32::Register out = OutputRegister(invoke);
    vixl32::Register in = InputRegisterAt(invoke, 0);

    __ Rsb(temp, in, 0);
    __ And(out, temp, in);
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitIntegerLowestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitIntegerLowestOneBit(HInvoke* invoke) {
  GenLowestOneBit(invoke, DataType::Type::kInt32, codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitLongLowestOneBit(HInvoke* invoke) {
  CreateLongToLongLocationsWithOverlap(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitLongLowestOneBit(HInvoke* invoke) {
  GenLowestOneBit(invoke, DataType::Type::kInt64, codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitStringGetCharsNoCheck(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->SetInAt(4, Location::RequiresRegister());

  // Temporary registers to store lengths of strings and for calculations.
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorARMVIXL::VisitStringGetCharsNoCheck(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  // Check assumption that sizeof(Char) is 2 (used in scaling below).
  const size_t char_size = DataType::Size(DataType::Type::kUint16);
  DCHECK_EQ(char_size, 2u);

  // Location of data in char array buffer.
  const uint32_t data_offset = mirror::Array::DataOffset(char_size).Uint32Value();

  // Location of char array data in string.
  const uint32_t value_offset = mirror::String::ValueOffset().Uint32Value();

  // void getCharsNoCheck(int srcBegin, int srcEnd, char[] dst, int dstBegin);
  // Since getChars() calls getCharsNoCheck() - we use registers rather than constants.
  vixl32::Register srcObj = InputRegisterAt(invoke, 0);
  vixl32::Register srcBegin = InputRegisterAt(invoke, 1);
  vixl32::Register srcEnd = InputRegisterAt(invoke, 2);
  vixl32::Register dstObj = InputRegisterAt(invoke, 3);
  vixl32::Register dstBegin = InputRegisterAt(invoke, 4);

  vixl32::Register num_chr = RegisterFrom(locations->GetTemp(0));
  vixl32::Register src_ptr = RegisterFrom(locations->GetTemp(1));
  vixl32::Register dst_ptr = RegisterFrom(locations->GetTemp(2));

  vixl32::Label done, compressed_string_loop;
  vixl32::Label* final_label = codegen_->GetFinalLabel(invoke, &done);
  // dst to be copied.
  __ Add(dst_ptr, dstObj, data_offset);
  __ Add(dst_ptr, dst_ptr, Operand(dstBegin, vixl32::LSL, 1));

  __ Subs(num_chr, srcEnd, srcBegin);
  // Early out for valid zero-length retrievals.
  __ B(eq, final_label, /* is_far_target= */ false);

  // src range to copy.
  __ Add(src_ptr, srcObj, value_offset);

  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  vixl32::Register temp;
  vixl32::Label compressed_string_preloop;
  if (mirror::kUseStringCompression) {
    // Location of count in string.
    const uint32_t count_offset = mirror::String::CountOffset().Uint32Value();
    temp = temps.Acquire();
    // String's length.
    __ Ldr(temp, MemOperand(srcObj, count_offset));
    __ Tst(temp, 1);
    temps.Release(temp);
    __ B(eq, &compressed_string_preloop, /* is_far_target= */ false);
  }
  __ Add(src_ptr, src_ptr, Operand(srcBegin, vixl32::LSL, 1));

  // Do the copy.
  vixl32::Label loop, remainder;

  temp = temps.Acquire();
  // Save repairing the value of num_chr on the < 4 character path.
  __ Subs(temp, num_chr, 4);
  __ B(lt, &remainder, /* is_far_target= */ false);

  // Keep the result of the earlier subs, we are going to fetch at least 4 characters.
  __ Mov(num_chr, temp);

  // Main loop used for longer fetches loads and stores 4x16-bit characters at a time.
  // (LDRD/STRD fault on unaligned addresses and it's not worth inlining extra code
  // to rectify these everywhere this intrinsic applies.)
  __ Bind(&loop);
  __ Ldr(temp, MemOperand(src_ptr, char_size * 2));
  __ Subs(num_chr, num_chr, 4);
  __ Str(temp, MemOperand(dst_ptr, char_size * 2));
  __ Ldr(temp, MemOperand(src_ptr, char_size * 4, PostIndex));
  __ Str(temp, MemOperand(dst_ptr, char_size * 4, PostIndex));
  temps.Release(temp);
  __ B(ge, &loop, /* is_far_target= */ false);

  __ Adds(num_chr, num_chr, 4);
  __ B(eq, final_label, /* is_far_target= */ false);

  // Main loop for < 4 character case and remainder handling. Loads and stores one
  // 16-bit Java character at a time.
  __ Bind(&remainder);
  temp = temps.Acquire();
  __ Ldrh(temp, MemOperand(src_ptr, char_size, PostIndex));
  __ Subs(num_chr, num_chr, 1);
  __ Strh(temp, MemOperand(dst_ptr, char_size, PostIndex));
  temps.Release(temp);
  __ B(gt, &remainder, /* is_far_target= */ false);

  if (mirror::kUseStringCompression) {
    __ B(final_label);

    const size_t c_char_size = DataType::Size(DataType::Type::kInt8);
    DCHECK_EQ(c_char_size, 1u);
    // Copy loop for compressed src, copying 1 character (8-bit) to (16-bit) at a time.
    __ Bind(&compressed_string_preloop);
    __ Add(src_ptr, src_ptr, srcBegin);
    __ Bind(&compressed_string_loop);
    temp = temps.Acquire();
    __ Ldrb(temp, MemOperand(src_ptr, c_char_size, PostIndex));
    __ Strh(temp, MemOperand(dst_ptr, char_size, PostIndex));
    temps.Release(temp);
    __ Subs(num_chr, num_chr, 1);
    __ B(gt, &compressed_string_loop, /* is_far_target= */ false);
  }

  if (done.IsReferenced()) {
    __ Bind(&done);
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitFloatIsInfinite(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitFloatIsInfinite(HInvoke* invoke) {
  ArmVIXLAssembler* const assembler = GetAssembler();
  const vixl32::Register out = OutputRegister(invoke);
  // Shifting left by 1 bit makes the value encodable as an immediate operand;
  // we don't care about the sign bit anyway.
  constexpr uint32_t infinity = kPositiveInfinityFloat << 1U;

  __ Vmov(out, InputSRegisterAt(invoke, 0));
  // We don't care about the sign bit, so shift left.
  __ Lsl(out, out, 1);
  __ Eor(out, out, infinity);
  codegen_->GenerateConditionWithZero(kCondEQ, out, out);
}

void IntrinsicLocationsBuilderARMVIXL::VisitDoubleIsInfinite(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitDoubleIsInfinite(HInvoke* invoke) {
  ArmVIXLAssembler* const assembler = GetAssembler();
  const vixl32::Register out = OutputRegister(invoke);
  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  const vixl32::Register temp = temps.Acquire();
  // The highest 32 bits of double precision positive infinity separated into
  // two constants encodable as immediate operands.
  constexpr uint32_t infinity_high  = 0x7f000000U;
  constexpr uint32_t infinity_high2 = 0x00f00000U;

  static_assert((infinity_high | infinity_high2) ==
                    static_cast<uint32_t>(kPositiveInfinityDouble >> 32U),
                "The constants do not add up to the high 32 bits of double "
                "precision positive infinity.");
  __ Vmov(temp, out, InputDRegisterAt(invoke, 0));
  __ Eor(out, out, infinity_high);
  __ Eor(out, out, infinity_high2);
  // We don't care about the sign bit, so shift left.
  __ Orr(out, temp, Operand(out, vixl32::LSL, 1));
  codegen_->GenerateConditionWithZero(kCondEQ, out, out);
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathCeil(HInvoke* invoke) {
  if (features_.HasARMv8AInstructions()) {
    CreateFPToFPLocations(allocator_, invoke);
  }
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathCeil(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  DCHECK(codegen_->GetInstructionSetFeatures().HasARMv8AInstructions());
  __ Vrintp(F64, OutputDRegister(invoke), InputDRegisterAt(invoke, 0));
}

void IntrinsicLocationsBuilderARMVIXL::VisitMathFloor(HInvoke* invoke) {
  if (features_.HasARMv8AInstructions()) {
    CreateFPToFPLocations(allocator_, invoke);
  }
}

void IntrinsicCodeGeneratorARMVIXL::VisitMathFloor(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  DCHECK(codegen_->GetInstructionSetFeatures().HasARMv8AInstructions());
  __ Vrintm(F64, OutputDRegister(invoke), InputDRegisterAt(invoke, 0));
}

void IntrinsicLocationsBuilderARMVIXL::VisitIntegerValueOf(HInvoke* invoke) {
  InvokeRuntimeCallingConventionARMVIXL calling_convention;
  IntrinsicVisitor::ComputeIntegerValueOfLocations(
      invoke,
      codegen_,
      LocationFrom(r0),
      LocationFrom(calling_convention.GetRegisterAt(0)));
}

void IntrinsicCodeGeneratorARMVIXL::VisitIntegerValueOf(HInvoke* invoke) {
  IntrinsicVisitor::IntegerValueOfInfo info =
      IntrinsicVisitor::ComputeIntegerValueOfInfo(invoke, codegen_->GetCompilerOptions());
  LocationSummary* locations = invoke->GetLocations();
  ArmVIXLAssembler* const assembler = GetAssembler();

  vixl32::Register out = RegisterFrom(locations->Out());
  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  vixl32::Register temp = temps.Acquire();
  auto allocate_instance = [&]() {
    DCHECK(out.Is(InvokeRuntimeCallingConventionARMVIXL().GetRegisterAt(0)));
    codegen_->LoadIntrinsicDeclaringClass(out, invoke);
    codegen_->InvokeRuntime(kQuickAllocObjectInitialized, invoke, invoke->GetDexPc());
    CheckEntrypointTypes<kQuickAllocObjectWithChecks, void*, mirror::Class*>();
  };
  if (invoke->InputAt(0)->IsConstant()) {
    int32_t value = invoke->InputAt(0)->AsIntConstant()->GetValue();
    if (static_cast<uint32_t>(value - info.low) < info.length) {
      // Just embed the j.l.Integer in the code.
      DCHECK_NE(info.value_boot_image_reference, IntegerValueOfInfo::kInvalidReference);
      codegen_->LoadBootImageAddress(out, info.value_boot_image_reference);
    } else {
      DCHECK(locations->CanCall());
      // Allocate and initialize a new j.l.Integer.
      // TODO: If we JIT, we could allocate the j.l.Integer now, and store it in the
      // JIT object table.
      allocate_instance();
      __ Mov(temp, value);
      assembler->StoreToOffset(kStoreWord, temp, out, info.value_offset);
      // Class pointer and `value` final field stores require a barrier before publication.
      codegen_->GenerateMemoryBarrier(MemBarrierKind::kStoreStore);
    }
  } else {
    DCHECK(locations->CanCall());
    vixl32::Register in = RegisterFrom(locations->InAt(0));
    // Check bounds of our cache.
    __ Add(out, in, -info.low);
    __ Cmp(out, info.length);
    vixl32::Label allocate, done;
    __ B(hs, &allocate, /* is_far_target= */ false);
    // If the value is within the bounds, load the j.l.Integer directly from the array.
    codegen_->LoadBootImageAddress(temp, info.array_data_boot_image_reference);
    codegen_->LoadFromShiftedRegOffset(DataType::Type::kReference, locations->Out(), temp, out);
    assembler->MaybeUnpoisonHeapReference(out);
    __ B(&done);
    __ Bind(&allocate);
    // Otherwise allocate and initialize a new j.l.Integer.
    allocate_instance();
    assembler->StoreToOffset(kStoreWord, in, out, info.value_offset);
    // Class pointer and `value` final field stores require a barrier before publication.
    codegen_->GenerateMemoryBarrier(MemBarrierKind::kStoreStore);
    __ Bind(&done);
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitReferenceGetReferent(HInvoke* invoke) {
  IntrinsicVisitor::CreateReferenceGetReferentLocations(invoke, codegen_);
}

void IntrinsicCodeGeneratorARMVIXL::VisitReferenceGetReferent(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Location obj = locations->InAt(0);
  Location out = locations->Out();

  SlowPathCodeARMVIXL* slow_path = new (GetAllocator()) IntrinsicSlowPathARMVIXL(invoke);
  codegen_->AddSlowPath(slow_path);

  if (kEmitCompilerReadBarrier) {
    // Check self->GetWeakRefAccessEnabled().
    UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
    vixl32::Register temp = temps.Acquire();
    __ Ldr(temp,
           MemOperand(tr, Thread::WeakRefAccessEnabledOffset<kArmPointerSize>().Uint32Value()));
    __ Cmp(temp, 0);
    __ B(eq, slow_path->GetEntryLabel());
  }

  {
    // Load the java.lang.ref.Reference class.
    UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
    vixl32::Register temp = temps.Acquire();
    codegen_->LoadIntrinsicDeclaringClass(temp, invoke);

    // Check static fields java.lang.ref.Reference.{disableIntrinsic,slowPathEnabled} together.
    MemberOffset disable_intrinsic_offset = IntrinsicVisitor::GetReferenceDisableIntrinsicOffset();
    DCHECK_ALIGNED(disable_intrinsic_offset.Uint32Value(), 2u);
    DCHECK_EQ(disable_intrinsic_offset.Uint32Value() + 1u,
              IntrinsicVisitor::GetReferenceSlowPathEnabledOffset().Uint32Value());
    __ Ldrh(temp, MemOperand(temp, disable_intrinsic_offset.Uint32Value()));
    __ Cmp(temp, 0);
    __ B(ne, slow_path->GetEntryLabel());
  }

  // Load the value from the field.
  uint32_t referent_offset = mirror::Reference::ReferentOffset().Uint32Value();
  if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
    codegen_->GenerateFieldLoadWithBakerReadBarrier(invoke,
                                                    out,
                                                    RegisterFrom(obj),
                                                    referent_offset,
                                                    /*maybe_temp=*/ Location::NoLocation(),
                                                    /*needs_null_check=*/ true);
    codegen_->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);  // `referent` is volatile.
  } else {
    {
      vixl::EmissionCheckScope guard(codegen_->GetVIXLAssembler(), kMaxMacroInstructionSizeInBytes);
      __ Ldr(RegisterFrom(out), MemOperand(RegisterFrom(obj), referent_offset));
      codegen_->MaybeRecordImplicitNullCheck(invoke);
    }
    codegen_->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);  // `referent` is volatile.
    codegen_->MaybeGenerateReadBarrierSlow(invoke, out, out, obj, referent_offset);
  }
  __ Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderARMVIXL::VisitReferenceRefersTo(HInvoke* invoke) {
  IntrinsicVisitor::CreateReferenceRefersToLocations(invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitReferenceRefersTo(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  ArmVIXLAssembler* assembler = GetAssembler();
  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());

  vixl32::Register obj = RegisterFrom(locations->InAt(0));
  vixl32::Register other = RegisterFrom(locations->InAt(1));
  vixl32::Register out = RegisterFrom(locations->Out());
  vixl32::Register tmp = temps.Acquire();

  uint32_t referent_offset = mirror::Reference::ReferentOffset().Uint32Value();
  uint32_t monitor_offset = mirror::Object::MonitorOffset().Int32Value();

  {
    // Ensure that between load and MaybeRecordImplicitNullCheck there are no pools emitted.
    // Loading scratch register always uses 32-bit encoding.
    vixl::ExactAssemblyScope eas(assembler->GetVIXLAssembler(),
                                 vixl32::k32BitT32InstructionSizeInBytes);
    __ ldr(tmp, MemOperand(obj, referent_offset));
    codegen_->MaybeRecordImplicitNullCheck(invoke);
  }
  assembler->MaybeUnpoisonHeapReference(tmp);
  codegen_->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);  // `referent` is volatile.

  if (kEmitCompilerReadBarrier) {
    DCHECK(kUseBakerReadBarrier);

    vixl32::Label calculate_result;
    __ Subs(out, tmp, other);
    __ B(eq, &calculate_result);  // `out` is 0 if taken.

    // Check if the loaded reference is null.
    __ Cmp(tmp, 0);
    __ B(eq, &calculate_result);  // `out` is not 0 if taken.

    // For correct memory visibility, we need a barrier before loading the lock word
    // but we already have the barrier emitted for volatile load above which is sufficient.

    // Load the lockword and check if it is a forwarding address.
    static_assert(LockWord::kStateShift == 30u);
    static_assert(LockWord::kStateForwardingAddress == 3u);
    __ Ldr(tmp, MemOperand(tmp, monitor_offset));
    __ Cmp(tmp, Operand(0xc0000000));
    __ B(lo, &calculate_result);   // `out` is not 0 if taken.

    // Extract the forwarding address and subtract from `other`.
    __ Sub(out, other, Operand(tmp, LSL, LockWord::kForwardingAddressShift));

    __ Bind(&calculate_result);
  } else {
    DCHECK(!kEmitCompilerReadBarrier);
    __ Sub(out, tmp, other);
  }

  // Convert 0 to 1 and non-zero to 0 for the Boolean result (`out = (out == 0)`).
  __ Clz(out, out);
  __ Lsr(out, out, WhichPowerOf2(out.GetSizeInBits()));
}

void IntrinsicLocationsBuilderARMVIXL::VisitThreadInterrupted(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorARMVIXL::VisitThreadInterrupted(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  vixl32::Register out = RegisterFrom(invoke->GetLocations()->Out());
  int32_t offset = Thread::InterruptedOffset<kArmPointerSize>().Int32Value();
  __ Ldr(out, MemOperand(tr, offset));
  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  vixl32::Register temp = temps.Acquire();
  vixl32::Label done;
  vixl32::Label* const final_label = codegen_->GetFinalLabel(invoke, &done);
  __ CompareAndBranchIfZero(out, final_label, /* is_far_target= */ false);
  __ Dmb(vixl32::ISH);
  __ Mov(temp, 0);
  assembler->StoreToOffset(kStoreWord, temp, tr, offset);
  __ Dmb(vixl32::ISH);
  if (done.IsReferenced()) {
    __ Bind(&done);
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitReachabilityFence(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::Any());
}

void IntrinsicCodeGeneratorARMVIXL::VisitReachabilityFence(HInvoke* invoke ATTRIBUTE_UNUSED) { }

void IntrinsicLocationsBuilderARMVIXL::VisitIntegerDivideUnsigned(HInvoke* invoke) {
  CreateIntIntToIntSlowPathCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorARMVIXL::VisitIntegerDivideUnsigned(HInvoke* invoke) {
  ArmVIXLAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  vixl32::Register dividend = RegisterFrom(locations->InAt(0));
  vixl32::Register divisor = RegisterFrom(locations->InAt(1));
  vixl32::Register out = RegisterFrom(locations->Out());

  // Check if divisor is zero, bail to managed implementation to handle.
  SlowPathCodeARMVIXL* slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathARMVIXL(invoke);
  codegen_->AddSlowPath(slow_path);
  __ CompareAndBranchIfZero(divisor, slow_path->GetEntryLabel());

  __ Udiv(out, dividend, divisor);

  __ Bind(slow_path->GetExitLabel());
}

static inline bool Use64BitExclusiveLoadStore(bool atomic, CodeGeneratorARMVIXL* codegen) {
  return atomic && !codegen->GetInstructionSetFeatures().HasAtomicLdrdAndStrd();
}

static void GenerateIntrinsicGet(HInvoke* invoke,
                                 CodeGeneratorARMVIXL* codegen,
                                 DataType::Type type,
                                 std::memory_order order,
                                 bool atomic,
                                 vixl32::Register base,
                                 vixl32::Register offset,
                                 Location out,
                                 Location maybe_temp,
                                 Location maybe_temp2,
                                 Location maybe_temp3) {
  bool seq_cst_barrier = (order == std::memory_order_seq_cst);
  bool acquire_barrier = seq_cst_barrier || (order == std::memory_order_acquire);
  DCHECK(acquire_barrier || order == std::memory_order_relaxed);
  DCHECK(atomic || order == std::memory_order_relaxed);

  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  MemOperand address(base, offset);
  switch (type) {
    case DataType::Type::kBool:
      __ Ldrb(RegisterFrom(out), address);
      break;
    case DataType::Type::kInt8:
      __ Ldrsb(RegisterFrom(out), address);
      break;
    case DataType::Type::kUint16:
      __ Ldrh(RegisterFrom(out), address);
      break;
    case DataType::Type::kInt16:
      __ Ldrsh(RegisterFrom(out), address);
      break;
    case DataType::Type::kInt32:
      __ Ldr(RegisterFrom(out), address);
      break;
    case DataType::Type::kInt64:
      if (Use64BitExclusiveLoadStore(atomic, codegen)) {
        vixl32::Register strexd_tmp = RegisterFrom(maybe_temp);
        UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
        const vixl32::Register temp_reg = temps.Acquire();
        __ Add(temp_reg, base, offset);
        vixl32::Label loop;
        __ Bind(&loop);
        __ Ldrexd(LowRegisterFrom(out), HighRegisterFrom(out), MemOperand(temp_reg));
        __ Strexd(strexd_tmp, LowRegisterFrom(out), HighRegisterFrom(out), MemOperand(temp_reg));
        __ Cmp(strexd_tmp, 0);
        __ B(ne, &loop);
      } else {
        __ Ldrd(LowRegisterFrom(out), HighRegisterFrom(out), address);
      }
      break;
    case DataType::Type::kReference:
      if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
        // Piggy-back on the field load path using introspection for the Baker read barrier.
        vixl32::Register temp = RegisterFrom(maybe_temp);
        __ Add(temp, base, offset);
        codegen->GenerateFieldLoadWithBakerReadBarrier(
            invoke, out, base, MemOperand(temp), /* needs_null_check= */ false);
      } else {
        __ Ldr(RegisterFrom(out), address);
      }
      break;
    case DataType::Type::kFloat32: {
      UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
      const vixl32::Register temp_reg = temps.Acquire();
      __ Add(temp_reg, base, offset);
      __ Vldr(SRegisterFrom(out), MemOperand(temp_reg));
      break;
    }
    case DataType::Type::kFloat64: {
      UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
      const vixl32::Register temp_reg = temps.Acquire();
      __ Add(temp_reg, base, offset);
      if (Use64BitExclusiveLoadStore(atomic, codegen)) {
        vixl32::Register lo = RegisterFrom(maybe_temp);
        vixl32::Register hi = RegisterFrom(maybe_temp2);
        vixl32::Register strexd_tmp = RegisterFrom(maybe_temp3);
        vixl32::Label loop;
        __ Bind(&loop);
        __ Ldrexd(lo, hi, MemOperand(temp_reg));
        __ Strexd(strexd_tmp, lo, hi, MemOperand(temp_reg));
        __ Cmp(strexd_tmp, 0);
        __ B(ne, &loop);
        __ Vmov(DRegisterFrom(out), lo, hi);
      } else {
        __ Vldr(DRegisterFrom(out), MemOperand(temp_reg));
      }
      break;
    }
    default:
      LOG(FATAL) << "Unexpected type " << type;
      UNREACHABLE();
  }
  if (acquire_barrier) {
    codegen->GenerateMemoryBarrier(
        seq_cst_barrier ? MemBarrierKind::kAnyAny : MemBarrierKind::kLoadAny);
  }
  if (type == DataType::Type::kReference && !(kEmitCompilerReadBarrier && kUseBakerReadBarrier)) {
    Location base_loc = LocationFrom(base);
    Location index_loc = LocationFrom(offset);
    codegen->MaybeGenerateReadBarrierSlow(invoke, out, out, base_loc, /* offset=*/ 0u, index_loc);
  }
}

static void CreateUnsafeGetLocations(HInvoke* invoke,
                                     CodeGeneratorARMVIXL* codegen,
                                     DataType::Type type,
                                     bool atomic) {
  bool can_call = kEmitCompilerReadBarrier &&
      (invoke->GetIntrinsic() == Intrinsics::kUnsafeGetObject ||
       invoke->GetIntrinsic() == Intrinsics::kUnsafeGetObjectVolatile);
  ArenaAllocator* allocator = invoke->GetBlock()->GetGraph()->GetAllocator();
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke,
                                      can_call
                                          ? LocationSummary::kCallOnSlowPath
                                          : LocationSummary::kNoCall,
                                      kIntrinsified);
  if (can_call && kUseBakerReadBarrier) {
    locations->SetCustomSlowPathCallerSaves(RegisterSet::Empty());  // No caller-save registers.
  }
  locations->SetInAt(0, Location::NoLocation());        // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(),
                    (can_call ? Location::kOutputOverlap : Location::kNoOutputOverlap));
  if ((kEmitCompilerReadBarrier && kUseBakerReadBarrier && type == DataType::Type::kReference) ||
      (type == DataType::Type::kInt64 && Use64BitExclusiveLoadStore(atomic, codegen))) {
    // We need a temporary register for the read barrier marking slow
    // path in CodeGeneratorARMVIXL::GenerateReferenceLoadWithBakerReadBarrier,
    // or the STREXD result for LDREXD/STREXD sequence when LDRD is non-atomic.
    locations->AddTemp(Location::RequiresRegister());
  }
}

static void GenUnsafeGet(HInvoke* invoke,
                         CodeGeneratorARMVIXL* codegen,
                         DataType::Type type,
                         std::memory_order order,
                         bool atomic) {
  LocationSummary* locations = invoke->GetLocations();
  vixl32::Register base = InputRegisterAt(invoke, 1);     // Object pointer.
  vixl32::Register offset = LowRegisterFrom(locations->InAt(2));  // Long offset, lo part only.
  Location out = locations->Out();
  Location maybe_temp = Location::NoLocation();
  if ((kEmitCompilerReadBarrier && kUseBakerReadBarrier && type == DataType::Type::kReference) ||
      (type == DataType::Type::kInt64 && Use64BitExclusiveLoadStore(atomic, codegen))) {
    maybe_temp = locations->GetTemp(0);
  }
  GenerateIntrinsicGet(invoke,
                       codegen,
                       type,
                       order,
                       atomic,
                       base,
                       offset,
                       out,
                       maybe_temp,
                       /*maybe_temp2=*/ Location::NoLocation(),
                       /*maybe_temp3=*/ Location::NoLocation());
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafeGet(HInvoke* invoke) {
  CreateUnsafeGetLocations(invoke, codegen_, DataType::Type::kInt32, /*atomic=*/ false);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafeGet(HInvoke* invoke) {
  GenUnsafeGet(
      invoke, codegen_, DataType::Type::kInt32, std::memory_order_relaxed, /*atomic=*/ false);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafeGetVolatile(HInvoke* invoke) {
  CreateUnsafeGetLocations(invoke, codegen_, DataType::Type::kInt32, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafeGetVolatile(HInvoke* invoke) {
  GenUnsafeGet(
      invoke, codegen_, DataType::Type::kInt32, std::memory_order_seq_cst, /*atomic=*/ true);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafeGetLong(HInvoke* invoke) {
  CreateUnsafeGetLocations(invoke, codegen_, DataType::Type::kInt64, /*atomic=*/ false);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafeGetLong(HInvoke* invoke) {
  GenUnsafeGet(
      invoke, codegen_, DataType::Type::kInt64, std::memory_order_relaxed, /*atomic=*/ false);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafeGetLongVolatile(HInvoke* invoke) {
  CreateUnsafeGetLocations(invoke, codegen_, DataType::Type::kInt64, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafeGetLongVolatile(HInvoke* invoke) {
  GenUnsafeGet(
      invoke, codegen_, DataType::Type::kInt64, std::memory_order_seq_cst, /*atomic=*/ true);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafeGetObject(HInvoke* invoke) {
  CreateUnsafeGetLocations(invoke, codegen_, DataType::Type::kReference, /*atomic=*/ false);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafeGetObject(HInvoke* invoke) {
  GenUnsafeGet(
      invoke, codegen_, DataType::Type::kReference, std::memory_order_relaxed, /*atomic=*/ false);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafeGetObjectVolatile(HInvoke* invoke) {
  CreateUnsafeGetLocations(invoke, codegen_, DataType::Type::kReference, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafeGetObjectVolatile(HInvoke* invoke) {
  GenUnsafeGet(
      invoke, codegen_, DataType::Type::kReference, std::memory_order_seq_cst, /*atomic=*/ true);
}

static void GenerateIntrinsicSet(CodeGeneratorARMVIXL* codegen,
                                 DataType::Type type,
                                 std::memory_order order,
                                 bool atomic,
                                 vixl32::Register base,
                                 vixl32::Register offset,
                                 Location value,
                                 Location maybe_temp,
                                 Location maybe_temp2,
                                 Location maybe_temp3) {
  bool seq_cst_barrier = (order == std::memory_order_seq_cst);
  bool release_barrier = seq_cst_barrier || (order == std::memory_order_release);
  DCHECK(release_barrier || order == std::memory_order_relaxed);
  DCHECK(atomic || order == std::memory_order_relaxed);

  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  if (release_barrier) {
    codegen->GenerateMemoryBarrier(MemBarrierKind::kAnyStore);
  }
  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  if (kPoisonHeapReferences && type == DataType::Type::kReference) {
    vixl32::Register temp = temps.Acquire();
    __ Mov(temp, RegisterFrom(value));
    assembler->PoisonHeapReference(temp);
    value = LocationFrom(temp);
  }
  MemOperand address = offset.IsValid() ? MemOperand(base, offset) : MemOperand(base);
  if (offset.IsValid() && (DataType::Is64BitType(type) || type == DataType::Type::kFloat32)) {
    const vixl32::Register temp_reg = temps.Acquire();
    __ Add(temp_reg, base, offset);
    address = MemOperand(temp_reg);
  }
  switch (type) {
    case DataType::Type::kBool:
    case DataType::Type::kInt8:
      __ Strb(RegisterFrom(value), address);
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Strh(RegisterFrom(value), address);
      break;
    case DataType::Type::kReference:
    case DataType::Type::kInt32:
      __ Str(RegisterFrom(value), address);
      break;
    case DataType::Type::kInt64:
      if (Use64BitExclusiveLoadStore(atomic, codegen)) {
        vixl32::Register lo_tmp = RegisterFrom(maybe_temp);
        vixl32::Register hi_tmp = RegisterFrom(maybe_temp2);
        vixl32::Label loop;
        __ Bind(&loop);
        __ Ldrexd(lo_tmp, hi_tmp, address);  // Ignore the retrieved value.
        __ Strexd(lo_tmp, LowRegisterFrom(value), HighRegisterFrom(value), address);
        __ Cmp(lo_tmp, 0);
        __ B(ne, &loop);
      } else {
        __ Strd(LowRegisterFrom(value), HighRegisterFrom(value), address);
      }
      break;
    case DataType::Type::kFloat32:
      __ Vstr(SRegisterFrom(value), address);
      break;
    case DataType::Type::kFloat64:
      if (Use64BitExclusiveLoadStore(atomic, codegen)) {
        vixl32::Register lo_tmp = RegisterFrom(maybe_temp);
        vixl32::Register hi_tmp = RegisterFrom(maybe_temp2);
        vixl32::Register strexd_tmp = RegisterFrom(maybe_temp3);
        vixl32::Label loop;
        __ Bind(&loop);
        __ Ldrexd(lo_tmp, hi_tmp, address);  // Ignore the retrieved value.
        __ Vmov(lo_tmp, hi_tmp, DRegisterFrom(value));
        __ Strexd(strexd_tmp, lo_tmp, hi_tmp, address);
        __ Cmp(strexd_tmp, 0);
        __ B(ne, &loop);
      } else {
        __ Vstr(DRegisterFrom(value), address);
      }
      break;
    default:
      LOG(FATAL) << "Unexpected type " << type;
      UNREACHABLE();
  }
  if (seq_cst_barrier) {
    codegen->GenerateMemoryBarrier(MemBarrierKind::kAnyAny);
  }
}

static void CreateUnsafePutLocations(HInvoke* invoke,
                                     CodeGeneratorARMVIXL* codegen,
                                     DataType::Type type,
                                     bool atomic) {
  ArenaAllocator* allocator = invoke->GetBlock()->GetGraph()->GetAllocator();
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::NoLocation());        // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());

  if (type == DataType::Type::kInt64) {
    // Potentially need temps for ldrexd-strexd loop.
    if (Use64BitExclusiveLoadStore(atomic, codegen)) {
      locations->AddTemp(Location::RequiresRegister());  // Temp_lo.
      locations->AddTemp(Location::RequiresRegister());  // Temp_hi.
    }
  } else if (type == DataType::Type::kReference) {
    // Temp for card-marking.
    locations->AddTemp(Location::RequiresRegister());  // Temp.
  }
}

static void GenUnsafePut(HInvoke* invoke,
                         DataType::Type type,
                         std::memory_order order,
                         bool atomic,
                         CodeGeneratorARMVIXL* codegen) {
  ArmVIXLAssembler* assembler = codegen->GetAssembler();

  LocationSummary* locations = invoke->GetLocations();
  vixl32::Register base = RegisterFrom(locations->InAt(1));       // Object pointer.
  vixl32::Register offset = LowRegisterFrom(locations->InAt(2));  // Long offset, lo part only.
  Location value = locations->InAt(3);
  Location maybe_temp = Location::NoLocation();
  Location maybe_temp2 = Location::NoLocation();
  if (type == DataType::Type::kInt64 && Use64BitExclusiveLoadStore(atomic, codegen)) {
    maybe_temp = locations->GetTemp(0);
    maybe_temp2 = locations->GetTemp(1);
  }

  GenerateIntrinsicSet(codegen,
                       type,
                       order,
                       atomic,
                       base,
                       offset,
                       value,
                       maybe_temp,
                       maybe_temp2,
                       /*maybe_temp3=*/ Location::NoLocation());

  if (type == DataType::Type::kReference) {
    vixl32::Register temp = RegisterFrom(locations->GetTemp(0));
    UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
    vixl32::Register card = temps.Acquire();
    bool value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MarkGCCard(temp, card, base, RegisterFrom(value), value_can_be_null);
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafePut(HInvoke* invoke) {
  CreateUnsafePutLocations(invoke, codegen_, DataType::Type::kInt32, /*atomic=*/ false);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafePut(HInvoke* invoke) {
  GenUnsafePut(invoke,
               DataType::Type::kInt32,
               std::memory_order_relaxed,
               /*atomic=*/ false,
               codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafePutOrdered(HInvoke* invoke) {
  CreateUnsafePutLocations(invoke, codegen_, DataType::Type::kInt32, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafePutOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke,
               DataType::Type::kInt32,
               std::memory_order_release,
               /*atomic=*/ true,
               codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafePutVolatile(HInvoke* invoke) {
  CreateUnsafePutLocations(invoke, codegen_, DataType::Type::kInt32, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafePutVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke,
               DataType::Type::kInt32,
               std::memory_order_seq_cst,
               /*atomic=*/ true,
               codegen_);
}
void IntrinsicLocationsBuilderARMVIXL::VisitUnsafePutObject(HInvoke* invoke) {
  CreateUnsafePutLocations(invoke, codegen_, DataType::Type::kReference, /*atomic=*/ false);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafePutObject(HInvoke* invoke) {
  GenUnsafePut(invoke,
               DataType::Type::kReference,
               std::memory_order_relaxed,
               /*atomic=*/ false,
               codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafePutObjectOrdered(HInvoke* invoke) {
  CreateUnsafePutLocations(invoke, codegen_, DataType::Type::kReference, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafePutObjectOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke,
               DataType::Type::kReference,
               std::memory_order_release,
               /*atomic=*/ true,
               codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafePutObjectVolatile(HInvoke* invoke) {
  CreateUnsafePutLocations(invoke, codegen_, DataType::Type::kReference, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafePutObjectVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke,
               DataType::Type::kReference,
               std::memory_order_seq_cst,
               /*atomic=*/ true,
               codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafePutLong(HInvoke* invoke) {
  CreateUnsafePutLocations(invoke, codegen_, DataType::Type::kInt64, /*atomic=*/ false);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafePutLong(HInvoke* invoke) {
  GenUnsafePut(invoke,
               DataType::Type::kInt64,
               std::memory_order_relaxed,
               /*atomic=*/ false,
               codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafePutLongOrdered(HInvoke* invoke) {
  CreateUnsafePutLocations(invoke, codegen_, DataType::Type::kInt64, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafePutLongOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke,
               DataType::Type::kInt64,
               std::memory_order_release,
               /*atomic=*/ true,
               codegen_);
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafePutLongVolatile(HInvoke* invoke) {
  CreateUnsafePutLocations(invoke, codegen_, DataType::Type::kInt64, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitUnsafePutLongVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke,
               DataType::Type::kInt64,
               std::memory_order_seq_cst,
               /*atomic=*/ true,
               codegen_);
}

static void EmitLoadExclusive(CodeGeneratorARMVIXL* codegen,
                              DataType::Type type,
                              vixl32::Register ptr,
                              Location old_value) {
  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  switch (type) {
    case DataType::Type::kBool:
    case DataType::Type::kInt8:
      __ Ldrexb(RegisterFrom(old_value), MemOperand(ptr));
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Ldrexh(RegisterFrom(old_value), MemOperand(ptr));
      break;
    case DataType::Type::kInt32:
    case DataType::Type::kReference:
      __ Ldrex(RegisterFrom(old_value), MemOperand(ptr));
      break;
    case DataType::Type::kInt64:
      __ Ldrexd(LowRegisterFrom(old_value), HighRegisterFrom(old_value), MemOperand(ptr));
      break;
    default:
      LOG(FATAL) << "Unexpected type: " << type;
      UNREACHABLE();
  }
  switch (type) {
    case DataType::Type::kInt8:
      __ Sxtb(RegisterFrom(old_value), RegisterFrom(old_value));
      break;
    case DataType::Type::kInt16:
      __ Sxth(RegisterFrom(old_value), RegisterFrom(old_value));
      break;
    case DataType::Type::kReference:
      assembler->MaybeUnpoisonHeapReference(RegisterFrom(old_value));
      break;
    default:
      break;
  }
}

static void EmitStoreExclusive(CodeGeneratorARMVIXL* codegen,
                               DataType::Type type,
                               vixl32::Register ptr,
                               vixl32::Register store_result,
                               Location new_value) {
  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  if (type == DataType::Type::kReference) {
    assembler->MaybePoisonHeapReference(RegisterFrom(new_value));
  }
  switch (type) {
    case DataType::Type::kBool:
    case DataType::Type::kInt8:
      __ Strexb(store_result, RegisterFrom(new_value), MemOperand(ptr));
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Strexh(store_result, RegisterFrom(new_value), MemOperand(ptr));
      break;
    case DataType::Type::kInt32:
    case DataType::Type::kReference:
      __ Strex(store_result, RegisterFrom(new_value), MemOperand(ptr));
      break;
    case DataType::Type::kInt64:
      __ Strexd(
          store_result, LowRegisterFrom(new_value), HighRegisterFrom(new_value), MemOperand(ptr));
      break;
    default:
      LOG(FATAL) << "Unexpected type: " << type;
      UNREACHABLE();
  }
  if (type == DataType::Type::kReference) {
    assembler->MaybeUnpoisonHeapReference(RegisterFrom(new_value));
  }
}

static void GenerateCompareAndSet(CodeGeneratorARMVIXL* codegen,
                                  DataType::Type type,
                                  bool strong,
                                  vixl32::Label* cmp_failure,
                                  bool cmp_failure_is_far_target,
                                  vixl32::Register ptr,
                                  Location expected,
                                  Location new_value,
                                  Location old_value,
                                  vixl32::Register store_result,
                                  vixl32::Register success) {
  // For kReference, the `expected` shall be a register pair when called from a read barrier
  // slow path, specifying both the original `expected` as well as the unmarked old value from
  // the main path attempt to emit CAS when it matched `expected` after marking.
  // Otherwise the type of `expected` shall match the type of `new_value` and `old_value`.
  if (type == DataType::Type::kInt64) {
    DCHECK(expected.IsRegisterPair());
    DCHECK(new_value.IsRegisterPair());
    DCHECK(old_value.IsRegisterPair());
  } else {
    DCHECK(expected.IsRegister() ||
           (type == DataType::Type::kReference && expected.IsRegisterPair()));
    DCHECK(new_value.IsRegister());
    DCHECK(old_value.IsRegister());
  }

  ArmVIXLAssembler* assembler = codegen->GetAssembler();

  // do {
  //   old_value = [ptr];  // Load exclusive.
  //   if (old_value != expected) goto cmp_failure;
  //   store_result = failed([ptr] <- new_value);  // Store exclusive.
  // } while (strong && store_result);
  //
  // If `success` is a valid register, there are additional instructions in the above code
  // to report success with value 1 and failure with value 0 in that register.

  vixl32::Label loop_head;
  if (strong) {
    __ Bind(&loop_head);
  }
  EmitLoadExclusive(codegen, type, ptr, old_value);
  // We do not need to initialize the failure code for comparison failure if the
  // branch goes to the read barrier slow path that clobbers `success` anyway.
  bool init_failure_for_cmp =
      success.IsValid() &&
      !(kEmitCompilerReadBarrier && type == DataType::Type::kReference && expected.IsRegister());
  // Instruction scheduling: Loading a constant between LDREX* and using the loaded value
  // is essentially free, so prepare the failure value here if we can.
  bool init_failure_for_cmp_early =
      init_failure_for_cmp && !old_value.Contains(LocationFrom(success));
  if (init_failure_for_cmp_early) {
    __ Mov(success, 0);  // Indicate failure if the comparison fails.
  }
  if (type == DataType::Type::kInt64) {
    __ Cmp(LowRegisterFrom(old_value), LowRegisterFrom(expected));
    ExactAssemblyScope aas(assembler->GetVIXLAssembler(), 2 * k16BitT32InstructionSizeInBytes);
    __ it(eq);
    __ cmp(eq, HighRegisterFrom(old_value), HighRegisterFrom(expected));
  } else if (expected.IsRegisterPair()) {
    DCHECK_EQ(type, DataType::Type::kReference);
    // Check if the loaded value matches any of the two registers in `expected`.
    __ Cmp(RegisterFrom(old_value), LowRegisterFrom(expected));
    ExactAssemblyScope aas(assembler->GetVIXLAssembler(), 2 * k16BitT32InstructionSizeInBytes);
    __ it(ne);
    __ cmp(ne, RegisterFrom(old_value), HighRegisterFrom(expected));
  } else {
    __ Cmp(RegisterFrom(old_value), RegisterFrom(expected));
  }
  if (init_failure_for_cmp && !init_failure_for_cmp_early) {
    __ Mov(LeaveFlags, success, 0);  // Indicate failure if the comparison fails.
  }
  __ B(ne, cmp_failure, /*is_far_target=*/ cmp_failure_is_far_target);
  EmitStoreExclusive(codegen, type, ptr, store_result, new_value);
  if (strong) {
    // Instruction scheduling: Loading a constant between STREX* and using its result
    // is essentially free, so prepare the success value here if needed.
    if (success.IsValid()) {
      DCHECK(!success.Is(store_result));
      __ Mov(success, 1);  // Indicate success if the store succeeds.
    }
    __ Cmp(store_result, 0);
    __ B(ne, &loop_head, /*is_far_target=*/ false);
  } else {
    // Weak CAS (VarHandle.CompareAndExchange variants) always indicates success.
    DCHECK(success.IsValid());
    // Flip the `store_result` to indicate success by 1 and failure by 0.
    __ Eor(success, store_result, 1);
  }
}

class ReadBarrierCasSlowPathARMVIXL : public SlowPathCodeARMVIXL {
 public:
  explicit ReadBarrierCasSlowPathARMVIXL(HInvoke* invoke,
                                         bool strong,
                                         vixl32::Register base,
                                         vixl32::Register offset,
                                         vixl32::Register expected,
                                         vixl32::Register new_value,
                                         vixl32::Register old_value,
                                         vixl32::Register old_value_temp,
                                         vixl32::Register store_result,
                                         vixl32::Register success,
                                         CodeGeneratorARMVIXL* arm_codegen)
      : SlowPathCodeARMVIXL(invoke),
        strong_(strong),
        base_(base),
        offset_(offset),
        expected_(expected),
        new_value_(new_value),
        old_value_(old_value),
        old_value_temp_(old_value_temp),
        store_result_(store_result),
        success_(success),
        mark_old_value_slow_path_(nullptr),
        update_old_value_slow_path_(nullptr) {
    if (!kUseBakerReadBarrier) {
      // We need to add the slow path now, it is too late when emitting slow path code.
      mark_old_value_slow_path_ = arm_codegen->AddReadBarrierSlowPath(
          invoke,
          Location::RegisterLocation(old_value_temp.GetCode()),
          Location::RegisterLocation(old_value.GetCode()),
          Location::RegisterLocation(base.GetCode()),
          /*offset=*/ 0u,
          /*index=*/ Location::RegisterLocation(offset.GetCode()));
      if (!success.IsValid()) {
        update_old_value_slow_path_ = arm_codegen->AddReadBarrierSlowPath(
            invoke,
            Location::RegisterLocation(old_value.GetCode()),
            Location::RegisterLocation(old_value_temp.GetCode()),
            Location::RegisterLocation(base.GetCode()),
            /*offset=*/ 0u,
            /*index=*/ Location::RegisterLocation(offset.GetCode()));
      }
    }
  }

  const char* GetDescription() const override { return "ReadBarrierCasSlowPathARMVIXL"; }

  void EmitNativeCode(CodeGenerator* codegen) override {
    CodeGeneratorARMVIXL* arm_codegen = down_cast<CodeGeneratorARMVIXL*>(codegen);
    ArmVIXLAssembler* assembler = arm_codegen->GetAssembler();
    __ Bind(GetEntryLabel());

    // Mark the `old_value_` from the main path and compare with `expected_`.
    if (kUseBakerReadBarrier) {
      DCHECK(mark_old_value_slow_path_ == nullptr);
      arm_codegen->GenerateIntrinsicCasMoveWithBakerReadBarrier(old_value_temp_, old_value_);
    } else {
      DCHECK(mark_old_value_slow_path_ != nullptr);
      __ B(mark_old_value_slow_path_->GetEntryLabel());
      __ Bind(mark_old_value_slow_path_->GetExitLabel());
    }
    __ Cmp(old_value_temp_, expected_);
    if (success_.IsValid()) {
      __ Mov(LeaveFlags, success_, 0);  // Indicate failure if we take the branch out.
    } else {
      // In case of failure, update the `old_value_` with the marked reference.
      ExactAssemblyScope aas(assembler->GetVIXLAssembler(), 2 * k16BitT32InstructionSizeInBytes);
      __ it(ne);
      __ mov(ne, old_value_, old_value_temp_);
    }
    __ B(ne, GetExitLabel());

    // The old value we have read did not match `expected` (which is always a to-space
    // reference) but after the read barrier the marked to-space value matched, so the
    // old value must be a from-space reference to the same object. Do the same CAS loop
    // as the main path but check for both `expected` and the unmarked old value
    // representing the to-space and from-space references for the same object.

    UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
    vixl32::Register tmp_ptr = temps.Acquire();

    // Recalculate the `tmp_ptr` clobbered above.
    __ Add(tmp_ptr, base_, offset_);

    vixl32::Label mark_old_value;
    GenerateCompareAndSet(arm_codegen,
                          DataType::Type::kReference,
                          strong_,
                          /*cmp_failure=*/ success_.IsValid() ? GetExitLabel() : &mark_old_value,
                          /*cmp_failure_is_far_target=*/ success_.IsValid(),
                          tmp_ptr,
                          /*expected=*/ LocationFrom(expected_, old_value_),
                          /*new_value=*/ LocationFrom(new_value_),
                          /*old_value=*/ LocationFrom(old_value_temp_),
                          store_result_,
                          success_);
    if (!success_.IsValid()) {
      // To reach this point, the `old_value_temp_` must be either a from-space or a to-space
      // reference of the `expected_` object. Update the `old_value_` to the to-space reference.
      __ Mov(old_value_, expected_);
    }

    __ B(GetExitLabel());

    if (!success_.IsValid()) {
      __ Bind(&mark_old_value);
      if (kUseBakerReadBarrier) {
        DCHECK(update_old_value_slow_path_ == nullptr);
        arm_codegen->GenerateIntrinsicCasMoveWithBakerReadBarrier(old_value_, old_value_temp_);
      } else {
        // Note: We could redirect the `failure` above directly to the entry label and bind
        // the exit label in the main path, but the main path would need to access the
        // `update_old_value_slow_path_`. To keep the code simple, keep the extra jumps.
        DCHECK(update_old_value_slow_path_ != nullptr);
        __ B(update_old_value_slow_path_->GetEntryLabel());
        __ Bind(update_old_value_slow_path_->GetExitLabel());
      }
      __ B(GetExitLabel());
    }
  }

 private:
  bool strong_;
  vixl32::Register base_;
  vixl32::Register offset_;
  vixl32::Register expected_;
  vixl32::Register new_value_;
  vixl32::Register old_value_;
  vixl32::Register old_value_temp_;
  vixl32::Register store_result_;
  vixl32::Register success_;
  SlowPathCodeARMVIXL* mark_old_value_slow_path_;
  SlowPathCodeARMVIXL* update_old_value_slow_path_;
};

static void CreateUnsafeCASLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  bool can_call = kEmitCompilerReadBarrier &&
      (invoke->GetIntrinsic() == Intrinsics::kUnsafeCASObject);
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke,
                                      can_call
                                          ? LocationSummary::kCallOnSlowPath
                                          : LocationSummary::kNoCall,
                                      kIntrinsified);
  if (can_call && kUseBakerReadBarrier) {
    locations->SetCustomSlowPathCallerSaves(RegisterSet::Empty());  // No caller-save registers.
  }
  locations->SetInAt(0, Location::NoLocation());        // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->SetInAt(4, Location::RequiresRegister());

  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);

  // Temporary register used in CAS. In the object case (UnsafeCASObject intrinsic),
  // this is also used for card-marking, and possibly for read barrier.
  locations->AddTemp(Location::RequiresRegister());
}

static void GenUnsafeCas(HInvoke* invoke, DataType::Type type, CodeGeneratorARMVIXL* codegen) {
  DCHECK_NE(type, DataType::Type::kInt64);

  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  vixl32::Register out = OutputRegister(invoke);                      // Boolean result.
  vixl32::Register base = InputRegisterAt(invoke, 1);                 // Object pointer.
  vixl32::Register offset = LowRegisterFrom(locations->InAt(2));      // Offset (discard high 4B).
  vixl32::Register expected = InputRegisterAt(invoke, 3);             // Expected.
  vixl32::Register new_value = InputRegisterAt(invoke, 4);            // New value.

  vixl32::Register tmp = RegisterFrom(locations->GetTemp(0));         // Temporary.

  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  vixl32::Register tmp_ptr = temps.Acquire();

  if (type == DataType::Type::kReference) {
    // Mark card for object assuming new value is stored. Worst case we will mark an unchanged
    // object and scan the receiver at the next GC for nothing.
    bool value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MarkGCCard(tmp_ptr, tmp, base, new_value, value_can_be_null);
  }

  vixl32::Label exit_loop_label;
  vixl32::Label* exit_loop = &exit_loop_label;
  vixl32::Label* cmp_failure = &exit_loop_label;

  if (kEmitCompilerReadBarrier && type == DataType::Type::kReference) {
    // If marking, check if the stored reference is a from-space reference to the same
    // object as the to-space reference `expected`. If so, perform a custom CAS loop.
    ReadBarrierCasSlowPathARMVIXL* slow_path =
        new (codegen->GetScopedAllocator()) ReadBarrierCasSlowPathARMVIXL(
            invoke,
            /*strong=*/ true,
            base,
            offset,
            expected,
            new_value,
            /*old_value=*/ tmp,
            /*old_value_temp=*/ out,
            /*store_result=*/ tmp,
            /*success=*/ out,
            codegen);
    codegen->AddSlowPath(slow_path);
    exit_loop = slow_path->GetExitLabel();
    cmp_failure = slow_path->GetEntryLabel();
  }

  // Unsafe CAS operations have std::memory_order_seq_cst semantics.
  codegen->GenerateMemoryBarrier(MemBarrierKind::kAnyAny);
  __ Add(tmp_ptr, base, offset);
  GenerateCompareAndSet(codegen,
                        type,
                        /*strong=*/ true,
                        cmp_failure,
                        /*cmp_failure_is_far_target=*/ cmp_failure != &exit_loop_label,
                        tmp_ptr,
                        /*expected=*/ LocationFrom(expected),  // TODO: Int64
                        /*new_value=*/ LocationFrom(new_value),  // TODO: Int64
                        /*old_value=*/ LocationFrom(tmp),  // TODO: Int64
                        /*store_result=*/ tmp,
                        /*success=*/ out);
  __ Bind(exit_loop);
  codegen->GenerateMemoryBarrier(MemBarrierKind::kAnyAny);

  if (type == DataType::Type::kReference) {
    codegen->MaybeGenerateMarkingRegisterCheck(/*code=*/ 128, /*temp_loc=*/ LocationFrom(tmp_ptr));
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitUnsafeCASInt(HInvoke* invoke) {
  CreateUnsafeCASLocations(allocator_, invoke);
}
void IntrinsicLocationsBuilderARMVIXL::VisitUnsafeCASObject(HInvoke* invoke) {
  // The only read barrier implementation supporting the
  // UnsafeCASObject intrinsic is the Baker-style read barriers. b/173104084
  if (kEmitCompilerReadBarrier && !kUseBakerReadBarrier) {
    return;
  }

  CreateUnsafeCASLocations(allocator_, invoke);
}
void IntrinsicCodeGeneratorARMVIXL::VisitUnsafeCASInt(HInvoke* invoke) {
  GenUnsafeCas(invoke, DataType::Type::kInt32, codegen_);
}
void IntrinsicCodeGeneratorARMVIXL::VisitUnsafeCASObject(HInvoke* invoke) {
  GenUnsafeCas(invoke, DataType::Type::kReference, codegen_);
}

enum class GetAndUpdateOp {
  kSet,
  kAdd,
  kAddWithByteSwap,
  kAnd,
  kOr,
  kXor
};

static void GenerateGetAndUpdate(CodeGeneratorARMVIXL* codegen,
                                 GetAndUpdateOp get_and_update_op,
                                 DataType::Type load_store_type,
                                 vixl32::Register ptr,
                                 Location arg,
                                 Location old_value,
                                 vixl32::Register store_result,
                                 Location maybe_temp,
                                 Location maybe_vreg_temp) {
  ArmVIXLAssembler* assembler = codegen->GetAssembler();

  Location loaded_value;
  Location new_value;
  switch (get_and_update_op) {
    case GetAndUpdateOp::kSet:
      loaded_value = old_value;
      new_value = arg;
      break;
    case GetAndUpdateOp::kAddWithByteSwap:
      if (old_value.IsRegisterPair()) {
        // To avoid register overlap when reversing bytes, load into temps.
        DCHECK(maybe_temp.IsRegisterPair());
        loaded_value = maybe_temp;
        new_value = loaded_value;  // Use the same temporaries for the new value.
        break;
      }
      FALLTHROUGH_INTENDED;
    case GetAndUpdateOp::kAdd:
      if (old_value.IsFpuRegisterPair()) {
        DCHECK(maybe_temp.IsRegisterPair());
        loaded_value = maybe_temp;
        new_value = loaded_value;  // Use the same temporaries for the new value.
        break;
      }
      if (old_value.IsFpuRegister()) {
        DCHECK(maybe_temp.IsRegister());
        loaded_value = maybe_temp;
        new_value = loaded_value;  // Use the same temporary for the new value.
        break;
      }
      FALLTHROUGH_INTENDED;
    case GetAndUpdateOp::kAnd:
    case GetAndUpdateOp::kOr:
    case GetAndUpdateOp::kXor:
      loaded_value = old_value;
      new_value = maybe_temp;
      break;
  }

  vixl32::Label loop_label;
  __ Bind(&loop_label);
  EmitLoadExclusive(codegen, load_store_type, ptr, loaded_value);
  switch (get_and_update_op) {
    case GetAndUpdateOp::kSet:
      break;
    case GetAndUpdateOp::kAddWithByteSwap:
      if (arg.IsFpuRegisterPair()) {
        GenerateReverseBytes(assembler, DataType::Type::kFloat64, loaded_value, old_value);
        vixl32::DRegister sum = DRegisterFrom(maybe_vreg_temp);
        __ Vadd(sum, DRegisterFrom(old_value), DRegisterFrom(arg));
        __ Vmov(HighRegisterFrom(new_value), LowRegisterFrom(new_value), sum);  // Swap low/high.
      } else if (arg.IsFpuRegister()) {
        GenerateReverseBytes(assembler, DataType::Type::kFloat32, loaded_value, old_value);
        vixl32::SRegister sum = LowSRegisterFrom(maybe_vreg_temp);  // The temporary is a pair.
        __ Vadd(sum, SRegisterFrom(old_value), SRegisterFrom(arg));
        __ Vmov(RegisterFrom(new_value), sum);
      } else if (load_store_type == DataType::Type::kInt64) {
        GenerateReverseBytes(assembler, DataType::Type::kInt64, loaded_value, old_value);
        // Swap low/high registers for the addition results.
        __ Adds(HighRegisterFrom(new_value), LowRegisterFrom(old_value), LowRegisterFrom(arg));
        __ Adc(LowRegisterFrom(new_value), HighRegisterFrom(old_value), HighRegisterFrom(arg));
      } else {
        GenerateReverseBytes(assembler, DataType::Type::kInt32, loaded_value, old_value);
        __ Add(RegisterFrom(new_value), RegisterFrom(old_value), RegisterFrom(arg));
      }
      if (load_store_type == DataType::Type::kInt64) {
        // The `new_value` already has the high and low word swapped. Reverse bytes in each.
        GenerateReverseBytesInPlaceForEachWord(assembler, new_value);
      } else {
        GenerateReverseBytes(assembler, load_store_type, new_value, new_value);
      }
      break;
    case GetAndUpdateOp::kAdd:
      if (arg.IsFpuRegisterPair()) {
        vixl32::DRegister old_value_vreg = DRegisterFrom(old_value);
        vixl32::DRegister sum = DRegisterFrom(maybe_vreg_temp);
        __ Vmov(old_value_vreg, LowRegisterFrom(loaded_value), HighRegisterFrom(loaded_value));
        __ Vadd(sum, old_value_vreg, DRegisterFrom(arg));
        __ Vmov(LowRegisterFrom(new_value), HighRegisterFrom(new_value), sum);
      } else if (arg.IsFpuRegister()) {
        vixl32::SRegister old_value_vreg = SRegisterFrom(old_value);
        vixl32::SRegister sum = LowSRegisterFrom(maybe_vreg_temp);  // The temporary is a pair.
        __ Vmov(old_value_vreg, RegisterFrom(loaded_value));
        __ Vadd(sum, old_value_vreg, SRegisterFrom(arg));
        __ Vmov(RegisterFrom(new_value), sum);
      } else if (load_store_type == DataType::Type::kInt64) {
        __ Adds(LowRegisterFrom(new_value), LowRegisterFrom(loaded_value), LowRegisterFrom(arg));
        __ Adc(HighRegisterFrom(new_value), HighRegisterFrom(loaded_value), HighRegisterFrom(arg));
      } else {
        __ Add(RegisterFrom(new_value), RegisterFrom(loaded_value), RegisterFrom(arg));
      }
      break;
    case GetAndUpdateOp::kAnd:
      if (load_store_type == DataType::Type::kInt64) {
        __ And(LowRegisterFrom(new_value), LowRegisterFrom(loaded_value), LowRegisterFrom(arg));
        __ And(HighRegisterFrom(new_value), HighRegisterFrom(loaded_value), HighRegisterFrom(arg));
      } else {
        __ And(RegisterFrom(new_value), RegisterFrom(loaded_value), RegisterFrom(arg));
      }
      break;
    case GetAndUpdateOp::kOr:
      if (load_store_type == DataType::Type::kInt64) {
        __ Orr(LowRegisterFrom(new_value), LowRegisterFrom(loaded_value), LowRegisterFrom(arg));
        __ Orr(HighRegisterFrom(new_value), HighRegisterFrom(loaded_value), HighRegisterFrom(arg));
      } else {
        __ Orr(RegisterFrom(new_value), RegisterFrom(loaded_value), RegisterFrom(arg));
      }
      break;
    case GetAndUpdateOp::kXor:
      if (load_store_type == DataType::Type::kInt64) {
        __ Eor(LowRegisterFrom(new_value), LowRegisterFrom(loaded_value), LowRegisterFrom(arg));
        __ Eor(HighRegisterFrom(new_value), HighRegisterFrom(loaded_value), HighRegisterFrom(arg));
      } else {
        __ Eor(RegisterFrom(new_value), RegisterFrom(loaded_value), RegisterFrom(arg));
      }
      break;
  }
  EmitStoreExclusive(codegen, load_store_type, ptr, store_result, new_value);
  __ Cmp(store_result, 0);
  __ B(ne, &loop_label);
}

class VarHandleSlowPathARMVIXL : public IntrinsicSlowPathARMVIXL {
 public:
  VarHandleSlowPathARMVIXL(HInvoke* invoke, std::memory_order order)
      : IntrinsicSlowPathARMVIXL(invoke),
        order_(order),
        atomic_(false),
        return_success_(false),
        strong_(false),
        get_and_update_op_(GetAndUpdateOp::kAdd) {
  }

  vixl32::Label* GetByteArrayViewCheckLabel() {
    return &byte_array_view_check_label_;
  }

  vixl32::Label* GetNativeByteOrderLabel() {
    return &native_byte_order_label_;
  }

  void SetAtomic(bool atomic) {
    DCHECK(GetAccessModeTemplate() == mirror::VarHandle::AccessModeTemplate::kGet ||
           GetAccessModeTemplate() == mirror::VarHandle::AccessModeTemplate::kSet);
    atomic_ = atomic;
  }

  void SetCompareAndSetOrExchangeArgs(bool return_success, bool strong) {
    if (return_success) {
      DCHECK(GetAccessModeTemplate() == mirror::VarHandle::AccessModeTemplate::kCompareAndSet);
    } else {
      DCHECK(GetAccessModeTemplate() == mirror::VarHandle::AccessModeTemplate::kCompareAndExchange);
    }
    return_success_ = return_success;
    strong_ = strong;
  }

  void SetGetAndUpdateOp(GetAndUpdateOp get_and_update_op) {
    DCHECK(GetAccessModeTemplate() == mirror::VarHandle::AccessModeTemplate::kGetAndUpdate);
    get_and_update_op_ = get_and_update_op;
  }

  void EmitNativeCode(CodeGenerator* codegen_in) override {
    if (GetByteArrayViewCheckLabel()->IsReferenced()) {
      EmitByteArrayViewCode(codegen_in);
    }
    IntrinsicSlowPathARMVIXL::EmitNativeCode(codegen_in);
  }

 private:
  HInvoke* GetInvoke() const {
    return GetInstruction()->AsInvoke();
  }

  mirror::VarHandle::AccessModeTemplate GetAccessModeTemplate() const {
    return mirror::VarHandle::GetAccessModeTemplateByIntrinsic(GetInvoke()->GetIntrinsic());
  }

  void EmitByteArrayViewCode(CodeGenerator* codegen_in);

  vixl32::Label byte_array_view_check_label_;
  vixl32::Label native_byte_order_label_;
  // Shared parameter for all VarHandle intrinsics.
  std::memory_order order_;
  // Extra argument for GenerateVarHandleGet() and GenerateVarHandleSet().
  bool atomic_;
  // Extra arguments for GenerateVarHandleCompareAndSetOrExchange().
  bool return_success_;
  bool strong_;
  // Extra argument for GenerateVarHandleGetAndUpdate().
  GetAndUpdateOp get_and_update_op_;
};

// Generate subtype check without read barriers.
static void GenerateSubTypeObjectCheckNoReadBarrier(CodeGeneratorARMVIXL* codegen,
                                                    SlowPathCodeARMVIXL* slow_path,
                                                    vixl32::Register object,
                                                    vixl32::Register type,
                                                    bool object_can_be_null = true) {
  ArmVIXLAssembler* assembler = codegen->GetAssembler();

  const MemberOffset class_offset = mirror::Object::ClassOffset();
  const MemberOffset super_class_offset = mirror::Class::SuperClassOffset();

  vixl32::Label success;
  if (object_can_be_null) {
    __ CompareAndBranchIfZero(object, &success, /*is_far_target=*/ false);
  }

  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  vixl32::Register temp = temps.Acquire();

  __ Ldr(temp, MemOperand(object, class_offset.Int32Value()));
  assembler->MaybeUnpoisonHeapReference(temp);
  vixl32::Label loop;
  __ Bind(&loop);
  __ Cmp(type, temp);
  __ B(eq, &success, /*is_far_target=*/ false);
  __ Ldr(temp, MemOperand(temp, super_class_offset.Int32Value()));
  assembler->MaybeUnpoisonHeapReference(temp);
  __ Cmp(temp, 0);
  __ B(eq, slow_path->GetEntryLabel());
  __ B(&loop);
  __ Bind(&success);
}

// Check access mode and the primitive type from VarHandle.varType.
// Check reference arguments against the VarHandle.varType; for references this is a subclass
// check without read barrier, so it can have false negatives which we handle in the slow path.
static void GenerateVarHandleAccessModeAndVarTypeChecks(HInvoke* invoke,
                                                        CodeGeneratorARMVIXL* codegen,
                                                        SlowPathCodeARMVIXL* slow_path,
                                                        DataType::Type type) {
  mirror::VarHandle::AccessMode access_mode =
      mirror::VarHandle::GetAccessModeByIntrinsic(invoke->GetIntrinsic());
  Primitive::Type primitive_type = DataTypeToPrimitive(type);

  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  vixl32::Register varhandle = InputRegisterAt(invoke, 0);

  const MemberOffset var_type_offset = mirror::VarHandle::VarTypeOffset();
  const MemberOffset access_mode_bit_mask_offset = mirror::VarHandle::AccessModesBitMaskOffset();
  const MemberOffset primitive_type_offset = mirror::Class::PrimitiveTypeOffset();

  // Use the temporary register reserved for offset. It is not used yet at this point.
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  vixl32::Register var_type_no_rb =
      RegisterFrom(invoke->GetLocations()->GetTemp(expected_coordinates_count == 0u ? 1u : 0u));

  // Check that the operation is permitted and the primitive type of varhandle.varType.
  // We do not need a read barrier when loading a reference only for loading constant
  // primitive field through the reference. Use LDRD to load the fields together.
  {
    UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
    vixl32::Register temp2 = temps.Acquire();
    DCHECK_EQ(var_type_offset.Int32Value() + 4, access_mode_bit_mask_offset.Int32Value());
    __ Ldrd(var_type_no_rb, temp2, MemOperand(varhandle, var_type_offset.Int32Value()));
    assembler->MaybeUnpoisonHeapReference(var_type_no_rb);
    __ Tst(temp2, 1u << static_cast<uint32_t>(access_mode));
    __ B(eq, slow_path->GetEntryLabel());
    __ Ldrh(temp2, MemOperand(var_type_no_rb, primitive_type_offset.Int32Value()));
    __ Cmp(temp2, static_cast<uint16_t>(primitive_type));
    __ B(ne, slow_path->GetEntryLabel());
  }

  if (type == DataType::Type::kReference) {
    // Check reference arguments against the varType.
    // False negatives due to varType being an interface or array type
    // or due to the missing read barrier are handled by the slow path.
    uint32_t arguments_start = /* VarHandle object */ 1u + expected_coordinates_count;
    uint32_t number_of_arguments = invoke->GetNumberOfArguments();
    for (size_t arg_index = arguments_start; arg_index != number_of_arguments; ++arg_index) {
      HInstruction* arg = invoke->InputAt(arg_index);
      DCHECK_EQ(arg->GetType(), DataType::Type::kReference);
      if (!arg->IsNullConstant()) {
        vixl32::Register arg_reg = RegisterFrom(invoke->GetLocations()->InAt(arg_index));
        GenerateSubTypeObjectCheckNoReadBarrier(codegen, slow_path, arg_reg, var_type_no_rb);
      }
    }
  }
}

static void GenerateVarHandleStaticFieldCheck(HInvoke* invoke,
                                              CodeGeneratorARMVIXL* codegen,
                                              SlowPathCodeARMVIXL* slow_path) {
  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  vixl32::Register varhandle = InputRegisterAt(invoke, 0);

  const MemberOffset coordinate_type0_offset = mirror::VarHandle::CoordinateType0Offset();

  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  vixl32::Register temp = temps.Acquire();

  // Check that the VarHandle references a static field by checking that coordinateType0 == null.
  // Do not emit read barrier (or unpoison the reference) for comparing to null.
  __ Ldr(temp, MemOperand(varhandle, coordinate_type0_offset.Int32Value()));
  __ Cmp(temp, 0);
  __ B(ne, slow_path->GetEntryLabel());
}

static void GenerateVarHandleInstanceFieldChecks(HInvoke* invoke,
                                                 CodeGeneratorARMVIXL* codegen,
                                                 SlowPathCodeARMVIXL* slow_path) {
  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  vixl32::Register varhandle = InputRegisterAt(invoke, 0);
  vixl32::Register object = InputRegisterAt(invoke, 1);

  const MemberOffset coordinate_type0_offset = mirror::VarHandle::CoordinateType0Offset();
  const MemberOffset coordinate_type1_offset = mirror::VarHandle::CoordinateType1Offset();

  // Null-check the object.
  __ Cmp(object, 0);
  __ B(eq, slow_path->GetEntryLabel());

  // Use the first temporary register, whether it's for the declaring class or the offset.
  // It is not used yet at this point.
  vixl32::Register temp = RegisterFrom(invoke->GetLocations()->GetTemp(0u));

  // Check that the VarHandle references an instance field by checking that
  // coordinateType1 == null. coordinateType0 should not be null, but this is handled by the
  // type compatibility check with the source object's type, which will fail for null.
  {
    UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
    vixl32::Register temp2 = temps.Acquire();
    DCHECK_EQ(coordinate_type0_offset.Int32Value() + 4, coordinate_type1_offset.Int32Value());
    __ Ldrd(temp, temp2, MemOperand(varhandle, coordinate_type0_offset.Int32Value()));
    assembler->MaybeUnpoisonHeapReference(temp);
    // No need for read barrier or unpoisoning of coordinateType1 for comparison with null.
    __ Cmp(temp2, 0);
    __ B(ne, slow_path->GetEntryLabel());
  }

  // Check that the object has the correct type.
  // We deliberately avoid the read barrier, letting the slow path handle the false negatives.
  GenerateSubTypeObjectCheckNoReadBarrier(
      codegen, slow_path, object, temp, /*object_can_be_null=*/ false);
}

static DataType::Type GetVarHandleExpectedValueType(HInvoke* invoke,
                                                    size_t expected_coordinates_count) {
  DCHECK_EQ(expected_coordinates_count, GetExpectedVarHandleCoordinatesCount(invoke));
  uint32_t number_of_arguments = invoke->GetNumberOfArguments();
  DCHECK_GE(number_of_arguments, /* VarHandle object */ 1u + expected_coordinates_count);
  if (number_of_arguments == /* VarHandle object */ 1u + expected_coordinates_count) {
    return invoke->GetType();
  } else {
    return GetDataTypeFromShorty(invoke, number_of_arguments - 1u);
  }
}

static void GenerateVarHandleArrayChecks(HInvoke* invoke,
                                         CodeGeneratorARMVIXL* codegen,
                                         VarHandleSlowPathARMVIXL* slow_path) {
  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  vixl32::Register varhandle = InputRegisterAt(invoke, 0);
  vixl32::Register object = InputRegisterAt(invoke, 1);
  vixl32::Register index = InputRegisterAt(invoke, 2);
  DataType::Type value_type =
      GetVarHandleExpectedValueType(invoke, /*expected_coordinates_count=*/ 2u);
  Primitive::Type primitive_type = DataTypeToPrimitive(value_type);

  const MemberOffset coordinate_type0_offset = mirror::VarHandle::CoordinateType0Offset();
  const MemberOffset coordinate_type1_offset = mirror::VarHandle::CoordinateType1Offset();
  const MemberOffset component_type_offset = mirror::Class::ComponentTypeOffset();
  const MemberOffset primitive_type_offset = mirror::Class::PrimitiveTypeOffset();
  const MemberOffset class_offset = mirror::Object::ClassOffset();
  const MemberOffset array_length_offset = mirror::Array::LengthOffset();

  // Null-check the object.
  __ Cmp(object, 0);
  __ B(eq, slow_path->GetEntryLabel());

  // Use the offset temporary register. It is not used yet at this point.
  vixl32::Register temp = RegisterFrom(invoke->GetLocations()->GetTemp(0u));

  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  vixl32::Register temp2 = temps.Acquire();

  // Check that the VarHandle references an array, byte array view or ByteBuffer by checking
  // that coordinateType1 != null. If that's true, coordinateType1 shall be int.class and
  // coordinateType0 shall not be null but we do not explicitly verify that.
  DCHECK_EQ(coordinate_type0_offset.Int32Value() + 4, coordinate_type1_offset.Int32Value());
  __ Ldrd(temp, temp2, MemOperand(varhandle, coordinate_type0_offset.Int32Value()));
  codegen->GetAssembler()->MaybeUnpoisonHeapReference(temp);
  // No need for read barrier or unpoisoning of coordinateType1 for comparison with null.
  __ Cmp(temp2, 0);
  __ B(eq, slow_path->GetEntryLabel());

  // Check object class against componentType0.
  //
  // This is an exact check and we defer other cases to the runtime. This includes
  // conversion to array of superclass references, which is valid but subsequently
  // requires all update operations to check that the value can indeed be stored.
  // We do not want to perform such extra checks in the intrinsified code.
  //
  // We do this check without read barrier, so there can be false negatives which we
  // defer to the slow path. There shall be no false negatives for array classes in the
  // boot image (including Object[] and primitive arrays) because they are non-movable.
  __ Ldr(temp2, MemOperand(object, class_offset.Int32Value()));
  codegen->GetAssembler()->MaybeUnpoisonHeapReference(temp2);
  __ Cmp(temp, temp2);
  __ B(ne, slow_path->GetEntryLabel());

  // Check that the coordinateType0 is an array type. We do not need a read barrier
  // for loading constant reference fields (or chains of them) for comparison with null,
  // nor for finally loading a constant primitive field (primitive type) below.
  __ Ldr(temp2, MemOperand(temp, component_type_offset.Int32Value()));
  codegen->GetAssembler()->MaybeUnpoisonHeapReference(temp2);
  __ Cmp(temp2, 0);
  __ B(eq, slow_path->GetEntryLabel());

  // Check that the array component type matches the primitive type.
  // With the exception of `kPrimNot`, `kPrimByte` and `kPrimBoolean`,
  // we shall check for a byte array view in the slow path.
  // The check requires the ByteArrayViewVarHandle.class to be in the boot image,
  // so we cannot emit that if we're JITting without boot image.
  bool boot_image_available =
      codegen->GetCompilerOptions().IsBootImage() ||
      !Runtime::Current()->GetHeap()->GetBootImageSpaces().empty();
  DCHECK(boot_image_available || codegen->GetCompilerOptions().IsJitCompiler());
  size_t can_be_view =
      ((value_type != DataType::Type::kReference) && (DataType::Size(value_type) != 1u)) &&
      boot_image_available;
  vixl32::Label* slow_path_label =
      can_be_view ? slow_path->GetByteArrayViewCheckLabel() : slow_path->GetEntryLabel();
  __ Ldrh(temp2, MemOperand(temp2, primitive_type_offset.Int32Value()));
  __ Cmp(temp2, static_cast<uint16_t>(primitive_type));
  __ B(ne, slow_path_label);

  // Check for array index out of bounds.
  __ Ldr(temp, MemOperand(object, array_length_offset.Int32Value()));
  __ Cmp(index, temp);
  __ B(hs, slow_path->GetEntryLabel());
}

static void GenerateVarHandleCoordinateChecks(HInvoke* invoke,
                                              CodeGeneratorARMVIXL* codegen,
                                              VarHandleSlowPathARMVIXL* slow_path) {
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  if (expected_coordinates_count == 0u) {
    GenerateVarHandleStaticFieldCheck(invoke, codegen, slow_path);
  } else if (expected_coordinates_count == 1u) {
    GenerateVarHandleInstanceFieldChecks(invoke, codegen, slow_path);
  } else {
    DCHECK_EQ(expected_coordinates_count, 2u);
    GenerateVarHandleArrayChecks(invoke, codegen, slow_path);
  }
}

static VarHandleSlowPathARMVIXL* GenerateVarHandleChecks(HInvoke* invoke,
                                                         CodeGeneratorARMVIXL* codegen,
                                                         std::memory_order order,
                                                         DataType::Type type) {
  VarHandleSlowPathARMVIXL* slow_path =
      new (codegen->GetScopedAllocator()) VarHandleSlowPathARMVIXL(invoke, order);
  codegen->AddSlowPath(slow_path);

  GenerateVarHandleAccessModeAndVarTypeChecks(invoke, codegen, slow_path, type);
  GenerateVarHandleCoordinateChecks(invoke, codegen, slow_path);

  return slow_path;
}

struct VarHandleTarget {
  vixl32::Register object;  // The object holding the value to operate on.
  vixl32::Register offset;  // The offset of the value to operate on.
};

static VarHandleTarget GetVarHandleTarget(HInvoke* invoke) {
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  LocationSummary* locations = invoke->GetLocations();

  VarHandleTarget target;
  // The temporary allocated for loading the offset.
  target.offset = RegisterFrom(locations->GetTemp(0u));
  // The reference to the object that holds the value to operate on.
  target.object = (expected_coordinates_count == 0u)
      ? RegisterFrom(locations->GetTemp(1u))
      : InputRegisterAt(invoke, 1);
  return target;
}

static void GenerateVarHandleTarget(HInvoke* invoke,
                                    const VarHandleTarget& target,
                                    CodeGeneratorARMVIXL* codegen) {
  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  vixl32::Register varhandle = InputRegisterAt(invoke, 0);
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);

  if (expected_coordinates_count <= 1u) {
    // For static fields, we need to fill the `target.object` with the declaring class,
    // so we can use `target.object` as temporary for the `ArtMethod*`. For instance fields,
    // we do not need the declaring class, so we can forget the `ArtMethod*` when
    // we load the `target.offset`, so use the `target.offset` to hold the `ArtMethod*`.
    vixl32::Register method = (expected_coordinates_count == 0) ? target.object : target.offset;

    const MemberOffset art_field_offset = mirror::FieldVarHandle::ArtFieldOffset();
    const MemberOffset offset_offset = ArtField::OffsetOffset();

    // Load the ArtField, the offset and, if needed, declaring class.
    __ Ldr(method, MemOperand(varhandle, art_field_offset.Int32Value()));
    __ Ldr(target.offset, MemOperand(method, offset_offset.Int32Value()));
    if (expected_coordinates_count == 0u) {
      codegen->GenerateGcRootFieldLoad(invoke,
                                       LocationFrom(target.object),
                                       method,
                                       ArtField::DeclaringClassOffset().Int32Value(),
                                       kCompilerReadBarrierOption);
    }
  } else {
    DCHECK_EQ(expected_coordinates_count, 2u);
    DataType::Type value_type =
        GetVarHandleExpectedValueType(invoke, /*expected_coordinates_count=*/ 2u);
    uint32_t size_shift = DataType::SizeShift(value_type);
    MemberOffset data_offset = mirror::Array::DataOffset(DataType::Size(value_type));

    vixl32::Register index = InputRegisterAt(invoke, 2);
    vixl32::Register shifted_index = index;
    if (size_shift != 0u) {
      shifted_index = target.offset;
      __ Lsl(shifted_index, index, size_shift);
    }
    __ Add(target.offset, shifted_index, data_offset.Int32Value());
  }
}

static bool HasVarHandleIntrinsicImplementation(HInvoke* invoke) {
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  if (expected_coordinates_count > 2u) {
    // Invalid coordinate count. This invoke shall throw at runtime.
    return false;
  }
  if (expected_coordinates_count != 0u &&
      invoke->InputAt(1)->GetType() != DataType::Type::kReference) {
    // Except for static fields (no coordinates), the first coordinate must be a reference.
    return false;
  }
  if (expected_coordinates_count == 2u) {
    // For arrays and views, the second coordinate must be convertible to `int`.
    // In this context, `boolean` is not convertible but we have to look at the shorty
    // as compiler transformations can give the invoke a valid boolean input.
    DataType::Type index_type = GetDataTypeFromShorty(invoke, 2);
    if (index_type == DataType::Type::kBool ||
        DataType::Kind(index_type) != DataType::Type::kInt32) {
      return false;
    }
  }

  uint32_t number_of_arguments = invoke->GetNumberOfArguments();
  DataType::Type return_type = invoke->GetType();
  mirror::VarHandle::AccessModeTemplate access_mode_template =
      mirror::VarHandle::GetAccessModeTemplateByIntrinsic(invoke->GetIntrinsic());
  switch (access_mode_template) {
    case mirror::VarHandle::AccessModeTemplate::kGet:
      // The return type should be the same as varType, so it shouldn't be void.
      if (return_type == DataType::Type::kVoid) {
        return false;
      }
      break;
    case mirror::VarHandle::AccessModeTemplate::kSet:
      if (return_type != DataType::Type::kVoid) {
        return false;
      }
      break;
    case mirror::VarHandle::AccessModeTemplate::kCompareAndSet: {
      if (return_type != DataType::Type::kBool) {
        return false;
      }
      uint32_t expected_value_index = number_of_arguments - 2;
      uint32_t new_value_index = number_of_arguments - 1;
      DataType::Type expected_value_type = GetDataTypeFromShorty(invoke, expected_value_index);
      DataType::Type new_value_type = GetDataTypeFromShorty(invoke, new_value_index);
      if (expected_value_type != new_value_type) {
        return false;
      }
      break;
    }
    case mirror::VarHandle::AccessModeTemplate::kCompareAndExchange: {
      uint32_t expected_value_index = number_of_arguments - 2;
      uint32_t new_value_index = number_of_arguments - 1;
      DataType::Type expected_value_type = GetDataTypeFromShorty(invoke, expected_value_index);
      DataType::Type new_value_type = GetDataTypeFromShorty(invoke, new_value_index);
      if (expected_value_type != new_value_type || return_type != expected_value_type) {
        return false;
      }
      break;
    }
    case mirror::VarHandle::AccessModeTemplate::kGetAndUpdate: {
      DataType::Type value_type = GetDataTypeFromShorty(invoke, number_of_arguments - 1);
      if (IsVarHandleGetAndAdd(invoke) &&
          (value_type == DataType::Type::kReference || value_type == DataType::Type::kBool)) {
        // We should only add numerical types.
        return false;
      } else if (IsVarHandleGetAndBitwiseOp(invoke) && !DataType::IsIntegralType(value_type)) {
        // We can only apply operators to bitwise integral types.
        // Note that bitwise VarHandle operations accept a non-integral boolean type and
        // perform the appropriate logical operation. However, the result is the same as
        // using the bitwise operation on our boolean representation and this fits well
        // with DataType::IsIntegralType() treating the compiler type kBool as integral.
        return false;
      }
      if (value_type != return_type) {
        return false;
      }
      break;
    }
  }

  return true;
}

static LocationSummary* CreateVarHandleCommonLocations(HInvoke* invoke) {
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  DataType::Type return_type = invoke->GetType();

  ArenaAllocator* allocator = invoke->GetBlock()->GetGraph()->GetAllocator();
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  // Require coordinates in registers. These are the object holding the value
  // to operate on (except for static fields) and index (for arrays and views).
  for (size_t i = 0; i != expected_coordinates_count; ++i) {
    locations->SetInAt(/* VarHandle object */ 1u + i, Location::RequiresRegister());
  }
  if (return_type != DataType::Type::kVoid) {
    if (DataType::IsFloatingPointType(return_type)) {
      locations->SetOut(Location::RequiresFpuRegister());
    } else {
      locations->SetOut(Location::RequiresRegister());
    }
  }
  uint32_t arguments_start = /* VarHandle object */ 1u + expected_coordinates_count;
  uint32_t number_of_arguments = invoke->GetNumberOfArguments();
  for (size_t arg_index = arguments_start; arg_index != number_of_arguments; ++arg_index) {
    HInstruction* arg = invoke->InputAt(arg_index);
    if (DataType::IsFloatingPointType(arg->GetType())) {
      locations->SetInAt(arg_index, Location::RequiresFpuRegister());
    } else {
      locations->SetInAt(arg_index, Location::RequiresRegister());
    }
  }

  // Add a temporary for offset.
  if ((kEmitCompilerReadBarrier && !kUseBakerReadBarrier) &&
      GetExpectedVarHandleCoordinatesCount(invoke) == 0u) {  // For static fields.
    // To preserve the offset value across the non-Baker read barrier slow path
    // for loading the declaring class, use a fixed callee-save register.
    constexpr int first_callee_save = CTZ(kArmCalleeSaveRefSpills);
    locations->AddTemp(Location::RegisterLocation(first_callee_save));
  } else {
    locations->AddTemp(Location::RequiresRegister());
  }
  if (expected_coordinates_count == 0u) {
    // Add a temporary to hold the declaring class.
    locations->AddTemp(Location::RequiresRegister());
  }

  return locations;
}

static void CreateVarHandleGetLocations(HInvoke* invoke,
                                        CodeGeneratorARMVIXL* codegen,
                                        bool atomic) {
  if (!HasVarHandleIntrinsicImplementation(invoke)) {
    return;
  }

  if ((kEmitCompilerReadBarrier && !kUseBakerReadBarrier) &&
      invoke->GetType() == DataType::Type::kReference &&
      invoke->GetIntrinsic() != Intrinsics::kVarHandleGet &&
      invoke->GetIntrinsic() != Intrinsics::kVarHandleGetOpaque) {
    // Unsupported for non-Baker read barrier because the artReadBarrierSlow() ignores
    // the passed reference and reloads it from the field. This gets the memory visibility
    // wrong for Acquire/Volatile operations. b/173104084
    return;
  }

  LocationSummary* locations = CreateVarHandleCommonLocations(invoke);

  DataType::Type type = invoke->GetType();
  if (type == DataType::Type::kFloat64 && Use64BitExclusiveLoadStore(atomic, codegen)) {
    // We need 3 temporaries for GenerateIntrinsicGet() but we can reuse the
    // declaring class (if present) and offset temporary.
    DCHECK_EQ(locations->GetTempCount(),
              (GetExpectedVarHandleCoordinatesCount(invoke) == 0) ? 2u : 1u);
    locations->AddRegisterTemps(3u - locations->GetTempCount());
  }
}

static void GenerateVarHandleGet(HInvoke* invoke,
                                 CodeGeneratorARMVIXL* codegen,
                                 std::memory_order order,
                                 bool atomic,
                                 bool byte_swap = false) {
  DataType::Type type = invoke->GetType();
  DCHECK_NE(type, DataType::Type::kVoid);

  LocationSummary* locations = invoke->GetLocations();
  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  Location out = locations->Out();

  VarHandleTarget target = GetVarHandleTarget(invoke);
  VarHandleSlowPathARMVIXL* slow_path = nullptr;
  if (!byte_swap) {
    slow_path = GenerateVarHandleChecks(invoke, codegen, order, type);
    slow_path->SetAtomic(atomic);
    GenerateVarHandleTarget(invoke, target, codegen);
    __ Bind(slow_path->GetNativeByteOrderLabel());
  }

  Location maybe_temp = Location::NoLocation();
  Location maybe_temp2 = Location::NoLocation();
  Location maybe_temp3 = Location::NoLocation();
  if (kEmitCompilerReadBarrier && kUseBakerReadBarrier && type == DataType::Type::kReference) {
    // Reuse the offset temporary.
    maybe_temp = LocationFrom(target.offset);
  } else if (DataType::Is64BitType(type) && Use64BitExclusiveLoadStore(atomic, codegen)) {
    // Reuse the offset temporary and declaring class (if present).
    // The address shall be constructed in the scratch register before they are clobbered.
    maybe_temp = LocationFrom(target.offset);
    DCHECK(maybe_temp.Equals(locations->GetTemp(0)));
    if (type == DataType::Type::kFloat64) {
      maybe_temp2 = locations->GetTemp(1);
      maybe_temp3 = locations->GetTemp(2);
    }
  }

  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  Location loaded_value = out;
  DataType::Type load_type = type;
  if (byte_swap) {
    if (type == DataType::Type::kFloat64) {
      if (Use64BitExclusiveLoadStore(atomic, codegen)) {
        // Change load type to Int64 and promote `maybe_temp2` and `maybe_temp3` to `loaded_value`.
        loaded_value = LocationFrom(RegisterFrom(maybe_temp2), RegisterFrom(maybe_temp3));
        maybe_temp2 = Location::NoLocation();
        maybe_temp3 = Location::NoLocation();
      } else {
        // Use the offset temporary and the scratch register.
        loaded_value = LocationFrom(target.offset, temps.Acquire());
      }
      load_type = DataType::Type::kInt64;
    } else if (type == DataType::Type::kFloat32) {
      // Reuse the offset temporary.
      loaded_value = LocationFrom(target.offset);
      load_type = DataType::Type::kInt32;
    } else if (type == DataType::Type::kInt64) {
      // Swap the high and low registers and reverse the bytes in each after the load.
      loaded_value = LocationFrom(HighRegisterFrom(out), LowRegisterFrom(out));
    }
  }

  GenerateIntrinsicGet(invoke,
                       codegen,
                       load_type,
                       order,
                       atomic,
                       target.object,
                       target.offset,
                       loaded_value,
                       maybe_temp,
                       maybe_temp2,
                       maybe_temp3);
  if (byte_swap) {
    if (type == DataType::Type::kInt64) {
      GenerateReverseBytesInPlaceForEachWord(assembler, loaded_value);
    } else {
      GenerateReverseBytes(assembler, type, loaded_value, out);
    }
  }

  if (!byte_swap) {
    __ Bind(slow_path->GetExitLabel());
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGet(HInvoke* invoke) {
  CreateVarHandleGetLocations(invoke, codegen_, /*atomic=*/ false);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGet(HInvoke* invoke) {
  GenerateVarHandleGet(invoke, codegen_, std::memory_order_relaxed, /*atomic=*/ false);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetOpaque(HInvoke* invoke) {
  CreateVarHandleGetLocations(invoke, codegen_, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetOpaque(HInvoke* invoke) {
  GenerateVarHandleGet(invoke, codegen_, std::memory_order_relaxed, /*atomic=*/ true);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAcquire(HInvoke* invoke) {
  CreateVarHandleGetLocations(invoke, codegen_, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAcquire(HInvoke* invoke) {
  GenerateVarHandleGet(invoke, codegen_, std::memory_order_acquire, /*atomic=*/ true);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetVolatile(HInvoke* invoke) {
  CreateVarHandleGetLocations(invoke, codegen_, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetVolatile(HInvoke* invoke) {
  GenerateVarHandleGet(invoke, codegen_, std::memory_order_seq_cst, /*atomic=*/ true);
}

static void CreateVarHandleSetLocations(HInvoke* invoke,
                                        CodeGeneratorARMVIXL* codegen,
                                        bool atomic) {
  if (!HasVarHandleIntrinsicImplementation(invoke)) {
    return;
  }

  LocationSummary* locations = CreateVarHandleCommonLocations(invoke);

  uint32_t number_of_arguments = invoke->GetNumberOfArguments();
  DataType::Type value_type = GetDataTypeFromShorty(invoke, number_of_arguments - 1u);
  if (DataType::Is64BitType(value_type)) {
    size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
    DCHECK_EQ(locations->GetTempCount(), (expected_coordinates_count == 0) ? 2u : 1u);
    HInstruction* arg = invoke->InputAt(number_of_arguments - 1u);
    bool has_reverse_bytes_slow_path =
        (expected_coordinates_count == 2u) &&
        !(arg->IsConstant() && arg->AsConstant()->IsZeroBitPattern());
    if (Use64BitExclusiveLoadStore(atomic, codegen)) {
      // We need 4 temporaries in the byte array view slow path. Otherwise, we need
      // 2 or 3 temporaries for GenerateIntrinsicSet() depending on the value type.
      // We can reuse the offset temporary and declaring class (if present).
      size_t temps_needed = has_reverse_bytes_slow_path
          ? 4u
          : ((value_type == DataType::Type::kFloat64) ? 3u : 2u);
      locations->AddRegisterTemps(temps_needed - locations->GetTempCount());
    } else if (has_reverse_bytes_slow_path) {
      // We need 2 temps for the value with reversed bytes in the byte array view slow path.
      // We can reuse the offset temporary.
      DCHECK_EQ(locations->GetTempCount(), 1u);
      locations->AddTemp(Location::RequiresRegister());
    }
  }
}

static void GenerateVarHandleSet(HInvoke* invoke,
                                 CodeGeneratorARMVIXL* codegen,
                                 std::memory_order order,
                                 bool atomic,
                                 bool byte_swap = false) {
  uint32_t value_index = invoke->GetNumberOfArguments() - 1;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, value_index);

  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  Location value = locations->InAt(value_index);

  VarHandleTarget target = GetVarHandleTarget(invoke);
  VarHandleSlowPathARMVIXL* slow_path = nullptr;
  if (!byte_swap) {
    slow_path = GenerateVarHandleChecks(invoke, codegen, order, value_type);
    slow_path->SetAtomic(atomic);
    GenerateVarHandleTarget(invoke, target, codegen);
    __ Bind(slow_path->GetNativeByteOrderLabel());
  }

  Location maybe_temp = Location::NoLocation();
  Location maybe_temp2 = Location::NoLocation();
  Location maybe_temp3 = Location::NoLocation();
  if (DataType::Is64BitType(value_type) && Use64BitExclusiveLoadStore(atomic, codegen)) {
    // Reuse the offset temporary and declaring class (if present).
    // The address shall be constructed in the scratch register before they are clobbered.
    maybe_temp = locations->GetTemp(0);
    maybe_temp2 = locations->GetTemp(1);
    if (value_type == DataType::Type::kFloat64) {
      maybe_temp3 = locations->GetTemp(2);
    }
  }

  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  if (byte_swap) {
    if (DataType::Is64BitType(value_type) || value_type == DataType::Type::kFloat32) {
      // Calculate the address in scratch register, so that we can use the offset temporary.
      vixl32::Register base = temps.Acquire();
      __ Add(base, target.object, target.offset);
      target.object = base;
      target.offset = vixl32::Register();
    }
    Location original_value = value;
    if (DataType::Is64BitType(value_type)) {
      size_t temp_start = 0u;
      if (Use64BitExclusiveLoadStore(atomic, codegen)) {
        // Clear `maybe_temp3` which was initialized above for Float64.
        DCHECK(value_type != DataType::Type::kFloat64 || maybe_temp3.Equals(locations->GetTemp(2)));
        maybe_temp3 = Location::NoLocation();
        temp_start = 2u;
      }
      value = LocationFrom(RegisterFrom(locations->GetTemp(temp_start)),
                           RegisterFrom(locations->GetTemp(temp_start + 1u)));
      if (value_type == DataType::Type::kFloat64) {
        __ Vmov(HighRegisterFrom(value), LowRegisterFrom(value), DRegisterFrom(original_value));
        GenerateReverseBytesInPlaceForEachWord(assembler, value);
        value_type = DataType::Type::kInt64;
      } else {
        GenerateReverseBytes(assembler, value_type, original_value, value);
      }
    } else if (value_type == DataType::Type::kFloat32) {
      value = locations->GetTemp(0);  // Use the offset temporary which was freed above.
      __ Vmov(RegisterFrom(value), SRegisterFrom(original_value));
      GenerateReverseBytes(assembler, DataType::Type::kInt32, value, value);
      value_type = DataType::Type::kInt32;
    } else {
      value = LocationFrom(temps.Acquire());
      GenerateReverseBytes(assembler, value_type, original_value, value);
    }
  }

  GenerateIntrinsicSet(codegen,
                       value_type,
                       order,
                       atomic,
                       target.object,
                       target.offset,
                       value,
                       maybe_temp,
                       maybe_temp2,
                       maybe_temp3);

  if (CodeGenerator::StoreNeedsWriteBarrier(value_type, invoke->InputAt(value_index))) {
    // Reuse the offset temporary for MarkGCCard.
    vixl32::Register temp = target.offset;
    vixl32::Register card = temps.Acquire();
    vixl32::Register value_reg = RegisterFrom(value);
    codegen->MarkGCCard(temp, card, target.object, value_reg, /*value_can_be_null=*/ true);
  }

  if (!byte_swap) {
    __ Bind(slow_path->GetExitLabel());
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleSet(HInvoke* invoke) {
  CreateVarHandleSetLocations(invoke, codegen_, /*atomic=*/ false);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleSet(HInvoke* invoke) {
  GenerateVarHandleSet(invoke, codegen_, std::memory_order_relaxed, /*atomic=*/ false);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleSetOpaque(HInvoke* invoke) {
  CreateVarHandleSetLocations(invoke, codegen_, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleSetOpaque(HInvoke* invoke) {
  GenerateVarHandleSet(invoke, codegen_, std::memory_order_relaxed, /*atomic=*/ true);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleSetRelease(HInvoke* invoke) {
  CreateVarHandleSetLocations(invoke, codegen_, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleSetRelease(HInvoke* invoke) {
  GenerateVarHandleSet(invoke, codegen_, std::memory_order_release, /*atomic=*/ true);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleSetVolatile(HInvoke* invoke) {
  CreateVarHandleSetLocations(invoke, codegen_, /*atomic=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleSetVolatile(HInvoke* invoke) {
  // ARM store-release instructions are implicitly sequentially consistent.
  GenerateVarHandleSet(invoke, codegen_, std::memory_order_seq_cst, /*atomic=*/ true);
}

static void CreateVarHandleCompareAndSetOrExchangeLocations(HInvoke* invoke, bool return_success) {
  if (!HasVarHandleIntrinsicImplementation(invoke)) {
    return;
  }

  uint32_t number_of_arguments = invoke->GetNumberOfArguments();
  DataType::Type value_type = GetDataTypeFromShorty(invoke, number_of_arguments - 1u);
  if ((kEmitCompilerReadBarrier && !kUseBakerReadBarrier) &&
      value_type == DataType::Type::kReference) {
    // Unsupported for non-Baker read barrier because the artReadBarrierSlow() ignores
    // the passed reference and reloads it from the field. This breaks the read barriers
    // in slow path in different ways. The marked old value may not actually be a to-space
    // reference to the same object as `old_value`, breaking slow path assumptions. And
    // for CompareAndExchange, marking the old value after comparison failure may actually
    // return the reference to `expected`, erroneously indicating success even though we
    // did not set the new value. (And it also gets the memory visibility wrong.) b/173104084
    return;
  }

  LocationSummary* locations = CreateVarHandleCommonLocations(invoke);

  if (kEmitCompilerReadBarrier && !kUseBakerReadBarrier) {
    // We need callee-save registers for both the class object and offset instead of
    // the temporaries reserved in CreateVarHandleCommonLocations().
    static_assert(POPCOUNT(kArmCalleeSaveRefSpills) >= 2u);
    constexpr int first_callee_save = CTZ(kArmCalleeSaveRefSpills);
    constexpr int second_callee_save = CTZ(kArmCalleeSaveRefSpills ^ (1u << first_callee_save));
    if (GetExpectedVarHandleCoordinatesCount(invoke) == 0u) {  // For static fields.
      DCHECK_EQ(locations->GetTempCount(), 2u);
      DCHECK(locations->GetTemp(0u).Equals(Location::RequiresRegister()));
      DCHECK(locations->GetTemp(1u).Equals(Location::RegisterLocation(first_callee_save)));
      locations->SetTempAt(0u, Location::RegisterLocation(second_callee_save));
    } else {
      DCHECK_EQ(locations->GetTempCount(), 1u);
      DCHECK(locations->GetTemp(0u).Equals(Location::RequiresRegister()));
      locations->SetTempAt(0u, Location::RegisterLocation(first_callee_save));
    }
  }

  if (DataType::IsFloatingPointType(value_type)) {
    // We can reuse the declaring class (if present) and offset temporary.
    DCHECK_EQ(locations->GetTempCount(),
              (GetExpectedVarHandleCoordinatesCount(invoke) == 0) ? 2u : 1u);
    size_t temps_needed = (value_type == DataType::Type::kFloat64)
        ? (return_success ? 5u : 7u)
        : (return_success ? 3u : 4u);
    locations->AddRegisterTemps(temps_needed - locations->GetTempCount());
  } else if (GetExpectedVarHandleCoordinatesCount(invoke) == 2u) {
    // Add temps for the byte-reversed `expected` and `new_value` in the byte array view slow path.
    DCHECK_EQ(locations->GetTempCount(), 1u);
    if (value_type == DataType::Type::kInt64) {
      // We would ideally add 4 temps for Int64 but that would simply run out of registers,
      // so we instead need to reverse bytes in actual arguments and undo it at the end.
    } else {
      locations->AddRegisterTemps(2u);
    }
  }
  if (kEmitCompilerReadBarrier && value_type == DataType::Type::kReference) {
    // Add a temporary for store result, also used for the `old_value_temp` in slow path.
    locations->AddTemp(Location::RequiresRegister());
  }
}

static void GenerateVarHandleCompareAndSetOrExchange(HInvoke* invoke,
                                                     CodeGeneratorARMVIXL* codegen,
                                                     std::memory_order order,
                                                     bool return_success,
                                                     bool strong,
                                                     bool byte_swap = false) {
  DCHECK(return_success || strong);

  uint32_t expected_index = invoke->GetNumberOfArguments() - 2;
  uint32_t new_value_index = invoke->GetNumberOfArguments() - 1;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, new_value_index);
  DCHECK_EQ(value_type, GetDataTypeFromShorty(invoke, expected_index));

  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  Location expected = locations->InAt(expected_index);
  Location new_value = locations->InAt(new_value_index);
  Location out = locations->Out();

  VarHandleTarget target = GetVarHandleTarget(invoke);
  VarHandleSlowPathARMVIXL* slow_path = nullptr;
  if (!byte_swap) {
    slow_path = GenerateVarHandleChecks(invoke, codegen, order, value_type);
    slow_path->SetCompareAndSetOrExchangeArgs(return_success, strong);
    GenerateVarHandleTarget(invoke, target, codegen);
    __ Bind(slow_path->GetNativeByteOrderLabel());
  }

  bool seq_cst_barrier = (order == std::memory_order_seq_cst);
  bool release_barrier = seq_cst_barrier || (order == std::memory_order_release);
  bool acquire_barrier = seq_cst_barrier || (order == std::memory_order_acquire);
  DCHECK(release_barrier || acquire_barrier || order == std::memory_order_relaxed);

  if (release_barrier) {
    codegen->GenerateMemoryBarrier(
        seq_cst_barrier ? MemBarrierKind::kAnyAny : MemBarrierKind::kAnyStore);
  }

  // Calculate the pointer to the value.
  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  vixl32::Register tmp_ptr = temps.Acquire();
  __ Add(tmp_ptr, target.object, target.offset);

  // Move floating point values to temporaries and prepare output registers.
  // Note that float/double CAS uses bitwise comparison, rather than the operator==.
  // Reuse the declaring class (if present) and offset temporary for non-reference types,
  // the address has already been constructed in the scratch register. We are more careful
  // for references due to read and write barrier, see below.
  Location old_value;
  vixl32::Register store_result;
  vixl32::Register success = return_success ? RegisterFrom(out) : vixl32::Register();
  DataType::Type cas_type = value_type;
  if (value_type == DataType::Type::kFloat64) {
    vixl32::DRegister expected_vreg = DRegisterFrom(expected);
    vixl32::DRegister new_value_vreg = DRegisterFrom(new_value);
    expected =
        LocationFrom(RegisterFrom(locations->GetTemp(0)), RegisterFrom(locations->GetTemp(1)));
    new_value =
        LocationFrom(RegisterFrom(locations->GetTemp(2)), RegisterFrom(locations->GetTemp(3)));
    store_result = RegisterFrom(locations->GetTemp(4));
    old_value = return_success
        ? LocationFrom(success, store_result)
        : LocationFrom(RegisterFrom(locations->GetTemp(5)), RegisterFrom(locations->GetTemp(6)));
    if (byte_swap) {
      __ Vmov(HighRegisterFrom(expected), LowRegisterFrom(expected), expected_vreg);
      __ Vmov(HighRegisterFrom(new_value), LowRegisterFrom(new_value), new_value_vreg);
      GenerateReverseBytesInPlaceForEachWord(assembler, expected);
      GenerateReverseBytesInPlaceForEachWord(assembler, new_value);
    } else {
      __ Vmov(LowRegisterFrom(expected), HighRegisterFrom(expected), expected_vreg);
      __ Vmov(LowRegisterFrom(new_value), HighRegisterFrom(new_value), new_value_vreg);
    }
    cas_type = DataType::Type::kInt64;
  } else if (value_type == DataType::Type::kFloat32) {
    vixl32::SRegister expected_vreg = SRegisterFrom(expected);
    vixl32::SRegister new_value_vreg = SRegisterFrom(new_value);
    expected = locations->GetTemp(0);
    new_value = locations->GetTemp(1);
    store_result = RegisterFrom(locations->GetTemp(2));
    old_value = return_success ? LocationFrom(store_result) : locations->GetTemp(3);
    __ Vmov(RegisterFrom(expected), expected_vreg);
    __ Vmov(RegisterFrom(new_value), new_value_vreg);
    if (byte_swap) {
      GenerateReverseBytes(assembler, DataType::Type::kInt32, expected, expected);
      GenerateReverseBytes(assembler, DataType::Type::kInt32, new_value, new_value);
    }
    cas_type = DataType::Type::kInt32;
  } else if (value_type == DataType::Type::kInt64) {
    store_result = RegisterFrom(locations->GetTemp(0));
    old_value = return_success
        ? LocationFrom(success, store_result)
        // If swapping bytes, swap the high/low regs and reverse the bytes in each after the load.
        : byte_swap ? LocationFrom(HighRegisterFrom(out), LowRegisterFrom(out)) : out;
    if (byte_swap) {
      // Due to lack of registers, reverse bytes in `expected` and `new_value` and undo that later.
      GenerateReverseBytesInPlaceForEachWord(assembler, expected);
      expected = LocationFrom(HighRegisterFrom(expected), LowRegisterFrom(expected));
      GenerateReverseBytesInPlaceForEachWord(assembler, new_value);
      new_value = LocationFrom(HighRegisterFrom(new_value), LowRegisterFrom(new_value));
    }
  } else {
    // Use the last temp. For references with read barriers, this is an extra temporary
    // allocated to avoid overwriting the temporaries for declaring class (if present)
    // and offset as they are needed in the slow path. Otherwise, this is the offset
    // temporary which also works for references without read barriers that need the
    // object register preserved for the write barrier.
    store_result = RegisterFrom(locations->GetTemp(locations->GetTempCount() - 1u));
    old_value = return_success ? LocationFrom(store_result) : out;
    if (byte_swap) {
      DCHECK_EQ(locations->GetTempCount(), 3u);
      Location original_expected = expected;
      Location original_new_value = new_value;
      expected = locations->GetTemp(0);
      new_value = locations->GetTemp(1);
      GenerateReverseBytes(assembler, value_type, original_expected, expected);
      GenerateReverseBytes(assembler, value_type, original_new_value, new_value);
    }
  }

  vixl32::Label exit_loop_label;
  vixl32::Label* exit_loop = &exit_loop_label;
  vixl32::Label* cmp_failure = &exit_loop_label;

  if (kEmitCompilerReadBarrier && value_type == DataType::Type::kReference) {
    // The `old_value_temp` is used first for the marked `old_value` and then for the unmarked
    // reloaded old value for subsequent CAS in the slow path.
    vixl32::Register old_value_temp = store_result;
    ReadBarrierCasSlowPathARMVIXL* rb_slow_path =
        new (codegen->GetScopedAllocator()) ReadBarrierCasSlowPathARMVIXL(
            invoke,
            strong,
            target.object,
            target.offset,
            RegisterFrom(expected),
            RegisterFrom(new_value),
            RegisterFrom(old_value),
            old_value_temp,
            store_result,
            success,
            codegen);
    codegen->AddSlowPath(rb_slow_path);
    exit_loop = rb_slow_path->GetExitLabel();
    cmp_failure = rb_slow_path->GetEntryLabel();
  }

  GenerateCompareAndSet(codegen,
                        cas_type,
                        strong,
                        cmp_failure,
                        /*cmp_failure_is_far_target=*/ cmp_failure != &exit_loop_label,
                        tmp_ptr,
                        expected,
                        new_value,
                        old_value,
                        store_result,
                        success);
  __ Bind(exit_loop);

  if (acquire_barrier) {
    codegen->GenerateMemoryBarrier(
        seq_cst_barrier ? MemBarrierKind::kAnyAny : MemBarrierKind::kLoadAny);
  }

  if (!return_success) {
    if (byte_swap) {
      if (value_type == DataType::Type::kInt64) {
        GenerateReverseBytesInPlaceForEachWord(assembler, old_value);
        // Undo byte swapping in `expected` and `new_value`. We do not have the
        // information whether the value in these registers shall be needed later.
        GenerateReverseBytesInPlaceForEachWord(assembler, expected);
        GenerateReverseBytesInPlaceForEachWord(assembler, new_value);
      } else {
        GenerateReverseBytes(assembler, value_type, old_value, out);
      }
    } else if (value_type == DataType::Type::kFloat64) {
      __ Vmov(DRegisterFrom(out), LowRegisterFrom(old_value), HighRegisterFrom(old_value));
    } else if (value_type == DataType::Type::kFloat32) {
      __ Vmov(SRegisterFrom(out), RegisterFrom(old_value));
    }
  }

  if (CodeGenerator::StoreNeedsWriteBarrier(value_type, invoke->InputAt(new_value_index))) {
    // Reuse the offset temporary and scratch register for MarkGCCard.
    vixl32::Register temp = target.offset;
    vixl32::Register card = tmp_ptr;
    // Mark card for object assuming new value is stored.
    bool new_value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MarkGCCard(temp, card, target.object, RegisterFrom(new_value), new_value_can_be_null);
  }

  if (!byte_swap) {
    __ Bind(slow_path->GetExitLabel());
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleCompareAndExchange(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, /*return_success=*/ false);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleCompareAndExchange(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_seq_cst, /*return_success=*/ false, /*strong=*/ true);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleCompareAndExchangeAcquire(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, /*return_success=*/ false);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleCompareAndExchangeAcquire(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_acquire, /*return_success=*/ false, /*strong=*/ true);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleCompareAndExchangeRelease(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, /*return_success=*/ false);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleCompareAndExchangeRelease(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_release, /*return_success=*/ false, /*strong=*/ true);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleCompareAndSet(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, /*return_success=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleCompareAndSet(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_seq_cst, /*return_success=*/ true, /*strong=*/ true);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleWeakCompareAndSet(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, /*return_success=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleWeakCompareAndSet(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_seq_cst, /*return_success=*/ true, /*strong=*/ false);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleWeakCompareAndSetAcquire(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, /*return_success=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleWeakCompareAndSetAcquire(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_acquire, /*return_success=*/ true, /*strong=*/ false);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleWeakCompareAndSetPlain(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, /*return_success=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleWeakCompareAndSetPlain(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_relaxed, /*return_success=*/ true, /*strong=*/ false);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleWeakCompareAndSetRelease(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, /*return_success=*/ true);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleWeakCompareAndSetRelease(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_release, /*return_success=*/ true, /*strong=*/ false);
}

static void CreateVarHandleGetAndUpdateLocations(HInvoke* invoke,
                                                 GetAndUpdateOp get_and_update_op) {
  if (!HasVarHandleIntrinsicImplementation(invoke)) {
    return;
  }

  if ((kEmitCompilerReadBarrier && !kUseBakerReadBarrier) &&
      invoke->GetType() == DataType::Type::kReference) {
    // Unsupported for non-Baker read barrier because the artReadBarrierSlow() ignores
    // the passed reference and reloads it from the field, thus seeing the new value
    // that we have just stored. (And it also gets the memory visibility wrong.) b/173104084
    return;
  }

  LocationSummary* locations = CreateVarHandleCommonLocations(invoke);

  // We can reuse the declaring class (if present) and offset temporary, except for
  // non-Baker read barriers that need them for the slow path.
  DCHECK_EQ(locations->GetTempCount(),
            (GetExpectedVarHandleCoordinatesCount(invoke) == 0) ? 2u : 1u);

  DataType::Type value_type = invoke->GetType();
  if (get_and_update_op == GetAndUpdateOp::kSet) {
    if (DataType::IsFloatingPointType(value_type)) {
      // Add temps needed to do the GenerateGetAndUpdate() with core registers.
      size_t temps_needed = (value_type == DataType::Type::kFloat64) ? 5u : 3u;
      locations->AddRegisterTemps(temps_needed - locations->GetTempCount());
    } else if ((kEmitCompilerReadBarrier && !kUseBakerReadBarrier) &&
               value_type == DataType::Type::kReference) {
      // We need to preserve the declaring class (if present) and offset for read barrier
      // slow paths, so we must use a separate temporary for the exclusive store result.
      locations->AddTemp(Location::RequiresRegister());
    } else if (GetExpectedVarHandleCoordinatesCount(invoke) == 2u) {
      // Add temps for the byte-reversed `arg` in the byte array view slow path.
      DCHECK_EQ(locations->GetTempCount(), 1u);
      locations->AddRegisterTemps((value_type == DataType::Type::kInt64) ? 2u : 1u);
    }
  } else {
    // We need temporaries for the new value and exclusive store result.
    size_t temps_needed = DataType::Is64BitType(value_type) ? 3u : 2u;
    if (get_and_update_op != GetAndUpdateOp::kAdd &&
        GetExpectedVarHandleCoordinatesCount(invoke) == 2u) {
      // Add temps for the byte-reversed `arg` in the byte array view slow path.
      if (value_type == DataType::Type::kInt64) {
        // We would ideally add 2 temps for Int64 but that would simply run out of registers,
        // so we instead need to reverse bytes in the actual argument and undo it at the end.
      } else {
        temps_needed += 1u;
      }
    }
    locations->AddRegisterTemps(temps_needed - locations->GetTempCount());
    if (DataType::IsFloatingPointType(value_type)) {
      // Note: This shall allocate a D register. There is no way to request an S register.
      locations->AddTemp(Location::RequiresFpuRegister());
    }
  }
}

static void GenerateVarHandleGetAndUpdate(HInvoke* invoke,
                                          CodeGeneratorARMVIXL* codegen,
                                          GetAndUpdateOp get_and_update_op,
                                          std::memory_order order,
                                          bool byte_swap = false) {
  uint32_t arg_index = invoke->GetNumberOfArguments() - 1;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, arg_index);

  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  Location arg = locations->InAt(arg_index);
  Location out = locations->Out();

  VarHandleTarget target = GetVarHandleTarget(invoke);
  VarHandleSlowPathARMVIXL* slow_path = nullptr;
  if (!byte_swap) {
    slow_path = GenerateVarHandleChecks(invoke, codegen, order, value_type);
    slow_path->SetGetAndUpdateOp(get_and_update_op);
    GenerateVarHandleTarget(invoke, target, codegen);
    __ Bind(slow_path->GetNativeByteOrderLabel());
  }

  bool seq_cst_barrier = (order == std::memory_order_seq_cst);
  bool release_barrier = seq_cst_barrier || (order == std::memory_order_release);
  bool acquire_barrier = seq_cst_barrier || (order == std::memory_order_acquire);
  DCHECK(release_barrier || acquire_barrier || order == std::memory_order_relaxed);

  if (release_barrier) {
    codegen->GenerateMemoryBarrier(
        seq_cst_barrier ? MemBarrierKind::kAnyAny : MemBarrierKind::kAnyStore);
  }

  // Use the scratch register for the pointer to the target location.
  UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
  vixl32::Register tmp_ptr = temps.Acquire();
  __ Add(tmp_ptr, target.object, target.offset);

  // Use the offset temporary for the exclusive store result.
  vixl32::Register store_result = target.offset;

  // The load/store type is never floating point.
  DataType::Type load_store_type = DataType::IsFloatingPointType(value_type)
      ? ((value_type == DataType::Type::kFloat32) ? DataType::Type::kInt32 : DataType::Type::kInt64)
      : value_type;

  // Prepare register for old value and temporaries if any.
  Location old_value = out;
  Location maybe_temp = Location::NoLocation();
  Location maybe_vreg_temp = Location::NoLocation();
  if (get_and_update_op == GetAndUpdateOp::kSet) {
    // For floating point GetAndSet, do the GenerateGetAndUpdate() with core registers,
    // rather than moving between core and FP registers in the loop.
    if (value_type == DataType::Type::kFloat64) {
      vixl32::DRegister arg_vreg = DRegisterFrom(arg);
      DCHECK_EQ(locations->GetTempCount(), 5u);  // `store_result` and the four here.
      old_value =
          LocationFrom(RegisterFrom(locations->GetTemp(1)), RegisterFrom(locations->GetTemp(2)));
      arg = LocationFrom(RegisterFrom(locations->GetTemp(3)), RegisterFrom(locations->GetTemp(4)));
      if (byte_swap) {
        __ Vmov(HighRegisterFrom(arg), LowRegisterFrom(arg), arg_vreg);
        GenerateReverseBytesInPlaceForEachWord(assembler, arg);
      } else {
        __ Vmov(LowRegisterFrom(arg), HighRegisterFrom(arg), arg_vreg);
      }
    } else if (value_type == DataType::Type::kFloat32) {
      vixl32::SRegister arg_vreg = SRegisterFrom(arg);
      DCHECK_EQ(locations->GetTempCount(), 3u);  // `store_result` and the two here.
      old_value = locations->GetTemp(1);
      arg = locations->GetTemp(2);
      __ Vmov(RegisterFrom(arg), arg_vreg);
      if (byte_swap) {
        GenerateReverseBytes(assembler, DataType::Type::kInt32, arg, arg);
      }
    } else if (kEmitCompilerReadBarrier && value_type == DataType::Type::kReference) {
      if (kUseBakerReadBarrier) {
        // Load the old value initially to a temporary register.
        // We shall move it to `out` later with a read barrier.
        old_value = LocationFrom(store_result);
        store_result = RegisterFrom(out);  // Use the `out` for the exclusive store result.
      } else {
        // The store_result is a separate temporary.
        DCHECK(!store_result.Is(target.object));
        DCHECK(!store_result.Is(target.offset));
      }
    } else if (byte_swap) {
      Location original_arg = arg;
      arg = locations->GetTemp(1);
      if (value_type == DataType::Type::kInt64) {
        arg = LocationFrom(RegisterFrom(arg), RegisterFrom(locations->GetTemp(2)));
        // Swap the high/low regs and reverse the bytes in each after the load.
        old_value = LocationFrom(HighRegisterFrom(out), LowRegisterFrom(out));
      }
      GenerateReverseBytes(assembler, value_type, original_arg, arg);
    }
  } else {
    maybe_temp = DataType::Is64BitType(value_type)
        ? LocationFrom(RegisterFrom(locations->GetTemp(1)), RegisterFrom(locations->GetTemp(2)))
        : locations->GetTemp(1);
    DCHECK(!maybe_temp.Contains(LocationFrom(store_result)));
    if (DataType::IsFloatingPointType(value_type)) {
      maybe_vreg_temp = locations->GetTemp(locations->GetTempCount() - 1u);
      DCHECK(maybe_vreg_temp.IsFpuRegisterPair());
    }
    if (byte_swap) {
      if (get_and_update_op == GetAndUpdateOp::kAdd) {
        // We need to do the byte swapping in the CAS loop for GetAndAdd.
        get_and_update_op = GetAndUpdateOp::kAddWithByteSwap;
      } else if (value_type == DataType::Type::kInt64) {
        // Swap the high/low regs and reverse the bytes in each after the load.
        old_value = LocationFrom(HighRegisterFrom(out), LowRegisterFrom(out));
        // Due to lack of registers, reverse bytes in `arg` and undo that later.
        GenerateReverseBytesInPlaceForEachWord(assembler, arg);
        arg = LocationFrom(HighRegisterFrom(arg), LowRegisterFrom(arg));
      } else {
        DCHECK(!DataType::IsFloatingPointType(value_type));
        Location original_arg = arg;
        arg = locations->GetTemp(2);
        DCHECK(!arg.Contains(LocationFrom(store_result)));
        GenerateReverseBytes(assembler, value_type, original_arg, arg);
      }
    }
  }

  GenerateGetAndUpdate(codegen,
                       get_and_update_op,
                       load_store_type,
                       tmp_ptr,
                       arg,
                       old_value,
                       store_result,
                       maybe_temp,
                       maybe_vreg_temp);

  if (acquire_barrier) {
    codegen->GenerateMemoryBarrier(
        seq_cst_barrier ? MemBarrierKind::kAnyAny : MemBarrierKind::kLoadAny);
  }

  if (byte_swap && get_and_update_op != GetAndUpdateOp::kAddWithByteSwap) {
    if (value_type == DataType::Type::kInt64) {
      GenerateReverseBytesInPlaceForEachWord(assembler, old_value);
      if (get_and_update_op != GetAndUpdateOp::kSet) {
        // Undo byte swapping in `arg`. We do not have the information
        // whether the value in these registers shall be needed later.
        GenerateReverseBytesInPlaceForEachWord(assembler, arg);
      }
    } else {
      GenerateReverseBytes(assembler, value_type, old_value, out);
    }
  } else if (get_and_update_op == GetAndUpdateOp::kSet &&
             DataType::IsFloatingPointType(value_type)) {
    if (value_type == DataType::Type::kFloat64) {
      __ Vmov(DRegisterFrom(out), LowRegisterFrom(old_value), HighRegisterFrom(old_value));
    } else {
      __ Vmov(SRegisterFrom(out), RegisterFrom(old_value));
    }
  } else if (kEmitCompilerReadBarrier && value_type == DataType::Type::kReference) {
    if (kUseBakerReadBarrier) {
      codegen->GenerateIntrinsicCasMoveWithBakerReadBarrier(RegisterFrom(out),
                                                            RegisterFrom(old_value));
    } else {
      codegen->GenerateReadBarrierSlow(
          invoke,
          Location::RegisterLocation(RegisterFrom(out).GetCode()),
          Location::RegisterLocation(RegisterFrom(old_value).GetCode()),
          Location::RegisterLocation(target.object.GetCode()),
          /*offset=*/ 0u,
          /*index=*/ Location::RegisterLocation(target.offset.GetCode()));
    }
  }

  if (CodeGenerator::StoreNeedsWriteBarrier(value_type, invoke->InputAt(arg_index))) {
    // Reuse the offset temporary and scratch register for MarkGCCard.
    vixl32::Register temp = target.offset;
    vixl32::Register card = tmp_ptr;
    // Mark card for object assuming new value is stored.
    bool new_value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MarkGCCard(temp, card, target.object, RegisterFrom(arg), new_value_can_be_null);
  }

  if (!byte_swap) {
    __ Bind(slow_path->GetExitLabel());
  }
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndSet(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kSet);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndSet(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kSet, std::memory_order_seq_cst);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndSetAcquire(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kSet);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndSetAcquire(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kSet, std::memory_order_acquire);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndSetRelease(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kSet);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndSetRelease(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kSet, std::memory_order_release);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndAdd(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kAdd);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndAdd(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kAdd, std::memory_order_seq_cst);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndAddAcquire(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kAdd);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndAddAcquire(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kAdd, std::memory_order_acquire);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndAddRelease(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kAdd);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndAddRelease(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kAdd, std::memory_order_release);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndBitwiseAnd(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kAnd);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndBitwiseAnd(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kAnd, std::memory_order_seq_cst);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndBitwiseAndAcquire(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kAnd);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndBitwiseAndAcquire(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kAnd, std::memory_order_acquire);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndBitwiseAndRelease(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kAnd);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndBitwiseAndRelease(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kAnd, std::memory_order_release);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndBitwiseOr(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kOr);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndBitwiseOr(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kOr, std::memory_order_seq_cst);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndBitwiseOrAcquire(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kOr);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndBitwiseOrAcquire(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kOr, std::memory_order_acquire);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndBitwiseOrRelease(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kOr);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndBitwiseOrRelease(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kOr, std::memory_order_release);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndBitwiseXor(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kXor);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndBitwiseXor(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kXor, std::memory_order_seq_cst);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndBitwiseXorAcquire(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kXor);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndBitwiseXorAcquire(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kXor, std::memory_order_acquire);
}

void IntrinsicLocationsBuilderARMVIXL::VisitVarHandleGetAndBitwiseXorRelease(HInvoke* invoke) {
  CreateVarHandleGetAndUpdateLocations(invoke, GetAndUpdateOp::kXor);
}

void IntrinsicCodeGeneratorARMVIXL::VisitVarHandleGetAndBitwiseXorRelease(HInvoke* invoke) {
  GenerateVarHandleGetAndUpdate(invoke, codegen_, GetAndUpdateOp::kXor, std::memory_order_release);
}

void VarHandleSlowPathARMVIXL::EmitByteArrayViewCode(CodeGenerator* codegen_in) {
  DCHECK(GetByteArrayViewCheckLabel()->IsReferenced());
  CodeGeneratorARMVIXL* codegen = down_cast<CodeGeneratorARMVIXL*>(codegen_in);
  ArmVIXLAssembler* assembler = codegen->GetAssembler();
  HInvoke* invoke = GetInvoke();
  mirror::VarHandle::AccessModeTemplate access_mode_template = GetAccessModeTemplate();
  DataType::Type value_type =
      GetVarHandleExpectedValueType(invoke, /*expected_coordinates_count=*/ 2u);
  DCHECK_NE(value_type, DataType::Type::kReference);
  size_t size = DataType::Size(value_type);
  DCHECK_GT(size, 1u);
  vixl32::Operand size_operand(dchecked_integral_cast<int32_t>(size));
  vixl32::Register varhandle = InputRegisterAt(invoke, 0);
  vixl32::Register object = InputRegisterAt(invoke, 1);
  vixl32::Register index = InputRegisterAt(invoke, 2);

  MemberOffset class_offset = mirror::Object::ClassOffset();
  MemberOffset array_length_offset = mirror::Array::LengthOffset();
  MemberOffset data_offset = mirror::Array::DataOffset(Primitive::kPrimByte);
  MemberOffset native_byte_order_offset = mirror::ByteArrayViewVarHandle::NativeByteOrderOffset();

  __ Bind(GetByteArrayViewCheckLabel());

  VarHandleTarget target = GetVarHandleTarget(invoke);
  {
    // Use the offset temporary register. It is not used yet at this point.
    vixl32::Register temp = RegisterFrom(invoke->GetLocations()->GetTemp(0u));

    UseScratchRegisterScope temps(assembler->GetVIXLAssembler());
    vixl32::Register temp2 = temps.Acquire();

    // The main path checked that the coordinateType0 is an array class that matches
    // the class of the actual coordinate argument but it does not match the value type.
    // Check if the `varhandle` references a ByteArrayViewVarHandle instance.
    __ Ldr(temp, MemOperand(varhandle, class_offset.Int32Value()));
    codegen->LoadClassRootForIntrinsic(temp2, ClassRoot::kJavaLangInvokeByteArrayViewVarHandle);
    __ Cmp(temp, temp2);
    __ B(ne, GetEntryLabel());

    // Check for array index out of bounds.
    __ Ldr(temp, MemOperand(object, array_length_offset.Int32Value()));
    if (!temp.IsLow()) {
      // Avoid using the 32-bit `cmp temp, #imm` in IT block by loading `size` into `temp2`.
      __ Mov(temp2, size_operand);
    }
    __ Subs(temp, temp, index);
    {
      // Use ExactAssemblyScope here because we are using IT.
      ExactAssemblyScope it_scope(assembler->GetVIXLAssembler(),
                                  2 * k16BitT32InstructionSizeInBytes);
      __ it(hs);
      if (temp.IsLow()) {
        __ cmp(hs, temp, size_operand);
      } else {
        __ cmp(hs, temp, temp2);
      }
    }
    __ B(lo, GetEntryLabel());

    // Construct the target.
    __ Add(target.offset, index, data_offset.Int32Value());  // Note: `temp` cannot be used below.

    // Alignment check. For unaligned access, go to the runtime.
    DCHECK(IsPowerOfTwo(size));
    __ Tst(target.offset, dchecked_integral_cast<int32_t>(size - 1u));
    __ B(ne, GetEntryLabel());

    // Byte order check. For native byte order return to the main path.
    if (access_mode_template == mirror::VarHandle::AccessModeTemplate::kSet) {
      HInstruction* arg = invoke->InputAt(invoke->GetNumberOfArguments() - 1u);
      if (arg->IsConstant() && arg->AsConstant()->IsZeroBitPattern()) {
        // There is no reason to differentiate between native byte order and byte-swap
        // for setting a zero bit pattern. Just return to the main path.
        __ B(GetNativeByteOrderLabel());
        return;
      }
    }
    __ Ldr(temp2, MemOperand(varhandle, native_byte_order_offset.Int32Value()));
    __ Cmp(temp2, 0);
    __ B(ne, GetNativeByteOrderLabel());
  }

  switch (access_mode_template) {
    case mirror::VarHandle::AccessModeTemplate::kGet:
      GenerateVarHandleGet(invoke, codegen, order_, atomic_, /*byte_swap=*/ true);
      break;
    case mirror::VarHandle::AccessModeTemplate::kSet:
      GenerateVarHandleSet(invoke, codegen, order_, atomic_, /*byte_swap=*/ true);
      break;
    case mirror::VarHandle::AccessModeTemplate::kCompareAndSet:
    case mirror::VarHandle::AccessModeTemplate::kCompareAndExchange:
      GenerateVarHandleCompareAndSetOrExchange(
          invoke, codegen, order_, return_success_, strong_, /*byte_swap=*/ true);
      break;
    case mirror::VarHandle::AccessModeTemplate::kGetAndUpdate:
      GenerateVarHandleGetAndUpdate(
          invoke, codegen, get_and_update_op_, order_, /*byte_swap=*/ true);
      break;
  }
  __ B(GetExitLabel());
}

UNIMPLEMENTED_INTRINSIC(ARMVIXL, MathRoundDouble)   // Could be done by changing rounding mode, maybe?
UNIMPLEMENTED_INTRINSIC(ARMVIXL, UnsafeCASLong)     // High register pressure.
UNIMPLEMENTED_INTRINSIC(ARMVIXL, SystemArrayCopyChar)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, LongDivideUnsigned)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, CRC32Update)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, CRC32UpdateBytes)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, CRC32UpdateByteBuffer)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, FP16ToFloat)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, FP16ToHalf)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, FP16Floor)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, FP16Ceil)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, FP16Rint)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, FP16Greater)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, FP16GreaterEquals)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, FP16Less)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, FP16LessEquals)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, MathMultiplyHigh)

UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringStringIndexOf);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringStringIndexOfAfter);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBufferAppend);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBufferLength);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBufferToString);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderAppendObject);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderAppendString);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderAppendCharSequence);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderAppendCharArray);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderAppendBoolean);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderAppendChar);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderAppendInt);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderAppendLong);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderAppendFloat);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderAppendDouble);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderLength);
UNIMPLEMENTED_INTRINSIC(ARMVIXL, StringBuilderToString);

// 1.8.
UNIMPLEMENTED_INTRINSIC(ARMVIXL, UnsafeGetAndAddInt)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, UnsafeGetAndAddLong)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, UnsafeGetAndSetInt)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, UnsafeGetAndSetLong)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, UnsafeGetAndSetObject)

UNIMPLEMENTED_INTRINSIC(ARMVIXL, MethodHandleInvokeExact)
UNIMPLEMENTED_INTRINSIC(ARMVIXL, MethodHandleInvoke)

UNREACHABLE_INTRINSICS(ARMVIXL)

#undef __

}  // namespace arm
}  // namespace art
