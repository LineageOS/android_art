/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "common_runtime_test.h"

#include <cstdio>

#include "gtest/gtest.h"

namespace art {

// Run the tests only on host.
#ifndef ART_TARGET_ANDROID

class PrebuiltToolsTest : public CommonRuntimeTest {
 public:
  static void CheckToolsExist(InstructionSet isa) {
    const char* tools[] = { "clang", "llvm-addr2line", "llvm-dwarfdump", "llvm-objdump" };
    for (const char* tool : tools) {
      std::string path = GetAndroidTool(tool, isa);
      ASSERT_TRUE(OS::FileExists(path.c_str())) << path;
    }
  }
};

TEST_F(PrebuiltToolsTest, CheckHostTools) {
  CheckToolsExist(InstructionSet::kX86);
  CheckToolsExist(InstructionSet::kX86_64);
}

TEST_F(PrebuiltToolsTest, CheckTargetTools) {
  CheckToolsExist(InstructionSet::kThumb2);
  CheckToolsExist(InstructionSet::kArm64);
}

#endif  // ART_TARGET_ANDROID

}  // namespace art
