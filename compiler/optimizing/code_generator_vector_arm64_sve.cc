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
using helpers::SveStackOperandFrom;
using helpers::VRegisterFrom;
using helpers::ZRegisterFrom;
using helpers::XRegisterFrom;

#define __ GetVIXLAssembler()->

// Returns whether the value of the constant can be directly encoded into the instruction as
// immediate.
static bool SVECanEncodeConstantAsImmediate(HConstant* constant, HInstruction* instr) {
  if (instr->IsVecReplicateScalar()) {
    if (constant->IsLongConstant()) {
      return false;
    } else if (constant->IsFloatConstant()) {
      return vixl::aarch64::Assembler::IsImmFP32(constant->AsFloatConstant()->GetValue());
    } else if (constant->IsDoubleConstant()) {
      return vixl::aarch64::Assembler::IsImmFP64(constant->AsDoubleConstant()->GetValue());
    }
    // TODO: Make use of shift part of DUP instruction.
    int64_t value = CodeGenerator::GetInt64ValueOf(constant);
    return IsInt<8>(value);
  }

  return false;
}

// Returns
//  - constant location - if 'constant' is an actual constant and its value can be
//    encoded into the instruction.
//  - register location otherwise.
inline Location SVEEncodableConstantOrRegister(HInstruction* constant, HInstruction* instr) {
  if (constant->IsConstant()
      && SVECanEncodeConstantAsImmediate(constant->AsConstant(), instr)) {
    return Location::ConstantLocation(constant->AsConstant());
  }

  return Location::RequiresRegister();
}

void InstructionCodeGeneratorARM64Sve::ValidateVectorLength(HVecOperation* instr) const {
  DCHECK_EQ(DataType::Size(instr->GetPackedType()) * instr->GetVectorLength(),
            codegen_->GetSIMDRegisterWidth());
}

void LocationsBuilderARM64Sve::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
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
      locations->SetInAt(0, SVEEncodableConstantOrRegister(input, instruction));
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      if (input->IsConstant() &&
          SVECanEncodeConstantAsImmediate(input->AsConstant(), instruction)) {
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

void InstructionCodeGeneratorARM64Sve::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  Location src_loc = locations->InAt(0);
  const ZRegister dst = ZRegisterFrom(locations->Out());
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      if (src_loc.IsConstant()) {
        __ Dup(dst.VnB(), Int64FromLocation(src_loc));
      } else {
        __ Dup(dst.VnB(), InputRegisterAt(instruction, 0));
      }
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      if (src_loc.IsConstant()) {
        __ Dup(dst.VnH(), Int64FromLocation(src_loc));
      } else {
        __ Dup(dst.VnH(), InputRegisterAt(instruction, 0));
      }
      break;
    case DataType::Type::kInt32:
      if (src_loc.IsConstant()) {
        __ Dup(dst.VnS(), Int64FromLocation(src_loc));
      } else {
        __ Dup(dst.VnS(), InputRegisterAt(instruction, 0));
      }
      break;
    case DataType::Type::kInt64:
      if (src_loc.IsConstant()) {
        __ Dup(dst.VnD(), Int64FromLocation(src_loc));
      } else {
        __ Dup(dst.VnD(), XRegisterFrom(src_loc));
      }
      break;
    case DataType::Type::kFloat32:
      if (src_loc.IsConstant()) {
        __ Fdup(dst.VnS(), src_loc.GetConstant()->AsFloatConstant()->GetValue());
      } else {
        __ Dup(dst.VnS(), ZRegisterFrom(src_loc).VnS(), 0);
      }
      break;
    case DataType::Type::kFloat64:
      if (src_loc.IsConstant()) {
        __ Fdup(dst.VnD(), src_loc.GetConstant()->AsDoubleConstant()->GetValue());
      } else {
        __ Dup(dst.VnD(), ZRegisterFrom(src_loc).VnD(), 0);
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecExtractScalar(HVecExtractScalar* instruction) {
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

void InstructionCodeGeneratorARM64Sve::VisitVecExtractScalar(HVecExtractScalar* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const VRegister src = VRegisterFrom(locations->InAt(0));
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kInt32:
      __ Umov(OutputRegister(instruction), src.V4S(), 0);
      break;
    case DataType::Type::kInt64:
      __ Umov(OutputRegister(instruction), src.V2D(), 0);
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
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

void LocationsBuilderARM64Sve::VisitVecReduce(HVecReduce* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecReduce(HVecReduce* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister src = ZRegisterFrom(locations->InAt(0));
  const VRegister dst = DRegisterFrom(locations->Out());
  const PRegister p_reg = LoopPReg();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kInt32:
      switch (instruction->GetReductionKind()) {
        case HVecReduce::kSum:
          __ Saddv(dst.S(), p_reg, src.VnS());
          break;
        default:
          LOG(FATAL) << "Unsupported SIMD instruction";
          UNREACHABLE();
      }
      break;
    case DataType::Type::kInt64:
      switch (instruction->GetReductionKind()) {
        case HVecReduce::kSum:
          __ Uaddv(dst.D(), p_reg, src.VnD());
          break;
        default:
          LOG(FATAL) << "Unsupported SIMD instruction";
          UNREACHABLE();
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecCnv(HVecCnv* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecCnv(HVecCnv* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister src = ZRegisterFrom(locations->InAt(0));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  DataType::Type from = instruction->GetInputType();
  DataType::Type to = instruction->GetResultType();
  ValidateVectorLength(instruction);
  if (from == DataType::Type::kInt32 && to == DataType::Type::kFloat32) {
    __ Scvtf(dst.VnS(), p_reg, src.VnS());
  } else {
    LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
  }
}

void LocationsBuilderARM64Sve::VisitVecNeg(HVecNeg* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecNeg(HVecNeg* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister src = ZRegisterFrom(locations->InAt(0));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Neg(dst.VnB(), p_reg, src.VnB());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Neg(dst.VnH(), p_reg, src.VnH());
      break;
    case DataType::Type::kInt32:
      __ Neg(dst.VnS(), p_reg, src.VnS());
      break;
    case DataType::Type::kInt64:
      __ Neg(dst.VnD(), p_reg, src.VnD());
      break;
    case DataType::Type::kFloat32:
      __ Fneg(dst.VnS(), p_reg, src.VnS());
      break;
    case DataType::Type::kFloat64:
      __ Fneg(dst.VnD(), p_reg, src.VnD());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecAbs(HVecAbs* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecAbs(HVecAbs* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister src = ZRegisterFrom(locations->InAt(0));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kInt8:
      __ Abs(dst.VnB(), p_reg, src.VnB());
      break;
    case DataType::Type::kInt16:
      __ Abs(dst.VnH(), p_reg, src.VnH());
      break;
    case DataType::Type::kInt32:
      __ Abs(dst.VnS(), p_reg, src.VnS());
      break;
    case DataType::Type::kInt64:
      __ Abs(dst.VnD(), p_reg, src.VnD());
      break;
    case DataType::Type::kFloat32:
      __ Fabs(dst.VnS(), p_reg, src.VnS());
      break;
    case DataType::Type::kFloat64:
      __ Fabs(dst.VnD(), p_reg, src.VnD());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecNot(HVecNot* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecNot(HVecNot* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister src = ZRegisterFrom(locations->InAt(0));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:  // special case boolean-not
      __ Dup(dst.VnB(), 1);
      __ Eor(dst.VnB(), p_reg, dst.VnB(), src.VnB());
      break;
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Not(dst.VnB(), p_reg, src.VnB());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Not(dst.VnH(), p_reg, src.VnH());
      break;
    case DataType::Type::kInt32:
      __ Not(dst.VnS(), p_reg, src.VnS());
      break;
    case DataType::Type::kInt64:
      __ Not(dst.VnD(), p_reg, src.VnD());
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
      locations->SetOut(Location::SameAsFirstInput());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecAdd(HVecAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecAdd(HVecAdd* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister lhs = ZRegisterFrom(locations->InAt(0));
  const ZRegister rhs = ZRegisterFrom(locations->InAt(1));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Add(dst.VnB(), p_reg, lhs.VnB(), rhs.VnB());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Add(dst.VnH(), p_reg, lhs.VnH(), rhs.VnH());
      break;
    case DataType::Type::kInt32:
      __ Add(dst.VnS(), p_reg, lhs.VnS(), rhs.VnS());
      break;
    case DataType::Type::kInt64:
      __ Add(dst.VnD(), p_reg, lhs.VnD(), rhs.VnD());
      break;
    case DataType::Type::kFloat32:
      __ Fadd(dst.VnS(), p_reg, lhs.VnS(), rhs.VnS(), StrictNaNPropagation);
      break;
    case DataType::Type::kFloat64:
      __ Fadd(dst.VnD(), p_reg, lhs.VnD(), rhs.VnD(), StrictNaNPropagation);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecSaturationAdd(HVecSaturationAdd* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorARM64Sve::VisitVecSaturationAdd(HVecSaturationAdd* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderARM64Sve::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorARM64Sve::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderARM64Sve::VisitVecSub(HVecSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecSub(HVecSub* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister lhs = ZRegisterFrom(locations->InAt(0));
  const ZRegister rhs = ZRegisterFrom(locations->InAt(1));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Sub(dst.VnB(), p_reg, lhs.VnB(), rhs.VnB());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Sub(dst.VnH(), p_reg, lhs.VnH(), rhs.VnH());
      break;
    case DataType::Type::kInt32:
      __ Sub(dst.VnS(), p_reg, lhs.VnS(), rhs.VnS());
      break;
    case DataType::Type::kInt64:
      __ Sub(dst.VnD(), p_reg, lhs.VnD(), rhs.VnD());
      break;
    case DataType::Type::kFloat32:
      __ Fsub(dst.VnS(), p_reg, lhs.VnS(), rhs.VnS());
      break;
    case DataType::Type::kFloat64:
      __ Fsub(dst.VnD(), p_reg, lhs.VnD(), rhs.VnD());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecSaturationSub(HVecSaturationSub* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorARM64Sve::VisitVecSaturationSub(HVecSaturationSub* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderARM64Sve::VisitVecMul(HVecMul* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecMul(HVecMul* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister lhs = ZRegisterFrom(locations->InAt(0));
  const ZRegister rhs = ZRegisterFrom(locations->InAt(1));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Mul(dst.VnB(), p_reg, lhs.VnB(), rhs.VnB());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Mul(dst.VnH(), p_reg, lhs.VnH(), rhs.VnH());
      break;
    case DataType::Type::kInt32:
      __ Mul(dst.VnS(), p_reg, lhs.VnS(), rhs.VnS());
      break;
    case DataType::Type::kInt64:
      __ Mul(dst.VnD(), p_reg, lhs.VnD(), rhs.VnD());
      break;
    case DataType::Type::kFloat32:
      __ Fmul(dst.VnS(), p_reg, lhs.VnS(), rhs.VnS(), StrictNaNPropagation);
      break;
    case DataType::Type::kFloat64:
      __ Fmul(dst.VnD(), p_reg, lhs.VnD(), rhs.VnD(), StrictNaNPropagation);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecDiv(HVecDiv* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecDiv(HVecDiv* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister lhs = ZRegisterFrom(locations->InAt(0));
  const ZRegister rhs = ZRegisterFrom(locations->InAt(1));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  ValidateVectorLength(instruction);

  // Note: VIXL guarantees StrictNaNPropagation for Fdiv.
  switch (instruction->GetPackedType()) {
    case DataType::Type::kFloat32:
      __ Fdiv(dst.VnS(), p_reg, lhs.VnS(), rhs.VnS());
      break;
    case DataType::Type::kFloat64:
      __ Fdiv(dst.VnD(), p_reg, lhs.VnD(), rhs.VnD());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecMin(HVecMin* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorARM64Sve::VisitVecMin(HVecMin* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderARM64Sve::VisitVecMax(HVecMax* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorARM64Sve::VisitVecMax(HVecMax* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderARM64Sve::VisitVecAnd(HVecAnd* instruction) {
  // TODO: Allow constants supported by BIC (vector, immediate).
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecAnd(HVecAnd* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister lhs = ZRegisterFrom(locations->InAt(0));
  const ZRegister rhs = ZRegisterFrom(locations->InAt(1));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ And(dst.VnB(), p_reg, lhs.VnB(), rhs.VnB());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ And(dst.VnH(), p_reg, lhs.VnH(), rhs.VnH());
      break;
    case DataType::Type::kInt32:
    case DataType::Type::kFloat32:
      __ And(dst.VnS(), p_reg, lhs.VnS(), rhs.VnS());
      break;
    case DataType::Type::kInt64:
    case DataType::Type::kFloat64:
      __ And(dst.VnD(), p_reg, lhs.VnD(), rhs.VnD());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecAndNot(HVecAndNot* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
}

void InstructionCodeGeneratorARM64Sve::VisitVecAndNot(HVecAndNot* instruction) {
  // TODO: Use BIC (vector, register).
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
}

void LocationsBuilderARM64Sve::VisitVecOr(HVecOr* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecOr(HVecOr* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister lhs = ZRegisterFrom(locations->InAt(0));
  const ZRegister rhs = ZRegisterFrom(locations->InAt(1));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Orr(dst.VnB(), p_reg, lhs.VnB(), rhs.VnB());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Orr(dst.VnH(), p_reg, lhs.VnH(), rhs.VnH());
      break;
    case DataType::Type::kInt32:
    case DataType::Type::kFloat32:
      __ Orr(dst.VnS(), p_reg, lhs.VnS(), rhs.VnS());
      break;
    case DataType::Type::kInt64:
    case DataType::Type::kFloat64:
      __ Orr(dst.VnD(), p_reg, lhs.VnD(), rhs.VnD());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecXor(HVecXor* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecXor(HVecXor* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister lhs = ZRegisterFrom(locations->InAt(0));
  const ZRegister rhs = ZRegisterFrom(locations->InAt(1));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Eor(dst.VnB(), p_reg, lhs.VnB(), rhs.VnB());
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Eor(dst.VnH(), p_reg, lhs.VnH(), rhs.VnH());
      break;
    case DataType::Type::kInt32:
    case DataType::Type::kFloat32:
      __ Eor(dst.VnS(), p_reg, lhs.VnS(), rhs.VnS());
      break;
    case DataType::Type::kInt64:
    case DataType::Type::kFloat64:
      __ Eor(dst.VnD(), p_reg, lhs.VnD(), rhs.VnD());
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

void LocationsBuilderARM64Sve::VisitVecShl(HVecShl* instruction) {
  CreateVecShiftLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecShl(HVecShl* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister lhs = ZRegisterFrom(locations->InAt(0));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Lsl(dst.VnB(), p_reg, lhs.VnB(), value);
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Lsl(dst.VnH(), p_reg, lhs.VnH(), value);
      break;
    case DataType::Type::kInt32:
      __ Lsl(dst.VnS(), p_reg, lhs.VnS(), value);
      break;
    case DataType::Type::kInt64:
      __ Lsl(dst.VnD(), p_reg, lhs.VnD(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecShr(HVecShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecShr(HVecShr* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister lhs = ZRegisterFrom(locations->InAt(0));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Asr(dst.VnB(), p_reg, lhs.VnB(), value);
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Asr(dst.VnH(), p_reg, lhs.VnH(), value);
      break;
    case DataType::Type::kInt32:
      __ Asr(dst.VnS(), p_reg, lhs.VnS(), value);
      break;
    case DataType::Type::kInt64:
      __ Asr(dst.VnD(), p_reg, lhs.VnD(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecUShr(HVecUShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetAllocator(), instruction);
}

void InstructionCodeGeneratorARM64Sve::VisitVecUShr(HVecUShr* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister lhs = ZRegisterFrom(locations->InAt(0));
  const ZRegister dst = ZRegisterFrom(locations->Out());
  const PRegisterM p_reg = LoopPReg().Merging();
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  ValidateVectorLength(instruction);
  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Lsr(dst.VnB(), p_reg, lhs.VnB(), value);
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Lsr(dst.VnH(), p_reg, lhs.VnH(), value);
      break;
    case DataType::Type::kInt32:
      __ Lsr(dst.VnS(), p_reg, lhs.VnS(), value);
      break;
    case DataType::Type::kInt64:
      __ Lsr(dst.VnD(), p_reg, lhs.VnD(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecSetScalars(HVecSetScalars* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);

  DCHECK_EQ(2u, instruction->InputCount());  // only one input currently implemented + predicate.

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

void InstructionCodeGeneratorARM64Sve::VisitVecSetScalars(HVecSetScalars* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister z_dst = ZRegisterFrom(locations->Out());

  DCHECK_EQ(2u, instruction->InputCount());  // only one input currently implemented + predicate.

  // Zero out all other elements first.
  __ Dup(z_dst.VnB(), 0);

  const VRegister dst = VRegisterFrom(locations->Out());
  // Shorthand for any type of zero.
  if (IsZeroBitPattern(instruction->InputAt(0))) {
    return;
  }
  ValidateVectorLength(instruction);

  // Set required elements.
  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Mov(dst.V16B(), 0, InputRegisterAt(instruction, 0));
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Mov(dst.V8H(), 0, InputRegisterAt(instruction, 0));
      break;
    case DataType::Type::kInt32:
      __ Mov(dst.V4S(), 0, InputRegisterAt(instruction, 0));
      break;
    case DataType::Type::kInt64:
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

void LocationsBuilderARM64Sve::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instruction) {
  CreateVecAccumLocations(GetGraph()->GetAllocator(), instruction);
}

// Some early revisions of the Cortex-A53 have an erratum (835769) whereby it is possible for a
// 64-bit scalar multiply-accumulate instruction in AArch64 state to generate an incorrect result.
// However vector MultiplyAccumulate instruction is not affected.
void InstructionCodeGeneratorARM64Sve::VisitVecMultiplyAccumulate(
    HVecMultiplyAccumulate* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  const ZRegister acc = ZRegisterFrom(locations->InAt(0));
  const ZRegister left = ZRegisterFrom(locations->InAt(1));
  const ZRegister right = ZRegisterFrom(locations->InAt(2));
  const PRegisterM p_reg = LoopPReg().Merging();

  DCHECK(locations->InAt(0).Equals(locations->Out()));
  ValidateVectorLength(instruction);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      if (instruction->GetOpKind() == HInstruction::kAdd) {
        __ Mla(acc.VnB(), p_reg, acc.VnB(), left.VnB(), right.VnB());
      } else {
        __ Mls(acc.VnB(), p_reg, acc.VnB(), left.VnB(), right.VnB());
      }
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      if (instruction->GetOpKind() == HInstruction::kAdd) {
        __ Mla(acc.VnH(), p_reg, acc.VnB(), left.VnH(), right.VnH());
      } else {
        __ Mls(acc.VnH(), p_reg, acc.VnB(), left.VnH(), right.VnH());
      }
      break;
    case DataType::Type::kInt32:
      if (instruction->GetOpKind() == HInstruction::kAdd) {
        __ Mla(acc.VnS(), p_reg, acc.VnB(), left.VnS(), right.VnS());
      } else {
        __ Mls(acc.VnS(), p_reg, acc.VnB(), left.VnS(), right.VnS());
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecSADAccumulate(HVecSADAccumulate* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void InstructionCodeGeneratorARM64Sve::VisitVecSADAccumulate(HVecSADAccumulate* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
  UNREACHABLE();
}

void LocationsBuilderARM64Sve::VisitVecDotProd(HVecDotProd* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  DCHECK(instruction->GetPackedType() == DataType::Type::kInt32);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetInAt(1, Location::RequiresFpuRegister());
  locations->SetInAt(2, Location::RequiresFpuRegister());
  locations->SetOut(Location::SameAsFirstInput());

  locations->AddTemp(Location::RequiresFpuRegister());
}

void InstructionCodeGeneratorARM64Sve::VisitVecDotProd(HVecDotProd* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  const ZRegister acc = ZRegisterFrom(locations->InAt(0));
  const ZRegister left = ZRegisterFrom(locations->InAt(1));
  const ZRegister right = ZRegisterFrom(locations->InAt(2));
  const PRegisterM p_reg = LoopPReg().Merging();
  HVecOperation* a = instruction->InputAt(1)->AsVecOperation();
  HVecOperation* b = instruction->InputAt(2)->AsVecOperation();
  DCHECK_EQ(HVecOperation::ToSignedType(a->GetPackedType()),
            HVecOperation::ToSignedType(b->GetPackedType()));
  DCHECK_EQ(instruction->GetPackedType(), DataType::Type::kInt32);
  ValidateVectorLength(instruction);

  size_t inputs_data_size = DataType::Size(a->GetPackedType());
  switch (inputs_data_size) {
    case 1u: {
      UseScratchRegisterScope temps(GetVIXLAssembler());
      const ZRegister tmp0 = temps.AcquireZ();
      const ZRegister tmp1 = ZRegisterFrom(locations->GetTemp(0));

      __ Dup(tmp1.VnB(), 0u);
      __ Sel(tmp0.VnB(), p_reg, left.VnB(), tmp1.VnB());
      __ Sel(tmp1.VnB(), p_reg, right.VnB(), tmp1.VnB());
      if (instruction->IsZeroExtending()) {
        __ Udot(acc.VnS(), acc.VnS(), tmp0.VnB(), tmp1.VnB());
      } else {
        __ Sdot(acc.VnS(), acc.VnS(), tmp0.VnB(), tmp1.VnB());
      }
      break;
    }
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

void LocationsBuilderARM64Sve::VisitVecLoad(HVecLoad* instruction) {
  CreateVecMemLocations(GetGraph()->GetAllocator(), instruction, /*is_load*/ true);
}

void InstructionCodeGeneratorARM64Sve::VisitVecLoad(HVecLoad* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  size_t size = DataType::Size(instruction->GetPackedType());
  const ZRegister reg = ZRegisterFrom(locations->Out());
  UseScratchRegisterScope temps(GetVIXLAssembler());
  Register scratch;
  const PRegisterZ p_reg = LoopPReg().Zeroing();
  ValidateVectorLength(instruction);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kInt16:  // (short) s.charAt(.) can yield HVecLoad/Int16/StringCharAt.
    case DataType::Type::kUint16:
      __ Ld1h(reg.VnH(), p_reg,
              VecSVEAddress(instruction, &temps, size, /*is_string_char_at*/ false, &scratch));
      break;
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Ld1b(reg.VnB(), p_reg,
              VecSVEAddress(instruction, &temps, size, /*is_string_char_at*/ false, &scratch));
      break;
    case DataType::Type::kInt32:
    case DataType::Type::kFloat32:
      __ Ld1w(reg.VnS(), p_reg,
              VecSVEAddress(instruction, &temps, size, /*is_string_char_at*/ false, &scratch));
      break;
    case DataType::Type::kInt64:
    case DataType::Type::kFloat64:
      __ Ld1d(reg.VnD(), p_reg,
              VecSVEAddress(instruction, &temps, size, /*is_string_char_at*/ false, &scratch));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecStore(HVecStore* instruction) {
  CreateVecMemLocations(GetGraph()->GetAllocator(), instruction, /*is_load*/ false);
}

void InstructionCodeGeneratorARM64Sve::VisitVecStore(HVecStore* instruction) {
  DCHECK(instruction->IsPredicated());
  LocationSummary* locations = instruction->GetLocations();
  size_t size = DataType::Size(instruction->GetPackedType());
  const ZRegister reg = ZRegisterFrom(locations->InAt(2));
  UseScratchRegisterScope temps(GetVIXLAssembler());
  Register scratch;
  const PRegisterZ p_reg = LoopPReg().Zeroing();
  ValidateVectorLength(instruction);

  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ St1b(reg.VnB(), p_reg,
          VecSVEAddress(instruction, &temps, size, /*is_string_char_at*/ false, &scratch));
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ St1h(reg.VnH(), p_reg,
          VecSVEAddress(instruction, &temps, size, /*is_string_char_at*/ false, &scratch));
      break;
    case DataType::Type::kInt32:
    case DataType::Type::kFloat32:
      __ St1w(reg.VnS(), p_reg,
          VecSVEAddress(instruction, &temps, size, /*is_string_char_at*/ false, &scratch));
      break;
    case DataType::Type::kInt64:
    case DataType::Type::kFloat64:
      __ St1d(reg.VnD(), p_reg,
          VecSVEAddress(instruction, &temps, size, /*is_string_char_at*/ false, &scratch));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecPredSetAll(HVecPredSetAll* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  DCHECK(instruction->InputAt(0)->IsIntConstant());
  locations->SetInAt(0, Location::NoLocation());
  locations->SetOut(Location::NoLocation());
}

void InstructionCodeGeneratorARM64Sve::VisitVecPredSetAll(HVecPredSetAll* instruction) {
  // Instruction is not predicated, see nodes_vector.h
  DCHECK(!instruction->IsPredicated());
  const PRegister p_reg = LoopPReg();

  switch (instruction->GetPackedType()) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
      __ Ptrue(p_reg.VnB(), vixl::aarch64::SVE_ALL);
      break;
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
      __ Ptrue(p_reg.VnH(), vixl::aarch64::SVE_ALL);
      break;
    case DataType::Type::kInt32:
    case DataType::Type::kFloat32:
      __ Ptrue(p_reg.VnS(), vixl::aarch64::SVE_ALL);
      break;
    case DataType::Type::kInt64:
    case DataType::Type::kFloat64:
      __ Ptrue(p_reg.VnD(), vixl::aarch64::SVE_ALL);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecPredWhile(HVecPredWhile* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  // The instruction doesn't really need a core register as out location; this is a hack
  // to workaround absence of support for vector predicates in register allocation.
  //
  // Semantically, the out location of this instruction and predicate inputs locations of
  // its users should be a fixed predicate register (similar to
  // Location::RegisterLocation(int reg)). But the register allocator (RA) doesn't support
  // SIMD regs (e.g. predicate), so LoopPReg() is used explicitly without exposing it
  // to the RA.
  //
  // To make the RA happy Location::NoLocation() was used for all the vector instructions
  // predicate inputs; but for the PredSetOperations (e.g. VecPredWhile) Location::NoLocation()
  // can't be used without changes to RA - "ssa_liveness_analysis.cc] Check failed:
  // input->IsEmittedAtUseSite()" would fire.
  //
  // Using a core register as a hack is the easiest way to tackle this problem. The RA will
  // block one core register for the loop without actually using it; this should not be
  // a performance issue as a SIMD loop operates mainly on SIMD registers.
  //
  // TODO: Support SIMD types in register allocator.
  locations->SetOut(Location::RequiresRegister());
}

void InstructionCodeGeneratorARM64Sve::VisitVecPredWhile(HVecPredWhile* instruction) {
  // Instruction is not predicated, see nodes_vector.h
  DCHECK(!instruction->IsPredicated());
  // Current implementation of predicated loop execution only supports kLO condition.
  DCHECK(instruction->GetCondKind() == HVecPredWhile::CondKind::kLO);
  Register left = InputRegisterAt(instruction, 0);
  Register right = InputRegisterAt(instruction, 1);

  DCHECK_EQ(codegen_->GetSIMDRegisterWidth() % instruction->GetVectorLength(), 0u);

  switch (codegen_->GetSIMDRegisterWidth() / instruction->GetVectorLength()) {
    case 1u:
      __ Whilelo(LoopPReg().VnB(), left, right);
      break;
    case 2u:
      __ Whilelo(LoopPReg().VnH(), left, right);
      break;
    case 4u:
      __ Whilelo(LoopPReg().VnS(), left, right);
      break;
    case 8u:
      __ Whilelo(LoopPReg().VnD(), left, right);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type: " << instruction->GetPackedType();
      UNREACHABLE();
  }
}

void LocationsBuilderARM64Sve::VisitVecPredCondition(HVecPredCondition* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  locations->SetInAt(0, Location::NoLocation());
  // Result of the operation - a boolean value in a core register.
  locations->SetOut(Location::RequiresRegister());
}

void InstructionCodeGeneratorARM64Sve::VisitVecPredCondition(HVecPredCondition* instruction) {
  // Instruction is not predicated, see nodes_vector.h
  DCHECK(!instruction->IsPredicated());
  Register reg = OutputRegister(instruction);
  // Currently VecPredCondition is only used as part of vectorized loop check condition
  // evaluation.
  DCHECK(instruction->GetPCondKind() == HVecPredCondition::PCondKind::kNFirst);
  __ Cset(reg, pl);
}

Location InstructionCodeGeneratorARM64Sve::AllocateSIMDScratchLocation(
    vixl::aarch64::UseScratchRegisterScope* scope) {
  return LocationFrom(scope->AcquireZ());
}

void InstructionCodeGeneratorARM64Sve::FreeSIMDScratchLocation(Location loc,
    vixl::aarch64::UseScratchRegisterScope* scope) {
  scope->Release(ZRegisterFrom(loc));
}

void InstructionCodeGeneratorARM64Sve::LoadSIMDRegFromStack(Location destination,
                                                            Location source) {
  __ Ldr(ZRegisterFrom(destination), SveStackOperandFrom(source));
}

void InstructionCodeGeneratorARM64Sve::MoveSIMDRegToSIMDReg(Location destination,
                                                            Location source) {
  __ Mov(ZRegisterFrom(destination), ZRegisterFrom(source));
}

void InstructionCodeGeneratorARM64Sve::MoveToSIMDStackSlot(Location destination,
                                                           Location source) {
  DCHECK(destination.IsSIMDStackSlot());

  if (source.IsFpuRegister()) {
    __ Str(ZRegisterFrom(source), SveStackOperandFrom(destination));
  } else {
    DCHECK(source.IsSIMDStackSlot());
    UseScratchRegisterScope temps(GetVIXLAssembler());
    if (GetVIXLAssembler()->GetScratchVRegisterList()->IsEmpty()) {
      // Very rare situation, only when there are cycles in ParallelMoveResolver graph.
      const Register temp = temps.AcquireX();
      DCHECK_EQ(codegen_->GetSIMDRegisterWidth() % kArm64WordSize, 0u);
      // Emit a number of LDR/STR (XRegister, 64-bit) to cover the whole SIMD register size
      // when copying a stack slot.
      for (size_t offset = 0, e = codegen_->GetSIMDRegisterWidth();
           offset < e;
           offset += kArm64WordSize) {
        __ Ldr(temp, MemOperand(sp, source.GetStackIndex() + offset));
        __ Str(temp, MemOperand(sp, destination.GetStackIndex() + offset));
      }
    } else {
      const ZRegister temp = temps.AcquireZ();
      __ Ldr(temp, SveStackOperandFrom(source));
      __ Str(temp, SveStackOperandFrom(destination));
    }
  }
}

template <bool is_save>
void SaveRestoreLiveRegistersHelperSveImpl(CodeGeneratorARM64* codegen,
                                           LocationSummary* locations,
                                           int64_t spill_offset) {
  const uint32_t core_spills = codegen->GetSlowPathSpills(locations, /* core_registers= */ true);
  const uint32_t fp_spills = codegen->GetSlowPathSpills(locations, /* core_registers= */ false);
  DCHECK(helpers::ArtVixlRegCodeCoherentForRegSet(core_spills,
                                                  codegen->GetNumberOfCoreRegisters(),
                                                  fp_spills,
                                                  codegen->GetNumberOfFloatingPointRegisters()));
  MacroAssembler* masm = codegen->GetVIXLAssembler();
  Register base = masm->StackPointer();

  CPURegList core_list = CPURegList(CPURegister::kRegister, kXRegSize, core_spills);
  int64_t core_spill_size = core_list.GetTotalSizeInBytes();
  int64_t fp_spill_offset = spill_offset + core_spill_size;

  if (codegen->GetGraph()->HasSIMD()) {
    if (is_save) {
      masm->StoreCPURegList(core_list, MemOperand(base, spill_offset));
    } else {
      masm->LoadCPURegList(core_list, MemOperand(base, spill_offset));
    }
    codegen->GetAssembler()->SaveRestoreZRegisterList<is_save>(fp_spills, fp_spill_offset);
    return;
  }

  // Case when we only need to restore D-registers.
  DCHECK(!codegen->GetGraph()->HasSIMD());
  DCHECK_LE(codegen->GetSlowPathFPWidth(), kDRegSizeInBytes);
  CPURegList fp_list = CPURegList(CPURegister::kVRegister, kDRegSize, fp_spills);
  if (is_save) {
    masm->StoreCPURegList(core_list, MemOperand(base, spill_offset));
    masm->StoreCPURegList(fp_list, MemOperand(base, fp_spill_offset));
  } else {
    masm->LoadCPURegList(core_list, MemOperand(base, spill_offset));
    masm->LoadCPURegList(fp_list, MemOperand(base, fp_spill_offset));
  }
}

void InstructionCodeGeneratorARM64Sve::SaveLiveRegistersHelper(LocationSummary* locations,
                                                               int64_t spill_offset) {
  SaveRestoreLiveRegistersHelperSveImpl</* is_save= */ true>(codegen_, locations, spill_offset);
}

void InstructionCodeGeneratorARM64Sve::RestoreLiveRegistersHelper(LocationSummary* locations,
                                                                  int64_t spill_offset) {
  SaveRestoreLiveRegistersHelperSveImpl</* is_save= */ false>(codegen_, locations, spill_offset);
}

#undef __

}  // namespace arm64
}  // namespace art
