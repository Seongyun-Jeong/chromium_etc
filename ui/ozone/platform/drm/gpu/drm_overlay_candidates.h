// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_DRM_GPU_DRM_OVERLAY_CANDIDATES_H_
#define UI_OZONE_PLATFORM_DRM_GPU_DRM_OVERLAY_CANDIDATES_H_

#include <vector>

#include "ui/gfx/native_widget_types.h"
#include "ui/ozone/public/overlay_candidates_ozone.h"

namespace ui {
class DrmOverlayManager;
class OverlaySurfaceCandidate;

// OverlayCandidatesOzone implementation that delegates decisions to
// DrmOverlayManager.
class DrmOverlayCandidates : public OverlayCandidatesOzone {
 public:
  DrmOverlayCandidates(DrmOverlayManager* manager,
                       gfx::AcceleratedWidget widget);

  DrmOverlayCandidates(const DrmOverlayCandidates&) = delete;
  DrmOverlayCandidates& operator=(const DrmOverlayCandidates&) = delete;

  ~DrmOverlayCandidates() override;

  // OverlayCandidatesOzone:
  void CheckOverlaySupport(
      std::vector<OverlaySurfaceCandidate>* candidates) override;
  void RegisterOverlayRequirement(bool requires_overlay) override;

 private:
  DrmOverlayManager* const overlay_manager_;  // Not owned.
  const gfx::AcceleratedWidget widget_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_DRM_GPU_DRM_OVERLAY_CANDIDATES_H_
