// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_SELECTION_SEGMENT_SCORE_PROVIDER_H_
#define COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_SELECTION_SEGMENT_SCORE_PROVIDER_H_

#include "base/callback.h"
#include "components/optimization_guide/proto/models.pb.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

using optimization_guide::proto::OptimizationTarget;

namespace segmentation_platform {

class SegmentInfoDatabase;

// Result of a single segment.
// TODO(shaktisahu, ssid): Modify the result fields as the API evolves.
struct SegmentScore {
  // Raw score from the model.
  absl::optional<float> score;

  // Constructors.
  SegmentScore();
  SegmentScore(const SegmentScore& other);
  ~SegmentScore();
};

// Used for retrieving the result of a particular model. The results are read
// from the database on startup and never modified during the current session.
// Note that this class is currently unused, but can be used to serve future
// clients and be modified as needed.
class SegmentScoreProvider {
 public:
  SegmentScoreProvider() = default;
  virtual ~SegmentScoreProvider() = default;

  using SegmentScoreCallback = base::OnceCallback<void(const SegmentScore&)>;

  // Creates the instance.
  static std::unique_ptr<SegmentScoreProvider> Create(
      SegmentInfoDatabase* segment_database);

  // Called to initialize the manager. Reads results from the database into
  // memory on startup. Must be invoked before calling any other method.
  virtual void Initialize(base::OnceClosure callback) = 0;

  // Client API to get the score for a single segment. Returns the cached score
  // from the last session.
  // Note that there is no strong reason to keep this async, feel free to change
  // this to sync if needed.
  virtual void GetSegmentScore(OptimizationTarget segment_id,
                               SegmentScoreCallback callback) = 0;
};

}  // namespace segmentation_platform

#endif  // COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_SELECTION_SEGMENT_SCORE_PROVIDER_H_
