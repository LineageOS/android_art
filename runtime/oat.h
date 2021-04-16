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

#ifndef ART_RUNTIME_OAT_H_
#define ART_RUNTIME_OAT_H_

#include <array>
#include <vector>

#include "base/compiler_filter.h"
#include "base/macros.h"
#include "base/safe_map.h"

namespace art {

enum class InstructionSet;
class InstructionSetFeatures;

class PACKED(4) OatHeader {
 public:
  static constexpr std::array<uint8_t, 4> kOatMagic { { 'o', 'a', 't', '\n' } };
  // Last oat version changed reason: Move code size from OatQuickMethodHeader to CodeInfo.
  static constexpr std::array<uint8_t, 4> kOatVersion { { '1', '9', '2', '\0' } };

  static constexpr const char* kDex2OatCmdLineKey = "dex2oat-cmdline";
  static constexpr const char* kDebuggableKey = "debuggable";
  static constexpr const char* kNativeDebuggableKey = "native-debuggable";
  static constexpr const char* kCompilerFilter = "compiler-filter";
  static constexpr const char* kClassPathKey = "classpath";
  static constexpr const char* kBootClassPathKey = "bootclasspath";
  static constexpr const char* kBootClassPathChecksumsKey = "bootclasspath-checksums";
  static constexpr const char* kConcurrentCopying = "concurrent-copying";
  static constexpr const char* kCompilationReasonKey = "compilation-reason";
  static constexpr const char* kRequiresImage = "requires-image";

  static constexpr const char kTrueValue[] = "true";
  static constexpr const char kFalseValue[] = "false";


  static OatHeader* Create(InstructionSet instruction_set,
                           const InstructionSetFeatures* instruction_set_features,
                           uint32_t dex_file_count,
                           const SafeMap<std::string, std::string>* variable_data);

  bool IsValid() const;
  std::string GetValidationErrorMessage() const;
  static void CheckOatVersion(std::array<uint8_t, 4> version);
  const char* GetMagic() const;
  uint32_t GetChecksum() const;
  void SetChecksum(uint32_t checksum);
  uint32_t GetDexFileCount() const {
    DCHECK(IsValid());
    return dex_file_count_;
  }
  uint32_t GetOatDexFilesOffset() const;
  void SetOatDexFilesOffset(uint32_t oat_dex_files_offset);
  uint32_t GetExecutableOffset() const;
  void SetExecutableOffset(uint32_t executable_offset);

  const void* GetJniDlsymLookupTrampoline() const;
  uint32_t GetJniDlsymLookupTrampolineOffset() const;
  void SetJniDlsymLookupTrampolineOffset(uint32_t offset);
  const void* GetJniDlsymLookupCriticalTrampoline() const;
  uint32_t GetJniDlsymLookupCriticalTrampolineOffset() const;
  void SetJniDlsymLookupCriticalTrampolineOffset(uint32_t offset);

  const void* GetQuickGenericJniTrampoline() const;
  uint32_t GetQuickGenericJniTrampolineOffset() const;
  void SetQuickGenericJniTrampolineOffset(uint32_t offset);
  const void* GetQuickResolutionTrampoline() const;
  uint32_t GetQuickResolutionTrampolineOffset() const;
  void SetQuickResolutionTrampolineOffset(uint32_t offset);
  const void* GetQuickImtConflictTrampoline() const;
  uint32_t GetQuickImtConflictTrampolineOffset() const;
  void SetQuickImtConflictTrampolineOffset(uint32_t offset);
  const void* GetQuickToInterpreterBridge() const;
  uint32_t GetQuickToInterpreterBridgeOffset() const;
  void SetQuickToInterpreterBridgeOffset(uint32_t offset);
  const void* GetNterpTrampoline() const;
  uint32_t GetNterpTrampolineOffset() const;
  void SetNterpTrampolineOffset(uint32_t offset);

  InstructionSet GetInstructionSet() const;
  uint32_t GetInstructionSetFeaturesBitmap() const;

  uint32_t GetKeyValueStoreSize() const;
  const uint8_t* GetKeyValueStore() const;
  const char* GetStoreValueByKey(const char* key) const;
  bool GetStoreKeyValuePairByIndex(size_t index, const char** key, const char** value) const;

  size_t GetHeaderSize() const;
  bool IsDebuggable() const;
  bool IsNativeDebuggable() const;
  CompilerFilter::Filter GetCompilerFilter() const;
  bool IsConcurrentCopying() const;
  bool RequiresImage() const;

 private:
  bool KeyHasValue(const char* key, const char* value, size_t value_size) const;

  OatHeader(InstructionSet instruction_set,
            const InstructionSetFeatures* instruction_set_features,
            uint32_t dex_file_count,
            const SafeMap<std::string, std::string>* variable_data);

  // Returns true if the value of the given key is "true", false otherwise.
  bool IsKeyEnabled(const char* key) const;

  void Flatten(const SafeMap<std::string, std::string>* variable_data);

  std::array<uint8_t, 4> magic_;
  std::array<uint8_t, 4> version_;
  uint32_t oat_checksum_;

  InstructionSet instruction_set_;
  uint32_t instruction_set_features_bitmap_;
  uint32_t dex_file_count_;
  uint32_t oat_dex_files_offset_;
  uint32_t executable_offset_;
  uint32_t jni_dlsym_lookup_trampoline_offset_;
  uint32_t jni_dlsym_lookup_critical_trampoline_offset_;
  uint32_t quick_generic_jni_trampoline_offset_;
  uint32_t quick_imt_conflict_trampoline_offset_;
  uint32_t quick_resolution_trampoline_offset_;
  uint32_t quick_to_interpreter_bridge_offset_;
  uint32_t nterp_trampoline_offset_;

  uint32_t key_value_store_size_;
  uint8_t key_value_store_[0];  // note variable width data at end

  DISALLOW_COPY_AND_ASSIGN(OatHeader);
};

}  // namespace art

#endif  // ART_RUNTIME_OAT_H_
