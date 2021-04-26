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

#include <gtest/gtest.h>
#include <sstream>
#include <string>

#include "android-base/file.h"
#include "android-base/strings.h"
#include "art_method-inl.h"
#include "base/globals.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "common_runtime_test.h"
#include "dex/descriptors_names.h"
#include "dex/dex_file_structs.h"
#include "dex/dex_instruction-inl.h"
#include "dex/dex_instruction_iterator.h"
#include "dex/type_reference.h"
#include "exec_utils.h"
#include "linear_alloc.h"
#include "mirror/class-inl.h"
#include "obj_ptr-inl.h"
#include "profile/profile_compilation_info.h"
#include "profile/profile_test_helper.h"
#include "profile_assistant.h"
#include "scoped_thread_state_change-inl.h"

namespace art {

using TypeReferenceSet = std::set<TypeReference, TypeReferenceValueComparator>;

// TODO(calin): These tests share a lot with the ProfileCompilationInfo tests.
// we should introduce a better abstraction to extract the common parts.
class ProfileAssistantTest : public CommonRuntimeTest, public ProfileTestHelper {
 public:
  void PostRuntimeCreate() override {
    allocator_.reset(new ArenaAllocator(Runtime::Current()->GetArenaPool()));

    dex1 = BuildDex("location1", /* checksum= */ 1, "LUnique1;", /* num_method_ids= */ 10001);
    dex2 = BuildDex("location2", /* checksum= */ 2, "LUnique2;", /* num_method_ids= */ 10002);
    dex3 = BuildDex("location3", /* checksum= */ 3, "LUnique3;", /* num_method_ids= */ 10003);
    dex4 = BuildDex("location4", /* checksum= */ 4, "LUnique4;", /* num_method_ids= */ 10004);

    dex1_checksum_missmatch =
        BuildDex("location1", /* checksum= */ 12, "LUnique1;", /* num_method_ids= */ 10001);
  }

 protected:
  void SetupProfile(const DexFile* dex_file1,
                    const DexFile* dex_file2,
                    uint16_t number_of_methods,
                    uint16_t number_of_classes,
                    const ScratchFile& profile,
                    ProfileCompilationInfo* info,
                    uint16_t start_method_index = 0,
                    bool reverse_dex_write_order = false) {
    for (uint16_t i = start_method_index; i < start_method_index + number_of_methods; i++) {
      // reverse_dex_write_order controls the order in which the dex files will be added to
      // the profile and thus written to disk.
      std::vector<ProfileInlineCache> inline_caches =
          GetTestInlineCaches(dex_file1, dex_file2, dex3);
      Hotness::Flag flags =
          static_cast<Hotness::Flag>(Hotness::kFlagHot | Hotness::kFlagPostStartup);
      if (reverse_dex_write_order) {
        ASSERT_TRUE(AddMethod(info, dex_file2, i, inline_caches, flags));
        ASSERT_TRUE(AddMethod(info, dex_file1, i, inline_caches, flags));
      } else {
        ASSERT_TRUE(AddMethod(info, dex_file1, i, inline_caches, flags));
        ASSERT_TRUE(AddMethod(info, dex_file2, i, inline_caches, flags));
      }
    }
    for (uint16_t i = 0; i < number_of_classes; i++) {
      ASSERT_TRUE(AddClass(info, dex_file1, dex::TypeIndex(i)));
    }

    ASSERT_TRUE(info->Save(GetFd(profile)));
    ASSERT_EQ(0, profile.GetFile()->Flush());
  }

  void SetupBasicProfile(const DexFile* dex,
                         const std::vector<uint32_t>& hot_methods,
                         const std::vector<uint32_t>& startup_methods,
                         const std::vector<uint32_t>& post_startup_methods,
                         const ScratchFile& profile,
                         ProfileCompilationInfo* info) {
    for (uint32_t idx : hot_methods) {
      AddMethod(info, dex, idx, Hotness::kFlagHot);
    }
    for (uint32_t idx : startup_methods) {
      AddMethod(info, dex, idx, Hotness::kFlagStartup);
    }
    for (uint32_t idx : post_startup_methods) {
      AddMethod(info, dex, idx, Hotness::kFlagPostStartup);
    }
    ASSERT_TRUE(info->Save(GetFd(profile)));
    ASSERT_EQ(0, profile.GetFile()->Flush());
  }

  // The dex1_substitute can be used to replace the default dex1 file.
  std::vector<ProfileInlineCache> GetTestInlineCaches(
        const DexFile* dex_file1, const DexFile* dex_file2, const DexFile* dex_file3) {
    std::vector<ProfileInlineCache> inline_caches;
    // Monomorphic
    for (uint16_t dex_pc = 0; dex_pc < 11; dex_pc++) {
      std::vector<TypeReference> types = {TypeReference(dex_file1, dex::TypeIndex(0))};
      inline_caches.push_back(ProfileInlineCache(dex_pc, /* missing_types*/ false, types));
    }
    // Polymorphic
    for (uint16_t dex_pc = 11; dex_pc < 22; dex_pc++) {
      std::vector<TypeReference> types = {
          TypeReference(dex_file1, dex::TypeIndex(0)),
          TypeReference(dex_file2, dex::TypeIndex(1)),
          TypeReference(dex_file3, dex::TypeIndex(2))};
      inline_caches.push_back(ProfileInlineCache(dex_pc, /* missing_types*/ false, types));
    }
    // Megamorphic
    for (uint16_t dex_pc = 22; dex_pc < 33; dex_pc++) {
      // we need 5 types to make the cache megamorphic
      std::vector<TypeReference> types = {
          TypeReference(dex_file1, dex::TypeIndex(0)),
          TypeReference(dex_file1, dex::TypeIndex(1)),
          TypeReference(dex_file1, dex::TypeIndex(2)),
          TypeReference(dex_file1, dex::TypeIndex(3)),
          TypeReference(dex_file1, dex::TypeIndex(4))};
      inline_caches.push_back(ProfileInlineCache(dex_pc, /* missing_types*/ false, types));
    }
    // Missing types
    for (uint16_t dex_pc = 33; dex_pc < 44; dex_pc++) {
      std::vector<TypeReference> types;
      inline_caches.push_back(ProfileInlineCache(dex_pc, /* missing_types*/ true, types));
    }

    return inline_caches;
  }

  int GetFd(const ScratchFile& file) const {
    return static_cast<int>(file.GetFd());
  }

  void CheckProfileInfo(ScratchFile& file, const ProfileCompilationInfo& info) {
    ProfileCompilationInfo file_info;
    ASSERT_TRUE(file_info.Load(GetFd(file)));
    ASSERT_TRUE(file_info.Equals(info));
  }

  std::string GetProfmanCmd() {
    std::string file_path = GetArtBinDir() + "/profman";
    if (kIsDebugBuild) {
      file_path += "d";
    }
    EXPECT_TRUE(OS::FileExists(file_path.c_str())) << file_path << " should be a valid file path";
    return file_path;
  }

  // Runs test with given arguments.
  int ProcessProfiles(
      const std::vector<int>& profiles_fd,
      int reference_profile_fd,
      const std::vector<const std::string>& extra_args = std::vector<const std::string>()) {
    std::string profman_cmd = GetProfmanCmd();
    std::vector<std::string> argv_str;
    argv_str.push_back(profman_cmd);
    for (size_t k = 0; k < profiles_fd.size(); k++) {
      argv_str.push_back("--profile-file-fd=" + std::to_string(profiles_fd[k]));
    }
    argv_str.push_back("--reference-profile-file-fd=" + std::to_string(reference_profile_fd));
    argv_str.insert(argv_str.end(), extra_args.begin(), extra_args.end());

    std::string error;
    return ExecAndReturnCode(argv_str, &error);
  }

  bool GenerateTestProfile(const std::string& filename) {
    std::string profman_cmd = GetProfmanCmd();
    std::vector<std::string> argv_str;
    argv_str.push_back(profman_cmd);
    argv_str.push_back("--generate-test-profile=" + filename);
    std::string error;
    return ExecAndReturnCode(argv_str, &error);
  }

  bool GenerateTestProfileWithInputDex(const std::string& filename) {
    std::string profman_cmd = GetProfmanCmd();
    std::vector<std::string> argv_str;
    argv_str.push_back(profman_cmd);
    argv_str.push_back("--generate-test-profile=" + filename);
    argv_str.push_back("--generate-test-profile-seed=0");
    argv_str.push_back("--apk=" + GetLibCoreDexFileNames()[0]);
    argv_str.push_back("--dex-location=" + GetLibCoreDexFileNames()[0]);
    std::string error;
    return ExecAndReturnCode(argv_str, &error);
  }

  bool CreateProfile(const std::string& profile_file_contents,
                     const std::string& filename,
                     const std::string& dex_location) {
    ScratchFile class_names_file;
    File* file = class_names_file.GetFile();
    EXPECT_TRUE(file->WriteFully(profile_file_contents.c_str(), profile_file_contents.length()));
    EXPECT_EQ(0, file->Flush());
    std::string profman_cmd = GetProfmanCmd();
    std::vector<std::string> argv_str;
    argv_str.push_back(profman_cmd);
    argv_str.push_back("--create-profile-from=" + class_names_file.GetFilename());
    argv_str.push_back("--reference-profile-file=" + filename);
    argv_str.push_back("--apk=" + dex_location);
    argv_str.push_back("--dex-location=" + dex_location);
    std::string error;
    EXPECT_EQ(ExecAndReturnCode(argv_str, &error), 0) << error;
    return true;
  }

  bool RunProfman(const std::string& filename,
                  std::vector<std::string>& extra_args,
                  std::string* output,
                  std::string_view target_apk) {
    ScratchFile output_file;
    std::string profman_cmd = GetProfmanCmd();
    std::vector<std::string> argv_str;
    argv_str.push_back(profman_cmd);
    argv_str.insert(argv_str.end(), extra_args.begin(), extra_args.end());
    argv_str.push_back("--profile-file=" + filename);
    argv_str.push_back(std::string("--apk=").append(target_apk));
    argv_str.push_back(std::string("--dex-location=").append(target_apk));
    argv_str.push_back("--dump-output-to-fd=" + std::to_string(GetFd(output_file)));
    std::string error;
    EXPECT_EQ(ExecAndReturnCode(argv_str, &error), 0) << error;
    File* file = output_file.GetFile();
    EXPECT_EQ(0, file->Flush());
    int64_t length = file->GetLength();
    std::unique_ptr<char[]> buf(new char[length]);
    EXPECT_EQ(file->Read(buf.get(), length, 0), length);
    *output = std::string(buf.get(), length);
    return true;
  }

  bool DumpClassesAndMethods(const std::string& filename,
                             std::string* file_contents,
                             std::optional<const std::string_view> target = std::nullopt) {
    std::vector<std::string> extra_args;
    extra_args.push_back("--dump-classes-and-methods");
    return RunProfman(
        filename, extra_args, file_contents, target.value_or(GetLibCoreDexFileNames()[0]));
  }

  bool DumpOnly(const std::string& filename, std::string* file_contents) {
    std::vector<std::string> extra_args;
    extra_args.push_back("--dump-only");
    return RunProfman(filename, extra_args, file_contents, GetLibCoreDexFileNames()[0]);
  }

  bool CreateAndDump(const std::string& input_file_contents,
                     std::string* output_file_contents,
                     std::optional<const std::string> target = std::nullopt) {
    ScratchFile profile_file;
    EXPECT_TRUE(CreateProfile(input_file_contents,
                              profile_file.GetFilename(),
                              target.value_or(GetLibCoreDexFileNames()[0])));
    EXPECT_TRUE(DumpClassesAndMethods(profile_file.GetFilename(), output_file_contents, target));
    return true;
  }

  ObjPtr<mirror::Class> GetClass(ScopedObjectAccess& soa,
                                 jobject class_loader,
                                 const std::string& clazz) REQUIRES_SHARED(Locks::mutator_lock_) {
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::ClassLoader> h_loader(hs.NewHandle(
        ObjPtr<mirror::ClassLoader>::DownCast(soa.Self()->DecodeJObject(class_loader))));
    return class_linker->FindClass(soa.Self(), clazz.c_str(), h_loader);
  }

  ArtMethod* GetVirtualMethod(jobject class_loader,
                              const std::string& clazz,
                              const std::string& name) {
    ScopedObjectAccess soa(Thread::Current());
    ObjPtr<mirror::Class> klass = GetClass(soa, class_loader, clazz);
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    const auto pointer_size = class_linker->GetImagePointerSize();
    ArtMethod* method = nullptr;
    for (auto& m : klass->GetVirtualMethods(pointer_size)) {
      if (name == m.GetName()) {
        EXPECT_TRUE(method == nullptr);
        method = &m;
      }
    }
    return method;
  }

  static TypeReference MakeTypeReference(ObjPtr<mirror::Class> klass)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return TypeReference(&klass->GetDexFile(), klass->GetDexTypeIndex());
  }

  // Find the first dex-pc in the given method after 'start_pc' (if given) which
  // contains a call to any method of 'klass'. If 'start_pc' is not given we
  // will search from the first dex-pc.
  uint16_t GetDexPcOfCallTo(ArtMethod* method,
                            Handle<mirror::Class> klass,
                            std::optional<uint32_t> start_pc = std::nullopt)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    const DexFile* dex_file = method->GetDexFile();
    for (const DexInstructionPcPair& inst :
         CodeItemInstructionAccessor(*dex_file, method->GetCodeItem())) {
      if (start_pc && inst.DexPc() <= *start_pc) {
        continue;
      } else if (inst->IsInvoke()) {
        const dex::MethodId& method_id = dex_file->GetMethodId(inst->VRegB());
        std::string_view desc(
            dex_file->GetTypeDescriptor(dex_file->GetTypeId(method_id.class_idx_)));
        std::string scratch;
        if (desc == klass->GetDescriptor(&scratch)) {
          return inst.DexPc();
        }
      }
    }
    EXPECT_TRUE(false) << "Unable to find dex-pc in " << method->PrettyMethod() << " for call to "
                       << klass->PrettyClass()
                       << " after dexpc: " << (start_pc ? static_cast<int64_t>(*start_pc) : -1);
    return -1;
  }

  void AssertInlineCaches(ArtMethod* method,
                          uint16_t dex_pc,
                          const TypeReferenceSet& expected_clases,
                          const ProfileCompilationInfo& info,
                          bool is_megamorphic,
                          bool is_missing_types)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    ProfileCompilationInfo::MethodHotness hotness =
        info.GetMethodHotness(MethodReference(method->GetDexFile(), method->GetDexMethodIndex()));
    ASSERT_TRUE(hotness.IsHot());
    const ProfileCompilationInfo::InlineCacheMap* inline_caches = hotness.GetInlineCacheMap();
    ASSERT_TRUE(inline_caches->find(dex_pc) != inline_caches->end());
    AssertInlineCaches(expected_clases,
                       info,
                       inline_caches->find(dex_pc)->second,
                       is_megamorphic,
                       is_missing_types);
  }
  void AssertInlineCaches(ArtMethod* method,
                          const TypeReferenceSet& expected_clases,
                          const ProfileCompilationInfo& info,
                          bool is_megamorphic,
                          bool is_missing_types)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    ProfileCompilationInfo::MethodHotness hotness =
        info.GetMethodHotness(MethodReference(method->GetDexFile(), method->GetDexMethodIndex()));
    ASSERT_TRUE(hotness.IsHot());
    const ProfileCompilationInfo::InlineCacheMap* inline_caches = hotness.GetInlineCacheMap();
    ASSERT_EQ(inline_caches->size(), 1u);
    AssertInlineCaches(expected_clases,
                       info,
                       inline_caches->begin()->second,
                       is_megamorphic,
                       is_missing_types);
  }

  void AssertInlineCaches(const TypeReferenceSet& expected_clases,
                          const ProfileCompilationInfo& info,
                          const ProfileCompilationInfo::DexPcData& dex_pc_data,
                          bool is_megamorphic,
                          bool is_missing_types)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    ASSERT_EQ(dex_pc_data.is_megamorphic, is_megamorphic);
    ASSERT_EQ(dex_pc_data.is_missing_types, is_missing_types);
    ASSERT_EQ(expected_clases.size(), dex_pc_data.classes.size());
    size_t found = 0;
    for (const TypeReference& type_ref : expected_clases) {
      for (const auto& class_ref : dex_pc_data.classes) {
        if (class_ref.type_index == type_ref.TypeIndex() &&
            ProfileIndexMatchesDexFile(info, class_ref.dex_profile_index, type_ref.dex_file)) {
          found++;
        }
      }
    }

    ASSERT_EQ(expected_clases.size(), found);
  }

  int CheckCompilationMethodPercentChange(uint16_t methods_in_cur_profile,
                                          uint16_t methods_in_ref_profile,
                                          const std::vector<const std::string>& extra_args =
                                              std::vector<const std::string>()) {
    ScratchFile profile;
    ScratchFile reference_profile;
    std::vector<int> profile_fds({ GetFd(profile)});
    int reference_profile_fd = GetFd(reference_profile);
    std::vector<uint32_t> hot_methods_cur;
    std::vector<uint32_t> hot_methods_ref;
    std::vector<uint32_t> empty_vector;
    for (size_t i = 0; i < methods_in_cur_profile; ++i) {
      hot_methods_cur.push_back(i);
    }
    for (size_t i = 0; i < methods_in_ref_profile; ++i) {
      hot_methods_ref.push_back(i);
    }
    ProfileCompilationInfo info1;
    SetupBasicProfile(dex1, hot_methods_cur, empty_vector, empty_vector,
        profile,  &info1);
    ProfileCompilationInfo info2;
    SetupBasicProfile(dex1, hot_methods_ref, empty_vector, empty_vector,
        reference_profile,  &info2);
    return ProcessProfiles(profile_fds, reference_profile_fd, extra_args);
  }

  int CheckCompilationClassPercentChange(uint16_t classes_in_cur_profile,
                                         uint16_t classes_in_ref_profile,
                                         const std::vector<const std::string>& extra_args =
                                             std::vector<const std::string>()) {
    ScratchFile profile;
    ScratchFile reference_profile;

    std::vector<int> profile_fds({ GetFd(profile)});
    int reference_profile_fd = GetFd(reference_profile);

    ProfileCompilationInfo info1;
    SetupProfile(dex1, dex2, 0, classes_in_cur_profile, profile,  &info1);
    ProfileCompilationInfo info2;
    SetupProfile(dex1, dex2, 0, classes_in_ref_profile, reference_profile, &info2);
    return ProcessProfiles(profile_fds, reference_profile_fd, extra_args);
  }

  std::unique_ptr<ArenaAllocator> allocator_;

  const DexFile* dex1;
  const DexFile* dex2;
  const DexFile* dex3;
  const DexFile* dex4;
  const DexFile* dex1_checksum_missmatch;
};

TEST_F(ProfileAssistantTest, AdviseCompilationEmptyReferences) {
  ScratchFile profile1;
  ScratchFile profile2;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1),
      GetFd(profile2)});
  int reference_profile_fd = GetFd(reference_profile);

  const uint16_t kNumberOfMethodsToEnableCompilation = 100;
  ProfileCompilationInfo info1;
  SetupProfile(dex1, dex2, kNumberOfMethodsToEnableCompilation, 0, profile1, &info1);
  ProfileCompilationInfo info2;
  SetupProfile(dex3, dex4, kNumberOfMethodsToEnableCompilation, 0, profile2, &info2);

  // We should advise compilation.
  ASSERT_EQ(ProfileAssistant::kCompile,
            ProcessProfiles(profile_fds, reference_profile_fd));
  // The resulting compilation info must be equal to the merge of the inputs.
  ProfileCompilationInfo result;
  ASSERT_TRUE(result.Load(reference_profile_fd));

  ProfileCompilationInfo expected;
  ASSERT_TRUE(expected.MergeWith(info1));
  ASSERT_TRUE(expected.MergeWith(info2));
  ASSERT_TRUE(expected.Equals(result));

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
  CheckProfileInfo(profile2, info2);
}

// TODO(calin): Add more tests for classes.
TEST_F(ProfileAssistantTest, AdviseCompilationEmptyReferencesBecauseOfClasses) {
  ScratchFile profile1;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1)});
  int reference_profile_fd = GetFd(reference_profile);

  const uint16_t kNumberOfClassesToEnableCompilation = 100;
  ProfileCompilationInfo info1;
  SetupProfile(dex1, dex2, 0, kNumberOfClassesToEnableCompilation, profile1, &info1);

  // We should advise compilation.
  ASSERT_EQ(ProfileAssistant::kCompile,
            ProcessProfiles(profile_fds, reference_profile_fd));
  // The resulting compilation info must be equal to the merge of the inputs.
  ProfileCompilationInfo result;
  ASSERT_TRUE(result.Load(reference_profile_fd));

  ProfileCompilationInfo expected;
  ASSERT_TRUE(expected.MergeWith(info1));
  ASSERT_TRUE(expected.Equals(result));

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
}

TEST_F(ProfileAssistantTest, AdviseCompilationNonEmptyReferences) {
  ScratchFile profile1;
  ScratchFile profile2;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1),
      GetFd(profile2)});
  int reference_profile_fd = GetFd(reference_profile);

  // The new profile info will contain the methods with indices 0-100.
  const uint16_t kNumberOfMethodsToEnableCompilation = 100;
  ProfileCompilationInfo info1;
  SetupProfile(dex1, dex2, kNumberOfMethodsToEnableCompilation, 0, profile1, &info1);
  ProfileCompilationInfo info2;
  SetupProfile(dex3, dex4, kNumberOfMethodsToEnableCompilation, 0, profile2, &info2);


  // The reference profile info will contain the methods with indices 50-150.
  const uint16_t kNumberOfMethodsAlreadyCompiled = 100;
  ProfileCompilationInfo reference_info;
  SetupProfile(dex1, dex2, kNumberOfMethodsAlreadyCompiled, 0, reference_profile,
      &reference_info, kNumberOfMethodsToEnableCompilation / 2);

  // We should advise compilation.
  ASSERT_EQ(ProfileAssistant::kCompile,
            ProcessProfiles(profile_fds, reference_profile_fd));

  // The resulting compilation info must be equal to the merge of the inputs
  ProfileCompilationInfo result;
  ASSERT_TRUE(result.Load(reference_profile_fd));

  ProfileCompilationInfo expected;
  ASSERT_TRUE(expected.MergeWith(info1));
  ASSERT_TRUE(expected.MergeWith(info2));
  ASSERT_TRUE(expected.MergeWith(reference_info));
  ASSERT_TRUE(expected.Equals(result));

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
  CheckProfileInfo(profile2, info2);
}

TEST_F(ProfileAssistantTest, DoNotAdviseCompilation) {
  ScratchFile profile1;
  ScratchFile profile2;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1),
      GetFd(profile2)});
  int reference_profile_fd = GetFd(reference_profile);

  const uint16_t kNumberOfMethodsToSkipCompilation = 24;  // Threshold is 100.
  ProfileCompilationInfo info1;
  SetupProfile(dex1, dex2, kNumberOfMethodsToSkipCompilation, 0, profile1, &info1);
  ProfileCompilationInfo info2;
  SetupProfile(dex3, dex4, kNumberOfMethodsToSkipCompilation, 0, profile2, &info2);

  // We should not advise compilation.
  ASSERT_EQ(ProfileAssistant::kSkipCompilation,
            ProcessProfiles(profile_fds, reference_profile_fd));

  // The information from profiles must remain the same.
  ProfileCompilationInfo file_info1;
  ASSERT_TRUE(file_info1.Load(GetFd(profile1)));
  ASSERT_TRUE(file_info1.Equals(info1));

  ProfileCompilationInfo file_info2;
  ASSERT_TRUE(file_info2.Load(GetFd(profile2)));
  ASSERT_TRUE(file_info2.Equals(info2));

  // Reference profile files must remain empty.
  ASSERT_EQ(0, reference_profile.GetFile()->GetLength());

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
  CheckProfileInfo(profile2, info2);
}

TEST_F(ProfileAssistantTest, DoNotAdviseCompilationMethodPercentage) {
  const uint16_t kNumberOfMethodsInRefProfile = 6000;
  const uint16_t kNumberOfMethodsInCurProfile = 6100;  // Threshold is 2%.
  std::vector<const std::string> extra_args({"--min-new-methods-percent-change=2"});

  // We should not advise compilation.
  ASSERT_EQ(ProfileAssistant::kSkipCompilation,
            CheckCompilationMethodPercentChange(kNumberOfMethodsInCurProfile,
                                                kNumberOfMethodsInRefProfile,
                                                extra_args));
}

TEST_F(ProfileAssistantTest, ShouldAdviseCompilationMethodPercentage) {
  const uint16_t kNumberOfMethodsInRefProfile = 6000;
  const uint16_t kNumberOfMethodsInCurProfile = 6200;  // Threshold is 2%.
  std::vector<const std::string> extra_args({"--min-new-methods-percent-change=2"});

  // We should advise compilation.
  ASSERT_EQ(ProfileAssistant::kCompile,
            CheckCompilationMethodPercentChange(kNumberOfMethodsInCurProfile,
                                                kNumberOfMethodsInRefProfile,
                                                extra_args));
}

TEST_F(ProfileAssistantTest, DoNotAdviseCompilationMethodPercentageWithNewMin) {
  const uint16_t kNumberOfMethodsInRefProfile = 6000;
  const uint16_t kNumberOfMethodsInCurProfile = 6200;  // Threshold is 20%.

  // We should not advise compilation.
  ASSERT_EQ(ProfileAssistant::kSkipCompilation,
            CheckCompilationMethodPercentChange(kNumberOfMethodsInCurProfile,
                                                kNumberOfMethodsInRefProfile));
}

TEST_F(ProfileAssistantTest, DoNotdviseCompilationClassPercentage) {
  const uint16_t kNumberOfClassesInRefProfile = 6000;
  const uint16_t kNumberOfClassesInCurProfile = 6110;  // Threshold is 2%.
  std::vector<const std::string> extra_args({"--min-new-classes-percent-change=2"});

  // We should not advise compilation.
  ASSERT_EQ(ProfileAssistant::kSkipCompilation,
            CheckCompilationClassPercentChange(kNumberOfClassesInCurProfile,
                                               kNumberOfClassesInRefProfile,
                                               extra_args));
}

TEST_F(ProfileAssistantTest, ShouldAdviseCompilationClassPercentage) {
  const uint16_t kNumberOfClassesInRefProfile = 6000;
  const uint16_t kNumberOfClassesInCurProfile = 6120;  // Threshold is 2%.
  std::vector<const std::string> extra_args({"--min-new-classes-percent-change=2"});

  // We should advise compilation.
  ASSERT_EQ(ProfileAssistant::kCompile,
            CheckCompilationClassPercentChange(kNumberOfClassesInCurProfile,
                                               kNumberOfClassesInRefProfile,
                                               extra_args));
}

TEST_F(ProfileAssistantTest, DoNotAdviseCompilationClassPercentageWithNewMin) {
  const uint16_t kNumberOfClassesInRefProfile = 6000;
  const uint16_t kNumberOfClassesInCurProfile = 6200;  // Threshold is 20%.

  // We should not advise compilation.
  ASSERT_EQ(ProfileAssistant::kSkipCompilation,
            CheckCompilationClassPercentChange(kNumberOfClassesInCurProfile,
                                               kNumberOfClassesInRefProfile));
}

TEST_F(ProfileAssistantTest, FailProcessingBecauseOfProfiles) {
  ScratchFile profile1;
  ScratchFile profile2;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1),
      GetFd(profile2)});
  int reference_profile_fd = GetFd(reference_profile);

  const uint16_t kNumberOfMethodsToEnableCompilation = 100;
  // Assign different hashes for the same dex file. This will make merging of information to fail.
  ProfileCompilationInfo info1;
  SetupProfile(dex1, dex2, kNumberOfMethodsToEnableCompilation, 0, profile1, &info1);
  ProfileCompilationInfo info2;
  SetupProfile(
      dex1_checksum_missmatch, dex2, kNumberOfMethodsToEnableCompilation, 0, profile2, &info2);

  // We should fail processing.
  ASSERT_EQ(ProfileAssistant::kErrorBadProfiles,
            ProcessProfiles(profile_fds, reference_profile_fd));

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
  CheckProfileInfo(profile2, info2);

  // Reference profile files must still remain empty.
  ASSERT_EQ(0, reference_profile.GetFile()->GetLength());
}

TEST_F(ProfileAssistantTest, FailProcessingBecauseOfReferenceProfiles) {
  ScratchFile profile1;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1)});
  int reference_profile_fd = GetFd(reference_profile);

  const uint16_t kNumberOfMethodsToEnableCompilation = 100;
  // Assign different hashes for the same dex file. This will make merging of information to fail.
  ProfileCompilationInfo info1;
  SetupProfile(dex1, dex2, kNumberOfMethodsToEnableCompilation, 0, profile1, &info1);
  ProfileCompilationInfo reference_info;
  SetupProfile(dex1_checksum_missmatch,
               dex2,
               kNumberOfMethodsToEnableCompilation,
               0,
               reference_profile,
               &reference_info);

  // We should not advise compilation.
  ASSERT_EQ(ProfileAssistant::kErrorBadProfiles,
            ProcessProfiles(profile_fds, reference_profile_fd));

  // The information from profiles must remain the same.
  CheckProfileInfo(profile1, info1);
}

TEST_F(ProfileAssistantTest, TestProfileGeneration) {
  ScratchFile profile;
  // Generate a test profile.
  GenerateTestProfile(profile.GetFilename());

  // Verify that the generated profile is valid and can be loaded.
  ProfileCompilationInfo info;
  ASSERT_TRUE(info.Load(GetFd(profile)));
}

TEST_F(ProfileAssistantTest, TestProfileGenerationWithIndexDex) {
  ScratchFile profile;
  // Generate a test profile passing in a dex file as reference.
  GenerateTestProfileWithInputDex(profile.GetFilename());

  // Verify that the generated profile is valid and can be loaded.
  ProfileCompilationInfo info;
  ASSERT_TRUE(info.Load(GetFd(profile)));
}

TEST_F(ProfileAssistantTest, TestProfileCreationAllMatch) {
  // Class names put here need to be in sorted order.
  std::vector<std::string> class_names = {
    "HLjava/lang/Object;-><init>()V",
    "Ljava/lang/Comparable;",
    "Ljava/lang/Math;",
    "Ljava/lang/Object;",
    "SPLjava/lang/Comparable;->compareTo(Ljava/lang/Object;)I",
  };
  std::string file_contents;
  for (std::string& class_name : class_names) {
    file_contents += class_name + std::string("\n");
  }
  std::string output_file_contents;
  ASSERT_TRUE(CreateAndDump(file_contents, &output_file_contents));
  ASSERT_EQ(output_file_contents, file_contents);
}

TEST_F(ProfileAssistantTest, TestArrayClass) {
  std::vector<std::string> class_names = {
    "[Ljava/lang/Comparable;",
  };
  std::string file_contents;
  for (std::string& class_name : class_names) {
    file_contents += class_name + std::string("\n");
  }
  std::string output_file_contents;
  ASSERT_TRUE(CreateAndDump(file_contents, &output_file_contents));
  ASSERT_EQ(output_file_contents, file_contents);
}

TEST_F(ProfileAssistantTest, TestProfileCreationGenerateMethods) {
  // Class names put here need to be in sorted order.
  std::vector<std::string> class_names = {
    "HLjava/lang/Math;->*",
  };
  std::string input_file_contents;
  std::string expected_contents;
  for (std::string& class_name : class_names) {
    input_file_contents += class_name + std::string("\n");
    expected_contents += DescriptorToDot(class_name.c_str()) +
        std::string("\n");
  }
  std::string output_file_contents;
  ScratchFile profile_file;
  EXPECT_TRUE(CreateProfile(input_file_contents,
                            profile_file.GetFilename(),
                            GetLibCoreDexFileNames()[0]));
  ProfileCompilationInfo info;
  ASSERT_TRUE(info.Load(GetFd(profile_file)));
  // Verify that the profile has matching methods.
  ScopedObjectAccess soa(Thread::Current());
  ObjPtr<mirror::Class> klass = GetClass(soa, /* class_loader= */ nullptr, "Ljava/lang/Math;");
  ASSERT_TRUE(klass != nullptr);
  size_t method_count = 0;
  for (ArtMethod& method : klass->GetMethods(kRuntimePointerSize)) {
    if (!method.IsCopied() && method.GetCodeItem() != nullptr) {
      ++method_count;
      ProfileCompilationInfo::MethodHotness hotness =
          info.GetMethodHotness(MethodReference(method.GetDexFile(), method.GetDexMethodIndex()));
      ASSERT_TRUE(hotness.IsHot()) << method.PrettyMethod();
    }
  }
  EXPECT_GT(method_count, 0u);
}

static std::string JoinProfileLines(const std::vector<std::string>& lines) {
  std::string result = android::base::Join(lines, '\n');
  return result + '\n';
}

TEST_F(ProfileAssistantTest, TestBootImageProfile) {
  const std::string core_dex = GetLibCoreDexFileNames()[0];

  std::vector<ScratchFile> profiles;

  // In image with enough clean occurrences.
  const std::string kCleanClass = "Ljava/lang/CharSequence;";
  // In image with enough dirty occurrences.
  const std::string kDirtyClass = "Ljava/lang/Object;";
  // Not in image becauseof not enough occurrences.
  const std::string kUncommonCleanClass = "Ljava/lang/Process;";
  const std::string kUncommonDirtyClass = "Ljava/lang/Package;";
  // Method that is common and hot. Should end up in profile.
  const std::string kCommonHotMethod = "Ljava/lang/Comparable;->compareTo(Ljava/lang/Object;)I";
  // Uncommon method, should not end up in profile
  const std::string kUncommonMethod = "Ljava/util/HashMap;-><init>()V";
  // Method that gets marked as hot since it's in multiple profile and marked as startup.
  const std::string kStartupMethodForUpgrade = "Ljava/util/ArrayList;->clear()V";
  // Startup method used by a special package which will get a different threshold;
  const std::string kSpecialPackageStartupMethod =
      "Ljava/lang/Object;->toString()Ljava/lang/String;";
  // Method used by a special package which will get a different threshold;
  const std::string kUncommonSpecialPackageMethod = "Ljava/lang/Object;->hashCode()I";
  // Denylisted class
  const std::string kPreloadedDenylistedClass = "Ljava/lang/Thread;";

  // Thresholds for this test.
  static const size_t kDirtyThreshold = 100;
  static const size_t kCleanThreshold = 50;
  static const size_t kPreloadedThreshold = 100;
  static const size_t kMethodThreshold = 75;
  static const size_t kSpecialThreshold = 50;
  const std::string kSpecialPackage = "dex4";

  // Create boot profile content, attributing the classes and methods to different dex files.
  std::vector<std::string> input_data = {
      "{dex1}" + kCleanClass,
      "{dex1}" + kDirtyClass,
      "{dex1}" + kUncommonCleanClass,
      "{dex1}H" + kCommonHotMethod,
      "{dex1}P" + kStartupMethodForUpgrade,
      "{dex1}" + kUncommonDirtyClass,
      "{dex1}" + kPreloadedDenylistedClass,

      "{dex2}" + kCleanClass,
      "{dex2}" + kDirtyClass,
      "{dex2}P" + kCommonHotMethod,
      "{dex2}P" + kStartupMethodForUpgrade,
      "{dex2}" + kUncommonDirtyClass,
      "{dex2}" + kPreloadedDenylistedClass,

      "{dex3}P" + kUncommonMethod,
      "{dex3}PS" + kStartupMethodForUpgrade,
      "{dex3}S" + kCommonHotMethod,
      "{dex3}S" + kSpecialPackageStartupMethod,
      "{dex3}" + kDirtyClass,
      "{dex3}" + kPreloadedDenylistedClass,

      "{dex4}" + kDirtyClass,
      "{dex4}P" + kCommonHotMethod,
      "{dex4}S" + kSpecialPackageStartupMethod,
      "{dex4}P" + kUncommonSpecialPackageMethod,
      "{dex4}" + kPreloadedDenylistedClass,
  };
  std::string input_file_contents = JoinProfileLines(input_data);

  ScratchFile preloaded_class_denylist;
  std::string denylist_content = DescriptorToDot(kPreloadedDenylistedClass.c_str());
  EXPECT_TRUE(preloaded_class_denylist.GetFile()->WriteFully(
      denylist_content.c_str(), denylist_content.length()));

  EXPECT_EQ(0, preloaded_class_denylist.GetFile()->Flush());
  // Expected data
  std::vector<std::string> expected_data = {
      kCleanClass,
      kDirtyClass,
      kPreloadedDenylistedClass,
      "HSP" + kCommonHotMethod,
      "HS" + kSpecialPackageStartupMethod,
      "HSP" + kStartupMethodForUpgrade
  };
  std::string expected_profile_content = JoinProfileLines(expected_data);

  std::vector<std::string> expected_preloaded_data = {
       DescriptorToDot(kDirtyClass.c_str())
  };
  std::string expected_preloaded_content = JoinProfileLines(expected_preloaded_data);

  ScratchFile profile;
  EXPECT_TRUE(CreateProfile(input_file_contents, profile.GetFilename(), core_dex));

  ProfileCompilationInfo bootProfile;
  bootProfile.Load(profile.GetFilename(), /*for_boot_image*/ true);

  // Generate the boot profile.
  ScratchFile out_profile;
  ScratchFile out_preloaded_classes;
  std::vector<std::string> args;
  args.push_back(GetProfmanCmd());
  args.push_back("--generate-boot-image-profile");
  args.push_back("--class-threshold=" + std::to_string(kDirtyThreshold));
  args.push_back("--clean-class-threshold=" + std::to_string(kCleanThreshold));
  args.push_back("--method-threshold=" + std::to_string(kMethodThreshold));
  args.push_back("--preloaded-class-threshold=" + std::to_string(kPreloadedThreshold));
  args.push_back(
      "--special-package=" + kSpecialPackage + ":" + std::to_string(kSpecialThreshold));
  args.push_back("--profile-file=" + profile.GetFilename());
  args.push_back("--out-profile-path=" + out_profile.GetFilename());
  args.push_back("--out-preloaded-classes-path=" + out_preloaded_classes.GetFilename());
  args.push_back("--apk=" + core_dex);
  args.push_back("--dex-location=" + core_dex);
  args.push_back("--preloaded-classes-denylist=" + preloaded_class_denylist.GetFilename());

  std::string error;
  ASSERT_EQ(ExecAndReturnCode(args, &error), 0) << error;

  // Verify the boot profile contents.
  std::string output_profile_contents;
  ASSERT_TRUE(android::base::ReadFileToString(
      out_profile.GetFilename(), &output_profile_contents));
  ASSERT_EQ(output_profile_contents, expected_profile_content);

    // Verify the preloaded classes content.
  std::string output_preloaded_contents;
  ASSERT_TRUE(android::base::ReadFileToString(
      out_preloaded_classes.GetFilename(), &output_preloaded_contents));
  ASSERT_EQ(output_preloaded_contents, expected_preloaded_content);
}

TEST_F(ProfileAssistantTest, TestBootImageProfileWith2RawProfiles) {
  const std::string core_dex = GetLibCoreDexFileNames()[0];

  std::vector<ScratchFile> profiles;

  const std::string kCommonClassUsedByDex1 = "Ljava/lang/CharSequence;";
  const std::string kCommonClassUsedByDex1Dex2 = "Ljava/lang/Object;";
  const std::string kUncommonClass = "Ljava/lang/Process;";
  const std::string kCommonHotMethodUsedByDex1 =
      "Ljava/lang/Comparable;->compareTo(Ljava/lang/Object;)I";
  const std::string kCommonHotMethodUsedByDex1Dex2 = "Ljava/lang/Object;->hashCode()I";
  const std::string kUncommonHotMethod = "Ljava/util/HashMap;-><init>()V";


  // Thresholds for this test.
  static const size_t kDirtyThreshold = 100;
  static const size_t kCleanThreshold = 100;
  static const size_t kMethodThreshold = 100;

    // Create boot profile content, attributing the classes and methods to different dex files.
  std::vector<std::string> input_data1 = {
      "{dex1}" + kCommonClassUsedByDex1,
      "{dex1}" + kCommonClassUsedByDex1Dex2,
      "{dex1}" + kUncommonClass,
      "{dex1}H" + kCommonHotMethodUsedByDex1Dex2,
      "{dex1}" + kCommonHotMethodUsedByDex1,
  };
  std::vector<std::string> input_data2 = {
      "{dex1}" + kCommonClassUsedByDex1,
      "{dex2}" + kCommonClassUsedByDex1Dex2,
      "{dex1}H" + kCommonHotMethodUsedByDex1,
      "{dex2}" + kCommonHotMethodUsedByDex1Dex2,
      "{dex1}" + kUncommonHotMethod,
  };
  std::string input_file_contents1 = JoinProfileLines(input_data1);
  std::string input_file_contents2 = JoinProfileLines(input_data2);

  // Expected data
  std::vector<std::string> expected_data = {
      kCommonClassUsedByDex1,
      kCommonClassUsedByDex1Dex2,
      "H" + kCommonHotMethodUsedByDex1,
      "H" + kCommonHotMethodUsedByDex1Dex2
  };
  std::string expected_profile_content = JoinProfileLines(expected_data);

  ScratchFile profile1;
  ScratchFile profile2;
  EXPECT_TRUE(CreateProfile(input_file_contents1, profile1.GetFilename(), core_dex));
  EXPECT_TRUE(CreateProfile(input_file_contents2, profile2.GetFilename(), core_dex));

  ProfileCompilationInfo boot_profile1;
  ProfileCompilationInfo boot_profile2;
  boot_profile1.Load(profile1.GetFilename(), /*for_boot_image*/ true);
  boot_profile2.Load(profile2.GetFilename(), /*for_boot_image*/ true);

  // Generate the boot profile.
  ScratchFile out_profile;
  ScratchFile out_preloaded_classes;
  std::vector<std::string> args;
  args.push_back(GetProfmanCmd());
  args.push_back("--generate-boot-image-profile");
  args.push_back("--class-threshold=" + std::to_string(kDirtyThreshold));
  args.push_back("--clean-class-threshold=" + std::to_string(kCleanThreshold));
  args.push_back("--method-threshold=" + std::to_string(kMethodThreshold));
  args.push_back("--profile-file=" + profile1.GetFilename());
  args.push_back("--profile-file=" + profile2.GetFilename());
  args.push_back("--out-profile-path=" + out_profile.GetFilename());
  args.push_back("--out-preloaded-classes-path=" + out_preloaded_classes.GetFilename());
  args.push_back("--apk=" + core_dex);
  args.push_back("--dex-location=" + core_dex);

  std::string error;
  ASSERT_EQ(ExecAndReturnCode(args, &error), 0) << error;

  // Verify the boot profile contents.
  std::string output_profile_contents;
  ASSERT_TRUE(android::base::ReadFileToString(
      out_profile.GetFilename(), &output_profile_contents));
  ASSERT_EQ(output_profile_contents, expected_profile_content);
}

TEST_F(ProfileAssistantTest, TestProfileCreationOneNotMatched) {
  // Class names put here need to be in sorted order.
  std::vector<std::string> class_names = {
    "Ldoesnt/match/this/one;",
    "Ljava/lang/Comparable;",
    "Ljava/lang/Object;"
  };
  std::string input_file_contents;
  for (std::string& class_name : class_names) {
    input_file_contents += class_name + std::string("\n");
  }
  std::string output_file_contents;
  ASSERT_TRUE(CreateAndDump(input_file_contents, &output_file_contents));
  std::string expected_contents =
      class_names[1] + std::string("\n") +
      class_names[2] + std::string("\n");
  ASSERT_EQ(output_file_contents, expected_contents);
}

TEST_F(ProfileAssistantTest, TestProfileCreationNoneMatched) {
  // Class names put here need to be in sorted order.
  std::vector<std::string> class_names = {
    "Ldoesnt/match/this/one;",
    "Ldoesnt/match/this/one/either;",
    "Lnor/this/one;"
  };
  std::string input_file_contents;
  for (std::string& class_name : class_names) {
    input_file_contents += class_name + std::string("\n");
  }
  std::string output_file_contents;
  ASSERT_TRUE(CreateAndDump(input_file_contents, &output_file_contents));
  std::string expected_contents("");
  ASSERT_EQ(output_file_contents, expected_contents);
}

// Test that we can dump profiles in a way they can be re-constituted.
// Test goes 'txt -> prof -> txt -> prof' and then compares the two profs.
TEST_F(ProfileAssistantTest, TestProfileRoundTrip) {
  // Create the profile content.
  std::vector<std::string_view> methods = {
    "HLTestInline;->inlineMonomorphic(LSuper;)I+LSubA;",
    "HLTestInline;->inlinePolymorphic(LSuper;)I+LSubA;,LSubB;,LSubC;",
    "HLTestInline;->inlineMegamorphic(LSuper;)I+LSubA;,LSubB;,LSubC;,LSubD;,LSubE;",
    "HLTestInline;->inlineMissingTypes(LSuper;)I+missing_types",
    "HLTestInline;->noInlineCache(LSuper;)I",
    "HLTestInline;->inlineMultiMonomorphic(LSuper;LSecret;)I+]LSuper;LSubA;]LSecret;LSubB;",
    "HLTestInline;->inlineMultiPolymorphic(LSuper;LSecret;)I+]LSuper;LSubA;,LSubB;,LSubC;]LSecret;LSubB;,LSubC;",
    "HLTestInline;->inlineMultiMegamorphic(LSuper;LSecret;)I+]LSuper;LSubA;,LSubB;,LSubC;,LSubD;,LSubE;]LSecret;megamorphic_types",
    "HLTestInline;->inlineMultiMissingTypes(LSuper;LSecret;)I+]LSuper;missing_types]LSecret;missing_types",
    "HLTestInline;->inlineTriplePolymorphic(LSuper;LSecret;LSecret;)I+]LSuper;LSubA;,LSubB;,LSubC;]LSecret;LSubB;,LSubC;",
    "HLTestInline;->noInlineCacheMulti(LSuper;LSecret;)I",
  };
  std::ostringstream input_file_contents;
  for (const std::string_view& m : methods) {
    input_file_contents << m << "\n";
  }

  // Create the profile and save it to disk.
  ScratchFile profile_file;
  ASSERT_TRUE(CreateProfile(input_file_contents.str(),
                            profile_file.GetFilename(),
                            GetTestDexFileName("ProfileTestMultiDex")));

  // Dump the file back into text.
  std::string text_two;
  ASSERT_TRUE(DumpClassesAndMethods(
      profile_file.GetFilename(), &text_two, GetTestDexFileName("ProfileTestMultiDex")));

  // Create another profile and save it to the disk as well.
  ScratchFile profile_two;
  ASSERT_TRUE(CreateProfile(
      text_two, profile_two.GetFilename(), GetTestDexFileName("ProfileTestMultiDex")));

  // These two profiles should be bit-identical.
  // TODO We could compare the 'text_two' to the methods but since the order is
  // arbitrary for many parts and there are multiple 'correct' dumps we'd need
  // to basically parse everything and this is simply easier.
  std::string error;
  std::vector<std::string> args { kIsTargetBuild ? "/system/bin/cmp" : "/usr/bin/cmp",
                                  "-s",
                                  profile_file.GetFilename(),
                                  profile_two.GetFilename() };
  ASSERT_EQ(ExecAndReturnCode(args, &error), 0) << error << " from " << text_two;
}


// Test that we can dump profiles in a way they can be re-constituted and
// annotations don't interfere. Test goes 'txt -> ProfileWithAnnotations -> txt
// -> prof' and then compares that to one that is 'txt ->
// prof_without_annotations'.
TEST_F(ProfileAssistantTest, TestProfileRoundTripWithAnnotations) {
  // Create the profile content.
  std::vector<std::string_view> methods = {
    "HLTestInline;->inlineMonomorphic(LSuper;)I+LSubA;",
    "HLTestInline;->inlinePolymorphic(LSuper;)I+LSubA;,LSubB;,LSubC;",
    "HLTestInline;->inlineMegamorphic(LSuper;)I+LSubA;,LSubB;,LSubC;,LSubD;,LSubE;",
    "HLTestInline;->inlineMissingTypes(LSuper;)I+missing_types",
    "HLTestInline;->noInlineCache(LSuper;)I",
    "HLTestInline;->inlineMultiMonomorphic(LSuper;LSecret;)I+]LSuper;LSubA;]LSecret;LSubB;",
    "HLTestInline;->inlineMultiPolymorphic(LSuper;LSecret;)I+]LSuper;LSubA;,LSubB;,LSubC;]LSecret;LSubB;,LSubC;",
    "HLTestInline;->inlineMultiMegamorphic(LSuper;LSecret;)I+]LSuper;LSubA;,LSubB;,LSubC;,LSubD;,LSubE;]LSecret;megamorphic_types",
    "HLTestInline;->inlineMultiMissingTypes(LSuper;LSecret;)I+]LSuper;missing_types]LSecret;missing_types",
    "HLTestInline;->inlineTriplePolymorphic(LSuper;LSecret;LSecret;)I+]LSuper;LSubA;,LSubB;,LSubC;]LSecret;LSubB;,LSubC;",
    "HLTestInline;->noInlineCacheMulti(LSuper;LSecret;)I",
  };
  std::ostringstream no_annotation_input_file_contents;
  std::ostringstream with_annotation_input_file_contents;
  for (const std::string_view& m : methods) {
    no_annotation_input_file_contents << m << "\n";
    with_annotation_input_file_contents << "{foobar}" << m << "\n";
  }

  // Create the profile and save it to disk.
  ScratchFile with_annotation_profile_file;
  ASSERT_TRUE(CreateProfile(with_annotation_input_file_contents.str(),
                            with_annotation_profile_file.GetFilename(),
                            GetTestDexFileName("ProfileTestMultiDex")));

  ScratchFile no_annotation_profile_file;
  ASSERT_TRUE(CreateProfile(no_annotation_input_file_contents.str(),
                            no_annotation_profile_file.GetFilename(),
                            GetTestDexFileName("ProfileTestMultiDex")));

  // Dump the file back into text.
  std::string text_two;
  ASSERT_TRUE(DumpClassesAndMethods(with_annotation_profile_file.GetFilename(),
                                    &text_two,
                                    GetTestDexFileName("ProfileTestMultiDex")));

  // Create another profile and save it to the disk as well.
  ScratchFile profile_two;
  ASSERT_TRUE(CreateProfile(
      text_two, profile_two.GetFilename(), GetTestDexFileName("ProfileTestMultiDex")));

  // These two profiles should be bit-identical.
  // TODO We could compare the 'text_two' to the methods but since the order is
  // arbitrary for many parts and there are multiple 'correct' dumps we'd need
  // to basically parse everything and this is simply easier.
  std::string error;
  std::vector<std::string> args { kIsTargetBuild ? "/system/bin/cmp" : "/usr/bin/cmp",
                                  "-s",
                                  no_annotation_profile_file.GetFilename(),
                                  profile_two.GetFilename() };
  ASSERT_EQ(ExecAndReturnCode(args, &error), 0) << error << " from " << text_two;
}

TEST_F(ProfileAssistantTest, TestProfileCreateInlineCache) {
  // Create the profile content.
  std::vector<std::string_view> methods = {
    "HLTestInline;->inlineMonomorphic(LSuper;)I+LSubA;",
    "HLTestInline;->inlinePolymorphic(LSuper;)I+LSubA;,LSubB;,LSubC;",
    "HLTestInline;->inlineMegamorphic(LSuper;)I+LSubA;,LSubB;,LSubC;,LSubD;,LSubE;",
    "HLTestInline;->inlineMissingTypes(LSuper;)I+missing_types",
    "HLTestInline;->noInlineCache(LSuper;)I",
    "HLTestInline;->inlineMultiMonomorphic(LSuper;LSecret;)I+]LSuper;LSubA;]LSecret;LSubB;",
    "HLTestInline;->inlineMultiPolymorphic(LSuper;LSecret;)I+]LSuper;LSubA;,LSubB;,LSubC;]LSecret;LSubB;,LSubC;",
    "HLTestInline;->inlineMultiMegamorphic(LSuper;LSecret;)I+]LSuper;LSubA;,LSubB;,LSubC;,LSubD;,LSubE;]LSecret;LSubA;,LSubB;,LSubC;,LSubD;,LSubE;",
    "HLTestInline;->inlineMultiMissingTypes(LSuper;LSecret;)I+]LSuper;missing_types]LSecret;missing_types",
    "HLTestInline;->inlineTriplePolymorphic(LSuper;LSecret;LSecret;)I+]LSuper;LSubA;,LSubB;,LSubC;]LSecret;LSubB;,LSubC;",
    "HLTestInline;->noInlineCacheMulti(LSuper;LSecret;)I",
  };
  std::ostringstream input_file_contents;
  for (const std::string_view& m : methods) {
    input_file_contents << m << "\n";
  }

  // Create the profile and save it to disk.
  ScratchFile profile_file;
  ASSERT_TRUE(CreateProfile(input_file_contents.str(),
                            profile_file.GetFilename(),
                            GetTestDexFileName("ProfileTestMultiDex")));

  // Load the profile from disk.
  ProfileCompilationInfo info;
  ASSERT_TRUE(info.Load(GetFd(profile_file)));

  // Load the dex files and verify that the profile contains the expected methods info.
  ScopedObjectAccess soa(Thread::Current());
  jobject class_loader = LoadDex("ProfileTestMultiDex");
  ASSERT_NE(class_loader, nullptr);

  StackHandleScope<5> hs(soa.Self());
  Handle<mirror::Class> super_klass = hs.NewHandle(GetClass(soa, class_loader, "LSuper;"));
  Handle<mirror::Class> secret_klass = hs.NewHandle(GetClass(soa, class_loader, "LSecret;"));
  Handle<mirror::Class> sub_a = hs.NewHandle(GetClass(soa, class_loader, "LSubA;"));
  Handle<mirror::Class> sub_b = hs.NewHandle(GetClass(soa, class_loader, "LSubB;"));
  Handle<mirror::Class> sub_c = hs.NewHandle(GetClass(soa, class_loader, "LSubC;"));

  ASSERT_TRUE(super_klass != nullptr);
  ASSERT_TRUE(secret_klass != nullptr);
  ASSERT_TRUE(sub_a != nullptr);
  ASSERT_TRUE(sub_b != nullptr);
  ASSERT_TRUE(sub_c != nullptr);

  {
    // Verify that method inlineMonomorphic has the expected inline caches and nothing else.
    ArtMethod* inline_monomorphic = GetVirtualMethod(class_loader,
                                                     "LTestInline;",
                                                     "inlineMonomorphic");
    ASSERT_TRUE(inline_monomorphic != nullptr);
    TypeReferenceSet expected_monomorphic;
    expected_monomorphic.insert(MakeTypeReference(sub_a.Get()));
    AssertInlineCaches(inline_monomorphic,
                       expected_monomorphic,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/false);
  }

  {
    // Verify that method inlinePolymorphic has the expected inline caches and nothing else.
    ArtMethod* inline_polymorhic = GetVirtualMethod(class_loader,
                                                    "LTestInline;",
                                                    "inlinePolymorphic");
    ASSERT_TRUE(inline_polymorhic != nullptr);
    TypeReferenceSet expected_polymorphic;
    expected_polymorphic.insert(MakeTypeReference(sub_a.Get()));
    expected_polymorphic.insert(MakeTypeReference(sub_b.Get()));
    expected_polymorphic.insert(MakeTypeReference(sub_c.Get()));
    AssertInlineCaches(inline_polymorhic,
                       expected_polymorphic,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/false);
  }

  {
    // Verify that method inlineMegamorphic has the expected inline caches and nothing else.
    ArtMethod* inline_megamorphic = GetVirtualMethod(class_loader,
                                                     "LTestInline;",
                                                     "inlineMegamorphic");
    ASSERT_TRUE(inline_megamorphic != nullptr);
    TypeReferenceSet expected_megamorphic;
    AssertInlineCaches(inline_megamorphic,
                       expected_megamorphic,
                       info,
                       /*is_megamorphic=*/true,
                       /*is_missing_types=*/false);
  }

  {
    // Verify that method inlineMegamorphic has the expected inline caches and nothing else.
    ArtMethod* inline_missing_types = GetVirtualMethod(class_loader,
                                                       "LTestInline;",
                                                       "inlineMissingTypes");
    ASSERT_TRUE(inline_missing_types != nullptr);
    TypeReferenceSet expected_missing_Types;
    AssertInlineCaches(inline_missing_types,
                       expected_missing_Types,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/true);
  }

  {
    // Verify that method noInlineCache has no inline caches in the profile.
    ArtMethod* no_inline_cache = GetVirtualMethod(class_loader, "LTestInline;", "noInlineCache");
    ASSERT_TRUE(no_inline_cache != nullptr);
    ProfileCompilationInfo::MethodHotness hotness_no_inline_cache = info.GetMethodHotness(
        MethodReference(no_inline_cache->GetDexFile(), no_inline_cache->GetDexMethodIndex()));
    ASSERT_TRUE(hotness_no_inline_cache.IsHot());
    ASSERT_TRUE(hotness_no_inline_cache.GetInlineCacheMap()->empty());
  }

  {
    // Verify that method inlineMonomorphic has the expected inline caches and nothing else.
    ArtMethod* inline_monomorphic = GetVirtualMethod(class_loader,
                                                     "LTestInline;",
                                                     "inlineMultiMonomorphic");
    ASSERT_TRUE(inline_monomorphic != nullptr);
    TypeReferenceSet expected_monomorphic_super;
    TypeReferenceSet expected_monomorphic_secret;
    expected_monomorphic_super.insert(MakeTypeReference(sub_a.Get()));
    expected_monomorphic_secret.insert(MakeTypeReference(sub_b.Get()));
    AssertInlineCaches(inline_monomorphic,
                       GetDexPcOfCallTo(inline_monomorphic, super_klass),
                       expected_monomorphic_super,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/false);
    AssertInlineCaches(inline_monomorphic,
                       GetDexPcOfCallTo(inline_monomorphic, secret_klass),
                       expected_monomorphic_secret,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/false);
  }

  {
    // Verify that method inlinePolymorphic has the expected inline caches and nothing else.
    ArtMethod* inline_polymorhic = GetVirtualMethod(class_loader,
                                                    "LTestInline;",
                                                    "inlineMultiPolymorphic");
    ASSERT_TRUE(inline_polymorhic != nullptr);
    TypeReferenceSet expected_polymorphic_super;
    expected_polymorphic_super.insert(MakeTypeReference(sub_a.Get()));
    expected_polymorphic_super.insert(MakeTypeReference(sub_b.Get()));
    expected_polymorphic_super.insert(MakeTypeReference(sub_c.Get()));
    TypeReferenceSet expected_polymorphic_secret;
    expected_polymorphic_secret.insert(MakeTypeReference(sub_b.Get()));
    expected_polymorphic_secret.insert(MakeTypeReference(sub_c.Get()));
    AssertInlineCaches(inline_polymorhic,
                       GetDexPcOfCallTo(inline_polymorhic, super_klass),
                       expected_polymorphic_super,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/false);
    AssertInlineCaches(inline_polymorhic,
                       GetDexPcOfCallTo(inline_polymorhic, secret_klass),
                       expected_polymorphic_secret,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/false);
  }

  {
    // Verify that method inlinePolymorphic has the expected inline caches and nothing else.
    ArtMethod* inline_polymorhic = GetVirtualMethod(class_loader,
                                                    "LTestInline;",
                                                    "inlineTriplePolymorphic");
    ASSERT_TRUE(inline_polymorhic != nullptr);
    TypeReferenceSet expected_polymorphic_super;
    expected_polymorphic_super.insert(MakeTypeReference(sub_a.Get()));
    expected_polymorphic_super.insert(MakeTypeReference(sub_b.Get()));
    expected_polymorphic_super.insert(MakeTypeReference(sub_c.Get()));
    TypeReferenceSet expected_polymorphic_secret;
    expected_polymorphic_secret.insert(MakeTypeReference(sub_b.Get()));
    expected_polymorphic_secret.insert(MakeTypeReference(sub_c.Get()));
    AssertInlineCaches(inline_polymorhic,
                       GetDexPcOfCallTo(inline_polymorhic, super_klass),
                       expected_polymorphic_super,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/false);
    uint16_t first_call = GetDexPcOfCallTo(inline_polymorhic, secret_klass);
    AssertInlineCaches(inline_polymorhic,
                       first_call,
                       expected_polymorphic_secret,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/false);
    uint16_t second_call = GetDexPcOfCallTo(inline_polymorhic, secret_klass, first_call);
    ASSERT_LT(first_call, second_call);
    AssertInlineCaches(inline_polymorhic,
                       second_call,
                       expected_polymorphic_secret,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/false);
  }

  {
    // Verify that method inlineMegamorphic has the expected inline caches and nothing else.
    ArtMethod* inline_megamorphic = GetVirtualMethod(class_loader,
                                                     "LTestInline;",
                                                     "inlineMultiMegamorphic");
    ASSERT_TRUE(inline_megamorphic != nullptr);
    TypeReferenceSet expected_megamorphic;
    AssertInlineCaches(inline_megamorphic,
                       GetDexPcOfCallTo(inline_megamorphic, super_klass),
                       expected_megamorphic,
                       info,
                       /*is_megamorphic=*/true,
                       /*is_missing_types=*/false);
    AssertInlineCaches(inline_megamorphic,
                       GetDexPcOfCallTo(inline_megamorphic, secret_klass),
                       expected_megamorphic,
                       info,
                       /*is_megamorphic=*/true,
                       /*is_missing_types=*/false);
  }

  {
    // Verify that method inlineMegamorphic has the expected inline caches and nothing else.
    ArtMethod* inline_missing_types = GetVirtualMethod(class_loader,
                                                       "LTestInline;",
                                                       "inlineMultiMissingTypes");
    ASSERT_TRUE(inline_missing_types != nullptr);
    TypeReferenceSet expected_missing_Types;
    AssertInlineCaches(inline_missing_types,
                       GetDexPcOfCallTo(inline_missing_types, super_klass),
                       expected_missing_Types,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/true);
    AssertInlineCaches(inline_missing_types,
                       GetDexPcOfCallTo(inline_missing_types, secret_klass),
                       expected_missing_Types,
                       info,
                       /*is_megamorphic=*/false,
                       /*is_missing_types=*/true);
  }

  {
    // Verify that method noInlineCacheMulti has no inline caches in the profile.
    ArtMethod* no_inline_cache =
        GetVirtualMethod(class_loader, "LTestInline;", "noInlineCacheMulti");
    ASSERT_TRUE(no_inline_cache != nullptr);
    ProfileCompilationInfo::MethodHotness hotness_no_inline_cache = info.GetMethodHotness(
        MethodReference(no_inline_cache->GetDexFile(), no_inline_cache->GetDexMethodIndex()));
    ASSERT_TRUE(hotness_no_inline_cache.IsHot());
    ASSERT_TRUE(hotness_no_inline_cache.GetInlineCacheMap()->empty());
  }
}

TEST_F(ProfileAssistantTest, MergeProfilesWithDifferentDexOrder) {
  ScratchFile profile1;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({GetFd(profile1)});
  int reference_profile_fd = GetFd(reference_profile);

  // The new profile info will contain the methods with indices 0-100.
  const uint16_t kNumberOfMethodsToEnableCompilation = 100;
  ProfileCompilationInfo info1;
  SetupProfile(dex1, dex2, kNumberOfMethodsToEnableCompilation, 0, profile1, &info1,
      /*start_method_index=*/0, /*reverse_dex_write_order=*/false);

  // The reference profile info will contain the methods with indices 50-150.
  // When setting up the profile reverse the order in which the dex files
  // are added to the profile. This will verify that profman merges profiles
  // with a different dex order correctly.
  const uint16_t kNumberOfMethodsAlreadyCompiled = 100;
  ProfileCompilationInfo reference_info;
  SetupProfile(dex1, dex2, kNumberOfMethodsAlreadyCompiled, 0, reference_profile,
      &reference_info, kNumberOfMethodsToEnableCompilation / 2, /*reverse_dex_write_order=*/true);

  // We should advise compilation.
  ASSERT_EQ(ProfileAssistant::kCompile,
            ProcessProfiles(profile_fds, reference_profile_fd));

  // The resulting compilation info must be equal to the merge of the inputs.
  ProfileCompilationInfo result;
  ASSERT_TRUE(result.Load(reference_profile_fd));

  ProfileCompilationInfo expected;
  ASSERT_TRUE(expected.MergeWith(reference_info));
  ASSERT_TRUE(expected.MergeWith(info1));
  ASSERT_TRUE(expected.Equals(result));

  // The information from profile must remain the same.
  CheckProfileInfo(profile1, info1);
}

TEST_F(ProfileAssistantTest, TestProfileCreateWithSubtype) {
  // Create the profile content.
  std::vector<std::string> profile_methods = {
      "HLTestInlineSubtype;->inlineMonomorphic(LSuper;)I+]LSuper;LSubA;",
  };
  std::string input_file_contents;
  for (std::string& m : profile_methods) {
    input_file_contents += m + std::string("\n");
  }

  // Create the profile and save it to disk.
  ScratchFile profile_file;
  std::string dex_filename = GetTestDexFileName("ProfileTestMultiDex");
  ASSERT_TRUE(CreateProfile(input_file_contents, profile_file.GetFilename(), dex_filename));

  // Load the profile from disk.
  ProfileCompilationInfo info;
  ASSERT_TRUE(info.Load(GetFd(profile_file)));
  LOG(ERROR) << profile_file.GetFilename();

  // Load the dex files and verify that the profile contains the expected
  // methods info.
  ScopedObjectAccess soa(Thread::Current());
  jobject class_loader = LoadDex("ProfileTestMultiDex");
  ASSERT_NE(class_loader, nullptr);

  // NB This is the supertype of the declared line!
  ArtMethod* inline_monomorphic_super =
      GetVirtualMethod(class_loader, "LTestInline;", "inlineMonomorphic");
  const DexFile* dex_file = inline_monomorphic_super->GetDexFile();

  // Verify that the inline cache is present in the superclass
  ProfileCompilationInfo::MethodHotness hotness_super = info.GetMethodHotness(
      MethodReference(dex_file, inline_monomorphic_super->GetDexMethodIndex()));
  ASSERT_TRUE(hotness_super.IsHot());
  const ProfileCompilationInfo::InlineCacheMap* inline_caches = hotness_super.GetInlineCacheMap();
  ASSERT_EQ(inline_caches->size(), 1u);
  const ProfileCompilationInfo::DexPcData& dex_pc_data = inline_caches->begin()->second;
  dex::TypeIndex target_type_index(dex_file->GetIndexForTypeId(*dex_file->FindTypeId("LSubA;")));
  ASSERT_EQ(1u, dex_pc_data.classes.size());
  ASSERT_EQ(target_type_index, dex_pc_data.classes.begin()->type_index);

  // Verify that the method is present in subclass but there are no
  // inline-caches (since there is no code).
  const dex::MethodId& super_method_id =
      dex_file->GetMethodId(inline_monomorphic_super->GetDexMethodIndex());
  uint32_t sub_method_index = dex_file->GetIndexForMethodId(
      *dex_file->FindMethodId(*dex_file->FindTypeId("LTestInlineSubtype;"),
                              dex_file->GetStringId(super_method_id.name_idx_),
                              dex_file->GetProtoId(super_method_id.proto_idx_)));
  ProfileCompilationInfo::MethodHotness hotness_sub =
      info.GetMethodHotness(MethodReference(dex_file, sub_method_index));
  ASSERT_TRUE(hotness_sub.IsHot());
  ASSERT_EQ(hotness_sub.GetInlineCacheMap()->size(), 0u);
}

TEST_F(ProfileAssistantTest, TestProfileCreateWithSubtypeAndDump) {
  // Create the profile content.
  std::vector<std::string> profile_methods = {
      "HLTestInlineSubtype;->inlineMonomorphic(LSuper;)I+]LSuper;LSubA;",
  };
  std::string input_file_contents;
  for (std::string& m : profile_methods) {
    input_file_contents += m + std::string("\n");
  }

  // Create the profile and save it to disk.
  ScratchFile profile_file;
  std::string dex_filename = GetTestDexFileName("ProfileTestMultiDex");
  ASSERT_TRUE(CreateProfile(input_file_contents, profile_file.GetFilename(), dex_filename));

  std::string dump_ic;
  ASSERT_TRUE(DumpClassesAndMethods(
      profile_file.GetFilename(), &dump_ic, GetTestDexFileName("ProfileTestMultiDex")));

  std::vector<std::string> lines;
  std::stringstream dump_stream(dump_ic);
  std::string cur;
  while (std::getline(dump_stream, cur, '\n')) {
    lines.push_back(std::move(cur));
  }

  EXPECT_EQ(lines.size(), 2u);
  EXPECT_TRUE(std::find(lines.cbegin(),
                        lines.cend(),
                        "HLTestInline;->inlineMonomorphic(LSuper;)I+]LSuper;LSubA;") !=
              lines.cend());
  EXPECT_TRUE(std::find(lines.cbegin(),
                        lines.cend(),
                        "HLTestInlineSubtype;->inlineMonomorphic(LSuper;)I") != lines.cend());
}

TEST_F(ProfileAssistantTest, TestProfileCreateWithInvalidData) {
  // Create the profile content.
  std::vector<std::string> profile_methods = {
    "HLTestInline;->inlineMonomorphic(LSuper;)I+invalid_class",
    "HLTestInline;->invalid_method",
    "invalid_class"
  };
  std::string input_file_contents;
  for (std::string& m : profile_methods) {
    input_file_contents += m + std::string("\n");
  }

  // Create the profile and save it to disk.
  ScratchFile profile_file;
  std::string dex_filename = GetTestDexFileName("ProfileTestMultiDex");
  ASSERT_TRUE(CreateProfile(input_file_contents,
                            profile_file.GetFilename(),
                            dex_filename));

  // Load the profile from disk.
  ProfileCompilationInfo info;
  ASSERT_TRUE(info.Load(GetFd(profile_file)));

  // Load the dex files and verify that the profile contains the expected methods info.
  ScopedObjectAccess soa(Thread::Current());
  jobject class_loader = LoadDex("ProfileTestMultiDex");
  ASSERT_NE(class_loader, nullptr);

  ArtMethod* inline_monomorphic = GetVirtualMethod(class_loader,
                                                   "LTestInline;",
                                                   "inlineMonomorphic");
  const DexFile* dex_file = inline_monomorphic->GetDexFile();

  // Verify that the inline cache contains the invalid type.
  ProfileCompilationInfo::MethodHotness hotness =
      info.GetMethodHotness(MethodReference(dex_file, inline_monomorphic->GetDexMethodIndex()));
  ASSERT_TRUE(hotness.IsHot());
  const ProfileCompilationInfo::InlineCacheMap* inline_caches = hotness.GetInlineCacheMap();
  ASSERT_EQ(inline_caches->size(), 1u);
  const ProfileCompilationInfo::DexPcData& dex_pc_data = inline_caches->begin()->second;
  dex::TypeIndex invalid_class_index(std::numeric_limits<uint16_t>::max() - 1);
  ASSERT_EQ(1u, dex_pc_data.classes.size());
  ASSERT_EQ(invalid_class_index, dex_pc_data.classes.begin()->type_index);

  // Verify that the start-up classes contain the invalid class.
  std::set<dex::TypeIndex> classes;
  std::set<uint16_t> hot_methods;
  std::set<uint16_t> startup_methods;
  std::set<uint16_t> post_start_methods;
  ASSERT_TRUE(info.GetClassesAndMethods(*dex_file,
                                        &classes,
                                        &hot_methods,
                                        &startup_methods,
                                        &post_start_methods));
  ASSERT_EQ(1u, classes.size());
  ASSERT_TRUE(classes.find(invalid_class_index) != classes.end());

  // Verify that the invalid method did not get in the profile.
  ASSERT_EQ(1u, hot_methods.size());
  uint16_t invalid_method_index = std::numeric_limits<uint16_t>::max() - 1;
  ASSERT_FALSE(hot_methods.find(invalid_method_index) != hot_methods.end());
}

TEST_F(ProfileAssistantTest, DumpOnly) {
  ScratchFile profile;

  const uint32_t kNumberOfMethods = 64;
  std::vector<uint32_t> hot_methods;
  std::vector<uint32_t> startup_methods;
  std::vector<uint32_t> post_startup_methods;
  for (size_t i = 0; i < kNumberOfMethods; ++i) {
    if (i % 2 == 0) {
      hot_methods.push_back(i);
    }
    if (i % 3 == 1) {
      startup_methods.push_back(i);
    }
    if (i % 4 == 2) {
      post_startup_methods.push_back(i);
    }
  }
  EXPECT_GT(hot_methods.size(), 0u);
  EXPECT_GT(startup_methods.size(), 0u);
  EXPECT_GT(post_startup_methods.size(), 0u);
  ProfileCompilationInfo info1;
  SetupBasicProfile(dex1,
                    hot_methods,
                    startup_methods,
                    post_startup_methods,
                    profile,
                    &info1);
  std::string output;
  DumpOnly(profile.GetFilename(), &output);
  const size_t hot_offset = output.find("hot methods:");
  const size_t startup_offset = output.find("startup methods:");
  const size_t post_startup_offset = output.find("post startup methods:");
  const size_t classes_offset = output.find("classes:");
  ASSERT_NE(hot_offset, std::string::npos);
  ASSERT_NE(startup_offset, std::string::npos);
  ASSERT_NE(post_startup_offset, std::string::npos);
  ASSERT_LT(hot_offset, startup_offset);
  ASSERT_LT(startup_offset, post_startup_offset);
  // Check the actual contents of the dump by looking at the offsets of the methods.
  for (uint32_t m : hot_methods) {
    const size_t pos = output.find(std::to_string(m) + "[],", hot_offset);
    ASSERT_NE(pos, std::string::npos) << output;
    EXPECT_LT(pos, startup_offset) << output;
  }
  for (uint32_t m : startup_methods) {
    const size_t pos = output.find(std::to_string(m) + ",", startup_offset);
    ASSERT_NE(pos, std::string::npos) << output;
    EXPECT_LT(pos, post_startup_offset) << output;
  }
  for (uint32_t m : post_startup_methods) {
    const size_t pos = output.find(std::to_string(m) + ",", post_startup_offset);
    ASSERT_NE(pos, std::string::npos) << output;
    EXPECT_LT(pos, classes_offset) << output;
  }
}

TEST_F(ProfileAssistantTest, MergeProfilesWithFilter) {
  ScratchFile profile1;
  ScratchFile profile2;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({
      GetFd(profile1),
      GetFd(profile2)});
  int reference_profile_fd = GetFd(reference_profile);

  // Use a real dex file to generate profile test data.
  // The file will be used during merging to filter unwanted data.
  std::vector<std::unique_ptr<const DexFile>> dex_files = OpenTestDexFiles("ProfileTestMultiDex");
  const DexFile& d1 = *dex_files[0];
  const DexFile& d2 = *dex_files[1];
  // The new profile info will contain the methods with indices 0-100.
  const uint16_t kNumberOfMethodsToEnableCompilation = 100;
  ProfileCompilationInfo info1;
  SetupProfile(&d1, dex1, kNumberOfMethodsToEnableCompilation, 0, profile1, &info1);
  ProfileCompilationInfo info2;
  SetupProfile(&d2, dex2, kNumberOfMethodsToEnableCompilation, 0, profile2, &info2);


  // The reference profile info will contain the methods with indices 50-150.
  const uint16_t kNumberOfMethodsAlreadyCompiled = 100;
  ProfileCompilationInfo reference_info;
  SetupProfile(&d1, dex1,
      kNumberOfMethodsAlreadyCompiled, 0, reference_profile,
      &reference_info, kNumberOfMethodsToEnableCompilation / 2);

  // Run profman and pass the dex file with --apk-fd.
  android::base::unique_fd apk_fd(
      open(GetTestDexFileName("ProfileTestMultiDex").c_str(), O_RDONLY));  // NOLINT
  ASSERT_GE(apk_fd.get(), 0);

  std::string profman_cmd = GetProfmanCmd();
  std::vector<std::string> argv_str;
  argv_str.push_back(profman_cmd);
  argv_str.push_back("--profile-file-fd=" + std::to_string(profile1.GetFd()));
  argv_str.push_back("--profile-file-fd=" + std::to_string(profile2.GetFd()));
  argv_str.push_back("--reference-profile-file-fd=" + std::to_string(reference_profile.GetFd()));
  argv_str.push_back("--apk-fd=" + std::to_string(apk_fd.get()));
  std::string error;

  EXPECT_EQ(ExecAndReturnCode(argv_str, &error), ProfileAssistant::kCompile) << error;

  // Verify that we can load the result.

  ProfileCompilationInfo result;
  ASSERT_TRUE(result.Load(reference_profile_fd));

  // Verify that the result filtered out data not belonging to the dex file.
  // This is equivalent to checking that the result is equal to the merging of
  // all profiles while filtering out data not belonging to the dex file.

  ProfileCompilationInfo::ProfileLoadFilterFn filter_fn =
      [&d1, &d2](const std::string& dex_location, uint32_t checksum) -> bool {
          return (dex_location == ProfileCompilationInfo::GetProfileDexFileBaseKey(d1.GetLocation())
              && checksum == d1.GetLocationChecksum())
              || (dex_location == ProfileCompilationInfo::GetProfileDexFileBaseKey(d2.GetLocation())
              && checksum == d2.GetLocationChecksum());
        };

  ProfileCompilationInfo info1_filter;
  ProfileCompilationInfo info2_filter;
  ProfileCompilationInfo expected;

  info2_filter.Load(profile1.GetFd(), /*merge_classes=*/ true, filter_fn);
  info2_filter.Load(profile2.GetFd(), /*merge_classes=*/ true, filter_fn);
  expected.Load(reference_profile.GetFd(), /*merge_classes=*/ true, filter_fn);

  ASSERT_TRUE(expected.MergeWith(info1_filter));
  ASSERT_TRUE(expected.MergeWith(info2_filter));

  ASSERT_TRUE(expected.Equals(result));
}

TEST_F(ProfileAssistantTest, CopyAndUpdateProfileKey) {
  ScratchFile profile1;
  ScratchFile reference_profile;

  // Use a real dex file to generate profile test data. During the copy-and-update the
  // matching is done based on checksum so we have to match with the real thing.
  std::vector<std::unique_ptr<const DexFile>> dex_files = OpenTestDexFiles("ProfileTestMultiDex");
  const DexFile& d1 = *dex_files[0];
  const DexFile& d2 = *dex_files[1];

  ProfileCompilationInfo info1;
  uint16_t num_methods_to_add = std::min(d1.NumMethodIds(), d2.NumMethodIds());

  const DexFile* dex_to_be_updated1 =
      BuildDex("fake-location1", d1.GetLocationChecksum(), "LC;", d1.NumMethodIds());
  const DexFile* dex_to_be_updated2 =
      BuildDex("fake-location2", d2.GetLocationChecksum(), "LC;", d2.NumMethodIds());
  SetupProfile(dex_to_be_updated1,
               dex_to_be_updated2,
               num_methods_to_add,
               /*number_of_classes=*/ 0,
               profile1,
               &info1);

  // Run profman and pass the dex file with --apk-fd.
  android::base::unique_fd apk_fd(
      open(GetTestDexFileName("ProfileTestMultiDex").c_str(), O_RDONLY));  // NOLINT
  ASSERT_GE(apk_fd.get(), 0);

  std::string profman_cmd = GetProfmanCmd();
  std::vector<std::string> argv_str;
  argv_str.push_back(profman_cmd);
  argv_str.push_back("--profile-file-fd=" + std::to_string(profile1.GetFd()));
  argv_str.push_back("--reference-profile-file-fd=" + std::to_string(reference_profile.GetFd()));
  argv_str.push_back("--apk-fd=" + std::to_string(apk_fd.get()));
  argv_str.push_back("--copy-and-update-profile-key");
  std::string error;

  ASSERT_EQ(ExecAndReturnCode(argv_str, &error), 0) << error;

  // Verify that we can load the result.
  ProfileCompilationInfo result;
  ASSERT_TRUE(result.Load(reference_profile.GetFd()));

  // Verify that the renaming was done.
  for (uint16_t i = 0; i < num_methods_to_add; i ++) {
    ASSERT_TRUE(result.GetMethodHotness(MethodReference(&d1, i)).IsHot()) << i;
    ASSERT_TRUE(result.GetMethodHotness(MethodReference(&d2, i)).IsHot()) << i;

    ASSERT_FALSE(result.GetMethodHotness(MethodReference(dex_to_be_updated1, i)).IsHot()) << i;
    ASSERT_FALSE(result.GetMethodHotness(MethodReference(dex_to_be_updated2, i)).IsHot()) << i;
  }
}

TEST_F(ProfileAssistantTest, BootImageMerge) {
  ScratchFile profile;
  ScratchFile reference_profile;
  std::vector<int> profile_fds({GetFd(profile)});
  int reference_profile_fd = GetFd(reference_profile);
  std::vector<uint32_t> hot_methods_cur;
  std::vector<uint32_t> hot_methods_ref;
  std::vector<uint32_t> empty_vector;
  size_t num_methods = 100;
  for (size_t i = 0; i < num_methods; ++i) {
    hot_methods_cur.push_back(i);
  }
  for (size_t i = 0; i < num_methods; ++i) {
    hot_methods_ref.push_back(i);
  }
  ProfileCompilationInfo info1;
  SetupBasicProfile(dex1, hot_methods_cur, empty_vector, empty_vector,
      profile, &info1);
  ProfileCompilationInfo info2(/*for_boot_image=*/true);
  SetupBasicProfile(dex1, hot_methods_ref, empty_vector, empty_vector,
      reference_profile, &info2);

  std::vector<const std::string> extra_args({"--force-merge", "--boot-image-merge"});

  int return_code = ProcessProfiles(profile_fds, reference_profile_fd, extra_args);

  ASSERT_EQ(return_code, ProfileAssistant::kSuccess);

  // Verify the result: it should be equal to info2 since info1 is a regular profile
  // and should be ignored.
  ProfileCompilationInfo result;
  ASSERT_TRUE(result.Load(reference_profile.GetFd()));
  ASSERT_TRUE(result.Equals(info2));
}

// Under default behaviour we should not advice compilation
// and the reference profile should not be updated.
// However we pass --force-merge to force aggregation and in this case
// we should see an update.
TEST_F(ProfileAssistantTest, ForceMerge) {
  const uint16_t kNumberOfClassesInRefProfile = 6000;
  const uint16_t kNumberOfClassesInCurProfile = 6110;  // Threshold is 2%.

  ScratchFile profile;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({ GetFd(profile)});
  int reference_profile_fd = GetFd(reference_profile);

  ProfileCompilationInfo info1;
  SetupProfile(dex1, dex2, 0, kNumberOfClassesInRefProfile, profile,  &info1);
  ProfileCompilationInfo info2;
  SetupProfile(dex1, dex2, 0, kNumberOfClassesInCurProfile, reference_profile, &info2);

  std::vector<const std::string> extra_args({"--force-merge"});
  int return_code = ProcessProfiles(profile_fds, reference_profile_fd, extra_args);

  ASSERT_EQ(return_code, ProfileAssistant::kSuccess);

  // Check that the result is the aggregation.
  ProfileCompilationInfo result;
  ASSERT_TRUE(result.Load(reference_profile.GetFd()));
  ASSERT_TRUE(info1.MergeWith(info2));
  ASSERT_TRUE(result.Equals(info1));
}

// Test that we consider the annations when we merge boot image profiles.
TEST_F(ProfileAssistantTest, BootImageMergeWithAnnotations) {
  ScratchFile profile;
  ScratchFile reference_profile;

  std::vector<int> profile_fds({GetFd(profile)});
  int reference_profile_fd = GetFd(reference_profile);

  // Use a real dex file to generate profile test data so that we can pass descriptors to profman.
  std::vector<std::unique_ptr<const DexFile>> dex_files = OpenTestDexFiles("ProfileTestMultiDex");
  const DexFile& d1 = *dex_files[0];
  const DexFile& d2 = *dex_files[1];
  // The new profile info will contain the methods with indices 0-100.
  ProfileCompilationInfo info(/*for_boot_image*/ true);
  ProfileCompilationInfo::ProfileSampleAnnotation psa1("package1");
  ProfileCompilationInfo::ProfileSampleAnnotation psa2("package2");

  AddMethod(&info, &d1, 0, Hotness::kFlagHot, psa1);
  AddMethod(&info, &d2, 0, Hotness::kFlagHot, psa2);
  info.Save(profile.GetFd());

  // Run profman and pass the dex file with --apk-fd.
  android::base::unique_fd apk_fd(
      open(GetTestDexFileName("ProfileTestMultiDex").c_str(), O_RDONLY));  // NOLINT
  ASSERT_GE(apk_fd.get(), 0);

  std::string profman_cmd = GetProfmanCmd();
  std::vector<std::string> argv_str;
  argv_str.push_back(profman_cmd);
  argv_str.push_back("--profile-file-fd=" + std::to_string(profile.GetFd()));
  argv_str.push_back("--reference-profile-file-fd=" + std::to_string(reference_profile.GetFd()));
  argv_str.push_back("--apk-fd=" + std::to_string(apk_fd.get()));
  argv_str.push_back("--force-merge");
  argv_str.push_back("--boot-image-merge");
  std::string error;

  EXPECT_EQ(ExecAndReturnCode(argv_str, &error), ProfileAssistant::kSuccess) << error;

  // Verify that we can load the result and that it equals to what we saved.
  ProfileCompilationInfo result;
  ASSERT_TRUE(result.Load(reference_profile_fd));
  ASSERT_TRUE(info.Equals(result));
}

TEST_F(ProfileAssistantTest, DifferentProfileVersions) {
  ScratchFile profile1;
  ScratchFile profile2;

  ProfileCompilationInfo info1(/*for_boot_image*/ false);
  info1.Save(profile1.GetFd());

  ProfileCompilationInfo info2(/*for_boot_image*/ true);
  info2.Save(profile2.GetFd());

  std::vector<int> profile_fds({ GetFd(profile1)});
  int reference_profile_fd = GetFd(profile2);
  ASSERT_EQ(ProcessProfiles(profile_fds, reference_profile_fd),
            ProfileAssistant::kErrorDifferentVersions);

  // Reverse the order of the profiles to verify we get the same behaviour.
  profile_fds[0] = GetFd(profile2);
  reference_profile_fd = GetFd(profile1);
  ASSERT_EQ(ProcessProfiles(profile_fds, reference_profile_fd),
            ProfileAssistant::kErrorDifferentVersions);
}

// Under default behaviour we will abort if we cannot load a profile during a merge
// operation. However, if we pass --force-merge to force aggregation we should
// ignore files we cannot load
TEST_F(ProfileAssistantTest, ForceMergeIgnoreProfilesItCannotLoad) {
  ScratchFile profile1;
  ScratchFile profile2;

  // Write corrupt data in the first file.
  std::string content = "giberish";
  ASSERT_TRUE(profile1.GetFile()->WriteFully(content.c_str(), content.length()));

  ProfileCompilationInfo info2(/*for_boot_image*/ true);
  info2.Save(profile2.GetFd());

  std::vector<int> profile_fds({ GetFd(profile1)});
  int reference_profile_fd = GetFd(profile2);

  // With force-merge we should merge successfully.
  std::vector<const std::string> extra_args({"--force-merge"});
  ASSERT_EQ(ProcessProfiles(profile_fds, reference_profile_fd, extra_args),
            ProfileAssistant::kSuccess);

  ProfileCompilationInfo result;
  ASSERT_TRUE(result.Load(reference_profile_fd));
  ASSERT_TRUE(info2.Equals(result));

  // Without force-merge we should fail.
  ASSERT_EQ(ProcessProfiles(profile_fds, reference_profile_fd),
            ProfileAssistant::kErrorBadProfiles);
}
}  // namespace art
