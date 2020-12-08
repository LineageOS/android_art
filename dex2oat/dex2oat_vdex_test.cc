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
                  const std::string* public_sdk,
                  const std::vector<std::string>& extra_args = {}) {
    std::vector<std::string> args;
    args.push_back("--dex-file=" + dex_location);
    args.push_back("--oat-file=" + odex_location);
    if (public_sdk != nullptr) {
      args.push_back("--public-sdk=" + *public_sdk);
    }
    args.push_back("--compiler-filter=" +
        CompilerFilter::NameOfFilter(CompilerFilter::Filter::kVerify));
    args.push_back("--runtime-arg");
    args.push_back("-Xnorelocate");
    args.push_back("--copy-dex-files=false");
    args.push_back("--runtime-arg");
    args.push_back("-verbose:verifier");
    // Use a single thread to facilitate debugging. We only compile tiny dex files.
    args.push_back("-j1");

    args.insert(args.end(), extra_args.begin(), extra_args.end());

    return Dex2Oat(args, &output_, &error_msg_) == 0;
  }

  std::unique_ptr<VerifierDeps> GetVerifierDeps(
        const std::string& vdex_location, const DexFile* dex_file) {
    // Verify the vdex file content: only the classes using public APIs should be verified.
    std::unique_ptr<VdexFile> vdex(VdexFile::Open(vdex_location.c_str(),
                                                  /*writable=*/ false,
                                                  /*low_4gb=*/ false,
                                                  /*unquicken=*/ false,
                                                  &error_msg_));
    // Check the vdex doesn't have dex.
    if (vdex->HasDexSection()) {
      ::testing::AssertionFailure() << "The vdex should not contain dex code";
    }

    // Verify the deps.
    VdexFile::VerifierDepsHeader vdex_header = vdex->GetVerifierDepsHeader();
    if (!vdex_header.IsValid()) {
      ::testing::AssertionFailure() << "Invalid vdex header";
    }

    std::vector<const DexFile*> dex_files;
    dex_files.push_back(dex_file);
    std::unique_ptr<VerifierDeps> deps(new VerifierDeps(dex_files, /*output_only=*/ false));

    if (!deps->ParseStoredData(dex_files, vdex->GetVerifierDepsData())) {
      ::testing::AssertionFailure() << error_msg_;
    }

    opened_vdex_files_.push_back(std::move(vdex));
    return deps;
  }

  uint16_t GetClassDefIndex(const std::string& cls, const DexFile& dex_file) {
    const dex::TypeId* type_id = dex_file.FindTypeId(cls.c_str());
    DCHECK(type_id != nullptr);
    dex::TypeIndex type_idx = dex_file.GetIndexForTypeId(*type_id);
    const dex::ClassDef* class_def = dex_file.FindClassDef(type_idx);
    DCHECK(class_def != nullptr);
    return dex_file.GetIndexForClassDef(*class_def);
  }

  bool HasVerifiedClass(const std::unique_ptr<VerifierDeps>& deps,
                        const std::string& cls,
                        const DexFile& dex_file) {
    uint16_t class_def_idx = GetClassDefIndex(cls, dex_file);
    return deps->GetVerifiedClasses(dex_file)[class_def_idx];
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

  std::string output_;
  std::string error_msg_;
  std::vector<std::unique_ptr<VdexFile>> opened_vdex_files_;
};

// Validates verification against public API stubs:
// - create a vdex file contraints by a predefined list of public API (passed as separate dex)
// - compile with the above vdex file as input to validate the compilation flow
TEST_F(Dex2oatVdexTest, VerifyPublicSdkStubs) {
  std::string error_msg;
  const std::string out_dir = GetScratchDir();
  const std::string odex_location = out_dir + "/base.oat";
  const std::string vdex_location = out_dir + "/base.vdex";

  // Dex2oatVdexTestDex is the subject app using normal APIs found in the boot classpath.
  std::unique_ptr<const DexFile> dex_file(OpenTestDexFile("Dex2oatVdexTestDex"));
  const std::string dex_location = dex_file->GetLocation();
  // Dex2oatVdexPublicSdkDex serves as the public API-stubs, restricting what can be verified.
  const std::string api_dex_location = GetTestDexFileName("Dex2oatVdexPublicSdkDex");

  // Compile the subject app using the predefined API-stubs
  ASSERT_TRUE(RunDex2oat(dex_location, odex_location, &api_dex_location));

  std::unique_ptr<VerifierDeps> deps = GetVerifierDeps(vdex_location, dex_file.get());

  // Verify public API usage. The classes should be verified.
  ASSERT_TRUE(HasVerifiedClass(deps, "LAccessPublicCtor;", *dex_file));
  ASSERT_TRUE(HasVerifiedClass(deps, "LAccessPublicMethod;", *dex_file));
  ASSERT_TRUE(HasVerifiedClass(deps, "LAccessPublicMethodFromParent;", *dex_file));
  ASSERT_TRUE(HasVerifiedClass(deps, "LAccessPublicStaticMethod;", *dex_file));
  ASSERT_TRUE(HasVerifiedClass(deps, "LAccessPublicStaticField;", *dex_file));

  // Verify NON public API usage. The classes should not ve verified.
  ASSERT_FALSE(HasVerifiedClass(deps, "LAccessNonPublicCtor;", *dex_file));
  ASSERT_FALSE(HasVerifiedClass(deps, "LAccessNonPublicMethod;", *dex_file));
  ASSERT_FALSE(HasVerifiedClass(deps, "LAccessNonPublicMethodFromParent;", *dex_file));
  ASSERT_FALSE(HasVerifiedClass(deps, "LAccessNonPublicStaticMethod;", *dex_file));

  // Accessing unresolved static fields do not lead to class verification
  // failures.
  // The linker will just throw NoSuchFieldError at runtime.
  ASSERT_TRUE(HasVerifiedClass(deps, "LAccessNonPublicStaticField;", *dex_file));

  // Compile again without public API stubs but with the previously generated vdex.
  // This simulates a normal install where the apk has its code pre-verified.
  // The results should be the same.

  std::string dm_file = out_dir + "/base.dm";
  CreateDexMetadata(vdex_location, dm_file);
  std::vector<std::string> extra_args;
  extra_args.push_back("--dm-file=" + dm_file);
  const std::string odex2_location = out_dir + "/base2.oat";
  const std::string vdex2_location = out_dir + "/base2.vdex";
  output_ = "";
  ASSERT_TRUE(RunDex2oat(dex_location, odex2_location, nullptr, extra_args));

  std::unique_ptr<VerifierDeps> deps2 = GetVerifierDeps(vdex_location, dex_file.get());

  ASSERT_TRUE(HasVerifiedClass(deps2, "LAccessPublicCtor;", *dex_file));
  ASSERT_TRUE(HasVerifiedClass(deps2, "LAccessPublicMethod;", *dex_file));
  ASSERT_TRUE(HasVerifiedClass(deps2, "LAccessPublicMethodFromParent;", *dex_file));
  ASSERT_TRUE(HasVerifiedClass(deps2, "LAccessPublicStaticMethod;", *dex_file));
  ASSERT_TRUE(HasVerifiedClass(deps2, "LAccessPublicStaticField;", *dex_file));

  ASSERT_FALSE(HasVerifiedClass(deps2, "LAccessNonPublicCtor;", *dex_file)) << output_;
  ASSERT_FALSE(HasVerifiedClass(deps2, "LAccessNonPublicMethod;", *dex_file));
  ASSERT_FALSE(HasVerifiedClass(deps2, "LAccessNonPublicMethodFromParent;", *dex_file));
  ASSERT_FALSE(HasVerifiedClass(deps2, "LAccessNonPublicStaticMethod;", *dex_file));
  ASSERT_TRUE(HasVerifiedClass(deps2, "LAccessNonPublicStaticField;", *dex_file));
}

}  // namespace art
