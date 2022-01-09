// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/optimization_guide/core/hints_fetcher.h"

#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/time/default_clock.h"
#include "components/optimization_guide/core/hints_processing_util.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/optimization_guide/core/optimization_guide_prefs.h"
#include "components/optimization_guide/core/optimization_guide_switches.h"
#include "components/optimization_guide/core/optimization_guide_util.h"
#include "components/optimization_guide/proto/hints.pb.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/variations/net/variations_http_headers.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/network_connection_tracker.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace optimization_guide {

namespace {

// Returns the string that can be used to record histograms for the request
// context.
//
// Keep in sync with RequestContext variant list in
// //tools/metrics/histograms/metadata/optimization/histograms.xml.
std::string GetStringNameForRequestContext(
    proto::RequestContext request_context) {
  switch (request_context) {
    case proto::RequestContext::CONTEXT_UNSPECIFIED:
    case proto::RequestContext::CONTEXT_BATCH_UPDATE_MODELS:
      NOTREACHED();
      return "Unknown";
    case proto::RequestContext::CONTEXT_PAGE_NAVIGATION:
      return "PageNavigation";
    case proto::RequestContext::CONTEXT_BATCH_UPDATE_GOOGLE_SRP:
      return "BatchUpdateGoogleSRP";
    case proto::RequestContext::CONTEXT_BATCH_UPDATE_ACTIVE_TABS:
      return "BatchUpdateActiveTabs";
    case proto::RequestContext::CONTEXT_BOOKMARKS:
      return "Bookmarks";
  }
  NOTREACHED();
  return std::string();
}

void RecordRequestStatusHistogram(proto::RequestContext request_context,
                                  HintsFetcherRequestStatus status) {
  base::UmaHistogramEnumeration(
      "OptimizationGuide.HintsFetcher.RequestStatus." +
          GetStringNameForRequestContext(request_context),
      status);
}

}  // namespace

HintsFetcher::HintsFetcher(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const GURL& optimization_guide_service_url,
    PrefService* pref_service,
    network::NetworkConnectionTracker* network_connection_tracker)
    : optimization_guide_service_url_(net::AppendOrReplaceQueryParameter(
          optimization_guide_service_url,
          "key",
          features::GetOptimizationGuideServiceAPIKey())),
      pref_service_(pref_service),
      network_connection_tracker_(network_connection_tracker),
      time_clock_(base::DefaultClock::GetInstance()) {
  url_loader_factory_ = std::move(url_loader_factory);
  // Allow non-https scheme only when it is overridden in command line. This is
  // needed for iOS EG2 tests which don't support HTTPS embedded test servers
  // due to ssl certificate validation. So, the EG2 tests use HTTP hints
  // servers.
  CHECK(optimization_guide_service_url_.SchemeIs(url::kHttpsScheme) ||
        base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kOptimizationGuideServiceGetHintsURL));
  DCHECK(features::IsRemoteFetchingEnabled());
}

HintsFetcher::~HintsFetcher() {
  if (active_url_loader_) {
    if (hints_fetched_callback_)
      std::move(hints_fetched_callback_).Run(absl::nullopt);
    base::UmaHistogramExactLinear(
        "OptimizationGuide.HintsFetcher.GetHintsRequest."
        "ActiveRequestCanceled." +
            GetStringNameForRequestContext(request_context_),
        1, 1);
  }
}

// static
void HintsFetcher::ClearHostsSuccessfullyFetched(PrefService* pref_service) {
  DictionaryPrefUpdate hosts_fetched_list(
      pref_service, prefs::kHintsFetcherHostsSuccessfullyFetched);
  hosts_fetched_list->DictClear();
}

void HintsFetcher::SetTimeClockForTesting(const base::Clock* time_clock) {
  time_clock_ = time_clock;
}

// static
bool HintsFetcher::WasHostCoveredByFetch(PrefService* pref_service,
                                         const std::string& host) {
  return WasHostCoveredByFetch(pref_service, host,
                               base::DefaultClock::GetInstance());
}

// static
bool HintsFetcher::WasHostCoveredByFetch(PrefService* pref_service,
                                         const std::string& host,
                                         const base::Clock* time_clock) {
  if (!optimization_guide::features::ShouldPersistHintsToDisk()) {
    // Don't consult the pref if we aren't even persisting hints to disk.
    return false;
  }

  DictionaryPrefUpdate hosts_fetched(
      pref_service, prefs::kHintsFetcherHostsSuccessfullyFetched);
  absl::optional<double> value =
      hosts_fetched->FindDoubleKey(HashHostForDictionary(host));
  if (!value)
    return false;

  base::Time host_valid_time =
      base::Time::FromDeltaSinceWindowsEpoch(base::Seconds(*value));
  return host_valid_time > time_clock->Now();
}

// static
void HintsFetcher::ClearSingleFetchedHost(PrefService* pref_service,
                                          const std::string& host) {
  DictionaryPrefUpdate hosts_fetched_list(
      pref_service, prefs::kHintsFetcherHostsSuccessfullyFetched);
  hosts_fetched_list->RemovePath(HashHostForDictionary(host));
}

// static
void HintsFetcher::AddFetchedHostForTesting(PrefService* pref_service,
                                            const std::string& host,
                                            base::Time time) {
  DictionaryPrefUpdate hosts_fetched_list(
      pref_service, prefs::kHintsFetcherHostsSuccessfullyFetched);
  hosts_fetched_list->SetDoubleKey(
      HashHostForDictionary(host),
      time.ToDeltaSinceWindowsEpoch().InSecondsF());
}

bool HintsFetcher::FetchOptimizationGuideServiceHints(
    const std::vector<std::string>& hosts,
    const std::vector<GURL>& urls,
    const base::flat_set<optimization_guide::proto::OptimizationType>&
        optimization_types,
    optimization_guide::proto::RequestContext request_context,
    const std::string& locale,
    HintsFetchedCallback hints_fetched_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_GT(optimization_types.size(), 0u);
  request_context_ = request_context;

  if (network_connection_tracker_->IsOffline()) {
    RecordRequestStatusHistogram(request_context_,
                                 HintsFetcherRequestStatus::kNetworkOffline);
    std::move(hints_fetched_callback).Run(absl::nullopt);
    return false;
  }

  if (active_url_loader_) {
    RecordRequestStatusHistogram(request_context_,
                                 HintsFetcherRequestStatus::kFetcherBusy);
    std::move(hints_fetched_callback).Run(absl::nullopt);
    return false;
  }

  std::vector<std::string> filtered_hosts =
      GetSizeLimitedHostsDueForHintsRefresh(hosts);
  std::vector<GURL> valid_urls = GetSizeLimitedURLsForFetching(urls);
  if (filtered_hosts.empty() && valid_urls.empty()) {
    RecordRequestStatusHistogram(
        request_context_, HintsFetcherRequestStatus::kNoHostsOrURLsToFetch);
    std::move(hints_fetched_callback).Run(absl::nullopt);
    return false;
  }

  DCHECK_GE(features::MaxHostsForOptimizationGuideServiceHintsFetch(),
            filtered_hosts.size());
  DCHECK_GE(features::MaxUrlsForOptimizationGuideServiceHintsFetch(),
            valid_urls.size());

  if (optimization_types.empty()) {
    RecordRequestStatusHistogram(
        request_context_,
        HintsFetcherRequestStatus::kNoSupportedOptimizationTypes);
    std::move(hints_fetched_callback).Run(absl::nullopt);
    return false;
  }

  hints_fetch_start_time_ = base::TimeTicks::Now();

  proto::GetHintsRequest get_hints_request;
  get_hints_request.add_supported_key_representations(proto::HOST);
  get_hints_request.add_supported_key_representations(proto::FULL_URL);

  for (const auto& optimization_type : optimization_types)
    get_hints_request.add_supported_optimizations(optimization_type);

  get_hints_request.set_context(request_context_);

  *get_hints_request.mutable_active_field_trials() =
      GetActiveFieldTrialsAllowedForFetch();

  get_hints_request.set_locale(locale);

  for (const auto& url : valid_urls)
    get_hints_request.add_urls()->set_url(url.spec());

  for (const auto& host : filtered_hosts) {
    proto::HostInfo* host_info = get_hints_request.add_hosts();
    host_info->set_host(host);
  }

  std::string serialized_request;
  get_hints_request.SerializeToString(&serialized_request);

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("hintsfetcher_gethintsrequest", R"(
        semantics {
          sender: "HintsFetcher"
          description:
            "Requests Hints from the Optimization Guide Service for use in "
            "providing data saving and pageload optimizations for Chrome."
          trigger:
            "Requested periodically if Data Saver is enabled and the browser "
            "has Hints that are older than a threshold set by "
            "the server."
          data: "A list of the user's most engaged websites."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: NO
          setting:
            "Users can control Data Saver on Android via 'Data Saver' setting. "
            "Data Saver is not available on iOS."
          policy_exception_justification: "Not implemented."
        })");

  auto resource_request = std::make_unique<network::ResourceRequest>();

  resource_request->url = optimization_guide_service_url_;

  resource_request->method = "POST";
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  active_url_loader_ = variations::CreateSimpleURLLoaderWithVariationsHeader(
      std::move(resource_request),
      // This is always InIncognito::kNo as the OptimizationGuideKeyedService is
      // not enabled on incognito sessions and is rechecked before each fetch.
      variations::InIncognito::kNo, variations::SignedIn::kNo,
      traffic_annotation);

  active_url_loader_->AttachStringForUpload(serialized_request,
                                            "application/x-protobuf");

  UMA_HISTOGRAM_COUNTS_100(
      "OptimizationGuide.HintsFetcher.GetHintsRequest.HostCount",
      filtered_hosts.size());
  UMA_HISTOGRAM_COUNTS_100(
      "OptimizationGuide.HintsFetcher.GetHintsRequest.UrlCount",
      valid_urls.size());

  // |active_url_loader_| should not retry on 5xx errors since the server may
  // already be overloaded. |active_url_loader_| should retry on network changes
  // since the network stack may receive the connection change event later than
  // |this|.
  static const int kMaxRetries = 1;
  active_url_loader_->SetRetryOptions(
      kMaxRetries, network::SimpleURLLoader::RETRY_ON_NETWORK_CHANGE);

  // It's safe to use |base::Unretained(this)| here because |this| owns
  // |active_url_loader_| and the callback will be canceled if
  // |active_url_loader_| is destroyed.
  active_url_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&HintsFetcher::OnURLLoadComplete, base::Unretained(this)));

  hints_fetched_callback_ = std::move(hints_fetched_callback);
  hosts_fetched_ = filtered_hosts;
  return true;
}

void HintsFetcher::HandleResponse(const std::string& get_hints_response_data,
                                  int net_status,
                                  int response_code) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::unique_ptr<proto::GetHintsResponse> get_hints_response =
      std::make_unique<proto::GetHintsResponse>();

  UMA_HISTOGRAM_ENUMERATION(
      "OptimizationGuide.HintsFetcher.GetHintsRequest.Status",
      static_cast<net::HttpStatusCode>(response_code),
      net::HTTP_VERSION_NOT_SUPPORTED);
  // Net error codes are negative but histogram enums must be positive.
  base::UmaHistogramSparse(
      "OptimizationGuide.HintsFetcher.GetHintsRequest.NetErrorCode",
      -net_status);

  if (net_status == net::OK && response_code == net::HTTP_OK &&
      get_hints_response->ParseFromString(get_hints_response_data)) {
    UMA_HISTOGRAM_COUNTS_100(
        "OptimizationGuide.HintsFetcher.GetHintsRequest.HintCount",
        get_hints_response->hints_size());
    base::TimeDelta fetch_latency =
        base::TimeTicks::Now() - hints_fetch_start_time_;
    UMA_HISTOGRAM_MEDIUM_TIMES(
        "OptimizationGuide.HintsFetcher.GetHintsRequest.FetchLatency",
        fetch_latency);
    base::UmaHistogramMediumTimes(
        "OptimizationGuide.HintsFetcher.GetHintsRequest.FetchLatency." +
            GetStringNameForRequestContext(request_context_),
        fetch_latency);
    base::TimeDelta valid_duration =
        features::StoredFetchedHintsFreshnessDuration();
    if (get_hints_response->has_max_cache_duration()) {
      valid_duration =
          base::Seconds(get_hints_response->max_cache_duration().seconds());
    }
    UpdateHostsSuccessfullyFetched(valid_duration);
    RecordRequestStatusHistogram(request_context_,
                                 HintsFetcherRequestStatus::kSuccess);
    std::move(hints_fetched_callback_).Run(std::move(get_hints_response));
  } else {
    hosts_fetched_.clear();
    RecordRequestStatusHistogram(request_context_,
                                 HintsFetcherRequestStatus::kResponseError);
    std::move(hints_fetched_callback_).Run(absl::nullopt);
  }
}

void HintsFetcher::UpdateHostsSuccessfullyFetched(
    base::TimeDelta valid_duration) {
  if (!optimization_guide::features::ShouldPersistHintsToDisk()) {
    // Do not persist any state if we aren't persisting hints to disk.
    return;
  }

  DictionaryPrefUpdate hosts_fetched_list(
      pref_service_, prefs::kHintsFetcherHostsSuccessfullyFetched);

  // Remove any expired hosts.
  std::vector<std::string> entries_to_remove;
  for (auto it : hosts_fetched_list->DictItems()) {
    if (base::Time::FromDeltaSinceWindowsEpoch(
            base::Seconds(it.second.GetDouble())) < time_clock_->Now()) {
      entries_to_remove.emplace_back(it.first);
    }
  }
  for (const auto& host : entries_to_remove) {
    hosts_fetched_list->RemovePath(host);
  }

  if (hosts_fetched_.empty())
    return;

  // Ensure there is enough space in the dictionary pref for the
  // most recent set of hosts to be stored.
  if (hosts_fetched_list->DictSize() + hosts_fetched_.size() >
      features::MaxHostsForRecordingSuccessfullyCovered()) {
    entries_to_remove.clear();
    size_t num_entries_to_remove =
        hosts_fetched_list->DictSize() + hosts_fetched_.size() -
        features::MaxHostsForRecordingSuccessfullyCovered();
    for (auto it : hosts_fetched_list->DictItems()) {
      if (entries_to_remove.size() >= num_entries_to_remove)
        break;
      entries_to_remove.emplace_back(it.first);
    }
    for (const auto& host : entries_to_remove) {
      hosts_fetched_list->RemovePath(host);
    }
  }

  // Add the covered hosts in |hosts_fetched_| to the dictionary pref.
  base::Time host_invalid_time = time_clock_->Now() + valid_duration;
  for (const std::string& host : hosts_fetched_) {
    hosts_fetched_list->SetDoubleKey(
        HashHostForDictionary(host),
        host_invalid_time.ToDeltaSinceWindowsEpoch().InSecondsF());
  }
  DCHECK_LE(hosts_fetched_list->DictSize(),
            features::MaxHostsForRecordingSuccessfullyCovered());
  hosts_fetched_.clear();
}

// Callback is only invoked if |active_url_loader_| is bound and still alive.
void HintsFetcher::OnURLLoadComplete(
    std::unique_ptr<std::string> response_body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  int response_code = -1;
  if (active_url_loader_->ResponseInfo() &&
      active_url_loader_->ResponseInfo()->headers) {
    response_code =
        active_url_loader_->ResponseInfo()->headers->response_code();
  }
  auto net_error = active_url_loader_->NetError();
  // Reset the active URL loader here since actions happening during response
  // handling may destroy |this|.
  active_url_loader_.reset();

  HandleResponse(response_body ? *response_body : "", net_error, response_code);
}

// Returns the subset of URLs from |urls| for which the URL is considered
// valid and can be included in a hints fetch.
std::vector<GURL> HintsFetcher::GetSizeLimitedURLsForFetching(
    const std::vector<GURL>& urls) const {
  std::vector<GURL> valid_urls;
  for (size_t i = 0; i < urls.size(); i++) {
    if (valid_urls.size() >=
        features::MaxUrlsForOptimizationGuideServiceHintsFetch()) {
      base::UmaHistogramCounts100(
          "OptimizationGuide.HintsFetcher.GetHintsRequest.DroppedUrls." +
              GetStringNameForRequestContext(request_context_),
          urls.size() - i);
      break;
    }
    if (IsValidURLForURLKeyedHint(urls[i]))
      valid_urls.push_back(urls[i]);
  }
  return valid_urls;
}

std::vector<std::string> HintsFetcher::GetSizeLimitedHostsDueForHintsRefresh(
    const std::vector<std::string>& hosts) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DictionaryPrefUpdate hosts_fetched(
      pref_service_, prefs::kHintsFetcherHostsSuccessfullyFetched);

  std::vector<std::string> target_hosts;
  target_hosts.reserve(hosts.size());

  for (size_t i = 0; i < hosts.size(); i++) {
    if (target_hosts.size() >=
        features::MaxHostsForOptimizationGuideServiceHintsFetch()) {
      base::UmaHistogramCounts100(
          "OptimizationGuide.HintsFetcher.GetHintsRequest.DroppedHosts." +
              GetStringNameForRequestContext(request_context_),
          hosts.size() - i);
      break;
    }

    std::string host = hosts[i];
    // Skip over localhosts, IP addresses, and invalid hosts.
    if (net::HostStringIsLocalhost(host))
      continue;
    url::CanonHostInfo host_info;
    std::string canonicalized_host(net::CanonicalizeHost(host, &host_info));
    if (host_info.IsIPAddress() ||
        !net::IsCanonicalizedHostCompliant(canonicalized_host)) {
      continue;
    }

    bool host_hints_due_for_refresh = true;

    absl::optional<double> value =
        hosts_fetched->FindDoubleKey(HashHostForDictionary(host));
    if (value && optimization_guide::features::ShouldPersistHintsToDisk()) {
      base::Time host_valid_time =
          base::Time::FromDeltaSinceWindowsEpoch(base::Seconds(*value));
      host_hints_due_for_refresh =
          (host_valid_time - features::GetHostHintsFetchRefreshDuration() <=
           time_clock_->Now());
    }
    if (host_hints_due_for_refresh)
      target_hosts.push_back(host);
  }
  DCHECK_GE(features::MaxHostsForOptimizationGuideServiceHintsFetch(),
            target_hosts.size());
  return target_hosts;
}

}  // namespace optimization_guide
