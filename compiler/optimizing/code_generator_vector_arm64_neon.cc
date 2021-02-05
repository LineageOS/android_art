/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "code_generator_arm64.h"

#include "arch/arm64/instruction_set_features_arm64.h"
#include "base/bit_utils_iterator.h"
#include "mirror/array-inl.h"
#include "mirror/string.h"

using namespace vixl::aarch64;  // NOLINT(build/namespaces)

namespace art {
namespace arm64 {

using helpers::DRegisterFrom;
using helpers::HeapOperand;
using helpers::InputRegisterAt;
using helpers::Int64FromLocation;
using helpers::LocationFrom;
using helpers::OutputRegister;
using helpers::QRegisterFrom;
using helpers::StackOperandFrom;
using helpers::VRegisterFrom;
using helpers::XRegisterFrom;

#define __ GetVIXLAssembler()->

// Returns whether the value of the constant can be directly encoded into the instruction as
// immediate.
inline bool NEONCanEncodeConstantAsImmediate(HConstant* constant, HInstruction* instr) {
  // TODO: Improve this when IsSIMDConstantEncodable method is implemented in VIXL.
  if (instr->IsVecReplicateScalar()) {
    if (constant->IsLongConstant()) {
      return false;
    } else if (constant->IsFloatConstant()) {
      return vixl::aarch64::Assembler::IsImmFP32(constant->AsFloatConstant()->GetValue());
    } else if (constant->IsDoubleConstant()) {
      return vixl::aarch64::Assembler::IsImmFP64(constant->AsDoubleConstant()->GetValue());
    }
    int64_t value = CodeGenerator::GetInt64ValueOf(constant);
    return IsUint<8>(value);
  }
  return false;
}

// Returns
//  - constant location - if 'constant' is an actual constant and its value can be
//    encoded into the instruction.
//  - register location otherwise.
inline Location NEONEncodableConstantOrRegister(HInstruction* constant,
                                                HInstruction* instr) {
  if (constant->IsConstant()
      && NEONCanEncodeConstantAsImmediate(constant->AsConstant(), instr)) {
    return Location::ConstantLocation(constant->AsConstant());
  }

  return Location::RequiresRegister();
}

// Returns whether dot product instructions should be emitted.
static bool ShouldEmitDotProductInstructions(const CodeGeneratorARM64* codegen_) {
  return codegen_->GetInstructionSetFeatures().HasDotProd();
}

void LocationsBuilderARM64Neon::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  HInstruction* input = instruction->InputAt(0);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, NEONEncodableConstantOrRegister(input, instruction));
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      if (input->IsConstant() &&
          NEONCanEncodeConstantAsImmediate(input->AsConstant(), instruction)) {
        locations->SetInAt(0, Location::ConstantLocation(input->AsConstant()));
        locations->SetOut(Location::RequiresFpuRegister());
      } else {
        locations->SetInAt(0, Location::RequiresFpuRegister());
        locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorARM64Neon::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  Location src_loc = locations->InAt(0);
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      if (src_loc.IsConstant()) {
        __ Movi(dst.V16B(), Int64FromLocation(src_loc));
      } else {
        __ Dup(dst.V16B(), InputRegisterAt(instruction, 0));
      }
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      if (src_loc.IsConstant()) {
        __ Movi(dst.V8H(), Int64FromLocation(src_loc));
      } else {
        __ Dup(dst.V8H(), InputRegisterAt(instruction, 0));
      }
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      if (src_loc.IsConstant()) {
        __ Movi(dst.V4S(), Int64FromLocation(src_loc));
      } else {
        __ Dup(dst.V4S(), InputRegisterAt(instruction, 0));
      }
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      if (src_loc.IsConstant()) {
        __ Movi(dst.V2D(), Int64FromLocation(src_loc));
      } else {
        __ Dup(dst.V2D(), XRegisterFrom(src_loc));
      }
      break;
    case DataType::Type::kFloat32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      if (src_loc.IsConstant()) {
        __ Fmov(dst.V4S(), src_loc.GetConstant()->AsFloatConstant()->GetValue());
      } else {
        __ Dup(dst.V4S(), VRegisterFrom(src_loc).V4S(), 0);
      }
      break;
    case DataType::Type::kFloat64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      if (src_loc.IsConstant()) {
        __ Fmov(dst.V2D(), src_loc.GetConstant()->AsDoubleConstant()->GetValue());
      } else {
        __ Dup(dst.V2D(), VRegisterFrom(src_loc).V2D(), 0);
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecExtractScalar(HVecExtractScalar* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresRegister());
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::SameAsFirstInput());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorARM64Neon::VisitVecExtractScalar(HVecExtractScalar* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister src = VRegisterFrom(locations->InAt(0));
  switch (instruction->GetPackedType()) {
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Umov(OutputRegister(instruction), src.V4S(), 0);
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Umov(OutputRegister(instruction), src.V2D(), 0);
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 4u);
      DCHECK(locations->InAt(0).Equals(locations->Out()));  // no code required
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

// Helper to set up locations for vector unary operations.
static void CreateVecUnOpLocations(ArenaAllocator* allocator, HVecUnaryOperation* instruction) {
  LocationSummary* locations = new (allocator) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(),
                        instruction->IsVecNot() ? Location::kOutputOverlap
                                                : Location::kNoOutputOverlap);
      break;
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecReduce(HVecReduce* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecReduce(HVecReduce* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister src = VRegisterFrom(locations->InAt(0));
  VRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      switch (instruction->GetReductionKind()) {
        case HVecReduce::kSum:
          __ Addv(dst.S(), src.V4S());
          break;
        case HVecReduce::kMin:
          __ Sminv(dst.S(), src.V4S());
          break;
        case HVecReduce::kMax:
          __ Smaxv(dst.S(), src.V4S());
          break;
      }
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      switch (instruction->GetReductionKind()) {
        case HVecReduce::kSum:
          __ Addp(dst.D(), src.V2D());
          break;
        default:
          LOG(FATAL) << "Unsupported SIMD min/max";
          UNREACHABLE();
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecCnv(HVecCnv* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecCnv(HVecCnv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister src = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  DataType::Type from = instruction->GetInputType();
  DataType::Type to = instruction->GetResultType();
  if (from == DataType::Type::kInt32 && to == DataType::Type::kFloat32) {
    DCHECK_EQ(4u, instruction->GetVectorLength());
    __ Scvtf(dst.V4S(), src.V4S());
  } else {
    LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
  }
}

void LocationsBuilderARM64Neon::VisitVecNeg(HVecNeg* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecNeg(HVecNeg* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister src = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Neg(dst.V16B(), src.V16B());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Neg(dst.V8H(), src.V8H());
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Neg(dst.V4S(), src.V4S());
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Neg(dst.V2D(), src.V2D());
      break;
    case DataType::Type::kFloat32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fneg(dst.V4S(), src.V4S());
      break;
    case DataType::Type::kFloat64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fneg(dst.V2D(), src.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecAbs(HVecAbs* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecAbs(HVecAbs* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister src = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Abs(dst.V16B(), src.V16B());
      break;
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Abs(dst.V8H(), src.V8H());
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Abs(dst.V4S(), src.V4S());
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Abs(dst.V2D(), src.V2D());
      break;
    case DataType::Type::kFloat32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fabs(dst.V4S(), src.V4S());
      break;
    case DataType::Type::kFloat64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fabs(dst.V2D(), src.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecNot(HVecNot* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecNot(HVecNot* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister src = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:  // special case boolean-not
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Movi(dst.V16B(), 1);
      __ Eor(dst.V16B(), dst.V16B(), src.V16B());
      break;
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      __ Not(dst.V16B(), src.V16B());  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

// Helper to set up locations for vector binary operations.
static void CreateVecBinOpLocations(ArenaAllocator* allocator, HVecBinaryOperation* instruction) {
  LocationSummary* locations = new (allocator) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecAdd(HVecAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecAdd(HVecAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Add(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Add(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Add(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Add(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    case DataType::Type::kFloat32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fadd(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kFloat64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fadd(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecSaturationAdd(HVecSaturationAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecSaturationAdd(HVecSaturationAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Uqadd(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Sqadd(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kUint16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Uqadd(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Sqadd(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      instruction->IsRounded()
          ? __ Urhadd(dst.V16B(), lhs.V16B(), rhs.V16B())
          : __ Uhadd(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      instruction->IsRounded()
          ? __ Srhadd(dst.V16B(), lhs.V16B(), rhs.V16B())
          : __ Shadd(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kUint16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      instruction->IsRounded()
          ? __ Urhadd(dst.V8H(), lhs.V8H(), rhs.V8H())
          : __ Uhadd(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      instruction->IsRounded()
          ? __ Srhadd(dst.V8H(), lhs.V8H(), rhs.V8H())
          : __ Shadd(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecSub(HVecSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecSub(HVecSub* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Sub(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Sub(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Sub(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Sub(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    case DataType::Type::kFloat32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fsub(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kFloat64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fsub(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecSaturationSub(HVecSaturationSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecSaturationSub(HVecSaturationSub* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Uqsub(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Sqsub(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kUint16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Uqsub(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Sqsub(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecMul(HVecMul* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecMul(HVecMul* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Mul(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Mul(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Mul(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kFloat32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fmul(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kFloat64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fmul(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecDiv(HVecDiv* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecDiv(HVecDiv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kFloat32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fdiv(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kFloat64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fdiv(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecMin(HVecMin* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecMin(HVecMin* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Umin(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Smin(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kUint16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Umin(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Smin(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case DataType::Type::kUint32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Umin(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Smin(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kFloat32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fmin(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kFloat64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fmin(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecMax(HVecMax* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecMax(HVecMax* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Umax(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Smax(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case DataType::Type::kUint16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Umax(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Smax(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case DataType::Type::kUint32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Umax(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Smax(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kFloat32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fmax(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case DataType::Type::kFloat64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fmax(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecAnd(HVecAnd* instruction) {
  // TODO: Allow constants supported by BIC (vector, immediate).
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecAnd(HVecAnd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      __ And(dst.V16B(), lhs.V16B(), rhs.V16B());  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecAndNot(HVecAndNot* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
}

void InstructionCodeGeneratorARM64Neon::VisitVecAndNot(HVecAndNot* instruction) {
  // TODO: Use BIC (vector, register).
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
}

void LocationsBuilderARM64Neon::VisitVecOr(HVecOr* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecOr(HVecOr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      __ Orr(dst.V16B(), lhs.V16B(), rhs.V16B());  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecXor(HVecXor* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecXor(HVecXor* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      __ Eor(dst.V16B(), lhs.V16B(), rhs.V16B());  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

// Helper to set up locations for vector shift operations.
static void CreateVecShiftLocations(ArenaAllocator* allocator, HVecBinaryOperation* instruction) {
  LocationSummary* locations = new (allocator) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::ConstantLocation(instruction->InputAt(1)->AsConstant()));
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecShl(HVecShl* instruction) {
  CreateVecShiftLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecShl(HVecShl* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Shl(dst.V16B(), lhs.V16B(), value);
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Shl(dst.V8H(), lhs.V8H(), value);
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Shl(dst.V4S(), lhs.V4S(), value);
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Shl(dst.V2D(), lhs.V2D(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecShr(HVecShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecShr(HVecShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Sshr(dst.V16B(), lhs.V16B(), value);
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Sshr(dst.V8H(), lhs.V8H(), value);
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Sshr(dst.V4S(), lhs.V4S(), value);
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Sshr(dst.V2D(), lhs.V2D(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecUShr(HVecUShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Neon::VisitVecUShr(HVecUShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Ushr(dst.V16B(), lhs.V16B(), value);
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Ushr(dst.V8H(), lhs.V8H(), value);
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Ushr(dst.V4S(), lhs.V4S(), value);
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Ushr(dst.V2D(), lhs.V2D(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecSetScalars(HVecSetScalars* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);

  DCHECK_EQ(1u, instruction->InputCount());  // only one input currently implemented

  HInstruction* input = instruction->InputAt(0);
  bool is_zero = IsZeroBitPattern(input);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, is_zero ? Location::ConstantLocation(input->AsConstant())
                                    : Location::RequiresRegister());
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, is_zero ? Location::ConstantLocation(input->AsConstant())
                                    : Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorARM64Neon::VisitVecSetScalars(HVecSetScalars* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister dst = VRegisterFrom(locations->Out());

  DCHECK_EQ(1u, instruction->InputCount());  // only one input currently implemented

  // Zero out all other elements first.
  __ Movi(dst.V16B(), 0);

  // Shorthand for any type of zero.
  if (IsZeroBitPattern(instruction->InputAt(0))) {
    return;
  }

  // Set required elements.
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Mov(dst.V16B(), 0, InputRegisterAt(instruction, 0));
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Mov(dst.V8H(), 0, InputRegisterAt(instruction, 0));
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Mov(dst.V4S(), 0, InputRegisterAt(instruction, 0));
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Mov(dst.V2D(), 0, InputRegisterAt(instruction, 0));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

// Helper to set up locations for vector accumulations.
static void CreateVecAccumLocations(ArenaAllocator* allocator, HVecOperation* instruction) {
  LocationSummary* locations = new (allocator) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetInAt(2, Location::RequiresFpuRegister());
      locations->SetOut(Location::SameAsFirstInput());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instruction) {
  CreateVecAccumLocations(GetGraph()->GetAllocator(), instruction);
}

// Some early revisions of the Cortex-A53 have an erratum (835769) whereby it is possible for a
// 64-bit scalar multiply-accumulate instruction in AArch64 state to generate an incorrect result.
// However vector MultiplyAccumulate instruction is not affected.
void InstructionCodeGeneratorARM64Neon::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister acc = VRegisterFrom(locations->InAt(0));
  VRegister left = VRegisterFrom(locations->InAt(1));
  VRegister right = VRegisterFrom(locations->InAt(2));

  DCHECK(locations->InAt(0).Equals(locations->Out()));

  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      if (instruction->GetOpKind() == HInstruction::kAdd) {
        __ Mla(acc.V16B(), left.V16B(), right.V16B());
      } else {
        __ Mls(acc.V16B(), left.V16B(), right.V16B());
      }
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      if (instruction->GetOpKind() == HInstruction::kAdd) {
        __ Mla(acc.V8H(), left.V8H(), right.V8H());
      } else {
        __ Mls(acc.V8H(), left.V8H(), right.V8H());
      }
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      if (instruction->GetOpKind() == HInstruction::kAdd) {
        __ Mla(acc.V4S(), left.V4S(), right.V4S());
      } else {
        __ Mls(acc.V4S(), left.V4S(), right.V4S());
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecSADAccumulate(HVecSADAccumulate* instruction) {
  CreateVecAccumLocations(GetGraph()->GetAllocator(), instruction);
  // Some conversions require temporary registers.
  LocationSummary* locations = instruction->GetLocations();
  HVecOperation* a = instruction->InputAt(1)->AsVecOperation();
  HVecOperation* b = instruction->InputAt(2)->AsVecOperation();
  DCHECK_EQ(HVecOperation::ToSignedType(a->GetPackedType()),
            HVecOperation::ToSignedType(b->GetPackedType()));
  switch (a->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      switch (instruction->GetPackedType()) {
        case DataType::Type::kInt64:
          locations->AddTemp(Location::RequiresFpuRegister());
          locations->AddTemp(Location::RequiresFpuRegister());
          FALLTHROUGH_INTENDED;
        case DataType::Type::kInt32:
          locations->AddTemp(Location::RequiresFpuRegister());
          locations->AddTemp(Location::RequiresFpuRegister());
          break;
        default:
          break;
      }
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      if (instruction->GetPackedType() == DataType::Type::kInt64) {
        locations->AddTemp(Location::RequiresFpuRegister());
        locations->AddTemp(Location::RequiresFpuRegister());
      }
      break;
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      if (instruction->GetPackedType() == a->GetPackedType()) {
        locations->AddTemp(Location::RequiresFpuRegister());
      }
      break;
    default:
      break;
  }
}

void InstructionCodeGeneratorARM64Neon::VisitVecSADAccumulate(HVecSADAccumulate* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister acc = VRegisterFrom(locations->InAt(0));
  VRegister left = VRegisterFrom(locations->InAt(1));
  VRegister right = VRegisterFrom(locations->InAt(2));

  DCHECK(locations->InAt(0).Equals(locations->Out()));

  // Handle all feasible acc_T += sad(a_S, b_S) type combinations (T x S).
  HVecOperation* a = instruction->InputAt(1)->AsVecOperation();
  HVecOperation* b = instruction->InputAt(2)->AsVecOperation();
  DCHECK_EQ(HVecOperation::ToSignedType(a->GetPackedType()),
            HVecOperation::ToSignedType(b->GetPackedType()));
  switch (a->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      DCHECK_EQ(16u, a->GetVectorLength());
      switch (instruction->GetPackedType()) {
        case DataType::Type::kInt16:
          DCHECK_EQ(8u, instruction->GetVectorLength());
          __ Sabal(acc.V8H(), left.V8B(), right.V8B());
          __ Sabal2(acc.V8H(), left.V16B(), right.V16B());
          break;
        case DataType::Type::kInt32: {
          DCHECK_EQ(4u, instruction->GetVectorLength());
          VRegister tmp1 = VRegisterFrom(locations->GetTemp(0));
          VRegister tmp2 = VRegisterFrom(locations->GetTemp(1));
          __ Sxtl(tmp1.V8H(), left.V8B());
          __ Sxtl(tmp2.V8H(), right.V8B());
          __ Sabal(acc.V4S(), tmp1.V4H(), tmp2.V4H());
          __ Sabal2(acc.V4S(), tmp1.V8H(), tmp2.V8H());
          __ Sxtl2(tmp1.V8H(), left.V16B());
          __ Sxtl2(tmp2.V8H(), right.V16B());
          __ Sabal(acc.V4S(), tmp1.V4H(), tmp2.V4H());
          __ Sabal2(acc.V4S(), tmp1.V8H(), tmp2.V8H());
          break;
        }
        case DataType::Type::kInt64: {
          DCHECK_EQ(2u, instruction->GetVectorLength());
          VRegister tmp1 = VRegisterFrom(locations->GetTemp(0));
          VRegister tmp2 = VRegisterFrom(locations->GetTemp(1));
          VRegister tmp3 = VRegisterFrom(locations->GetTemp(2));
          VRegister tmp4 = VRegisterFrom(locations->GetTemp(3));
          __ Sxtl(tmp1.V8H(), left.V8B());
          __ Sxtl(tmp2.V8H(), right.V8B());
          __ Sxtl(tmp3.V4S(), tmp1.V4H());
          __ Sxtl(tmp4.V4S(), tmp2.V4H());
          __ Sabal(acc.V2D(), tmp3.V2S(), tmp4.V2S());
          __ Sabal2(acc.V2D(), tmp3.V4S(), tmp4.V4S());
          __ Sxtl2(tmp3.V4S(), tmp1.V8H());
          __ Sxtl2(tmp4.V4S(), tmp2.V8H());
          __ Sabal(acc.V2D(), tmp3.V2S(), tmp4.V2S());
          __ Sabal2(acc.V2D(), tmp3.V4S(), tmp4.V4S());
          __ Sxtl2(tmp1.V8H(), left.V16B());
          __ Sxtl2(tmp2.V8H(), right.V16B());
          __ Sxtl(tmp3.V4S(), tmp1.V4H());
          __ Sxtl(tmp4.V4S(), tmp2.V4H());
          __ Sabal(acc.V2D(), tmp3.V2S(), tmp4.V2S());
          __ Sabal2(acc.V2D(), tmp3.V4S(), tmp4.V4S());
          __ Sxtl2(tmp3.V4S(), tmp1.V8H());
          __ Sxtl2(tmp4.V4S(), tmp2.V8H());
          __ Sabal(acc.V2D(), tmp3.V2S(), tmp4.V2S());
          __ Sabal2(acc.V2D(), tmp3.V4S(), tmp4.V4S());
          break;
        }
        default:
          LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
          UNREACHABLE();
      }
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      DCHECK_EQ(8u, a->GetVectorLength());
      switch (instruction->GetPackedType()) {
        case DataType::Type::kInt32:
          DCHECK_EQ(4u, instruction->GetVectorLength());
          __ Sabal(acc.V4S(), left.V4H(), right.V4H());
          __ Sabal2(acc.V4S(), left.V8H(), right.V8H());
          break;
        case DataType::Type::kInt64: {
          DCHECK_EQ(2u, instruction->GetVectorLength());
          VRegister tmp1 = VRegisterFrom(locations->GetTemp(0));
          VRegister tmp2 = VRegisterFrom(locations->GetTemp(1));
          __ Sxtl(tmp1.V4S(), left.V4H());
          __ Sxtl(tmp2.V4S(), right.V4H());
          __ Sabal(acc.V2D(), tmp1.V2S(), tmp2.V2S());
          __ Sabal2(acc.V2D(), tmp1.V4S(), tmp2.V4S());
          __ Sxtl2(tmp1.V4S(), left.V8H());
          __ Sxtl2(tmp2.V4S(), right.V8H());
          __ Sabal(acc.V2D(), tmp1.V2S(), tmp2.V2S());
          __ Sabal2(acc.V2D(), tmp1.V4S(), tmp2.V4S());
          break;
        }
        default:
          LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
          UNREACHABLE();
      }
      break;
    case DataType::Type::kInt32:
      DCHECK_EQ(4u, a->GetVectorLength());
      switch (instruction->GetPackedType()) {
        case DataType::Type::kInt32: {
          DCHECK_EQ(4u, instruction->GetVectorLength());
          VRegister tmp = VRegisterFrom(locations->GetTemp(0));
          __ Sub(tmp.V4S(), left.V4S(), right.V4S());
          __ Abs(tmp.V4S(), tmp.V4S());
          __ Add(acc.V4S(), acc.V4S(), tmp.V4S());
          break;
        }
        case DataType::Type::kInt64:
          DCHECK_EQ(2u, instruction->GetVectorLength());
          __ Sabal(acc.V2D(), left.V2S(), right.V2S());
          __ Sabal2(acc.V2D(), left.V4S(), right.V4S());
          break;
        default:
          LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
          UNREACHABLE();
      }
      break;
    case DataType::Type::kInt64:
      DCHECK_EQ(2u, a->GetVectorLength());
      switch (instruction->GetPackedType()) {
        case DataType::Type::kInt64: {
          DCHECK_EQ(2u, instruction->GetVectorLength());
          VRegister tmp = VRegisterFrom(locations->GetTemp(0));
          __ Sub(tmp.V2D(), left.V2D(), right.V2D());
          __ Abs(tmp.V2D(), tmp.V2D());
          __ Add(acc.V2D(), acc.V2D(), tmp.V2D());
          break;
        }
        default:
          LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
          UNREACHABLE();
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
  }
}

void LocationsBuilderARM64Neon::VisitVecDotProd(HVecDotProd* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  DCHECK(instruction->GetPackedType() == DataType::Type::kInt32);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetInAt(1, Location::RequiresFpuRegister());
  locations->SetInAt(2, Location::RequiresFpuRegister());
  locations->SetOut(Location::SameAsFirstInput());

  // For Int8 and Uint8 general case we need a temp register.
  if ((DataType::Size(instruction->InputAt(1)->AsVecOperation()->GetPackedType()) == 1) &&
      !ShouldEmitDotProductInstructions(codegen_)) {
    locations->AddTemp(Location::RequiresFpuRegister());
  }
}

void InstructionCodeGeneratorARM64Neon::VisitVecDotProd(HVecDotProd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  VRegister acc = VRegisterFrom(locations->InAt(0));
  VRegister left = VRegisterFrom(locations->InAt(1));
  VRegister right = VRegisterFrom(locations->InAt(2));
  HVecOperation* a = instruction->InputAt(1)->AsVecOperation();
  HVecOperation* b = instruction->InputAt(2)->AsVecOperation();
  DCHECK_EQ(HVecOperation::ToSignedType(a->GetPackedType()),
            HVecOperation::ToSignedType(b->GetPackedType()));
  DCHECK_EQ(instruction->GetPackedType(), DataType::Type::kInt32);
  DCHECK_EQ(4u, instruction->GetVectorLength());

  size_t inputs_data_size = DataType::Size(a->GetPackedType());
  switch (inputs_data_size) {
    case 1u: {
      DCHECK_EQ(16u, a->GetVectorLength());
      if (instruction->IsZeroExtending()) {
        if (ShouldEmitDotProductInstructions(codegen_)) {
          __ Udot(acc.V4S(), left.V16B(), right.V16B());
        } else {
          VRegister tmp = VRegisterFrom(locations->GetTemp(0));
          __ Umull(tmp.V8H(), left.V8B(), right.V8B());
          __ Uaddw(acc.V4S(), acc.V4S(), tmp.V4H());
          __ Uaddw2(acc.V4S(), acc.V4S(), tmp.V8H());

          __ Umull2(tmp.V8H(), left.V16B(), right.V16B());
          __ Uaddw(acc.V4S(), acc.V4S(), tmp.V4H());
          __ Uaddw2(acc.V4S(), acc.V4S(), tmp.V8H());
        }
      } else {
        if (ShouldEmitDotProductInstructions(codegen_)) {
          __ Sdot(acc.V4S(), left.V16B(), right.V16B());
        } else {
          VRegister tmp = VRegisterFrom(locations->GetTemp(0));
          __ Smull(tmp.V8H(), left.V8B(), right.V8B());
          __ Saddw(acc.V4S(), acc.V4S(), tmp.V4H());
          __ Saddw2(acc.V4S(), acc.V4S(), tmp.V8H());

          __ Smull2(tmp.V8H(), left.V16B(), right.V16B());
          __ Saddw(acc.V4S(), acc.V4S(), tmp.V4H());
          __ Saddw2(acc.V4S(), acc.V4S(), tmp.V8H());
        }
      }
      break;
    }
    case 2u:
      DCHECK_EQ(8u, a->GetVectorLength());
      if (instruction->IsZeroExtending()) {
        __ Umlal(acc.V4S(), left.V4H(), right.V4H());
        __ Umlal2(acc.V4S(), left.V8H(), right.V8H());
      } else {
        __ Smlal(acc.V4S(), left.V4H(), right.V4H());
        __ Smlal2(acc.V4S(), left.V8H(), right.V8H());
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type size: " << inputs_data_size;
  }
}

// Helper to set up locations for vector memory operations.
static void CreateVecMemLocations(ArenaAllocator* allocator,
                                  HVecMemoryOperation* instruction,
                                  bool is_load) {
  LocationSummary* locations = new (allocator) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(instruction->InputAt(1)));
      if (is_load) {
        locations->SetOut(Location::RequiresFpuRegister());
      } else {
        locations->SetInAt(2, Location::RequiresFpuRegister());
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecLoad(HVecLoad* instruction) {
  CreateVecMemLocations(GetGraph()->GetAllocator(), instruction, /*is_load*/ true);
}

void InstructionCodeGeneratorARM64Neon::VisitVecLoad(HVecLoad* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t size = DataType::Size(instruction->GetPackedType());
  VRegister reg = VRegisterFrom(locations->Out());
  UseScratchRegisterScope temps(GetVIXLAssembler());
  Register scratch;

  switch (instruction->GetPackedType()) {
    case DataType::Type::kInt16:  // (short) s.charAt(.) can yield HVecLoad/Int16/StringCharAt.
    case DataType::Type::kUint16:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      // Special handling of compressed/uncompressed string load.
      if (mirror::kUseStringCompression && instruction->IsStringCharAt()) {
        vixl::aarch64::Label uncompressed_load, done;
        // Test compression bit.
        static_assert(static_cast<uint32_t>(mirror::StringCompressionFlag::kCompressed) == 0u,
                      "Expecting 0=compressed, 1=uncompressed");
        uint32_t count_offset = mirror::String::CountOffset().Uint32Value();
        Register length = temps.AcquireW();
        __ Ldr(length, HeapOperand(InputRegisterAt(instruction, 0), count_offset));
        __ Tbnz(length.W(), 0, &uncompressed_load);
        temps.Release(length);  // no longer needed
        // Zero extend 8 compressed bytes into 8 chars.
        __ Ldr(DRegisterFrom(locations->Out()).V8B(),
               VecNEONAddress(instruction, &temps, 1, /*is_string_char_at*/ true, &scratch));
        __ Uxtl(reg.V8H(), reg.V8B());
        __ B(&done);
        if (scratch.IsValid()) {
          temps.Release(scratch);  // if used, no longer needed
        }
        // Load 8 direct uncompressed chars.
        __ Bind(&uncompressed_load);
        __ Ldr(reg,
               VecNEONAddress(instruction, &temps, size, /*is_string_char_at*/ true, &scratch));
        __ Bind(&done);
        return;
      }
      FALLTHROUGH_INTENDED;
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kInt32:
    case DataType::Type::kFloat32:
    case DataType::Type::kInt64:
    case DataType::Type::kFloat64:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ Ldr(reg,
             VecNEONAddress(instruction, &temps, size, instruction->IsStringCharAt(), &scratch));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecStore(HVecStore* instruction) {
  CreateVecMemLocations(GetGraph()->GetAllocator(), instruction, /*is_load*/ false);
}

void InstructionCodeGeneratorARM64Neon::VisitVecStore(HVecStore* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t size = DataType::Size(instruction->GetPackedType());
  VRegister reg = VRegisterFrom(locations->InAt(2));
  UseScratchRegisterScope temps(GetVIXLAssembler());
  Register scratch;

  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kFloat32:
    case DataType::Type::kInt64:
    case DataType::Type::kFloat64:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ Str(reg,
             VecNEONAddress(instruction, &temps, size, /*is_string_char_at*/ false, &scratch));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Neon::VisitVecPredSetAll(HVecPredSetAll* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  DCHECK(instruction->InputAt(0)->IsIntConstant());
  locations->SetInAt(0, Location::NoLocation());
  locations->SetOut(Location::NoLocation());
}

void InstructionCodeGeneratorARM64Neon::VisitVecPredSetAll(HVecPredSetAll*) {
}

void LocationsBuilderARM64Neon::VisitVecPredWhile(HVecPredWhile* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorARM64Neon::VisitVecPredWhile(HVecPredWhile* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderARM64Neon::VisitVecPredCondition(HVecPredCondition* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorARM64Neon::VisitVecPredCondition(HVecPredCondition* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
  UNREACHABLE();
}

Location InstructionCodeGeneratorARM64Neon::AllocateSIMDScratchLocation(
    vixl::aarch64::UseScratchRegisterScope* scope) {
  DCHECK_EQ(codegen_->GetSIMDRegisterWidth(), kQRegSizeInBytes);
  return LocationFrom(scope->AcquireVRegisterOfSize(kQRegSize));
}

void InstructionCodeGeneratorARM64Neon::FreeSIMDScratchLocation(Location loc,
    vixl::aarch64::UseScratchRegisterScope* scope) {
  DCHECK_EQ(codegen_->GetSIMDRegisterWidth(), kQRegSizeInBytes);
  scope->Release(QRegisterFrom(loc));
}

void InstructionCodeGeneratorARM64Neon::LoadSIMDRegFromStack(Location destination,
                                                             Location source) {
  DCHECK_EQ(codegen_->GetSIMDRegisterWidth(), kQRegSizeInBytes);
  __ Ldr(QRegisterFrom(destination), StackOperandFrom(source));
}

void InstructionCodeGeneratorARM64Neon::MoveSIMDRegToSIMDReg(Location destination,
                                                             Location source) {
  DCHECK_EQ(codegen_->GetSIMDRegisterWidth(), kQRegSizeInBytes);
  __ Mov(QRegisterFrom(destination), QRegisterFrom(source));
}

void InstructionCodeGeneratorARM64Neon::MoveToSIMDStackSlot(Location destination,
                                                            Location source) {
  DCHECK(destination.IsSIMDStackSlot());
  DCHECK_EQ(codegen_->GetSIMDRegisterWidth(), kQRegSizeInBytes);

  if (source.IsFpuRegister()) {
    __ Str(QRegisterFrom(source), StackOperandFrom(destination));
  } else {
    DCHECK(source.IsSIMDStackSlot());
    UseScratchRegisterScope temps(GetVIXLAssembler());
    if (GetVIXLAssembler()->GetScratchVRegisterList()->IsEmpty()) {
      Register temp = temps.AcquireX();
      __ Ldr(temp, MemOperand(sp, source.GetStackIndex()));
      __ Str(temp, MemOperand(sp, destination.GetStackIndex()));
      __ Ldr(temp, MemOperand(sp, source.GetStackIndex() + kArm64WordSize));
      __ Str(temp, MemOperand(sp, destination.GetStackIndex() + kArm64WordSize));
    } else {
      VRegister temp = temps.AcquireVRegisterOfSize(kQRegSize);
      __ Ldr(temp, StackOperandFrom(source));
      __ Str(temp, StackOperandFrom(destination));
    }
  }
}

// Calculate memory accessing operand for save/restore live registers.
template <bool is_save>
void SaveRestoreLiveRegistersHelperNeonImpl(CodeGeneratorARM64* codegen,
                                            LocationSummary* locations,
                                            int64_t spill_offset) {
  const uint32_t core_spills = codegen->GetSlowPathSpills(locations, /* core_registers= */ true);
  const uint32_t fp_spills = codegen->GetSlowPathSpills(locations, /* core_registers= */ false);
  DCHECK(helpers::ArtVixlRegCodeCoherentForRegSet(core_spills,
                                                  codegen->GetNumberOfCoreRegisters(),
                                                  fp_spills,
                                                  codegen->GetNumberOfFloatingPointRegisters()));

  CPURegList core_list = CPURegList(CPURegister::kRegister, kXRegSize, core_spills);
  const unsigned v_reg_size_in_bits = codegen->GetSlowPathFPWidth() * 8;
  DCHECK_LE(codegen->GetSIMDRegisterWidth(), kQRegSizeInBytes);
  CPURegList fp_list = CPURegList(CPURegister::kVRegister, v_reg_size_in_bits, fp_spills);

  MacroAssembler* masm = codegen->GetVIXLAssembler();
  UseScratchRegisterScope temps(masm);

  Register base = masm->StackPointer();
  int64_t core_spill_size = core_list.GetTotalSizeInBytes();
  int64_t fp_spill_size = fp_list.GetTotalSizeInBytes();
  int64_t reg_size = kXRegSizeInBytes;
  int64_t max_ls_pair_offset = spill_offset + core_spill_size + fp_spill_size - 2 * reg_size;
  uint32_t ls_access_size = WhichPowerOf2(reg_size);
  if (((core_list.GetCount() > 1) || (fp_list.GetCount() > 1)) &&
      !masm->IsImmLSPair(max_ls_pair_offset, ls_access_size)) {
    // If the offset does not fit in the instruction's immediate field, use an alternate register
    // to compute the base address(float point registers spill base address).
    Register new_base = temps.AcquireSameSizeAs(base);
    masm->Add(new_base, base, Operand(spill_offset + core_spill_size));
    base = new_base;
    spill_offset = -core_spill_size;
    int64_t new_max_ls_pair_offset = fp_spill_size - 2 * reg_size;
    DCHECK(masm->IsImmLSPair(spill_offset, ls_access_size));
    DCHECK(masm->IsImmLSPair(new_max_ls_pair_offset, ls_access_size));
  }

  if (is_save) {
    masm->StoreCPURegList(core_list, MemOperand(base, spill_offset));
    masm->StoreCPURegList(fp_list, MemOperand(base, spill_offset + core_spill_size));
  } else {
    masm->LoadCPURegList(core_list, MemOperand(base, spill_offset));
    masm->LoadCPURegList(fp_list, MemOperand(base, spill_offset + core_spill_size));
  }
}

void InstructionCodeGeneratorARM64Neon::SaveLiveRegistersHelper(LocationSummary* locations,
                                                                int64_t spill_offset) {
  SaveRestoreLiveRegistersHelperNeonImpl</* is_save= */ true>(codegen_, locations, spill_offset);
}

void InstructionCodeGeneratorARM64Neon::RestoreLiveRegistersHelper(LocationSummary* locations,
                                                                   int64_t spill_offset) {
  SaveRestoreLiveRegistersHelperNeonImpl</* is_save= */ false>(codegen_, locations, spill_offset);
}

#undef __

}  // namespace arm64
}  // namespace art
