// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/navigation_throttle_runner.h"

#include "base/metrics/histogram_functions.h"
#include "base/strings/strcat.h"
#include "content/browser/devtools/devtools_instrumentation.h"
#include "content/browser/portal/portal_navigation_throttle.h"
#include "content/browser/prerender/prerender_navigation_throttle.h"
#include "content/browser/prerender/prerender_subframe_navigation_throttle.h"
#include "content/browser/renderer_host/ancestor_throttle.h"
#include "content/browser/renderer_host/blocked_scheme_navigation_throttle.h"
#include "content/browser/renderer_host/http_error_navigation_throttle.h"
#include "content/browser/renderer_host/isolated_app_throttle.h"
#include "content/browser/renderer_host/mixed_content_navigation_throttle.h"
#include "content/browser/renderer_host/navigation_request.h"
#include "content/browser/renderer_host/navigator_delegate.h"
#include "content/browser/renderer_host/origin_policy_throttle.h"
#include "content/browser/webid/federated_auth_navigation_throttle.h"
#include "content/public/browser/navigation_handle.h"

namespace content {

namespace {

NavigationThrottle::ThrottleCheckResult ExecuteNavigationEvent(
    NavigationThrottle* throttle,
    NavigationThrottleRunner::Event event) {
  switch (event) {
    case NavigationThrottleRunner::Event::WillStartRequest:
      return throttle->WillStartRequest();
    case NavigationThrottleRunner::Event::WillRedirectRequest:
      return throttle->WillRedirectRequest();
    case NavigationThrottleRunner::Event::WillFailRequest:
      return throttle->WillFailRequest();
    case NavigationThrottleRunner::Event::WillProcessResponse:
      return throttle->WillProcessResponse();
    default:
      NOTREACHED();
  }
  NOTREACHED();
  return NavigationThrottle::CANCEL_AND_IGNORE;
}

const char* GetEventName(NavigationThrottleRunner::Event event) {
  switch (event) {
    case NavigationThrottleRunner::Event::WillStartRequest:
      return "NavigationThrottle::WillStartRequest";
    case NavigationThrottleRunner::Event::WillRedirectRequest:
      return "NavigationThrottle::WillRedirectRequest";
    case NavigationThrottleRunner::Event::WillFailRequest:
      return "NavigationThrottle::WillFailRequest";
    case NavigationThrottleRunner::Event::WillProcessResponse:
      return "NavigationThrottle::WillProcessResponse";
    default:
      NOTREACHED();
  }
  return "";
}

const char* GetEventNameForHistogram(NavigationThrottleRunner::Event event) {
  switch (event) {
    case NavigationThrottleRunner::Event::WillStartRequest:
      return "WillStartRequest";
    case NavigationThrottleRunner::Event::WillRedirectRequest:
      return "WillRedirectRequest";
    case NavigationThrottleRunner::Event::WillFailRequest:
      return "WillFailRequest";
    case NavigationThrottleRunner::Event::WillProcessResponse:
      return "WillProcessResponse";
    default:
      NOTREACHED();
  }
  return "";
}

void RecordHistogram(NavigationThrottleRunner::Event event,
                     base::Time start,
                     const std::string& metric_type) {
  base::TimeDelta delta = base::Time::Now() - start;
  base::UmaHistogramTimes(base::StrCat({"Navigation.Throttle", metric_type, ".",
                                        GetEventNameForHistogram(event)}),
                          delta);
}

void RecordDeferTimeHistogram(NavigationThrottleRunner::Event event,
                              base::Time start) {
  RecordHistogram(event, start, "DeferTime");
}

void RecordExecutionTimeHistogram(NavigationThrottleRunner::Event event,
                                  base::Time start) {
  RecordHistogram(event, start, "ExecutionTime");
}

}  // namespace

NavigationThrottleRunner::NavigationThrottleRunner(Delegate* delegate,
                                                   int64_t navigation_id)
    : delegate_(delegate), navigation_id_(navigation_id) {}

NavigationThrottleRunner::~NavigationThrottleRunner() = default;

void NavigationThrottleRunner::ProcessNavigationEvent(Event event) {
  DCHECK_NE(Event::NoEvent, event);
  current_event_ = event;
  next_index_ = 0;
  ProcessInternal();
}

void NavigationThrottleRunner::ResumeProcessingNavigationEvent(
    NavigationThrottle* deferring_throttle) {
  DCHECK_EQ(GetDeferringThrottle(), deferring_throttle);
  RecordDeferTimeHistogram(current_event_, defer_start_time_);
  ProcessInternal();
}

void NavigationThrottleRunner::CallResumeForTesting() {
  ProcessInternal();
}

void NavigationThrottleRunner::RegisterNavigationThrottles() {
  // Note: |throttle_| might not be empty. Some NavigationThrottles might have
  // been registered with RegisterThrottleForTesting. These must reside at the
  // end of |throttles_|. TestNavigationManagerThrottle expects that the
  // NavigationThrottles added for test are the last NavigationThrottles to
  // execute. Take them out while appending the rest of the
  // NavigationThrottles.
  std::vector<std::unique_ptr<NavigationThrottle>> testing_throttles =
      std::move(throttles_);

  // The NavigationRequest associated with the NavigationThrottles this
  // NavigationThrottleRunner manages.
  // Unit tests that do not use NavigationRequest should never call
  // RegisterNavigationThrottles as this function expects |delegate_| to be a
  // NavigationRequest.
  NavigationRequest* request = static_cast<NavigationRequest*>(delegate_);

  throttles_ = request->GetDelegate()->CreateThrottlesForNavigation(request);

  // Check for renderer-inititated main frame navigations to blocked URL schemes
  // (data, filesystem). This is done early as it may block the main frame
  // navigation altogether.
  AddThrottle(
      BlockedSchemeNavigationThrottle::CreateThrottleForNavigation(request));

  AddThrottle(AncestorThrottle::MaybeCreateThrottleFor(request));

  // Check for mixed content. This is done after the AncestorThrottle and the
  // FormSubmissionThrottle so that when folks block mixed content with a CSP
  // policy, they don't get a warning. They'll still get a warning in the
  // console about CSP blocking the load.
  AddThrottle(
      MixedContentNavigationThrottle::CreateThrottleForNavigation(request));

  // Handle Origin Policy (if enabled)
  AddThrottle(OriginPolicyThrottle::MaybeCreateThrottleFor(request));

  // Block certain requests that are not permitted for portals.
  AddThrottle(PortalNavigationThrottle::MaybeCreateThrottleFor(request));

  // Block certain requests that are not permitted for prerendering.
  AddThrottle(PrerenderNavigationThrottle::MaybeCreateThrottleFor(request));

  // Defer cross-origin subframe loading during prerendering state.
  AddThrottle(
      PrerenderSubframeNavigationThrottle::MaybeCreateThrottleFor(request));

  // Intercept federated identity requests.
  AddThrottle(FederatedAuthNavigationThrottle::MaybeCreateThrottleFor(request));

  // Prevent navigations to/from isolated apps.
  AddThrottle(IsolatedAppThrottle::MaybeCreateThrottleFor(request));

  for (auto& throttle :
       devtools_instrumentation::CreateNavigationThrottles(request)) {
    AddThrottle(std::move(throttle));
  }

  // Make main frame navigations with error HTTP status code and an empty body
  // commit an error page instead. Note that this should take lower priority
  // than other throttles that might care about those navigations, e.g.
  // throttles handling pages with 407 errors that require extra authentication.
  AddThrottle(HttpErrorNavigationThrottle::MaybeCreateThrottleFor(*request));

  // Insert all testing NavigationThrottles last.
  throttles_.insert(throttles_.end(),
                    std::make_move_iterator(testing_throttles.begin()),
                    std::make_move_iterator(testing_throttles.end()));
}

NavigationThrottle* NavigationThrottleRunner::GetDeferringThrottle() const {
  if (next_index_ == 0)
    return nullptr;
  return throttles_[next_index_ - 1].get();
}

void NavigationThrottleRunner::AddThrottle(
    std::unique_ptr<NavigationThrottle> navigation_throttle) {
  if (navigation_throttle)
    throttles_.push_back(std::move(navigation_throttle));
}

void NavigationThrottleRunner::ProcessInternal() {
  DCHECK_NE(Event::NoEvent, current_event_);
  base::WeakPtr<NavigationThrottleRunner> weak_ref = weak_factory_.GetWeakPtr();

  // Capture into a local variable the |navigation_id_| value, since this
  // object can be freed by any of the throttles being invoked and the trace
  // events need to be able to use the navigation id safely in such a case.
  int64_t local_navigation_id = navigation_id_;

  for (size_t i = next_index_; i < throttles_.size(); ++i) {
    TRACE_EVENT_NESTABLE_ASYNC_BEGIN1(
        "navigation", GetEventName(current_event_), local_navigation_id,
        "throttle", throttles_[i]->GetNameForLogging());

    base::Time start = base::Time::Now();
    NavigationThrottle::ThrottleCheckResult result =
        ExecuteNavigationEvent(throttles_[i].get(), current_event_);
    if (!weak_ref) {
      // The NavigationThrottle execution has destroyed this
      // NavigationThrottleRunner. Return immediately.
      TRACE_EVENT_NESTABLE_ASYNC_END1("navigation", "", local_navigation_id,
                                      "result", "deleted");
      return;
    }
    RecordExecutionTimeHistogram(current_event_, start);
    TRACE_EVENT_NESTABLE_ASYNC_END1("navigation", GetEventName(current_event_),
                                    local_navigation_id, "result",
                                    result.action());

    switch (result.action()) {
      case NavigationThrottle::PROCEED:
        continue;

      case NavigationThrottle::BLOCK_REQUEST_AND_COLLAPSE:
      case NavigationThrottle::BLOCK_REQUEST:
      case NavigationThrottle::BLOCK_RESPONSE:
      case NavigationThrottle::CANCEL:
      case NavigationThrottle::CANCEL_AND_IGNORE:
        next_index_ = 0;
        InformDelegate(result);
        return;

      case NavigationThrottle::DEFER:
        next_index_ = i + 1;
        defer_start_time_ = base::Time::Now();
        return;
    }
  }

  next_index_ = 0;
  InformDelegate(NavigationThrottle::PROCEED);
}

void NavigationThrottleRunner::InformDelegate(
    const NavigationThrottle::ThrottleCheckResult& result) {
  // Now that the event has executed, reset the current event to NoEvent since
  // we're no longer processing any event. Do it before the call to the
  // delegate, as it might lead to the deletion of this
  // NavigationThrottleRunner.
  Event event = current_event_;
  current_event_ = Event::NoEvent;
  delegate_->OnNavigationEventProcessed(event, result);
  // DO NOT ADD CODE AFTER THIS. The NavigationThrottleRunner might have been
  // deleted by the previous call.
}

}  // namespace content
