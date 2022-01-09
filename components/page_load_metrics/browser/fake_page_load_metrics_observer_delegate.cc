// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/page_load_metrics/browser/fake_page_load_metrics_observer_delegate.h"
#include "base/time/default_tick_clock.h"

namespace page_load_metrics {

FakePageLoadMetricsObserverDelegate::FakePageLoadMetricsObserverDelegate()
    : user_initiated_info_(UserInitiatedInfo::NotUserInitiated()),
      page_end_user_initiated_info_(UserInitiatedInfo::NotUserInitiated()),
      visibility_tracker_(base::DefaultTickClock::GetInstance(),
                          /*is_shown=*/true) {}
FakePageLoadMetricsObserverDelegate::~FakePageLoadMetricsObserverDelegate() =
    default;

content::WebContents* FakePageLoadMetricsObserverDelegate::GetWebContents()
    const {
  return web_contents_;
}

base::TimeTicks FakePageLoadMetricsObserverDelegate::GetNavigationStart()
    const {
  return base::TimeTicks();
}

absl::optional<base::TimeDelta>
FakePageLoadMetricsObserverDelegate::GetTimeToFirstBackground() const {
  return absl::optional<base::TimeDelta>();
}

absl::optional<base::TimeDelta>
FakePageLoadMetricsObserverDelegate::GetTimeToFirstForeground() const {
  return absl::optional<base::TimeDelta>();
}

const PageLoadMetricsObserverDelegate::BackForwardCacheRestore&
FakePageLoadMetricsObserverDelegate::GetBackForwardCacheRestore(
    size_t index) const {
  return back_forward_cache_restores_[index];
}

bool FakePageLoadMetricsObserverDelegate::StartedInForeground() const {
  return true;
}

bool FakePageLoadMetricsObserverDelegate::
    WasPrerenderedThenActivatedInForeground() const {
  return false;
}

const UserInitiatedInfo&
FakePageLoadMetricsObserverDelegate::GetUserInitiatedInfo() const {
  return user_initiated_info_;
}

const GURL& FakePageLoadMetricsObserverDelegate::GetUrl() const {
  return url_;
}

const GURL& FakePageLoadMetricsObserverDelegate::GetStartUrl() const {
  return start_url_;
}

bool FakePageLoadMetricsObserverDelegate::DidCommit() const {
  return false;
}

PageEndReason FakePageLoadMetricsObserverDelegate::GetPageEndReason() const {
  return page_end_reason_;
}

const UserInitiatedInfo&
FakePageLoadMetricsObserverDelegate::GetPageEndUserInitiatedInfo() const {
  return page_end_user_initiated_info_;
}

absl::optional<base::TimeDelta>
FakePageLoadMetricsObserverDelegate::GetTimeToPageEnd() const {
  return absl::optional<base::TimeDelta>();
}

const base::TimeTicks& FakePageLoadMetricsObserverDelegate::GetPageEndTime()
    const {
  return page_end_time_;
}

const mojom::FrameMetadata&
FakePageLoadMetricsObserverDelegate::GetMainFrameMetadata() const {
  return main_frame_metadata_;
}

const mojom::FrameMetadata&
FakePageLoadMetricsObserverDelegate::GetSubframeMetadata() const {
  return subframe_metadata_;
}

const PageRenderData& FakePageLoadMetricsObserverDelegate::GetPageRenderData()
    const {
  return page_render_data_;
}

const NormalizedCLSData&
FakePageLoadMetricsObserverDelegate::GetNormalizedCLSData(
    BfcacheStrategy bfcache_strategy) const {
  return normalized_cls_data_;
}

const NormalizedResponsivenessMetrics&
FakePageLoadMetricsObserverDelegate::GetNormalizedResponsivenessMetrics()
    const {
  return normalized_responsiveness_metrics_;
}

const mojom::InputTiming&
FakePageLoadMetricsObserverDelegate::GetPageInputTiming() const {
  return page_input_timing_;
}

const absl::optional<blink::MobileFriendliness>&
FakePageLoadMetricsObserverDelegate::GetMobileFriendliness() const {
  return mobile_friendliness_;
}

const PageRenderData&
FakePageLoadMetricsObserverDelegate::GetMainFrameRenderData() const {
  return main_frame_render_data_;
}

const ui::ScopedVisibilityTracker&
FakePageLoadMetricsObserverDelegate::GetVisibilityTracker() const {
  return visibility_tracker_;
}

const ResourceTracker& FakePageLoadMetricsObserverDelegate::GetResourceTracker()
    const {
  return resource_tracker_;
}

const LargestContentfulPaintHandler&
FakePageLoadMetricsObserverDelegate::GetLargestContentfulPaintHandler() const {
  return largest_contentful_paint_handler_;
}

const LargestContentfulPaintHandler& FakePageLoadMetricsObserverDelegate::
    GetExperimentalLargestContentfulPaintHandler() const {
  return experimental_largest_contentful_paint_handler_;
}

ukm::SourceId FakePageLoadMetricsObserverDelegate::GetPageUkmSourceId() const {
  return ukm::kInvalidSourceId;
}

bool FakePageLoadMetricsObserverDelegate::IsFirstNavigationInWebContents()
    const {
  return false;
}

void FakePageLoadMetricsObserverDelegate::AddBackForwardCacheRestore(
    PageLoadMetricsObserverDelegate::BackForwardCacheRestore bfcache_restore) {
  back_forward_cache_restores_.push_back(bfcache_restore);
}

void FakePageLoadMetricsObserverDelegate::ClearBackForwardCacheRestores() {
  back_forward_cache_restores_.clear();
}

}  // namespace page_load_metrics
