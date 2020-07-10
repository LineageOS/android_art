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

#ifndef ART_COMPILER_UTILS_ASSEMBLER_TEST_BASE_H_
#define ART_COMPILER_UTILS_ASSEMBLER_TEST_BASE_H_

#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "android-base/strings.h"

#include "base/os.h"
#include "base/utils.h"
#include "common_runtime_test.h"  // For ScratchDir.
#include "elf/elf_builder.h"
#include "elf/elf_debug_reader.h"
#include "exec_utils.h"
#include "stream/file_output_stream.h"

namespace art {

// If you want to take a look at the differences between the ART assembler and clang,
// set this flag to true. The disassembled files will then remain in the tmp directory.
static constexpr bool kKeepDisassembledFiles = false;

// We put this into a class as gtests are self-contained, so this helper needs to be in an h-file.
class AssemblerTestBase : public testing::Test {
 public:
  AssemblerTestBase() {}

  void SetUp() override {
    // Fake a runtime test for ScratchDir.
    CommonArtTest::SetUpAndroidRootEnvVars();
    CommonRuntimeTest::SetUpAndroidDataDir(android_data_);
    scratch_dir_.emplace(/*keep_files=*/ kKeepDisassembledFiles);
  }

  void TearDown() override {
    // We leave temporaries in case this failed so we can debug issues.
    CommonRuntimeTest::TearDownAndroidDataDir(android_data_, false);
  }

  // This is intended to be run as a test.
  bool CheckTools() {
    for (auto cmd : { GetAssemblerCommand()[0], GetDisassemblerCommand()[0] }) {
      if (!OS::FileExists(cmd.c_str())) {
        LOG(ERROR) << "Could not find " << cmd;
        return false;
      }
    }
    return true;
  }

  // Driver() assembles and compares the results. If the results are not equal and we have a
  // disassembler, disassemble both and check whether they have the same mnemonics (in which case
  // we just warn).
  void Driver(const std::vector<uint8_t>& art_code,
              const std::string& assembly_text,
              const std::string& test_name) {
    ASSERT_NE(assembly_text.length(), 0U) << "Empty assembly";
    InstructionSet isa = GetIsa();
    auto test_path = [&](const char* ext) { return scratch_dir_->GetPath() + test_name + ext; };

    // Create file containing the reference source code.
    std::string ref_asm_file = test_path(".ref.S");
    WriteFile(ref_asm_file, assembly_text.data(), assembly_text.size());

    // Assemble reference object file.
    std::string ref_obj_file = test_path(".ref.o");
    ASSERT_TRUE(Assemble(ref_asm_file.c_str(), ref_obj_file.c_str()));

    // Read the code produced by assembler from the ELF file.
    std::vector<uint8_t> ref_code;
    if (Is64BitInstructionSet(isa)) {
      ReadElf</*IsElf64=*/true>(ref_obj_file, &ref_code);
    } else {
      ReadElf</*IsElf64=*/false>(ref_obj_file, &ref_code);
    }

    // Compare the ART generated code to the expected reference code.
    if (art_code == ref_code) {
      return;  // Success!
    }

    // Create ELF file containing the ART code.
    std::string art_obj_file = test_path(".art.o");
    if (Is64BitInstructionSet(isa)) {
      WriteElf</*IsElf64=*/true>(art_obj_file, isa, art_code);
    } else {
      WriteElf</*IsElf64=*/false>(art_obj_file, isa, art_code);
    }

    // Disassemble both object files, and check that the outputs match.
    std::string art_disassembly;
    ASSERT_TRUE(Disassemble(art_obj_file, &art_disassembly));
    art_disassembly = Replace(art_disassembly, art_obj_file, test_path("<extension-redacted>"));
    std::string ref_disassembly;
    ASSERT_TRUE(Disassemble(ref_obj_file, &ref_disassembly));
    ref_disassembly = Replace(ref_disassembly, ref_obj_file, test_path("<extension-redacted>"));
    ASSERT_EQ(art_disassembly, ref_disassembly) << "Outputs (and disassembly) not identical.";

    // ART produced different (but valid) code than the reference assembler, report it.
    if (art_code.size() > ref_code.size()) {
      EXPECT_TRUE(false) << "ART code is larger then the reference code, but the disassembly"
          "of machine code is equal: this means that ART is generating sub-optimal encoding! "
          "ART code size=" << art_code.size() << ", reference code size=" << ref_code.size();
    } else if (art_code.size() < ref_code.size()) {
      EXPECT_TRUE(false) << "ART code is smaller than the reference code. Too good to be true?";
    } else {
      LOG(INFO) << "Reference assembler chose a different encoding than ART (of the same size)";
    }
  }

 protected:
  virtual InstructionSet GetIsa() = 0;

  std::string FindTool(const std::string& tool_name) {
    return CommonArtTest::GetAndroidTool(tool_name.c_str(), GetIsa());
  }

  virtual std::vector<std::string> GetAssemblerCommand() {
    switch (GetIsa()) {
      case InstructionSet::kX86:
        return {FindTool("as"), "--32"};
      case InstructionSet::kX86_64:
        return {FindTool("as"), "--64"};
      default:
        return {FindTool("as")};
    }
  }

  virtual std::vector<std::string> GetDisassemblerCommand() {
    switch (GetIsa()) {
      case InstructionSet::kThumb2:
        return {FindTool("objdump"), "--disassemble", "-M", "force-thumb"};
      default:
        return {FindTool("objdump"), "--disassemble", "--no-show-raw-insn"};
    }
  }

  bool Assemble(const std::string& asm_file, const std::string& obj_file) {
    std::vector<std::string> args = GetAssemblerCommand();
    args.insert(args.end(), {"-o", obj_file, asm_file});
    std::string output;
    bool ok = CommonArtTestImpl::ForkAndExec(args, [](){ return true; }, &output).StandardSuccess();
    if (!ok) {
      LOG(ERROR) << "Assembler error:\n" << output;
    }
    return ok;
  }

  bool Disassemble(const std::string& obj_file, std::string* output) {
    std::vector<std::string> args = GetDisassemblerCommand();
    args.insert(args.end(), {obj_file});
    bool ok = CommonArtTestImpl::ForkAndExec(args, [](){ return true; }, output).StandardSuccess();
    if (!ok) {
      LOG(ERROR) << "Disassembler error:\n" << *output;
    }
    *output = Replace(*output, "\t", " ");
    return ok;
  }

  std::vector<uint8_t> ReadFile(const std::string& filename) {
    std::unique_ptr<File> file(OS::OpenFileForReading(filename.c_str()));
    CHECK(file.get() != nullptr);
    std::vector<uint8_t> data(file->GetLength());
    bool success = file->ReadFully(&data[0], data.size());
    CHECK(success) << filename;
    return data;
  }

  void WriteFile(const std::string& filename, const void* data, size_t size) {
    std::unique_ptr<File> file(OS::CreateEmptyFile(filename.c_str()));
    CHECK(file.get() != nullptr);
    bool success = file->WriteFully(data, size);
    CHECK(success) << filename;
    CHECK_EQ(file->FlushClose(), 0);
  }

  // Helper method which reads the content of .text section from ELF file.
  template<bool IsElf64>
  void ReadElf(const std::string& filename, /*out*/ std::vector<uint8_t>* code) {
    using ElfTypes = typename std::conditional<IsElf64, ElfTypes64, ElfTypes32>::type;
    std::vector<uint8_t> data = ReadFile(filename);
    ElfDebugReader<ElfTypes> reader((ArrayRef<const uint8_t>(data)));
    const typename ElfTypes::Shdr* text = reader.GetSection(".text");
    CHECK(text != nullptr);
    *code = std::vector<uint8_t>(&data[text->sh_offset], &data[text->sh_offset + text->sh_size]);
  }

  // Helper method to create an ELF file containing only the given code in the .text section.
  template<bool IsElf64>
  void WriteElf(const std::string& filename, InstructionSet isa, const std::vector<uint8_t>& code) {
    using ElfTypes = typename std::conditional<IsElf64, ElfTypes64, ElfTypes32>::type;
    std::unique_ptr<File> file(OS::CreateEmptyFile(filename.c_str()));
    CHECK(file.get() != nullptr);
    FileOutputStream out(file.get());
    std::unique_ptr<ElfBuilder<ElfTypes>> builder(new ElfBuilder<ElfTypes>(isa, &out));
    builder->Start(/* write_program_headers= */ false);
    builder->GetText()->Start();
    builder->GetText()->WriteFully(code.data(), code.size());
    builder->GetText()->End();
    builder->End();
    CHECK(builder->Good());
    CHECK_EQ(file->Close(), 0);
  }

  static std::string GetRootPath() {
    // 1) Check ANDROID_BUILD_TOP
    char* build_top = getenv("ANDROID_BUILD_TOP");
    if (build_top != nullptr) {
      return std::string(build_top) + "/";
    }

    // 2) Do cwd
    char temp[1024];
    return getcwd(temp, 1024) ? std::string(temp) + "/" : std::string("");
  }

  std::string Replace(const std::string& str, const std::string& from, const std::string& to) {
    std::string output;
    size_t pos = 0;
    for (auto match = str.find(from); match != str.npos; match = str.find(from, pos)) {
      output += str.substr(pos, match - pos);
      output += to;
      pos = match + from.size();
    }
    output += str.substr(pos, str.size() - pos);
    return output;
  }

  std::optional<ScratchDir> scratch_dir_;
  std::string android_data_;
  DISALLOW_COPY_AND_ASSIGN(AssemblerTestBase);
};

}  // namespace art

#endif  // ART_COMPILER_UTILS_ASSEMBLER_TEST_BASE_H_
