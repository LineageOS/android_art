/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <string>
#include <vector>

#include <backtrace/BacktraceMap.h>
#include <gtest/gtest.h>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "base/file_utils.h"
#include "base/mem_map.h"
#include "common_runtime_test.h"
#include "compiler_callbacks.h"
#include "dex2oat_environment_test.h"
#include "dexopt_test.h"
#include "gc/space/image_space.h"
#include "hidden_api.h"
#include "oat.h"

namespace art {
void DexoptTest::SetUp() {
  ReserveImageSpace();
  Dex2oatEnvironmentTest::SetUp();
}

void DexoptTest::PreRuntimeCreate() {
  std::string error_msg;
  UnreserveImageSpace();
}

void DexoptTest::PostRuntimeCreate() {
  ReserveImageSpace();
}

bool DexoptTest::Dex2Oat(const std::vector<std::string>& args, std::string* error_msg) {
  std::vector<std::string> argv;
  if (!CommonRuntimeTest::StartDex2OatCommandLine(&argv, error_msg)) {
    return false;
  }

  Runtime* runtime = Runtime::Current();
  if (runtime->GetHiddenApiEnforcementPolicy() == hiddenapi::EnforcementPolicy::kEnabled) {
    argv.push_back("--runtime-arg");
    argv.push_back("-Xhidden-api-policy:enabled");
  }

  if (!kIsTargetBuild) {
    argv.push_back("--host");
  }

  argv.insert(argv.end(), args.begin(), args.end());

  std::string command_line(android::base::Join(argv, ' '));
  return Exec(argv, error_msg);
}

std::string DexoptTest::GenerateAlternateImage(const std::string& scratch_dir) {
  std::vector<std::string> libcore_dex_files = GetLibCoreDexFileNames();
  std::vector<std::string> libcore_dex_locations = GetLibCoreDexLocations();

  std::string image_dir = scratch_dir + GetInstructionSetString(kRuntimeISA);
  int mkdir_result = mkdir(image_dir.c_str(), 0700);
  CHECK_EQ(0, mkdir_result) << image_dir.c_str();

  std::vector<std::string> extra_args {
    "--compiler-filter=verify",
    android::base::StringPrintf("--base=0x%08x", ART_BASE_ADDRESS),
  };
  std::string filename_prefix = image_dir + "/boot-interpreter";
  ArrayRef<const std::string> dex_files(libcore_dex_files);
  ArrayRef<const std::string> dex_locations(libcore_dex_locations);
  std::string error_msg;
  bool ok = CompileBootImage(extra_args, filename_prefix, dex_files, dex_locations, &error_msg);
  EXPECT_TRUE(ok) << error_msg;

  return scratch_dir + "boot-interpreter.art";
}

void DexoptTest::GenerateOatForTest(const std::string& dex_location,
                                    const std::string& oat_location,
                                    CompilerFilter::Filter filter,
                                    bool with_alternate_image,
                                    const char* compilation_reason,
                                    const std::vector<std::string>& extra_args) {
  std::vector<std::string> args;
  args.push_back("--dex-file=" + dex_location);
  args.push_back("--oat-file=" + oat_location);
  args.push_back("--compiler-filter=" + CompilerFilter::NameOfFilter(filter));
  args.push_back("--runtime-arg");

  // Use -Xnorelocate regardless of the relocate argument.
  // We control relocation by redirecting the dalvik cache when needed
  // rather than use this flag.
  args.push_back("-Xnorelocate");

  ScratchFile profile_file;
  if (CompilerFilter::DependsOnProfile(filter)) {
    args.push_back("--profile-file=" + profile_file.GetFilename());
  }

  std::string image_location = GetImageLocation();
  std::optional<ScratchDir> scratch;
  if (with_alternate_image) {
    scratch.emplace();  // Create the scratch directory for the generated boot image.
    std::string alternate_image_location = GenerateAlternateImage(scratch->GetPath());
    args.push_back("--boot-image=" + alternate_image_location);
  }

  if (compilation_reason != nullptr) {
    args.push_back("--compilation-reason=" + std::string(compilation_reason));
  }

  args.insert(args.end(), extra_args.begin(), extra_args.end());

  std::string error_msg;
  ASSERT_TRUE(Dex2Oat(args, &error_msg)) << error_msg;

  // Verify the odex file was generated as expected.
  std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/ -1,
                                                   oat_location.c_str(),
                                                   oat_location.c_str(),
                                                   /*executable=*/ false,
                                                   /*low_4gb=*/ false,
                                                   dex_location,
                                                   &error_msg));
  ASSERT_TRUE(odex_file.get() != nullptr) << error_msg;
  EXPECT_EQ(filter, odex_file->GetCompilerFilter());

  if (CompilerFilter::DependsOnImageChecksum(filter)) {
    const OatHeader& oat_header = odex_file->GetOatHeader();
    const char* oat_bcp = oat_header.GetStoreValueByKey(OatHeader::kBootClassPathKey);
    ASSERT_TRUE(oat_bcp != nullptr);
    ASSERT_EQ(oat_bcp, android::base::Join(Runtime::Current()->GetBootClassPathLocations(), ':'));
    const char* checksums = oat_header.GetStoreValueByKey(OatHeader::kBootClassPathChecksumsKey);
    ASSERT_TRUE(checksums != nullptr);

    bool match = gc::space::ImageSpace::VerifyBootClassPathChecksums(
        checksums,
        oat_bcp,
        image_location,
        ArrayRef<const std::string>(Runtime::Current()->GetBootClassPathLocations()),
        ArrayRef<const std::string>(Runtime::Current()->GetBootClassPath()),
        kRuntimeISA,
        gc::space::ImageSpaceLoadingOrder::kSystemFirst,
        &error_msg);
    ASSERT_EQ(!with_alternate_image, match) << error_msg;
  }
}

void DexoptTest::GenerateOdexForTest(const std::string& dex_location,
                                     const std::string& odex_location,
                                     CompilerFilter::Filter filter,
                                     const char* compilation_reason,
                                     const std::vector<std::string>& extra_args) {
  GenerateOatForTest(dex_location,
                     odex_location,
                     filter,
                     /*with_alternate_image=*/ false,
                     compilation_reason,
                     extra_args);
}

void DexoptTest::GenerateOatForTest(const char* dex_location,
                                    CompilerFilter::Filter filter,
                                    bool with_alternate_image) {
  std::string oat_location;
  std::string error_msg;
  ASSERT_TRUE(OatFileAssistant::DexLocationToOatFilename(
        dex_location, kRuntimeISA, &oat_location, &error_msg)) << error_msg;
  GenerateOatForTest(dex_location,
                     oat_location,
                     filter,
                     with_alternate_image);
}

void DexoptTest::GenerateOatForTest(const char* dex_location, CompilerFilter::Filter filter) {
  GenerateOatForTest(dex_location, filter, /*with_alternate_image=*/ false);
}

void DexoptTest::ReserveImageSpace() {
  MemMap::Init();

  // Ensure a chunk of memory is reserved for the image space.
  uint64_t reservation_start = ART_BASE_ADDRESS;
  uint64_t reservation_end = ART_BASE_ADDRESS + 384 * MB;

  std::unique_ptr<BacktraceMap> map(BacktraceMap::Create(getpid(), true));
  ASSERT_TRUE(map.get() != nullptr) << "Failed to build process map";
  for (BacktraceMap::iterator it = map->begin();
      reservation_start < reservation_end && it != map->end(); ++it) {
    const backtrace_map_t* entry = *it;
    ReserveImageSpaceChunk(reservation_start, std::min(entry->start, reservation_end));
    reservation_start = std::max(reservation_start, entry->end);
  }
  ReserveImageSpaceChunk(reservation_start, reservation_end);
}

void DexoptTest::ReserveImageSpaceChunk(uintptr_t start, uintptr_t end) {
  if (start < end) {
    std::string error_msg;
    image_reservation_.push_back(MemMap::MapAnonymous("image reservation",
                                                      reinterpret_cast<uint8_t*>(start),
                                                      end - start,
                                                      PROT_NONE,
                                                      /*low_4gb=*/ false,
                                                      /*reuse=*/ false,
                                                      /*reservation=*/ nullptr,
                                                      &error_msg));
    ASSERT_TRUE(image_reservation_.back().IsValid()) << error_msg;
    LOG(INFO) << "Reserved space for image " <<
      reinterpret_cast<void*>(image_reservation_.back().Begin()) << "-" <<
      reinterpret_cast<void*>(image_reservation_.back().End());
  }
}

void DexoptTest::UnreserveImageSpace() {
  image_reservation_.clear();
}

}  // namespace art
