// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/drm/gpu/drm_overlay_manager.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/metrics/histogram_macros.h"
#include "base/trace_event/trace_event.h"
#include "ui/base/ui_base_features.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/ozone/platform/drm/gpu/drm_overlay_candidates.h"
#include "ui/ozone/public/overlay_surface_candidate.h"

namespace ui {
namespace {

// Maximum number of overlay configurations to keep in MRU cache.
constexpr size_t kMaxCacheSize = 100;

// How many times an overlay configuration needs to be requested before sending
// a query to display controller to see if the request will work. The overlay
// configuration will be rejected until a query is sent and response received.
constexpr int kThrottleRequestSize = 3;

// Returns |candidates| but with all NativePixmap pointers removed in order to
// avoid keeping them alive.
std::vector<OverlaySurfaceCandidate> ToCacheKey(
    const std::vector<OverlaySurfaceCandidate>& candidates) {
  std::vector<OverlaySurfaceCandidate> result = candidates;
  for (auto& candidate : result) {
    // Make sure the cache entry does not keep the NativePixmap alive.
    candidate.native_pixmap = nullptr;
  }
  return result;
}

}  // namespace

DrmOverlayManager::DrmOverlayManager(
    bool allow_sync_and_real_buffer_page_flip_testing) {
  allow_sync_and_real_buffer_page_flip_testing_ =
      allow_sync_and_real_buffer_page_flip_testing;
  DETACH_FROM_THREAD(thread_checker_);
}

DrmOverlayManager::~DrmOverlayManager() = default;

std::unique_ptr<OverlayCandidatesOzone>
DrmOverlayManager::CreateOverlayCandidates(gfx::AcceleratedWidget widget) {
  return std::make_unique<DrmOverlayCandidates>(this, widget);
}

void DrmOverlayManager::ResetCache() {
  TRACE_EVENT0("hwoverlays", "DrmOverlayManager::ResetCache");
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  widget_cache_map_.clear();
}

void DrmOverlayManager::CheckOverlaySupport(
    std::vector<OverlaySurfaceCandidate>* candidates,
    gfx::AcceleratedWidget widget) {
  TRACE_EVENT0("hwoverlays", "DrmOverlayManager::CheckOverlaySupport");
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // Check if another display has an overlay requirement, and if so do not
  // allow overlays. Some ChromeOS boards only support one overlay across all
  // displays so we want the overlay to go somewhere that requires it first vs.
  // a display that will just be using it as an optimization.
  if (!widgets_with_required_overlays_.empty() &&
      !widgets_with_required_overlays_.contains(widget)) {
    return;
  }

  std::vector<OverlaySurfaceCandidate> result_candidates;
  for (auto& candidate : *candidates) {
    bool can_handle = CanHandleCandidate(candidate, widget);

    // CanHandleCandidate() should never return false if the candidate is
    // the primary plane.
    DCHECK(can_handle || candidate.plane_z_order != 0);

    // If we can't handle the candidate in an overlay replace it with default
    // value. The quad might have a non-integer display rect which hits a
    // DCHECK when converting to gfx::Rect in the comparator.
    result_candidates.push_back(can_handle ? candidate
                                           : OverlaySurfaceCandidate());
    result_candidates.back().overlay_handled = can_handle;
  }

  if (allow_sync_and_real_buffer_page_flip_testing_ &&
      features::IsSynchronousPageFlipTestingEnabled()) {
    std::vector<OverlayStatus> status =
        SendOverlayValidationRequestSync(result_candidates, widget);
    size_t size = candidates->size();
    DCHECK_EQ(size, status.size());
    for (size_t i = 0; i < size; i++) {
      DCHECK(status[i] == OVERLAY_STATUS_ABLE ||
             status[i] == OVERLAY_STATUS_NOT);
      candidates->at(i).overlay_handled = status[i] == OVERLAY_STATUS_ABLE;
    }
    return;
  }

  auto widget_cache_map_it = widget_cache_map_.find(widget);
  if (widget_cache_map_it == widget_cache_map_.end()) {
    widget_cache_map_it =
        widget_cache_map_.emplace(widget, kMaxCacheSize).first;
  }
  OverlayCandidatesListCache& cache = widget_cache_map_it->second;
  std::vector<OverlaySurfaceCandidate> cache_key =
      ToCacheKey(result_candidates);
  auto iter = cache.Get(cache_key);
  if (iter == cache.end()) {
    // We can skip GPU side validation in case all candidates are invalid.
    bool needs_gpu_validation = std::any_of(
        result_candidates.begin(), result_candidates.end(),
        [](OverlaySurfaceCandidate& c) { return c.overlay_handled; });
    OverlayValidationCacheValue value;
    value.status.resize(result_candidates.size(), needs_gpu_validation
                                                      ? OVERLAY_STATUS_PENDING
                                                      : OVERLAY_STATUS_NOT);
    iter = cache.Put(cache_key, std::move(value));
  }

  bool cache_hit = false;
  OverlayValidationCacheValue& value = iter->second;
  if (value.request_num < kThrottleRequestSize) {
    value.request_num++;
  } else if (value.request_num == kThrottleRequestSize) {
    value.request_num++;
    if (value.status.back() == OVERLAY_STATUS_PENDING)
      SendOverlayValidationRequest(result_candidates, widget);
  } else if (value.status.back() != OVERLAY_STATUS_PENDING) {
    cache_hit = true;
    size_t size = candidates->size();
    const std::vector<OverlayStatus>& status = value.status;
    DCHECK_EQ(size, status.size());
    for (size_t i = 0; i < size; i++) {
      DCHECK(status[i] == OVERLAY_STATUS_ABLE ||
             status[i] == OVERLAY_STATUS_NOT);
      candidates->at(i).overlay_handled = status[i] == OVERLAY_STATUS_ABLE;
    }
  }
  UMA_HISTOGRAM_BOOLEAN("Compositing.Display.DrmOverlayManager.CacheHit",
                        cache_hit);
}

void DrmOverlayManager::RegisterOverlayRequirement(
    gfx::AcceleratedWidget widget,
    bool requires_overlay) {
  if (requires_overlay)
    widgets_with_required_overlays_.insert(widget);
  else
    widgets_with_required_overlays_.erase(widget);
}

bool DrmOverlayManager::CanHandleCandidate(
    const OverlaySurfaceCandidate& candidate,
    gfx::AcceleratedWidget widget) const {
  if (candidate.buffer_size.IsEmpty())
    return false;

  if (candidate.transform == gfx::OVERLAY_TRANSFORM_INVALID)
    return false;

  // The remaining checks are for ensuring consistency between GL compositing
  // and overlays. If we must use an overlay, then skip the remaining checks.
  if (candidate.requires_overlay)
    return true;

  // Reject candidates that don't fall on a pixel boundary.
  if (!gfx::IsNearestRectWithinDistance(candidate.display_rect, 0.01f)) {
    VLOG(3) << "Overlay Rejected: display_rect="
            << candidate.display_rect.ToString();
    return false;
  }

  // DRM supposedly supports subpixel source crop. However, according to
  // drm_plane_funcs.update_plane, devices which don't support that are
  // free to ignore the fractional part, and every device seems to do that as
  // of 5.4. So reject candidates that require subpixel source crop.
  gfx::RectF crop(candidate.crop_rect);
  crop.Scale(candidate.buffer_size.width(), candidate.buffer_size.height());
  if (!gfx::IsNearestRectWithinDistance(crop, 0.01f)) {
    VLOG(3) << "Overlay Rejected: crop=" << crop.ToString();
    return false;
  }

  if (candidate.clip_rect && !candidate.clip_rect->Contains(
                                 gfx::ToNearestRect(candidate.display_rect))) {
    VLOG(3) << "Overlay Rejected: clip_rect=" << candidate.clip_rect->ToString()
            << ", display_rect=" << candidate.display_rect.ToString();
    return false;
  }

  return true;
}

void DrmOverlayManager::UpdateCacheForOverlayCandidates(
    const std::vector<OverlaySurfaceCandidate>& candidates,
    const gfx::AcceleratedWidget widget,
    const std::vector<OverlayStatus>& status) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  auto widget_cache_map_it = widget_cache_map_.find(widget);
  if (widget_cache_map_it == widget_cache_map_.end())
    return;

  OverlayCandidatesListCache& cache = widget_cache_map_it->second;
  auto iter = cache.Peek(ToCacheKey(candidates));
  if (iter != cache.end())
    iter->second.status = status;
}

DrmOverlayManager::OverlayValidationCacheValue::OverlayValidationCacheValue() =
    default;
DrmOverlayManager::OverlayValidationCacheValue::OverlayValidationCacheValue(
    OverlayValidationCacheValue&&) = default;
DrmOverlayManager::OverlayValidationCacheValue::~OverlayValidationCacheValue() =
    default;
DrmOverlayManager::OverlayValidationCacheValue&
DrmOverlayManager::OverlayValidationCacheValue::operator=(
    OverlayValidationCacheValue&& other) = default;

}  // namespace ui
