/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "instruction_simplifier.h"

#include <initializer_list>
#include <tuple>

#include "gtest/gtest.h"

#include "class_root-inl.h"
#include "nodes.h"
#include "optimizing/data_type.h"
#include "optimizing_unit_test.h"

namespace art {

namespace mirror {
class ClassExt;
class Throwable;
}  // namespace mirror

template<typename SuperClass>
class InstructionSimplifierTestBase : public SuperClass, public OptimizingUnitTestHelper {
 public:
  void SetUp() override {
    SuperClass::SetUp();
    gLogVerbosity.compiler = true;
  }

  void TearDown() override {
    SuperClass::TearDown();
    gLogVerbosity.compiler = false;
  }
};

class InstructionSimplifierTest : public InstructionSimplifierTestBase<CommonCompilerTest> {};

// Various configs we can use for testing. Currently used in PartialComparison tests.
enum class InstanceOfKind {
  kSelf,
  kUnrelatedLoaded,
  kUnrelatedUnloaded,
  kSupertype,
};

std::ostream& operator<<(std::ostream& os, const InstanceOfKind& comp) {
  switch (comp) {
    case InstanceOfKind::kSupertype:
      return os << "kSupertype";
    case InstanceOfKind::kSelf:
      return os << "kSelf";
    case InstanceOfKind::kUnrelatedLoaded:
      return os << "kUnrelatedLoaded";
    case InstanceOfKind::kUnrelatedUnloaded:
      return os << "kUnrelatedUnloaded";
  }
}

class InstanceOfInstructionSimplifierTestGroup
    : public InstructionSimplifierTestBase<CommonCompilerTestWithParam<InstanceOfKind>> {
 public:
  bool GetConstantResult() const {
    switch (GetParam()) {
      case InstanceOfKind::kSupertype:
      case InstanceOfKind::kSelf:
        return true;
      case InstanceOfKind::kUnrelatedLoaded:
      case InstanceOfKind::kUnrelatedUnloaded:
        return false;
    }
  }

  std::pair<HLoadClass*, HLoadClass*> GetLoadClasses(VariableSizedHandleScope* vshs) {
    InstanceOfKind kind = GetParam();
    ScopedObjectAccess soa(Thread::Current());
    // New inst always needs to have a valid rti since we dcheck that.
    HLoadClass* new_inst = MakeClassLoad(
        /* ti= */ std::nullopt, vshs->NewHandle<mirror::Class>(GetClassRoot<mirror::ClassExt>()));
    new_inst->SetValidLoadedClassRTI();
    if (kind == InstanceOfKind::kSelf) {
      return {new_inst, new_inst};
    }
    if (kind == InstanceOfKind::kUnrelatedUnloaded) {
      HLoadClass* target_class = MakeClassLoad();
      EXPECT_FALSE(target_class->GetLoadedClassRTI().IsValid());
      return {new_inst, target_class};
    }
    // Force both classes to be a real classes.
    // For simplicity we use class-roots as the types. The new-inst will always
    // be a ClassExt, unrelated-loaded will always be Throwable and super will
    // always be Object
    HLoadClass* target_class = MakeClassLoad(
        /* ti= */ std::nullopt,
        vshs->NewHandle<mirror::Class>(kind == InstanceOfKind::kSupertype ?
                                           GetClassRoot<mirror::Object>() :
                                           GetClassRoot<mirror::Throwable>()));
    target_class->SetValidLoadedClassRTI();
    EXPECT_TRUE(target_class->GetLoadedClassRTI().IsValid());
    return {new_inst, target_class};
  }
};

// // ENTRY
// switch (param) {
// case 1:
//   obj1 = param2; break;
// case 2:
//   obj1 = param3; break;
// default:
//   obj2 = new Obj();
// }
// val_phi = PHI[3,4,10]
// target_phi = PHI[param2, param3, obj2]
// return PredFieldGet[val_phi, target_phi] => PredFieldGet[val_phi, target_phi]
TEST_F(InstructionSimplifierTest, SimplifyPredicatedFieldGetNoMerge) {
  VariableSizedHandleScope vshs(Thread::Current());
  CreateGraph(&vshs);
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 {{"entry", "case1"},
                                                  {"entry", "case2"},
                                                  {"entry", "case3"},
                                                  {"case1", "breturn"},
                                                  {"case2", "breturn"},
                                                  {"case3", "breturn"},
                                                  {"breturn", "exit"}}));
#define GET_BLOCK(name) HBasicBlock* name = blks.Get(#name)
  GET_BLOCK(entry);
  GET_BLOCK(exit);
  GET_BLOCK(case1);
  GET_BLOCK(case2);
  GET_BLOCK(case3);
  GET_BLOCK(breturn);
#undef GET_BLOCK

  HInstruction* bool_value = MakeParam(DataType::Type::kInt32);
  HInstruction* obj1_param = MakeParam(DataType::Type::kReference);
  HInstruction* obj2_param = MakeParam(DataType::Type::kReference);
  HInstruction* c3 = graph_->GetIntConstant(3);
  HInstruction* c4 = graph_->GetIntConstant(4);
  HInstruction* c10 = graph_->GetIntConstant(10);

  HInstruction* cls = MakeClassLoad();
  HInstruction* switch_inst = new (GetAllocator()) HPackedSwitch(0, 2, bool_value);
  entry->AddInstruction(cls);
  entry->AddInstruction(switch_inst);
  ManuallyBuildEnvFor(cls, {});

  HInstruction* goto_c1 = new (GetAllocator()) HGoto();
  case1->AddInstruction(goto_c1);

  HInstruction* goto_c2 = new (GetAllocator()) HGoto();
  case2->AddInstruction(goto_c2);

  HInstruction* obj3 = MakeNewInstance(cls);
  HInstruction* goto_c3 = new (GetAllocator()) HGoto();
  case3->AddInstruction(obj3);
  case3->AddInstruction(goto_c3);

  HPhi* val_phi = MakePhi({c3, c4, c10});
  HPhi* obj_phi = MakePhi({obj1_param, obj2_param, obj3});
  HPredicatedInstanceFieldGet* read_end =
      new (GetAllocator()) HPredicatedInstanceFieldGet(obj_phi,
                                                       nullptr,
                                                       val_phi,
                                                       val_phi->GetType(),
                                                       MemberOffset(10),
                                                       false,
                                                       42,
                                                       0,
                                                       graph_->GetDexFile(),
                                                       0);
  HInstruction* return_exit = new (GetAllocator()) HReturn(read_end);
  breturn->AddPhi(val_phi);
  breturn->AddPhi(obj_phi);
  breturn->AddInstruction(read_end);
  breturn->AddInstruction(return_exit);

  SetupExit(exit);

  LOG(INFO) << "Pre simplification " << blks;
  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();
  InstructionSimplifier simp(graph_, /*codegen=*/nullptr);
  simp.Run();

  LOG(INFO) << "Post simplify " << blks;

  EXPECT_INS_RETAINED(read_end);

  EXPECT_INS_EQ(read_end->GetTarget(), obj_phi);
  EXPECT_INS_EQ(read_end->GetDefaultValue(), val_phi);
}

// // ENTRY
// switch (param) {
// case 1:
//   obj1 = param2; break;
// case 2:
//   obj1 = param3; break;
// default:
//   obj2 = new Obj();
// }
// val_phi = PHI[3,3,10]
// target_phi = PHI[param2, param3, obj2]
// return PredFieldGet[val_phi, target_phi] => PredFieldGet[3, target_phi]
TEST_F(InstructionSimplifierTest, SimplifyPredicatedFieldGetMerge) {
  VariableSizedHandleScope vshs(Thread::Current());
  CreateGraph(&vshs);
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 {{"entry", "case1"},
                                                  {"entry", "case2"},
                                                  {"entry", "case3"},
                                                  {"case1", "breturn"},
                                                  {"case2", "breturn"},
                                                  {"case3", "breturn"},
                                                  {"breturn", "exit"}}));
#define GET_BLOCK(name) HBasicBlock* name = blks.Get(#name)
  GET_BLOCK(entry);
  GET_BLOCK(exit);
  GET_BLOCK(case1);
  GET_BLOCK(case2);
  GET_BLOCK(case3);
  GET_BLOCK(breturn);
#undef GET_BLOCK

  HInstruction* bool_value = MakeParam(DataType::Type::kInt32);
  HInstruction* obj1_param = MakeParam(DataType::Type::kReference);
  HInstruction* obj2_param = MakeParam(DataType::Type::kReference);
  HInstruction* c3 = graph_->GetIntConstant(3);
  HInstruction* c10 = graph_->GetIntConstant(10);

  HInstruction* cls = MakeClassLoad();
  HInstruction* switch_inst = new (GetAllocator()) HPackedSwitch(0, 2, bool_value);
  entry->AddInstruction(cls);
  entry->AddInstruction(switch_inst);
  ManuallyBuildEnvFor(cls, {});

  HInstruction* goto_c1 = new (GetAllocator()) HGoto();
  case1->AddInstruction(goto_c1);

  HInstruction* goto_c2 = new (GetAllocator()) HGoto();
  case2->AddInstruction(goto_c2);

  HInstruction* obj3 = MakeNewInstance(cls);
  HInstruction* goto_c3 = new (GetAllocator()) HGoto();
  case3->AddInstruction(obj3);
  case3->AddInstruction(goto_c3);

  HPhi* val_phi = MakePhi({c3, c3, c10});
  HPhi* obj_phi = MakePhi({obj1_param, obj2_param, obj3});
  HPredicatedInstanceFieldGet* read_end =
      new (GetAllocator()) HPredicatedInstanceFieldGet(obj_phi,
                                                       nullptr,
                                                       val_phi,
                                                       val_phi->GetType(),
                                                       MemberOffset(10),
                                                       false,
                                                       42,
                                                       0,
                                                       graph_->GetDexFile(),
                                                       0);
  HInstruction* return_exit = new (GetAllocator()) HReturn(read_end);
  breturn->AddPhi(val_phi);
  breturn->AddPhi(obj_phi);
  breturn->AddInstruction(read_end);
  breturn->AddInstruction(return_exit);

  SetupExit(exit);

  LOG(INFO) << "Pre simplification " << blks;
  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();
  InstructionSimplifier simp(graph_, /*codegen=*/nullptr);
  simp.Run();

  LOG(INFO) << "Post simplify " << blks;

  EXPECT_FALSE(obj3->CanBeNull());
  EXPECT_INS_RETAINED(read_end);

  EXPECT_INS_EQ(read_end->GetTarget(), obj_phi);
  EXPECT_INS_EQ(read_end->GetDefaultValue(), c3);
}

// // ENTRY
// if (param) {
//   obj1 = new Obj();
// } else {
//   obj2 = new Obj();
// }
// val_phi = PHI[3,10]
// target_phi = PHI[obj1, obj2]
// return PredFieldGet[val_phi, target_phi] => FieldGet[target_phi]
TEST_F(InstructionSimplifierTest, SimplifyPredicatedFieldGetNoNull) {
  VariableSizedHandleScope vshs(Thread::Current());
  CreateGraph(&vshs);
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 {{"entry", "left"},
                                                  {"entry", "right"},
                                                  {"left", "breturn"},
                                                  {"right", "breturn"},
                                                  {"breturn", "exit"}}));
#define GET_BLOCK(name) HBasicBlock* name = blks.Get(#name)
  GET_BLOCK(entry);
  GET_BLOCK(exit);
  GET_BLOCK(left);
  GET_BLOCK(right);
  GET_BLOCK(breturn);
#undef GET_BLOCK

  HInstruction* bool_value = MakeParam(DataType::Type::kBool);
  HInstruction* c3 = graph_->GetIntConstant(3);
  HInstruction* c10 = graph_->GetIntConstant(10);

  HInstruction* cls = MakeClassLoad();
  HInstruction* if_inst = new (GetAllocator()) HIf(bool_value);
  entry->AddInstruction(cls);
  entry->AddInstruction(if_inst);
  ManuallyBuildEnvFor(cls, {});

  HInstruction* obj1 = MakeNewInstance(cls);
  HInstruction* goto_left = new (GetAllocator()) HGoto();
  left->AddInstruction(obj1);
  left->AddInstruction(goto_left);

  HInstruction* obj2 = MakeNewInstance(cls);
  HInstruction* goto_right = new (GetAllocator()) HGoto();
  right->AddInstruction(obj2);
  right->AddInstruction(goto_right);

  HPhi* val_phi = MakePhi({c3, c10});
  HPhi* obj_phi = MakePhi({obj1, obj2});
  obj_phi->SetCanBeNull(false);
  HInstruction* read_end = new (GetAllocator()) HPredicatedInstanceFieldGet(obj_phi,
                                                                            nullptr,
                                                                            val_phi,
                                                                            val_phi->GetType(),
                                                                            MemberOffset(10),
                                                                            false,
                                                                            42,
                                                                            0,
                                                                            graph_->GetDexFile(),
                                                                            0);
  HInstruction* return_exit = new (GetAllocator()) HReturn(read_end);
  breturn->AddPhi(val_phi);
  breturn->AddPhi(obj_phi);
  breturn->AddInstruction(read_end);
  breturn->AddInstruction(return_exit);

  SetupExit(exit);

  LOG(INFO) << "Pre simplification " << blks;
  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();
  InstructionSimplifier simp(graph_, /*codegen=*/nullptr);
  simp.Run();

  LOG(INFO) << "Post simplify " << blks;

  EXPECT_FALSE(obj1->CanBeNull());
  EXPECT_FALSE(obj2->CanBeNull());
  EXPECT_INS_REMOVED(read_end);

  HInstanceFieldGet* ifget = FindSingleInstruction<HInstanceFieldGet>(graph_, breturn);
  ASSERT_NE(ifget, nullptr);
  EXPECT_INS_EQ(ifget->InputAt(0), obj_phi);
}

// // ENTRY
// obj = new Obj();
// // Make sure this graph isn't broken
// if (obj instanceof <other>) {
//   // LEFT
// } else {
//   // RIGHT
// }
// EXIT
// return obj.field
TEST_P(InstanceOfInstructionSimplifierTestGroup, ExactClassInstanceOfOther) {
  VariableSizedHandleScope vshs(Thread::Current());
  InitGraph(/*handles=*/&vshs);

  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 {{"entry", "left"},
                                                  {"entry", "right"},
                                                  {"left", "breturn"},
                                                  {"right", "breturn"},
                                                  {"breturn", "exit"}}));
#define GET_BLOCK(name) HBasicBlock* name = blks.Get(#name)
  GET_BLOCK(entry);
  GET_BLOCK(exit);
  GET_BLOCK(breturn);
  GET_BLOCK(left);
  GET_BLOCK(right);
#undef GET_BLOCK
  EnsurePredecessorOrder(breturn, {left, right});
  HInstruction* test_res = graph_->GetIntConstant(GetConstantResult() ? 1 : 0);

  auto [new_inst_klass, target_klass] = GetLoadClasses(&vshs);
  HInstruction* new_inst = MakeNewInstance(new_inst_klass);
  new_inst->SetReferenceTypeInfo(
      ReferenceTypeInfo::Create(new_inst_klass->GetClass(), /*is_exact=*/true));
  HInstanceOf* instance_of = new (GetAllocator()) HInstanceOf(new_inst,
                                                              target_klass,
                                                              TypeCheckKind::kClassHierarchyCheck,
                                                              target_klass->GetClass(),
                                                              0u,
                                                              GetAllocator(),
                                                              nullptr,
                                                              nullptr);
  if (target_klass->GetLoadedClassRTI().IsValid()) {
    instance_of->SetValidTargetClassRTI();
  }
  HInstruction* if_inst = new (GetAllocator()) HIf(instance_of);
  entry->AddInstruction(new_inst_klass);
  if (new_inst_klass != target_klass) {
    entry->AddInstruction(target_klass);
  }
  entry->AddInstruction(new_inst);
  entry->AddInstruction(instance_of);
  entry->AddInstruction(if_inst);
  ManuallyBuildEnvFor(new_inst_klass, {});
  if (new_inst_klass != target_klass) {
    target_klass->CopyEnvironmentFrom(new_inst_klass->GetEnvironment());
  }
  new_inst->CopyEnvironmentFrom(new_inst_klass->GetEnvironment());

  HInstruction* goto_left = new (GetAllocator()) HGoto();
  left->AddInstruction(goto_left);

  HInstruction* goto_right = new (GetAllocator()) HGoto();
  right->AddInstruction(goto_right);

  HInstruction* read_bottom = MakeIFieldGet(new_inst, DataType::Type::kInt32, MemberOffset(32));
  HInstruction* return_exit = new (GetAllocator()) HReturn(read_bottom);
  breturn->AddInstruction(read_bottom);
  breturn->AddInstruction(return_exit);

  SetupExit(exit);

  // PerformLSE expects this to be empty.
  graph_->ClearDominanceInformation();

  LOG(INFO) << "Pre simplification " << blks;
  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();
  InstructionSimplifier simp(graph_, /*codegen=*/nullptr);
  simp.Run();

  LOG(INFO) << "Post simplify " << blks;

  if (!GetConstantResult() || GetParam() == InstanceOfKind::kSelf) {
    EXPECT_INS_RETAINED(target_klass);
  } else {
    EXPECT_INS_REMOVED(target_klass);
  }
  EXPECT_INS_REMOVED(instance_of);
  EXPECT_INS_EQ(if_inst->InputAt(0), test_res);
}

// // ENTRY
// obj = new Obj();
// (<other>)obj;
// // Make sure this graph isn't broken
// EXIT
// return obj
TEST_P(InstanceOfInstructionSimplifierTestGroup, ExactClassCheckCastOther) {
  VariableSizedHandleScope vshs(Thread::Current());
  InitGraph(/*handles=*/&vshs);

  AdjacencyListGraph blks(SetupFromAdjacencyList("entry", "exit", {{"entry", "exit"}}));
#define GET_BLOCK(name) HBasicBlock* name = blks.Get(#name)
  GET_BLOCK(entry);
  GET_BLOCK(exit);
#undef GET_BLOCK

  auto [new_inst_klass, target_klass] = GetLoadClasses(&vshs);
  HInstruction* new_inst = MakeNewInstance(new_inst_klass);
  new_inst->SetReferenceTypeInfo(
      ReferenceTypeInfo::Create(new_inst_klass->GetClass(), /*is_exact=*/true));
  HCheckCast* check_cast = new (GetAllocator()) HCheckCast(new_inst,
                                                           target_klass,
                                                           TypeCheckKind::kClassHierarchyCheck,
                                                           target_klass->GetClass(),
                                                           0u,
                                                           GetAllocator(),
                                                           nullptr,
                                                           nullptr);
  if (target_klass->GetLoadedClassRTI().IsValid()) {
    check_cast->SetValidTargetClassRTI();
  }
  HInstruction* entry_return = new (GetAllocator()) HReturn(new_inst);
  entry->AddInstruction(new_inst_klass);
  if (new_inst_klass != target_klass) {
    entry->AddInstruction(target_klass);
  }
  entry->AddInstruction(new_inst);
  entry->AddInstruction(check_cast);
  entry->AddInstruction(entry_return);
  ManuallyBuildEnvFor(new_inst_klass, {});
  if (new_inst_klass != target_klass) {
    target_klass->CopyEnvironmentFrom(new_inst_klass->GetEnvironment());
  }
  new_inst->CopyEnvironmentFrom(new_inst_klass->GetEnvironment());

  SetupExit(exit);

  // PerformLSE expects this to be empty.
  graph_->ClearDominanceInformation();

  LOG(INFO) << "Pre simplification " << blks;
  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();
  InstructionSimplifier simp(graph_, /*codegen=*/nullptr);
  simp.Run();

  LOG(INFO) << "Post simplify " << blks;

  if (!GetConstantResult() || GetParam() == InstanceOfKind::kSelf) {
    EXPECT_INS_RETAINED(target_klass);
  } else {
    EXPECT_INS_REMOVED(target_klass);
  }
  if (GetConstantResult()) {
    EXPECT_INS_REMOVED(check_cast);
  } else {
    EXPECT_INS_RETAINED(check_cast);
  }
}

INSTANTIATE_TEST_SUITE_P(InstructionSimplifierTest,
                         InstanceOfInstructionSimplifierTestGroup,
                         testing::Values(InstanceOfKind::kSelf,
                                         InstanceOfKind::kUnrelatedLoaded,
                                         InstanceOfKind::kUnrelatedUnloaded,
                                         InstanceOfKind::kSupertype));

}  // namespace art
