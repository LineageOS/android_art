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

#include "execution_subgraph_test.h"

#include <array>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "base/scoped_arena_allocator.h"
#include "base/stl_util.h"
#include "class_root.h"
#include "dex/dex_file_types.h"
#include "dex/method_reference.h"
#include "entrypoints/quick/quick_entrypoints_enum.h"
#include "execution_subgraph.h"
#include "gtest/gtest.h"
#include "handle.h"
#include "handle_scope.h"
#include "nodes.h"
#include "optimizing/data_type.h"
#include "optimizing_unit_test.h"
#include "scoped_thread_state_change.h"

namespace art {

using BlockSet = std::unordered_set<const HBasicBlock*>;

// Helper that checks validity directly.
bool ExecutionSubgraphTestHelper::CalculateValidity(HGraph* graph, const ExecutionSubgraph* esg) {
  bool reached_end = false;
  std::queue<const HBasicBlock*> worklist;
  std::unordered_set<const HBasicBlock*> visited;
  worklist.push(graph->GetEntryBlock());
  while (!worklist.empty()) {
    const HBasicBlock* cur = worklist.front();
    worklist.pop();
    if (visited.find(cur) != visited.end()) {
      continue;
    } else {
      visited.insert(cur);
    }
    if (cur == graph->GetExitBlock()) {
      reached_end = true;
      continue;
    }
    bool has_succ = false;
    for (const HBasicBlock* succ : cur->GetSuccessors()) {
      DCHECK(succ != nullptr) << "Bad successors on block " << cur->GetBlockId();
      if (!esg->ContainsBlock(succ)) {
        continue;
      }
      has_succ = true;
      worklist.push(succ);
    }
    if (!has_succ) {
      // We aren't at the end and have nowhere to go so fail.
      return false;
    }
  }
  return reached_end;
}

class ExecutionSubgraphTest : public OptimizingUnitTest {
 public:
  ExecutionSubgraphTest() : graph_(CreateGraph()) {}

  AdjacencyListGraph SetupFromAdjacencyList(const std::string_view entry_name,
                                            const std::string_view exit_name,
                                            const std::vector<AdjacencyListGraph::Edge>& adj) {
    return AdjacencyListGraph(graph_, GetAllocator(), entry_name, exit_name, adj);
  }

  bool IsValidSubgraph(const ExecutionSubgraph* esg) {
    return ExecutionSubgraphTestHelper::CalculateValidity(graph_, esg);
  }

  bool IsValidSubgraph(const ExecutionSubgraph& esg) {
    return ExecutionSubgraphTestHelper::CalculateValidity(graph_, &esg);
  }

  HGraph* graph_;
};

// Some comparators used by these tests to avoid having to deal with various set types.
template <typename BLKS, typename = std::enable_if_t<!std::is_same_v<BlockSet, BLKS>>>
bool operator==(const BlockSet& bs, const BLKS& sas) {
  std::unordered_set<const HBasicBlock*> us(sas.begin(), sas.end());
  return bs == us;
}
template <typename BLKS, typename = std::enable_if_t<!std::is_same_v<BlockSet, BLKS>>>
bool operator==(const BLKS& sas, const BlockSet& bs) {
  return bs == sas;
}
template <typename BLKS, typename = std::enable_if_t<!std::is_same_v<BlockSet, BLKS>>>
bool operator!=(const BlockSet& bs, const BLKS& sas) {
  return !(bs == sas);
}
template <typename BLKS, typename = std::enable_if_t<!std::is_same_v<BlockSet, BLKS>>>
bool operator!=(const BLKS& sas, const BlockSet& bs) {
  return !(bs == sas);
}

// +-------+       +-------+
// | right | <--   | entry |
// +-------+       +-------+
//   |               |
//   |               |
//   |               v
//   |           + - - - - - +
//   |           '  removed  '
//   |           '           '
//   |           ' +-------+ '
//   |           ' | left  | '
//   |           ' +-------+ '
//   |           '           '
//   |           + - - - - - +
//   |               |
//   |               |
//   |               v
//   |             +-------+
//   +--------->   | exit  |
//                 +-------+
TEST_F(ExecutionSubgraphTest, Basic) {
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      { { "entry", "left" }, { "entry", "right" }, { "left", "exit" }, { "right", "exit" } }));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, true, GetScopedAllocator());
  esg.RemoveBlock(blks.Get("left"));
  esg.Finalize();
  ASSERT_TRUE(esg.IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 3u);
  ASSERT_TRUE(contents.find(blks.Get("left")) == contents.end());

  ASSERT_TRUE(contents.find(blks.Get("right")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
  esg.RemoveBlock(blks.Get("right"));
  esg.Finalize();
  std::unordered_set<const HBasicBlock*> contents_2(esg.ReachableBlocks().begin(),
                                                    esg.ReachableBlocks().end());
  ASSERT_EQ(contents_2.size(), 0u);
}

//                   +-------+         +-------+
//                   | right |   <--   | entry |
//                   +-------+         +-------+
//                     |                 |
//                     |                 |
//                     |                 v
//                     |             + - - - - - - - - - - - - - - - - - - - -+
//                     |             '             indirectly_removed         '
//                     |             '                                        '
//                     |             ' +-------+                      +-----+ '
//                     |             ' |  l1   | -------------------> | l1r | '
//                     |             ' +-------+                      +-----+ '
//                     |             '   |                              |     '
//                     |             '   |                              |     '
//                     |             '   v                              |     '
//                     |             ' +-------+                        |     '
//                     |             ' |  l1l  |                        |     '
//                     |             ' +-------+                        |     '
//                     |             '   |                              |     '
//                     |             '   |                              |     '
//                     |             '   |                              |     '
// + - - - - - - - -+  |      +- - -     |                              |     '
// '                '  |      +-         v                              |     '
// ' +-----+           |               +----------------+               |     '
// ' | l2r | <---------+-------------- |  l2 (removed)  | <-------------+     '
// ' +-----+           |               +----------------+                     '
// '   |            '  |      +-         |                                    '
// '   |       - - -+  |      +- - -     |         - - - - - - - - - - - - - -+
// '   |     '         |             '   |       '
// '   |     '         |             '   |       '
// '   |     '         |             '   v       '
// '   |     '         |             ' +-------+ '
// '   |     '         |             ' |  l2l  | '
// '   |     '         |             ' +-------+ '
// '   |     '         |             '   |       '
// '   |     '         |             '   |       '
// '   |     '         |             '   |       '
// '   |       - - -+  |      +- - -     |       '
// '   |            '  |      +-         v       '
// '   |               |               +-------+ '
// '   +---------------+-------------> |  l3   | '
// '                   |               +-------+ '
// '                '  |      +-                 '
// + - - - - - - - -+  |      +- - - - - - - - - +
//                     |                 |
//                     |                 |
//                     |                 v
//                     |               +-------+
//                     +----------->   | exit  |
//                                     +-------+
TEST_F(ExecutionSubgraphTest, Propagation) {
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 { { "entry", "l1" },
                                                   { "l1", "l1l" },
                                                   { "l1", "l1r" },
                                                   { "l1l", "l2" },
                                                   { "l1r", "l2" },
                                                   { "l2", "l2l" },
                                                   { "l2", "l2r" },
                                                   { "l2l", "l3" },
                                                   { "l2r", "l3" },
                                                   { "l3", "exit" },
                                                   { "entry", "right" },
                                                   { "right", "exit" } }));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  esg.RemoveBlock(blks.Get("l2"));
  esg.Finalize();
  ASSERT_TRUE(esg.IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  // ASSERT_EQ(contents.size(), 3u);
  // Not present, no path through.
  ASSERT_TRUE(contents.find(blks.Get("l1")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("l2")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("l3")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("l1l")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("l1r")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("l2l")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("l2r")) == contents.end());

  // present, path through.
  ASSERT_TRUE(contents.find(blks.Get("right")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
}

// +------------------------------------+
// |                                    |
// |  +-------+       +-------+         |
// |  | right | <--   | entry |         |
// |  +-------+       +-------+         |
// |    |               |               |
// |    |               |               |
// |    |               v               |
// |    |             +-------+       +--------+
// +----+--------->   |  l1   |   --> | l1loop |
//      |             +-------+       +--------+
//      |               |
//      |               |
//      |               v
//      |           +- - - - - -+
//      |           '  removed  '
//      |           '           '
//      |           ' +-------+ '
//      |           ' |  l2   | '
//      |           ' +-------+ '
//      |           '           '
//      |           +- - - - - -+
//      |               |
//      |               |
//      |               v
//      |             +-------+
//      +--------->   | exit  |
//                    +-------+
TEST_F(ExecutionSubgraphTest, PropagationLoop) {
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 { { "entry", "l1" },
                                                   { "l1", "l2" },
                                                   { "l1", "l1loop" },
                                                   { "l1loop", "l1" },
                                                   { "l2", "exit" },
                                                   { "entry", "right" },
                                                   { "right", "exit" } }));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  esg.RemoveBlock(blks.Get("l2"));
  esg.Finalize();
  ASSERT_TRUE(esg.IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 5u);

  // Not present, no path through.
  ASSERT_TRUE(contents.find(blks.Get("l2")) == contents.end());

  // present, path through.
  // Since the loop can diverge we should leave it in the execution subgraph.
  ASSERT_TRUE(contents.find(blks.Get("l1")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("l1loop")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("right")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
}

// +--------------------------------+
// |                                |
// |  +-------+     +-------+       |
// |  | right | <-- | entry |       |
// |  +-------+     +-------+       |
// |    |             |             |
// |    |             |             |
// |    |             v             |
// |    |           +-------+     +--------+
// +----+---------> |  l1   | --> | l1loop |
//      |           +-------+     +--------+
//      |             |
//      |             |
//      |             v
//      |           +-------+
//      |           |  l2   |
//      |           +-------+
//      |             |
//      |             |
//      |             v
//      |           +-------+
//      +---------> | exit  |
//                  +-------+
TEST_F(ExecutionSubgraphTest, PropagationLoop2) {
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 { { "entry", "l1" },
                                                   { "l1", "l2" },
                                                   { "l1", "l1loop" },
                                                   { "l1loop", "l1" },
                                                   { "l2", "exit" },
                                                   { "entry", "right" },
                                                   { "right", "exit" } }));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  esg.RemoveBlock(blks.Get("l1"));
  esg.Finalize();
  ASSERT_TRUE(esg.IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 3u);

  // Not present, no path through.
  ASSERT_TRUE(contents.find(blks.Get("l1")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("l1loop")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("l2")) == contents.end());

  // present, path through.
  ASSERT_TRUE(contents.find(blks.Get("right")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
}

// +--------------------------------+
// |                                |
// |  +-------+     +-------+       |
// |  | right | <-- | entry |       |
// |  +-------+     +-------+       |
// |    |             |             |
// |    |             |             |
// |    |             v             |
// |    |           +-------+     +--------+
// +----+---------> |  l1   | --> | l1loop |
//      |           +-------+     +--------+
//      |             |
//      |             |
//      |             v
//      |           +-------+
//      |           |  l2   |
//      |           +-------+
//      |             |
//      |             |
//      |             v
//      |           +-------+
//      +---------> | exit  |
//                  +-------+
TEST_F(ExecutionSubgraphTest, PropagationLoop3) {
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 { { "entry", "l1" },
                                                   { "l1", "l2" },
                                                   { "l1", "l1loop" },
                                                   { "l1loop", "l1" },
                                                   { "l2", "exit" },
                                                   { "entry", "right" },
                                                   { "right", "exit" } }));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  esg.RemoveBlock(blks.Get("l1loop"));
  esg.Finalize();
  ASSERT_TRUE(esg.IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 3u);

  // Not present, no path through. If we got to l1 loop then we must merge back
  // with l1 and l2 so they're bad too.
  ASSERT_TRUE(contents.find(blks.Get("l1loop")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("l1")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("l2")) == contents.end());

  // present, path through.
  ASSERT_TRUE(contents.find(blks.Get("right")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
}

TEST_F(ExecutionSubgraphTest, Invalid) {
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      { { "entry", "left" }, { "entry", "right" }, { "left", "exit" }, { "right", "exit" } }));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  esg.RemoveBlock(blks.Get("left"));
  esg.RemoveBlock(blks.Get("right"));
  esg.Finalize();

  ASSERT_FALSE(esg.IsValid());
  ASSERT_FALSE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 0u);
}
// Sibling branches are disconnected.
TEST_F(ExecutionSubgraphTest, Exclusions) {
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 { { "entry", "a" },
                                                   { "entry", "b" },
                                                   { "entry", "c" },
                                                   { "a", "exit" },
                                                   { "b", "exit" },
                                                   { "c", "exit" } }));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  esg.RemoveBlock(blks.Get("a"));
  esg.RemoveBlock(blks.Get("c"));
  esg.Finalize();
  ASSERT_TRUE(esg.IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 3u);
  // Not present, no path through.
  ASSERT_TRUE(contents.find(blks.Get("a")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("c")) == contents.end());

  // present, path through.
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("b")) != contents.end());

  ArrayRef<const ExecutionSubgraph::ExcludedCohort> exclusions(esg.GetExcludedCohorts());
  ASSERT_EQ(exclusions.size(), 2u);
  std::unordered_set<const HBasicBlock*> exclude_a({ blks.Get("a") });
  std::unordered_set<const HBasicBlock*> exclude_c({ blks.Get("c") });
  ASSERT_TRUE(std::find_if(exclusions.cbegin(),
                           exclusions.cend(),
                           [&](const ExecutionSubgraph::ExcludedCohort& it) {
                             return it.Blocks() == exclude_a;
                           }) != exclusions.cend());
  ASSERT_TRUE(std::find_if(exclusions.cbegin(),
                           exclusions.cend(),
                           [&](const ExecutionSubgraph::ExcludedCohort& it) {
                             return it.Blocks() == exclude_c;
                           }) != exclusions.cend());
}

// Sibling branches are disconnected.
//                                      +- - - - - - - - - - - - - - - - - - - - - - +
//                                      '                      remove_c              '
//                                      '                                            '
//                                      ' +-----------+                              '
//                                      ' | c_begin_2 | -------------------------+   '
//                                      ' +-----------+                          |   '
//                                      '                                        |   '
//                                      +- - - - - - - - - - - - - - - - - -     |   '
//                                          ^                                '   |   '
//                                          |                                '   |   '
//                                          |                                '   |   '
//                   + - - - - - -+                                          '   |   '
//                   '  remove_a  '                                          '   |   '
//                   '            '                                          '   |   '
//                   ' +--------+ '       +-----------+                 +---+'   |   '
//                   ' | **a**  | ' <--   |   entry   |   -->           | b |'   |   '
//                   ' +--------+ '       +-----------+                 +---+'   |   '
//                   '            '                                          '   |   '
//                   + - - - - - -+                                          '   |   '
//                       |                  |                             |  '   |   '
//                       |                  |                             |  '   |   '
//                       |                  v                             |  '   |   '
//                       |              +- - - - - - - -+                 |  '   |   '
//                       |              '               '                 |  '   |   '
//                       |              ' +-----------+ '                 |  '   |   '
//                       |              ' | c_begin_1 | '                 |  '   |   '
//                       |              ' +-----------+ '                 |  '   |   '
//                       |              '   |           '                 |  '   |   '
//                       |              '   |           '                 |  '   |   '
//                       |              '   |           '                 |  '   |   '
// + - - - - - - - - -+  |       + - - -    |            - - - - - - - +  |  '   |   '
// '                  '  |       +          v                          '  |  +   |   '
// ' +---------+         |                +-----------+                   |      |   '
// ' | c_end_2 | <-------+--------------- | **c_mid** | <-----------------+------+   '
// ' +---------+         |                +-----------+                   |          '
// '                  '  |       +          |                          '  |  +       '
// + - - - - - - - - -+  |       + - - -    |            - - - - - - - +  |  + - - - +
//     |                 |              '   |           '                 |
//     |                 |              '   |           '                 |
//     |                 |              '   v           '                 |
//     |                 |              ' +-----------+ '                 |
//     |                 |              ' |  c_end_1  | '                 |
//     |                 |              ' +-----------+ '                 |
//     |                 |              '               '                 |
//     |                 |              +- - - - - - - -+                 |
//     |                 |                  |                             |
//     |                 |                  |                             |
//     |                 |                  v                             v
//     |                 |                +---------------------------------+
//     |                 +------------>   |              exit               |
//     |                                  +---------------------------------+
//     |                                    ^
//     +------------------------------------+
TEST_F(ExecutionSubgraphTest, ExclusionExtended) {
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 { { "entry", "a" },
                                                   { "entry", "b" },
                                                   { "entry", "c_begin_1" },
                                                   { "entry", "c_begin_2" },
                                                   { "c_begin_1", "c_mid" },
                                                   { "c_begin_2", "c_mid" },
                                                   { "c_mid", "c_end_1" },
                                                   { "c_mid", "c_end_2" },
                                                   { "a", "exit" },
                                                   { "b", "exit" },
                                                   { "c_end_1", "exit" },
                                                   { "c_end_2", "exit" } }));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  esg.RemoveBlock(blks.Get("a"));
  esg.RemoveBlock(blks.Get("c_mid"));
  esg.Finalize();
  ASSERT_TRUE(esg.IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 3u);
  // Not present, no path through.
  ASSERT_TRUE(contents.find(blks.Get("a")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("c_begin_1")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("c_begin_2")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("c_mid")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("c_end_1")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("c_end_2")) == contents.end());

  // present, path through.
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("b")) != contents.end());

  ArrayRef<const ExecutionSubgraph::ExcludedCohort> exclusions(esg.GetExcludedCohorts());
  ASSERT_EQ(exclusions.size(), 2u);
  BlockSet exclude_a({ blks.Get("a") });
  BlockSet exclude_c({ blks.Get("c_begin_1"),
                       blks.Get("c_begin_2"),
                       blks.Get("c_mid"),
                       blks.Get("c_end_1"),
                       blks.Get("c_end_2") });
  ASSERT_TRUE(std::find_if(exclusions.cbegin(),
                           exclusions.cend(),
                           [&](const ExecutionSubgraph::ExcludedCohort& it) {
                             return it.Blocks() == exclude_a;
                           }) != exclusions.cend());
  ASSERT_TRUE(
      std::find_if(
          exclusions.cbegin(), exclusions.cend(), [&](const ExecutionSubgraph::ExcludedCohort& it) {
            return it.Blocks() == exclude_c &&
                   BlockSet({ blks.Get("c_begin_1"), blks.Get("c_begin_2") }) == it.EntryBlocks() &&
                   BlockSet({ blks.Get("c_end_1"), blks.Get("c_end_2") }) == it.ExitBlocks();
          }) != exclusions.cend());
}

//    ┌───────┐     ┌────────────┐
// ┌─ │ right │ ◀── │   entry    │
// │  └───────┘     └────────────┘
// │                  │
// │                  │
// │                  ▼
// │                ┌────────────┐
// │                │  esc_top   │
// │                └────────────┘
// │                  │
// │                  │
// │                  ▼
// │                ┌────────────┐
// └──────────────▶ │   middle   │ ─┐
//                  └────────────┘  │
//                    │             │
//                    │             │
//                    ▼             │
//                  ┌────────────┐  │
//                  │ esc_bottom │  │
//                  └────────────┘  │
//                    │             │
//                    │             │
//                    ▼             │
//                  ┌────────────┐  │
//                  │    exit    │ ◀┘
//                  └────────────┘
TEST_F(ExecutionSubgraphTest, InAndOutEscape) {
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 { { "entry", "esc_top" },
                                                   { "entry", "right" },
                                                   { "esc_top", "middle" },
                                                   { "right", "middle" },
                                                   { "middle", "exit" },
                                                   { "middle", "esc_bottom" },
                                                   { "esc_bottom", "exit" } }));

  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  esg.RemoveBlock(blks.Get("esc_top"));
  esg.RemoveBlock(blks.Get("esc_bottom"));
  esg.Finalize();

  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());
  ASSERT_EQ(contents.size(), 0u);
  ASSERT_FALSE(esg.IsValid());
  ASSERT_FALSE(IsValidSubgraph(esg));

  ASSERT_EQ(contents.size(), 0u);
}

// Test with max number of successors and no removals.
TEST_F(ExecutionSubgraphTest, BigNodes) {
  std::vector<std::string> mid_blocks;
  for (auto i : Range(ExecutionSubgraph::kMaxFilterableSuccessors)) {
    std::ostringstream oss;
    oss << "blk" << i;
    mid_blocks.push_back(oss.str().c_str());
  }
  ASSERT_EQ(mid_blocks.size(), ExecutionSubgraph::kMaxFilterableSuccessors);
  std::vector<AdjacencyListGraph::Edge> edges;
  for (const auto& mid : mid_blocks) {
    edges.emplace_back("entry", mid);
    edges.emplace_back(mid, "exit");
  }
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry", "exit", edges));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  esg.Finalize();
  ASSERT_TRUE(esg.IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  for (const auto& mid : mid_blocks) {
    EXPECT_TRUE(contents.find(blks.Get(mid)) != contents.end()) << mid;
  }
  // + 2 for entry and exit nodes.
  ASSERT_EQ(contents.size(), ExecutionSubgraph::kMaxFilterableSuccessors + 2);
}

// Test with max number of successors and some removals.
TEST_F(ExecutionSubgraphTest, BigNodesMissing) {
  std::vector<std::string> mid_blocks;
  for (auto i : Range(ExecutionSubgraph::kMaxFilterableSuccessors)) {
    std::ostringstream oss;
    oss << "blk" << i;
    mid_blocks.push_back(oss.str());
  }
  std::vector<AdjacencyListGraph::Edge> edges;
  for (const auto& mid : mid_blocks) {
    edges.emplace_back("entry", mid);
    edges.emplace_back(mid, "exit");
  }
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry", "exit", edges));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  esg.RemoveBlock(blks.Get("blk2"));
  esg.RemoveBlock(blks.Get("blk4"));
  esg.Finalize();
  ASSERT_TRUE(esg.IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  ASSERT_EQ(contents.size(), ExecutionSubgraph::kMaxFilterableSuccessors + 2 - 2);

  // Not present, no path through.
  ASSERT_TRUE(contents.find(blks.Get("blk2")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("blk4")) == contents.end());
}

// Test with max number of successors and all successors removed.
TEST_F(ExecutionSubgraphTest, BigNodesNoPath) {
  std::vector<std::string> mid_blocks;
  for (auto i : Range(ExecutionSubgraph::kMaxFilterableSuccessors)) {
    std::ostringstream oss;
    oss << "blk" << i;
    mid_blocks.push_back(oss.str());
  }
  std::vector<AdjacencyListGraph::Edge> edges;
  for (const auto& mid : mid_blocks) {
    edges.emplace_back("entry", mid);
    edges.emplace_back(mid, "exit");
  }
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry", "exit", edges));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  for (const auto& mid : mid_blocks) {
    esg.RemoveBlock(blks.Get(mid));
  }
  esg.Finalize();
  ASSERT_FALSE(esg.IsValid());
  ASSERT_FALSE(IsValidSubgraph(esg));
}

// Test with max number of successors
TEST_F(ExecutionSubgraphTest, CanAnalyseBig) {
  // Make an absurdly huge and well connected graph. This should be pretty worst-case scenario.
  constexpr size_t kNumBlocks = ExecutionSubgraph::kMaxFilterableSuccessors + 1000;
  std::vector<std::string> mid_blocks;
  for (auto i : Range(kNumBlocks)) {
    std::ostringstream oss;
    oss << "blk" << i;
    mid_blocks.push_back(oss.str());
  }
  std::vector<AdjacencyListGraph::Edge> edges;
  for (auto cur : Range(kNumBlocks)) {
    for (auto nxt :
         Range(cur + 1,
               std::min(cur + ExecutionSubgraph::kMaxFilterableSuccessors + 1, kNumBlocks))) {
      edges.emplace_back(mid_blocks[cur], mid_blocks[nxt]);
    }
  }
  AdjacencyListGraph blks(SetupFromAdjacencyList(mid_blocks.front(), mid_blocks.back(), edges));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));

  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  esg.Finalize();
  ASSERT_TRUE(esg.IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  ASSERT_EQ(contents.size(), kNumBlocks);
}

// Test with many successors
TEST_F(ExecutionSubgraphTest, CanAnalyseBig2) {
  // Make an absurdly huge and well connected graph. This should be pretty worst-case scenario.
  constexpr size_t kNumBlocks = ExecutionSubgraph::kMaxFilterableSuccessors + 1000;
  constexpr size_t kTestMaxSuccessors = ExecutionSubgraph::kMaxFilterableSuccessors - 1;
  std::vector<std::string> mid_blocks;
  for (auto i : Range(kNumBlocks)) {
    std::ostringstream oss;
    oss << "blk" << i;
    mid_blocks.push_back(oss.str());
  }
  std::vector<AdjacencyListGraph::Edge> edges;
  for (auto cur : Range(kNumBlocks)) {
    for (auto nxt : Range(cur + 1, std::min(cur + 1 + kTestMaxSuccessors, kNumBlocks))) {
      edges.emplace_back(mid_blocks[cur], mid_blocks[nxt]);
    }
  }
  edges.emplace_back(mid_blocks.front(), mid_blocks.back());
  AdjacencyListGraph blks(SetupFromAdjacencyList(mid_blocks.front(), mid_blocks.back(), edges));
  ASSERT_TRUE(ExecutionSubgraph::CanAnalyse(graph_));
  ExecutionSubgraph esg(graph_, /*analysis_possible=*/true, GetScopedAllocator());
  constexpr size_t kToRemoveIdx = kNumBlocks / 2;
  HBasicBlock* remove_implicit = blks.Get(mid_blocks[kToRemoveIdx]);
  for (HBasicBlock* pred : remove_implicit->GetPredecessors()) {
    esg.RemoveBlock(pred);
  }
  esg.Finalize();
  EXPECT_TRUE(esg.IsValid());
  EXPECT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg.ReachableBlocks().begin(),
                                                  esg.ReachableBlocks().end());

  // Only entry and exit. The middle ones should eliminate everything else.
  EXPECT_EQ(contents.size(), 2u);
  EXPECT_TRUE(contents.find(remove_implicit) == contents.end());
  EXPECT_TRUE(contents.find(blks.Get(mid_blocks.front())) != contents.end());
  EXPECT_TRUE(contents.find(blks.Get(mid_blocks.back())) != contents.end());
}

// Test with too many successors
TEST_F(ExecutionSubgraphTest, CanNotAnalyseBig) {
  std::vector<std::string> mid_blocks;
  for (auto i : Range(ExecutionSubgraph::kMaxFilterableSuccessors + 4)) {
    std::ostringstream oss;
    oss << "blk" << i;
    mid_blocks.push_back(oss.str());
  }
  std::vector<AdjacencyListGraph::Edge> edges;
  for (const auto& mid : mid_blocks) {
    edges.emplace_back("entry", mid);
    edges.emplace_back(mid, "exit");
  }
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry", "exit", edges));
  ASSERT_FALSE(ExecutionSubgraph::CanAnalyse(graph_));
}
}  // namespace art
