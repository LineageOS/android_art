/*
 * Copyright (C) 2019 The Android Open Source Project
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
#include <stdio.h>

#include "base/arena_allocator.h"
#include "base/common_art_test.h"
#include "base/unix_file/fd_file.h"
#include "dex/dex_file.h"
#include "profile/profile_boot_info.h"

namespace art {

class ProfileBootInfoTest : public CommonArtTest {
 public:
  void SetUp() override {
    CommonArtTest::SetUp();
  }
};


TEST_F(ProfileBootInfoTest, LoadEmpty) {
  ScratchFile profile;
  std::vector<const DexFile*> dex_files;

  ProfileBootInfo loaded_info;
  ASSERT_TRUE(loaded_info.IsEmpty());
  ASSERT_TRUE(loaded_info.Load(profile.GetFd(), dex_files));
  ASSERT_TRUE(loaded_info.IsEmpty());
}

TEST_F(ProfileBootInfoTest, OneMethod) {
  ScratchFile profile;
  std::unique_ptr<const DexFile> dex(OpenTestDexFile("ManyMethods"));
  std::vector<const DexFile*> dex_files = { dex.get() };

  ProfileBootInfo saved_info;
  saved_info.Add(dex.get(), 0);
  ASSERT_TRUE(saved_info.Save(profile.GetFd()));
  ASSERT_TRUE(profile.GetFile()->ResetOffset());

  ProfileBootInfo loaded_info;
  ASSERT_TRUE(loaded_info.Load(profile.GetFd(), dex_files));
  ASSERT_EQ(loaded_info.GetDexFiles().size(), 1u);
  ASSERT_STREQ(loaded_info.GetDexFiles()[0]->GetLocation().c_str(), dex->GetLocation().c_str());
  ASSERT_EQ(loaded_info.GetMethods().size(), 1u);
  ASSERT_EQ(loaded_info.GetMethods()[0].first, 0u);
  ASSERT_EQ(loaded_info.GetMethods()[0].second, 0u);
}

TEST_F(ProfileBootInfoTest, ManyDexFiles) {
  ScratchFile profile;
  std::vector<std::unique_ptr<const DexFile>> dex_files = OpenTestDexFiles("MultiDex");
  std::vector<const DexFile*> dex_files2;
  for (const std::unique_ptr<const DexFile>& file : dex_files) {
    dex_files2.push_back(file.get());
  }

  ProfileBootInfo saved_info;
  saved_info.Add(dex_files[0].get(), 42);
  saved_info.Add(dex_files[1].get(), 108);
  saved_info.Add(dex_files[1].get(), 54);
  ASSERT_TRUE(saved_info.Save(profile.GetFd()));
  ASSERT_TRUE(profile.GetFile()->ResetOffset());

  ProfileBootInfo loaded_info;
  ASSERT_TRUE(loaded_info.Load(profile.GetFd(), dex_files2));
  ASSERT_EQ(loaded_info.GetDexFiles().size(), 2u);
  ASSERT_STREQ(loaded_info.GetDexFiles()[0]->GetLocation().c_str(),
               dex_files[0]->GetLocation().c_str());
  ASSERT_EQ(loaded_info.GetMethods().size(), 3u);
  ASSERT_EQ(loaded_info.GetMethods()[0].first, 0u);
  ASSERT_EQ(loaded_info.GetMethods()[0].second, 42u);
  ASSERT_EQ(loaded_info.GetMethods()[1].first, 1u);
  ASSERT_EQ(loaded_info.GetMethods()[1].second, 108u);
  ASSERT_EQ(loaded_info.GetMethods()[2].first, 1u);
  ASSERT_EQ(loaded_info.GetMethods()[2].second, 54u);
}

TEST_F(ProfileBootInfoTest, LoadWrongDexFile) {
  ScratchFile profile;
  std::unique_ptr<const DexFile> dex(OpenTestDexFile("ManyMethods"));

  ProfileBootInfo saved_info;
  saved_info.Add(dex.get(), 42);
  ASSERT_TRUE(saved_info.Save(profile.GetFd()));


  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ProfileBootInfo loaded_info;
  std::vector<std::unique_ptr<const DexFile>> dex_files = OpenTestDexFiles("MultiDex");
  std::vector<const DexFile*> dex_files2;
  for (const std::unique_ptr<const DexFile>& file : dex_files) {
    dex_files2.push_back(file.get());
  }
  ASSERT_FALSE(loaded_info.Load(profile.GetFd(), dex_files2));
}

}  // namespace art
