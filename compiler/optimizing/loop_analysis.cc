/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "loop_analysis.h"

#include "base/bit_vector-inl.h"
#include "induction_var_range.h"

namespace art {

void LoopAnalysis::CalculateLoopBasicProperties(HLoopInformation* loop_info,
                                                LoopAnalysisInfo* analysis_results,
                                                int64_t trip_count) {
  analysis_results->trip_count_ = trip_count;

  for (HBlocksInLoopIterator block_it(*loop_info);
       !block_it.Done();
       block_it.Advance()) {
    HBasicBlock* block = block_it.Current();

    // Check whether one of the successor is loop exit.
    for (HBasicBlock* successor : block->GetSuccessors()) {
      if (!loop_info->Contains(*successor)) {
        analysis_results->exits_num_++;

        // We track number of invariant loop exits which correspond to HIf instruction and
        // can be eliminated by loop peeling; other control flow instruction are ignored and will
        // not cause loop peeling to happen as they either cannot be inside a loop, or by
        // definition cannot be loop exits (unconditional instructions), or are not beneficial for
        // the optimization.
        HIf* hif = block->GetLastInstruction()->AsIf();
        if (hif != nullptr && !loop_info->Contains(*hif->InputAt(0)->GetBlock())) {
          analysis_results->invariant_exits_num_++;
        }
      }
    }

    for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
      HInstruction* instruction = it.Current();
      if (it.Current()->GetType() == DataType::Type::kInt64) {
        analysis_results->has_long_type_instructions_ = true;
      }
      if (MakesScalarPeelingUnrollingNonBeneficial(instruction)) {
        analysis_results->has_instructions_preventing_scalar_peeling_ = true;
        analysis_results->has_instructions_preventing_scalar_unrolling_ = true;
      }
      analysis_results->instr_num_++;
    }
    analysis_results->bb_num_++;
  }
}

int64_t LoopAnalysis::GetLoopTripCount(HLoopInformation* loop_info,
                                       const InductionVarRange* induction_range) {
  int64_t trip_count;
  if (!induction_range->HasKnownTripCount(loop_info, &trip_count)) {
    trip_count = LoopAnalysisInfo::kUnknownTripCount;
  }
  return trip_count;
}

// Default implementation of loop helper; used for all targets unless a custom implementation
// is provided. Enables scalar loop peeling and unrolling with the most conservative heuristics.
class ArchDefaultLoopHelper : public ArchNoOptsLoopHelper {
 public:
  // Scalar loop unrolling parameters and heuristics.
  //
  // Maximum possible unrolling factor.
  static constexpr uint32_t kScalarMaxUnrollFactor = 2;
  // Loop's maximum instruction count. Loops with higher count will not be peeled/unrolled.
  static constexpr uint32_t kScalarHeuristicMaxBodySizeInstr = 17;
  // Loop's maximum basic block count. Loops with higher count will not be peeled/unrolled.
  static constexpr uint32_t kScalarHeuristicMaxBodySizeBlocks = 6;
  // Maximum number of instructions to be created as a result of full unrolling.
  static constexpr uint32_t kScalarHeuristicFullyUnrolledMaxInstrThreshold = 35;

  bool IsLoopNonBeneficialForScalarOpts(LoopAnalysisInfo* analysis_info) const override {
    return analysis_info->HasLongTypeInstructions() ||
           IsLoopTooBig(analysis_info,
                        kScalarHeuristicMaxBodySizeInstr,
                        kScalarHeuristicMaxBodySizeBlocks);
  }

  uint32_t GetScalarUnrollingFactor(const LoopAnalysisInfo* analysis_info) const override {
    int64_t trip_count = analysis_info->GetTripCount();
    // Unroll only loops with known trip count.
    if (trip_count == LoopAnalysisInfo::kUnknownTripCount) {
      return LoopAnalysisInfo::kNoUnrollingFactor;
    }
    uint32_t desired_unrolling_factor = kScalarMaxUnrollFactor;
    if (trip_count < desired_unrolling_factor || trip_count % desired_unrolling_factor != 0) {
      return LoopAnalysisInfo::kNoUnrollingFactor;
    }

    return desired_unrolling_factor;
  }

  bool IsLoopPeelingEnabled() const override { return true; }

  bool IsFullUnrollingBeneficial(LoopAnalysisInfo* analysis_info) const override {
    int64_t trip_count = analysis_info->GetTripCount();
    // We assume that trip count is known.
    DCHECK_NE(trip_count, LoopAnalysisInfo::kUnknownTripCount);
    size_t instr_num = analysis_info->GetNumberOfInstructions();
    return (trip_count * instr_num < kScalarHeuristicFullyUnrolledMaxInstrThreshold);
  }

 protected:
  bool IsLoopTooBig(LoopAnalysisInfo* loop_analysis_info,
                    size_t instr_threshold,
                    size_t bb_threshold) const {
    size_t instr_num = loop_analysis_info->GetNumberOfInstructions();
    size_t bb_num = loop_analysis_info->GetNumberOfBasicBlocks();
    return (instr_num >= instr_threshold || bb_num >= bb_threshold);
  }
};

// Custom implementation of loop helper for arm64 target. Enables heuristics for scalar loop
// peeling and unrolling and supports SIMD loop unrolling.
class Arm64LoopHelper : public ArchDefaultLoopHelper {
 public:
  // SIMD loop unrolling parameters and heuristics.
  //
  // Maximum possible unrolling factor.
  static constexpr uint32_t kArm64SimdMaxUnrollFactor = 8;
  // Loop's maximum instruction count. Loops with higher count will not be unrolled.
  static constexpr uint32_t kArm64SimdHeuristicMaxBodySizeInstr = 50;

  // Loop's maximum instruction count. Loops with higher count will not be peeled/unrolled.
  static constexpr uint32_t kArm64ScalarHeuristicMaxBodySizeInstr = 40;
  // Loop's maximum basic block count. Loops with higher count will not be peeled/unrolled.
  static constexpr uint32_t kArm64ScalarHeuristicMaxBodySizeBlocks = 8;

  bool IsLoopNonBeneficialForScalarOpts(LoopAnalysisInfo* loop_analysis_info) const override {
    return IsLoopTooBig(loop_analysis_info,
                        kArm64ScalarHeuristicMaxBodySizeInstr,
                        kArm64ScalarHeuristicMaxBodySizeBlocks);
  }

  uint32_t GetSIMDUnrollingFactor(HBasicBlock* block,
                                  int64_t trip_count,
                                  uint32_t max_peel,
                                  uint32_t vector_length) const override {
    // Don't unroll with insufficient iterations.
    // TODO: Unroll loops with unknown trip count.
    DCHECK_NE(vector_length, 0u);
    if (trip_count < (2 * vector_length + max_peel)) {
      return LoopAnalysisInfo::kNoUnrollingFactor;
    }
    // Don't unroll for large loop body size.
    uint32_t instruction_count = block->GetInstructions().CountSize();
    if (instruction_count >= kArm64SimdHeuristicMaxBodySizeInstr) {
      return LoopAnalysisInfo::kNoUnrollingFactor;
    }
    // Find a beneficial unroll factor with the following restrictions:
    //  - At least one iteration of the transformed loop should be executed.
    //  - The loop body shouldn't be "too big" (heuristic).

    uint32_t uf1 = kArm64SimdHeuristicMaxBodySizeInstr / instruction_count;
    uint32_t uf2 = (trip_count - max_peel) / vector_length;
    uint32_t unroll_factor =
        TruncToPowerOfTwo(std::min({uf1, uf2, kArm64SimdMaxUnrollFactor}));
    DCHECK_GE(unroll_factor, 1u);
    return unroll_factor;
  }
};

// Custom implementation of loop helper for X86_64 target. Enables heuristics for scalar loop
// peeling and unrolling and supports SIMD loop unrolling.
class X86_64LoopHelper : public ArchDefaultLoopHelper {
  // mapping of machine instruction count for most used IR instructions
  // Few IRs generate different number of instructions based on input and result type.
  // We checked top java apps, benchmarks and used the most generated instruction count.
  uint32_t GetMachineInstructionCount(HInstruction* inst) const {
    switch (inst->GetKind()) {
      case HInstruction::InstructionKind::kAbs:
        return 3;
      case HInstruction::InstructionKind::kAdd:
        return 1;
      case HInstruction::InstructionKind::kAnd:
        return 1;
      case HInstruction::InstructionKind::kArrayLength:
        return 1;
      case HInstruction::InstructionKind::kArrayGet:
        return 1;
      case HInstruction::InstructionKind::kArraySet:
        return 1;
      case HInstruction::InstructionKind::kBoundsCheck:
        return 2;
      case HInstruction::InstructionKind::kCheckCast:
        return 9;
      case HInstruction::InstructionKind::kDiv:
        return 8;
      case HInstruction::InstructionKind::kDivZeroCheck:
        return 2;
      case HInstruction::InstructionKind::kEqual:
        return 3;
      case HInstruction::InstructionKind::kGreaterThan:
        return 3;
      case HInstruction::InstructionKind::kGreaterThanOrEqual:
        return 3;
      case HInstruction::InstructionKind::kIf:
        return 2;
      case HInstruction::InstructionKind::kInstanceFieldGet:
        return 2;
      case HInstruction::InstructionKind::kInstanceFieldSet:
        return 1;
      case HInstruction::InstructionKind::kLessThan:
        return 3;
      case HInstruction::InstructionKind::kLessThanOrEqual:
        return 3;
      case HInstruction::InstructionKind::kMax:
        return 2;
      case HInstruction::InstructionKind::kMin:
        return 2;
      case HInstruction::InstructionKind::kMul:
        return 1;
      case HInstruction::InstructionKind::kNotEqual:
        return 3;
      case HInstruction::InstructionKind::kOr:
        return 1;
      case HInstruction::InstructionKind::kRem:
        return 11;
      case HInstruction::InstructionKind::kSelect:
        return 2;
      case HInstruction::InstructionKind::kShl:
        return 1;
      case HInstruction::InstructionKind::kShr:
        return 1;
      case HInstruction::InstructionKind::kSub:
        return 1;
      case HInstruction::InstructionKind::kTypeConversion:
        return 1;
      case HInstruction::InstructionKind::kUShr:
        return 1;
      case HInstruction::InstructionKind::kVecReplicateScalar:
        return 2;
      case HInstruction::InstructionKind::kVecExtractScalar:
       return 1;
      case HInstruction::InstructionKind::kVecReduce:
        return 4;
      case HInstruction::InstructionKind::kVecNeg:
        return 2;
      case HInstruction::InstructionKind::kVecAbs:
        return 4;
      case HInstruction::InstructionKind::kVecNot:
        return 3;
      case HInstruction::InstructionKind::kVecAdd:
        return 1;
      case HInstruction::InstructionKind::kVecSub:
        return 1;
      case HInstruction::InstructionKind::kVecMul:
        return 1;
      case HInstruction::InstructionKind::kVecDiv:
        return 1;
      case HInstruction::InstructionKind::kVecMax:
        return 1;
      case HInstruction::InstructionKind::kVecMin:
        return 1;
      case HInstruction::InstructionKind::kVecOr:
        return 1;
      case HInstruction::InstructionKind::kVecXor:
        return 1;
      case HInstruction::InstructionKind::kVecShl:
        return 1;
      case HInstruction::InstructionKind::kVecShr:
        return 1;
      case HInstruction::InstructionKind::kVecLoad:
        return 1;
      case HInstruction::InstructionKind::kVecStore:
        return 1;
      case HInstruction::InstructionKind::kXor:
        return 1;
      default:
        return 1;
    }
  }

  // Maximum possible unrolling factor.
  static constexpr uint32_t kX86_64MaxUnrollFactor = 2;  // pow(2,2) = 4

  // According to Intel® 64 and IA-32 Architectures Optimization Reference Manual,
  // avoid excessive loop unrolling to ensure LSD (loop stream decoder) is operating efficiently.
  // This variable takes care that unrolled loop instructions should not exceed LSD size.
  // For Intel Atom processors (silvermont & goldmont), LSD size is 28
  // TODO - identify architecture and LSD size at runtime
  static constexpr uint32_t kX86_64UnrolledMaxBodySizeInstr = 28;

  // Loop's maximum basic block count. Loops with higher count will not be partial
  // unrolled (unknown iterations).
  static constexpr uint32_t kX86_64UnknownIterMaxBodySizeBlocks = 2;

  uint32_t GetUnrollingFactor(HLoopInformation* loop_info, HBasicBlock* header) const;

 public:
  uint32_t GetSIMDUnrollingFactor(HBasicBlock* block,
                                  int64_t trip_count,
                                  uint32_t max_peel,
                                  uint32_t vector_length) const override {
    DCHECK_NE(vector_length, 0u);
    HLoopInformation* loop_info = block->GetLoopInformation();
    DCHECK(loop_info);
    HBasicBlock* header = loop_info->GetHeader();
    DCHECK(header);
    uint32_t unroll_factor = 0;

    if ((trip_count == 0) || (trip_count == LoopAnalysisInfo::kUnknownTripCount)) {
      // Don't unroll for large loop body size.
      unroll_factor = GetUnrollingFactor(loop_info, header);
      if (unroll_factor <= 1) {
        return LoopAnalysisInfo::kNoUnrollingFactor;
      }
    } else {
      // Don't unroll with insufficient iterations.
      if (trip_count < (2 * vector_length + max_peel)) {
        return LoopAnalysisInfo::kNoUnrollingFactor;
      }

      // Don't unroll for large loop body size.
      uint32_t unroll_cnt = GetUnrollingFactor(loop_info, header);
      if (unroll_cnt <= 1) {
        return LoopAnalysisInfo::kNoUnrollingFactor;
      }

      // Find a beneficial unroll factor with the following restrictions:
      //  - At least one iteration of the transformed loop should be executed.
      //  - The loop body shouldn't be "too big" (heuristic).
      uint32_t uf2 = (trip_count - max_peel) / vector_length;
      unroll_factor = TruncToPowerOfTwo(std::min(uf2, unroll_cnt));
      DCHECK_GE(unroll_factor, 1u);
    }

    return unroll_factor;
  }
};

uint32_t X86_64LoopHelper::GetUnrollingFactor(HLoopInformation* loop_info,
                                              HBasicBlock* header) const {
  uint32_t num_inst = 0, num_inst_header = 0, num_inst_loop_body = 0;
  for (HBlocksInLoopIterator it(*loop_info); !it.Done(); it.Advance()) {
    HBasicBlock* block = it.Current();
    DCHECK(block);
    num_inst = 0;

    for (HInstructionIterator it1(block->GetInstructions()); !it1.Done(); it1.Advance()) {
      HInstruction* inst = it1.Current();
      DCHECK(inst);

      // SuspendCheck inside loop is handled with Goto.
      // Ignoring SuspendCheck & Goto as partially unrolled loop body will have only one Goto.
      // Instruction count for Goto is being handled during unroll factor calculation below.
      if (inst->IsSuspendCheck() || inst->IsGoto()) {
        continue;
      }

      num_inst += GetMachineInstructionCount(inst);
    }

    if (block == header) {
      num_inst_header = num_inst;
    } else {
      num_inst_loop_body += num_inst;
    }
  }

  // Calculate actual unroll factor.
  uint32_t unrolling_factor = kX86_64MaxUnrollFactor;
  uint32_t unrolling_inst = kX86_64UnrolledMaxBodySizeInstr;
  // "-3" for one Goto instruction.
  uint32_t desired_size = unrolling_inst - num_inst_header - 3;
  if (desired_size < (2 * num_inst_loop_body)) {
    return 1;
  }

  while (unrolling_factor > 0) {
    if ((desired_size >> unrolling_factor) >= num_inst_loop_body) {
      break;
    }
    unrolling_factor--;
  }

  return (1 << unrolling_factor);
}

ArchNoOptsLoopHelper* ArchNoOptsLoopHelper::Create(InstructionSet isa,
                                                   ArenaAllocator* allocator) {
  switch (isa) {
    case InstructionSet::kArm64: {
      return new (allocator) Arm64LoopHelper;
    }
    case InstructionSet::kX86_64: {
      return new (allocator) X86_64LoopHelper;
    }
    default: {
      return new (allocator) ArchDefaultLoopHelper;
    }
  }
}

}  // namespace art
