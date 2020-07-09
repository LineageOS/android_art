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

#include "execution_subgraph.h"

#include <algorithm>
#include <unordered_set>

#include "android-base/macros.h"
#include "base/arena_allocator.h"
#include "base/arena_bit_vector.h"
#include "base/globals.h"
#include "base/scoped_arena_allocator.h"
#include "nodes.h"

namespace art {

ExecutionSubgraph::ExecutionSubgraph(HGraph* graph,
                                     bool analysis_possible,
                                     ScopedArenaAllocator* allocator)
    : graph_(graph),
      allocator_(allocator),
      allowed_successors_(analysis_possible ? graph_->GetBlocks().size() : 0,
                          ~(std::bitset<kMaxFilterableSuccessors> {}),
                          allocator_->Adapter(kArenaAllocLSA)),
      unreachable_blocks_(
          allocator_, analysis_possible ? graph_->GetBlocks().size() : 0, false, kArenaAllocLSA),
      valid_(analysis_possible),
      needs_prune_(false),
      finalized_(false) {
  if (valid_) {
    DCHECK(std::all_of(graph->GetBlocks().begin(), graph->GetBlocks().end(), [](HBasicBlock* it) {
      return it == nullptr || it->GetSuccessors().size() <= kMaxFilterableSuccessors;
    }));
  }
}

void ExecutionSubgraph::RemoveBlock(const HBasicBlock* to_remove) {
  if (!valid_) {
    return;
  }
  uint32_t id = to_remove->GetBlockId();
  if (unreachable_blocks_.IsBitSet(id)) {
    if (kIsDebugBuild) {
      // This isn't really needed but it's good to have this so it functions as
      // a DCHECK that we always call Prune after removing any block.
      needs_prune_ = true;
    }
    return;
  }
  unreachable_blocks_.SetBit(id);
  for (HBasicBlock* pred : to_remove->GetPredecessors()) {
    std::bitset<kMaxFilterableSuccessors> allowed_successors {};
    // ZipCount iterates over both the successors and the index of them at the same time.
    for (auto [succ, i] : ZipCount(MakeIterationRange(pred->GetSuccessors()))) {
      if (succ != to_remove) {
        allowed_successors.set(i);
      }
    }
    LimitBlockSuccessors(pred, allowed_successors);
  }
}

// Removes sink nodes.
void ExecutionSubgraph::Prune() {
  if (UNLIKELY(!valid_)) {
    return;
  }
  needs_prune_ = false;
  // This is the record of the edges that were both (1) explored and (2) reached
  // the exit node.
  {
    // Allocator for temporary values.
    ScopedArenaAllocator temporaries(graph_->GetArenaStack());
    ScopedArenaVector<std::bitset<kMaxFilterableSuccessors>> results(
        graph_->GetBlocks().size(), temporaries.Adapter(kArenaAllocLSA));
    unreachable_blocks_.ClearAllBits();
    // TODO We should support infinite loops as well.
    if (UNLIKELY(graph_->GetExitBlock() == nullptr)) {
      // Infinite loop
      valid_ = false;
      return;
    }
    // Fills up the 'results' map with what we need to add to update
    // allowed_successors in order to prune sink nodes.
    bool start_reaches_end = false;
    // This is basically a DFS of the graph with some edges skipped.
    {
      const size_t num_blocks = graph_->GetBlocks().size();
      constexpr ssize_t kUnvisitedSuccIdx = -1;
      ArenaBitVector visiting(&temporaries, num_blocks, false, kArenaAllocLSA);
      // How many of the successors of each block we have already examined. This
      // has three states.
      // (1) kUnvisitedSuccIdx: we have not examined any edges,
      // (2) 0 <= val < # of successors: we have examined 'val' successors/are
      // currently examining successors_[val],
      // (3) kMaxFilterableSuccessors: We have examined all of the successors of
      // the block (the 'result' is final).
      ScopedArenaVector<ssize_t> last_succ_seen(
          num_blocks, kUnvisitedSuccIdx, temporaries.Adapter(kArenaAllocLSA));
      // A stack of which blocks we are visiting in this DFS traversal. Does not
      // include the current-block. Used with last_succ_seen to figure out which
      // bits to set if we find a path to the end/loop.
      ScopedArenaVector<uint32_t> current_path(temporaries.Adapter(kArenaAllocLSA));
      // Just ensure we have enough space. The allocator will be cleared shortly
      // anyway so this is fast.
      current_path.reserve(num_blocks);
      // Current block we are examining. Modified only by 'push_block' and 'pop_block'
      const HBasicBlock* cur_block = graph_->GetEntryBlock();
      // Used to note a recur where we will start iterating on 'blk' and save
      // where we are. We must 'continue' immediately after this.
      auto push_block = [&](const HBasicBlock* blk) {
        DCHECK(std::find(current_path.cbegin(), current_path.cend(), cur_block->GetBlockId()) ==
               current_path.end());
        if (kIsDebugBuild) {
          std::for_each(current_path.cbegin(), current_path.cend(), [&](auto id) {
            DCHECK_GT(last_succ_seen[id], kUnvisitedSuccIdx) << id;
            DCHECK_LT(last_succ_seen[id], static_cast<ssize_t>(kMaxFilterableSuccessors)) << id;
          });
        }
        current_path.push_back(cur_block->GetBlockId());
        visiting.SetBit(cur_block->GetBlockId());
        cur_block = blk;
      };
      // Used to note that we have fully explored a block and should return back
      // up. Sets cur_block appropriately. We must 'continue' immediately after
      // calling this.
      auto pop_block = [&]() {
        if (UNLIKELY(current_path.empty())) {
          // Should only happen if entry-blocks successors are exhausted.
          DCHECK_GE(last_succ_seen[graph_->GetEntryBlock()->GetBlockId()],
                    static_cast<ssize_t>(graph_->GetEntryBlock()->GetSuccessors().size()));
          cur_block = nullptr;
        } else {
          const HBasicBlock* last = graph_->GetBlocks()[current_path.back()];
          visiting.ClearBit(current_path.back());
          current_path.pop_back();
          cur_block = last;
        }
      };
      // Mark the current path as a path to the end. This is in contrast to paths
      // that end in (eg) removed blocks.
      auto propagate_true = [&]() {
        for (uint32_t id : current_path) {
          DCHECK_GT(last_succ_seen[id], kUnvisitedSuccIdx);
          DCHECK_LT(last_succ_seen[id], static_cast<ssize_t>(kMaxFilterableSuccessors));
          results[id].set(last_succ_seen[id]);
        }
      };
      ssize_t num_entry_succ = graph_->GetEntryBlock()->GetSuccessors().size();
      // As long as the entry-block has not explored all successors we still have
      // work to do.
      const uint32_t entry_block_id = graph_->GetEntryBlock()->GetBlockId();
      while (num_entry_succ > last_succ_seen[entry_block_id]) {
        DCHECK(cur_block != nullptr);
        uint32_t id = cur_block->GetBlockId();
        DCHECK((current_path.empty() && cur_block == graph_->GetEntryBlock()) ||
               current_path.front() == graph_->GetEntryBlock()->GetBlockId())
            << "current path size: " << current_path.size()
            << " cur_block id: " << cur_block->GetBlockId() << " entry id "
            << graph_->GetEntryBlock()->GetBlockId();
        DCHECK(!visiting.IsBitSet(id))
            << "Somehow ended up in a loop! This should have been caught before now! " << id;
        std::bitset<kMaxFilterableSuccessors>& result = results[id];
        if (cur_block == graph_->GetExitBlock()) {
          start_reaches_end = true;
          propagate_true();
          pop_block();
          continue;
        } else if (last_succ_seen[id] == kMaxFilterableSuccessors) {
          // Already fully explored.
          if (result.any()) {
            propagate_true();
          }
          pop_block();
          continue;
        }
        // NB This is a pointer. Modifications modify the last_succ_seen.
        ssize_t* cur_succ = &last_succ_seen[id];
        std::bitset<kMaxFilterableSuccessors> succ_bitmap = GetAllowedSuccessors(cur_block);
        // Get next successor allowed.
        while (++(*cur_succ) < static_cast<ssize_t>(kMaxFilterableSuccessors) &&
               !succ_bitmap.test(*cur_succ)) {
          DCHECK_GE(*cur_succ, 0);
        }
        if (*cur_succ >= static_cast<ssize_t>(cur_block->GetSuccessors().size())) {
          // No more successors. Mark that we've checked everything. Later visits
          // to this node can use the existing data.
          DCHECK_LE(*cur_succ, static_cast<ssize_t>(kMaxFilterableSuccessors));
          *cur_succ = kMaxFilterableSuccessors;
          pop_block();
          continue;
        }
        const HBasicBlock* nxt = cur_block->GetSuccessors()[*cur_succ];
        DCHECK(nxt != nullptr) << "id: " << *cur_succ
                               << " max: " << cur_block->GetSuccessors().size();
        if (visiting.IsBitSet(nxt->GetBlockId())) {
          // This is a loop. Mark it and continue on. Mark allowed-successor on
          // this block's results as well.
          result.set(*cur_succ);
          propagate_true();
        } else {
          // Not a loop yet. Recur.
          push_block(nxt);
        }
      }
    }
    // If we can't reach the end then there is no path through the graph without
    // hitting excluded blocks
    if (UNLIKELY(!start_reaches_end)) {
      valid_ = false;
      return;
    }
    // Mark blocks we didn't see in the ReachesEnd flood-fill
    for (const HBasicBlock* blk : graph_->GetBlocks()) {
      if (blk != nullptr &&
          results[blk->GetBlockId()].none() &&
          blk != graph_->GetExitBlock() &&
          blk != graph_->GetEntryBlock()) {
        // We never visited this block, must be unreachable.
        unreachable_blocks_.SetBit(blk->GetBlockId());
      }
    }
    // write the new data.
    memcpy(allowed_successors_.data(),
           results.data(),
           results.size() * sizeof(std::bitset<kMaxFilterableSuccessors>));
  }
  RecalculateExcludedCohort();
}

void ExecutionSubgraph::RemoveConcavity() {
  if (UNLIKELY(!valid_)) {
    return;
  }
  DCHECK(!needs_prune_);
  for (const HBasicBlock* blk : graph_->GetBlocks()) {
    if (blk == nullptr || unreachable_blocks_.IsBitSet(blk->GetBlockId())) {
      continue;
    }
    uint32_t blkid = blk->GetBlockId();
    if (std::any_of(unreachable_blocks_.Indexes().begin(),
                    unreachable_blocks_.Indexes().end(),
                    [&](uint32_t skipped) { return graph_->PathBetween(skipped, blkid); }) &&
        std::any_of(unreachable_blocks_.Indexes().begin(),
                    unreachable_blocks_.Indexes().end(),
                    [&](uint32_t skipped) { return graph_->PathBetween(blkid, skipped); })) {
      RemoveBlock(blk);
    }
  }
  Prune();
}

void ExecutionSubgraph::RecalculateExcludedCohort() {
  DCHECK(!needs_prune_);
  excluded_list_.emplace(allocator_->Adapter(kArenaAllocLSA));
  ScopedArenaVector<ExcludedCohort>& res = excluded_list_.value();
  // Make a copy of unreachable_blocks_;
  ArenaBitVector unreachable(allocator_, graph_->GetBlocks().size(), false, kArenaAllocLSA);
  unreachable.Copy(&unreachable_blocks_);
  // Split cohorts with union-find
  while (unreachable.IsAnyBitSet()) {
    res.emplace_back(allocator_, graph_);
    ExcludedCohort& cohort = res.back();
    // We don't allocate except for the queue beyond here so create another arena to save memory.
    ScopedArenaAllocator alloc(graph_->GetArenaStack());
    ScopedArenaQueue<const HBasicBlock*> worklist(alloc.Adapter(kArenaAllocLSA));
    // Select an arbitrary node
    const HBasicBlock* first = graph_->GetBlocks()[unreachable.GetHighestBitSet()];
    worklist.push(first);
    do {
      // Flood-fill both forwards and backwards.
      const HBasicBlock* cur = worklist.front();
      worklist.pop();
      if (!unreachable.IsBitSet(cur->GetBlockId())) {
        // Already visited or reachable somewhere else.
        continue;
      }
      unreachable.ClearBit(cur->GetBlockId());
      cohort.blocks_.SetBit(cur->GetBlockId());
      // don't bother filtering here, it's done next go-around
      for (const HBasicBlock* pred : cur->GetPredecessors()) {
        worklist.push(pred);
      }
      for (const HBasicBlock* succ : cur->GetSuccessors()) {
        worklist.push(succ);
      }
    } while (!worklist.empty());
  }
  // Figure out entry & exit nodes.
  for (ExcludedCohort& cohort : res) {
    DCHECK(cohort.blocks_.IsAnyBitSet());
    auto is_external = [&](const HBasicBlock* ext) -> bool {
      return !cohort.blocks_.IsBitSet(ext->GetBlockId());
    };
    for (const HBasicBlock* blk : cohort.Blocks()) {
      const auto& preds = blk->GetPredecessors();
      const auto& succs = blk->GetSuccessors();
      if (std::any_of(preds.cbegin(), preds.cend(), is_external)) {
        cohort.entry_blocks_.SetBit(blk->GetBlockId());
      }
      if (std::any_of(succs.cbegin(), succs.cend(), is_external)) {
        cohort.exit_blocks_.SetBit(blk->GetBlockId());
      }
    }
  }
}

std::ostream& operator<<(std::ostream& os, const ExecutionSubgraph::ExcludedCohort& ex) {
  ex.Dump(os);
  return os;
}

void ExecutionSubgraph::ExcludedCohort::Dump(std::ostream& os) const {
  auto dump = [&](BitVecBlockRange arr) {
    os << "[";
    bool first = true;
    for (const HBasicBlock* b : arr) {
      if (!first) {
        os << ", ";
      }
      first = false;
      os << b->GetBlockId();
    }
    os << "]";
  };
  auto dump_blocks = [&]() {
    os << "[";
    bool first = true;
    for (const HBasicBlock* b : Blocks()) {
      if (!entry_blocks_.IsBitSet(b->GetBlockId()) && !exit_blocks_.IsBitSet(b->GetBlockId())) {
        if (!first) {
          os << ", ";
        }
        first = false;
        os << b->GetBlockId();
      }
    }
    os << "]";
  };

  os << "{ entry: ";
  dump(EntryBlocks());
  os << ", interior: ";
  dump_blocks();
  os << ", exit: ";
  dump(ExitBlocks());
  os << "}";
}

}  // namespace art
