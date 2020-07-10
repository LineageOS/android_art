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

#include "dwarf_test.h"

#include "dwarf/debug_frame_opcode_writer.h"
#include "dwarf/debug_info_entry_writer.h"
#include "dwarf/debug_line_opcode_writer.h"
#include "dwarf/dwarf_constants.h"
#include "dwarf/headers.h"
#include "gtest/gtest.h"

namespace art {
namespace dwarf {

// Run the tests only on host since we need objdump.
#ifndef ART_TARGET_ANDROID

TEST_F(DwarfTest, DebugFrame) {
  const bool is64bit = false;

  // Pick offset value which would catch Uleb vs Sleb errors.
  const int offset = 40000;
  ASSERT_EQ(UnsignedLeb128Size(offset / 4), 2u);
  ASSERT_EQ(SignedLeb128Size(offset / 4), 3u);
  const Reg reg(6);

  // Test the opcodes in the order mentioned in the spec.
  // There are usually several encoding variations of each opcode.
  DebugFrameOpCodeWriter<> opcodes;
  DW_CHECK(".debug_frame contents:");
  DW_CHECK("FDE");
  DW_CHECK_NEXT("DW_CFA_nop:");  // TODO: Why is a nop here.
  int pc = 0;
  for (int i : {0, 1, 0x3F, 0x40, 0xFF, 0x100, 0xFFFF, 0x10000}) {
    pc += i;
    opcodes.AdvancePC(pc);
  }
  DW_CHECK_NEXT("DW_CFA_advance_loc: 1");
  DW_CHECK_NEXT("DW_CFA_advance_loc: 63");
  DW_CHECK_NEXT("DW_CFA_advance_loc1: 64");
  DW_CHECK_NEXT("DW_CFA_advance_loc1: 255");
  DW_CHECK_NEXT("DW_CFA_advance_loc2: 256");
  DW_CHECK_NEXT("DW_CFA_advance_loc2: 65535");
  DW_CHECK_NEXT("DW_CFA_advance_loc4: 65536");
  opcodes.DefCFA(reg, offset);
  DW_CHECK_NEXT("DW_CFA_def_cfa: reg6 +40000");
  opcodes.DefCFA(reg, -offset);
  DW_CHECK_NEXT("DW_CFA_def_cfa_sf: reg6 -40000");
  opcodes.DefCFARegister(reg);
  DW_CHECK_NEXT("DW_CFA_def_cfa_register: reg6");
  opcodes.DefCFAOffset(offset);
  DW_CHECK_NEXT("DW_CFA_def_cfa_offset: +40000");
  opcodes.DefCFAOffset(-offset);
  DW_CHECK_NEXT("DW_CFA_def_cfa_offset_sf: -40000");
  uint8_t expr[] = { /*nop*/ 0x96 };
  opcodes.DefCFAExpression(expr, arraysize(expr));
  DW_CHECK_NEXT("DW_CFA_def_cfa_expression: DW_OP_nop");
  opcodes.Undefined(reg);
  DW_CHECK_NEXT("DW_CFA_undefined: reg6");
  opcodes.SameValue(reg);
  DW_CHECK_NEXT("DW_CFA_same_value: reg6");
  opcodes.Offset(Reg(0x3F), -offset);
  DW_CHECK_NEXT("DW_CFA_offset: reg63 -40000");
  opcodes.Offset(Reg(0x40), -offset);
  DW_CHECK_NEXT("DW_CFA_offset_extended: reg64 -40000");
  opcodes.Offset(Reg(0x40), offset);
  DW_CHECK_NEXT("DW_CFA_offset_extended_sf: reg64 40000");
  opcodes.ValOffset(reg, -offset);
  DW_CHECK_NEXT("DW_CFA_val_offset: reg6 -40000");
  opcodes.ValOffset(reg, offset);
  DW_CHECK_NEXT("DW_CFA_val_offset_sf: reg6 40000");
  opcodes.Register(reg, Reg(1));
  DW_CHECK_NEXT("DW_CFA_register: reg6 reg1");
  opcodes.Expression(reg, expr, arraysize(expr));
  DW_CHECK_NEXT("DW_CFA_expression: reg6 DW_OP_nop");
  opcodes.ValExpression(reg, expr, arraysize(expr));
  DW_CHECK_NEXT("DW_CFA_val_expression: reg6 DW_OP_nop");
  opcodes.Restore(Reg(0x3F));
  DW_CHECK_NEXT("DW_CFA_restore: reg63");
  opcodes.Restore(Reg(0x40));
  DW_CHECK_NEXT("DW_CFA_restore_extended: reg64");
  opcodes.Restore(reg);
  DW_CHECK_NEXT("DW_CFA_restore: reg6");
  opcodes.RememberState();
  DW_CHECK_NEXT("DW_CFA_remember_state:");
  opcodes.RestoreState();
  DW_CHECK_NEXT("DW_CFA_restore_state:");
  opcodes.Nop();
  DW_CHECK_NEXT("DW_CFA_nop:");

  // Also test helpers.
  opcodes.DefCFA(Reg(4), 100);  // ESP
  DW_CHECK_NEXT("DW_CFA_def_cfa: reg4 +100");
  opcodes.AdjustCFAOffset(8);
  DW_CHECK_NEXT("DW_CFA_def_cfa_offset: +108");
  opcodes.RelOffset(Reg(0), 0);  // push R0
  DW_CHECK_NEXT("DW_CFA_offset: reg0 -108");
  opcodes.RelOffset(Reg(1), 4);  // push R1
  DW_CHECK_NEXT("DW_CFA_offset: reg1 -104");
  opcodes.RelOffsetForMany(Reg(2), 8, 1 | (1 << 3), 4);  // push R2 and R5
  DW_CHECK_NEXT("DW_CFA_offset: reg2 -100");
  DW_CHECK_NEXT("DW_CFA_offset: reg5 -96");
  opcodes.RestoreMany(Reg(2), 1 | (1 << 3));  // pop R2 and R5
  DW_CHECK_NEXT("DW_CFA_restore: reg2");
  DW_CHECK_NEXT("DW_CFA_restore: reg5");

  DebugFrameOpCodeWriter<> initial_opcodes;
  WriteCIE(is64bit, Reg(is64bit ? 16 : 8), initial_opcodes, &debug_frame_data_);
  WriteFDE(is64bit,
           /* cie_pointer= */ 0,
           0x01000000,
           0x01000000,
           ArrayRef<const uint8_t>(*opcodes.data()),
           &debug_frame_data_);

  CheckObjdumpOutput(is64bit, "-debug-frame");
}

TEST_F(DwarfTest, DISABLED_DebugFrame64) {
  constexpr bool is64bit = true;
  DebugFrameOpCodeWriter<> initial_opcodes;
  WriteCIE(is64bit, Reg(16), initial_opcodes, &debug_frame_data_);
  DebugFrameOpCodeWriter<> opcodes;
  DW_CHECK(".debug_frame contents:");
  WriteFDE(is64bit,
           /* cie_pointer= */ 0,
           0x0100000000000000,
           0x0200000000000000,
           ArrayRef<const uint8_t>(*opcodes.data()),
           &debug_frame_data_);
  DW_CHECK("FDE cie=00000000 pc=100000000000000..300000000000000");

  CheckObjdumpOutput(is64bit, "-debug-frame");
}

// Test x86_64 register mapping. It is the only non-trivial architecture.
// ARM and X86 have: dwarf_reg = art_reg + constant.
TEST_F(DwarfTest, x86_64_RegisterMapping) {
  constexpr bool is64bit = true;
  DebugFrameOpCodeWriter<> opcodes;
  DW_CHECK(".debug_frame contents:");
  for (int i = 0; i < 16; i++) {
    opcodes.RelOffset(Reg::X86_64Core(i), 0);
  }
  DW_CHECK("FDE");
  DW_CHECK_NEXT("DW_CFA_nop:");  // TODO: Why is a nop here.
  DW_CHECK_NEXT("DW_CFA_offset: reg0 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg2 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg1 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg3 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg7 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg6 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg4 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg5 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg8 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg9 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg10 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg11 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg12 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg13 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg14 0");
  DW_CHECK_NEXT("DW_CFA_offset: reg15 0");

  DebugFrameOpCodeWriter<> initial_opcodes;
  WriteCIE(is64bit, Reg(16), initial_opcodes, &debug_frame_data_);
  WriteFDE(is64bit,
           /* cie_pointer= */ 0,
           0x0100000000000000,
           0x0200000000000000,
           ArrayRef<const uint8_t>(*opcodes.data()),
           &debug_frame_data_);

  CheckObjdumpOutput(is64bit, "-debug-frame");
}

TEST_F(DwarfTest, DebugLine) {
  const bool is64bit = false;
  const int code_factor_bits = 1;
  DebugLineOpCodeWriter<> opcodes(is64bit, code_factor_bits);
  DW_CHECK(".debug_line contents:");

  std::vector<std::string> include_directories;
  include_directories.push_back("/path/to/source");
  DW_CHECK("include_directories[  1] = \"/path/to/source\"");

  std::vector<FileEntry> files {
    { "file0.c", 0, 1000, 2000 },
    { "file1.c", 1, 1000, 2000 },
    { "file2.c", 1, 1000, 2000 },
  };
  DW_CHECK_NEXT("file_names[  1]:");
  DW_CHECK_NEXT("           name: \"file0.c\"");
  DW_CHECK_NEXT("      dir_index: 0");
  DW_CHECK_NEXT("       mod_time: 0x000003e8");
  DW_CHECK_NEXT("         length: 0x000007d0");
  DW_CHECK_NEXT("file_names[  2]:");
  DW_CHECK_NEXT("           name: \"file1.c\"");
  DW_CHECK_NEXT("      dir_index: 1");
  DW_CHECK_NEXT("       mod_time: 0x000003e8");
  DW_CHECK_NEXT("         length: 0x000007d0");
  DW_CHECK_NEXT("file_names[  3]:");
  DW_CHECK_NEXT("           name: \"file2.c\"");
  DW_CHECK_NEXT("      dir_index: 1");
  DW_CHECK_NEXT("       mod_time: 0x000003e8");
  DW_CHECK_NEXT("         length: 0x000007d0");
  DW_CHECK_NEXT("file_names[  4]:");
  DW_CHECK_NEXT("           name: \"file.c\"");
  DW_CHECK_NEXT("      dir_index: 0");
  DW_CHECK_NEXT("       mod_time: 0x000003e8");
  DW_CHECK_NEXT("         length: 0x000007d0");

  opcodes.SetAddress(0x01000000);
  opcodes.SetIsStmt(true);
  opcodes.AddRow();
  opcodes.AdvancePC(0x01000100);
  opcodes.SetFile(2);
  opcodes.AdvanceLine(3);
  opcodes.SetColumn(4);
  opcodes.SetIsStmt(false);
  opcodes.SetBasicBlock();
  opcodes.SetPrologueEnd();
  opcodes.SetEpilogueBegin();
  opcodes.SetISA(5);
  opcodes.EndSequence();
  opcodes.DefineFile("file.c", 0, 1000, 2000);
  DW_CHECK_NEXT("Address            Line   Column File   ISA Discriminator Flags");
  DW_CHECK_NEXT("------------------ ------ ------ ------ --- ------------- -------------");
  DW_CHECK_NEXT("0x0000000001000000      1      0      1   0             0  is_stmt");
  DW_CHECK_NEXT("0x0000000001000100      3      4      2   5             0  basic_block prologue_end epilogue_begin end_sequence");

  WriteDebugLineTable(include_directories, files, opcodes, &debug_line_data_);

  CheckObjdumpOutput(is64bit, "-debug-line");
}

// DWARF has special one byte codes which advance PC and line at the same time.
TEST_F(DwarfTest, DebugLineSpecialOpcodes) {
  const bool is64bit = false;
  const int code_factor_bits = 1;
  uint32_t pc = 0x01000000;
  int line = 1;
  DebugLineOpCodeWriter<> opcodes(is64bit, code_factor_bits);
  opcodes.SetAddress(pc);
  size_t num_rows = 0;
  DW_CHECK(".debug_line contents:");
  DW_CHECK("file_names[  1]:");
  DW_CHECK("           name: \"file.c\"");
  DW_CHECK("Address            Line   Column File   ISA Discriminator Flags");
  DW_CHECK("------------------ ------ ------ ------ --- ------------- -------------");
  for (int addr_delta = 0; addr_delta < 80; addr_delta += 2) {
    for (int line_delta = 16; line_delta >= -16; --line_delta) {
      pc += addr_delta;
      line += line_delta;
      opcodes.AddRow(pc, line);
      num_rows++;
      ASSERT_EQ(opcodes.CurrentAddress(), pc);
      ASSERT_EQ(opcodes.CurrentLine(), line);
      char expected[1024];
      sprintf(expected, "0x%016x %6i      0      1   0             0", pc, line);
      DW_CHECK_NEXT(expected);
    }
  }
  opcodes.EndSequence();
  EXPECT_LT(opcodes.data()->size(), num_rows * 3);

  std::vector<std::string> directories;
  std::vector<FileEntry> files = { { "file.c", 0, 1000, 2000 } };
  WriteDebugLineTable(directories, files, opcodes, &debug_line_data_);

  CheckObjdumpOutput(is64bit, "-debug-line");
}

TEST_F(DwarfTest, DebugInfo) {
  constexpr bool is64bit = false;

  DebugAbbrevWriter<> debug_abbrev(&debug_abbrev_data_);
  DW_CHECK(".debug_abbrev contents:");
  DW_CHECK_NEXT("Abbrev table for offset: 0x00000000");
  DW_CHECK_NEXT("[1] DW_TAG_compile_unit DW_CHILDREN_yes");
  DW_CHECK_NEXT(" DW_AT_producer DW_FORM_strp");
  DW_CHECK_NEXT(" DW_AT_low_pc DW_FORM_addr");
  DW_CHECK_NEXT(" DW_AT_high_pc DW_FORM_addr");
  DW_CHECK_NEXT("[2] DW_TAG_subprogram DW_CHILDREN_no");
  DW_CHECK_NEXT(" DW_AT_name DW_FORM_strp");
  DW_CHECK_NEXT(" DW_AT_low_pc DW_FORM_addr");
  DW_CHECK_NEXT(" DW_AT_high_pc DW_FORM_addr");
  DW_CHECK_NEXT("[3] DW_TAG_compile_unit DW_CHILDREN_no");

  DebugInfoEntryWriter<> info(is64bit, &debug_abbrev);
  DW_CHECK(".debug_info contents:");
  info.StartTag(dwarf::DW_TAG_compile_unit);
  DW_CHECK_NEXT("Compile Unit: length = 0x00000030 version = 0x0004 abbr_offset = 0x0000 addr_size = 0x04");
  DW_CHECK_NEXT("DW_TAG_compile_unit");
  info.WriteStrp(dwarf::DW_AT_producer, "Compiler name", &debug_str_data_);
  DW_CHECK_NEXT("  DW_AT_producer (\"Compiler name\")");
  info.WriteAddr(dwarf::DW_AT_low_pc, 0x01000000);
  DW_CHECK_NEXT("  DW_AT_low_pc (0x0000000001000000)");
  info.WriteAddr(dwarf::DW_AT_high_pc, 0x02000000);
  DW_CHECK_NEXT("  DW_AT_high_pc (0x0000000002000000)");
  info.StartTag(dwarf::DW_TAG_subprogram);
  DW_CHECK_NEXT("  DW_TAG_subprogram");
  info.WriteStrp(dwarf::DW_AT_name, "Foo", &debug_str_data_);
  DW_CHECK_NEXT("    DW_AT_name (\"Foo\")");
  info.WriteAddr(dwarf::DW_AT_low_pc, 0x01010000);
  DW_CHECK_NEXT("    DW_AT_low_pc (0x0000000001010000)");
  info.WriteAddr(dwarf::DW_AT_high_pc, 0x01020000);
  DW_CHECK_NEXT("    DW_AT_high_pc (0x0000000001020000)");
  info.EndTag();  // DW_TAG_subprogram
  info.StartTag(dwarf::DW_TAG_subprogram);
  DW_CHECK_NEXT("  DW_TAG_subprogram");
  info.WriteStrp(dwarf::DW_AT_name, "Bar", &debug_str_data_);
  DW_CHECK_NEXT("    DW_AT_name (\"Bar\")");
  info.WriteAddr(dwarf::DW_AT_low_pc, 0x01020000);
  DW_CHECK_NEXT("    DW_AT_low_pc (0x0000000001020000)");
  info.WriteAddr(dwarf::DW_AT_high_pc, 0x01030000);
  DW_CHECK_NEXT("    DW_AT_high_pc (0x0000000001030000)");
  info.EndTag();  // DW_TAG_subprogram
  info.EndTag();  // DW_TAG_compile_unit
  DW_CHECK_NEXT("  NULL");
  // Test that previous list was properly terminated and empty children.
  info.StartTag(dwarf::DW_TAG_compile_unit);
  info.EndTag();  // DW_TAG_compile_unit

  dwarf::WriteDebugInfoCU(/* debug_abbrev_offset= */ 0, info, &debug_info_data_);

  CheckObjdumpOutput(is64bit, "-debug-info -debug-abbrev");
}

#endif  // ART_TARGET_ANDROID

}  // namespace dwarf
}  // namespace art
