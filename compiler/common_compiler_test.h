/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_COMPILER_COMMON_COMPILER_TEST_H_
#define ART_COMPILER_COMMON_COMPILER_TEST_H_

#include <list>
#include <vector>

#include <jni.h>

#include "arch/instruction_set.h"
#include "arch/instruction_set_features.h"
#include "common_runtime_test.h"
#include "compiler.h"
#include "oat_file.h"

namespace art {
namespace mirror {
class ClassLoader;
}  // namespace mirror

class CompiledMethod;
class CompilerOptions;
class CumulativeLogger;
class DexFile;
class TimingLogger;
class VerificationResults;

template<class T> class Handle;

class CommonCompilerTestImpl {
 public:
  static std::unique_ptr<CompilerOptions> CreateCompilerOptions(InstructionSet instruction_set,
                                                                const std::string& variant);

  CommonCompilerTestImpl();
  virtual ~CommonCompilerTestImpl();

  // Create an executable copy of the code with given metadata.
  const void* MakeExecutable(ArrayRef<const uint8_t> code,
                             ArrayRef<const uint8_t> vmap_table,
                             InstructionSet instruction_set);

  void MakeExecutable(ArtMethod* method, const CompiledMethod* compiled_method)
      REQUIRES_SHARED(Locks::mutator_lock_);

 protected:
  void SetUp();

  void SetUpRuntimeOptionsImpl();

  Compiler::Kind GetCompilerKind() const;
  void SetCompilerKind(Compiler::Kind compiler_kind);

  virtual CompilerFilter::Filter GetCompilerFilter() const {
    return CompilerFilter::kDefaultCompilerFilter;
  }

  void TearDown();

  void CompileMethod(ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_);

  void CompileDirectMethod(Handle<mirror::ClassLoader> class_loader, const char* class_name,
                           const char* method_name, const char* signature)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void CompileVirtualMethod(Handle<mirror::ClassLoader> class_loader, const char* class_name,
                            const char* method_name, const char* signature)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void ApplyInstructionSet();
  void OverrideInstructionSetFeatures(InstructionSet instruction_set, const std::string& variant);

  void ClearBootImageOption();

  Compiler::Kind compiler_kind_ = Compiler::kOptimizing;

  InstructionSet instruction_set_ =
      (kRuntimeISA == InstructionSet::kArm) ? InstructionSet::kThumb2 : kRuntimeISA;
  // Take the default set of instruction features from the build.
  std::unique_ptr<const InstructionSetFeatures> instruction_set_features_
      = InstructionSetFeatures::FromCppDefines();

  std::unique_ptr<CompilerOptions> compiler_options_;
  std::unique_ptr<VerificationResults> verification_results_;

 protected:
  virtual ClassLinker* GetClassLinker() = 0;
  virtual Runtime* GetRuntime() = 0;

 private:
  class CodeAndMetadata;
  std::vector<CodeAndMetadata> code_and_metadata_;
};

template <typename RuntimeBase>
class CommonCompilerTestBase : public CommonCompilerTestImpl, public RuntimeBase {
 public:
  void SetUp() override {
    RuntimeBase::SetUp();
    CommonCompilerTestImpl::SetUp();
  }
  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    RuntimeBase::SetUpRuntimeOptions(options);
    CommonCompilerTestImpl::SetUpRuntimeOptionsImpl();
  }
  void TearDown() override {
    CommonCompilerTestImpl::TearDown();
    RuntimeBase::TearDown();
  }

 protected:
  ClassLinker* GetClassLinker() override {
    return RuntimeBase::class_linker_;
  }
  Runtime* GetRuntime() override {
    return RuntimeBase::runtime_.get();
  }
};

class CommonCompilerTest : public CommonCompilerTestBase<CommonRuntimeTest> {};

template <typename Param>
class CommonCompilerTestWithParam
    : public CommonCompilerTestBase<CommonRuntimeTestWithParam<Param>> {};

}  // namespace art

#endif  // ART_COMPILER_COMMON_COMPILER_TEST_H_
