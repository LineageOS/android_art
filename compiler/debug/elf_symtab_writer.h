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

#ifndef ART_COMPILER_DEBUG_ELF_SYMTAB_WRITER_H_
#define ART_COMPILER_DEBUG_ELF_SYMTAB_WRITER_H_

#include <map>
#include <unordered_set>
#include <unordered_map>

#include "base/utils.h"
#include "debug/debug_info.h"
#include "debug/method_debug_info.h"
#include "dex/code_item_accessors.h"
#include "dex/descriptors_names.h"
#include "dex/dex_file-inl.h"
#include "elf/elf_builder.h"

namespace art {
namespace debug {

// The ARM specification defines three special mapping symbols
// $a, $t and $d which mark ARM, Thumb and data ranges respectively.
// These symbols can be used by tools, for example, to pretty
// print instructions correctly.  Objdump will use them if they
// exist, but it will still work well without them.
// However, these extra symbols take space, so let's just generate
// one symbol which marks the whole .text section as code.
// Note that ARM's Streamline requires it to match function symbol.
constexpr bool kGenerateArmMappingSymbol = true;

// Create magic symbol to let libunwindstack know that symtab is sorted by address.
constexpr bool kGenerateSortedSymbol = true;
constexpr const char kSortedSymbolName[] = "$android.symtab.sorted";
constexpr size_t kSortedSymbolMinCount = 100;  // Don't bother if the table is very small (JIT).

// Magic name for .symtab symbols which enumerate dex files used
// by this ELF file (currently mmapped inside the .dex section).
constexpr const char* kDexFileSymbolName = "$dexfile";

// Return common parts of method names; shared by all methods in the given set.
// (e.g. "[DEDUPED] ?.<init>" or "com.android.icu.charset.CharsetEncoderICU.?")
static void GetDedupedName(const std::vector<const MethodDebugInfo*>& methods, std::string* out) {
  DCHECK(!methods.empty());
  const MethodDebugInfo* first = methods.front();
  auto is_same_class = [&first](const MethodDebugInfo* mi) {
    DCHECK(mi->dex_file != nullptr);
    return mi->dex_file == first->dex_file && mi->class_def_index == first->class_def_index;
  };
  auto is_same_method_name = [&first](const MethodDebugInfo* mi) {
    return strcmp(mi->dex_file->GetMethodName(mi->dex_method_index),
                  first->dex_file->GetMethodName(first->dex_method_index)) == 0;
  };
  bool all_same_class = std::all_of(methods.begin(), methods.end(), is_same_class);
  bool all_same_method_name = std::all_of(methods.begin(), methods.end(), is_same_method_name);
  *out = "[DEDUPED]";
  if (all_same_class || all_same_method_name) {
    *out += ' ';
    if (all_same_class) {
      auto& dex_class_def = first->dex_file->GetClassDef(first->class_def_index);
      AppendPrettyDescriptor(first->dex_file->GetClassDescriptor(dex_class_def), &*out);
    } else {
      *out += '?';
    }
    *out += '.';
    if (all_same_method_name) {
      *out += first->dex_file->GetMethodName(first->dex_method_index);
    } else {
      *out += '?';
    }
  }
}

template <typename ElfTypes>
static void WriteDebugSymbols(ElfBuilder<ElfTypes>* builder,
                              bool mini_debug_info,
                              const DebugInfo& debug_info) {
  uint64_t mapping_symbol_address = std::numeric_limits<uint64_t>::max();
  const auto* text = builder->GetText();
  auto* strtab = builder->GetStrTab();
  auto* symtab = builder->GetSymTab();

  if (debug_info.Empty()) {
    return;
  }

  // Find all addresses which contain deduped methods.
  // The first instance of method is not marked deduped_, but the rest is.
  std::unordered_set<uint64_t> deduped_addresses;
  for (const MethodDebugInfo& info : debug_info.compiled_methods) {
    if (info.deduped) {
      deduped_addresses.insert(info.code_address);
    }
    if (kGenerateArmMappingSymbol && info.isa == InstructionSet::kThumb2) {
      uint64_t address = info.code_address;
      address += info.is_code_address_text_relative ? text->GetAddress() : 0;
      mapping_symbol_address = std::min(mapping_symbol_address, address);
    }
  }

  // Create list of deduped methods per function address.
  // We have to do it separately since the first method does not have the deduped flag.
  std::unordered_map<uint64_t, std::vector<const MethodDebugInfo*>> deduped_methods;
  for (const MethodDebugInfo& info : debug_info.compiled_methods) {
    if (deduped_addresses.find(info.code_address) != deduped_addresses.end()) {
      deduped_methods[info.code_address].push_back(&info);
    }
  }

  strtab->Start();
  // Generate marker to annotate the symbol table as sorted (guaranteed by the ElfBuilder).
  // Note that LOCAL symbols are sorted before GLOBAL ones, so don't mix the two types.
  if (kGenerateSortedSymbol && debug_info.compiled_methods.size() >= kSortedSymbolMinCount) {
    symtab->Add(strtab->Write(kSortedSymbolName), nullptr, 0, 0, STB_GLOBAL, STT_NOTYPE);
  }
  // Generate ARM mapping symbols. ELF local symbols must be added first.
  if (mapping_symbol_address != std::numeric_limits<uint64_t>::max()) {
    symtab->Add(strtab->Write("$t"), text, mapping_symbol_address, 0, STB_GLOBAL, STT_NOTYPE);
  }
  // Add symbols for compiled methods.
  for (const MethodDebugInfo& info : debug_info.compiled_methods) {
    if (info.deduped) {
      continue;  // Add symbol only for the first instance.
    }
    size_t name_offset;
    if (!info.custom_name.empty()) {
      name_offset = strtab->Write(info.custom_name);
    } else {
      DCHECK(info.dex_file != nullptr);
      std::string name = info.dex_file->PrettyMethod(info.dex_method_index, !mini_debug_info);
      if (deduped_addresses.find(info.code_address) != deduped_addresses.end()) {
        // Create method name common to all the deduped methods if possible.
        // Around half of the time, there is either common class or method name.
        // NB: We used to return one method at random with tag, but developers found it confusing.
        GetDedupedName(deduped_methods[info.code_address], &name);
      }
      name_offset = strtab->Write(name);
    }

    uint64_t address = info.code_address;
    address += info.is_code_address_text_relative ? text->GetAddress() : 0;
    // Add in code delta, e.g., thumb bit 0 for Thumb2 code.
    address += CompiledMethod::CodeDelta(info.isa);
    symtab->Add(name_offset, text, address, info.code_size, STB_GLOBAL, STT_FUNC);
  }
  // Add symbols for dex files.
  if (!debug_info.dex_files.empty() && builder->GetDex()->Exists()) {
    auto dex = builder->GetDex();
    for (auto it : debug_info.dex_files) {
      uint64_t dex_address = dex->GetAddress() + it.first /* offset within the section */;
      const DexFile* dex_file = it.second;
      typename ElfTypes::Word dex_name = strtab->Write(kDexFileSymbolName);
      symtab->Add(dex_name, dex, dex_address, dex_file->Size(), STB_GLOBAL, STT_FUNC);
    }
  }
  strtab->End();

  // Symbols are buffered and written after names (because they are smaller).
  symtab->WriteCachedSection();
}

}  // namespace debug
}  // namespace art

#endif  // ART_COMPILER_DEBUG_ELF_SYMTAB_WRITER_H_

