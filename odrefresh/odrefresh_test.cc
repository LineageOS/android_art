/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "odrefresh/odrefresh.h"

#include "base/common_art_test.h"
#include "base/file_utils.h"

namespace art {
namespace odrefresh {

TEST(OdRefreshTest, OdrefreshArtifactDirectory) {
    // odrefresh.h defines kOdrefreshArtifactDirectory for external callers of odrefresh. This is
    // where compilation artifacts end up.
    ScopedUnsetEnvironmentVariable no_env("ART_APEX_DATA");
    EXPECT_EQ(kOdrefreshArtifactDirectory, GetArtApexData() + "/dalvik-cache");
}

}  // namespace odrefresh
}  // namespace art
