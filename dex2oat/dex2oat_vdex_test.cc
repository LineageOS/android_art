/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "common_runtime_test.h"
#include "dex2oat_environment_test.h"

#include "vdex_file.h"
#include "verifier/verifier_deps.h"
#include "ziparchive/zip_writer.h"

namespace art {

using verifier::VerifierDeps;

class Dex2oatVdexTest : public Dex2oatEnvironmentTest {
 public:
  void TearDown() override {
    Dex2oatEnvironmentTest::TearDown();

    output_ = "";
    error_msg_ = "";
    opened_vdex_files_.clear();
  }

 protected:
  bool RunDex2oat(const std::string& dex_location,
                  const std::string& odex_location,
                  bool copy_dex_files = false,
                  const std::vector<std::string>& extra_args = {}) {
    std::vector<std::string> args;
    args.push_back("--dex-file=" + dex_location);
    args.push_back("--oat-file=" + odex_location);
    args.push_back("--compiler-filter=" +
        CompilerFilter::NameOfFilter(CompilerFilter::Filter::kVerify));
    args.push_back("--runtime-arg");
    args.push_back("-Xnorelocate");
    if (!copy_dex_files) {
      args.push_back("--copy-dex-files=false");
    }
    args.push_back("--runtime-arg");
    args.push_back("-verbose:verifier,compiler");
    // Use a single thread to facilitate debugging. We only compile tiny dex files.
    args.push_back("-j1");

    args.insert(args.end(), extra_args.begin(), extra_args.end());

    return Dex2Oat(args, &output_, &error_msg_) == 0;
  }

  void CreateDexMetadata(const std::string& vdex, const std::string& out_dm) {
    // Read the vdex bytes.
    std::unique_ptr<File> vdex_file(OS::OpenFileForReading(vdex.c_str()));
    std::vector<uint8_t> data(vdex_file->GetLength());
    ASSERT_TRUE(vdex_file->ReadFully(data.data(), data.size()));

    // Zip the content.
    FILE* file = fopen(out_dm.c_str(), "wb");
    ZipWriter writer(file);
    writer.StartEntry("primary.vdex", ZipWriter::kAlign32);
    writer.WriteBytes(data.data(), data.size());
    writer.FinishEntry();
    writer.Finish();
    fflush(file);
    fclose(file);
  }

  std::string GetFilename(const std::unique_ptr<const DexFile>& dex_file) {
    const std::string& str = dex_file->GetLocation();
    size_t idx = str.rfind('/');
    if (idx == std::string::npos) {
      return str;
    }
    return str.substr(idx + 1);
  }

  std::string GetOdex(const std::unique_ptr<const DexFile>& dex_file,
                      const std::string& suffix = "") {
    return GetScratchDir() + "/" + GetFilename(dex_file) + suffix + ".odex";
  }

  std::string GetVdex(const std::unique_ptr<const DexFile>& dex_file,
                      const std::string& suffix = "") {
    return GetScratchDir() + "/" + GetFilename(dex_file) + suffix + ".vdex";
  }

  std::string output_;
  std::string error_msg_;
  std::vector<std::unique_ptr<VdexFile>> opened_vdex_files_;
};

// Check that if the input dm does contain dex files then the compilation fails
TEST_F(Dex2oatVdexTest, VerifyPublicSdkStubsWithDexFiles) {
  std::string error_msg;

  // Dex2oatVdexTestDex is the subject app using normal APIs found in the boot classpath.
  std::unique_ptr<const DexFile> dex_file(OpenTestDexFile("Dex2oatVdexTestDex"));

  // Compile the subject app using the predefined API-stubs
  ASSERT_TRUE(RunDex2oat(
      dex_file->GetLocation(),
      GetOdex(dex_file),
      /*copy_dex_files=*/ true));

  // Create the .dm file with the output.
  std::string dm_file = GetScratchDir() + "/base.dm";
  CreateDexMetadata(GetVdex(dex_file), dm_file);
  std::vector<std::string> extra_args;
  extra_args.push_back("--dm-file=" + dm_file);

  // Recompile again with the .dm file which contains a vdex with code.
  // The compilation should fail.
  ASSERT_FALSE(RunDex2oat(
      dex_file->GetLocation(),
      GetOdex(dex_file, "v2"),
      /*copy_dex_files=*/ true,
      extra_args));
}

}  // namespace art
