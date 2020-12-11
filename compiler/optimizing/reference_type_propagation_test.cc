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

#include "reference_type_propagation.h"

#include <random>

#include "base/arena_allocator.h"
#include "base/transform_array_ref.h"
#include "base/transform_iterator.h"
#include "builder.h"
#include "nodes.h"
#include "object_lock.h"
#include "optimizing_unit_test.h"

namespace art {

// TODO It would be good to use the following but there is a miniscule amount of
// chance for flakiness so we'll just use a set seed instead.
constexpr bool kUseTrueRandomness = false;

/**
 * Fixture class for unit testing the ReferenceTypePropagation phase. Used to verify the
 * functionality of methods and situations that are hard to set up with checker tests.
 */
template<typename SuperTest>
class ReferenceTypePropagationTestBase : public SuperTest, public OptimizingUnitTestHelper {
 public:
  ReferenceTypePropagationTestBase() : graph_(nullptr), propagation_(nullptr) { }

  ~ReferenceTypePropagationTestBase() { }

  void SetupPropagation(VariableSizedHandleScope* handles) {
    graph_ = CreateGraph(handles);
    propagation_ = new (GetAllocator()) ReferenceTypePropagation(graph_,
                                                                 Handle<mirror::ClassLoader>(),
                                                                 Handle<mirror::DexCache>(),
                                                                 true,
                                                                 "test_prop");
  }

  // Relay method to merge type in reference type propagation.
  ReferenceTypeInfo MergeTypes(const ReferenceTypeInfo& a,
                               const ReferenceTypeInfo& b) REQUIRES_SHARED(Locks::mutator_lock_) {
    return propagation_->MergeTypes(a, b, graph_->GetHandleCache());
  }

  // Helper method to construct an invalid type.
  ReferenceTypeInfo InvalidType() {
    return ReferenceTypeInfo::CreateInvalid();
  }

  // Helper method to construct the Object type.
  ReferenceTypeInfo ObjectType(bool is_exact = true) REQUIRES_SHARED(Locks::mutator_lock_) {
    return ReferenceTypeInfo::Create(graph_->GetHandleCache()->GetObjectClassHandle(), is_exact);
  }

  // Helper method to construct the String type.
  ReferenceTypeInfo StringType(bool is_exact = true) REQUIRES_SHARED(Locks::mutator_lock_) {
    return ReferenceTypeInfo::Create(graph_->GetHandleCache()->GetStringClassHandle(), is_exact);
  }

  // General building fields.
  HGraph* graph_;

  ReferenceTypePropagation* propagation_;
};

class ReferenceTypePropagationTest : public ReferenceTypePropagationTestBase<CommonCompilerTest> {};

enum class ShuffleOrder {
  kTopological,
  kReverseTopological,
  kAlmostTopological,
  kTrueRandom,
  kRandomSetSeed,

  kRandom = kUseTrueRandomness ? kTrueRandom : kRandomSetSeed,
};

std::ostream& operator<<(std::ostream& os, ShuffleOrder so) {
  switch (so) {
    case ShuffleOrder::kAlmostTopological:
      return os << "AlmostTopological";
    case ShuffleOrder::kReverseTopological:
      return os << "ReverseTopological";
    case ShuffleOrder::kTopological:
      return os << "Topological";
    case ShuffleOrder::kTrueRandom:
      return os << "TrueRandom";
    case ShuffleOrder::kRandomSetSeed:
      return os << "RandomSetSeed";
  }
}

template <typename Param>
class ParamReferenceTypePropagationTest
    : public ReferenceTypePropagationTestBase<CommonCompilerTestWithParam<Param>> {
 public:
  void MutateList(std::vector<HInstruction*>& lst, ShuffleOrder type);
};

class NonLoopReferenceTypePropagationTestGroup
    : public ParamReferenceTypePropagationTest<ShuffleOrder> {
 public:
  template <typename Func>
  void RunVisitListTest(Func mutator);
};

enum class InitialNullState {
  kAllNull,
  kAllNonNull,
  kHalfNull,
  kTrueRandom,
  kRandomSetSeed,

  kRandom = kUseTrueRandomness ? kTrueRandom : kRandomSetSeed,
};

std::ostream& operator<<(std::ostream& os, InitialNullState ni) {
  switch (ni) {
    case InitialNullState::kAllNull:
      return os << "AllNull";
    case InitialNullState::kAllNonNull:
      return os << "AllNonNull";
    case InitialNullState::kHalfNull:
      return os << "HalfNull";
    case InitialNullState::kTrueRandom:
      return os << "TrueRandom";
    case InitialNullState::kRandomSetSeed:
      return os << "RandomSetSeed";
  }
}

struct LoopOptions {
 public:
  using GtestParam = std::tuple<ShuffleOrder, ssize_t, size_t, InitialNullState>;
  explicit LoopOptions(GtestParam in) {
    std::tie(shuffle_, null_insertion_, null_phi_arg_, initial_null_state_) = in;
  }

  ShuffleOrder shuffle_;
  // Where in the list of phis we put the null. -1 if don't insert
  ssize_t null_insertion_;
  // Where in the phi arg-list we put the null.
  size_t null_phi_arg_;
  // What to set the initial null-state of all the phis to.
  InitialNullState initial_null_state_;
};

class LoopReferenceTypePropagationTestGroup
    : public ParamReferenceTypePropagationTest<LoopOptions::GtestParam> {
 public:
  template <typename Func>
  void RunVisitListTest(Func mutator);
};

//
// The actual ReferenceTypePropgation unit tests.
//

TEST_F(ReferenceTypePropagationTest, ProperSetup) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope handles(soa.Self());
  SetupPropagation(&handles);

  EXPECT_TRUE(propagation_ != nullptr);
  EXPECT_TRUE(graph_->GetInexactObjectRti().IsEqual(ObjectType(false)));
}

TEST_F(ReferenceTypePropagationTest, MergeInvalidTypes) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope handles(soa.Self());
  SetupPropagation(&handles);

  // Two invalid types.
  ReferenceTypeInfo t1(MergeTypes(InvalidType(), InvalidType()));
  EXPECT_FALSE(t1.IsValid());
  EXPECT_FALSE(t1.IsExact());
  EXPECT_TRUE(t1.IsEqual(InvalidType()));

  // Valid type on right.
  ReferenceTypeInfo t2(MergeTypes(InvalidType(), ObjectType()));
  EXPECT_TRUE(t2.IsValid());
  EXPECT_TRUE(t2.IsExact());
  EXPECT_TRUE(t2.IsEqual(ObjectType()));
  ReferenceTypeInfo t3(MergeTypes(InvalidType(), StringType()));
  EXPECT_TRUE(t3.IsValid());
  EXPECT_TRUE(t3.IsExact());
  EXPECT_TRUE(t3.IsEqual(StringType()));

  // Valid type on left.
  ReferenceTypeInfo t4(MergeTypes(ObjectType(), InvalidType()));
  EXPECT_TRUE(t4.IsValid());
  EXPECT_TRUE(t4.IsExact());
  EXPECT_TRUE(t4.IsEqual(ObjectType()));
  ReferenceTypeInfo t5(MergeTypes(StringType(), InvalidType()));
  EXPECT_TRUE(t5.IsValid());
  EXPECT_TRUE(t5.IsExact());
  EXPECT_TRUE(t5.IsEqual(StringType()));
}

TEST_F(ReferenceTypePropagationTest, MergeValidTypes) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope handles(soa.Self());
  SetupPropagation(&handles);

  // Same types.
  ReferenceTypeInfo t1(MergeTypes(ObjectType(), ObjectType()));
  EXPECT_TRUE(t1.IsValid());
  EXPECT_TRUE(t1.IsExact());
  EXPECT_TRUE(t1.IsEqual(ObjectType()));
  ReferenceTypeInfo t2(MergeTypes(StringType(), StringType()));
  EXPECT_TRUE(t2.IsValid());
  EXPECT_TRUE(t2.IsExact());
  EXPECT_TRUE(t2.IsEqual(StringType()));

  // Left is super class of right.
  ReferenceTypeInfo t3(MergeTypes(ObjectType(), StringType()));
  EXPECT_TRUE(t3.IsValid());
  EXPECT_FALSE(t3.IsExact());
  EXPECT_TRUE(t3.IsEqual(ObjectType(false)));

  // Right is super class of left.
  ReferenceTypeInfo t4(MergeTypes(StringType(), ObjectType()));
  EXPECT_TRUE(t4.IsValid());
  EXPECT_FALSE(t4.IsExact());
  EXPECT_TRUE(t4.IsEqual(ObjectType(false)));

  // Same types, but one or both are inexact.
  ReferenceTypeInfo t5(MergeTypes(ObjectType(false), ObjectType()));
  EXPECT_TRUE(t5.IsValid());
  EXPECT_FALSE(t5.IsExact());
  EXPECT_TRUE(t5.IsEqual(ObjectType(false)));
  ReferenceTypeInfo t6(MergeTypes(ObjectType(), ObjectType(false)));
  EXPECT_TRUE(t6.IsValid());
  EXPECT_FALSE(t6.IsExact());
  EXPECT_TRUE(t6.IsEqual(ObjectType(false)));
  ReferenceTypeInfo t7(MergeTypes(ObjectType(false), ObjectType(false)));
  EXPECT_TRUE(t7.IsValid());
  EXPECT_FALSE(t7.IsExact());
  EXPECT_TRUE(t7.IsEqual(ObjectType(false)));
}

// This generates a large graph with a ton of phis including loop-phis. It then
// calls the 'mutator' function with the list of all the phis and a CanBeNull
// instruction and then tries to propagate the types. mutator should reorder the
// list in some way and modify some phis in whatever way it wants. We verify
// everything worked by making sure every phi has valid type information.
template <typename Func>
void LoopReferenceTypePropagationTestGroup::RunVisitListTest(Func mutator) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope handles(soa.Self());
  SetupPropagation(&handles);
  // Make a well-connected graph with a lot of edges.
  constexpr size_t kNumBlocks = 100;
  constexpr size_t kTestMaxSuccessors = 3;
  std::vector<std::string> mid_blocks;
  for (auto i : Range(kNumBlocks)) {
    std::ostringstream oss;
    oss << "blk" << i;
    mid_blocks.push_back(oss.str());
  }
  // Create the edge list.
  std::vector<AdjacencyListGraph::Edge> edges;
  for (auto cur : Range(kNumBlocks)) {
    for (auto nxt : Range(cur + 1, std::min(cur + 1 + kTestMaxSuccessors, kNumBlocks))) {
      edges.emplace_back(mid_blocks[cur], mid_blocks[nxt]);
    }
  }
  // Add a loop.
  edges.emplace_back("start", mid_blocks.front());
  edges.emplace_back(mid_blocks.back(), mid_blocks.front());
  edges.emplace_back(mid_blocks.front(), "exit");

  AdjacencyListGraph alg(graph_, GetAllocator(), "start", "exit", edges);
  std::unordered_map<HBasicBlock*, HInstruction*> single_value;
  HInstruction* maybe_null_val = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kReference);
  ASSERT_TRUE(maybe_null_val->CanBeNull());
  // Setup the entry-block with the type to be propagated.
  HInstruction* cls =
      new (GetAllocator()) HLoadClass(graph_->GetCurrentMethod(),
                                      dex::TypeIndex(10),
                                      graph_->GetDexFile(),
                                      graph_->GetHandleCache()->GetObjectClassHandle(),
                                      false,
                                      0,
                                      false);
  HInstruction* new_inst =
      new (GetAllocator()) HNewInstance(cls,
                                        0,
                                        dex::TypeIndex(10),
                                        graph_->GetDexFile(),
                                        false,
                                        QuickEntrypointEnum::kQuickAllocObjectInitialized);
  single_value[alg.Get(mid_blocks.front())] = new_inst;
  HBasicBlock* start = alg.Get("start");
  start->AddInstruction(maybe_null_val);
  start->AddInstruction(cls);
  start->AddInstruction(new_inst);
  new_inst->SetReferenceTypeInfo(ObjectType(true));
  maybe_null_val->SetReferenceTypeInfo(ObjectType(true));
  single_value[start] = new_inst;

  // Setup all the other blocks with a single PHI
  auto range = MakeIterationRange(mid_blocks);
  auto succ_blocks = MakeTransformRange(range, [&](const auto& sv) { return alg.Get(sv); });
  for (HBasicBlock* blk : succ_blocks) {
    HPhi* phi_inst = new (GetAllocator()) HPhi(
        GetAllocator(), kNoRegNumber, blk->GetPredecessors().size(), DataType::Type::kReference);
    single_value[blk] = phi_inst;
  }
  for (HBasicBlock* blk : succ_blocks) {
    HInstruction* my_val = single_value[blk];
    for (const auto& [pred, index] : ZipCount(MakeIterationRange(blk->GetPredecessors()))) {
      CHECK(single_value[pred] != nullptr) << pred->GetBlockId() << " " << alg.GetName(pred);
      my_val->SetRawInputAt(index, single_value[pred]);
    }
  }
  for (HBasicBlock* blk : succ_blocks) {
    CHECK(single_value[blk]->IsPhi()) << blk->GetBlockId();
    blk->AddPhi(single_value[blk]->AsPhi());
  }
  auto vals = MakeTransformRange(succ_blocks, [&](HBasicBlock* blk) {
    DCHECK(single_value[blk]->IsPhi());
    return single_value[blk];
  });
  std::vector<HInstruction*> ins(vals.begin(), vals.end());
  CHECK(std::none_of(ins.begin(), ins.end(), [](auto x) { return x == nullptr; }));
  mutator(ins, maybe_null_val);
  propagation_->Visit(ArrayRef<HInstruction* const>(ins));
  bool is_nullable = !maybe_null_val->GetUses().empty();
  for (auto [blk, i] : single_value) {
    if (blk == start) {
      continue;
    }
    EXPECT_TRUE(i->GetReferenceTypeInfo().IsValid())
        << i->GetId() << " blk: " << alg.GetName(i->GetBlock());
    if (is_nullable) {
      EXPECT_TRUE(i->CanBeNull());
    } else {
      EXPECT_FALSE(i->CanBeNull());
    }
  }
}

// This generates a large graph with a ton of phis. It then calls the 'mutator'
// function with the list of all the phis and then tries to propagate the types.
// mutator should reorder the list in some way. We verify everything worked by
// making sure every phi has valid type information.
template <typename Func>
void NonLoopReferenceTypePropagationTestGroup::RunVisitListTest(Func mutator) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope handles(soa.Self());
  SetupPropagation(&handles);
  // Make a well-connected graph with a lot of edges.
  constexpr size_t kNumBlocks = 5000;
  constexpr size_t kTestMaxSuccessors = 2;
  std::vector<std::string> mid_blocks;
  for (auto i : Range(kNumBlocks)) {
    std::ostringstream oss;
    oss << "blk" << i;
    mid_blocks.push_back(oss.str());
  }
  // Create the edge list.
  std::vector<AdjacencyListGraph::Edge> edges;
  for (auto cur : Range(kNumBlocks)) {
    for (auto nxt : Range(cur + 1, std::min(cur + 1 + kTestMaxSuccessors, kNumBlocks))) {
      edges.emplace_back(mid_blocks[cur], mid_blocks[nxt]);
    }
  }
  AdjacencyListGraph alg(graph_, GetAllocator(), mid_blocks.front(), mid_blocks.back(), edges);
  std::unordered_map<HBasicBlock*, HInstruction*> single_value;
  // Setup the entry-block with the type to be propagated.
  HInstruction* cls =
      new (GetAllocator()) HLoadClass(graph_->GetCurrentMethod(),
                                      dex::TypeIndex(10),
                                      graph_->GetDexFile(),
                                      graph_->GetHandleCache()->GetObjectClassHandle(),
                                      false,
                                      0,
                                      false);
  HInstruction* new_inst =
      new (GetAllocator()) HNewInstance(cls,
                                        0,
                                        dex::TypeIndex(10),
                                        graph_->GetDexFile(),
                                        false,
                                        QuickEntrypointEnum::kQuickAllocObjectInitialized);
  single_value[alg.Get(mid_blocks.front())] = new_inst;
  HBasicBlock* start = alg.Get(mid_blocks.front());
  start->AddInstruction(cls);
  start->AddInstruction(new_inst);
  new_inst->SetReferenceTypeInfo(ObjectType(true));

  // Setup all the other blocks with a single PHI
  auto succ_blk_names = MakeIterationRange(mid_blocks.begin() + 1, mid_blocks.end());
  auto succ_blocks =
      MakeTransformRange(succ_blk_names, [&](const auto& sv) { return alg.Get(sv); });
  for (HBasicBlock* blk : succ_blocks) {
    HPhi* phi_inst = new (GetAllocator()) HPhi(
        GetAllocator(), kNoRegNumber, blk->GetPredecessors().size(), DataType::Type::kReference);
    single_value[blk] = phi_inst;
  }
  for (HBasicBlock* blk : succ_blocks) {
    HInstruction* my_val = single_value[blk];
    for (const auto& [pred, index] : ZipCount(MakeIterationRange(blk->GetPredecessors()))) {
      my_val->SetRawInputAt(index, single_value[pred]);
    }
    blk->AddPhi(my_val->AsPhi());
  }
  auto vals = MakeTransformRange(succ_blocks, [&](HBasicBlock* blk) { return single_value[blk]; });
  std::vector<HInstruction*> ins(vals.begin(), vals.end());
  graph_->ClearReachabilityInformation();
  graph_->ComputeReachabilityInformation();
  mutator(ins);
  propagation_->Visit(ArrayRef<HInstruction* const>(ins));
  for (auto [blk, i] : single_value) {
    if (blk == start) {
      continue;
    }
    EXPECT_TRUE(i->GetReferenceTypeInfo().IsValid())
        << i->GetId() << " blk: " << alg.GetName(i->GetBlock());
  }
}

template <typename Param>
void ParamReferenceTypePropagationTest<Param>::MutateList(std::vector<HInstruction*>& lst,
                                                          ShuffleOrder type) {
  DCHECK(std::none_of(lst.begin(), lst.end(), [](auto* i) { return i == nullptr; }));
  std::default_random_engine g(type != ShuffleOrder::kTrueRandom ? 42 : std::rand());
  switch (type) {
    case ShuffleOrder::kTopological: {
      // Input is topologically sorted due to the way we create the phis.
      break;
    }
    case ShuffleOrder::kReverseTopological: {
      std::reverse(lst.begin(), lst.end());
      break;
    }
    case ShuffleOrder::kAlmostTopological: {
      std::swap(lst.front(), lst.back());
      break;
    }
    case ShuffleOrder::kRandomSetSeed:
    case ShuffleOrder::kTrueRandom: {
      std::shuffle(lst.begin(), lst.end(), g);
      break;
    }
  }
}

TEST_P(LoopReferenceTypePropagationTestGroup, RunVisitTest) {
  LoopOptions lo(GetParam());
  std::default_random_engine g(
      lo.initial_null_state_ != InitialNullState::kTrueRandom ? 42 : std::rand());
  std::uniform_int_distribution<bool> uid(false, true);
  RunVisitListTest([&](std::vector<HInstruction*>& lst, HInstruction* null_input) {
    auto pred_null = false;
    auto next_null = [&]() {
      switch (lo.initial_null_state_) {
        case InitialNullState::kAllNonNull:
          return false;
        case InitialNullState::kAllNull:
          return true;
        case InitialNullState::kHalfNull:
          pred_null = !pred_null;
          return pred_null;
        case InitialNullState::kRandomSetSeed:
        case InitialNullState::kTrueRandom:
          return uid(g);
      }
    };
    HPhi* nulled_phi = lo.null_insertion_ >= 0 ? lst[lo.null_insertion_]->AsPhi() : nullptr;
    if (nulled_phi != nullptr) {
      nulled_phi->ReplaceInput(null_input, lo.null_phi_arg_);
    }
    MutateList(lst, lo.shuffle_);
    std::for_each(lst.begin(), lst.end(), [&](HInstruction* ins) {
      ins->AsPhi()->SetCanBeNull(next_null());
    });
  });
}

INSTANTIATE_TEST_SUITE_P(ReferenceTypePropagationTest,
                         LoopReferenceTypePropagationTestGroup,
                         testing::Combine(testing::Values(ShuffleOrder::kAlmostTopological,
                                                          ShuffleOrder::kReverseTopological,
                                                          ShuffleOrder::kTopological,
                                                          ShuffleOrder::kRandom),
                                          testing::Values(-1, 10, 40),
                                          testing::Values(0, 1),
                                          testing::Values(InitialNullState::kAllNonNull,
                                                          InitialNullState::kAllNull,
                                                          InitialNullState::kHalfNull,
                                                          InitialNullState::kRandom)));

TEST_P(NonLoopReferenceTypePropagationTestGroup, RunVisitTest) {
  RunVisitListTest([&](std::vector<HInstruction*>& lst) { MutateList(lst, GetParam()); });
}

INSTANTIATE_TEST_SUITE_P(ReferenceTypePropagationTest,
                         NonLoopReferenceTypePropagationTestGroup,
                         testing::Values(ShuffleOrder::kAlmostTopological,
                                         ShuffleOrder::kReverseTopological,
                                         ShuffleOrder::kTopological,
                                         ShuffleOrder::kRandom));

}  // namespace art
