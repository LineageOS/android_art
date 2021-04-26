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

#include <array>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "base/scoped_arena_allocator.h"
#include "class_root.h"
#include "dex/dex_file_types.h"
#include "dex/method_reference.h"
#include "entrypoints/quick/quick_entrypoints_enum.h"
#include "execution_subgraph.h"
#include "execution_subgraph_test.h"
#include "gtest/gtest.h"
#include "handle.h"
#include "handle_scope.h"
#include "nodes.h"
#include "optimizing/data_type.h"
#include "optimizing_unit_test.h"
#include "scoped_thread_state_change.h"

namespace art {

class LoadStoreAnalysisTest : public CommonCompilerTest, public OptimizingUnitTestHelper {
 public:
  LoadStoreAnalysisTest() {}

  AdjacencyListGraph SetupFromAdjacencyList(
      const std::string_view entry_name,
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
  void CheckReachability(const AdjacencyListGraph& adj,
                         const std::vector<AdjacencyListGraph::Edge>& reach);
};

TEST_F(LoadStoreAnalysisTest, ArrayHeapLocations) {
  CreateGraph();
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);

  // entry:
  // array         ParameterValue
  // index         ParameterValue
  // c1            IntConstant
  // c2            IntConstant
  // c3            IntConstant
  // array_get1    ArrayGet [array, c1]
  // array_get2    ArrayGet [array, c2]
  // array_set1    ArraySet [array, c1, c3]
  // array_set2    ArraySet [array, index, c3]
  HInstruction* array = new (GetAllocator()) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(0), 0, DataType::Type::kReference);
  HInstruction* index = new (GetAllocator()) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kInt32);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c3 = graph_->GetIntConstant(3);
  HInstruction* array_get1 = new (GetAllocator()) HArrayGet(array, c1, DataType::Type::kInt32, 0);
  HInstruction* array_get2 = new (GetAllocator()) HArrayGet(array, c2, DataType::Type::kInt32, 0);
  HInstruction* array_set1 =
      new (GetAllocator()) HArraySet(array, c1, c3, DataType::Type::kInt32, 0);
  HInstruction* array_set2 =
      new (GetAllocator()) HArraySet(array, index, c3, DataType::Type::kInt32, 0);
  entry->AddInstruction(array);
  entry->AddInstruction(index);
  entry->AddInstruction(array_get1);
  entry->AddInstruction(array_get2);
  entry->AddInstruction(array_set1);
  entry->AddInstruction(array_set2);

  // Test HeapLocationCollector initialization.
  // Should be no heap locations, no operations on the heap.
  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  HeapLocationCollector heap_location_collector(graph_, &allocator, LoadStoreAnalysisType::kFull);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 0U);
  ASSERT_FALSE(heap_location_collector.HasHeapStores());

  // Test that after visiting the graph_, it must see following heap locations
  // array[c1], array[c2], array[index]; and it should see heap stores.
  heap_location_collector.VisitBasicBlock(entry);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 3U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's ref info and index records.
  ReferenceInfo* ref = heap_location_collector.FindReferenceInfoOf(array);
  DataType::Type type = DataType::Type::kInt32;
  size_t field = HeapLocation::kInvalidFieldOffset;
  size_t vec = HeapLocation::kScalar;
  size_t class_def = HeapLocation::kDeclaringClassDefIndexForArrays;
  size_t loc1 = heap_location_collector.FindHeapLocationIndex(
      ref, type, field, c1, vec, class_def);
  size_t loc2 = heap_location_collector.FindHeapLocationIndex(
      ref, type, field, c2, vec, class_def);
  size_t loc3 = heap_location_collector.FindHeapLocationIndex(
      ref, type, field, index, vec, class_def);
  // must find this reference info for array in HeapLocationCollector.
  ASSERT_TRUE(ref != nullptr);
  // must find these heap locations;
  // and array[1], array[2], array[3] should be different heap locations.
  ASSERT_TRUE(loc1 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc2 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc3 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc1 != loc2);
  ASSERT_TRUE(loc2 != loc3);
  ASSERT_TRUE(loc1 != loc3);

  // Test alias relationships after building aliasing matrix.
  // array[1] and array[2] clearly should not alias;
  // array[index] should alias with the others, because index is an unknow value.
  heap_location_collector.BuildAliasingMatrix();
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc3));
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc3));

  EXPECT_TRUE(CheckGraph(graph_));
}

TEST_F(LoadStoreAnalysisTest, FieldHeapLocations) {
  CreateGraph();
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);

  // entry:
  // object              ParameterValue
  // c1                  IntConstant
  // set_field10         InstanceFieldSet [object, c1, 10]
  // get_field10         InstanceFieldGet [object, 10]
  // get_field20         InstanceFieldGet [object, 20]

  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* object = new (GetAllocator()) HParameterValue(graph_->GetDexFile(),
                                                              dex::TypeIndex(0),
                                                              0,
                                                              DataType::Type::kReference);
  HInstanceFieldSet* set_field10 = new (GetAllocator()) HInstanceFieldSet(object,
                                                                          c1,
                                                                          nullptr,
                                                                          DataType::Type::kInt32,
                                                                          MemberOffset(32),
                                                                          false,
                                                                          kUnknownFieldIndex,
                                                                          kUnknownClassDefIndex,
                                                                          graph_->GetDexFile(),
                                                                          0);
  HInstanceFieldGet* get_field10 = new (GetAllocator()) HInstanceFieldGet(object,
                                                                          nullptr,
                                                                          DataType::Type::kInt32,
                                                                          MemberOffset(32),
                                                                          false,
                                                                          kUnknownFieldIndex,
                                                                          kUnknownClassDefIndex,
                                                                          graph_->GetDexFile(),
                                                                          0);
  HInstanceFieldGet* get_field20 = new (GetAllocator()) HInstanceFieldGet(object,
                                                                          nullptr,
                                                                          DataType::Type::kInt32,
                                                                          MemberOffset(20),
                                                                          false,
                                                                          kUnknownFieldIndex,
                                                                          kUnknownClassDefIndex,
                                                                          graph_->GetDexFile(),
                                                                          0);
  entry->AddInstruction(object);
  entry->AddInstruction(set_field10);
  entry->AddInstruction(get_field10);
  entry->AddInstruction(get_field20);

  // Test HeapLocationCollector initialization.
  // Should be no heap locations, no operations on the heap.
  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  HeapLocationCollector heap_location_collector(graph_, &allocator, LoadStoreAnalysisType::kFull);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 0U);
  ASSERT_FALSE(heap_location_collector.HasHeapStores());

  // Test that after visiting the graph, it must see following heap locations
  // object.field10, object.field20 and it should see heap stores.
  heap_location_collector.VisitBasicBlock(entry);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 2U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's ref info and index records.
  ReferenceInfo* ref = heap_location_collector.FindReferenceInfoOf(object);
  size_t loc1 = heap_location_collector.GetFieldHeapLocation(object, &get_field10->GetFieldInfo());
  size_t loc2 = heap_location_collector.GetFieldHeapLocation(object, &get_field20->GetFieldInfo());
  // must find references info for object and in HeapLocationCollector.
  ASSERT_TRUE(ref != nullptr);
  // must find these heap locations.
  ASSERT_TRUE(loc1 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc2 != HeapLocationCollector::kHeapLocationNotFound);
  // different fields of same object.
  ASSERT_TRUE(loc1 != loc2);
  // accesses to different fields of the same object should not alias.
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  EXPECT_TRUE(CheckGraph(graph_));
}

TEST_F(LoadStoreAnalysisTest, ArrayIndexAliasingTest) {
  CreateGraph();
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  graph_->BuildDominatorTree();

  HInstruction* array = new (GetAllocator()) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(0), 0, DataType::Type::kReference);
  HInstruction* index = new (GetAllocator()) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kInt32);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c_neg1 = graph_->GetIntConstant(-1);
  HInstruction* add0 = new (GetAllocator()) HAdd(DataType::Type::kInt32, index, c0);
  HInstruction* add1 = new (GetAllocator()) HAdd(DataType::Type::kInt32, index, c1);
  HInstruction* sub0 = new (GetAllocator()) HSub(DataType::Type::kInt32, index, c0);
  HInstruction* sub1 = new (GetAllocator()) HSub(DataType::Type::kInt32, index, c1);
  HInstruction* sub_neg1 = new (GetAllocator()) HSub(DataType::Type::kInt32, index, c_neg1);
  HInstruction* rev_sub1 = new (GetAllocator()) HSub(DataType::Type::kInt32, c1, index);
  HInstruction* arr_set1 = new (GetAllocator()) HArraySet(array, c0, c0, DataType::Type::kInt32, 0);
  HInstruction* arr_set2 = new (GetAllocator()) HArraySet(array, c1, c0, DataType::Type::kInt32, 0);
  HInstruction* arr_set3 =
      new (GetAllocator()) HArraySet(array, add0, c0, DataType::Type::kInt32, 0);
  HInstruction* arr_set4 =
      new (GetAllocator()) HArraySet(array, add1, c0, DataType::Type::kInt32, 0);
  HInstruction* arr_set5 =
      new (GetAllocator()) HArraySet(array, sub0, c0, DataType::Type::kInt32, 0);
  HInstruction* arr_set6 =
      new (GetAllocator()) HArraySet(array, sub1, c0, DataType::Type::kInt32, 0);
  HInstruction* arr_set7 =
      new (GetAllocator()) HArraySet(array, rev_sub1, c0, DataType::Type::kInt32, 0);
  HInstruction* arr_set8 =
      new (GetAllocator()) HArraySet(array, sub_neg1, c0, DataType::Type::kInt32, 0);

  entry->AddInstruction(array);
  entry->AddInstruction(index);
  entry->AddInstruction(add0);
  entry->AddInstruction(add1);
  entry->AddInstruction(sub0);
  entry->AddInstruction(sub1);
  entry->AddInstruction(sub_neg1);
  entry->AddInstruction(rev_sub1);

  entry->AddInstruction(arr_set1);  // array[0] = c0
  entry->AddInstruction(arr_set2);  // array[1] = c0
  entry->AddInstruction(arr_set3);  // array[i+0] = c0
  entry->AddInstruction(arr_set4);  // array[i+1] = c0
  entry->AddInstruction(arr_set5);  // array[i-0] = c0
  entry->AddInstruction(arr_set6);  // array[i-1] = c0
  entry->AddInstruction(arr_set7);  // array[1-i] = c0
  entry->AddInstruction(arr_set8);  // array[i-(-1)] = c0

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kBasic);
  lsa.Run();
  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();

  // LSA/HeapLocationCollector should see those ArrayGet instructions.
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 8U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's aliasing matrix after load store analysis.
  size_t loc1 = HeapLocationCollector::kHeapLocationNotFound;
  size_t loc2 = HeapLocationCollector::kHeapLocationNotFound;

  // Test alias: array[0] and array[1]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set1);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set2);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0] and array[i-0]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set3);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set5);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+1] and array[i-1]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set4);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set6);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+1] and array[1-i]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set4);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set7);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+1] and array[i-(-1)]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set4);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set8);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  EXPECT_TRUE(CheckGraphSkipRefTypeInfoChecks(graph_));
}

TEST_F(LoadStoreAnalysisTest, ArrayAliasingTest) {
  CreateGraph();
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  graph_->BuildDominatorTree();

  HInstruction* array = new (GetAllocator()) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(0), 0, DataType::Type::kReference);
  HInstruction* index = new (GetAllocator()) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kInt32);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c6 = graph_->GetIntConstant(6);
  HInstruction* c8 = graph_->GetIntConstant(8);

  HInstruction* arr_set_0 = new (GetAllocator()) HArraySet(array,
                                                           c0,
                                                           c0,
                                                           DataType::Type::kInt32,
                                                           0);
  HInstruction* arr_set_1 = new (GetAllocator()) HArraySet(array,
                                                           c1,
                                                           c0,
                                                           DataType::Type::kInt32,
                                                           0);
  HInstruction* arr_set_i = new (GetAllocator()) HArraySet(array,
                                                           index,
                                                           c0,
                                                           DataType::Type::kInt32,
                                                           0);

  HVecOperation* v1 = new (GetAllocator()) HVecReplicateScalar(GetAllocator(),
                                                               c1,
                                                               DataType::Type::kInt32,
                                                               4,
                                                               kNoDexPc);
  HVecOperation* v2 = new (GetAllocator()) HVecReplicateScalar(GetAllocator(),
                                                               c1,
                                                               DataType::Type::kInt32,
                                                               2,
                                                               kNoDexPc);
  HInstruction* i_add6 = new (GetAllocator()) HAdd(DataType::Type::kInt32, index, c6);
  HInstruction* i_add8 = new (GetAllocator()) HAdd(DataType::Type::kInt32, index, c8);

  HInstruction* vstore_0 = new (GetAllocator()) HVecStore(
      GetAllocator(),
      array,
      c0,
      v1,
      DataType::Type::kInt32,
      SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
      4,
      kNoDexPc);
  HInstruction* vstore_1 = new (GetAllocator()) HVecStore(
      GetAllocator(),
      array,
      c1,
      v1,
      DataType::Type::kInt32,
      SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
      4,
      kNoDexPc);
  HInstruction* vstore_8 = new (GetAllocator()) HVecStore(
      GetAllocator(),
      array,
      c8,
      v1,
      DataType::Type::kInt32,
      SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
      4,
      kNoDexPc);
  HInstruction* vstore_i = new (GetAllocator()) HVecStore(
      GetAllocator(),
      array,
      index,
      v1,
      DataType::Type::kInt32,
      SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
      4,
      kNoDexPc);
  HInstruction* vstore_i_add6 = new (GetAllocator()) HVecStore(
      GetAllocator(),
      array,
      i_add6,
      v1,
      DataType::Type::kInt32,
      SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
      4,
      kNoDexPc);
  HInstruction* vstore_i_add8 = new (GetAllocator()) HVecStore(
      GetAllocator(),
      array,
      i_add8,
      v1,
      DataType::Type::kInt32,
      SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
      4,
      kNoDexPc);
  HInstruction* vstore_i_add6_vlen2 = new (GetAllocator()) HVecStore(
      GetAllocator(),
      array,
      i_add6,
      v2,
      DataType::Type::kInt32,
      SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
      2,
      kNoDexPc);

  entry->AddInstruction(array);
  entry->AddInstruction(index);

  entry->AddInstruction(arr_set_0);
  entry->AddInstruction(arr_set_1);
  entry->AddInstruction(arr_set_i);
  entry->AddInstruction(v1);
  entry->AddInstruction(v2);
  entry->AddInstruction(i_add6);
  entry->AddInstruction(i_add8);
  entry->AddInstruction(vstore_0);
  entry->AddInstruction(vstore_1);
  entry->AddInstruction(vstore_8);
  entry->AddInstruction(vstore_i);
  entry->AddInstruction(vstore_i_add6);
  entry->AddInstruction(vstore_i_add8);
  entry->AddInstruction(vstore_i_add6_vlen2);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kBasic);
  lsa.Run();
  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();

  // LSA/HeapLocationCollector should see those instructions.
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 10U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's aliasing matrix after load store analysis.
  size_t loc1, loc2;

  // Test alias: array[0] and array[0,1,2,3]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_0);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[0] and array[1,2,3,4]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_1);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[0] and array[8,9,10,11]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_8);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[1] and array[8,9,10,11]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_1);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_8);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[1] and array[0,1,2,3]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_1);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_0);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[0,1,2,3] and array[8,9,10,11]
  loc1 = heap_location_collector.GetArrayHeapLocation(vstore_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_8);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[0,1,2,3] and array[1,2,3,4]
  loc1 = heap_location_collector.GetArrayHeapLocation(vstore_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_1);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[0] and array[i,i+1,i+2,i+3]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_0);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i] and array[0,1,2,3]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_i);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_0);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i] and array[i,i+1,i+2,i+3]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_i);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i] and array[i+8,i+9,i+10,i+11]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_i);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i_add8);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+6,i+7,i+8,i+9] and array[i+8,i+9,i+10,i+11]
  // Test partial overlap.
  loc1 = heap_location_collector.GetArrayHeapLocation(vstore_i_add6);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i_add8);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+6,i+7] and array[i,i+1,i+2,i+3]
  // Test different vector lengths.
  loc1 = heap_location_collector.GetArrayHeapLocation(vstore_i_add6_vlen2);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+6,i+7] and array[i+8,i+9,i+10,i+11]
  loc1 = heap_location_collector.GetArrayHeapLocation(vstore_i_add6_vlen2);
  loc2 = heap_location_collector.GetArrayHeapLocation(vstore_i_add8);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));
}

TEST_F(LoadStoreAnalysisTest, ArrayIndexCalculationOverflowTest) {
  CreateGraph();
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  graph_->BuildDominatorTree();

  HInstruction* array = new (GetAllocator()) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(0), 0, DataType::Type::kReference);
  HInstruction* index = new (GetAllocator()) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kInt32);

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c_0x80000000 = graph_->GetIntConstant(0x80000000);
  HInstruction* c_0x10 = graph_->GetIntConstant(0x10);
  HInstruction* c_0xFFFFFFF0 = graph_->GetIntConstant(0xFFFFFFF0);
  HInstruction* c_0x7FFFFFFF = graph_->GetIntConstant(0x7FFFFFFF);
  HInstruction* c_0x80000001 = graph_->GetIntConstant(0x80000001);

  // `index+0x80000000` and `index-0x80000000` array indices MAY alias.
  HInstruction* add_0x80000000 = new (GetAllocator()) HAdd(
      DataType::Type::kInt32, index, c_0x80000000);
  HInstruction* sub_0x80000000 = new (GetAllocator()) HSub(
      DataType::Type::kInt32, index, c_0x80000000);
  HInstruction* arr_set_1 = new (GetAllocator()) HArraySet(
      array, add_0x80000000, c0, DataType::Type::kInt32, 0);
  HInstruction* arr_set_2 = new (GetAllocator()) HArraySet(
      array, sub_0x80000000, c0, DataType::Type::kInt32, 0);

  // `index+0x10` and `index-0xFFFFFFF0` array indices MAY alias.
  HInstruction* add_0x10 = new (GetAllocator()) HAdd(DataType::Type::kInt32, index, c_0x10);
  HInstruction* sub_0xFFFFFFF0 = new (GetAllocator()) HSub(
      DataType::Type::kInt32, index, c_0xFFFFFFF0);
  HInstruction* arr_set_3 = new (GetAllocator()) HArraySet(
      array, add_0x10, c0, DataType::Type::kInt32, 0);
  HInstruction* arr_set_4 = new (GetAllocator()) HArraySet(
      array, sub_0xFFFFFFF0, c0, DataType::Type::kInt32, 0);

  // `index+0x7FFFFFFF` and `index-0x80000001` array indices MAY alias.
  HInstruction* add_0x7FFFFFFF = new (GetAllocator()) HAdd(
      DataType::Type::kInt32, index, c_0x7FFFFFFF);
  HInstruction* sub_0x80000001 = new (GetAllocator()) HSub(
      DataType::Type::kInt32, index, c_0x80000001);
  HInstruction* arr_set_5 = new (GetAllocator()) HArraySet(
      array, add_0x7FFFFFFF, c0, DataType::Type::kInt32, 0);
  HInstruction* arr_set_6 = new (GetAllocator()) HArraySet(
      array, sub_0x80000001, c0, DataType::Type::kInt32, 0);

  // `index+0` and `index-0` array indices MAY alias.
  HInstruction* add_0 = new (GetAllocator()) HAdd(DataType::Type::kInt32, index, c0);
  HInstruction* sub_0 = new (GetAllocator()) HSub(DataType::Type::kInt32, index, c0);
  HInstruction* arr_set_7 = new (GetAllocator()) HArraySet(
      array, add_0, c0, DataType::Type::kInt32, 0);
  HInstruction* arr_set_8 = new (GetAllocator()) HArraySet(
      array, sub_0, c0, DataType::Type::kInt32, 0);

  entry->AddInstruction(array);
  entry->AddInstruction(index);
  entry->AddInstruction(add_0x80000000);
  entry->AddInstruction(sub_0x80000000);
  entry->AddInstruction(add_0x10);
  entry->AddInstruction(sub_0xFFFFFFF0);
  entry->AddInstruction(add_0x7FFFFFFF);
  entry->AddInstruction(sub_0x80000001);
  entry->AddInstruction(add_0);
  entry->AddInstruction(sub_0);
  entry->AddInstruction(arr_set_1);
  entry->AddInstruction(arr_set_2);
  entry->AddInstruction(arr_set_3);
  entry->AddInstruction(arr_set_4);
  entry->AddInstruction(arr_set_5);
  entry->AddInstruction(arr_set_6);
  entry->AddInstruction(arr_set_7);
  entry->AddInstruction(arr_set_8);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kBasic);
  lsa.Run();
  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();

  // LSA/HeapLocationCollector should see those ArrayGet instructions.
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 8U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's aliasing matrix after load store analysis.
  size_t loc1 = HeapLocationCollector::kHeapLocationNotFound;
  size_t loc2 = HeapLocationCollector::kHeapLocationNotFound;

  // Test alias: array[i+0x80000000] and array[i-0x80000000]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_1);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_2);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0x10] and array[i-0xFFFFFFF0]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_3);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_4);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0x7FFFFFFF] and array[i-0x80000001]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_5);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_6);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0] and array[i-0]
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_7);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_8);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Should not alias:
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_2);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_6);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Should not alias:
  loc1 = heap_location_collector.GetArrayHeapLocation(arr_set_7);
  loc2 = heap_location_collector.GetArrayHeapLocation(arr_set_2);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));
}

TEST_F(LoadStoreAnalysisTest, TestHuntOriginalRef) {
  CreateGraph();
  HBasicBlock* entry = new (GetAllocator()) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);

  // Different ways where orignal array reference are transformed & passed to ArrayGet.
  // ParameterValue --> ArrayGet
  // ParameterValue --> BoundType --> ArrayGet
  // ParameterValue --> BoundType --> NullCheck --> ArrayGet
  // ParameterValue --> BoundType --> NullCheck --> IntermediateAddress --> ArrayGet
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* array = new (GetAllocator()) HParameterValue(graph_->GetDexFile(),
                                                             dex::TypeIndex(0),
                                                             0,
                                                             DataType::Type::kReference);
  HInstruction* array_get1 = new (GetAllocator()) HArrayGet(array,
                                                            c1,
                                                            DataType::Type::kInt32,
                                                            0);

  HInstruction* bound_type = new (GetAllocator()) HBoundType(array);
  HInstruction* array_get2 = new (GetAllocator()) HArrayGet(bound_type,
                                                            c1,
                                                            DataType::Type::kInt32,
                                                            0);

  HInstruction* null_check = new (GetAllocator()) HNullCheck(bound_type, 0);
  HInstruction* array_get3 = new (GetAllocator()) HArrayGet(null_check,
                                                            c1,
                                                            DataType::Type::kInt32,
                                                            0);

  HInstruction* inter_addr = new (GetAllocator()) HIntermediateAddress(null_check, c1, 0);
  HInstruction* array_get4 = new (GetAllocator()) HArrayGet(inter_addr,
                                                            c1,
                                                            DataType::Type::kInt32,
                                                            0);
  entry->AddInstruction(array);
  entry->AddInstruction(array_get1);
  entry->AddInstruction(bound_type);
  entry->AddInstruction(array_get2);
  entry->AddInstruction(null_check);
  entry->AddInstruction(array_get3);
  entry->AddInstruction(inter_addr);
  entry->AddInstruction(array_get4);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  HeapLocationCollector heap_location_collector(graph_, &allocator, LoadStoreAnalysisType::kFull);
  heap_location_collector.VisitBasicBlock(entry);

  // Test that the HeapLocationCollector should be able to tell
  // that there is only ONE array location, no matter how many
  // times the original reference has been transformed by BoundType,
  // NullCheck, IntermediateAddress, etc.
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 1U);
  size_t loc1 = heap_location_collector.GetArrayHeapLocation(array_get1);
  size_t loc2 = heap_location_collector.GetArrayHeapLocation(array_get2);
  size_t loc3 = heap_location_collector.GetArrayHeapLocation(array_get3);
  size_t loc4 = heap_location_collector.GetArrayHeapLocation(array_get4);
  ASSERT_TRUE(loc1 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_EQ(loc1, loc2);
  ASSERT_EQ(loc1, loc3);
  ASSERT_EQ(loc1, loc4);
}

void LoadStoreAnalysisTest::CheckReachability(const AdjacencyListGraph& adj,
                                              const std::vector<AdjacencyListGraph::Edge>& reach) {
  uint32_t cnt = 0;
  for (HBasicBlock* blk : graph_->GetBlocks()) {
    if (adj.HasBlock(blk)) {
      for (HBasicBlock* other : graph_->GetBlocks()) {
        if (other == nullptr) {
          continue;
        }
        if (adj.HasBlock(other)) {
          bool contains_edge =
              std::find(reach.begin(),
                        reach.end(),
                        AdjacencyListGraph::Edge { adj.GetName(blk), adj.GetName(other) }) !=
              reach.end();
          if (graph_->PathBetween(blk, other)) {
            cnt++;
            EXPECT_TRUE(contains_edge) << "Unexpected edge found between " << adj.GetName(blk)
                                       << " and " << adj.GetName(other);
          } else {
            EXPECT_FALSE(contains_edge) << "Expected edge not found between " << adj.GetName(blk)
                                        << " and " << adj.GetName(other);
          }
        } else if (graph_->PathBetween(blk, other)) {
          ADD_FAILURE() << "block " << adj.GetName(blk)
                        << " has path to non-adjacency-graph block id: " << other->GetBlockId();
        }
      }
    } else {
      for (HBasicBlock* other : graph_->GetBlocks()) {
        if (other == nullptr) {
          continue;
        }
        EXPECT_FALSE(graph_->PathBetween(blk, other))
            << "Reachable blocks outside of adjacency-list";
      }
    }
  }
  EXPECT_EQ(cnt, reach.size());
}

TEST_F(LoadStoreAnalysisTest, ReachabilityTest1) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      { { "entry", "left" }, { "entry", "right" }, { "left", "exit" }, { "right", "exit" } }));
  CheckReachability(blks,
                    {
                        { "entry", "left" },
                        { "entry", "right" },
                        { "entry", "exit" },
                        { "right", "exit" },
                        { "left", "exit" },
                    });
}

TEST_F(LoadStoreAnalysisTest, ReachabilityTest2) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      { { "entry", "loop-header" }, { "loop-header", "loop" }, { "loop", "loop-header" } }));
  CheckReachability(blks,
                    {
                        { "entry", "loop-header" },
                        { "entry", "loop" },
                        { "loop-header", "loop-header" },
                        { "loop-header", "loop" },
                        { "loop", "loop-header" },
                        { "loop", "loop" },
                    });
}

TEST_F(LoadStoreAnalysisTest, ReachabilityTest3) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 { { "entry", "loop-header" },
                                                   { "loop-header", "loop" },
                                                   { "loop", "loop-header" },
                                                   { "entry", "right" },
                                                   { "right", "exit" } }));
  CheckReachability(blks,
                    {
                        { "entry", "loop-header" },
                        { "entry", "loop" },
                        { "entry", "right" },
                        { "entry", "exit" },
                        { "loop-header", "loop-header" },
                        { "loop-header", "loop" },
                        { "loop", "loop-header" },
                        { "loop", "loop" },
                        { "right", "exit" },
                    });
}

static bool AreExclusionsIndependent(HGraph* graph, const ExecutionSubgraph* esg) {
  auto excluded = esg->GetExcludedCohorts();
  if (excluded.size() < 2) {
    return true;
  }
  for (auto first = excluded.begin(); first != excluded.end(); ++first) {
    for (auto second = excluded.begin(); second != excluded.end(); ++second) {
      if (first == second) {
        continue;
      }
      for (const HBasicBlock* entry : first->EntryBlocks()) {
        for (const HBasicBlock* exit : second->ExitBlocks()) {
          if (graph->PathBetween(exit, entry)) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

// // ENTRY
// obj = new Obj();
// if (parameter_value) {
//   // LEFT
//   call_func(obj);
// } else {
//   // RIGHT
//   obj.field = 1;
// }
// // EXIT
// obj.field;
TEST_F(LoadStoreAnalysisTest, PartialEscape) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      { { "entry", "left" }, { "entry", "right" }, { "left", "exit" }, { "right", "exit" } }));
  HBasicBlock* entry = blks.Get("entry");
  HBasicBlock* left = blks.Get("left");
  HBasicBlock* right = blks.Get("right");
  HBasicBlock* exit = blks.Get("exit");

  HInstruction* bool_value = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kBool);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* cls = new (GetAllocator()) HLoadClass(graph_->GetCurrentMethod(),
                                                      dex::TypeIndex(10),
                                                      graph_->GetDexFile(),
                                                      ScopedNullHandle<mirror::Class>(),
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
  HInstruction* if_inst = new (GetAllocator()) HIf(bool_value);
  entry->AddInstruction(bool_value);
  entry->AddInstruction(cls);
  entry->AddInstruction(new_inst);
  entry->AddInstruction(if_inst);

  HInstruction* call_left = new (GetAllocator())
      HInvokeStaticOrDirect(GetAllocator(),
                            1,
                            DataType::Type::kVoid,
                            0,
                            { nullptr, 0 },
                            nullptr,
                            {},
                            InvokeType::kStatic,
                            { nullptr, 0 },
                            HInvokeStaticOrDirect::ClinitCheckRequirement::kNone);
  HInstruction* goto_left = new (GetAllocator()) HGoto();
  call_left->AsInvoke()->SetRawInputAt(0, new_inst);
  left->AddInstruction(call_left);
  left->AddInstruction(goto_left);

  HInstruction* write_right = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                     c0,
                                                                     nullptr,
                                                                     DataType::Type::kInt32,
                                                                     MemberOffset(32),
                                                                     false,
                                                                     0,
                                                                     0,
                                                                     graph_->GetDexFile(),
                                                                     0);
  HInstruction* goto_right = new (GetAllocator()) HGoto();
  right->AddInstruction(write_right);
  right->AddInstruction(goto_right);

  HInstruction* read_final = new (GetAllocator()) HInstanceFieldGet(new_inst,
                                                                    nullptr,
                                                                    DataType::Type::kInt32,
                                                                    MemberOffset(32),
                                                                    false,
                                                                    0,
                                                                    0,
                                                                    graph_->GetDexFile(),
                                                                    0);
  exit->AddInstruction(read_final);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kFull);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  const ExecutionSubgraph* esg = info->GetNoEscapeSubgraph();

  ASSERT_TRUE(esg->IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  ASSERT_TRUE(AreExclusionsIndependent(graph_, esg));
  std::unordered_set<const HBasicBlock*> contents(esg->ReachableBlocks().begin(),
                                                  esg->ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 3u);
  ASSERT_TRUE(contents.find(blks.Get("left")) == contents.end());

  ASSERT_TRUE(contents.find(blks.Get("right")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
}

// // ENTRY
// obj = new Obj();
// if (parameter_value) {
//   // LEFT
//   call_func(obj);
// } else {
//   // RIGHT
//   obj.field = 1;
// }
// // EXIT
// obj.field2;
TEST_F(LoadStoreAnalysisTest, PartialEscape2) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      { { "entry", "left" }, { "entry", "right" }, { "left", "exit" }, { "right", "exit" } }));
  HBasicBlock* entry = blks.Get("entry");
  HBasicBlock* left = blks.Get("left");
  HBasicBlock* right = blks.Get("right");
  HBasicBlock* exit = blks.Get("exit");

  HInstruction* bool_value = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kBool);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* cls = new (GetAllocator()) HLoadClass(graph_->GetCurrentMethod(),
                                                      dex::TypeIndex(10),
                                                      graph_->GetDexFile(),
                                                      ScopedNullHandle<mirror::Class>(),
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
  HInstruction* if_inst = new (GetAllocator()) HIf(bool_value);
  entry->AddInstruction(bool_value);
  entry->AddInstruction(cls);
  entry->AddInstruction(new_inst);
  entry->AddInstruction(if_inst);

  HInstruction* call_left = new (GetAllocator())
      HInvokeStaticOrDirect(GetAllocator(),
                            1,
                            DataType::Type::kVoid,
                            0,
                            { nullptr, 0 },
                            nullptr,
                            {},
                            InvokeType::kStatic,
                            { nullptr, 0 },
                            HInvokeStaticOrDirect::ClinitCheckRequirement::kNone);
  HInstruction* goto_left = new (GetAllocator()) HGoto();
  call_left->AsInvoke()->SetRawInputAt(0, new_inst);
  left->AddInstruction(call_left);
  left->AddInstruction(goto_left);

  HInstruction* write_right = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                     c0,
                                                                     nullptr,
                                                                     DataType::Type::kInt32,
                                                                     MemberOffset(32),
                                                                     false,
                                                                     0,
                                                                     0,
                                                                     graph_->GetDexFile(),
                                                                     0);
  HInstruction* goto_right = new (GetAllocator()) HGoto();
  right->AddInstruction(write_right);
  right->AddInstruction(goto_right);

  HInstruction* read_final = new (GetAllocator()) HInstanceFieldGet(new_inst,
                                                                    nullptr,
                                                                    DataType::Type::kInt32,
                                                                    MemberOffset(16),
                                                                    false,
                                                                    0,
                                                                    0,
                                                                    graph_->GetDexFile(),
                                                                    0);
  exit->AddInstruction(read_final);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kFull);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  const ExecutionSubgraph* esg = info->GetNoEscapeSubgraph();

  ASSERT_TRUE(esg->IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  ASSERT_TRUE(AreExclusionsIndependent(graph_, esg));
  std::unordered_set<const HBasicBlock*> contents(esg->ReachableBlocks().begin(),
                                                  esg->ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 3u);
  ASSERT_TRUE(contents.find(blks.Get("left")) == contents.end());

  ASSERT_TRUE(contents.find(blks.Get("right")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
}

// // ENTRY
// obj = new Obj();
// obj.field = 10;
// if (parameter_value) {
//   // LEFT
//   call_func(obj);
// } else {
//   // RIGHT
//   obj.field = 20;
// }
// // EXIT
// obj.field;
TEST_F(LoadStoreAnalysisTest, PartialEscape3) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      { { "entry", "left" }, { "entry", "right" }, { "left", "exit" }, { "right", "exit" } }));
  HBasicBlock* entry = blks.Get("entry");
  HBasicBlock* left = blks.Get("left");
  HBasicBlock* right = blks.Get("right");
  HBasicBlock* exit = blks.Get("exit");

  HInstruction* bool_value = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kBool);
  HInstruction* c10 = graph_->GetIntConstant(10);
  HInstruction* c20 = graph_->GetIntConstant(20);
  HInstruction* cls = new (GetAllocator()) HLoadClass(graph_->GetCurrentMethod(),
                                                      dex::TypeIndex(10),
                                                      graph_->GetDexFile(),
                                                      ScopedNullHandle<mirror::Class>(),
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

  HInstruction* write_entry = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                     c10,
                                                                     nullptr,
                                                                     DataType::Type::kInt32,
                                                                     MemberOffset(32),
                                                                     false,
                                                                     0,
                                                                     0,
                                                                     graph_->GetDexFile(),
                                                                     0);
  HInstruction* if_inst = new (GetAllocator()) HIf(bool_value);
  entry->AddInstruction(bool_value);
  entry->AddInstruction(cls);
  entry->AddInstruction(new_inst);
  entry->AddInstruction(write_entry);
  entry->AddInstruction(if_inst);

  HInstruction* call_left = new (GetAllocator())
      HInvokeStaticOrDirect(GetAllocator(),
                            1,
                            DataType::Type::kVoid,
                            0,
                            { nullptr, 0 },
                            nullptr,
                            {},
                            InvokeType::kStatic,
                            { nullptr, 0 },
                            HInvokeStaticOrDirect::ClinitCheckRequirement::kNone);
  HInstruction* goto_left = new (GetAllocator()) HGoto();
  call_left->AsInvoke()->SetRawInputAt(0, new_inst);
  left->AddInstruction(call_left);
  left->AddInstruction(goto_left);

  HInstruction* write_right = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                     c20,
                                                                     nullptr,
                                                                     DataType::Type::kInt32,
                                                                     MemberOffset(32),
                                                                     false,
                                                                     0,
                                                                     0,
                                                                     graph_->GetDexFile(),
                                                                     0);
  HInstruction* goto_right = new (GetAllocator()) HGoto();
  right->AddInstruction(write_right);
  right->AddInstruction(goto_right);

  HInstruction* read_final = new (GetAllocator()) HInstanceFieldGet(new_inst,
                                                                    nullptr,
                                                                    DataType::Type::kInt32,
                                                                    MemberOffset(32),
                                                                    false,
                                                                    0,
                                                                    0,
                                                                    graph_->GetDexFile(),
                                                                    0);
  exit->AddInstruction(read_final);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kFull);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  const ExecutionSubgraph* esg = info->GetNoEscapeSubgraph();

  ASSERT_TRUE(esg->IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  ASSERT_TRUE(AreExclusionsIndependent(graph_, esg));
  std::unordered_set<const HBasicBlock*> contents(esg->ReachableBlocks().begin(),
                                                  esg->ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 3u);
  ASSERT_TRUE(contents.find(blks.Get("left")) == contents.end());

  ASSERT_TRUE(contents.find(blks.Get("right")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
}

// For simplicity Partial LSE considers check-casts to escape. It means we don't
// need to worry about inserting throws.
// // ENTRY
// obj = new Obj();
// obj.field = 10;
// if (parameter_value) {
//   // LEFT
//   (Foo)obj;
// } else {
//   // RIGHT
//   obj.field = 20;
// }
// // EXIT
// obj.field;
TEST_F(LoadStoreAnalysisTest, PartialEscape4) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      { { "entry", "left" }, { "entry", "right" }, { "left", "exit" }, { "right", "exit" } }));
  HBasicBlock* entry = blks.Get("entry");
  HBasicBlock* left = blks.Get("left");
  HBasicBlock* right = blks.Get("right");
  HBasicBlock* exit = blks.Get("exit");

  HInstruction* bool_value = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kBool);
  HInstruction* c10 = graph_->GetIntConstant(10);
  HInstruction* c20 = graph_->GetIntConstant(20);
  HInstruction* cls = MakeClassLoad();
  HInstruction* new_inst = MakeNewInstance(cls);

  HInstruction* write_entry = MakeIFieldSet(new_inst, c10, MemberOffset(32));
  HInstruction* if_inst = new (GetAllocator()) HIf(bool_value);
  entry->AddInstruction(bool_value);
  entry->AddInstruction(cls);
  entry->AddInstruction(new_inst);
  entry->AddInstruction(write_entry);
  entry->AddInstruction(if_inst);

  ScopedNullHandle<mirror::Class> null_klass_;
  HInstruction* cls2 = MakeClassLoad();
  HInstruction* check_cast = new (GetAllocator()) HCheckCast(
      new_inst, cls2, TypeCheckKind::kExactCheck, null_klass_, 0, GetAllocator(), nullptr, nullptr);
  HInstruction* goto_left = new (GetAllocator()) HGoto();
  left->AddInstruction(cls2);
  left->AddInstruction(check_cast);
  left->AddInstruction(goto_left);

  HInstruction* write_right = MakeIFieldSet(new_inst, c20, MemberOffset(32));
  HInstruction* goto_right = new (GetAllocator()) HGoto();
  right->AddInstruction(write_right);
  right->AddInstruction(goto_right);

  HInstruction* read_final = MakeIFieldGet(new_inst, DataType::Type::kInt32, MemberOffset(32));
  exit->AddInstruction(read_final);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kFull);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  const ExecutionSubgraph* esg = info->GetNoEscapeSubgraph();

  ASSERT_TRUE(esg->IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  ASSERT_TRUE(AreExclusionsIndependent(graph_, esg));
  std::unordered_set<const HBasicBlock*> contents(esg->ReachableBlocks().begin(),
                                                  esg->ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 3u);
  ASSERT_TRUE(contents.find(blks.Get("left")) == contents.end());

  ASSERT_TRUE(contents.find(blks.Get("right")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
}

// For simplicity Partial LSE considers instance-ofs with bitvectors to escape.
// // ENTRY
// obj = new Obj();
// obj.field = 10;
// if (parameter_value) {
//   // LEFT
//   obj instanceof /*bitvector*/ Foo;
// } else {
//   // RIGHT
//   obj.field = 20;
// }
// // EXIT
// obj.field;
TEST_F(LoadStoreAnalysisTest, PartialEscape5) {
  VariableSizedHandleScope vshs(Thread::Current());
  CreateGraph(&vshs);
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      { { "entry", "left" }, { "entry", "right" }, { "left", "exit" }, { "right", "exit" } }));
  HBasicBlock* entry = blks.Get("entry");
  HBasicBlock* left = blks.Get("left");
  HBasicBlock* right = blks.Get("right");
  HBasicBlock* exit = blks.Get("exit");

  HInstruction* bool_value = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kBool);
  HInstruction* c10 = graph_->GetIntConstant(10);
  HInstruction* c20 = graph_->GetIntConstant(20);
  HIntConstant* bs1 = graph_->GetIntConstant(0xffff);
  HIntConstant* bs2 = graph_->GetIntConstant(0x00ff);
  HInstruction* cls = MakeClassLoad();
  HInstruction* null_const = graph_->GetNullConstant();
  HInstruction* new_inst = MakeNewInstance(cls);

  HInstruction* write_entry = MakeIFieldSet(new_inst, c10, MemberOffset(32));
  HInstruction* if_inst = new (GetAllocator()) HIf(bool_value);
  entry->AddInstruction(bool_value);
  entry->AddInstruction(cls);
  entry->AddInstruction(new_inst);
  entry->AddInstruction(write_entry);
  entry->AddInstruction(if_inst);

  ScopedNullHandle<mirror::Class> null_klass_;
  HInstruction* instanceof = new (GetAllocator()) HInstanceOf(new_inst,
                                                              null_const,
                                                              TypeCheckKind::kBitstringCheck,
                                                              null_klass_,
                                                              0,
                                                              GetAllocator(),
                                                              bs1,
                                                              bs2);
  HInstruction* goto_left = new (GetAllocator()) HGoto();
  left->AddInstruction(instanceof);
  left->AddInstruction(goto_left);

  HInstruction* write_right = MakeIFieldSet(new_inst, c20, MemberOffset(32));
  HInstruction* goto_right = new (GetAllocator()) HGoto();
  right->AddInstruction(write_right);
  right->AddInstruction(goto_right);

  HInstruction* read_final = MakeIFieldGet(new_inst, DataType::Type::kInt32, MemberOffset(32));
  exit->AddInstruction(read_final);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kFull);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  const ExecutionSubgraph* esg = info->GetNoEscapeSubgraph();

  ASSERT_TRUE(esg->IsValid());
  ASSERT_TRUE(IsValidSubgraph(esg));
  ASSERT_TRUE(AreExclusionsIndependent(graph_, esg));
  std::unordered_set<const HBasicBlock*> contents(esg->ReachableBlocks().begin(),
                                                  esg->ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 3u);
  ASSERT_TRUE(contents.find(blks.Get("left")) == contents.end());

  ASSERT_TRUE(contents.find(blks.Get("right")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("entry")) != contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) != contents.end());
}

// before we had predicated-set we needed to be able to remove the store as
// well. This test makes sure that still works.
// // ENTRY
// obj = new Obj();
// if (parameter_value) {
//   // LEFT
//   call_func(obj);
// } else {
//   // RIGHT
//   obj.f1 = 0;
// }
// // EXIT
// // call_func prevents the elimination of this store.
// obj.f2 = 0;
TEST_F(LoadStoreAnalysisTest, TotalEscapeAdjacentNoPredicated) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      {{"entry", "left"}, {"entry", "right"}, {"left", "exit"}, {"right", "exit"}}));
  HBasicBlock* entry = blks.Get("entry");
  HBasicBlock* left = blks.Get("left");
  HBasicBlock* right = blks.Get("right");
  HBasicBlock* exit = blks.Get("exit");

  HInstruction* bool_value = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kBool);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* cls = new (GetAllocator()) HLoadClass(graph_->GetCurrentMethod(),
                                                      dex::TypeIndex(10),
                                                      graph_->GetDexFile(),
                                                      ScopedNullHandle<mirror::Class>(),
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
  HInstruction* if_inst = new (GetAllocator()) HIf(bool_value);
  entry->AddInstruction(bool_value);
  entry->AddInstruction(cls);
  entry->AddInstruction(new_inst);
  entry->AddInstruction(if_inst);

  HInstruction* call_left = new (GetAllocator())
      HInvokeStaticOrDirect(GetAllocator(),
                            1,
                            DataType::Type::kVoid,
                            0,
                            {nullptr, 0},
                            nullptr,
                            {},
                            InvokeType::kStatic,
                            {nullptr, 0},
                            HInvokeStaticOrDirect::ClinitCheckRequirement::kNone);
  HInstruction* goto_left = new (GetAllocator()) HGoto();
  call_left->AsInvoke()->SetRawInputAt(0, new_inst);
  left->AddInstruction(call_left);
  left->AddInstruction(goto_left);

  HInstruction* write_right = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                     c0,
                                                                     nullptr,
                                                                     DataType::Type::kInt32,
                                                                     MemberOffset(32),
                                                                     false,
                                                                     0,
                                                                     0,
                                                                     graph_->GetDexFile(),
                                                                     0);
  HInstruction* goto_right = new (GetAllocator()) HGoto();
  right->AddInstruction(write_right);
  right->AddInstruction(goto_right);

  HInstruction* write_final = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                     c0,
                                                                     nullptr,
                                                                     DataType::Type::kInt32,
                                                                     MemberOffset(16),
                                                                     false,
                                                                     0,
                                                                     0,
                                                                     graph_->GetDexFile(),
                                                                     0);
  exit->AddInstruction(write_final);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();
  LoadStoreAnalysis lsa(
      graph_, nullptr, &allocator, LoadStoreAnalysisType::kNoPredicatedInstructions);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  const ExecutionSubgraph* esg = info->GetNoEscapeSubgraph();

  EXPECT_FALSE(esg->IsValid()) << esg->GetExcludedCohorts();
  EXPECT_FALSE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg->ReachableBlocks().begin(),
                                                  esg->ReachableBlocks().end());

  EXPECT_EQ(contents.size(), 0u);
  EXPECT_TRUE(contents.find(blks.Get("left")) == contents.end());
  EXPECT_TRUE(contents.find(blks.Get("right")) == contents.end());
  EXPECT_TRUE(contents.find(blks.Get("entry")) == contents.end());
  EXPECT_TRUE(contents.find(blks.Get("exit")) == contents.end());
}

// With predicated-set we can (partially) remove the store as well.
// // ENTRY
// obj = new Obj();
// if (parameter_value) {
//   // LEFT
//   call_func(obj);
// } else {
//   // RIGHT
//   obj.f1 = 0;
// }
// // EXIT
// // call_func prevents the elimination of this store.
// obj.f2 = 0;
TEST_F(LoadStoreAnalysisTest, TotalEscapeAdjacent) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      { { "entry", "left" }, { "entry", "right" }, { "left", "exit" }, { "right", "exit" } }));
  HBasicBlock* entry = blks.Get("entry");
  HBasicBlock* left = blks.Get("left");
  HBasicBlock* right = blks.Get("right");
  HBasicBlock* exit = blks.Get("exit");

  HInstruction* bool_value = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kBool);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* cls = new (GetAllocator()) HLoadClass(graph_->GetCurrentMethod(),
                                                      dex::TypeIndex(10),
                                                      graph_->GetDexFile(),
                                                      ScopedNullHandle<mirror::Class>(),
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
  HInstruction* if_inst = new (GetAllocator()) HIf(bool_value);
  entry->AddInstruction(bool_value);
  entry->AddInstruction(cls);
  entry->AddInstruction(new_inst);
  entry->AddInstruction(if_inst);

  HInstruction* call_left = new (GetAllocator())
      HInvokeStaticOrDirect(GetAllocator(),
                            1,
                            DataType::Type::kVoid,
                            0,
                            { nullptr, 0 },
                            nullptr,
                            {},
                            InvokeType::kStatic,
                            { nullptr, 0 },
                            HInvokeStaticOrDirect::ClinitCheckRequirement::kNone);
  HInstruction* goto_left = new (GetAllocator()) HGoto();
  call_left->AsInvoke()->SetRawInputAt(0, new_inst);
  left->AddInstruction(call_left);
  left->AddInstruction(goto_left);

  HInstruction* write_right = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                     c0,
                                                                     nullptr,
                                                                     DataType::Type::kInt32,
                                                                     MemberOffset(32),
                                                                     false,
                                                                     0,
                                                                     0,
                                                                     graph_->GetDexFile(),
                                                                     0);
  HInstruction* goto_right = new (GetAllocator()) HGoto();
  right->AddInstruction(write_right);
  right->AddInstruction(goto_right);

  HInstruction* write_final = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                     c0,
                                                                     nullptr,
                                                                     DataType::Type::kInt32,
                                                                     MemberOffset(16),
                                                                     false,
                                                                     0,
                                                                     0,
                                                                     graph_->GetDexFile(),
                                                                     0);
  exit->AddInstruction(write_final);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kFull);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  const ExecutionSubgraph* esg = info->GetNoEscapeSubgraph();

  EXPECT_TRUE(esg->IsValid()) << esg->GetExcludedCohorts();
  EXPECT_TRUE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg->ReachableBlocks().begin(),
                                                  esg->ReachableBlocks().end());

  EXPECT_EQ(contents.size(), 3u);
  EXPECT_TRUE(contents.find(blks.Get("left")) == contents.end());
  EXPECT_FALSE(contents.find(blks.Get("right")) == contents.end());
  EXPECT_FALSE(contents.find(blks.Get("entry")) == contents.end());
  EXPECT_FALSE(contents.find(blks.Get("exit")) == contents.end());
}

// // ENTRY
// obj = new Obj();
// if (parameter_value) {
//   // LEFT
//   call_func(obj);
// } else {
//   // RIGHT
//   obj.f0 = 0;
//   call_func2(obj);
// }
// // EXIT
// obj.f0;
TEST_F(LoadStoreAnalysisTest, TotalEscape) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList(
      "entry",
      "exit",
      { { "entry", "left" }, { "entry", "right" }, { "left", "exit" }, { "right", "exit" } }));
  HBasicBlock* entry = blks.Get("entry");
  HBasicBlock* left = blks.Get("left");
  HBasicBlock* right = blks.Get("right");
  HBasicBlock* exit = blks.Get("exit");

  HInstruction* bool_value = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kBool);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* cls = new (GetAllocator()) HLoadClass(graph_->GetCurrentMethod(),
                                                      dex::TypeIndex(10),
                                                      graph_->GetDexFile(),
                                                      ScopedNullHandle<mirror::Class>(),
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
  HInstruction* if_inst = new (GetAllocator()) HIf(bool_value);
  entry->AddInstruction(bool_value);
  entry->AddInstruction(cls);
  entry->AddInstruction(new_inst);
  entry->AddInstruction(if_inst);

  HInstruction* call_left = new (GetAllocator())
      HInvokeStaticOrDirect(GetAllocator(),
                            1,
                            DataType::Type::kVoid,
                            0,
                            { nullptr, 0 },
                            nullptr,
                            {},
                            InvokeType::kStatic,
                            { nullptr, 0 },
                            HInvokeStaticOrDirect::ClinitCheckRequirement::kNone);
  HInstruction* goto_left = new (GetAllocator()) HGoto();
  call_left->AsInvoke()->SetRawInputAt(0, new_inst);
  left->AddInstruction(call_left);
  left->AddInstruction(goto_left);

  HInstruction* call_right = new (GetAllocator())
      HInvokeStaticOrDirect(GetAllocator(),
                            1,
                            DataType::Type::kVoid,
                            0,
                            { nullptr, 0 },
                            nullptr,
                            {},
                            InvokeType::kStatic,
                            { nullptr, 0 },
                            HInvokeStaticOrDirect::ClinitCheckRequirement::kNone);
  HInstruction* write_right = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                     c0,
                                                                     nullptr,
                                                                     DataType::Type::kInt32,
                                                                     MemberOffset(32),
                                                                     false,
                                                                     0,
                                                                     0,
                                                                     graph_->GetDexFile(),
                                                                     0);
  HInstruction* goto_right = new (GetAllocator()) HGoto();
  call_right->AsInvoke()->SetRawInputAt(0, new_inst);
  right->AddInstruction(write_right);
  right->AddInstruction(call_right);
  right->AddInstruction(goto_right);

  HInstruction* read_final = new (GetAllocator()) HInstanceFieldGet(new_inst,
                                                                    nullptr,
                                                                    DataType::Type::kInt32,
                                                                    MemberOffset(32),
                                                                    false,
                                                                    0,
                                                                    0,
                                                                    graph_->GetDexFile(),
                                                                    0);
  exit->AddInstruction(read_final);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kFull);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  const ExecutionSubgraph* esg = info->GetNoEscapeSubgraph();

  ASSERT_FALSE(esg->IsValid());
  ASSERT_FALSE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg->ReachableBlocks().begin(),
                                                  esg->ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 0u);
  ASSERT_TRUE(contents.find(blks.Get("left")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("right")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("entry")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) == contents.end());
}

// // ENTRY
// obj = new Obj();
// obj.foo = 0;
// // EXIT
// return obj;
TEST_F(LoadStoreAnalysisTest, TotalEscape2) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry", "exit", { { "entry", "exit" } }));
  HBasicBlock* entry = blks.Get("entry");
  HBasicBlock* exit = blks.Get("exit");

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* cls = new (GetAllocator()) HLoadClass(graph_->GetCurrentMethod(),
                                                      dex::TypeIndex(10),
                                                      graph_->GetDexFile(),
                                                      ScopedNullHandle<mirror::Class>(),
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

  HInstruction* write_start = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                     c0,
                                                                     nullptr,
                                                                     DataType::Type::kInt32,
                                                                     MemberOffset(32),
                                                                     false,
                                                                     0,
                                                                     0,
                                                                     graph_->GetDexFile(),
                                                                     0);
  HInstruction* goto_inst = new (GetAllocator()) HGoto();
  entry->AddInstruction(cls);
  entry->AddInstruction(new_inst);
  entry->AddInstruction(write_start);
  entry->AddInstruction(goto_inst);

  HInstruction* return_final = new (GetAllocator()) HReturn(new_inst);
  exit->AddInstruction(return_final);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kFull);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  const ExecutionSubgraph* esg = info->GetNoEscapeSubgraph();

  ASSERT_FALSE(esg->IsValid());
  ASSERT_FALSE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg->ReachableBlocks().begin(),
                                                  esg->ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 0u);
  ASSERT_TRUE(contents.find(blks.Get("entry")) == contents.end());
  ASSERT_TRUE(contents.find(blks.Get("exit")) == contents.end());
}

// // ENTRY
// obj = new Obj();
// if (parameter_value) {
//   // HIGH_LEFT
//   call_func(obj);
// } else {
//   // HIGH_RIGHT
//   obj.f0 = 1;
// }
// // MID
// obj.f0 *= 2;
// if (parameter_value2) {
//   // LOW_LEFT
//   call_func(obj);
// } else {
//   // LOW_RIGHT
//   obj.f0 = 1;
// }
// // EXIT
// obj.f0
TEST_F(LoadStoreAnalysisTest, DoubleDiamondEscape) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 { { "entry", "high_left" },
                                                   { "entry", "high_right" },
                                                   { "low_left", "exit" },
                                                   { "low_right", "exit" },
                                                   { "high_right", "mid" },
                                                   { "high_left", "mid" },
                                                   { "mid", "low_left" },
                                                   { "mid", "low_right" } }));
  HBasicBlock* entry = blks.Get("entry");
  HBasicBlock* high_left = blks.Get("high_left");
  HBasicBlock* high_right = blks.Get("high_right");
  HBasicBlock* mid = blks.Get("mid");
  HBasicBlock* low_left = blks.Get("low_left");
  HBasicBlock* low_right = blks.Get("low_right");
  HBasicBlock* exit = blks.Get("exit");

  HInstruction* bool_value1 = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kBool);
  HInstruction* bool_value2 = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 2, DataType::Type::kBool);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* cls = new (GetAllocator()) HLoadClass(graph_->GetCurrentMethod(),
                                                      dex::TypeIndex(10),
                                                      graph_->GetDexFile(),
                                                      ScopedNullHandle<mirror::Class>(),
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
  HInstruction* if_inst = new (GetAllocator()) HIf(bool_value1);
  entry->AddInstruction(bool_value1);
  entry->AddInstruction(bool_value2);
  entry->AddInstruction(cls);
  entry->AddInstruction(new_inst);
  entry->AddInstruction(if_inst);

  HInstruction* call_left = new (GetAllocator())
      HInvokeStaticOrDirect(GetAllocator(),
                            1,
                            DataType::Type::kVoid,
                            0,
                            { nullptr, 0 },
                            nullptr,
                            {},
                            InvokeType::kStatic,
                            { nullptr, 0 },
                            HInvokeStaticOrDirect::ClinitCheckRequirement::kNone);
  HInstruction* goto_left = new (GetAllocator()) HGoto();
  call_left->AsInvoke()->SetRawInputAt(0, new_inst);
  high_left->AddInstruction(call_left);
  high_left->AddInstruction(goto_left);

  HInstruction* write_right = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                     c0,
                                                                     nullptr,
                                                                     DataType::Type::kInt32,
                                                                     MemberOffset(32),
                                                                     false,
                                                                     0,
                                                                     0,
                                                                     graph_->GetDexFile(),
                                                                     0);
  HInstruction* goto_right = new (GetAllocator()) HGoto();
  high_right->AddInstruction(write_right);
  high_right->AddInstruction(goto_right);

  HInstruction* read_mid = new (GetAllocator()) HInstanceFieldGet(new_inst,
                                                                  nullptr,
                                                                  DataType::Type::kInt32,
                                                                  MemberOffset(32),
                                                                  false,
                                                                  0,
                                                                  0,
                                                                  graph_->GetDexFile(),
                                                                  0);
  HInstruction* mul_mid = new (GetAllocator()) HMul(DataType::Type::kInt32, read_mid, c2);
  HInstruction* write_mid = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                   mul_mid,
                                                                   nullptr,
                                                                   DataType::Type::kInt32,
                                                                   MemberOffset(32),
                                                                   false,
                                                                   0,
                                                                   0,
                                                                   graph_->GetDexFile(),
                                                                   0);
  HInstruction* if_mid = new (GetAllocator()) HIf(bool_value2);
  mid->AddInstruction(read_mid);
  mid->AddInstruction(mul_mid);
  mid->AddInstruction(write_mid);
  mid->AddInstruction(if_mid);

  HInstruction* call_low_left = new (GetAllocator())
      HInvokeStaticOrDirect(GetAllocator(),
                            1,
                            DataType::Type::kVoid,
                            0,
                            { nullptr, 0 },
                            nullptr,
                            {},
                            InvokeType::kStatic,
                            { nullptr, 0 },
                            HInvokeStaticOrDirect::ClinitCheckRequirement::kNone);
  HInstruction* goto_low_left = new (GetAllocator()) HGoto();
  call_low_left->AsInvoke()->SetRawInputAt(0, new_inst);
  low_left->AddInstruction(call_low_left);
  low_left->AddInstruction(goto_low_left);

  HInstruction* write_low_right = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                                         c0,
                                                                         nullptr,
                                                                         DataType::Type::kInt32,
                                                                         MemberOffset(32),
                                                                         false,
                                                                         0,
                                                                         0,
                                                                         graph_->GetDexFile(),
                                                                         0);
  HInstruction* goto_low_right = new (GetAllocator()) HGoto();
  low_right->AddInstruction(write_low_right);
  low_right->AddInstruction(goto_low_right);

  HInstruction* read_final = new (GetAllocator()) HInstanceFieldGet(new_inst,
                                                                    nullptr,
                                                                    DataType::Type::kInt32,
                                                                    MemberOffset(32),
                                                                    false,
                                                                    0,
                                                                    0,
                                                                    graph_->GetDexFile(),
                                                                    0);
  exit->AddInstruction(read_final);

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kFull);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  const ExecutionSubgraph* esg = info->GetNoEscapeSubgraph();

  ASSERT_FALSE(esg->IsValid());
  ASSERT_FALSE(IsValidSubgraph(esg));
  std::unordered_set<const HBasicBlock*> contents(esg->ReachableBlocks().begin(),
                                                  esg->ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 0u);
}

// // ENTRY
// Obj new_inst = new Obj();
// new_inst.foo = 12;
// Obj obj;
// Obj out;
// if (param1) {
//   // LEFT_START
//   if (param2) {
//     // LEFT_LEFT
//     obj = new_inst;
//   } else {
//     // LEFT_RIGHT
//     obj = obj_param;
//   }
//   // LEFT_MERGE
//   // technically the phi is enough to cause an escape but might as well be
//   // thorough.
//   // obj = phi[new_inst, param]
//   escape(obj);
//   out = obj;
// } else {
//   // RIGHT
//   out = obj_param;
// }
// // EXIT
// // Can't do anything with this since we don't have good tracking for the heap-locations
// // out = phi[param, phi[new_inst, param]]
// return out.foo
TEST_F(LoadStoreAnalysisTest, PartialPhiPropagation1) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 {{"entry", "left"},
                                                  {"entry", "right"},
                                                  {"left", "left_left"},
                                                  {"left", "left_right"},
                                                  {"left_left", "left_merge"},
                                                  {"left_right", "left_merge"},
                                                  {"left_merge", "breturn"},
                                                  {"right", "breturn"},
                                                  {"breturn", "exit"}}));
#define GET_BLOCK(name) HBasicBlock* name = blks.Get(#name)
  GET_BLOCK(entry);
  GET_BLOCK(exit);
  GET_BLOCK(breturn);
  GET_BLOCK(left);
  GET_BLOCK(right);
  GET_BLOCK(left_left);
  GET_BLOCK(left_right);
  GET_BLOCK(left_merge);
#undef GET_BLOCK
  EnsurePredecessorOrder(breturn, {left_merge, right});
  EnsurePredecessorOrder(left_merge, {left_left, left_right});
  HInstruction* param1 = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 1, DataType::Type::kBool);
  HInstruction* param2 = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(1), 2, DataType::Type::kBool);
  HInstruction* obj_param = new (GetAllocator())
      HParameterValue(graph_->GetDexFile(), dex::TypeIndex(10), 3, DataType::Type::kReference);
  HInstruction* c12 = graph_->GetIntConstant(12);
  HInstruction* cls = new (GetAllocator()) HLoadClass(graph_->GetCurrentMethod(),
                                                      dex::TypeIndex(10),
                                                      graph_->GetDexFile(),
                                                      ScopedNullHandle<mirror::Class>(),
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
  HInstruction* store = new (GetAllocator()) HInstanceFieldSet(new_inst,
                                                               c12,
                                                               nullptr,
                                                               DataType::Type::kInt32,
                                                               MemberOffset(32),
                                                               false,
                                                               0,
                                                               0,
                                                               graph_->GetDexFile(),
                                                               0);
  HInstruction* if_param1 = new (GetAllocator()) HIf(param1);
  entry->AddInstruction(param1);
  entry->AddInstruction(param2);
  entry->AddInstruction(obj_param);
  entry->AddInstruction(cls);
  entry->AddInstruction(new_inst);
  entry->AddInstruction(store);
  entry->AddInstruction(if_param1);
  ArenaVector<HInstruction*> current_locals({}, GetAllocator()->Adapter(kArenaAllocInstruction));
  ManuallyBuildEnvFor(cls, &current_locals);
  new_inst->CopyEnvironmentFrom(cls->GetEnvironment());

  HInstruction* if_left = new (GetAllocator()) HIf(param2);
  left->AddInstruction(if_left);

  HInstruction* goto_left_left = new (GetAllocator()) HGoto();
  left_left->AddInstruction(goto_left_left);

  HInstruction* goto_left_right = new (GetAllocator()) HGoto();
  left_right->AddInstruction(goto_left_right);

  HPhi* left_phi =
      new (GetAllocator()) HPhi(GetAllocator(), kNoRegNumber, 2, DataType::Type::kReference);
  HInstruction* call_left = new (GetAllocator())
      HInvokeStaticOrDirect(GetAllocator(),
                            1,
                            DataType::Type::kVoid,
                            0,
                            {nullptr, 0},
                            nullptr,
                            {},
                            InvokeType::kStatic,
                            {nullptr, 0},
                            HInvokeStaticOrDirect::ClinitCheckRequirement::kNone);
  HInstruction* goto_left_merge = new (GetAllocator()) HGoto();
  left_phi->SetRawInputAt(0, obj_param);
  left_phi->SetRawInputAt(1, new_inst);
  call_left->AsInvoke()->SetRawInputAt(0, left_phi);
  left_merge->AddPhi(left_phi);
  left_merge->AddInstruction(call_left);
  left_merge->AddInstruction(goto_left_merge);
  left_phi->SetCanBeNull(true);
  call_left->CopyEnvironmentFrom(cls->GetEnvironment());

  HInstruction* goto_right = new (GetAllocator()) HGoto();
  right->AddInstruction(goto_right);

  HPhi* return_phi =
      new (GetAllocator()) HPhi(GetAllocator(), kNoRegNumber, 2, DataType::Type::kReference);
  HInstruction* read_exit = new (GetAllocator()) HInstanceFieldGet(return_phi,
                                                                   nullptr,
                                                                   DataType::Type::kReference,
                                                                   MemberOffset(32),
                                                                   false,
                                                                   0,
                                                                   0,
                                                                   graph_->GetDexFile(),
                                                                   0);
  HInstruction* return_exit = new (GetAllocator()) HReturn(read_exit);
  return_phi->SetRawInputAt(0, left_phi);
  return_phi->SetRawInputAt(1, obj_param);
  breturn->AddPhi(return_phi);
  breturn->AddInstruction(read_exit);
  breturn->AddInstruction(return_exit);

  HInstruction* exit_instruction = new (GetAllocator()) HExit();
  exit->AddInstruction(exit_instruction);

  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  LoadStoreAnalysis lsa(graph_, nullptr, &allocator, LoadStoreAnalysisType::kFull);
  lsa.Run();

  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();
  ReferenceInfo* info = heap_location_collector.FindReferenceInfoOf(new_inst);
  const ExecutionSubgraph* esg = info->GetNoEscapeSubgraph();
  std::unordered_set<const HBasicBlock*> contents(esg->ReachableBlocks().begin(),
                                                  esg->ReachableBlocks().end());

  ASSERT_EQ(contents.size(), 0u);
  ASSERT_FALSE(esg->IsValid());
}
}  // namespace art
