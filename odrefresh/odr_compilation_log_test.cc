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

#include <odr_compilation_log.h>

#include <time.h>

#include <cstdint>
#include <ctime>
#include <iosfwd>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "android-base/file.h"
#include "base/common_art_test.h"
#include "odrefresh/odrefresh.h"
#include "odr_metrics.h"

namespace art {
namespace odrefresh {

const time_t kSecondsPerDay = 86'400;

class OdrCompilationLogTest : public CommonArtTest {};

TEST(OdrCompilationLogEntry, Equality) {
  OdrCompilationLogEntry a{1, 2, 3, 4, 5};

  ASSERT_EQ(a, (OdrCompilationLogEntry{1, 2, 3, 4, 5}));
  ASSERT_NE(a, (OdrCompilationLogEntry{9, 2, 3, 4, 5}));
  ASSERT_NE(a, (OdrCompilationLogEntry{1, 9, 3, 4, 5}));
  ASSERT_NE(a, (OdrCompilationLogEntry{1, 2, 9, 4, 5}));
  ASSERT_NE(a, (OdrCompilationLogEntry{2, 2, 3, 9, 5}));
  ASSERT_NE(a, (OdrCompilationLogEntry{2, 2, 3, 5, 9}));
}

TEST(OdrCompilationLogEntry, InputOutput) {
  const OdrCompilationLogEntry entries[] = {
      {1, 2, 3, 4, 5},
      {std::numeric_limits<int64_t>::min(),
       std::numeric_limits<int64_t>::min(),
       std::numeric_limits<int32_t>::min(),
       std::numeric_limits<time_t>::min(),
       std::numeric_limits<int32_t>::min()},
      {std::numeric_limits<int64_t>::max(),
       std::numeric_limits<int64_t>::max(),
       std::numeric_limits<int32_t>::max(),
       std::numeric_limits<time_t>::max(),
       std::numeric_limits<int32_t>::max()},
       {0, 0, 0, 0, 0},
      {0x7fedcba9'87654321, 0x5a5a5a5a'5a5a5a5a, 0x12345678, 0x2346789, 0x76543210}
  };
  for (const auto& entry : entries) {
    std::stringstream ss;
    ss << entry;
    OdrCompilationLogEntry actual;
    ss >> actual;
    ASSERT_EQ(entry, actual);
  }
}

TEST(OdrCompilationLogEntry, TruncatedInput) {
  std::stringstream ss;
  ss << "1 2";

  OdrCompilationLogEntry entry;
  ss >> entry;

  ASSERT_TRUE(ss.fail());
  ASSERT_FALSE(ss.bad());
}

TEST(OdrCompilationLogEntry, ReadMultiple) {
  std::stringstream ss;
  ss << "0 1 2 3 4\n5 6 7 8 9\n";

  OdrCompilationLogEntry entry0, entry1;
  ss >> entry0 >> entry1;
  ASSERT_EQ(entry0, (OdrCompilationLogEntry{0, 1, 2, 3, 4}));
  ASSERT_EQ(entry1, (OdrCompilationLogEntry{5, 6, 7, 8, 9}));

  ASSERT_FALSE(ss.fail());
  ASSERT_FALSE(ss.bad());
}

TEST(OdrCompilationLog, ShouldAttemptCompile) {
  OdrCompilationLog ocl(/*compilation_log_path=*/nullptr);

  ASSERT_TRUE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1, /*last_update_millis=*/762, OdrMetrics::Trigger::kMissingArtifacts, 0));

  ocl.Log(
      /*apex_version=*/1,
      /*last_update_millis=*/762,
      OdrMetrics::Trigger::kApexVersionMismatch,
      ExitCode::kCompilationSuccess);
  ASSERT_TRUE(ocl.ShouldAttemptCompile(
      /*apex_version=*/2, /*last_update_millis=*/762, OdrMetrics::Trigger::kApexVersionMismatch));
  ASSERT_TRUE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1, /*last_update_millis=*/10000, OdrMetrics::Trigger::kApexVersionMismatch));
  ASSERT_FALSE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1, /*last_update_millis=*/762, OdrMetrics::Trigger::kApexVersionMismatch));
  ASSERT_TRUE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1, /*last_update_millis=*/762, OdrMetrics::Trigger::kDexFilesChanged));
  ASSERT_FALSE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1, /*last_update_millis=*/762, OdrMetrics::Trigger::kUnknown));
}

TEST(OdrCompilationLog, BackOffNoHistory) {
  time_t start_time;
  time(&start_time);

  OdrCompilationLog ocl(/*compilation_log_path=*/nullptr);

  ASSERT_TRUE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time));

  // Start log
  ocl.Log(/*apex_version=*/1,
          /*last_update_millis=*/0,
          OdrMetrics::Trigger::kApexVersionMismatch,
          start_time,
          ExitCode::kCompilationFailed);
  ASSERT_FALSE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time));
  ASSERT_FALSE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + kSecondsPerDay / 2));
  ASSERT_TRUE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + kSecondsPerDay));

  // Add one more log entry
  ocl.Log(/*apex_version=*/1,
          /*last_update_millis=*/0,
          OdrMetrics::Trigger::kApexVersionMismatch,
          start_time,
          ExitCode::kCompilationFailed);
  ASSERT_FALSE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + kSecondsPerDay));
  ASSERT_TRUE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + 2 * kSecondsPerDay));

  // One more.
  ocl.Log(/*apex_version=*/1,
          /*last_update_millis=*/0,
          OdrMetrics::Trigger::kApexVersionMismatch,
          start_time,
          ExitCode::kCompilationFailed);
  ASSERT_FALSE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + 3 * kSecondsPerDay));
  ASSERT_TRUE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + 4 * kSecondsPerDay));

  // And one for the road.
  ocl.Log(/*apex_version=*/1,
          /*last_update_millis=*/0,
          OdrMetrics::Trigger::kApexVersionMismatch,
          start_time,
          ExitCode::kCompilationFailed);
  ASSERT_FALSE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + 7 * kSecondsPerDay));
  ASSERT_TRUE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + 8 * kSecondsPerDay));
}

TEST(OdrCompilationLog, BackOffHappyHistory) {
  time_t start_time;
  time(&start_time);

  OdrCompilationLog ocl(/*compilation_log_path=*/nullptr);

  // Start log with a successful entry.
  ocl.Log(/*apex_version=*/1,
          /*last_update_millis=*/0,
          OdrMetrics::Trigger::kApexVersionMismatch,
          start_time,
          ExitCode::kCompilationSuccess);
  ASSERT_FALSE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time));
  ASSERT_FALSE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + kSecondsPerDay / 4));
  ASSERT_TRUE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + kSecondsPerDay / 2));

  // Add a log entry for a failed compilation.
  ocl.Log(/*apex_version=*/1,
          /*last_update_millis=*/0,
          OdrMetrics::Trigger::kApexVersionMismatch,
          start_time,
          ExitCode::kCompilationFailed);
  ASSERT_FALSE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + kSecondsPerDay / 2));
  ASSERT_TRUE(ocl.ShouldAttemptCompile(
      /*apex_version=*/1,
      /*last_update_millis=*/0,
      OdrMetrics::Trigger::kApexVersionMismatch,
      start_time + kSecondsPerDay));
}

TEST_F(OdrCompilationLogTest, LogNumberOfEntriesAndPeek) {
  OdrCompilationLog ocl(/*compilation_log_path=*/nullptr);

  std::vector<OdrCompilationLogEntry> entries = {
    { 0, 1, 2, 3, 4 },
    { 1, 2, 3, 4, 5 },
    { 2, 3, 4, 5, 6 },
    { 3, 4, 5, 6, 7 },
    { 4, 5, 6, 7, 8 },
    { 5, 6, 7, 8, 9 },
    { 6, 7, 8, 9, 10 }
  };

  for (size_t i = 0; i < entries.size(); ++i) {
    OdrCompilationLogEntry& e = entries[i];
    ocl.Log(e.apex_version,
            e.last_update_millis,
            static_cast<OdrMetrics::Trigger>(e.trigger),
            e.when,
            static_cast<ExitCode>(e.exit_code));
    if (i < OdrCompilationLog::kMaxLoggedEntries) {
      ASSERT_EQ(i + 1, ocl.NumberOfEntries());
    } else {
      ASSERT_EQ(OdrCompilationLog::kMaxLoggedEntries, ocl.NumberOfEntries());
    }

    for (size_t j = 0; j < ocl.NumberOfEntries(); ++j) {
      const OdrCompilationLogEntry* logged = ocl.Peek(j);
      ASSERT_TRUE(logged != nullptr);
      const OdrCompilationLogEntry& expected = entries[i + 1 - ocl.NumberOfEntries() + j];
      ASSERT_EQ(expected, *logged);
    }
  }
}

TEST_F(OdrCompilationLogTest, LogReadWrite) {
  std::vector<OdrCompilationLogEntry> entries = {
    { 0, 1, 2, 3, 4 },
    { 1, 2, 3, 4, 5 },
    { 2, 3, 4, 5, 6 },
    { 3, 4, 5, 6, 7 },
    { 4, 5, 6, 7, 8 },
    { 5, 6, 7, 8, 9 },
    { 6, 7, 8, 9, 10 }
  };

  ScratchFile scratch_file;
  scratch_file.Close();

  for (size_t i = 0; i < entries.size(); ++i) {
    {
      OdrCompilationLog ocl(scratch_file.GetFilename().c_str());
      OdrCompilationLogEntry& e = entries[i];
      ocl.Log(e.apex_version,
              e.last_update_millis,
              static_cast<OdrMetrics::Trigger>(e.trigger),
              e.when,
              static_cast<ExitCode>(e.exit_code));
    }

    {
      OdrCompilationLog ocl(scratch_file.GetFilename().c_str());
      if (i < OdrCompilationLog::kMaxLoggedEntries) {
        ASSERT_EQ(i + 1, ocl.NumberOfEntries());
      } else {
        ASSERT_EQ(OdrCompilationLog::kMaxLoggedEntries, ocl.NumberOfEntries());
      }

      for (size_t j = 0; j < ocl.NumberOfEntries(); ++j) {
        const OdrCompilationLogEntry* logged = ocl.Peek(j);
        ASSERT_TRUE(logged != nullptr);
        const OdrCompilationLogEntry& expected = entries[i + 1 - ocl.NumberOfEntries() + j];
        ASSERT_EQ(expected, *logged);
      }
    }
  }
}

TEST_F(OdrCompilationLogTest, BackoffBasedOnLog) {
  time_t start_time;
  time(&start_time);

  ScratchFile scratch_file;
  scratch_file.Close();

  const char* log_path = scratch_file.GetFilename().c_str();
  {
    OdrCompilationLog ocl(log_path);

    ASSERT_TRUE(ocl.ShouldAttemptCompile(
        /*apex_version=*/1,
        /*last_update_millis=*/0,
        OdrMetrics::Trigger::kApexVersionMismatch,
        start_time));
  }

  {
    OdrCompilationLog ocl(log_path);

    // Start log
    ocl.Log(/*apex_version=*/1,
            /*last_update_millis=*/0,
            OdrMetrics::Trigger::kApexVersionMismatch,
            start_time,
            ExitCode::kCompilationFailed);
  }

  {
    OdrCompilationLog ocl(log_path);
    ASSERT_FALSE(ocl.ShouldAttemptCompile(
        /*apex_version=*/1,
        /*last_update_millis=*/0,
        OdrMetrics::Trigger::kApexVersionMismatch,
        start_time));
    ASSERT_FALSE(ocl.ShouldAttemptCompile(
        /*apex_version=*/1,
        /*last_update_millis=*/0,
        OdrMetrics::Trigger::kApexVersionMismatch,
        start_time + kSecondsPerDay / 2));
    ASSERT_TRUE(ocl.ShouldAttemptCompile(
        /*apex_version=*/1,
        /*last_update_millis=*/0,
        OdrMetrics::Trigger::kApexVersionMismatch,
        start_time + kSecondsPerDay));
  }

  {
    // Add one more log entry
    OdrCompilationLog ocl(log_path);
    ocl.Log(/*apex_version=*/1,
            /*last_update_millis=*/0,
            OdrMetrics::Trigger::kApexVersionMismatch,
            start_time,
            ExitCode::kCompilationFailed);
  }

  {
    OdrCompilationLog ocl(log_path);

    ASSERT_FALSE(ocl.ShouldAttemptCompile(
        /*apex_version=*/1,
        /*last_update_millis=*/0,
        OdrMetrics::Trigger::kApexVersionMismatch,
        start_time + kSecondsPerDay));
    ASSERT_TRUE(ocl.ShouldAttemptCompile(
        /*apex_version=*/1,
        /*last_update_millis=*/0,
        OdrMetrics::Trigger::kApexVersionMismatch,
        start_time + 2 * kSecondsPerDay));
  }

  {
    // One more log entry.
    OdrCompilationLog ocl(log_path);
    ocl.Log(/*apex_version=*/1,
            /*last_update_millis=*/0,
            OdrMetrics::Trigger::kApexVersionMismatch,
            start_time,
            ExitCode::kCompilationFailed);
  }

  {
    OdrCompilationLog ocl(log_path);
    ASSERT_FALSE(ocl.ShouldAttemptCompile(
        /*apex_version=*/1,
        /*last_update_millis=*/0,
        OdrMetrics::Trigger::kApexVersionMismatch,
        start_time + 3 * kSecondsPerDay));
    ASSERT_TRUE(ocl.ShouldAttemptCompile(
        /*apex_version=*/1,
        /*last_update_millis=*/0,
        OdrMetrics::Trigger::kApexVersionMismatch,
        start_time + 4 * kSecondsPerDay));
  }

  {
    // And one for the road.
    OdrCompilationLog ocl(log_path);
    ocl.Log(/*apex_version=*/1,
            /*last_update_millis=*/0,
            OdrMetrics::Trigger::kApexVersionMismatch,
            start_time,
            ExitCode::kCompilationFailed);
  }

  {
    OdrCompilationLog ocl(log_path);
    ASSERT_FALSE(ocl.ShouldAttemptCompile(
        /*apex_version=*/1,
        /*last_update_millis=*/0,
        OdrMetrics::Trigger::kApexVersionMismatch,
        start_time + 7 * kSecondsPerDay));
    ASSERT_TRUE(ocl.ShouldAttemptCompile(
        /*apex_version=*/1,
        /*last_update_millis=*/0,
        OdrMetrics::Trigger::kApexVersionMismatch,
        start_time + 8 * kSecondsPerDay));
  }
}

TEST(OdrCompilationLog, LastUpdateMillisChangeTriggersCompilation) {
  time_t start_time;
  time(&start_time);

  OdrCompilationLog ocl(/*compilation_log_path=*/nullptr);

  for (int64_t last_update_millis = 0; last_update_millis < 10000; last_update_millis += 1000) {
    static const int64_t kApexVersion = 19999;
    ASSERT_TRUE(ocl.ShouldAttemptCompile(
        kApexVersion, last_update_millis, OdrMetrics::Trigger::kApexVersionMismatch, start_time));
    ocl.Log(kApexVersion,
            last_update_millis,
            OdrMetrics::Trigger::kApexVersionMismatch,
            start_time,
            ExitCode::kCompilationSuccess);
    ASSERT_FALSE(ocl.ShouldAttemptCompile(kApexVersion,
                                          last_update_millis,
                                          OdrMetrics::Trigger::kApexVersionMismatch,
                                          start_time + 1));
  }
}

TEST(OdrCompilationLog, ApexVersionChangeTriggersCompilation) {
  time_t start_time;
  time(&start_time);

  OdrCompilationLog ocl(/*compilation_log_path=*/nullptr);

  for (int64_t apex_version = 0; apex_version < 10000; apex_version += 1000) {
    static const int64_t kLastUpdateMillis = 777;
    ASSERT_TRUE(ocl.ShouldAttemptCompile(apex_version,
                                         kLastUpdateMillis,
                                         OdrMetrics::Trigger::kApexVersionMismatch,
                                         start_time + 8 * kSecondsPerDay));
    ocl.Log(apex_version,
            kLastUpdateMillis,
            OdrMetrics::Trigger::kApexVersionMismatch,
            start_time,
            ExitCode::kCompilationSuccess);
    ASSERT_FALSE(ocl.ShouldAttemptCompile(apex_version,
                                          kLastUpdateMillis,
                                          OdrMetrics::Trigger::kApexVersionMismatch,
                                          start_time + 1));
  }
}

TEST_F(OdrCompilationLogTest, NewLogVersionTriggersCompilation) {
  static const int64_t kApexVersion = 1066;
  static const int64_t kLastUpdateMillis = 777;
  time_t start_time;
  time(&start_time);

  ScratchFile scratch_file;
  scratch_file.Close();

  // Generate a compilation log.
  {
    OdrCompilationLog ocl(scratch_file.GetFilename().c_str());
    for (size_t i = 0; i < OdrCompilationLog::kMaxLoggedEntries; ++i) {
      ocl.Log(kApexVersion,
              kLastUpdateMillis,
              OdrMetrics::Trigger::kApexVersionMismatch,
              start_time,
              ExitCode::kCompilationSuccess);
      ASSERT_FALSE(ocl.ShouldAttemptCompile(
          kApexVersion, kLastUpdateMillis, OdrMetrics::Trigger::kApexVersionMismatch, start_time));
    }
  }

  // Replace version string in the compilation log.
  std::string log_text;
  ASSERT_TRUE(android::base::ReadFileToString(scratch_file.GetFilename(), &log_text));
  std::string new_log_version = std::string(OdrCompilationLog::kLogVersion) + "a";
  log_text.replace(0, new_log_version.size() - 1, new_log_version);
  ASSERT_TRUE(android::base::WriteStringToFile(log_text, scratch_file.GetFilename()));

  // Read log with updated version entry, check it is treated as out-of-date.
  {
    OdrCompilationLog ocl(scratch_file.GetFilename().c_str());
    ASSERT_TRUE(ocl.ShouldAttemptCompile(
        kApexVersion, kLastUpdateMillis, OdrMetrics::Trigger::kApexVersionMismatch, start_time));
    ASSERT_EQ(0u, ocl.NumberOfEntries());
  }
}

}  // namespace odrefresh
}  // namespace art
