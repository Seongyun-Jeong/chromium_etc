// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/core/ukm_page_load_metrics_observer.h"

#include <cmath>
#include <memory>
#include <vector>

#include "base/feature_list.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/timer/elapsed_timer.h"
#include "base/trace_event/common/trace_event_common.h"
#include "base/trace_event/trace_event.h"
#include "cc/metrics/ukm_smoothness_data.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/cookie_settings_factory.h"
#include "chrome/browser/history_clusters/history_clusters_tab_helper.h"
#include "chrome/browser/prefetch/no_state_prefetch/no_state_prefetch_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/features.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/metrics/metrics_data_validation.h"
#include "components/metrics/net/network_metrics_provider.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_manager.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_utils.h"
#include "components/no_state_prefetch/common/no_state_prefetch_final_status.h"
#include "components/no_state_prefetch/common/prerender_origin.h"
#include "components/offline_pages/buildflags/buildflags.h"
#include "components/page_load_metrics/browser/observers/core/largest_contentful_paint_handler.h"
#include "components/page_load_metrics/browser/page_load_metrics_util.h"
#include "components/page_load_metrics/browser/protocol_util.h"
#include "components/page_load_metrics/common/page_visit_final_status.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url_service.h"
#include "components/site_engagement/content/site_engagement_service.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "media/base/mime_util.h"
#include "net/base/load_timing_info.h"
#include "net/cookies/cookie_options.h"
#include "net/http/http_response_headers.h"
#include "services/metrics/public/cpp/metrics_utils.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "services/network/public/cpp/network_quality_tracker.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/mime_util/mime_util.h"
#include "third_party/blink/public/common/performance/largest_contentful_paint_type.h"
#include "third_party/metrics_proto/system_profile.pb.h"
#include "ui/events/blink/blink_features.h"

#if BUILDFLAG(ENABLE_OFFLINE_PAGES)
#include "chrome/browser/offline_pages/offline_page_tab_helper.h"
#endif

using page_load_metrics::PageVisitFinalStatus;

namespace {

const char kOfflinePreviewsMimeType[] = "multipart/related";
extern const base::Feature kLayoutShiftNormalizationRecordUKM{
    "LayoutShiftNormalizationRecordUKM", base::FEATURE_ENABLED_BY_DEFAULT};

bool IsSupportedProtocol(page_load_metrics::NetworkProtocol protocol) {
  switch (protocol) {
    case page_load_metrics::NetworkProtocol::kHttp11:
      return true;
    case page_load_metrics::NetworkProtocol::kHttp2:
      return true;
    case page_load_metrics::NetworkProtocol::kQuic:
      return true;
    case page_load_metrics::NetworkProtocol::kOther:
      return false;
  }
}

bool IsDefaultSearchEngine(content::BrowserContext* browser_context,
                           const GURL& url) {
  if (!browser_context)
    return false;

  auto* template_service = TemplateURLServiceFactory::GetForProfile(
      Profile::FromBrowserContext(browser_context));

  if (!template_service)
    return false;

  return template_service->IsSearchResultsPageFromDefaultSearchProvider(url);
}

bool IsUserHomePage(content::BrowserContext* browser_context, const GURL& url) {
  if (!browser_context)
    return false;

  return url.spec() == Profile::FromBrowserContext(browser_context)
                           ->GetPrefs()
                           ->GetString(prefs::kHomePage);
}

std::unique_ptr<base::trace_event::TracedValue> CumulativeShiftScoreTraceData(
    float layout_shift_score,
    float layout_shift_score_before_input_or_scroll) {
  std::unique_ptr<base::trace_event::TracedValue> data =
      std::make_unique<base::trace_event::TracedValue>();
  data->SetDouble("layoutShiftScore", layout_shift_score);
  data->SetDouble("layoutShiftScoreBeforeInputOrScroll",
                  layout_shift_score_before_input_or_scroll);
  return data;
}

int SiteInstanceRenderProcessAssignmentToInt(
    content::SiteInstanceProcessAssignment assignment) {
  // These values are logged in UKM and should not be reordered or changed. Add
  // new values to the end and be sure to update the enum
  // |SiteInstanceProcessAssignment| in
  // //tools/metrics/histograms/enums.xml.
  switch (assignment) {
    case content::SiteInstanceProcessAssignment::UNKNOWN:
      return 0;
    case content::SiteInstanceProcessAssignment::REUSED_EXISTING_PROCESS:
      return 1;
    case content::SiteInstanceProcessAssignment::USED_SPARE_PROCESS:
      return 2;
    case content::SiteInstanceProcessAssignment::CREATED_NEW_PROCESS:
      return 3;
  }
  return 0;
}

}  // namespace

// static
std::unique_ptr<page_load_metrics::PageLoadMetricsObserver>
UkmPageLoadMetricsObserver::CreateIfNeeded() {
  if (!ukm::UkmRecorder::Get()) {
    return nullptr;
  }
  return std::make_unique<UkmPageLoadMetricsObserver>(
      g_browser_process->network_quality_tracker());
}

UkmPageLoadMetricsObserver::UkmPageLoadMetricsObserver(
    network::NetworkQualityTracker* network_quality_tracker)
    : network_quality_tracker_(network_quality_tracker) {
  DCHECK(network_quality_tracker_);
}

UkmPageLoadMetricsObserver::~UkmPageLoadMetricsObserver() = default;

UkmPageLoadMetricsObserver::ObservePolicy UkmPageLoadMetricsObserver::OnStart(
    content::NavigationHandle* navigation_handle,
    const GURL& currently_committed_url,
    bool started_in_foreground) {
  content::WebContents* web_contents = navigation_handle->GetWebContents();
  is_portal_ = web_contents->IsPortal();
  browser_context_ = web_contents->GetBrowserContext();
  navigation_id_ = navigation_handle->GetNavigationId();
  if (auto* clusters_helper =
          HistoryClustersTabHelper::FromWebContents(web_contents)) {
    clusters_helper->TagNavigationAsExpectingUkmNavigationComplete(
        navigation_id_);
  }

  start_url_is_default_search_ =
      IsDefaultSearchEngine(browser_context_, navigation_handle->GetURL());
  start_url_is_home_page_ =
      IsUserHomePage(browser_context_, navigation_handle->GetURL());

  if (started_in_foreground) {
    last_time_shown_ = navigation_handle->NavigationStart();
  }
  currently_in_foreground_ = started_in_foreground;

  if (!started_in_foreground) {
    was_hidden_ = true;
    return CONTINUE_OBSERVING;
  }

  // When OnStart is invoked, we don't yet know whether we're observing a web
  // page load, vs another kind of load (e.g. a download or a PDF). Thus,
  // metrics and source information should not be recorded here. Instead, we
  // store data we might want to persist in member variables below, and later
  // record UKM metrics for that data once we've confirmed that we're observing
  // a web page load.

  effective_connection_type_ =
      network_quality_tracker_->GetEffectiveConnectionType();
  http_rtt_estimate_ = network_quality_tracker_->GetHttpRTT();
  transport_rtt_estimate_ = network_quality_tracker_->GetTransportRTT();
  downstream_kbps_estimate_ =
      network_quality_tracker_->GetDownstreamThroughputKbps();
  page_transition_ = navigation_handle->GetPageTransition();
  UpdateMainFrameRequestHadCookie(
      navigation_handle->GetWebContents()->GetBrowserContext(),
      navigation_handle->GetURL());

  return CONTINUE_OBSERVING;
}

page_load_metrics::PageLoadMetricsObserver::ObservePolicy
UkmPageLoadMetricsObserver::OnRedirect(
    content::NavigationHandle* navigation_handle) {
  main_frame_request_redirect_count_++;
  UpdateMainFrameRequestHadCookie(
      navigation_handle->GetWebContents()->GetBrowserContext(),
      navigation_handle->GetURL());

  return CONTINUE_OBSERVING;
}

void UkmPageLoadMetricsObserver::UpdateMainFrameRequestHadCookie(
    content::BrowserContext* browser_context,
    const GURL& url) {
  content::StoragePartition* partition =
      browser_context->GetStoragePartitionForUrl(url);

  partition->GetCookieManagerForBrowserProcess()->GetCookieList(
      url, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection::Todo(),
      base::BindOnce(
          &UkmPageLoadMetricsObserver::OnMainFrameRequestHadCookieResult,
          weak_factory_.GetWeakPtr(), base::Time::Now()));
}

void UkmPageLoadMetricsObserver::OnMainFrameRequestHadCookieResult(
    base::Time query_start_time,
    const net::CookieAccessResultList& cookies,
    const net::CookieAccessResultList& excluded_cookies) {
  main_frame_request_had_cookies_ =
      main_frame_request_had_cookies_.value_or(false) || !cookies.empty();
}

UkmPageLoadMetricsObserver::ObservePolicy
UkmPageLoadMetricsObserver::ShouldObserveMimeType(
    const std::string& mime_type) const {
  if (PageLoadMetricsObserver::ShouldObserveMimeType(mime_type) ==
          CONTINUE_OBSERVING ||
      mime_type == kOfflinePreviewsMimeType) {
    return CONTINUE_OBSERVING;
  }
  return STOP_OBSERVING;
}

UkmPageLoadMetricsObserver::ObservePolicy UkmPageLoadMetricsObserver::OnCommit(
    content::NavigationHandle* navigation_handle,
    ukm::SourceId source_id) {
  content::WebContents* web_contents = navigation_handle->GetWebContents();
  if (web_contents->GetContentsMimeType() == kOfflinePreviewsMimeType) {
    if (!IsOfflinePreview(web_contents))
      return STOP_OBSERVING;
  }
  connection_info_ = navigation_handle->GetConnectionInfo();
  const net::HttpResponseHeaders* response_headers =
      navigation_handle->GetResponseHeaders();
  if (response_headers)
    http_response_code_ = response_headers->response_code();
  // The PageTransition for the navigation may be updated on commit.
  page_transition_ = navigation_handle->GetPageTransition();
  was_cached_ = navigation_handle->WasResponseCached();
  navigation_handle_timing_ = navigation_handle->GetNavigationHandleTiming();
  prerender::NoStatePrefetchManager* const no_state_prefetch_manager =
      prerender::NoStatePrefetchManagerFactory::GetForBrowserContext(
          web_contents->GetBrowserContext());
  if (no_state_prefetch_manager) {
    prerender::RecordNoStatePrefetchMetrics(navigation_handle, source_id,
                                            no_state_prefetch_manager);
  }
  RecordGeneratedNavigationUKM(source_id, navigation_handle->GetURL());
  navigation_is_cross_process_ = !navigation_handle->IsSameProcess();
  navigation_entry_offset_ = navigation_handle->GetNavigationEntryOffset();
  main_document_sequence_number_ = web_contents->GetController()
                                       .GetLastCommittedEntry()
                                       ->GetMainFrameDocumentSequenceNumber();

  render_process_assignment_ = web_contents->GetMainFrame()
                                   ->GetSiteInstance()
                                   ->GetLastProcessAssignmentOutcome();

  return CONTINUE_OBSERVING;
}

UkmPageLoadMetricsObserver::ObservePolicy
UkmPageLoadMetricsObserver::FlushMetricsOnAppEnterBackground(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  if (is_portal_)
    return STOP_OBSERVING;

  base::TimeTicks current_time = base::TimeTicks::Now();
  if (!was_hidden_) {
    RecordNavigationTimingMetrics();
    RecordPageLoadMetrics(current_time);
    RecordRendererUsageMetrics();
    RecordSiteEngagement();
    RecordInputTimingMetrics();
  }
  if (GetDelegate().StartedInForeground())
    RecordTimingMetrics(timing);
  ReportLayoutStability();
  RecordSmoothnessMetrics();
  // Assume that page ends on this method, as the app could be evicted right
  // after.
  RecordPageEndMetrics(&timing, current_time,
                       /* app_entered_background */ true);
  return STOP_OBSERVING;
}

UkmPageLoadMetricsObserver::ObservePolicy UkmPageLoadMetricsObserver::OnHidden(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  if (is_portal_)
    return CONTINUE_OBSERVING;

  if (currently_in_foreground_ && !last_time_shown_.is_null()) {
    total_foreground_duration_ += base::TimeTicks::Now() - last_time_shown_;
  }
  currently_in_foreground_ = false;
  if (!was_hidden_) {
    RecordNavigationTimingMetrics();
    RecordPageLoadMetrics(base::TimeTicks() /* no app_background_time */);
    RecordRendererUsageMetrics();
    RecordSiteEngagement();
    RecordInputTimingMetrics();
    was_hidden_ = true;
  }

  // Record the CLS metrics when the tab is first hidden after it is first
  // shown in foreground, in case that OnComplete is not called.
  // last_time_shown_ is set when the page starts in the foreground or the page
  // becomes foregrounded.
  if (!was_hidden_after_first_show_in_foreground &&
      !last_time_shown_.is_null()) {
    ReportLayoutInstabilityAfterFirstForeground();
    was_hidden_after_first_show_in_foreground = true;
  }
  return CONTINUE_OBSERVING;
}

UkmPageLoadMetricsObserver::ObservePolicy
UkmPageLoadMetricsObserver::OnShown() {
  if (is_portal_)
    return CONTINUE_OBSERVING;

  currently_in_foreground_ = true;
  last_time_shown_ = base::TimeTicks::Now();
  return CONTINUE_OBSERVING;
}

void UkmPageLoadMetricsObserver::OnFailedProvisionalLoad(
    const page_load_metrics::FailedProvisionalLoadInfo& failed_load_info) {
  if (is_portal_)
    return;

  RecordPageEndMetrics(nullptr, base::TimeTicks(),
                       /* app_entered_background */ false);
  if (was_hidden_)
    return;

  RecordPageLoadMetrics(base::TimeTicks() /* no app_background_time */);

  RecordRendererUsageMetrics();

  // Error codes have negative values, however we log net error code enum values
  // for UMA histograms using the equivalent positive value. For consistency in
  // UKM, we convert to a positive value here.
  int64_t net_error_code = static_cast<int64_t>(failed_load_info.error) * -1;
  DCHECK_GE(net_error_code, 0);
  ukm::builders::PageLoad(GetDelegate().GetPageUkmSourceId())
      .SetNet_ErrorCode_OnFailedProvisionalLoad(net_error_code)
      .SetPageTiming_NavigationToFailedProvisionalLoad(
          failed_load_info.time_to_failed_provisional_load.InMilliseconds())
      .Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::OnComplete(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  if (is_portal_)
    return;

  base::TimeTicks current_time = base::TimeTicks::Now();
  if (!was_hidden_) {
    RecordNavigationTimingMetrics();
    RecordPageLoadMetrics(current_time /* no app_background_time */);
    RecordRendererUsageMetrics();
    RecordSiteEngagement();
    RecordInputTimingMetrics();
  }
  if (GetDelegate().StartedInForeground())
    RecordTimingMetrics(timing);
  ReportLayoutStability();
  RecordSmoothnessMetrics();
  RecordPageEndMetrics(&timing, current_time,
                       /* app_entered_background */ false);
  RecordMobileFriendlinessMetrics();
}

void UkmPageLoadMetricsObserver::OnResourceDataUseObserved(
    content::RenderFrameHost* content,
    const std::vector<page_load_metrics::mojom::ResourceDataUpdatePtr>&
        resources) {
  if (was_hidden_)
    return;
  for (auto const& resource : resources) {
    network_bytes_ += resource->delta_bytes;

    if (blink::IsSupportedImageMimeType(resource->mime_type)) {
      image_total_bytes_ += resource->delta_bytes;
      if (!resource->is_main_frame_resource)
        image_subframe_bytes_ += resource->delta_bytes;
    } else if (media::IsSupportedMediaMimeType(resource->mime_type) ||
               base::StartsWith(resource->mime_type, "audio/",
                                base::CompareCase::SENSITIVE) ||
               base::StartsWith(resource->mime_type, "video/",
                                base::CompareCase::SENSITIVE)) {
      media_bytes_ += resource->delta_bytes;
    }

    // Only sum body lengths for completed resources.
    if (!resource->is_complete)
      continue;
    if (blink::IsSupportedJavascriptMimeType(resource->mime_type)) {
      js_decoded_bytes_ += resource->decoded_body_length;
      if (resource->decoded_body_length > js_max_decoded_bytes_)
        js_max_decoded_bytes_ = resource->decoded_body_length;
    }
    if (resource->cache_type !=
        page_load_metrics::mojom::CacheType::kNotCached) {
      cache_bytes_ += resource->encoded_body_length;
    }
  }
}

void UkmPageLoadMetricsObserver::OnLoadedResource(
    const page_load_metrics::ExtraRequestCompleteInfo&
        extra_request_complete_info) {
  if (was_hidden_)
    return;
  if (extra_request_complete_info.request_destination ==
      network::mojom::RequestDestination::kDocument) {
    DCHECK(!main_frame_timing_.has_value());
    main_frame_timing_ = *extra_request_complete_info.load_timing_info;
  }
}

void UkmPageLoadMetricsObserver::RecordNavigationTimingMetrics() {
  const base::TimeTicks navigation_start_time =
      GetDelegate().GetNavigationStart();
  const content::NavigationHandleTiming& timing = navigation_handle_timing_;

  // Record metrics for navigation only when all relevant milestones are
  // recorded and in the expected order. It is allowed that they have the same
  // value for some cases (e.g., internal redirection for HSTS).
  if (navigation_start_time.is_null() ||
      timing.first_request_start_time.is_null() ||
      timing.first_response_start_time.is_null() ||
      timing.first_loader_callback_time.is_null() ||
      timing.final_request_start_time.is_null() ||
      timing.final_response_start_time.is_null() ||
      timing.final_loader_callback_time.is_null() ||
      timing.navigation_commit_sent_time.is_null()) {
    return;
  }
  // TODO(https://crbug.com/1076710): Change these early-returns to DCHECKs
  // after the issue 1076710 is fixed.
  if (navigation_start_time > timing.first_request_start_time ||
      timing.first_request_start_time > timing.first_response_start_time ||
      timing.first_response_start_time > timing.first_loader_callback_time ||
      timing.first_loader_callback_time > timing.navigation_commit_sent_time) {
    return;
  }
  if (navigation_start_time > timing.final_request_start_time ||
      timing.final_request_start_time > timing.final_response_start_time ||
      timing.final_response_start_time > timing.final_loader_callback_time ||
      timing.final_loader_callback_time > timing.navigation_commit_sent_time) {
    return;
  }
  DCHECK_LE(timing.first_request_start_time, timing.final_request_start_time);
  DCHECK_LE(timing.first_response_start_time, timing.final_response_start_time);
  DCHECK_LE(timing.first_loader_callback_time,
            timing.final_loader_callback_time);

  ukm::builders::NavigationTiming builder(GetDelegate().GetPageUkmSourceId());

  // Record the elapsed time from the navigation start milestone.
  builder
      .SetFirstRequestStart(
          (timing.first_request_start_time - navigation_start_time)
              .InMilliseconds())
      .SetFirstResponseStart(
          (timing.first_response_start_time - navigation_start_time)
              .InMilliseconds())
      .SetFirstLoaderCallback(
          (timing.first_loader_callback_time - navigation_start_time)
              .InMilliseconds())
      .SetFinalRequestStart(
          (timing.final_request_start_time - navigation_start_time)
              .InMilliseconds())
      .SetFinalResponseStart(
          (timing.final_response_start_time - navigation_start_time)
              .InMilliseconds())
      .SetFinalLoaderCallback(
          (timing.final_loader_callback_time - navigation_start_time)
              .InMilliseconds())
      .SetNavigationCommitSent(
          (timing.navigation_commit_sent_time - navigation_start_time)
              .InMilliseconds());

  builder.Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::OnFirstContentfulPaintInPage(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  if (is_portal_)
    return;

  if (!page_load_metrics::WasStartedInForegroundOptionalEventInForeground(
          timing.paint_timing->first_contentful_paint, GetDelegate()))
    return;

  DCHECK(timing.paint_timing->first_contentful_paint.has_value());

  ukm::builders::PageLoad builder(GetDelegate().GetPageUkmSourceId());
  builder.SetPaintTiming_NavigationToFirstContentfulPaint(
      timing.paint_timing->first_contentful_paint.value().InMilliseconds());
  builder.Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::RecordSiteEngagement() const {
  ukm::builders::PageLoad builder(GetDelegate().GetPageUkmSourceId());

  absl::optional<int64_t> rounded_site_engagement_score =
      GetRoundedSiteEngagementScore();
  if (rounded_site_engagement_score) {
    builder.SetSiteEngagementScore(rounded_site_engagement_score.value());
  }

  builder.Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::RecordTimingMetrics(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  ukm::builders::PageLoad builder(GetDelegate().GetPageUkmSourceId());

  if (timing.input_to_navigation_start) {
    builder.SetExperimental_InputToNavigationStart(
        timing.input_to_navigation_start.value().InMilliseconds());
  }
  if (WasStartedInForegroundOptionalEventInForeground(
          timing.parse_timing->parse_start, GetDelegate())) {
    builder.SetParseTiming_NavigationToParseStart(
        timing.parse_timing->parse_start.value().InMilliseconds());
  }
  if (WasStartedInForegroundOptionalEventInForeground(
          timing.document_timing->dom_content_loaded_event_start,
          GetDelegate())) {
    builder.SetDocumentTiming_NavigationToDOMContentLoadedEventFired(
        timing.document_timing->dom_content_loaded_event_start.value()
            .InMilliseconds());
  }
  if (WasStartedInForegroundOptionalEventInForeground(
          timing.document_timing->load_event_start, GetDelegate())) {
    builder.SetDocumentTiming_NavigationToLoadEventFired(
        timing.document_timing->load_event_start.value().InMilliseconds());
  }
  if (WasStartedInForegroundOptionalEventInForeground(
          timing.paint_timing->first_paint, GetDelegate())) {
    builder.SetPaintTiming_NavigationToFirstPaint(
        timing.paint_timing->first_paint.value().InMilliseconds());
  }

  // FCP is reported in OnFirstContentfulPaintInPage.

  if (WasStartedInForegroundOptionalEventInForeground(
          timing.paint_timing->first_meaningful_paint, GetDelegate())) {
    builder.SetExperimental_PaintTiming_NavigationToFirstMeaningfulPaint(
        timing.paint_timing->first_meaningful_paint.value().InMilliseconds());
  }
  const page_load_metrics::ContentfulPaintTimingInfo&
      main_frame_largest_contentful_paint =
          GetDelegate()
              .GetLargestContentfulPaintHandler()
              .MainFrameLargestContentfulPaint();
  if (main_frame_largest_contentful_paint.ContainsValidTime() &&
      WasStartedInForegroundOptionalEventInForeground(
          main_frame_largest_contentful_paint.Time(), GetDelegate())) {
    builder.SetPaintTiming_NavigationToLargestContentfulPaint2_MainFrame(
        main_frame_largest_contentful_paint.Time().value().InMilliseconds());
  }
  const page_load_metrics::ContentfulPaintTimingInfo&
      all_frames_largest_contentful_paint =
          GetDelegate()
              .GetLargestContentfulPaintHandler()
              .MergeMainFrameAndSubframes();
  if (all_frames_largest_contentful_paint.ContainsValidTime() &&
      WasStartedInForegroundOptionalEventInForeground(
          all_frames_largest_contentful_paint.Time(), GetDelegate())) {
    builder.SetPaintTiming_NavigationToLargestContentfulPaint2(
        all_frames_largest_contentful_paint.Time().value().InMilliseconds());
    builder.SetPaintTiming_LargestContentfulPaintType(
        all_frames_largest_contentful_paint.Type());
  }
  const page_load_metrics::ContentfulPaintTimingInfo&
      cross_site_sub_frame_largest_contentful_paint =
          GetDelegate()
              .GetLargestContentfulPaintHandler()
              .CrossSiteSubframesLargestContentfulPaint();
  if (cross_site_sub_frame_largest_contentful_paint.ContainsValidTime() &&
      WasStartedInForegroundOptionalEventInForeground(
          cross_site_sub_frame_largest_contentful_paint.Time(),
          GetDelegate())) {
    builder
        .SetPaintTiming_NavigationToLargestContentfulPaint2_CrossSiteSubFrame(
            cross_site_sub_frame_largest_contentful_paint.Time()
                .value()
                .InMilliseconds());
  }
  RecordInternalTimingMetrics(all_frames_largest_contentful_paint);
  if (timing.interactive_timing->first_input_delay &&
      WasStartedInForegroundOptionalEventInForeground(
          timing.interactive_timing->first_input_timestamp, GetDelegate())) {
    base::TimeDelta first_input_delay =
        timing.interactive_timing->first_input_delay.value();
    builder.SetInteractiveTiming_FirstInputDelay4(
        first_input_delay.InMilliseconds());
  }
  if (WasStartedInForegroundOptionalEventInForeground(
          timing.interactive_timing->first_input_timestamp, GetDelegate())) {
    base::TimeDelta first_input_timestamp =
        timing.interactive_timing->first_input_timestamp.value();
    builder.SetInteractiveTiming_FirstInputTimestamp4(
        first_input_timestamp.InMilliseconds());
  }

  if (timing.interactive_timing->longest_input_delay) {
    base::TimeDelta longest_input_delay =
        timing.interactive_timing->longest_input_delay.value();
    builder.SetInteractiveTiming_LongestInputDelay4(
        longest_input_delay.InMilliseconds());
  }
  if (timing.interactive_timing->longest_input_timestamp) {
    base::TimeDelta longest_input_timestamp =
        timing.interactive_timing->longest_input_timestamp.value();
    builder.SetInteractiveTiming_LongestInputTimestamp4(
        longest_input_timestamp.InMilliseconds());
  }

  const page_load_metrics::NormalizedResponsivenessMetrics&
      normalized_responsiveness_metrics =
          GetDelegate().GetNormalizedResponsivenessMetrics();
  auto& max_event_durations =
      normalized_responsiveness_metrics.normalized_max_event_durations;
  auto& total_event_durations =
      normalized_responsiveness_metrics.normalized_total_event_durations;
  if (normalized_responsiveness_metrics.num_user_interactions) {
    builder.SetInteractiveTiming_WorstUserInteractionLatency_MaxEventDuration(
        max_event_durations.worst_latency.InMilliseconds());
    builder.SetInteractiveTiming_WorstUserInteractionLatency_TotalEventDuration(
        total_event_durations.worst_latency.InMilliseconds());
    if (base::FeatureList::IsEnabled(
            blink::features::kSendAllUserInteractionLatencies)) {
      // When the flag is disabled, we don't know the type of user interactions
      // and can't calculate the worst over budget.
      builder
          .SetInteractiveTiming_WorstUserInteractionLatencyOverBudget_MaxEventDuration(
              max_event_durations.worst_latency_over_budget.InMilliseconds());
      builder
          .SetInteractiveTiming_WorstUserInteractionLatencyOverBudget_TotalEventDuration(
              total_event_durations.worst_latency_over_budget.InMilliseconds());
      builder
          .SetInteractiveTiming_SumOfUserInteractionLatencyOverBudget_MaxEventDuration(
              max_event_durations.sum_of_latency_over_budget.InMilliseconds());
      builder
          .SetInteractiveTiming_SumOfUserInteractionLatencyOverBudget_TotalEventDuration(
              total_event_durations.sum_of_latency_over_budget
                  .InMilliseconds());
      builder
          .SetInteractiveTiming_AverageUserInteractionLatencyOverBudget_MaxEventDuration(
              max_event_durations.sum_of_latency_over_budget.InMilliseconds() /
              normalized_responsiveness_metrics.num_user_interactions);
      builder
          .SetInteractiveTiming_AverageUserInteractionLatencyOverBudget_TotalEventDuration(
              total_event_durations.sum_of_latency_over_budget
                  .InMilliseconds() /
              normalized_responsiveness_metrics.num_user_interactions);
      builder
          .SetInteractiveTiming_SlowUserInteractionLatencyOverBudget_HighPercentile_MaxEventDuration(
              max_event_durations.high_percentile_latency_over_budget
                  .InMilliseconds());
      builder
          .SetInteractiveTiming_SlowUserInteractionLatencyOverBudget_HighPercentile_TotalEventDuration(
              total_event_durations.high_percentile_latency_over_budget
                  .InMilliseconds());
      builder
          .SetInteractiveTiming_SlowUserInteractionLatencyOverBudget_HighPercentile2_MaxEventDuration(
              page_load_metrics::ResponsivenessMetricsNormalization::
                  ApproximateHighPercentile(
                      normalized_responsiveness_metrics.num_user_interactions,
                      max_event_durations.worst_ten_latencies_over_budget)
                      .InMilliseconds());
      builder
          .SetInteractiveTiming_SlowUserInteractionLatencyOverBudget_HighPercentile2_TotalEventDuration(
              page_load_metrics::ResponsivenessMetricsNormalization::
                  ApproximateHighPercentile(
                      normalized_responsiveness_metrics.num_user_interactions,
                      total_event_durations.worst_ten_latencies_over_budget)
                      .InMilliseconds());
    }
  }
  if (timing.interactive_timing->first_scroll_delay &&
      WasStartedInForegroundOptionalEventInForeground(
          timing.interactive_timing->first_scroll_timestamp, GetDelegate())) {
    base::TimeDelta first_scroll_delay =
        timing.interactive_timing->first_scroll_delay.value();
    builder.SetInteractiveTiming_FirstScrollDelay(
        first_scroll_delay.InMilliseconds());
  }
  if (timing.interactive_timing->first_scroll_timestamp &&
      WasStartedInForegroundOptionalEventInForeground(
          timing.interactive_timing->first_scroll_timestamp, GetDelegate())) {
    base::TimeDelta first_scroll_timestamp =
        timing.interactive_timing->first_scroll_timestamp.value();
    builder.SetInteractiveTiming_FirstScrollTimestamp(
        ukm::GetExponentialBucketMinForUserTiming(
            first_scroll_timestamp.InMilliseconds()));
  }

  if (timing.interactive_timing->first_input_processing_time &&
      WasStartedInForegroundOptionalEventInForeground(
          timing.interactive_timing->first_input_timestamp, GetDelegate())) {
    base::TimeDelta first_input_processing_time =
        timing.interactive_timing->first_input_processing_time.value();
    builder.SetInteractiveTiming_FirstInputProcessingTimes(
        first_input_processing_time.InMilliseconds());
  }
  if (timing.user_timing_mark_fully_loaded) {
    builder.SetPageTiming_UserTimingMarkFullyLoaded(
        timing.user_timing_mark_fully_loaded.value().InMilliseconds());
  }
  if (timing.user_timing_mark_fully_visible) {
    builder.SetPageTiming_UserTimingMarkFullyVisible(
        timing.user_timing_mark_fully_visible.value().InMilliseconds());
  }
  if (timing.user_timing_mark_interactive) {
    builder.SetPageTiming_UserTimingMarkInteractive(
        timing.user_timing_mark_interactive.value().InMilliseconds());
  }
  builder.SetCpuTime(total_foreground_cpu_time_.InMilliseconds());

  builder.SetNet_CacheBytes2(
      ukm::GetExponentialBucketMinForBytes(cache_bytes_));
  builder.SetNet_NetworkBytes2(
      ukm::GetExponentialBucketMinForBytes(network_bytes_));

  builder.SetNet_JavaScriptBytes2(
      ukm::GetExponentialBucketMinForBytes(js_decoded_bytes_));
  builder.SetNet_JavaScriptMaxBytes2(
      ukm::GetExponentialBucketMinForBytes(js_max_decoded_bytes_));

  builder.SetNet_ImageBytes2(
      ukm::GetExponentialBucketMinForBytes(image_total_bytes_));
  builder.SetNet_ImageSubframeBytes2(
      ukm::GetExponentialBucketMinForBytes(image_subframe_bytes_));
  builder.SetNet_MediaBytes2(
      ukm::GetExponentialBucketMinForBytes(media_bytes_));

  if (main_frame_timing_)
    ReportMainResourceTimingMetrics(builder);

  builder.Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::RecordInternalTimingMetrics(
    const page_load_metrics::ContentfulPaintTimingInfo&
        all_frames_largest_contentful_paint) {
  ukm::builders::PageLoad_Internal debug_builder(
      GetDelegate().GetPageUkmSourceId());
  LargestContentState lcp_state = LargestContentState::kNotFound;
  if (all_frames_largest_contentful_paint.ContainsValidTime()) {
    if (WasStartedInForegroundOptionalEventInForeground(
            all_frames_largest_contentful_paint.Time(), GetDelegate())) {
      debug_builder.SetPaintTiming_LargestContentfulPaint_ContentType(
          static_cast<int>(all_frames_largest_contentful_paint.TextOrImage()));
      lcp_state = LargestContentState::kReported;
    } else {
      // This can be reached if LCP occurs after tab hide.
      lcp_state = LargestContentState::kFoundButNotReported;
    }
  } else if (all_frames_largest_contentful_paint.Time().has_value()) {
    DCHECK(all_frames_largest_contentful_paint.Size());
    lcp_state = LargestContentState::kLargestImageLoading;
  } else {
    DCHECK(all_frames_largest_contentful_paint.Empty());
    lcp_state = LargestContentState::kNotFound;
  }
  debug_builder.SetPaintTiming_LargestContentfulPaint_TerminationState(
      static_cast<int>(lcp_state));
  debug_builder.Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::RecordPageLoadMetrics(
    base::TimeTicks app_background_time) {
  ukm::builders::PageLoad builder(GetDelegate().GetPageUkmSourceId());

  absl::optional<bool> third_party_cookie_blocking_enabled =
      GetThirdPartyCookieBlockingEnabled();
  if (third_party_cookie_blocking_enabled) {
    builder.SetThirdPartyCookieBlockingEnabledForSite(
        third_party_cookie_blocking_enabled.value());
  }

  absl::optional<base::TimeDelta> foreground_duration =
      page_load_metrics::GetInitialForegroundDuration(GetDelegate(),
                                                      app_background_time);
  if (foreground_duration) {
    builder.SetPageTiming_ForegroundDuration(
        foreground_duration.value().InMilliseconds());
  }

  // Convert to the EffectiveConnectionType as used in SystemProfileProto
  // before persisting the metric.
  metrics::SystemProfileProto::Network::EffectiveConnectionType
      proto_effective_connection_type =
          metrics::ConvertEffectiveConnectionType(effective_connection_type_);
  if (proto_effective_connection_type !=
      metrics::SystemProfileProto::Network::EFFECTIVE_CONNECTION_TYPE_UNKNOWN) {
    builder.SetNet_EffectiveConnectionType2_OnNavigationStart(
        static_cast<int64_t>(proto_effective_connection_type));
  }

  if (http_response_code_) {
    builder.SetNet_HttpResponseCode(
        static_cast<int64_t>(http_response_code_.value()));
  }
  if (http_rtt_estimate_) {
    builder.SetNet_HttpRttEstimate_OnNavigationStart(
        static_cast<int64_t>(http_rtt_estimate_.value().InMilliseconds()));
  }
  if (transport_rtt_estimate_) {
    builder.SetNet_TransportRttEstimate_OnNavigationStart(
        static_cast<int64_t>(transport_rtt_estimate_.value().InMilliseconds()));
  }
  if (downstream_kbps_estimate_) {
    builder.SetNet_DownstreamKbpsEstimate_OnNavigationStart(
        static_cast<int64_t>(downstream_kbps_estimate_.value()));
  }
  if (GetDelegate().DidCommit() && was_cached_) {
    builder.SetWasCached(1);
  }
  if (GetDelegate().DidCommit() && navigation_is_cross_process_) {
    builder.SetIsCrossProcessNavigation(navigation_is_cross_process_);
  }
  if (GetDelegate().DidCommit()) {
    builder.SetNavigationEntryOffset(navigation_entry_offset_);
    builder.SetMainDocumentSequenceNumber(main_document_sequence_number_);
  }

  builder.Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::RecordRendererUsageMetrics() {
  ukm::builders::PageLoad builder(GetDelegate().GetPageUkmSourceId());

  if (render_process_assignment_) {
    builder.SetSiteInstanceRenderProcessAssignment(
        SiteInstanceRenderProcessAssignmentToInt(
            render_process_assignment_.value()));
  }

  builder.Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::ReportMainResourceTimingMetrics(
    ukm::builders::PageLoad& builder) {
  DCHECK(main_frame_timing_.has_value());

  builder.SetMainFrameResource_SocketReused(main_frame_timing_->socket_reused);

  int64_t dns_start_ms =
      main_frame_timing_->connect_timing.dns_start.since_origin()
          .InMilliseconds();
  int64_t dns_end_ms = main_frame_timing_->connect_timing.dns_end.since_origin()
                           .InMilliseconds();
  int64_t connect_start_ms =
      main_frame_timing_->connect_timing.connect_start.since_origin()
          .InMilliseconds();
  int64_t connect_end_ms =
      main_frame_timing_->connect_timing.connect_end.since_origin()
          .InMilliseconds();
  int64_t request_start_ms =
      main_frame_timing_->request_start.since_origin().InMilliseconds();
  int64_t send_start_ms =
      main_frame_timing_->send_start.since_origin().InMilliseconds();
  int64_t receive_headers_end_ms =
      main_frame_timing_->receive_headers_end.since_origin().InMilliseconds();

  DCHECK_LE(dns_start_ms, dns_end_ms);
  DCHECK_LE(dns_end_ms, connect_start_ms);
  DCHECK_LE(dns_start_ms, connect_start_ms);
  DCHECK_LE(connect_start_ms, connect_end_ms);

  int64_t dns_duration_ms = dns_end_ms - dns_start_ms;
  int64_t connect_duration_ms = connect_end_ms - connect_start_ms;
  int64_t request_start_to_send_start_ms = send_start_ms - request_start_ms;
  int64_t send_start_to_receive_headers_end_ms =
      receive_headers_end_ms - send_start_ms;
  int64_t request_start_to_receive_headers_end_ms =
      receive_headers_end_ms - request_start_ms;

  builder.SetMainFrameResource_DNSDelay(dns_duration_ms);
  builder.SetMainFrameResource_ConnectDelay(connect_duration_ms);
  if (request_start_to_send_start_ms >= 0) {
    builder.SetMainFrameResource_RequestStartToSendStart(
        request_start_to_send_start_ms);
  }
  if (send_start_to_receive_headers_end_ms >= 0) {
    builder.SetMainFrameResource_SendStartToReceiveHeadersEnd(
        send_start_to_receive_headers_end_ms);
  }
  builder.SetMainFrameResource_RequestStartToReceiveHeadersEnd(
      request_start_to_receive_headers_end_ms);

  if (!main_frame_timing_->request_start.is_null() &&
      !GetDelegate().GetNavigationStart().is_null()) {
    base::TimeDelta navigation_start_to_request_start =
        main_frame_timing_->request_start - GetDelegate().GetNavigationStart();

    builder.SetMainFrameResource_NavigationStartToRequestStart(
        navigation_start_to_request_start.InMilliseconds());
  }

  if (!main_frame_timing_->receive_headers_start.is_null() &&
      !GetDelegate().GetNavigationStart().is_null()) {
    base::TimeDelta navigation_start_to_receive_headers_start =
        main_frame_timing_->receive_headers_start -
        GetDelegate().GetNavigationStart();
    builder.SetMainFrameResource_NavigationStartToReceiveHeadersStart(
        navigation_start_to_receive_headers_start.InMilliseconds());
  }

  if (connection_info_.has_value()) {
    page_load_metrics::NetworkProtocol protocol =
        page_load_metrics::GetNetworkProtocol(*connection_info_);
    if (IsSupportedProtocol(protocol)) {
      builder.SetMainFrameResource_HttpProtocolScheme(
          static_cast<int>(protocol));
    }
  }

  if (main_frame_request_redirect_count_ > 0) {
    builder.SetMainFrameResource_RedirectCount(
        main_frame_request_redirect_count_);
  }
  if (main_frame_request_had_cookies_.has_value()) {
    builder.SetMainFrameResource_RequestHadCookies(
        main_frame_request_had_cookies_.value() ? 1 : 0);
  }
}

void UkmPageLoadMetricsObserver::ReportLayoutStability() {
  // Don't report CLS if we were never in the foreground.
  if (last_time_shown_.is_null())
    return;

  ukm::builders::PageLoad builder(GetDelegate().GetPageUkmSourceId());
  builder
      .SetLayoutInstability_CumulativeShiftScore(
          page_load_metrics::LayoutShiftUkmValue(
              GetDelegate().GetPageRenderData().layout_shift_score))
      .SetLayoutInstability_CumulativeShiftScore_BeforeInputOrScroll(
          page_load_metrics::LayoutShiftUkmValue(
              GetDelegate()
                  .GetPageRenderData()
                  .layout_shift_score_before_input_or_scroll))
      .SetLayoutInstability_CumulativeShiftScore_MainFrame(
          page_load_metrics::LayoutShiftUkmValue(
              GetDelegate().GetMainFrameRenderData().layout_shift_score))
      .SetLayoutInstability_CumulativeShiftScore_MainFrame_BeforeInputOrScroll(
          page_load_metrics::LayoutShiftUkmValue(
              GetDelegate()
                  .GetMainFrameRenderData()
                  .layout_shift_score_before_input_or_scroll));
  // Record CLS normalization UKM.
  const page_load_metrics::NormalizedCLSData& normalized_cls_data =
      GetDelegate().GetNormalizedCLSData(
          page_load_metrics::PageLoadMetricsObserverDelegate::BfcacheStrategy::
              ACCUMULATE);
  if (base::FeatureList::IsEnabled(kLayoutShiftNormalizationRecordUKM) &&
      !normalized_cls_data.data_tainted) {
    builder
        .SetLayoutInstability_MaxCumulativeShiftScore_SessionWindow_Gap1000ms_Max5000ms(
            page_load_metrics::LayoutShiftUkmValue(
                normalized_cls_data
                    .session_windows_gap1000ms_max5000ms_max_cls));
    base::UmaHistogramCounts100(
        "PageLoad.LayoutInstability.MaxCumulativeShiftScore.SessionWindow."
        "Gap1000ms.Max5000ms",
        page_load_metrics::LayoutShiftUmaValue(
            normalized_cls_data.session_windows_gap1000ms_max5000ms_max_cls));
    base::UmaHistogramCustomCounts(
        "PageLoad.LayoutInstability.MaxCumulativeShiftScore.SessionWindow."
        "Gap1000ms.Max5000ms2",
        page_load_metrics::LayoutShiftUmaValue10000(
            normalized_cls_data.session_windows_gap1000ms_max5000ms_max_cls),
        1, 24000, 50);
  }
  builder.Record(ukm::UkmRecorder::Get());

  // TODO(crbug.com/1064483): We should move UMA recording to components/

  const float page_shift_score = page_load_metrics::LayoutShiftUmaValue(
      GetDelegate().GetPageRenderData().layout_shift_score);
  UMA_HISTOGRAM_COUNTS_100("PageLoad.LayoutInstability.CumulativeShiftScore",
                           page_shift_score);
  // The pseudo metric of PageLoad.LayoutInstability.CumulativeShiftScore. Only
  // used to assess field trial data quality.
  UMA_HISTOGRAM_COUNTS_100(
      "UMA.Pseudo.PageLoad.LayoutInstability.CumulativeShiftScore",
      metrics::GetPseudoMetricsSample(page_shift_score));

  TRACE_EVENT_INSTANT1("loading", "CumulativeShiftScore::AllFrames::UMA",
                       TRACE_EVENT_SCOPE_THREAD, "data",
                       CumulativeShiftScoreTraceData(
                           GetDelegate().GetPageRenderData().layout_shift_score,
                           GetDelegate()
                               .GetPageRenderData()
                               .layout_shift_score_before_input_or_scroll));

  UMA_HISTOGRAM_COUNTS_100(
      "PageLoad.LayoutInstability.CumulativeShiftScore.MainFrame",
      page_load_metrics::LayoutShiftUmaValue(
          GetDelegate().GetMainFrameRenderData().layout_shift_score));
}

void UkmPageLoadMetricsObserver::ReportLayoutInstabilityAfterFirstForeground() {
  DCHECK(!last_time_shown_.is_null());

  ukm::builders::PageLoad builder(GetDelegate().GetPageUkmSourceId());
  builder.SetExperimental_LayoutInstability_CumulativeShiftScoreAtFirstOnHidden(
      page_load_metrics::LayoutShiftUkmValue(
          GetDelegate().GetPageRenderData().layout_shift_score));
  // Record CLS normalization UKM.
  const page_load_metrics::NormalizedCLSData& normalized_cls_data =
      GetDelegate().GetNormalizedCLSData(
          page_load_metrics::PageLoadMetricsObserverDelegate::BfcacheStrategy::
              ACCUMULATE);
  if (base::FeatureList::IsEnabled(kLayoutShiftNormalizationRecordUKM) &&
      !normalized_cls_data.data_tainted) {
    builder
        .SetExperimental_LayoutInstability_MaxCumulativeShiftScoreAtFirstOnHidden_SessionWindow_Gap1000ms_Max5000ms(
            page_load_metrics::LayoutShiftUkmValue(
                normalized_cls_data
                    .session_windows_gap1000ms_max5000ms_max_cls));
  }
  builder.Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::RecordAbortMetrics(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    base::TimeTicks page_end_time,
    ukm::builders::PageLoad* builder) {
  PageVisitFinalStatus page_visit_status =
      page_load_metrics::RecordPageVisitFinalStatusForTiming(
          timing, GetDelegate(), GetDelegate().GetPageUkmSourceId());
  if (currently_in_foreground_ && !last_time_shown_.is_null()) {
    total_foreground_duration_ += page_end_time - last_time_shown_;
  }
  UMA_HISTOGRAM_ENUMERATION("PageLoad.PageVisitFinalStatus", page_visit_status);
  PAGE_LOAD_LONG_HISTOGRAM("PageLoad.Experimental.TotalForegroundDuration",
                           total_foreground_duration_);

  builder->SetPageVisitFinalStatus(static_cast<int>(page_visit_status))
      .SetPageTiming_TotalForegroundDuration(
          ukm::GetSemanticBucketMinForDurationTiming(
              total_foreground_duration_.InMilliseconds()));
}

void UkmPageLoadMetricsObserver::RecordMemoriesMetrics(
    ukm::builders::PageLoad& builder,
    const page_load_metrics::PageEndReason page_end_reason) {
  content::WebContents* web_contents = GetDelegate().GetWebContents();
  DCHECK(web_contents);
  HistoryClustersTabHelper* clusters_helper =
      HistoryClustersTabHelper::FromWebContents(web_contents);
  if (!clusters_helper)
    return;
  const history::VisitContextAnnotations context_annotations =
      clusters_helper->OnUkmNavigationComplete(
          navigation_id_, total_foreground_duration_, page_end_reason);
  // Send ALL Memories signals to UKM at page end. This is to harmonize with
  // the fact that they may only be recorded into History at page end, when
  // we can be sure that the visit row already exists.
  //
  // Please note: We don't record everything in |context_annotations| into UKM,
  // because some of these signals are already recorded elsewhere.
  builder.SetOmniboxUrlCopied(context_annotations.omnibox_url_copied);
  builder.SetIsExistingPartOfTabGroup(
      context_annotations.is_existing_part_of_tab_group);
  builder.SetIsPlacedInTabGroup(context_annotations.is_placed_in_tab_group);
  builder.SetIsExistingBookmark(context_annotations.is_existing_bookmark);
  builder.SetIsNewBookmark(context_annotations.is_new_bookmark);
  builder.SetIsNTPCustomLink(context_annotations.is_ntp_custom_link);
  builder.SetDurationSinceLastVisitSeconds(
      context_annotations.duration_since_last_visit.InSeconds());
}

void UkmPageLoadMetricsObserver::RecordInputTimingMetrics() {
  ukm::builders::PageLoad(GetDelegate().GetPageUkmSourceId())
      .SetInteractiveTiming_NumInputEvents(
          GetDelegate().GetPageInputTiming().num_input_events)
      .SetInteractiveTiming_TotalInputDelay(
          GetDelegate().GetPageInputTiming().total_input_delay.InMilliseconds())
      .SetInteractiveTiming_TotalAdjustedInputDelay(
          GetDelegate()
              .GetPageInputTiming()
              .total_adjusted_input_delay.InMilliseconds())
      .Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::RecordSmoothnessMetrics() {
  auto* smoothness =
      ukm_smoothness_data_.GetMemoryAs<cc::UkmSmoothnessDataShared>();
  if (!smoothness) {
    return;
  }

  base::ElapsedTimer timer;
  cc::UkmSmoothnessData smoothness_data;
  bool success = smoothness->Read(smoothness_data);

  UMA_HISTOGRAM_CUSTOM_MICROSECONDS_TIMES(
      "Graphics.Smoothness.Diagnostic.ReadSharedMemoryDuration",
      timer.Elapsed(), base::Microseconds(1), base::Milliseconds(5), 100);
  UMA_HISTOGRAM_BOOLEAN(
      "Graphics.Smoothness.Diagnostic.ReadSharedMemoryUKMSuccess", success);

  if (!success)
    return;

  ukm::builders::Graphics_Smoothness_NormalizedPercentDroppedFrames builder(
      GetDelegate().GetPageUkmSourceId());
  builder.SetAverage(smoothness_data.avg_smoothness)
      .SetMedian(smoothness_data.median_smoothness)
      .SetPercentile95(smoothness_data.percentile_95)
      .SetAboveThreshold(smoothness_data.above_threshold)
      .SetWorstCase(smoothness_data.worst_smoothness)
      .SetVariance(smoothness_data.variance)
      .SetTimingSinceFCPWorstCase(
          smoothness_data.time_max_delta.InMilliseconds())
      .SetSmoothnessVeryGood(smoothness_data.buckets[0])
      .SetSmoothnessGood(smoothness_data.buckets[1])
      .SetSmoothnessOkay(smoothness_data.buckets[2])
      .SetSmoothnessBad(smoothness_data.buckets[3])
      .SetSmoothnessVeryBad25to50(smoothness_data.buckets[4])
      .SetSmoothnessVeryBad50to75(smoothness_data.buckets[5])
      .SetSmoothnessVeryBad75to100(smoothness_data.buckets[6])
      .SetMainFocusedMedian(smoothness_data.main_focused_median)
      .SetMainFocusedPercentile95(smoothness_data.main_focused_percentile_95)
      .SetMainFocusedVariance(smoothness_data.main_focused_variance)
      .SetCompositorFocusedMedian(smoothness_data.compositor_focused_median)
      .SetCompositorFocusedPercentile95(
          smoothness_data.compositor_focused_percentile_95)
      .SetCompositorFocusedVariance(smoothness_data.compositor_focused_variance)
      .SetScrollFocusedMedian(smoothness_data.scroll_focused_median)
      .SetScrollFocusedPercentile95(
          smoothness_data.scroll_focused_percentile_95)
      .SetScrollFocusedVariance(smoothness_data.scroll_focused_variance);
  if (smoothness_data.worst_smoothness_after1sec >= 0)
    builder.SetWorstCaseAfter1Sec(smoothness_data.worst_smoothness_after1sec);
  if (smoothness_data.worst_smoothness_after2sec >= 0)
    builder.SetWorstCaseAfter2Sec(smoothness_data.worst_smoothness_after2sec);
  if (smoothness_data.worst_smoothness_after5sec >= 0)
    builder.SetWorstCaseAfter5Sec(smoothness_data.worst_smoothness_after5sec);
  builder.Record(ukm::UkmRecorder::Get());

  base::UmaHistogramPercentage(
      "Graphics.Smoothness.PerSession.AveragePercentDroppedFrames",
      smoothness_data.avg_smoothness);
  base::UmaHistogramPercentage(
      "Graphics.Smoothness.PerSession.95pctPercentDroppedFrames_1sWindow",
      smoothness_data.percentile_95);
  base::UmaHistogramPercentage(
      "Graphics.Smoothness.PerSession.MaxPercentDroppedFrames_1sWindow",
      smoothness_data.worst_smoothness);
  base::UmaHistogramCustomTimes(
      "Graphics.Smoothness.PerSession.TimeMaxPercentDroppedFrames_1sWindow",
      smoothness_data.time_max_delta, base::Milliseconds(1), base::Seconds(25),
      50);
}

void UkmPageLoadMetricsObserver::RecordMobileFriendlinessMetrics() {
  ukm::builders::MobileFriendliness builder(GetDelegate().GetPageUkmSourceId());
  const absl::optional<blink::MobileFriendliness>& mf =
      GetDelegate().GetMobileFriendliness();
  if (!mf.has_value())
    return;

  builder.SetViewportDeviceWidth(mf->viewport_device_width);
  builder.SetAllowUserZoom(mf->allow_user_zoom);

  builder.SetSmallTextRatio(mf->small_text_ratio);
  builder.SetViewportInitialScaleX10(
      page_load_metrics::GetBucketedViewportInitialScale(*mf));
  builder.SetViewportHardcodedWidth(
      page_load_metrics::GetBucketedViewportHardcodedWidth(*mf));
  builder.SetTextContentOutsideViewportPercentage(
      mf->text_content_outside_viewport_percentage);
  builder.SetBadTapTargetsRatio(mf->bad_tap_targets_ratio);

  // Make sure at least one MF evaluation happen.
  builder.Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::RecordPageEndMetrics(
    const page_load_metrics::mojom::PageLoadTiming* timing,
    base::TimeTicks page_end_time,
    bool app_entered_background) {
  ukm::builders::PageLoad builder(GetDelegate().GetPageUkmSourceId());
  // page_transition_ fits in a uint32_t, so we can safely cast to int64_t.
  builder.SetNavigation_PageTransition(static_cast<int64_t>(page_transition_));

  // GetDelegate().GetPageEndReason() fits in a uint32_t, so we can safely cast
  // to int64_t.
  auto page_end_reason = GetDelegate().GetPageEndReason();
  if (page_end_reason == page_load_metrics::PageEndReason::END_NONE &&
      app_entered_background) {
    page_end_reason =
        page_load_metrics::PageEndReason::END_APP_ENTER_BACKGROUND;
  }
  builder.SetNavigation_PageEndReason3(page_end_reason);
  bool is_user_initiated_navigation =
      // All browser initiated page loads are user-initiated.
      GetDelegate().GetUserInitiatedInfo().browser_initiated ||
      // Renderer-initiated navigations are user-initiated if there is an
      // associated input event.
      GetDelegate().GetUserInitiatedInfo().user_input_event;
  builder.SetExperimental_Navigation_UserInitiated(
      is_user_initiated_navigation);
  if (timing)
    RecordAbortMetrics(*timing, page_end_time, &builder);

  RecordMemoriesMetrics(builder, page_end_reason);

  builder.Record(ukm::UkmRecorder::Get());

  // Also log UserInitiated in UserPerceivedPageVisit.
  ukm::builders::UserPerceivedPageVisit(GetDelegate().GetPageUkmSourceId())
      .SetUserInitiated(is_user_initiated_navigation)
      .Record(ukm::UkmRecorder::Get());
}

absl::optional<int64_t>
UkmPageLoadMetricsObserver::GetRoundedSiteEngagementScore() const {
  if (!browser_context_)
    return absl::nullopt;

  Profile* profile = Profile::FromBrowserContext(browser_context_);
  site_engagement::SiteEngagementService* engagement_service =
      site_engagement::SiteEngagementService::Get(profile);

  // UKM privacy requires the engagement score be rounded to nearest
  // value of 10.
  int64_t rounded_document_engagement_score =
      static_cast<int>(std::roundf(
          engagement_service->GetScore(GetDelegate().GetUrl()) / 10.0)) *
      10;

  DCHECK(rounded_document_engagement_score >= 0 &&
         rounded_document_engagement_score <=
             engagement_service->GetMaxPoints());

  return rounded_document_engagement_score;
}

absl::optional<bool>
UkmPageLoadMetricsObserver::GetThirdPartyCookieBlockingEnabled() const {
  if (!browser_context_)
    return absl::nullopt;

  Profile* profile = Profile::FromBrowserContext(browser_context_);
  auto cookie_settings = CookieSettingsFactory::GetForProfile(profile);
  if (!cookie_settings->ShouldBlockThirdPartyCookies())
    return absl::nullopt;

  return !cookie_settings->IsThirdPartyAccessAllowed(GetDelegate().GetUrl(),
                                                     nullptr /* source */);
}

void UkmPageLoadMetricsObserver::OnTimingUpdate(
    content::RenderFrameHost* subframe_rfh,
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  bool loading_enabled;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("loading", &loading_enabled);
  if (!loading_enabled)
    return;
  const page_load_metrics::ContentfulPaintTimingInfo& paint =
      GetDelegate()
          .GetLargestContentfulPaintHandler()
          .MergeMainFrameAndSubframes();

  if (paint.ContainsValidTime()) {
    TRACE_EVENT_INSTANT2(
        "loading",
        "NavStartToLargestContentfulPaint::Candidate::AllFrames::UKM",
        TRACE_EVENT_SCOPE_THREAD, "data", paint.DataAsTraceValue(),
        "main_frame_tree_node_id",
        GetDelegate().GetLargestContentfulPaintHandler().MainFrameTreeNodeId());
  } else {
    TRACE_EVENT_INSTANT1(
        "loading",
        "NavStartToLargestContentfulPaint::"
        "Invalidate::AllFrames::UKM",
        TRACE_EVENT_SCOPE_THREAD, "main_frame_tree_node_id",
        GetDelegate().GetLargestContentfulPaintHandler().MainFrameTreeNodeId());
  }

  const page_load_metrics::ContentfulPaintTimingInfo&
      experimental_largest_contentful_paint =
          GetDelegate()
              .GetExperimentalLargestContentfulPaintHandler()
              .MergeMainFrameAndSubframes();
  if (experimental_largest_contentful_paint.ContainsValidTime()) {
    TRACE_EVENT_INSTANT2(
        "loading",
        "NavStartToExperimentalLargestContentfulPaint::Candidate::AllFrames::"
        "UKM",
        TRACE_EVENT_SCOPE_THREAD, "data",
        experimental_largest_contentful_paint.DataAsTraceValue(),
        "main_frame_tree_node_id",
        GetDelegate()
            .GetExperimentalLargestContentfulPaintHandler()
            .MainFrameTreeNodeId());
  } else {
    TRACE_EVENT_INSTANT1("loading",
                         "NavStartToExperimentalLargestContentfulPaint::"
                         "Invalidate::AllFrames::UKM",
                         TRACE_EVENT_SCOPE_THREAD, "main_frame_tree_node_id",
                         GetDelegate()
                             .GetExperimentalLargestContentfulPaintHandler()
                             .MainFrameTreeNodeId());
  }
}

void UkmPageLoadMetricsObserver::SetUpSharedMemoryForSmoothness(
    const base::ReadOnlySharedMemoryRegion& shared_memory) {
  ukm_smoothness_data_ = shared_memory.Map();
}

void UkmPageLoadMetricsObserver::OnCpuTimingUpdate(
    content::RenderFrameHost* subframe_rfh,
    const page_load_metrics::mojom::CpuTiming& timing) {
  if (GetDelegate().GetVisibilityTracker().currently_in_foreground() &&
      !was_hidden_)
    total_foreground_cpu_time_ += timing.task_time;
}

void UkmPageLoadMetricsObserver::DidActivatePortal(
    base::TimeTicks activation_time) {
  is_portal_ = false;
}

void UkmPageLoadMetricsObserver::RecordNoStatePrefetchMetrics(
    content::NavigationHandle* navigation_handle,
    ukm::SourceId source_id) {
  prerender::NoStatePrefetchManager* const no_state_prefetch_manager =
      prerender::NoStatePrefetchManagerFactory::GetForBrowserContext(
          navigation_handle->GetWebContents()->GetBrowserContext());
  if (!no_state_prefetch_manager)
    return;

  const std::vector<GURL>& redirects = navigation_handle->GetRedirectChain();

  base::TimeDelta prefetch_age;
  prerender::FinalStatus final_status;
  prerender::Origin prefetch_origin;

  bool no_state_prefetch_entry_found =
      no_state_prefetch_manager->GetPrefetchInformation(
          navigation_handle->GetURL(), &prefetch_age, &final_status,
          &prefetch_origin);

  // Try the URLs from the redirect chain.
  if (!no_state_prefetch_entry_found) {
    for (const auto& url : redirects) {
      no_state_prefetch_entry_found =
          no_state_prefetch_manager->GetPrefetchInformation(
              url, &prefetch_age, &final_status, &prefetch_origin);
      if (no_state_prefetch_entry_found)
        break;
    }
  }

  if (!no_state_prefetch_entry_found)
    return;

  ukm::builders::NoStatePrefetch builder(source_id);
  builder.SetPrefetchedRecently_PrefetchAge(
      ukm::GetExponentialBucketMinForUserTiming(prefetch_age.InMilliseconds()));
  builder.SetPrefetchedRecently_FinalStatus(final_status);
  builder.SetPrefetchedRecently_Origin(prefetch_origin);
  builder.Record(ukm::UkmRecorder::Get());
}

bool UkmPageLoadMetricsObserver::IsOfflinePreview(
    content::WebContents* web_contents) const {
#if BUILDFLAG(ENABLE_OFFLINE_PAGES)
  offline_pages::OfflinePageTabHelper* tab_helper =
      offline_pages::OfflinePageTabHelper::FromWebContents(web_contents);
  return tab_helper && tab_helper->GetOfflinePreviewItem();
#else
  return false;
#endif
}

void UkmPageLoadMetricsObserver::RecordGeneratedNavigationUKM(
    ukm::SourceId source_id,
    const GURL& committed_url) {
  bool final_url_is_home_page = IsUserHomePage(browser_context_, committed_url);
  bool final_url_is_default_search =
      IsDefaultSearchEngine(browser_context_, committed_url);

  if (!final_url_is_home_page && !final_url_is_default_search &&
      !start_url_is_home_page_ && !start_url_is_default_search_) {
    return;
  }

  ukm::builders::GeneratedNavigation builder(source_id);
  builder.SetFinalURLIsHomePage(final_url_is_home_page);
  builder.SetFinalURLIsDefaultSearchEngine(final_url_is_default_search);
  builder.SetFirstURLIsHomePage(start_url_is_home_page_);
  builder.SetFirstURLIsDefaultSearchEngine(start_url_is_default_search_);
  builder.Record(ukm::UkmRecorder::Get());
}
