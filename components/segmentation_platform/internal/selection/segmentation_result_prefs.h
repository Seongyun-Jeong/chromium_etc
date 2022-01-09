// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_SELECTION_SEGMENTATION_RESULT_PREFS_H_
#define COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_SELECTION_SEGMENTATION_RESULT_PREFS_H_

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "components/optimization_guide/proto/models.pb.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

using optimization_guide::proto::OptimizationTarget;

class PrefService;

namespace segmentation_platform {

// Struct containing information about the selected segment. Convenient for
// reading and writing to prefs.
struct SelectedSegment {
 public:
  explicit SelectedSegment(OptimizationTarget segment_id);
  ~SelectedSegment();

  // The segment selection result.
  OptimizationTarget segment_id;

  // The time when the segment was selected.
  base::Time selection_time;

  // Whether or not the segment selection result is in use.
  bool in_use;
};

// Stores the result of segmentation into prefs for faster lookup. The result
// consists of (1) The selected segment ID. (2) The time when the segment was
// first selected. Used to enforce segment selection TTL. (3) Whether the
// selected segment has started to be used by clients.
class SegmentationResultPrefs {
 public:
  explicit SegmentationResultPrefs(PrefService* pref_service);
  virtual ~SegmentationResultPrefs() = default;

  // Disallow copy/assign.
  SegmentationResultPrefs(const SegmentationResultPrefs& other) = delete;
  SegmentationResultPrefs operator=(const SegmentationResultPrefs& other) =
      delete;

  // Writes the selected segment to prefs. Deletes the previous results if
  // |selected_segment| is empty.
  virtual void SaveSegmentationResultToPref(
      const std::string& result_key,
      const absl::optional<SelectedSegment>& selected_segment);

  // Reads the selected segment from pref, if any.
  virtual absl::optional<SelectedSegment> ReadSegmentationResultFromPref(
      const std::string& result_key);

 private:
  raw_ptr<PrefService> prefs_;
};

}  // namespace segmentation_platform

#endif  // COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_SELECTION_SEGMENTATION_RESULT_PREFS_H_
