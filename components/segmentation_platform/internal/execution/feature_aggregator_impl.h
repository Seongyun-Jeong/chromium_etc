// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_EXECUTION_FEATURE_AGGREGATOR_IMPL_H_
#define COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_EXECUTION_FEATURE_AGGREGATOR_IMPL_H_

#include <cstdint>
#include <vector>

#include "base/time/time.h"
#include "components/segmentation_platform/internal/database/signal_database.h"
#include "components/segmentation_platform/internal/execution/feature_aggregator.h"
#include "components/segmentation_platform/internal/proto/aggregation.pb.h"
#include "components/segmentation_platform/internal/proto/model_metadata.pb.h"
#include "components/segmentation_platform/internal/proto/types.pb.h"

namespace segmentation_platform {

// Core implementation of the FeatureAggregator.
class FeatureAggregatorImpl : public FeatureAggregator {
 public:
  FeatureAggregatorImpl();
  ~FeatureAggregatorImpl() override;

  // Disallow copy/assign.
  FeatureAggregatorImpl(const FeatureAggregatorImpl&) = delete;
  FeatureAggregatorImpl& operator=(const FeatureAggregatorImpl&) = delete;

  // FeatureAggregator overrides.
  std::vector<float> Process(
      proto::SignalType signal_type,
      proto::Aggregation aggregation,
      uint64_t bucket_count,
      const base::Time& end_time,
      const base::TimeDelta& bucket_duration,
      const std::vector<SignalDatabase::Sample>& samples) const override;
  void FilterEnumSamples(
      const std::vector<int32_t>& accepted_enum_ids,
      std::vector<SignalDatabase::Sample>& samples) const override;
};

}  // namespace segmentation_platform

#endif  // COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_EXECUTION_FEATURE_AGGREGATOR_IMPL_H_
