/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <tuple>

#include "load_store_analysis.h"
#include "load_store_elimination.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "side_effects_analysis.h"

#include "gtest/gtest.h"

namespace art {

class LoadStoreEliminationTest : public ImprovedOptimizingUnitTest {
 public:
  void PerformLSE() {
    graph_->BuildDominatorTree();
    SideEffectsAnalysis side_effects(graph_);
    side_effects.Run();
    LoadStoreAnalysis lsa(graph_);
    lsa.Run();
    LoadStoreElimination lse(graph_, side_effects, lsa, nullptr);
    lse.Run();
    EXPECT_TRUE(CheckGraphSkipRefTypeInfoChecks());
  }

  // Create instructions shared among tests.
  void CreateEntryBlockInstructions() {
    HInstruction* c1 = graph_->GetIntConstant(1);
    HInstruction* c4 = graph_->GetIntConstant(4);
    i_add1_ = new (GetAllocator()) HAdd(DataType::Type::kInt32, i_, c1);
    i_add4_ = new (GetAllocator()) HAdd(DataType::Type::kInt32, i_, c4);
    entry_block_->AddInstruction(i_add1_);
    entry_block_->AddInstruction(i_add4_);
    entry_block_->AddInstruction(new (GetAllocator()) HGoto());
  }

  // Create the major CFG used by tests:
  //    entry
  //      |
  //  pre_header
  //      |
  //    loop[]
  //      |
  //   return
  //      |
  //     exit
  void CreateTestControlFlowGraph() {
    pre_header_ = new (GetAllocator()) HBasicBlock(graph_);
    loop_ = new (GetAllocator()) HBasicBlock(graph_);

    graph_->AddBlock(pre_header_);
    graph_->AddBlock(loop_);

    entry_block_->ReplaceSuccessor(return_block_, pre_header_);
    pre_header_->AddSuccessor(loop_);
    loop_->AddSuccessor(loop_);
    loop_->AddSuccessor(return_block_);

    HInstruction* c0 = graph_->GetIntConstant(0);
    HInstruction* c1 = graph_->GetIntConstant(1);
    HInstruction* c128 = graph_->GetIntConstant(128);

    CreateEntryBlockInstructions();

    // pre_header block
    //   phi = 0;
    phi_ = new (GetAllocator()) HPhi(GetAllocator(), 0, 0, DataType::Type::kInt32);
    loop_->AddPhi(phi_);
    pre_header_->AddInstruction(new (GetAllocator()) HGoto());
    phi_->AddInput(c0);

    // loop block:
    //   suspend_check
    //   phi++;
    //   if (phi >= 128)
    suspend_check_ = new (GetAllocator()) HSuspendCheck();
    HInstruction* inc_phi = new (GetAllocator()) HAdd(DataType::Type::kInt32, phi_, c1);
    HInstruction* cmp = new (GetAllocator()) HGreaterThanOrEqual(phi_, c128);
    HInstruction* hif = new (GetAllocator()) HIf(cmp);
    loop_->AddInstruction(suspend_check_);
    loop_->AddInstruction(inc_phi);
    loop_->AddInstruction(cmp);
    loop_->AddInstruction(hif);
    phi_->AddInput(inc_phi);

    CreateEnvForSuspendCheck();
  }

  void CreateEnvForSuspendCheck() {
    ArenaVector<HInstruction*> current_locals({array_, i_, j_},
                                              GetAllocator()->Adapter(kArenaAllocInstruction));
    ManuallyBuildEnvFor(suspend_check_, &current_locals);
  }

  // Create the diamond-shaped CFG:
  //      upper
  //      /   \
  //    left  right
  //      \   /
  //      down
  //
  // Return: the basic blocks forming the CFG in the following order {upper, left, right, down}.
  std::tuple<HBasicBlock*, HBasicBlock*, HBasicBlock*, HBasicBlock*> CreateDiamondShapedCFG() {
    CreateEntryBlockInstructions();

    HBasicBlock* upper = new (GetAllocator()) HBasicBlock(graph_);
    HBasicBlock* left = new (GetAllocator()) HBasicBlock(graph_);
    HBasicBlock* right = new (GetAllocator()) HBasicBlock(graph_);

    graph_->AddBlock(upper);
    graph_->AddBlock(left);
    graph_->AddBlock(right);

    entry_block_->ReplaceSuccessor(return_block_, upper);
    upper->AddSuccessor(left);
    upper->AddSuccessor(right);
    left->AddSuccessor(return_block_);
    right->AddSuccessor(return_block_);

    HInstruction* cmp = new (GetAllocator()) HGreaterThanOrEqual(i_, j_);
    HInstruction* hif = new (GetAllocator()) HIf(cmp);
    upper->AddInstruction(cmp);
    upper->AddInstruction(hif);

    left->AddInstruction(new (GetAllocator()) HGoto());
    right->AddInstruction(new (GetAllocator()) HGoto());

    return std::make_tuple(upper, left, right, return_block_);
  }

  // Add a HVecLoad instruction to the end of the provided basic block.
  //
  // Return: the created HVecLoad instruction.
  HInstruction* AddVecLoad(HBasicBlock* block, HInstruction* array, HInstruction* index) {
    DCHECK(block != nullptr);
    DCHECK(array != nullptr);
    DCHECK(index != nullptr);
    HInstruction* vload = new (GetAllocator()) HVecLoad(
        GetAllocator(),
        array,
        index,
        DataType::Type::kInt32,
        SideEffects::ArrayReadOfType(DataType::Type::kInt32),
        4,
        /*is_string_char_at*/ false,
        kNoDexPc);
    block->InsertInstructionBefore(vload, block->GetLastInstruction());
    return vload;
  }

  // Add a HVecStore instruction to the end of the provided basic block.
  // If no vdata is specified, generate HVecStore: array[index] = [1,1,1,1].
  //
  // Return: the created HVecStore instruction.
  HInstruction* AddVecStore(HBasicBlock* block,
                            HInstruction* array,
                            HInstruction* index,
                            HInstruction* vdata = nullptr) {
    DCHECK(block != nullptr);
    DCHECK(array != nullptr);
    DCHECK(index != nullptr);
    if (vdata == nullptr) {
      HInstruction* c1 = graph_->GetIntConstant(1);
      vdata = new (GetAllocator()) HVecReplicateScalar(GetAllocator(),
                                                       c1,
                                                       DataType::Type::kInt32,
                                                       4,
                                                       kNoDexPc);
      block->InsertInstructionBefore(vdata, block->GetLastInstruction());
    }
    HInstruction* vstore = new (GetAllocator()) HVecStore(
        GetAllocator(),
        array,
        index,
        vdata,
        DataType::Type::kInt32,
        SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
        4,
        kNoDexPc);
    block->InsertInstructionBefore(vstore, block->GetLastInstruction());
    return vstore;
  }

  // Add a HArrayGet instruction to the end of the provided basic block.
  //
  // Return: the created HArrayGet instruction.
  HInstruction* AddArrayGet(HBasicBlock* block, HInstruction* array, HInstruction* index) {
    DCHECK(block != nullptr);
    DCHECK(array != nullptr);
    DCHECK(index != nullptr);
    HInstruction* get = new (GetAllocator()) HArrayGet(array, index, DataType::Type::kInt32, 0);
    block->InsertInstructionBefore(get, block->GetLastInstruction());
    return get;
  }

  // Add a HArraySet instruction to the end of the provided basic block.
  // If no data is specified, generate HArraySet: array[index] = 1.
  //
  // Return: the created HArraySet instruction.
  HInstruction* AddArraySet(HBasicBlock* block,
                            HInstruction* array,
                            HInstruction* index,
                            HInstruction* data = nullptr) {
    DCHECK(block != nullptr);
    DCHECK(array != nullptr);
    DCHECK(index != nullptr);
    if (data == nullptr) {
      data = graph_->GetIntConstant(1);
    }
    HInstruction* store = new (GetAllocator()) HArraySet(array,
                                                         index,
                                                         data,
                                                         DataType::Type::kInt32,
                                                         0);
    block->InsertInstructionBefore(store, block->GetLastInstruction());
    return store;
  }

  void CreateParameters() override {
    parameters_.push_back(new (GetAllocator()) HParameterValue(graph_->GetDexFile(),
                                                               dex::TypeIndex(0),
                                                               0,
                                                               DataType::Type::kInt32));
    array_ = parameters_.back();
    parameters_.push_back(new (GetAllocator()) HParameterValue(graph_->GetDexFile(),
                                                               dex::TypeIndex(1),
                                                               1,
                                                               DataType::Type::kInt32));
    i_ = parameters_.back();
    parameters_.push_back(new (GetAllocator()) HParameterValue(graph_->GetDexFile(),
                                                               dex::TypeIndex(1),
                                                               2,
                                                               DataType::Type::kInt32));
    j_ = parameters_.back();
  }

  HBasicBlock* pre_header_;
  HBasicBlock* loop_;

  HInstruction* array_;
  HInstruction* i_;
  HInstruction* j_;
  HInstruction* i_add1_;
  HInstruction* i_add4_;
  HInstruction* suspend_check_;

  HPhi* phi_;
};

TEST_F(LoadStoreEliminationTest, ArrayGetSetElimination) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c3 = graph_->GetIntConstant(3);

  // array[1] = 1;
  // x = array[1];  <--- Remove.
  // y = array[2];
  // array[1] = 1;  <--- Remove, since it stores same value.
  // array[i] = 3;  <--- MAY alias.
  // array[1] = 1;  <--- Cannot remove, even if it stores the same value.
  AddArraySet(entry_block_, array_, c1, c1);
  HInstruction* load1 = AddArrayGet(entry_block_, array_, c1);
  HInstruction* load2 = AddArrayGet(entry_block_, array_, c2);
  HInstruction* store1 = AddArraySet(entry_block_, array_, c1, c1);
  AddArraySet(entry_block_, array_, i_, c3);
  HInstruction* store2 = AddArraySet(entry_block_, array_, c1, c1);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(load1));
  ASSERT_FALSE(IsRemoved(load2));
  ASSERT_TRUE(IsRemoved(store1));
  ASSERT_FALSE(IsRemoved(store2));
}

TEST_F(LoadStoreEliminationTest, SameHeapValue1) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);

  // Test LSE handling same value stores on array.
  // array[1] = 1;
  // array[2] = 1;
  // array[1] = 1;  <--- Can remove.
  // array[1] = 2;  <--- Can NOT remove.
  AddArraySet(entry_block_, array_, c1, c1);
  AddArraySet(entry_block_, array_, c2, c1);
  HInstruction* store1 = AddArraySet(entry_block_, array_, c1, c1);
  HInstruction* store2 = AddArraySet(entry_block_, array_, c1, c2);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(store1));
  ASSERT_FALSE(IsRemoved(store2));
}

TEST_F(LoadStoreEliminationTest, SameHeapValue2) {
  InitGraph();
  CreateTestControlFlowGraph();

  // Test LSE handling same value stores on vector.
  // vdata = [0x1, 0x2, 0x3, 0x4, ...]
  // VecStore array[i...] = vdata;
  // VecStore array[j...] = vdata;  <--- MAY ALIAS.
  // VecStore array[i...] = vdata;  <--- Cannot Remove, even if it's same value.
  AddVecStore(entry_block_, array_, i_);
  AddVecStore(entry_block_, array_, j_);
  HInstruction* vstore = AddVecStore(entry_block_, array_, i_);

  PerformLSE();

  ASSERT_FALSE(IsRemoved(vstore));
}

TEST_F(LoadStoreEliminationTest, SameHeapValue3) {
  InitGraph();
  CreateTestControlFlowGraph();

  // VecStore array[i...] = vdata;
  // VecStore array[i+1...] = vdata;  <--- MAY alias due to partial overlap.
  // VecStore array[i...] = vdata;    <--- Cannot remove, even if it's same value.
  AddVecStore(entry_block_, array_, i_);
  AddVecStore(entry_block_, array_, i_add1_);
  HInstruction* vstore = AddVecStore(entry_block_, array_, i_);

  PerformLSE();

  ASSERT_FALSE(IsRemoved(vstore));
}

TEST_F(LoadStoreEliminationTest, OverlappingLoadStore) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c1 = graph_->GetIntConstant(1);

  // Test LSE handling array LSE when there is vector store in between.
  // a[i] = 1;
  // .. = a[i];                <-- Remove.
  // a[i,i+1,i+2,i+3] = data;  <-- PARTIAL OVERLAP !
  // .. = a[i];                <-- Cannot remove.
  AddArraySet(entry_block_, array_, i_, c1);
  HInstruction* load1 = AddArrayGet(entry_block_, array_, i_);
  AddVecStore(entry_block_, array_, i_);
  HInstruction* load2 = AddArrayGet(entry_block_, array_, i_);

  // Test LSE handling vector load/store partial overlap.
  // a[i,i+1,i+2,i+3] = data;
  // a[i+4,i+5,i+6,i+7] = data;
  // .. = a[i,i+1,i+2,i+3];
  // .. = a[i+4,i+5,i+6,i+7];
  // a[i+1,i+2,i+3,i+4] = data;  <-- PARTIAL OVERLAP !
  // .. = a[i,i+1,i+2,i+3];
  // .. = a[i+4,i+5,i+6,i+7];
  AddVecStore(entry_block_, array_, i_);
  AddVecStore(entry_block_, array_, i_add4_);
  HInstruction* vload1 = AddVecLoad(entry_block_, array_, i_);
  HInstruction* vload2 = AddVecLoad(entry_block_, array_, i_add4_);
  AddVecStore(entry_block_, array_, i_add1_);
  HInstruction* vload3 = AddVecLoad(entry_block_, array_, i_);
  HInstruction* vload4 = AddVecLoad(entry_block_, array_, i_add4_);

  // Test LSE handling vector LSE when there is array store in between.
  // a[i,i+1,i+2,i+3] = data;
  // a[i+1] = 1;                 <-- PARTIAL OVERLAP !
  // .. = a[i,i+1,i+2,i+3];
  AddVecStore(entry_block_, array_, i_);
  AddArraySet(entry_block_, array_, i_, c1);
  HInstruction* vload5 = AddVecLoad(entry_block_, array_, i_);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(load1));
  ASSERT_FALSE(IsRemoved(load2));

  ASSERT_TRUE(IsRemoved(vload1));
  ASSERT_TRUE(IsRemoved(vload2));
  ASSERT_FALSE(IsRemoved(vload3));
  ASSERT_FALSE(IsRemoved(vload4));

  ASSERT_FALSE(IsRemoved(vload5));
}
// function (int[] a, int j) {
// a[j] = 1;
// for (int i=0; i<128; i++) {
//    /* doesn't do any write */
// }
// a[j] = 1;
TEST_F(LoadStoreEliminationTest, StoreAfterLoopWithoutSideEffects) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c1 = graph_->GetIntConstant(1);

  // a[j] = 1
  AddArraySet(pre_header_, array_, j_, c1);

  // LOOP BODY:
  // .. = a[i,i+1,i+2,i+3];
  AddVecLoad(loop_, array_, phi_);

  // a[j] = 1;
  HInstruction* array_set = AddArraySet(return_block_, array_, j_, c1);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(array_set));
}

// function (int[] a, int j) {
//   int[] b = new int[128];
//   a[j] = 0;
//   for (int phi=0; phi<128; phi++) {
//     a[phi,phi+1,phi+2,phi+3] = [1,1,1,1];
//     b[phi,phi+1,phi+2,phi+3] = a[phi,phi+1,phi+2,phi+3];
//   }
//   a[j] = 0;
// }
TEST_F(LoadStoreEliminationTest, StoreAfterSIMDLoopWithSideEffects) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_b = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_b, pre_header_->GetLastInstruction());
  array_b->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // a[j] = 0;
  AddArraySet(pre_header_, array_, j_, c0);

  // LOOP BODY:
  // a[phi,phi+1,phi+2,phi+3] = [1,1,1,1];
  // b[phi,phi+1,phi+2,phi+3] = a[phi,phi+1,phi+2,phi+3];
  AddVecStore(loop_, array_, phi_);
  HInstruction* vload = AddVecLoad(loop_, array_, phi_);
  AddVecStore(loop_, array_b, phi_, vload->AsVecLoad());

  // a[j] = 0;
  HInstruction* a_set = AddArraySet(return_block_, array_, j_, c0);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(vload));
  ASSERT_FALSE(IsRemoved(a_set));  // Cannot remove due to write side-effect in the loop.
}

// function (int[] a, int j) {
//   int[] b = new int[128];
//   a[j] = 0;
//   for (int phi=0; phi<128; phi++) {
//     a[phi,phi+1,phi+2,phi+3] = [1,1,1,1];
//     b[phi,phi+1,phi+2,phi+3] = a[phi,phi+1,phi+2,phi+3];
//   }
//   x = a[j];
// }
TEST_F(LoadStoreEliminationTest, LoadAfterSIMDLoopWithSideEffects) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_b = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_b, pre_header_->GetLastInstruction());
  array_b->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // a[j] = 0;
  AddArraySet(pre_header_, array_, j_, c0);

  // LOOP BODY:
  // a[phi,phi+1,phi+2,phi+3] = [1,1,1,1];
  // b[phi,phi+1,phi+2,phi+3] = a[phi,phi+1,phi+2,phi+3];
  AddVecStore(loop_, array_, phi_);
  HInstruction* vload = AddVecLoad(loop_, array_, phi_);
  AddVecStore(loop_, array_b, phi_, vload->AsVecLoad());

  // x = a[j];
  HInstruction* load = AddArrayGet(return_block_, array_, j_);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(vload));
  ASSERT_FALSE(IsRemoved(load));  // Cannot remove due to write side-effect in the loop.
}

// Check that merging works correctly when there are VecStors in predecessors.
//
//                  vstore1: a[i,... i + 3] = [1,...1]
//                       /          \
//                      /            \
// vstore2: a[i,... i + 3] = [1,...1]  vstore3: a[i+1, ... i + 4] = [1, ... 1]
//                     \              /
//                      \            /
//                  vstore4: a[i,... i + 3] = [1,...1]
//
// Expected:
//   'vstore2' is removed.
//   'vstore3' is not removed.
//   'vstore4' is not removed. Such cases are not supported at the moment.
TEST_F(LoadStoreEliminationTest, MergePredecessorVecStores) {
  InitGraph();

  HBasicBlock* upper;
  HBasicBlock* left;
  HBasicBlock* right;
  HBasicBlock* down;
  std::tie(upper, left, right, down) = CreateDiamondShapedCFG();

  // upper: a[i,... i + 3] = [1,...1]
  HInstruction* vstore1 = AddVecStore(upper, array_, i_);
  HInstruction* vdata = vstore1->InputAt(2);

  // left: a[i,... i + 3] = [1,...1]
  HInstruction* vstore2 = AddVecStore(left, array_, i_, vdata);

  // right: a[i+1, ... i + 4] = [1, ... 1]
  HInstruction* vstore3 = AddVecStore(right, array_, i_add1_, vdata);

  // down: a[i,... i + 3] = [1,...1]
  HInstruction* vstore4 = AddVecStore(down, array_, i_, vdata);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(vstore2));
  ASSERT_FALSE(IsRemoved(vstore3));
  ASSERT_FALSE(IsRemoved(vstore4));
}

// Check that merging works correctly when there are ArraySets in predecessors.
//
//          a[i] = 1
//        /          \
//       /            \
// store1: a[i] = 1  store2: a[i+1] = 1
//       \            /
//        \          /
//          store3: a[i] = 1
//
// Expected:
//   'store1' is removed.
//   'store2' is not removed.
//   'store3' is removed.
TEST_F(LoadStoreEliminationTest, MergePredecessorStores) {
  InitGraph();

  HBasicBlock* upper;
  HBasicBlock* left;
  HBasicBlock* right;
  HBasicBlock* down;
  std::tie(upper, left, right, down) = CreateDiamondShapedCFG();

  // upper: a[i,... i + 3] = [1,...1]
  AddArraySet(upper, array_, i_);

  // left: a[i,... i + 3] = [1,...1]
  HInstruction* store1 = AddArraySet(left, array_, i_);

  // right: a[i+1, ... i + 4] = [1, ... 1]
  HInstruction* store2 = AddArraySet(right, array_, i_add1_);

  // down: a[i,... i + 3] = [1,...1]
  HInstruction* store3 = AddArraySet(down, array_, i_);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(store1));
  ASSERT_FALSE(IsRemoved(store2));
  ASSERT_TRUE(IsRemoved(store3));
}

// Check that redundant VStore/VLoad are removed from a SIMD loop.
//
//  LOOP BODY
//     vstore1: a[i,... i + 3] = [1,...1]
//     vload:   x = a[i,... i + 3]
//     vstore2: b[i,... i + 3] = x
//     vstore3: a[i,... i + 3] = [1,...1]
//
// Expected:
//   'vstore1' is not removed.
//   'vload' is removed.
//   'vstore3' is removed.
TEST_F(LoadStoreEliminationTest, RedundantVStoreVLoadInLoop) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  HInstruction* array_b = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_b, pre_header_->GetLastInstruction());
  array_b->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // LOOP BODY:
  //    a[i,... i + 3] = [1,...1]
  //    x = a[i,... i + 3]
  //    b[i,... i + 3] = x
  //    a[i,... i + 3] = [1,...1]
  HInstruction* vstore1 = AddVecStore(loop_, array_a, phi_);
  HInstruction* vload = AddVecLoad(loop_, array_a, phi_);
  AddVecStore(loop_, array_b, phi_, vload->AsVecLoad());
  HInstruction* vstore3 = AddVecStore(loop_, array_a, phi_, vstore1->InputAt(2));

  PerformLSE();

  ASSERT_FALSE(IsRemoved(vstore1));
  ASSERT_TRUE(IsRemoved(vload));
  ASSERT_TRUE(IsRemoved(vstore3));
}

// Loop write side effects invalidate all stores.
// This causes stores after such loops not to be removed, even
// their values are known.
TEST_F(LoadStoreEliminationTest, StoreAfterLoopWithSideEffects) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c128 = graph_->GetIntConstant(128);

  // array[0] = 2;
  // loop:
  //   b[i] = array[i]
  // array[0] = 2
  AddArraySet(entry_block_, array_, c0, c2);

  HInstruction* array_b = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_b, pre_header_->GetLastInstruction());
  array_b->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  HInstruction* load = AddArrayGet(loop_, array_, phi_);
  AddArraySet(loop_, array_b, phi_, load);

  HInstruction* store = AddArraySet(return_block_, array_, c0, c2);

  PerformLSE();

  ASSERT_FALSE(IsRemoved(store));
}

// As it is not allowed to use defaults for VecLoads, check if there is a new created array
// a VecLoad used in a loop and after it is not replaced with a default.
TEST_F(LoadStoreEliminationTest, VLoadDefaultValueInLoopWithoutWriteSideEffects) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // LOOP BODY:
  //    v = a[i,... i + 3]
  // array[0,... 3] = v
  HInstruction* vload = AddVecLoad(loop_, array_a, phi_);
  HInstruction* vstore = AddVecStore(return_block_, array_, c0, vload->AsVecLoad());

  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload));
  ASSERT_FALSE(IsRemoved(vstore));
}

// As it is not allowed to use defaults for VecLoads, check if there is a new created array
// a VecLoad is not replaced with a default.
TEST_F(LoadStoreEliminationTest, VLoadDefaultValue) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // v = a[0,... 3]
  // array[0,... 3] = v
  HInstruction* vload = AddVecLoad(pre_header_, array_a, c0);
  HInstruction* vstore = AddVecStore(return_block_, array_, c0, vload->AsVecLoad());

  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload));
  ASSERT_FALSE(IsRemoved(vstore));
}

// As it is allowed to use defaults for ordinary loads, check if there is a new created array
// a load used in a loop and after it is replaced with a default.
TEST_F(LoadStoreEliminationTest, LoadDefaultValueInLoopWithoutWriteSideEffects) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // LOOP BODY:
  //    v = a[i]
  // array[0] = v
  HInstruction* load = AddArrayGet(loop_, array_a, phi_);
  HInstruction* store = AddArraySet(return_block_, array_, c0, load);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(load));
  ASSERT_FALSE(IsRemoved(store));
}

// As it is allowed to use defaults for ordinary loads, check if there is a new created array
// a load is replaced with a default.
TEST_F(LoadStoreEliminationTest, LoadDefaultValue) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // v = a[0]
  // array[0] = v
  HInstruction* load = AddArrayGet(pre_header_, array_a, c0);
  HInstruction* store = AddArraySet(return_block_, array_, c0, load);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(load));
  ASSERT_FALSE(IsRemoved(store));
}

// As it is not allowed to use defaults for VecLoads but allowed for regular loads,
// check if there is a new created array, a VecLoad and a load used in a loop and after it,
// VecLoad is not replaced with a default but the load is.
TEST_F(LoadStoreEliminationTest, VLoadAndLoadDefaultValueInLoopWithoutWriteSideEffects) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // LOOP BODY:
  //    v = a[i,... i + 3]
  //    v1 = a[i]
  // array[0,... 3] = v
  // array[0] = v1
  HInstruction* vload = AddVecLoad(loop_, array_a, phi_);
  HInstruction* load = AddArrayGet(loop_, array_a, phi_);
  HInstruction* vstore = AddVecStore(return_block_, array_, c0, vload->AsVecLoad());
  HInstruction* store = AddArraySet(return_block_, array_, c0, load);

  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload));
  ASSERT_TRUE(IsRemoved(load));
  ASSERT_FALSE(IsRemoved(vstore));
  ASSERT_FALSE(IsRemoved(store));
}

// As it is not allowed to use defaults for VecLoads but allowed for regular loads,
// check if there is a new created array, a VecLoad and a load,
// VecLoad is not replaced with a default but the load is.
TEST_F(LoadStoreEliminationTest, VLoadAndLoadDefaultValue) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // v = a[0,... 3]
  // v1 = a[0]
  // array[0,... 3] = v
  // array[0] = v1
  HInstruction* vload = AddVecLoad(pre_header_, array_a, c0);
  HInstruction* load = AddArrayGet(pre_header_, array_a, c0);
  HInstruction* vstore = AddVecStore(return_block_, array_, c0, vload->AsVecLoad());
  HInstruction* store = AddArraySet(return_block_, array_, c0, load);

  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload));
  ASSERT_TRUE(IsRemoved(load));
  ASSERT_FALSE(IsRemoved(vstore));
  ASSERT_FALSE(IsRemoved(store));
}

// It is not allowed to use defaults for VecLoads. However it should not prevent from removing
// loads getting the same value.
// Check a load getting a known value is eliminated (a loop test case).
TEST_F(LoadStoreEliminationTest, VLoadDefaultValueAndVLoadInLoopWithoutWriteSideEffects) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // LOOP BODY:
  //    v = a[i,... i + 3]
  //    v1 = a[i,... i + 3]
  // array[0,... 3] = v
  // array[128,... 131] = v1
  HInstruction* vload1 = AddVecLoad(loop_, array_a, phi_);
  HInstruction* vload2 = AddVecLoad(loop_, array_a, phi_);
  HInstruction* vstore1 = AddVecStore(return_block_, array_, c0, vload1->AsVecLoad());
  HInstruction* vstore2 = AddVecStore(return_block_, array_, c128, vload2->AsVecLoad());

  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload1));
  ASSERT_TRUE(IsRemoved(vload2));
  ASSERT_FALSE(IsRemoved(vstore1));
  ASSERT_FALSE(IsRemoved(vstore2));
}

// It is not allowed to use defaults for VecLoads. However it should not prevent from removing
// loads getting the same value.
// Check a load getting a known value is eliminated.
TEST_F(LoadStoreEliminationTest, VLoadDefaultValueAndVLoad) {
  InitGraph();
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // v = a[0,... 3]
  // v1 = a[0,... 3]
  // array[0,... 3] = v
  // array[128,... 131] = v1
  HInstruction* vload1 = AddVecLoad(pre_header_, array_a, c0);
  HInstruction* vload2 = AddVecLoad(pre_header_, array_a, c0);
  HInstruction* vstore1 = AddVecStore(return_block_, array_, c0, vload1->AsVecLoad());
  HInstruction* vstore2 = AddVecStore(return_block_, array_, c128, vload2->AsVecLoad());

  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload1));
  ASSERT_TRUE(IsRemoved(vload2));
  ASSERT_FALSE(IsRemoved(vstore1));
  ASSERT_FALSE(IsRemoved(vstore2));
}

}  // namespace art
