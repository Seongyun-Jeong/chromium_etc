// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compute_pressure/cpu_core_speed_info.h"

#include <vector>

#include "content/browser/compute_pressure/compute_pressure_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

TEST(CpuCoreSpeedInfoTest, NormalizedSpeed_InvalidInputs) {
  std::vector<CpuCoreSpeedInfo> test_cases = {
      // -1 inputs.
      {-1, 3'800'000'000, 3'000'000'000, 1'000'000'000},
      {1'000'000'000, -1, 3'000'000'000, 1'000'000'000},
      {1'000'000'000, 3'800'000'000, 3'000'000'000, -1},

      // Inverted min/max frequencies.
      {3'800'000'000, 1'000'000'000, 3'000'000'000, 1'000'000'000},
      // Equal min/max frequencies. (no scaling)
      {1'000'000'000, 1'000'000'000, 1'000'000'000, 1'000'000'000},
  };

  for (const CpuCoreSpeedInfo& test_case : test_cases) {
    SCOPED_TRACE(test_case);
    EXPECT_EQ(false, test_case.IsValid());
  }
}

TEST(CpuCoreSpeedInfoTest, NormalizedSpeed_Math) {
  struct TestCase {
    int64_t min_frequency;
    int64_t max_frequency;
    int64_t base_frequency;
    int64_t current_frequency;
    double cpu_speed;
  };

  std::vector<TestCase> test_cases = {
      // Various points on the axis, with an explicit baseline.
      {1'000'000'000, 3'800'000'000, 3'000'000'000, 1'000'000'000, 0.0},
      {1'000'000'000, 3'800'000'000, 3'000'000'000, 1'500'000'000, 0.125},
      {1'000'000'000, 3'800'000'000, 3'000'000'000, 2'000'000'000, 0.25},
      {1'000'000'000, 3'800'000'000, 3'000'000'000, 2'500'000'000, 0.375},
      {1'000'000'000, 3'800'000'000, 3'000'000'000, 3'000'000'000, 0.5},
      {1'000'000'000, 3'800'000'000, 3'000'000'000, 3'200'000'000, 0.625},
      {1'000'000'000, 3'800'000'000, 3'000'000'000, 3'400'000'000, 0.75},
      {1'000'000'000, 3'800'000'000, 3'000'000'000, 3'600'000'000, 0.875},
      {1'000'000'000, 3'800'000'000, 3'000'000'000, 3'800'000'000, 1.0},

      // Various points on the axis, with no baseline.
      {1'000'000'000, 3'000'000'000, -1, 1'000'000'000, 0.0},
      {1'000'000'000, 3'000'000'000, -1, 1'250'000'000, 0.125},
      {1'000'000'000, 3'000'000'000, -1, 1'500'000'000, 0.25},
      {1'000'000'000, 3'000'000'000, -1, 1'750'000'000, 0.375},
      {1'000'000'000, 3'000'000'000, -1, 2'000'000'000, 0.5},
      {1'000'000'000, 3'000'000'000, -1, 2'250'000'000, 0.625},
      {1'000'000'000, 3'000'000'000, -1, 2'500'000'000, 0.75},
      {1'000'000'000, 3'000'000'000, -1, 2'750'000'000, 0.875},
      {1'000'000'000, 3'000'000'000, -1, 3'000'000'000, 1.0},

      // No speeds above baseline.
      {1'000'000'000, 3'000'000'000, 3'000'000'000, 1'000'000'000, 0.0},
      {1'000'000'000, 3'000'000'000, 3'000'000'000, 1'500'000'000, 0.125},
      {1'000'000'000, 3'000'000'000, 3'000'000'000, 2'000'000'000, 0.25},
      {1'000'000'000, 3'000'000'000, 3'000'000'000, 2'500'000'000, 0.375},
      {1'000'000'000, 3'000'000'000, 3'000'000'000, 3'000'000'000, 0.5},

      // No speeds below baseline.
      {1'000'000'000, 3'000'000'000, 1'000'000'000, 1'000'000'000, 0.5},
      {1'000'000'000, 3'000'000'000, 1'000'000'000, 1'500'000'000, 0.625},
      {1'000'000'000, 3'000'000'000, 1'000'000'000, 2'000'000'000, 0.75},
      {1'000'000'000, 3'000'000'000, 1'000'000'000, 2'500'000'000, 0.875},
      {1'000'000'000, 3'000'000'000, 1'000'000'000, 3'000'000'000, 1.0},

      // Minimum speed is zero, explicit baseline.
      {0, 2'800'000'000, 2'000'000'000, 0, 0.0},
      {0, 2'800'000'000, 2'000'000'000, 500'000'000, 0.125},
      {0, 2'800'000'000, 2'000'000'000, 1'000'000'000, 0.25},
      {0, 2'800'000'000, 2'000'000'000, 1'500'000'000, 0.375},
      {0, 2'800'000'000, 2'000'000'000, 2'000'000'000, 0.5},
      {0, 2'800'000'000, 2'000'000'000, 2'200'000'000, 0.625},
      {0, 2'800'000'000, 2'000'000'000, 2'400'000'000, 0.75},
      {0, 2'800'000'000, 2'000'000'000, 2'600'000'000, 0.875},
      {0, 2'800'000'000, 2'000'000'000, 2'800'000'000, 1.0},

      // Minimum speed is zero, no baseline.
      {0, 2'000'000'000, -1, 0, 0.0},
      {0, 2'000'000'000, -1, 250'000'000, 0.125},
      {0, 2'000'000'000, -1, 500'000'000, 0.25},
      {0, 2'000'000'000, -1, 750'000'000, 0.375},
      {0, 2'000'000'000, -1, 1'000'000'000, 0.5},
      {0, 2'000'000'000, -1, 1'250'000'000, 0.625},
      {0, 2'000'000'000, -1, 1'500'000'000, 0.75},
      {0, 2'000'000'000, -1, 1'750'000'000, 0.875},
      {0, 2'000'000'000, -1, 2'000'000'000, 1.0},

      // Baseline is zero.
      {0, 2'000'000'000, 0, 0, 0.5},
      {0, 2'000'000'000, 0, 500'000'000, 0.625},
      {0, 2'000'000'000, 0, 1'000'000'000, 0.75},
      {0, 2'000'000'000, 0, 1'500'000'000, 0.875},
      {0, 2'000'000'000, 0, 2'000'000'000, 1.0},

      // Capped to minimum frequency.
      {1'000'000'000, 3'800'000'000, 3'000'000'000, 100'000'000, 0.0},

      // Capped to maximum frequency.
      {1'000'000'000, 3'800'000'000, 3'000'000'000, 4'000'000'000, 1.0},
  };

  for (const TestCase& test_case : test_cases) {
    CpuCoreSpeedInfo info = {
        .min_frequency = test_case.min_frequency,
        .max_frequency = test_case.max_frequency,
        .base_frequency = test_case.base_frequency,
        .current_frequency = test_case.current_frequency,
    };

    SCOPED_TRACE(info);
    ASSERT_EQ(true, info.IsValid());
    EXPECT_EQ(test_case.cpu_speed, info.NormalizedSpeed());
  }
}

}  // namespace content
