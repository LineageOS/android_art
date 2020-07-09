/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef ART_COMPILER_UTILS_JNI_MACRO_ASSEMBLER_TEST_H_
#define ART_COMPILER_UTILS_JNI_MACRO_ASSEMBLER_TEST_H_

#include "jni_macro_assembler.h"

#include "assembler_test_base.h"
#include "base/malloc_arena_pool.h"
#include "common_runtime_test.h"  // For ScratchFile

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace art {

template<typename Ass>
class JNIMacroAssemblerTest : public AssemblerTestBase {
 public:
  Ass* GetAssembler() {
    return assembler_.get();
  }

  typedef std::string (*TestFn)(JNIMacroAssemblerTest* assembler_test, Ass* assembler);

  void DriverFn(TestFn f, const std::string& test_name) {
    DriverWrapper(f(this, assembler_.get()), test_name);
  }

  // This driver assumes the assembler has already been called.
  void DriverStr(const std::string& assembly_string, const std::string& test_name) {
    DriverWrapper(assembly_string, test_name);
  }

 protected:
  JNIMacroAssemblerTest() {}

  void SetUp() override {
    AssemblerTestBase::SetUp();
    allocator_.reset(new ArenaAllocator(&pool_));
    assembler_.reset(CreateAssembler(allocator_.get()));
    SetUpHelpers();
  }

  void TearDown() override {
    AssemblerTestBase::TearDown();
    assembler_.reset();
    allocator_.reset();
  }

  // Override this to set up any architecture-specific things, e.g., CPU revision.
  virtual Ass* CreateAssembler(ArenaAllocator* allocator) {
    return new (allocator) Ass(allocator);
  }

  // Override this to set up any architecture-specific things, e.g., register vectors.
  virtual void SetUpHelpers() {}

 private:
  // Override this to pad the code with NOPs to a certain size if needed.
  virtual void Pad(std::vector<uint8_t>& data ATTRIBUTE_UNUSED) {
  }

  void DriverWrapper(const std::string& assembly_text, const std::string& test_name) {
    assembler_->FinalizeCode();
    size_t cs = assembler_->CodeSize();
    std::unique_ptr<std::vector<uint8_t>> data(new std::vector<uint8_t>(cs));
    MemoryRegion code(&(*data)[0], data->size());
    assembler_->FinalizeInstructions(code);
    Pad(*data);
    Driver(*data, assembly_text, test_name);
  }

  MallocArenaPool pool_;
  std::unique_ptr<ArenaAllocator> allocator_;
  std::unique_ptr<Ass> assembler_;

  DISALLOW_COPY_AND_ASSIGN(JNIMacroAssemblerTest);
};

}  // namespace art

#endif  // ART_COMPILER_UTILS_JNI_MACRO_ASSEMBLER_TEST_H_
