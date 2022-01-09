// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/page_load_metrics/browser/page_load_metrics_observer.h"

#include <utility>

#include "net/base/load_timing_info.h"

namespace {

int BucketWithOffsetAndUnit(int num, int offset, int unit) {
  // Bucketing raw number with `offset` centered.
  const int grid = (num - offset) / unit;
  const int bucketed =
      grid == 0 ? 0
                : grid > 0 ? std::pow(2, static_cast<int>(std::log2(grid)))
                           : -std::pow(2, static_cast<int>(std::log2(-grid)));
  return bucketed * unit + offset;
}

}  // namespace

namespace page_load_metrics {

int GetBucketedViewportInitialScale(const blink::MobileFriendliness& mf) {
  return mf.viewport_initial_scale_x10 <= -1
             ? -1
             : BucketWithOffsetAndUnit(mf.viewport_initial_scale_x10, 10, 2);
}

int GetBucketedViewportHardcodedWidth(const blink::MobileFriendliness& mf) {
  return mf.viewport_hardcoded_width <= -1
             ? -1
             : BucketWithOffsetAndUnit(mf.viewport_hardcoded_width, 500, 10);
}

MemoryUpdate::MemoryUpdate(content::GlobalRenderFrameHostId id, int64_t delta)
    : routing_id(id), delta_bytes(delta) {}

ExtraRequestCompleteInfo::ExtraRequestCompleteInfo(
    const url::Origin& origin_of_final_url,
    const net::IPEndPoint& remote_endpoint,
    int frame_tree_node_id,
    bool was_cached,
    int64_t raw_body_bytes,
    int64_t original_network_content_length,
    network::mojom::RequestDestination request_destination,
    int net_error,
    std::unique_ptr<net::LoadTimingInfo> load_timing_info)
    : origin_of_final_url(origin_of_final_url),
      remote_endpoint(remote_endpoint),
      frame_tree_node_id(frame_tree_node_id),
      was_cached(was_cached),
      raw_body_bytes(raw_body_bytes),
      original_network_content_length(original_network_content_length),
      request_destination(request_destination),
      net_error(net_error),
      load_timing_info(std::move(load_timing_info)) {}

ExtraRequestCompleteInfo::ExtraRequestCompleteInfo(
    const ExtraRequestCompleteInfo& other)
    : origin_of_final_url(other.origin_of_final_url),
      remote_endpoint(other.remote_endpoint),
      frame_tree_node_id(other.frame_tree_node_id),
      was_cached(other.was_cached),
      raw_body_bytes(other.raw_body_bytes),
      original_network_content_length(other.original_network_content_length),
      request_destination(other.request_destination),
      net_error(other.net_error),
      load_timing_info(other.load_timing_info == nullptr
                           ? nullptr
                           : std::make_unique<net::LoadTimingInfo>(
                                 *other.load_timing_info)) {}

ExtraRequestCompleteInfo::~ExtraRequestCompleteInfo() {}

FailedProvisionalLoadInfo::FailedProvisionalLoadInfo(base::TimeDelta interval,
                                                     net::Error error)
    : time_to_failed_provisional_load(interval), error(error) {}

FailedProvisionalLoadInfo::~FailedProvisionalLoadInfo() {}

PageLoadMetricsObserver::ObservePolicy PageLoadMetricsObserver::OnStart(
    content::NavigationHandle* navigation_handle,
    const GURL& currently_committed_url,
    bool started_in_foreground) {
  return CONTINUE_OBSERVING;
}

PageLoadMetricsObserver::ObservePolicy
PageLoadMetricsObserver::OnPrerenderStart(
    content::NavigationHandle* navigation_handle,
    const GURL& currently_committed_url) {
  return STOP_OBSERVING;
}

PageLoadMetricsObserver::ObservePolicy PageLoadMetricsObserver::OnRedirect(
    content::NavigationHandle* navigation_handle) {
  return CONTINUE_OBSERVING;
}

PageLoadMetricsObserver::ObservePolicy PageLoadMetricsObserver::OnCommit(
    content::NavigationHandle* navigation_handle,
    ukm::SourceId source_id) {
  return CONTINUE_OBSERVING;
}

PageLoadMetricsObserver::ObservePolicy PageLoadMetricsObserver::OnHidden(
    const mojom::PageLoadTiming& timing) {
  return CONTINUE_OBSERVING;
}

PageLoadMetricsObserver::ObservePolicy PageLoadMetricsObserver::OnShown() {
  return CONTINUE_OBSERVING;
}

PageLoadMetricsObserver::ObservePolicy
PageLoadMetricsObserver::OnEnterBackForwardCache(
    const mojom::PageLoadTiming& timing) {
  // Invoke OnComplete to ensure that recorded data is dumped.
  OnComplete(timing);
  return STOP_OBSERVING;
}

PageLoadMetricsObserver::ObservePolicy
PageLoadMetricsObserver::FlushMetricsOnAppEnterBackground(
    const mojom::PageLoadTiming& timing) {
  return CONTINUE_OBSERVING;
}

PageLoadMetricsObserver::ObservePolicy
PageLoadMetricsObserver::ShouldObserveMimeType(
    const std::string& mime_type) const {
  return IsStandardWebPageMimeType(mime_type) ? CONTINUE_OBSERVING
                                              : STOP_OBSERVING;
}

// static
bool PageLoadMetricsObserver::IsStandardWebPageMimeType(
    const std::string& mime_type) {
  return mime_type == "text/html" || mime_type == "application/xhtml+xml";
}

const PageLoadMetricsObserverDelegate& PageLoadMetricsObserver::GetDelegate()
    const {
  // The delegate must exist and outlive the page load metrics observer.
  DCHECK(delegate_);
  return *delegate_;
}

void PageLoadMetricsObserver::SetDelegate(
    PageLoadMetricsObserverDelegate* delegate) {
  delegate_ = delegate;
}

}  // namespace page_load_metrics
