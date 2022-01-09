// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/loading_predictor_page_load_metrics_observer.h"

#include <memory>

#include "chrome/browser/predictors/loading_predictor.h"
#include "chrome/browser/predictors/loading_predictor_factory.h"
#include "chrome/browser/predictors/loading_predictor_tab_helper.h"
#include "chrome/browser/predictors/resource_prefetch_predictor.h"
#include "chrome/browser/profiles/profile.h"
#include "components/page_load_metrics/browser/page_load_metrics_util.h"
#include "content/public/browser/web_contents.h"

namespace internal {

const char kHistogramLoadingPredictorFirstContentfulPaintPreconnectable[] =
    "PageLoad.Clients.LoadingPredictor.PaintTiming."
    "NavigationToFirstContentfulPaint.Preconnectable";
const char kHistogramLoadingPredictorFirstMeaningfulPaintPreconnectable[] =
    "PageLoad.Clients.LoadingPredictor.Experimental.PaintTiming."
    "NavigationToFirstMeaningfulPaint.Preconnectable";

}  // namespace internal

// static
std::unique_ptr<LoadingPredictorPageLoadMetricsObserver>
LoadingPredictorPageLoadMetricsObserver::CreateIfNeeded(
    content::WebContents* web_contents) {
  auto* loading_predictor = predictors::LoadingPredictorFactory::GetForProfile(
      Profile::FromBrowserContext(web_contents->GetBrowserContext()));
  auto* loading_predictor_tab_helper =
      predictors::LoadingPredictorTabHelper::FromWebContents(web_contents);
  if (!loading_predictor || !loading_predictor_tab_helper)
    return nullptr;
  return std::make_unique<LoadingPredictorPageLoadMetricsObserver>(
      loading_predictor->resource_prefetch_predictor(),
      loading_predictor_tab_helper);
}

LoadingPredictorPageLoadMetricsObserver::
    LoadingPredictorPageLoadMetricsObserver(
        predictors::ResourcePrefetchPredictor* predictor,
        predictors::LoadingPredictorTabHelper* predictor_tab_helper)
    : predictor_(predictor),
      predictor_tab_helper_(predictor_tab_helper),
      record_histogram_preconnectable_(false) {
  DCHECK(predictor_);
  DCHECK(predictor_tab_helper_);
}

LoadingPredictorPageLoadMetricsObserver::
    ~LoadingPredictorPageLoadMetricsObserver() {}

page_load_metrics::PageLoadMetricsObserver::ObservePolicy
LoadingPredictorPageLoadMetricsObserver::OnStart(
    content::NavigationHandle* navigation_handle,
    const GURL& currently_commited_url,
    bool started_in_foreground) {
  record_histogram_preconnectable_ =
      started_in_foreground &&
      predictor_->IsUrlPreconnectable(navigation_handle->GetURL());

  return CONTINUE_OBSERVING;
}

page_load_metrics::PageLoadMetricsObserver::ObservePolicy
LoadingPredictorPageLoadMetricsObserver::OnHidden(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  record_histogram_preconnectable_ = false;
  return CONTINUE_OBSERVING;
}

void LoadingPredictorPageLoadMetricsObserver::OnFirstContentfulPaintInPage(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  // TODO(https://crbug.com/1190112): The code uses the primary FrameTree, but
  // this event may have been dispatched for a non-primary FrameTree.
  auto* web_contents = GetDelegate().GetWebContents();
  auto* frame = web_contents->GetMainFrame();

  predictor_tab_helper_->RecordFirstContentfulPaint(
      frame, GetDelegate().GetNavigationStart() +
                 timing.paint_timing->first_contentful_paint.value());
  if (record_histogram_preconnectable_) {
    PAGE_LOAD_HISTOGRAM(
        internal::kHistogramLoadingPredictorFirstContentfulPaintPreconnectable,
        timing.paint_timing->first_contentful_paint.value());
  }
}

void LoadingPredictorPageLoadMetricsObserver::
    OnFirstMeaningfulPaintInMainFrameDocument(
        const page_load_metrics::mojom::PageLoadTiming& timing) {
  if (record_histogram_preconnectable_) {
    PAGE_LOAD_HISTOGRAM(
        internal::kHistogramLoadingPredictorFirstMeaningfulPaintPreconnectable,
        timing.paint_timing->first_meaningful_paint.value());
  }
}
