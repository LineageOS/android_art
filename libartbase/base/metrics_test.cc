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

#include "metrics.h"

#include "gtest/gtest.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {
namespace metrics {

class MetricsTest : public testing::Test {};

TEST_F(MetricsTest, SimpleCounter) {
  MetricsCounter test_counter;

  EXPECT_EQ(0u, test_counter.Value());

  test_counter.AddOne();
  EXPECT_EQ(1u, test_counter.Value());

  test_counter.Add(5);
  EXPECT_EQ(6u, test_counter.Value());
}

TEST_F(MetricsTest, DatumName) {
  EXPECT_EQ("ClassVerificationTotalTime", DatumName(DatumId::kClassVerificationTotalTime));
}

}  // namespace metrics
}  // namespace art

#pragma clang diagnostic pop  // -Wconversion
