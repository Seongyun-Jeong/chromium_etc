// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/core/amp_page_load_metrics_observer.h"

#include <algorithm>
#include <string>

#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "components/page_load_metrics/browser/observers/core/largest_contentful_paint_handler.h"
#include "components/page_load_metrics/browser/page_load_metrics_util.h"
#include "components/page_load_metrics/common/page_load_timing.h"
#include "components/page_load_metrics/common/page_visit_final_status.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "net/base/url_util.h"
#include "services/metrics/public/cpp/metrics_utils.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/features.h"
#include "url/gurl.h"

namespace {

const char kHistogramPrefix[] = "PageLoad.Clients.AMP.";

const char kHistogramAMPSubframeNavigationToInput[] =
    "Experimental.PageTiming.NavigationToInput.Subframe";
const char kHistogramAMPSubframeInputToNavigation[] =
    "Experimental.PageTiming.InputToNavigation.Subframe";
const char kHistogramAMPSubframeMainFrameToSubFrameNavigation[] =
    "Experimental.PageTiming.MainFrameToSubFrameNavigationDelta.Subframe";
const char kHistogramAMPSubframeFirstContentfulPaint[] =
    "PaintTiming.InputToFirstContentfulPaint.Subframe";
const char kHistogramAMPSubframeFirstContentfulPaintFullNavigation[] =
    "PaintTiming.InputToFirstContentfulPaint.Subframe.FullNavigation";
const char kHistogramAMPSubframeLargestContentfulPaint[] =
    "PaintTiming.InputToLargestContentfulPaint.Subframe";
const char kHistogramAMPSubframeLargestContentfulPaintFullNavigation[] =
    "PaintTiming.InputToLargestContentfulPaint.Subframe.FullNavigation";
const char kHistogramAMPSubframeFirstInputDelay[] =
    "InteractiveTiming.FirstInputDelay4.Subframe";
const char kHistogramAMPSubframeFirstInputDelayFullNavigation[] =
    "InteractiveTiming.FirstInputDelay4.Subframe.FullNavigation";
const char kHistogramAMPSubframeLayoutInstabilityShiftScore[] =
    "LayoutInstability.CumulativeShiftScore.Subframe";
const char kHistogramAMPSubframeLayoutInstabilityShiftScoreFullNavigation[] =
    "LayoutInstability.CumulativeShiftScore.Subframe.FullNavigation";

const char
    kHistogramAMPSubframeAverageUserInteractionLatencyOverBudgetMaxEventDuration
        [] = "InteractiveTiming.AverageUserInteractionLatencyOverBudget."
             "MaxEventDuration.Subframe";
const char
    kHistogramAMPSubframeSlowUserInteractionLatencyOverBudgetHighPercentileMaxEventDuration
        [] = "InteractiveTiming.SlowUserInteractionLatencyOverBudget."
             "HighPercentile.MaxEventDuration.Subframe";
const char
    kHistogramAMPSubframeSlowUserInteractionLatencyOverBudgetHighPercentile2MaxEventDuration
        [] = "InteractiveTiming.SlowUserInteractionLatencyOverBudget."
             "HighPercentile2.MaxEventDuration.Subframe";
const char
    kHistogramAMPSubframeSumOfUserInteractionLatencyOverBudgetMaxEventDuration
        [] = "InteractiveTiming.SumOfUserInteractionLatencyOverBudget."
             "MaxEventDuration.Subframe";
const char kHistogramAMPSubframeWorstUserInteractionLatencyMaxEventDuration[] =
    "InteractiveTiming.WorstUserInteractionLatency.MaxEventDuration.Subframe";
const char
    kHistogramAMPSubframeWorstUserInteractionLatencyOverBudgetMaxEventDuration
        [] = "InteractiveTiming.WorstUserInteractionLatencyOverBudget."
             "MaxEventDuration.Subframe";
const char
    kHistogramAMPSubframeAverageUserInteractionLatencyOverBudgetTotalEventDuration
        [] = "InteractiveTiming.AverageUserInteractionLatencyOverBudget."
             "TotalEventDuration.Subframe";
const char
    kHistogramAMPSubframeSlowUserInteractionLatencyOverBudgetHighPercentileTotalEventDuration
        [] = "InteractiveTiming.SlowUserInteractionLatencyOverBudget."
             "HighPercentile.TotalEventDuration.Subframe";
const char
    kHistogramAMPSubframeSlowUserInteractionLatencyOverBudgetHighPercentile2TotalEventDuration
        [] = "InteractiveTiming.SlowUserInteractionLatencyOverBudget."
             "HighPercentile2.TotalEventDuration.Subframe";
const char
    kHistogramAMPSubframeSumOfUserInteractionLatencyOverBudgetTotalEventDuration
        [] = "InteractiveTiming.SumOfUserInteractionLatencyOverBudget."
             "TotalEventDuration.Subframe";
const char
    kHistogramAMPSubframeWorstUserInteractionLatencyTotalEventDuration[] =
        "InteractiveTiming.WorstUserInteractionLatency.TotalEventDuration."
        "Subframe";
const char
    kHistogramAMPSubframeWorstUserInteractionLatencyOverBudgetTotalEventDuration
        [] = "InteractiveTiming.WorstUserInteractionLatencyOverBudget."
             "TotalEventDuration.Subframe";

GURL GetCanonicalizedSameDocumentUrl(const GURL& url) {
  if (!url.has_ref())
    return url;

  // We're only interested in same document navigations where the full URL
  // changes, so we ignore the 'ref' or '#fragment' portion of the URL.
  GURL::Replacements replacements;
  replacements.ClearRef();
  return url.ReplaceComponents(replacements);
}

bool IsLikelyAmpCacheUrl(const GURL& url) {
  // Our heuristic to identify AMP cache URLs is to check for the presence of
  // the amp_js_v query param.
  for (net::QueryIterator it(url); !it.IsAtEnd(); it.Advance()) {
    if (it.GetKey() == "amp_js_v")
      return true;
  }
  return false;
}

// Extracts the AMP viewer URL from a URL, as encoded in a fragment parameter.
GURL GetViewerUrlFromCacheUrl(const GURL& url) {
  // The viewer URL is encoded in the fragment as a query string parameter
  // (&viewerURL=<URL>). net::QueryIterator only operates on the query string,
  // so we copy the fragment into the query string, then iterate over the
  // parameters below.
  base::StringPiece ref = url.ref_piece();
  GURL::Replacements replacements;
  replacements.SetQueryStr(ref);
  GURL modified_url = url.ReplaceComponents(replacements);
  for (net::QueryIterator it(modified_url); !it.IsAtEnd(); it.Advance()) {
    if (it.GetKey() == "viewerUrl")
      return GURL(it.GetUnescapedValue());
  }
  return GURL::EmptyGURL();
}

base::TimeDelta ClampToZero(base::TimeDelta t) {
  return std::max(base::TimeDelta(), t);
}

}  // namespace

AMPPageLoadMetricsObserver::AMPPageLoadMetricsObserver() {}

AMPPageLoadMetricsObserver::~AMPPageLoadMetricsObserver() {}

AMPPageLoadMetricsObserver::SubFrameInfo::SubFrameInfo() = default;
AMPPageLoadMetricsObserver::SubFrameInfo::~SubFrameInfo() = default;

page_load_metrics::PageLoadMetricsObserver::ObservePolicy
AMPPageLoadMetricsObserver::OnCommit(
    content::NavigationHandle* navigation_handle,
    ukm::SourceId source_id) {
  current_url_ = navigation_handle->GetURL();
  ProcessMainFrameNavigation(navigation_handle);
  return CONTINUE_OBSERVING;
}

void AMPPageLoadMetricsObserver::OnCommitSameDocumentNavigation(
    content::NavigationHandle* navigation_handle) {
  const GURL url = GetCanonicalizedSameDocumentUrl(navigation_handle->GetURL());

  // Ignore same document navigations where the URL doesn't change.
  if (url == current_url_)
    return;
  current_url_ = url;

  // We're transitioning to a new URL, so record metrics for the previous AMP
  // document, if any.
  MaybeRecordAmpDocumentMetrics();
  current_main_frame_nav_info_ = nullptr;
  ProcessMainFrameNavigation(navigation_handle);
}

void AMPPageLoadMetricsObserver::OnDidFinishSubFrameNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->HasCommitted())
    return;

  // Ignore same document navigations; see crbug.com/1104365
  if (navigation_handle->IsSameDocument())
    return;

  // A new navigation is committing, so ensure any old information associated
  // with this frame is discarded.
  amp_subframe_info_.erase(navigation_handle->GetRenderFrameHost());

  // Only track frames or fenced frames that are direct descendants of the main
  // frame.
  if (navigation_handle->GetParentFrame() == nullptr ||
      navigation_handle->GetParentFrame()->GetParentOrOuterDocument() !=
          nullptr) {
    return;
  }

  // Only track frames that have AMP cache-like URLs.
  if (!IsLikelyAmpCacheUrl(navigation_handle->GetURL()))
    return;

  GURL viewer_url = GetViewerUrlFromCacheUrl(navigation_handle->GetURL());
  if (viewer_url.is_empty())
    return;

  // Record information about the document loaded in this subframe, which we may
  // use later to record metrics. Note that we don't yet know if the document in
  // the subframe is an AMP document. That's determined in
  // OnLoadingBehaviorObserved.
  auto& subframe_info =
      amp_subframe_info_[navigation_handle->GetRenderFrameHost()];
  subframe_info.viewer_url = viewer_url;
  subframe_info.navigation_start = navigation_handle->NavigationStart();
}

void AMPPageLoadMetricsObserver::OnRenderFrameDeleted(
    content::RenderFrameHost* rfh) {
  if (current_main_frame_nav_info_ &&
      current_main_frame_nav_info_->subframe_rfh == rfh) {
    MaybeRecordAmpDocumentMetrics();
    current_main_frame_nav_info_->subframe_rfh = nullptr;
  }
  amp_subframe_info_.erase(rfh);
}

void AMPPageLoadMetricsObserver::OnTimingUpdate(
    content::RenderFrameHost* subframe_rfh,
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  if (subframe_rfh == nullptr)
    return;

  auto it = amp_subframe_info_.find(subframe_rfh);
  if (it == amp_subframe_info_.end())
    return;

  it->second.timing = timing.Clone();
}

void AMPPageLoadMetricsObserver::OnInputTimingUpdate(
    content::RenderFrameHost* subframe_rfh,
    const page_load_metrics::mojom::InputTiming& input_timing_delta) {
  if (subframe_rfh == nullptr)
    return;

  auto it = amp_subframe_info_.find(subframe_rfh);
  if (it == amp_subframe_info_.end())
    return;

  if (input_timing_delta.num_interactions) {
    it->second.responsiveness_metrics_normalization
        .AddNewUserInteractionLatencies(
            input_timing_delta.num_interactions,
            *(input_timing_delta.max_event_durations),
            *(input_timing_delta.total_event_durations));
  }
}

void AMPPageLoadMetricsObserver::OnMobileFriendlinessUpdate(
    const blink::MobileFriendliness& mf) {
  if (mf == blink::MobileFriendliness() ||
      current_main_frame_nav_info_ == nullptr ||
      current_main_frame_nav_info_->subframe_rfh == nullptr)
    return;

  auto it = amp_subframe_info_.find(current_main_frame_nav_info_->subframe_rfh);
  if (it == amp_subframe_info_.end())
    return;

  SubFrameInfo& subframe_info = it->second;
  if (subframe_info.viewer_url != current_main_frame_nav_info_->url ||
      !subframe_info.amp_document_loaded) {
    return;
  }

  subframe_info.mobile_friendliness = mf;
}

void AMPPageLoadMetricsObserver::OnSubFrameRenderDataUpdate(
    content::RenderFrameHost* subframe_rfh,
    const page_load_metrics::mojom::FrameRenderDataUpdate& render_data) {
  if (subframe_rfh == nullptr)
    return;

  auto it = amp_subframe_info_.find(subframe_rfh);
  if (it == amp_subframe_info_.end())
    return;

  it->second.render_data.layout_shift_score += render_data.layout_shift_delta;
  it->second.render_data.layout_shift_score_before_input_or_scroll +=
      render_data.layout_shift_delta_before_input_or_scroll;

  it->second.layout_shift_normalization.AddNewLayoutShifts(
      render_data.new_layout_shifts, base::TimeTicks::Now(),
      it->second.render_data.layout_shift_score);
}

void AMPPageLoadMetricsObserver::OnComplete(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  MaybeRecordAmpDocumentMetrics();
  current_main_frame_nav_info_ = nullptr;
  page_load_metrics::RecordPageVisitFinalStatusForTiming(
      timing, GetDelegate(), GetDelegate().GetPageUkmSourceId());
}

void AMPPageLoadMetricsObserver::ProcessMainFrameNavigation(
    content::NavigationHandle* navigation_handle) {
  // Find the subframe RenderFrameHost hosting the AMP document for this
  // navigation. Note that in some cases, the subframe may not exist yet, in
  // which case logic in OnLoadingBehaviorObserved will associate the subframe
  // with current_main_frame_nav_info_.
  content::RenderFrameHost* subframe_rfh = nullptr;
  for (const auto& kv : amp_subframe_info_) {
    if (navigation_handle->GetURL() == kv.second.viewer_url) {
      subframe_rfh = kv.first;
      break;
    }
  }

  current_main_frame_nav_info_ = base::WrapUnique(new MainFrameNavigationInfo{
      navigation_handle->GetURL(),
      ukm::ConvertToSourceId(navigation_handle->GetNavigationId(),
                             ukm::SourceIdType::NAVIGATION_ID),
      subframe_rfh, navigation_handle->NavigationStart(),
      navigation_handle->IsSameDocument()});
}

void AMPPageLoadMetricsObserver::OnLoadingBehaviorObserved(
    content::RenderFrameHost* subframe_rfh,
    int behavior_flags) {
  RecordLoadingBehaviorObserved();

  if (subframe_rfh == nullptr)
    return;

  if ((behavior_flags &
       blink::LoadingBehaviorFlag::kLoadingBehaviorAmpDocumentLoaded) == 0)
    return;

  auto it = amp_subframe_info_.find(subframe_rfh);
  if (it == amp_subframe_info_.end())
    return;

  SubFrameInfo& subframe_info = it->second;
  if (subframe_info.amp_document_loaded)
    return;

  subframe_info.amp_document_loaded = true;

  // If the current MainFrameNavigationInfo doesn't yet have a subframe
  // RenderFrameHost, and its URL matches the AMP subframe's viewer URL, then
  // associate the MainFrameNavigationInfo with this frame.
  if (current_main_frame_nav_info_ &&
      current_main_frame_nav_info_->subframe_rfh == nullptr &&
      subframe_info.viewer_url == current_main_frame_nav_info_->url) {
    current_main_frame_nav_info_->subframe_rfh = subframe_rfh;
  }
}

void AMPPageLoadMetricsObserver::RecordLoadingBehaviorObserved() {
  ukm::builders::AmpPageLoad builder(GetDelegate().GetPageUkmSourceId());
  bool should_record = false;
  if (!observed_amp_main_frame_ &&
      (GetDelegate().GetMainFrameMetadata().behavior_flags &
       blink::LoadingBehaviorFlag::kLoadingBehaviorAmpDocumentLoaded) != 0) {
    builder.SetMainFrameAmpPageLoad(true);
    observed_amp_main_frame_ = true;
    should_record = true;
  }

  if (!observed_amp_sub_frame_ &&
      (GetDelegate().GetSubframeMetadata().behavior_flags &
       blink::LoadingBehaviorFlag::kLoadingBehaviorAmpDocumentLoaded) != 0) {
    builder.SetSubFrameAmpPageLoad(true);
    observed_amp_sub_frame_ = true;
    should_record = true;
  }
  if (should_record)
    builder.Record(ukm::UkmRecorder::Get());
}

void AMPPageLoadMetricsObserver::MaybeRecordAmpDocumentMetrics() {
  if (current_main_frame_nav_info_ == nullptr ||
      current_main_frame_nav_info_->subframe_rfh == nullptr)
    return;

  auto it = amp_subframe_info_.find(current_main_frame_nav_info_->subframe_rfh);
  if (it == amp_subframe_info_.end())
    return;

  const SubFrameInfo& subframe_info = it->second;
  if (subframe_info.viewer_url != current_main_frame_nav_info_->url)
    return;

  if (!subframe_info.amp_document_loaded)
    return;

  // TimeDeltas in subframe_info are relative to the navigation start in the AMP
  // subframe. Given that AMP subframes can be prerendered and thus their
  // navigation start may be long before a user initiates the navigation to that
  // AMP document, we need to adjust the times by the difference between the
  // top-level navigation start (which is when the top-level URL was updated to
  // reflect the AMP Viewer URL for the AMP document) and the navigation start
  // in the AMP subframe. Note that we use the top-level navigation start as our
  // best estimate of when the user initiated the navigation.
  base::TimeDelta navigation_input_delta =
      current_main_frame_nav_info_->navigation_start -
      subframe_info.navigation_start;

  ukm::builders::AmpPageLoad builder(
      current_main_frame_nav_info_->ukm_source_id);
  builder.SetSubFrame_MainFrameToSubFrameNavigationDelta(
      -navigation_input_delta.InMilliseconds());

  if (!current_main_frame_nav_info_->is_same_document_navigation) {
    // For non same document navigations, we expect the main frame navigation
    // to be before the subframe navigation. This measures the time from main
    // frame navigation to the time the AMP subframe is added to the document.
    PAGE_LOAD_HISTOGRAM(
        std::string(kHistogramPrefix)
            .append(kHistogramAMPSubframeMainFrameToSubFrameNavigation),
        -navigation_input_delta);
  } else {
    if (navigation_input_delta >= base::TimeDelta()) {
      // Prerender case: subframe navigation happens before main frame
      // navigation.
      PAGE_LOAD_HISTOGRAM(std::string(kHistogramPrefix)
                              .append(kHistogramAMPSubframeNavigationToInput),
                          navigation_input_delta);
    } else {
      // For same document navigations, if the main frame navigation is
      // initiated before the AMP subframe is navigated,
      // |navigation_input_delta| will be negative. This happens in the
      // non-prerender case. We record this delta to ensure it's consistently a
      // small value (the expected case).
      PAGE_LOAD_HISTOGRAM(std::string(kHistogramPrefix)
                              .append(kHistogramAMPSubframeInputToNavigation),
                          -navigation_input_delta);
    }
  }

  if (!subframe_info.timing.is_null()) {
    if (subframe_info.timing->paint_timing->first_paint.has_value()) {
      builder.SetSubFrame_PaintTiming_NavigationToFirstPaint(
          subframe_info.timing->paint_timing->first_paint.value()
              .InMilliseconds());
    }

    if (subframe_info.timing->paint_timing->first_contentful_paint
            .has_value()) {
      builder.SetSubFrame_PaintTiming_NavigationToFirstContentfulPaint(
          subframe_info.timing->paint_timing->first_contentful_paint.value()
              .InMilliseconds());

      base::TimeDelta first_contentful_paint = ClampToZero(
          subframe_info.timing->paint_timing->first_contentful_paint.value() -
          navigation_input_delta);
      if (current_main_frame_nav_info_->is_same_document_navigation) {
        PAGE_LOAD_HISTOGRAM(
            std::string(kHistogramPrefix)
                .append(kHistogramAMPSubframeFirstContentfulPaint),
            first_contentful_paint);
      } else {
        PAGE_LOAD_HISTOGRAM(
            std::string(kHistogramPrefix)
                .append(
                    kHistogramAMPSubframeFirstContentfulPaintFullNavigation),
            first_contentful_paint);
      }
    }

    absl::optional<base::TimeDelta> largest_content_paint_time;
    uint64_t largest_content_paint_size;
    page_load_metrics::ContentfulPaintTimingInfo::LargestContentTextOrImage
        largest_content_text_or_image;
    const page_load_metrics::mojom::PaintTimingPtr& paint_timing =
        subframe_info.timing->paint_timing;
    if (page_load_metrics::LargestContentfulPaintHandler::
            AssignTimeAndSizeForLargestContentfulPaint(
                *paint_timing->largest_contentful_paint,
                &largest_content_paint_time, &largest_content_paint_size,
                &largest_content_text_or_image)) {
      builder.SetSubFrame_PaintTiming_NavigationToLargestContentfulPaint2(
          largest_content_paint_time.value().InMilliseconds());

      // Adjust by the navigation_input_delta.
      largest_content_paint_time = ClampToZero(
          largest_content_paint_time.value() - navigation_input_delta);
      if (current_main_frame_nav_info_->is_same_document_navigation) {
        PAGE_LOAD_HISTOGRAM(
            std::string(kHistogramPrefix)
                .append(kHistogramAMPSubframeLargestContentfulPaint),
            largest_content_paint_time.value());
      } else {
        PAGE_LOAD_HISTOGRAM(
            std::string(kHistogramPrefix)
                .append(
                    kHistogramAMPSubframeLargestContentfulPaintFullNavigation),
            largest_content_paint_time.value());
      }
    }

    if (subframe_info.timing->interactive_timing->first_input_delay
            .has_value()) {
      builder.SetSubFrame_InteractiveTiming_FirstInputDelay4(
          subframe_info.timing->interactive_timing->first_input_delay.value()
              .InMilliseconds());

      if (current_main_frame_nav_info_->is_same_document_navigation) {
        UMA_HISTOGRAM_CUSTOM_TIMES(
            std::string(kHistogramPrefix)
                .append(kHistogramAMPSubframeFirstInputDelay),
            subframe_info.timing->interactive_timing->first_input_delay.value(),
            base::Milliseconds(1), base::Seconds(60), 50);
      } else {
        UMA_HISTOGRAM_CUSTOM_TIMES(
            std::string(kHistogramPrefix)
                .append(kHistogramAMPSubframeFirstInputDelayFullNavigation),
            subframe_info.timing->interactive_timing->first_input_delay.value(),
            base::Milliseconds(1), base::Seconds(60), 50);
      }
    }
  }

  // Clamp the score to a max of 10, which is equivalent to a frame with 10
  // full-frame layout shifts.
  float clamped_shift_score =
      std::min(subframe_info.render_data.layout_shift_score, 10.0f);
  float clamped_shift_score_before_input_or_scroll = std::min(
      subframe_info.render_data.layout_shift_score_before_input_or_scroll,
      10.0f);

  // For UKM, report (shift_score * 100) as an int in the range [0, 1000].
  builder
      .SetSubFrame_LayoutInstability_CumulativeShiftScore(
          static_cast<int>(roundf(clamped_shift_score * 100.0f)))
      .SetSubFrame_LayoutInstability_CumulativeShiftScore_BeforeInputOrScroll(
          static_cast<int>(
              roundf(clamped_shift_score_before_input_or_scroll * 100.0f)));

  const page_load_metrics::NormalizedCLSData& normalized_cls_data =
      subframe_info.layout_shift_normalization.normalized_cls_data();
  if (!normalized_cls_data.data_tainted) {
    builder
        .SetSubFrame_LayoutInstability_MaxCumulativeShiftScore_SessionWindow_Gap1000ms_Max5000ms(
            page_load_metrics::LayoutShiftUkmValue(
                normalized_cls_data
                    .session_windows_gap1000ms_max5000ms_max_cls));
  }

  // For UMA, report (shift_score * 10) an an int in the range [0,100].
  int32_t uma_value = static_cast<int>(roundf(clamped_shift_score * 10.0f));
  if (current_main_frame_nav_info_->is_same_document_navigation) {
    UMA_HISTOGRAM_COUNTS_100(
        std::string(kHistogramPrefix)
            .append(kHistogramAMPSubframeLayoutInstabilityShiftScore),
        uma_value);
    if (!normalized_cls_data.data_tainted) {
      base::UmaHistogramCounts100(
          "PageLoad.Clients.AMP.LayoutInstability.MaxCumulativeShiftScore."
          "Subframe.SessionWindow.Gap1000ms.Max5000ms",
          page_load_metrics::LayoutShiftUmaValue(
              normalized_cls_data.session_windows_gap1000ms_max5000ms_max_cls));
      base::UmaHistogramCustomCounts(
          "PageLoad.Clients.AMP.LayoutInstability.MaxCumulativeShiftScore."
          "Subframe.SessionWindow.Gap1000ms.Max5000ms2",
          page_load_metrics::LayoutShiftUmaValue10000(
              normalized_cls_data.session_windows_gap1000ms_max5000ms_max_cls),
          1, 24000, 50);
    }
    RecordMobileFriendliness(builder);
  } else {
    UMA_HISTOGRAM_COUNTS_100(
        std::string(kHistogramPrefix)
            .append(
                kHistogramAMPSubframeLayoutInstabilityShiftScoreFullNavigation),
        uma_value);
    if (!normalized_cls_data.data_tainted) {
      base::UmaHistogramCounts100(
          "PageLoad.Clients.AMP.LayoutInstability.MaxCumulativeShiftScore."
          "Subframe.FullNavigation.SessionWindow.Gap1000ms.Max5000ms",
          page_load_metrics::LayoutShiftUmaValue(
              normalized_cls_data.session_windows_gap1000ms_max5000ms_max_cls));
    }
  }

  RecordNormalizedResponsivenessMetrics(
      subframe_info.responsiveness_metrics_normalization
          .GetNormalizedResponsivenessMetrics(),
      builder);
  builder.Record(ukm::UkmRecorder::Get());
}

void AMPPageLoadMetricsObserver::RecordNormalizedResponsivenessMetrics(
    const page_load_metrics::NormalizedResponsivenessMetrics&
        normalized_responsiveness_metrics,
    ukm::builders::AmpPageLoad& builder) {
  if (!normalized_responsiveness_metrics.num_user_interactions)
    return;

  const std::string histogram_suffix =
      current_main_frame_nav_info_->is_same_document_navigation
          ? ""
          : ".FullNavigation";
  auto& max_event_durations =
      normalized_responsiveness_metrics.normalized_max_event_durations;
  auto& total_event_durations =
      normalized_responsiveness_metrics.normalized_total_event_durations;

  builder
      .SetSubFrame_InteractiveTiming_WorstUserInteractionLatency_MaxEventduration(
          max_event_durations.worst_latency.InMilliseconds());
  builder
      .SetSubFrame_InteractiveTiming_WorstUserInteractionLatency_TotalEventduration(
          total_event_durations.worst_latency.InMilliseconds());
  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeWorstUserInteractionLatencyMaxEventDuration)
          .append(histogram_suffix),
      max_event_durations.worst_latency, base::Milliseconds(1),
      base::Seconds(60), 50);
  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeWorstUserInteractionLatencyTotalEventDuration)
          .append(histogram_suffix),
      total_event_durations.worst_latency, base::Milliseconds(1),
      base::Seconds(60), 50);

  if (!base::FeatureList::IsEnabled(
          blink::features::kSendAllUserInteractionLatencies))
    return;

  base::TimeDelta high_percentile2_max_event_duration = page_load_metrics::
      ResponsivenessMetricsNormalization::ApproximateHighPercentile(
          normalized_responsiveness_metrics.num_user_interactions,
          max_event_durations.worst_ten_latencies_over_budget);
  base::TimeDelta high_percentile2_total_event_duration = page_load_metrics::
      ResponsivenessMetricsNormalization::ApproximateHighPercentile(
          normalized_responsiveness_metrics.num_user_interactions,
          total_event_durations.worst_ten_latencies_over_budget);

  builder
      .SetSubFrame_InteractiveTiming_WorstUserInteractionLatencyOverBudget_MaxEventduration(
          max_event_durations.worst_latency_over_budget.InMilliseconds());
  builder
      .SetSubFrame_InteractiveTiming_WorstUserInteractionLatencyOverBudget_TotalEventduration(
          total_event_durations.worst_latency_over_budget.InMilliseconds());
  builder
      .SetSubFrame_InteractiveTiming_SumOfUserInteractionLatencyOverBudget_MaxEventduration(
          max_event_durations.sum_of_latency_over_budget.InMilliseconds());
  builder
      .SetSubFrame_InteractiveTiming_SumOfUserInteractionLatencyOverBudget_TotalEventduration(
          total_event_durations.sum_of_latency_over_budget.InMilliseconds());
  builder
      .SetSubFrame_InteractiveTiming_AverageUserInteractionLatencyOverBudget_MaxEventduration(
          max_event_durations.sum_of_latency_over_budget.InMilliseconds() /
          normalized_responsiveness_metrics.num_user_interactions);
  builder
      .SetSubFrame_InteractiveTiming_AverageUserInteractionLatencyOverBudget_TotalEventduration(
          total_event_durations.sum_of_latency_over_budget.InMilliseconds() /
          normalized_responsiveness_metrics.num_user_interactions);
  builder
      .SetSubFrame_InteractiveTiming_SlowUserInteractionLatencyOverBudget_HighPercentile_MaxEventduration(
          max_event_durations.high_percentile_latency_over_budget
              .InMilliseconds());
  builder
      .SetSubFrame_InteractiveTiming_SlowUserInteractionLatencyOverBudget_HighPercentile_TotalEventduration(
          total_event_durations.high_percentile_latency_over_budget
              .InMilliseconds());
  builder
      .SetSubFrame_InteractiveTiming_SlowUserInteractionLatencyOverBudget_HighPercentile2_MaxEventduration(
          high_percentile2_max_event_duration.InMilliseconds());
  builder
      .SetSubFrame_InteractiveTiming_SlowUserInteractionLatencyOverBudget_HighPercentile2_TotalEventduration(
          high_percentile2_total_event_duration.InMilliseconds());

  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeWorstUserInteractionLatencyOverBudgetMaxEventDuration)
          .append(histogram_suffix),
      max_event_durations.worst_latency_over_budget, base::Milliseconds(1),
      base::Seconds(60), 50);
  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeWorstUserInteractionLatencyOverBudgetTotalEventDuration)
          .append(histogram_suffix),
      total_event_durations.worst_latency_over_budget, base::Milliseconds(1),
      base::Seconds(60), 50);
  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeSumOfUserInteractionLatencyOverBudgetMaxEventDuration)
          .append(histogram_suffix),
      max_event_durations.sum_of_latency_over_budget, base::Milliseconds(1),
      base::Seconds(60), 50);
  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeSumOfUserInteractionLatencyOverBudgetTotalEventDuration)
          .append(histogram_suffix),
      total_event_durations.sum_of_latency_over_budget, base::Milliseconds(1),
      base::Seconds(60), 50);
  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeAverageUserInteractionLatencyOverBudgetMaxEventDuration)
          .append(histogram_suffix),
      max_event_durations.sum_of_latency_over_budget /
          normalized_responsiveness_metrics.num_user_interactions,
      base::Milliseconds(1), base::Seconds(60), 50);
  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeAverageUserInteractionLatencyOverBudgetTotalEventDuration)
          .append(histogram_suffix),
      total_event_durations.worst_latency /
          normalized_responsiveness_metrics.num_user_interactions,
      base::Milliseconds(1), base::Seconds(60), 50);
  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeSlowUserInteractionLatencyOverBudgetHighPercentileMaxEventDuration)
          .append(histogram_suffix),
      max_event_durations.high_percentile_latency_over_budget,
      base::Milliseconds(1), base::Seconds(60), 50);
  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeSlowUserInteractionLatencyOverBudgetHighPercentileTotalEventDuration)
          .append(histogram_suffix),
      total_event_durations.high_percentile_latency_over_budget,
      base::Milliseconds(1), base::Seconds(60), 50);
  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeSlowUserInteractionLatencyOverBudgetHighPercentile2MaxEventDuration)
          .append(histogram_suffix),
      high_percentile2_max_event_duration, base::Milliseconds(1),
      base::Seconds(60), 50);
  base::UmaHistogramCustomTimes(
      std::string(kHistogramPrefix)
          .append(
              kHistogramAMPSubframeSlowUserInteractionLatencyOverBudgetHighPercentile2TotalEventDuration)
          .append(histogram_suffix),
      high_percentile2_total_event_duration, base::Milliseconds(1),
      base::Seconds(60), 50);
}

void AMPPageLoadMetricsObserver::RecordMobileFriendliness(
    ukm::builders::AmpPageLoad& builder) {
  auto it = amp_subframe_info_.find(current_main_frame_nav_info_->subframe_rfh);
  if (it == amp_subframe_info_.end())
    return;

  const SubFrameInfo& subframe_info = it->second;
  if (subframe_info.viewer_url != current_main_frame_nav_info_->url)
    return;

  if (!subframe_info.amp_document_loaded)
    return;

  const blink::MobileFriendliness& mf = subframe_info.mobile_friendliness;

  // Make sure at least one MF evaluation happen.
  if (mf.small_text_ratio == -1 && mf.bad_tap_targets_ratio == -1)
    return;

  builder.SetSubFrame_MobileFriendliness_ViewportDeviceWidth(
      mf.viewport_device_width);
  builder.SetSubFrame_MobileFriendliness_AllowUserZoom(mf.allow_user_zoom);
  builder.SetSubFrame_MobileFriendliness_SmallTextRatio(mf.small_text_ratio);
  builder.SetSubFrame_MobileFriendliness_ViewportInitialScaleX10(
      page_load_metrics::GetBucketedViewportInitialScale(mf));
  builder.SetSubFrame_MobileFriendliness_ViewportHardcodedWidth(
      page_load_metrics::GetBucketedViewportHardcodedWidth(mf));
  builder.SetSubFrame_MobileFriendliness_TextContentOutsideViewportPercentage(
      mf.text_content_outside_viewport_percentage);
  builder.SetSubFrame_MobileFriendliness_BadTapTargetsRatio(
      mf.bad_tap_targets_ratio);
}
