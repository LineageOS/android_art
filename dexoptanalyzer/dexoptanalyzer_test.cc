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

#include <gtest/gtest.h>

#include <string>

#include "arch/instruction_set.h"
#include "base/compiler_filter.h"
#include "dexopt_test.h"
#include "dexoptanalyzer.h"

namespace art {
namespace dexoptanalyzer {

class DexoptAnalyzerTest : public DexoptTest {
 protected:
  std::string GetDexoptAnalyzerCmd() {
    std::string file_path = GetArtBinDir() + "/dexoptanalyzer";
    if (kIsDebugBuild) {
      file_path += 'd';
    }
    EXPECT_TRUE(OS::FileExists(file_path.c_str())) << file_path << " should be a valid file path";
    return file_path;
  }

  int Analyze(const std::string& dex_file,
              CompilerFilter::Filter compiler_filter,
              ProfileAnalysisResult profile_analysis_result,
              const char* class_loader_context,
              bool downgrade = false) {
    std::string dexoptanalyzer_cmd = GetDexoptAnalyzerCmd();
    std::vector<std::string> argv_str;
    argv_str.push_back(dexoptanalyzer_cmd);
    argv_str.push_back("--dex-file=" + dex_file);
    argv_str.push_back("--isa=" + std::string(GetInstructionSetString(kRuntimeISA)));
    argv_str.push_back("--compiler-filter=" + CompilerFilter::NameOfFilter(compiler_filter));
    argv_str.push_back("--profile-analysis-result=" +
        std::to_string(static_cast<int>(profile_analysis_result)));
    if (downgrade) {
      argv_str.push_back("--downgrade");
    }

    argv_str.push_back("--runtime-arg");
    argv_str.push_back(GetClassPathOption("-Xbootclasspath:", GetLibCoreDexFileNames()));
    argv_str.push_back("--runtime-arg");
    argv_str.push_back(GetClassPathOption("-Xbootclasspath-locations:", GetLibCoreDexLocations()));
    argv_str.push_back("--image=" + GetImageLocation());
    argv_str.push_back("--android-data=" + android_data_);
    if (class_loader_context != nullptr) {
      argv_str.push_back("--class-loader-context=" + std::string(class_loader_context));
    }

    std::string error;
    return ExecAndReturnCode(argv_str, &error);
  }

  int DexoptanalyzerToOatFileAssistant(int dexoptanalyzerResult) {
    switch (dexoptanalyzerResult) {
      case 0: return OatFileAssistant::kNoDexOptNeeded;
      case 1: return OatFileAssistant::kDex2OatFromScratch;
      case 2: return OatFileAssistant::kDex2OatForBootImage;
      case 3: return OatFileAssistant::kDex2OatForFilter;
      case 4: return -OatFileAssistant::kDex2OatForBootImage;
      case 5: return -OatFileAssistant::kDex2OatForFilter;
      default: return dexoptanalyzerResult;
    }
  }

  // Verify that the output of dexoptanalyzer for the given arguments is the same
  // as the output of OatFileAssistant::GetDexOptNeeded.
  void Verify(const std::string& dex_file,
              CompilerFilter::Filter compiler_filter,
              ProfileAnalysisResult profile_analysis_result =
                  ProfileAnalysisResult::kDontOptimizeSmallDelta,
              bool downgrade = false,
              const char* class_loader_context = "PCL[]") {
    std::unique_ptr<ClassLoaderContext> context = class_loader_context == nullptr
        ? nullptr
        : ClassLoaderContext::Create(class_loader_context);
    if (context != nullptr) {
      std::vector<int> context_fds;
      ASSERT_TRUE(context->OpenDexFiles("", context_fds, /*only_read_checksums*/ true));
    }

    int dexoptanalyzerResult = Analyze(
        dex_file, compiler_filter, profile_analysis_result, class_loader_context, downgrade);
    dexoptanalyzerResult = DexoptanalyzerToOatFileAssistant(dexoptanalyzerResult);
    OatFileAssistant oat_file_assistant(dex_file.c_str(),
                                        kRuntimeISA,
                                        context.get(),
                                        /*load_executable=*/ false);
    bool assume_profile_changed = profile_analysis_result == ProfileAnalysisResult::kOptimize;
    int assistantResult = oat_file_assistant.GetDexOptNeeded(
        compiler_filter, assume_profile_changed, downgrade);
    EXPECT_EQ(assistantResult, dexoptanalyzerResult);
  }
};

// The tests below exercise the same test case from oat_file_assistant_test.cc.

// Case: We have a DEX file, but no ODEX file for it.
TEST_F(DexoptAnalyzerTest, DexNoOat) {
  std::string dex_location = GetScratchDir() + "/DexNoOat.jar";
  Copy(GetDexSrc1(), dex_location);

  Verify(dex_location, CompilerFilter::kSpeed);
  Verify(dex_location, CompilerFilter::kExtract);
  Verify(dex_location, CompilerFilter::kVerify);
  Verify(dex_location, CompilerFilter::kSpeedProfile);
  Verify(dex_location, CompilerFilter::kSpeed,
      ProfileAnalysisResult::kDontOptimizeSmallDelta, false, nullptr);
}

// Case: We have a DEX file and up-to-date ODEX file for it.
TEST_F(DexoptAnalyzerTest, OatUpToDate) {
  std::string dex_location = GetScratchDir() + "/OatUpToDate.jar";
  std::string odex_location = GetOdexDir() + "/OatUpToDate.odex";
  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location.c_str(), odex_location.c_str(), CompilerFilter::kSpeed);

  Verify(dex_location, CompilerFilter::kSpeed);
  Verify(dex_location, CompilerFilter::kVerify);
  Verify(dex_location, CompilerFilter::kExtract);
  Verify(dex_location, CompilerFilter::kEverything);
  Verify(dex_location, CompilerFilter::kSpeed,
      ProfileAnalysisResult::kDontOptimizeSmallDelta, false, nullptr);
}

// Case: We have a DEX file and speed-profile ODEX file for it.
TEST_F(DexoptAnalyzerTest, ProfileOatUpToDate) {
  std::string dex_location = GetScratchDir() + "/ProfileOatUpToDate.jar";
  std::string odex_location = GetOdexDir() + "/ProfileOatUpToDate.odex";
  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location.c_str(), odex_location.c_str(), CompilerFilter::kSpeedProfile);

  Verify(dex_location, CompilerFilter::kSpeedProfile,
      ProfileAnalysisResult::kDontOptimizeSmallDelta);
  Verify(dex_location, CompilerFilter::kVerify, ProfileAnalysisResult::kDontOptimizeSmallDelta);
  Verify(dex_location, CompilerFilter::kSpeedProfile, ProfileAnalysisResult::kOptimize);
  Verify(dex_location, CompilerFilter::kVerify, ProfileAnalysisResult::kOptimize);
}

// Case: We have a DEX file, verify odex file for it, and we ask if it's up to date
// when the profiles are empty or full.
TEST_F(DexoptAnalyzerTest, VerifyAndEmptyProfiles) {
  std::string dex_location = GetScratchDir() + "/VerifyAndEmptyProfiles.jar";
  std::string odex_location = GetOdexDir() + "/VerifyAndEmptyProfiles.odex";
  Copy(GetDexSrc1(), dex_location);

  GenerateOdexForTest(dex_location.c_str(), odex_location.c_str(), CompilerFilter::kVerify);

  // If we want to speed-profile something that was verified, do it even if
  // the profile analysis returns kDontOptimizeSmallDelta (it means that we do have profile data,
  // so a transition verify -> speed-profile is still worth).
  ASSERT_EQ(
      static_cast<int>(ReturnCode::kDex2OatForFilterOdex),
      Analyze(dex_location, CompilerFilter::kSpeedProfile,
          ProfileAnalysisResult::kDontOptimizeSmallDelta, "PCL[]"));
  // If we want to speed-profile something that was verified but the profiles are empty,
  // don't do it - there will be no gain.
  ASSERT_EQ(
      static_cast<int>(ReturnCode::kNoDexOptNeeded),
      Analyze(dex_location, CompilerFilter::kSpeedProfile,
          ProfileAnalysisResult::kDontOptimizeEmptyProfiles, "PCL[]"));
  // Standard case where we need to re-compile a speed-profile because of sufficient new
  // information in the profile.
  ASSERT_EQ(
      static_cast<int>(ReturnCode::kDex2OatForFilterOdex),
      Analyze(dex_location, CompilerFilter::kSpeedProfile,
          ProfileAnalysisResult::kOptimize, "PCL[]"));
}

TEST_F(DexoptAnalyzerTest, Downgrade) {
  std::string dex_location = GetScratchDir() + "/Downgrade.jar";
  std::string odex_location = GetOdexDir() + "/Downgrade.odex";
  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location.c_str(), odex_location.c_str(), CompilerFilter::kVerify);

  Verify(dex_location, CompilerFilter::kSpeedProfile,
      ProfileAnalysisResult::kDontOptimizeSmallDelta, true);
  Verify(dex_location, CompilerFilter::kVerify,
      ProfileAnalysisResult::kDontOptimizeSmallDelta, true);
  Verify(dex_location, CompilerFilter::kExtract,
      ProfileAnalysisResult::kDontOptimizeSmallDelta, true);
}

// Case: We have a MultiDEX file and up-to-date ODEX file for it.
TEST_F(DexoptAnalyzerTest, MultiDexOatUpToDate) {
  std::string dex_location = GetScratchDir() + "/MultiDexOatUpToDate.jar";
  std::string odex_location = GetOdexDir() + "/MultiDexOatUpToDate.odex";

  Copy(GetMultiDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location.c_str(), odex_location.c_str(), CompilerFilter::kSpeed);

  Verify(dex_location, CompilerFilter::kSpeed, ProfileAnalysisResult::kDontOptimizeSmallDelta);
}

// Case: We have a MultiDEX file where the secondary dex file is out of date.
TEST_F(DexoptAnalyzerTest, MultiDexSecondaryOutOfDate) {
  std::string dex_location = GetScratchDir() + "/MultiDexSecondaryOutOfDate.jar";
  std::string odex_location = GetOdexDir() + "/MultiDexSecondaryOutOfDate.odex";

  // Compile code for GetMultiDexSrc1.
  Copy(GetMultiDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location.c_str(), odex_location.c_str(), CompilerFilter::kSpeed);

  // Now overwrite the dex file with GetMultiDexSrc2 so the secondary checksum
  // is out of date.
  Copy(GetMultiDexSrc2(), dex_location);

  Verify(dex_location, CompilerFilter::kSpeed, ProfileAnalysisResult::kDontOptimizeSmallDelta);
}

// Case: We have a DEX file and an ODEX file out of date with respect to the
// dex checksum.
TEST_F(DexoptAnalyzerTest, OatDexOutOfDate) {
  std::string dex_location = GetScratchDir() + "/OatDexOutOfDate.jar";
  std::string odex_location = GetOdexDir() + "/OatDexOutOfDate.odex";

  // We create a dex, generate an oat for it, then overwrite the dex with a
  // different dex to make the oat out of date.
  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location.c_str(), odex_location.c_str(), CompilerFilter::kSpeed);
  Copy(GetDexSrc2(), dex_location);

  Verify(dex_location, CompilerFilter::kExtract);
  Verify(dex_location, CompilerFilter::kSpeed);
}

// Case: We have a DEX file and an ODEX file out of date with respect to the
// boot image.
TEST_F(DexoptAnalyzerTest, OatImageOutOfDate) {
  std::string dex_location = GetScratchDir() + "/OatImageOutOfDate.jar";
  std::string odex_location = GetOdexDir() + "/OatImageOutOfDate.odex";

  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(),
                     odex_location.c_str(),
                     CompilerFilter::kSpeed,
                     /*with_alternate_image=*/true);

  Verify(dex_location, CompilerFilter::kExtract);
  Verify(dex_location, CompilerFilter::kVerify);
  Verify(dex_location, CompilerFilter::kSpeed);
}

// Case: We have a DEX file and a verify-at-runtime OAT file out of date with
// respect to the boot image.
// It shouldn't matter that the OAT file is out of date, because it is
// verify-at-runtime.
TEST_F(DexoptAnalyzerTest, OatVerifyAtRuntimeImageOutOfDate) {
  std::string dex_location = GetScratchDir() + "/OatVerifyAtRuntimeImageOutOfDate.jar";
  std::string odex_location = GetOdexDir() + "/OatVerifyAtRuntimeImageOutOfDate.odex";

  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(),
                     odex_location.c_str(),
                     CompilerFilter::kExtract,
                     /*with_alternate_image=*/true);

  Verify(dex_location, CompilerFilter::kExtract);
  Verify(dex_location, CompilerFilter::kVerify);
}

// Case: We have a DEX file and an ODEX file, but no OAT file.
TEST_F(DexoptAnalyzerTest, DexOdexNoOat) {
  std::string dex_location = GetScratchDir() + "/DexOdexNoOat.jar";
  std::string odex_location = GetOdexDir() + "/DexOdexNoOat.odex";

  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed);

  Verify(dex_location, CompilerFilter::kExtract);
  Verify(dex_location, CompilerFilter::kSpeed);
  Verify(dex_location, CompilerFilter::kEverything);
}

// Case: We have a stripped (or resource-only) DEX file, no ODEX file and no
// OAT file. Expect: The status is kNoDexOptNeeded.
TEST_F(DexoptAnalyzerTest, ResourceOnlyDex) {
  std::string dex_location = GetScratchDir() + "/ResourceOnlyDex.jar";

  Copy(GetResourceOnlySrc1(), dex_location);

  Verify(dex_location, CompilerFilter::kSpeed);
  Verify(dex_location, CompilerFilter::kExtract);
  Verify(dex_location, CompilerFilter::kVerify);
}

// Case: We have a DEX file, an ODEX file and an OAT file.
TEST_F(DexoptAnalyzerTest, OdexOatOverlap) {
  std::string dex_location = GetScratchDir() + "/OdexOatOverlap.jar";
  std::string odex_location = GetOdexDir() + "/OdexOatOverlap.odex";
  std::string oat_location = GetOdexDir() + "/OdexOatOverlap.oat";

  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed);

  // Create the oat file by copying the odex so they are located in the same
  // place in memory.
  Copy(odex_location, oat_location);

  Verify(dex_location, CompilerFilter::kSpeed);
}

// Case: We have a DEX file and a VerifyAtRuntime ODEX file, but no OAT file..
TEST_F(DexoptAnalyzerTest, DexVerifyAtRuntimeOdexNoOat) {
  std::string dex_location = GetScratchDir() + "/DexVerifyAtRuntimeOdexNoOat.jar";
  std::string odex_location = GetOdexDir() + "/DexVerifyAtRuntimeOdexNoOat.odex";

  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kExtract);

  Verify(dex_location, CompilerFilter::kExtract);
  Verify(dex_location, CompilerFilter::kSpeed);
}

// Case: Non-standard extension for dex file.
TEST_F(DexoptAnalyzerTest, LongDexExtension) {
  std::string dex_location = GetScratchDir() + "/LongDexExtension.jarx";
  Copy(GetDexSrc1(), dex_location);

  Verify(dex_location, CompilerFilter::kSpeed);
}

// Case: Very short, non-existent Dex location.
TEST_F(DexoptAnalyzerTest, ShortDexLocation) {
  std::string dex_location = "/xx";

  Verify(dex_location, CompilerFilter::kSpeed);
}

// Case: We have a DEX file and up-to-date OAT file for it, and we check with
// a class loader context.
TEST_F(DexoptAnalyzerTest, ClassLoaderContext) {
  std::string dex_location1 = GetScratchDir() + "/DexToAnalyze.jar";
  std::string odex_location1 = GetOdexDir() + "/DexToAnalyze.odex";
  std::string dex_location2 = GetScratchDir() + "/DexInContext.jar";
  Copy(GetDexSrc1(), dex_location1);
  Copy(GetDexSrc2(), dex_location2);

  std::string class_loader_context = "PCL[" + dex_location2 + "]";
  std::string class_loader_context_option = "--class-loader-context=PCL[" + dex_location2 + "]";

  // Generate the odex to get the class loader context also open the dex files.
  GenerateOdexForTest(dex_location1, odex_location1, CompilerFilter::kSpeed, /* compilation_reason= */ nullptr, /* extra_args= */ { class_loader_context_option });

  Verify(dex_location1, CompilerFilter::kSpeed, ProfileAnalysisResult::kDontOptimizeSmallDelta,
      false, class_loader_context.c_str());
}

}  // namespace dexoptanalyzer
}  // namespace art
