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

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include <fstream>
#include <map>
#include <regex>

#include "gtest/gtest.h"

#include "jni/quick/calling_convention.h"
#include "utils/arm/jni_macro_assembler_arm_vixl.h"
#include "utils/assembler_test_base.h"

#include "base/hex_dump.h"
#include "base/malloc_arena_pool.h"
#include "common_runtime_test.h"

namespace art {
namespace arm {

// Include results file (generated manually)
#include "assembler_thumb_test_expected.cc.inc"

class ArmVIXLAssemblerTest : public AssemblerTestBase {
 public:
  ArmVIXLAssemblerTest() : pool(), allocator(&pool), assembler(&allocator) { }

 protected:
  InstructionSet GetIsa() override { return InstructionSet::kThumb2; }

  void DumpAndCheck(std::vector<uint8_t>& code, const char* testname, const std::string& expected) {
#ifndef ART_TARGET_ANDROID
    std::string obj_file = scratch_dir_->GetPath() + testname + ".o";
    WriteElf</*IsElf64=*/false>(obj_file, InstructionSet::kThumb2, code);
    std::string disassembly;
    ASSERT_TRUE(Disassemble(obj_file, &disassembly));

    // objdump on buildbot seems to sometimes add annotation like in "bne #226 <.text+0x1e8>".
    // It is unclear why it does not reproduce locally. As work-around, remove the annotation.
    std::regex annotation_re(" <\\.text\\+\\w+>");
    disassembly = std::regex_replace(disassembly, annotation_re, "");

    std::string expected2 = "\n" +
        obj_file + ": file format ELF32-arm-little\n\n\n"
        "Disassembly of section .text:\n\n"
        "00000000 .text:\n" +
        expected;
    EXPECT_EQ(expected2, disassembly);
    if (expected2 != disassembly) {
      std::string out = "  \"" + Replace(disassembly, "\n", "\\n\"\n  \"") + "\"";
      printf("C++ formatted disassembler output for %s:\n%s\n", testname, out.c_str());
    }
#endif  // ART_TARGET_ANDROID
  }

#define __ assembler.

  void EmitAndCheck(const char* testname, const char* expected) {
    __ FinalizeCode();
    size_t cs = __ CodeSize();
    std::vector<uint8_t> managed_code(cs);
    MemoryRegion code(&managed_code[0], managed_code.size());
    __ FinalizeInstructions(code);

    DumpAndCheck(managed_code, testname, expected);
  }

#undef __

#define __ assembler.

  MallocArenaPool pool;
  ArenaAllocator allocator;
  ArmVIXLJNIMacroAssembler assembler;
};

TEST_F(ArmVIXLAssemblerTest, VixlJniHelpers) {
  // Run the test only with Baker read barriers, as the expected
  // generated code contains a Marking Register refresh instruction.
  TEST_DISABLED_WITHOUT_BAKER_READ_BARRIERS();

  const bool is_static = true;
  const bool is_synchronized = false;
  const bool is_critical_native = false;
  const char* shorty = "IIFII";

  std::unique_ptr<JniCallingConvention> jni_conv(
      JniCallingConvention::Create(&allocator,
                                   is_static,
                                   is_synchronized,
                                   is_critical_native,
                                   shorty,
                                   InstructionSet::kThumb2));
  std::unique_ptr<ManagedRuntimeCallingConvention> mr_conv(
      ManagedRuntimeCallingConvention::Create(
          &allocator, is_static, is_synchronized, shorty, InstructionSet::kThumb2));
  const int frame_size(jni_conv->FrameSize());
  ArrayRef<const ManagedRegister> callee_save_regs = jni_conv->CalleeSaveRegisters();

  const ManagedRegister method_register = ArmManagedRegister::FromCoreRegister(R0);
  const ManagedRegister hidden_arg_register = ArmManagedRegister::FromCoreRegister(R4);
  const ManagedRegister scratch_register = ArmManagedRegister::FromCoreRegister(R12);

  __ BuildFrame(frame_size, mr_conv->MethodRegister(), callee_save_regs);

  // Spill arguments.
  mr_conv->ResetIterator(FrameOffset(frame_size));
  for (; mr_conv->HasNext(); mr_conv->Next()) {
    if (mr_conv->IsCurrentParamInRegister()) {
      size_t size = mr_conv->IsCurrentParamALongOrDouble() ? 8u : 4u;
      __ Store(mr_conv->CurrentParamStackOffset(), mr_conv->CurrentParamRegister(), size);
    }
  }
  __ IncreaseFrameSize(32);

  // Loads
  __ IncreaseFrameSize(4096);
  __ Load(method_register, FrameOffset(32), 4);
  __ Load(method_register, FrameOffset(124), 4);
  __ Load(method_register, FrameOffset(132), 4);
  __ Load(method_register, FrameOffset(1020), 4);
  __ Load(method_register, FrameOffset(1024), 4);
  __ Load(scratch_register, FrameOffset(4092), 4);
  __ Load(scratch_register, FrameOffset(4096), 4);
  __ LoadRawPtrFromThread(scratch_register, ThreadOffset32(512));
  __ LoadRef(method_register, scratch_register, MemberOffset(128), /* unpoison_reference= */ false);

  // Stores
  __ Store(FrameOffset(32), method_register, 4);
  __ Store(FrameOffset(124), method_register, 4);
  __ Store(FrameOffset(132), method_register, 4);
  __ Store(FrameOffset(1020), method_register, 4);
  __ Store(FrameOffset(1024), method_register, 4);
  __ Store(FrameOffset(4092), scratch_register, 4);
  __ Store(FrameOffset(4096), scratch_register, 4);
  __ StoreImmediateToFrame(FrameOffset(48), 0xFF);
  __ StoreImmediateToFrame(FrameOffset(48), 0xFFFFFF);
  __ StoreRawPtr(FrameOffset(48), scratch_register);
  __ StoreRef(FrameOffset(48), scratch_register);
  __ StoreSpanning(FrameOffset(48), method_register, FrameOffset(48));
  __ StoreStackOffsetToThread(ThreadOffset32(512), FrameOffset(4096));
  __ StoreStackPointerToThread(ThreadOffset32(512));

  // Other
  __ Call(method_register, FrameOffset(48));
  __ Copy(FrameOffset(48), FrameOffset(44), 4);
  __ CopyRawPtrFromThread(FrameOffset(44), ThreadOffset32(512));
  __ CopyRef(FrameOffset(48), FrameOffset(44));
  __ GetCurrentThread(method_register);
  __ GetCurrentThread(FrameOffset(48));
  __ Move(hidden_arg_register, method_register, 4);
  __ VerifyObject(scratch_register, false);

  __ CreateHandleScopeEntry(scratch_register, FrameOffset(48), scratch_register, true);
  __ CreateHandleScopeEntry(scratch_register, FrameOffset(48), scratch_register, false);
  __ CreateHandleScopeEntry(method_register, FrameOffset(48), scratch_register, true);
  __ CreateHandleScopeEntry(FrameOffset(48), FrameOffset(64), true);
  __ CreateHandleScopeEntry(method_register, FrameOffset(0), scratch_register, true);
  __ CreateHandleScopeEntry(method_register, FrameOffset(1025), scratch_register, true);
  __ CreateHandleScopeEntry(scratch_register, FrameOffset(1025), scratch_register, true);

  __ ExceptionPoll(0);

  // Push the target out of range of branch emitted by ExceptionPoll.
  for (int i = 0; i < 64; i++) {
    __ Store(FrameOffset(2047), scratch_register, 4);
  }

  __ DecreaseFrameSize(4096);
  __ DecreaseFrameSize(32);
  __ RemoveFrame(frame_size, callee_save_regs, /* may_suspend= */ true);

  EmitAndCheck("VixlJniHelpers", VixlJniHelpersResults);
}

#undef __

// TODO: Avoid these macros.
#define R0 vixl::aarch32::r0
#define R2 vixl::aarch32::r2
#define R4 vixl::aarch32::r4
#define R12 vixl::aarch32::r12

#define __ assembler.asm_.

TEST_F(ArmVIXLAssemblerTest, VixlLoadFromOffset) {
  __ LoadFromOffset(kLoadWord, R2, R4, 12);
  __ LoadFromOffset(kLoadWord, R2, R4, 0xfff);
  __ LoadFromOffset(kLoadWord, R2, R4, 0x1000);
  __ LoadFromOffset(kLoadWord, R2, R4, 0x1000a4);
  __ LoadFromOffset(kLoadWord, R2, R4, 0x101000);
  __ LoadFromOffset(kLoadWord, R4, R4, 0x101000);
  __ LoadFromOffset(kLoadUnsignedHalfword, R2, R4, 12);
  __ LoadFromOffset(kLoadUnsignedHalfword, R2, R4, 0xfff);
  __ LoadFromOffset(kLoadUnsignedHalfword, R2, R4, 0x1000);
  __ LoadFromOffset(kLoadUnsignedHalfword, R2, R4, 0x1000a4);
  __ LoadFromOffset(kLoadUnsignedHalfword, R2, R4, 0x101000);
  __ LoadFromOffset(kLoadUnsignedHalfword, R4, R4, 0x101000);
  __ LoadFromOffset(kLoadWordPair, R2, R4, 12);
  __ LoadFromOffset(kLoadWordPair, R2, R4, 0x3fc);
  __ LoadFromOffset(kLoadWordPair, R2, R4, 0x400);
  __ LoadFromOffset(kLoadWordPair, R2, R4, 0x400a4);
  __ LoadFromOffset(kLoadWordPair, R2, R4, 0x40400);
  __ LoadFromOffset(kLoadWordPair, R4, R4, 0x40400);

  vixl::aarch32::UseScratchRegisterScope temps(assembler.asm_.GetVIXLAssembler());
  temps.Exclude(R12);
  __ LoadFromOffset(kLoadWord, R0, R12, 12);  // 32-bit because of R12.
  temps.Include(R12);
  __ LoadFromOffset(kLoadWord, R2, R4, 0xa4 - 0x100000);

  __ LoadFromOffset(kLoadSignedByte, R2, R4, 12);
  __ LoadFromOffset(kLoadUnsignedByte, R2, R4, 12);
  __ LoadFromOffset(kLoadSignedHalfword, R2, R4, 12);

  EmitAndCheck("VixlLoadFromOffset", VixlLoadFromOffsetResults);
}

TEST_F(ArmVIXLAssemblerTest, VixlStoreToOffset) {
  __ StoreToOffset(kStoreWord, R2, R4, 12);
  __ StoreToOffset(kStoreWord, R2, R4, 0xfff);
  __ StoreToOffset(kStoreWord, R2, R4, 0x1000);
  __ StoreToOffset(kStoreWord, R2, R4, 0x1000a4);
  __ StoreToOffset(kStoreWord, R2, R4, 0x101000);
  __ StoreToOffset(kStoreWord, R4, R4, 0x101000);
  __ StoreToOffset(kStoreHalfword, R2, R4, 12);
  __ StoreToOffset(kStoreHalfword, R2, R4, 0xfff);
  __ StoreToOffset(kStoreHalfword, R2, R4, 0x1000);
  __ StoreToOffset(kStoreHalfword, R2, R4, 0x1000a4);
  __ StoreToOffset(kStoreHalfword, R2, R4, 0x101000);
  __ StoreToOffset(kStoreHalfword, R4, R4, 0x101000);
  __ StoreToOffset(kStoreWordPair, R2, R4, 12);
  __ StoreToOffset(kStoreWordPair, R2, R4, 0x3fc);
  __ StoreToOffset(kStoreWordPair, R2, R4, 0x400);
  __ StoreToOffset(kStoreWordPair, R2, R4, 0x400a4);
  __ StoreToOffset(kStoreWordPair, R2, R4, 0x40400);
  __ StoreToOffset(kStoreWordPair, R4, R4, 0x40400);

  vixl::aarch32::UseScratchRegisterScope temps(assembler.asm_.GetVIXLAssembler());
  temps.Exclude(R12);
  __ StoreToOffset(kStoreWord, R0, R12, 12);  // 32-bit because of R12.
  temps.Include(R12);
  __ StoreToOffset(kStoreWord, R2, R4, 0xa4 - 0x100000);

  __ StoreToOffset(kStoreByte, R2, R4, 12);

  EmitAndCheck("VixlStoreToOffset", VixlStoreToOffsetResults);
}

#undef __
}  // namespace arm
}  // namespace art
