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

#include "load_store_analysis.h"

namespace art {

// A cap for the number of heap locations to prevent pathological time/space consumption.
// The number of heap locations for most of the methods stays below this threshold.
constexpr size_t kMaxNumberOfHeapLocations = 32;

// Test if two integer ranges [l1,h1] and [l2,h2] overlap.
// Note that the ranges are inclusive on both ends.
//       l1|------|h1
//  l2|------|h2
static bool CanIntegerRangesOverlap(int64_t l1, int64_t h1, int64_t l2, int64_t h2) {
  return std::max(l1, l2) <= std::min(h1, h2);
}

static bool CanBinaryOpAndIndexAlias(const HBinaryOperation* idx1,
                                     const size_t vector_length1,
                                     const HInstruction* idx2,
                                     const size_t vector_length2) {
  if (!IsAddOrSub(idx1)) {
    // We currently only support Add and Sub operations.
    return true;
  }
  if (idx1->AsBinaryOperation()->GetLeastConstantLeft() != idx2) {
    // Cannot analyze [i+CONST1] and [j].
    return true;
  }
  if (!idx1->GetConstantRight()->IsIntConstant()) {
    return true;
  }

  // Since 'i' are the same in [i+CONST] and [i],
  // further compare [CONST] and [0].
  int64_t l1 = idx1->IsAdd() ?
               idx1->GetConstantRight()->AsIntConstant()->GetValue() :
               -idx1->GetConstantRight()->AsIntConstant()->GetValue();
  int64_t l2 = 0;
  int64_t h1 = l1 + (vector_length1 - 1);
  int64_t h2 = l2 + (vector_length2 - 1);
  return CanIntegerRangesOverlap(l1, h1, l2, h2);
}

static bool CanBinaryOpsAlias(const HBinaryOperation* idx1,
                              const size_t vector_length1,
                              const HBinaryOperation* idx2,
                              const size_t vector_length2) {
  if (!IsAddOrSub(idx1) || !IsAddOrSub(idx2)) {
    // We currently only support Add and Sub operations.
    return true;
  }
  if (idx1->AsBinaryOperation()->GetLeastConstantLeft() !=
      idx2->AsBinaryOperation()->GetLeastConstantLeft()) {
    // Cannot analyze [i+CONST1] and [j+CONST2].
    return true;
  }
  if (!idx1->GetConstantRight()->IsIntConstant() ||
      !idx2->GetConstantRight()->IsIntConstant()) {
    return true;
  }

  // Since 'i' are the same in [i+CONST1] and [i+CONST2],
  // further compare [CONST1] and [CONST2].
  int64_t l1 = idx1->IsAdd() ?
               idx1->GetConstantRight()->AsIntConstant()->GetValue() :
               -idx1->GetConstantRight()->AsIntConstant()->GetValue();
  int64_t l2 = idx2->IsAdd() ?
               idx2->GetConstantRight()->AsIntConstant()->GetValue() :
               -idx2->GetConstantRight()->AsIntConstant()->GetValue();
  int64_t h1 = l1 + (vector_length1 - 1);
  int64_t h2 = l2 + (vector_length2 - 1);
  return CanIntegerRangesOverlap(l1, h1, l2, h2);
}

// Make sure we mark any writes/potential writes to heap-locations within partially
// escaped values as escaping.
void ReferenceInfo::PrunePartialEscapeWrites() {
  if (!subgraph_.IsValid()) {
    // All paths escape.
    return;
  }
  HGraph* graph = reference_->GetBlock()->GetGraph();
  ArenaBitVector additional_exclusions(
      allocator_, graph->GetBlocks().size(), false, kArenaAllocLSA);
  for (const HUseListNode<HInstruction*>& use : reference_->GetUses()) {
    const HInstruction* user = use.GetUser();
    const bool possible_exclusion =
        !additional_exclusions.IsBitSet(user->GetBlock()->GetBlockId()) &&
        subgraph_.ContainsBlock(user->GetBlock());
    const bool is_written_to =
        (user->IsUnresolvedInstanceFieldSet() || user->IsUnresolvedStaticFieldSet() ||
         user->IsInstanceFieldSet() || user->IsStaticFieldSet() || user->IsArraySet()) &&
        (reference_ == user->InputAt(0));
    if (possible_exclusion && is_written_to &&
        std::any_of(subgraph_.UnreachableBlocks().begin(),
                    subgraph_.UnreachableBlocks().end(),
                    [&](const HBasicBlock* excluded) -> bool {
                      return reference_->GetBlock()->GetGraph()->PathBetween(excluded,
                                                                             user->GetBlock());
                    })) {
      // This object had memory written to it somewhere, if it escaped along
      // some paths prior to the current block this write also counts as an
      // escape.
      additional_exclusions.SetBit(user->GetBlock()->GetBlockId());
    }
  }
  if (UNLIKELY(additional_exclusions.IsAnyBitSet())) {
    for (uint32_t exc : additional_exclusions.Indexes()) {
      subgraph_.RemoveBlock(graph->GetBlocks()[exc]);
    }
  }
}

bool HeapLocationCollector::InstructionEligibleForLSERemoval(HInstruction* inst) const {
  if (inst->IsNewInstance()) {
    return !inst->AsNewInstance()->NeedsChecks();
  } else if (inst->IsNewArray()) {
    HInstruction* array_length = inst->AsNewArray()->GetLength();
    bool known_array_length =
        array_length->IsIntConstant() && array_length->AsIntConstant()->GetValue() >= 0;
    return known_array_length &&
           std::all_of(inst->GetUses().cbegin(),
                       inst->GetUses().cend(),
                       [&](const HUseListNode<HInstruction*>& user) {
                         if (user.GetUser()->IsArrayGet() || user.GetUser()->IsArraySet()) {
                           return user.GetUser()->InputAt(1)->IsIntConstant();
                         }
                         return true;
                       });
  } else {
    return false;
  }
}

void HeapLocationCollector::DumpReferenceStats(OptimizingCompilerStats* stats) {
  if (stats == nullptr) {
    return;
  }
  std::vector<bool> seen_instructions(GetGraph()->GetCurrentInstructionId(), false);
  for (auto hl : heap_locations_) {
    auto ri = hl->GetReferenceInfo();
    if (ri == nullptr || seen_instructions[ri->GetReference()->GetId()]) {
      continue;
    }
    auto instruction = ri->GetReference();
    seen_instructions[instruction->GetId()] = true;
    if (ri->IsSingletonAndRemovable()) {
      if (InstructionEligibleForLSERemoval(instruction)) {
        MaybeRecordStat(stats, MethodCompilationStat::kFullLSEPossible);
      }
    }
    // TODO This is an estimate of the number of allocations we will be able
    // to (partially) remove. As additional work is done this can be refined.
    if (ri->IsPartialSingleton() && instruction->IsNewInstance() &&
        ri->GetNoEscapeSubgraph()->ContainsBlock(instruction->GetBlock()) &&
        !ri->GetNoEscapeSubgraph()->GetExcludedCohorts().empty() &&
        InstructionEligibleForLSERemoval(instruction)) {
      MaybeRecordStat(stats, MethodCompilationStat::kPartialLSEPossible);
    }
  }
}

bool HeapLocationCollector::CanArrayElementsAlias(const HInstruction* idx1,
                                                  const size_t vector_length1,
                                                  const HInstruction* idx2,
                                                  const size_t vector_length2) const {
  DCHECK(idx1 != nullptr);
  DCHECK(idx2 != nullptr);
  DCHECK_GE(vector_length1, HeapLocation::kScalar);
  DCHECK_GE(vector_length2, HeapLocation::kScalar);

  // [i] and [i].
  if (idx1 == idx2) {
    return true;
  }

  // [CONST1] and [CONST2].
  if (idx1->IsIntConstant() && idx2->IsIntConstant()) {
    int64_t l1 = idx1->AsIntConstant()->GetValue();
    int64_t l2 = idx2->AsIntConstant()->GetValue();
    // To avoid any overflow in following CONST+vector_length calculation,
    // use int64_t instead of int32_t.
    int64_t h1 = l1 + (vector_length1 - 1);
    int64_t h2 = l2 + (vector_length2 - 1);
    return CanIntegerRangesOverlap(l1, h1, l2, h2);
  }

  // [i+CONST] and [i].
  if (idx1->IsBinaryOperation() &&
      idx1->AsBinaryOperation()->GetConstantRight() != nullptr &&
      idx1->AsBinaryOperation()->GetLeastConstantLeft() == idx2) {
    return CanBinaryOpAndIndexAlias(idx1->AsBinaryOperation(),
                                    vector_length1,
                                    idx2,
                                    vector_length2);
  }

  // [i] and [i+CONST].
  if (idx2->IsBinaryOperation() &&
      idx2->AsBinaryOperation()->GetConstantRight() != nullptr &&
      idx2->AsBinaryOperation()->GetLeastConstantLeft() == idx1) {
    return CanBinaryOpAndIndexAlias(idx2->AsBinaryOperation(),
                                    vector_length2,
                                    idx1,
                                    vector_length1);
  }

  // [i+CONST1] and [i+CONST2].
  if (idx1->IsBinaryOperation() &&
      idx1->AsBinaryOperation()->GetConstantRight() != nullptr &&
      idx2->IsBinaryOperation() &&
      idx2->AsBinaryOperation()->GetConstantRight() != nullptr) {
    return CanBinaryOpsAlias(idx1->AsBinaryOperation(),
                             vector_length1,
                             idx2->AsBinaryOperation(),
                             vector_length2);
  }

  // By default, MAY alias.
  return true;
}

bool LoadStoreAnalysis::Run() {
  for (HBasicBlock* block : graph_->GetReversePostOrder()) {
    heap_location_collector_.VisitBasicBlock(block);
  }

  if (heap_location_collector_.GetNumberOfHeapLocations() > kMaxNumberOfHeapLocations) {
    // Bail out if there are too many heap locations to deal with.
    heap_location_collector_.CleanUp();
    return false;
  }
  if (!heap_location_collector_.HasHeapStores()) {
    // Without heap stores, this pass would act mostly as GVN on heap accesses.
    heap_location_collector_.CleanUp();
    return false;
  }
  if (heap_location_collector_.HasVolatile() || heap_location_collector_.HasMonitorOps()) {
    // Don't do load/store elimination if the method has volatile field accesses or
    // monitor operations, for now.
    // TODO: do it right.
    heap_location_collector_.CleanUp();
    return false;
  }

  heap_location_collector_.BuildAliasingMatrix();
  heap_location_collector_.DumpReferenceStats(stats_);
  return true;
}

}  // namespace art
