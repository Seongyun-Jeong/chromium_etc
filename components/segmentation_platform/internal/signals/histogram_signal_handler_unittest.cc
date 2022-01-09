// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/segmentation_platform/internal/signals/histogram_signal_handler.h"

#include "base/metrics/histogram_macros.h"
#include "base/metrics/metrics_hashes.h"
#include "base/test/task_environment.h"
#include "components/segmentation_platform/internal/database/mock_signal_database.h"
#include "components/segmentation_platform/internal/proto/types.pb.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Eq;

namespace segmentation_platform {

namespace {

constexpr char kExpectedHistogram[] = "some_histogram";
const uint64_t kExpectedHash = base::HashMetricName(kExpectedHistogram);

}  // namespace

class HistogramSignalHandlerTest : public testing::Test {
 public:
  HistogramSignalHandlerTest() = default;
  ~HistogramSignalHandlerTest() override = default;

  void SetUp() override {
    signal_database_ = std::make_unique<MockSignalDatabase>();
    histogram_signal_handler_ =
        std::make_unique<HistogramSignalHandler>(signal_database_.get());
  }

  void SetupHistograms() {
    std::set<std::pair<std::string, proto::SignalType>> histograms;
    histograms.insert(
        std::make_pair(kExpectedHistogram, proto::SignalType::HISTOGRAM_ENUM));
    histogram_signal_handler_->SetRelevantHistograms(histograms);
  }

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<MockSignalDatabase> signal_database_;
  std::unique_ptr<HistogramSignalHandler> histogram_signal_handler_;
};

TEST_F(HistogramSignalHandlerTest, HistogramsAreRecorded) {
  // Initialize and register the list of histograms we are listening to.
  histogram_signal_handler_->EnableMetrics(true);
  SetupHistograms();

  // Record a registered histogram sample. It should be recorded.
  EXPECT_CALL(*signal_database_, WriteSample(proto::SignalType::HISTOGRAM_ENUM,
                                             kExpectedHash, Eq(1), _));

  UMA_HISTOGRAM_BOOLEAN(kExpectedHistogram, true);
  task_environment_.RunUntilIdle();

  // Record an unrelated histogram sample. It should be ignored.
  std::string kUnrelatedHistogram = "unrelated_histogram";
  EXPECT_CALL(*signal_database_,
              WriteSample(_, base::HashMetricName(kUnrelatedHistogram), _, _))
      .Times(0);
  UMA_HISTOGRAM_BOOLEAN(kUnrelatedHistogram, true);
  task_environment_.RunUntilIdle();
}

TEST_F(HistogramSignalHandlerTest, DisableMetrics) {
  SetupHistograms();

  // Metrics is disabled on startup.
  EXPECT_CALL(*signal_database_, WriteSample(proto::SignalType::HISTOGRAM_ENUM,
                                             kExpectedHash, Eq(1), _))
      .Times(0);

  UMA_HISTOGRAM_BOOLEAN(kExpectedHistogram, true);
  task_environment_.RunUntilIdle();

  // Enable metrics.
  histogram_signal_handler_->EnableMetrics(true);
  EXPECT_CALL(*signal_database_, WriteSample(proto::SignalType::HISTOGRAM_ENUM,
                                             kExpectedHash, Eq(1), _))
      .Times(1);
  UMA_HISTOGRAM_BOOLEAN(kExpectedHistogram, true);
  task_environment_.RunUntilIdle();

  // Disable metrics again.
  histogram_signal_handler_->EnableMetrics(false);
  EXPECT_CALL(*signal_database_, WriteSample(proto::SignalType::HISTOGRAM_ENUM,
                                             kExpectedHash, Eq(1), _))
      .Times(0);
  UMA_HISTOGRAM_BOOLEAN(kExpectedHistogram, true);
  task_environment_.RunUntilIdle();

  // Enable metrics again.
  histogram_signal_handler_->EnableMetrics(true);
  EXPECT_CALL(*signal_database_, WriteSample(proto::SignalType::HISTOGRAM_ENUM,
                                             kExpectedHash, Eq(1), _))
      .Times(1);
  UMA_HISTOGRAM_BOOLEAN(kExpectedHistogram, true);
  task_environment_.RunUntilIdle();
}

}  // namespace segmentation_platform
