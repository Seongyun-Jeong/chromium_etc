// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/site_affiliation/affiliation_service_impl.h"

#include "base/barrier_closure.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/containers/contains.h"
#include "base/containers/cxx20_erase.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/metrics/histogram_functions.h"
#include "base/ranges/algorithm.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/time/default_clock.h"
#include "base/time/default_tick_clock.h"
#include "components/password_manager/core/browser/android_affiliation/affiliation_backend.h"
#include "components/password_manager/core/browser/android_affiliation/affiliation_fetcher_interface.h"
#include "components/password_manager/core/browser/password_store_factory_util.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"

namespace password_manager {

namespace {

void LogFetchResult(metrics_util::GetChangePasswordUrlMetric result) {
  base::UmaHistogramEnumeration(kGetChangePasswordURLMetricName, result);
}

// Creates a look-up (Facet URI : change password URL) map for facets from
// requested |groupings|. If a facet does not have change password URL it gets
// paired with another facet's URL, which belongs to the same group. In case
// none of the group's facets have change password URLs then those facets are
// not inserted to the map.
std::map<FacetURI, AffiliationServiceImpl::ChangePasswordUrlMatch>
CreateFacetUriToChangePasswordUrlMap(
    const std::vector<GroupedFacets>& groupings) {
  std::map<FacetURI, AffiliationServiceImpl::ChangePasswordUrlMatch> uri_to_url;
  for (const auto& grouped_facets : groupings) {
    std::vector<FacetURI> uris_without_urls;
    GURL fallback_url;
    for (const auto& facet : grouped_facets) {
      if (!facet.change_password_url.is_valid()) {
        uris_without_urls.push_back(facet.uri);
        continue;
      }
      uri_to_url[facet.uri] = AffiliationServiceImpl::ChangePasswordUrlMatch{
          .change_password_url = facet.change_password_url,
          .group_url_override = false};
      fallback_url = facet.change_password_url;
    }
    if (fallback_url.is_valid()) {
      for (const auto& uri : uris_without_urls) {
        uri_to_url[uri] = AffiliationServiceImpl::ChangePasswordUrlMatch{
            .change_password_url = fallback_url, .group_url_override = true};
      }
    }
  }
  return uri_to_url;
}

}  // namespace

const char kGetChangePasswordURLMetricName[] =
    "PasswordManager.AffiliationService.GetChangePasswordUsage";

struct AffiliationServiceImpl::FetchInfo {
  FetchInfo(std::unique_ptr<AffiliationFetcherInterface> pending_fetcher,
            std::vector<url::SchemeHostPort> tuple_origins,
            base::OnceClosure result_callback)
      : fetcher(std::move(pending_fetcher)),
        requested_tuple_origins(std::move(tuple_origins)),
        callback(std::move(result_callback)) {}

  FetchInfo(FetchInfo&& other) = default;

  FetchInfo& operator=(FetchInfo&& other) = default;

  ~FetchInfo() {
    // The check is essential here, because emplace_back calls move constructor
    // and destructor, respectively. Therefore, the check is necessary to
    // prevent accessing already moved object.
    if (callback)
      std::move(callback).Run();
  }

  std::unique_ptr<AffiliationFetcherInterface> fetcher;
  std::vector<url::SchemeHostPort> requested_tuple_origins;
  // Callback is passed in PrefetchChangePasswordURLs and is run to indicate the
  // prefetch has finished or got canceled.
  base::OnceClosure callback;
};

// TODO(crbug.com/1246291): Create the backend task runner in Init and stop
// passing it in the constructor.
AffiliationServiceImpl::AffiliationServiceImpl(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    scoped_refptr<base::SequencedTaskRunner> backend_task_runner)
    : url_loader_factory_(std::move(url_loader_factory)),
      fetcher_factory_(std::make_unique<AffiliationFetcherFactoryImpl>()),
      backend_task_runner_(std::move(backend_task_runner)) {}

AffiliationServiceImpl::~AffiliationServiceImpl() = default;

void AffiliationServiceImpl::Init(
    network::NetworkConnectionTracker* network_connection_tracker,
    const base::FilePath& db_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  backend_ = new AffiliationBackend(backend_task_runner_,
                                    base::DefaultClock::GetInstance(),
                                    base::DefaultTickClock::GetInstance());

  backend_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&AffiliationBackend::Initialize,
                     base::Unretained(backend_), url_loader_factory_->Clone(),
                     base::Unretained(network_connection_tracker), db_path));
}

void AffiliationServiceImpl::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (backend_) {
    backend_task_runner_->DeleteSoon(FROM_HERE, backend_.get());
    backend_ = nullptr;
  }
}

void AffiliationServiceImpl::PrefetchChangePasswordURLs(
    const std::vector<GURL>& urls,
    base::OnceClosure callback) {
  std::vector<FacetURI> facets;
  std::vector<url::SchemeHostPort> tuple_origins;
  for (const auto& url : urls) {
    if (url.is_valid()) {
      url::SchemeHostPort scheme_host_port(url);
      if (!base::Contains(change_password_urls_, scheme_host_port)) {
        facets.push_back(
            FacetURI::FromCanonicalSpec(scheme_host_port.Serialize()));
        tuple_origins.push_back(std::move(scheme_host_port));
      }
    }
  }
  if (!facets.empty()) {
    auto fetcher = fetcher_factory_->CreateInstance(url_loader_factory_, this);
    fetcher->StartRequest(facets,
                          /*request_info=*/{.change_password_info = true});
    pending_fetches_.emplace_back(std::move(fetcher), tuple_origins,
                                  std::move(callback));
  }
}

void AffiliationServiceImpl::Clear() {
  pending_fetches_.clear();
  change_password_urls_.clear();
}

GURL AffiliationServiceImpl::GetChangePasswordURL(const GURL& url) const {
  auto it = change_password_urls_.find(url::SchemeHostPort(url));
  if (it != change_password_urls_.end()) {
    if (it->second.group_url_override) {
      LogFetchResult(
          metrics_util::GetChangePasswordUrlMetric::kGroupUrlOverrideUsed);
    } else {
      LogFetchResult(
          metrics_util::GetChangePasswordUrlMetric::kUrlOverrideUsed);
    }
    return it->second.change_password_url;
  }

  url::SchemeHostPort tuple(url);
  if (base::ranges::any_of(pending_fetches_, [&tuple](const auto& info) {
        return base::Contains(info.requested_tuple_origins, tuple);
      })) {
    LogFetchResult(metrics_util::GetChangePasswordUrlMetric::kNotFetchedYet);
  } else {
    LogFetchResult(
        metrics_util::GetChangePasswordUrlMetric::kNoUrlOverrideAvailable);
  }
  return GURL();
}

void AffiliationServiceImpl::OnFetchSucceeded(
    AffiliationFetcherInterface* fetcher,
    std::unique_ptr<AffiliationFetcherDelegate::Result> result) {
  auto processed_fetch =
      base::ranges::find(pending_fetches_, fetcher,
                         [](const auto& info) { return info.fetcher.get(); });
  if (processed_fetch == pending_fetches_.end())
    return;

  std::map<FacetURI, AffiliationServiceImpl::ChangePasswordUrlMatch>
      uri_to_url = CreateFacetUriToChangePasswordUrlMap(result->groupings);
  for (const auto& requested_tuple : processed_fetch->requested_tuple_origins) {
    auto it = uri_to_url.find(
        FacetURI::FromPotentiallyInvalidSpec(requested_tuple.Serialize()));
    if (it != uri_to_url.end()) {
      change_password_urls_[requested_tuple] = it->second;
    }
  }

  pending_fetches_.erase(processed_fetch);
}

void AffiliationServiceImpl::OnFetchFailed(
    AffiliationFetcherInterface* fetcher) {
  base::EraseIf(pending_fetches_, [fetcher](const auto& info) {
    return info.fetcher.get() == fetcher;
  });
}

void AffiliationServiceImpl::OnMalformedResponse(
    AffiliationFetcherInterface* fetcher) {
  base::EraseIf(pending_fetches_, [fetcher](const auto& info) {
    return info.fetcher.get() == fetcher;
  });
}

void AffiliationServiceImpl::GetAffiliationsAndBranding(
    const FacetURI& facet_uri,
    AffiliationService::StrategyOnCacheMiss cache_miss_strategy,
    ResultCallback result_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(backend_);
  backend_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&AffiliationBackend::GetAffiliationsAndBranding,
                                base::Unretained(backend_), facet_uri,
                                cache_miss_strategy, std::move(result_callback),
                                base::SequencedTaskRunnerHandle::Get()));
}

void AffiliationServiceImpl::Prefetch(const FacetURI& facet_uri,
                                      const base::Time& keep_fresh_until) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(backend_);
  backend_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&AffiliationBackend::Prefetch, base::Unretained(backend_),
                     facet_uri, keep_fresh_until));
}

void AffiliationServiceImpl::CancelPrefetch(
    const FacetURI& facet_uri,
    const base::Time& keep_fresh_until) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(backend_);
  backend_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&AffiliationBackend::CancelPrefetch,
                     base::Unretained(backend_), facet_uri, keep_fresh_until));
}

void AffiliationServiceImpl::KeepPrefetchForFacets(
    std::vector<FacetURI> facet_uris) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(backend_);
  backend_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&AffiliationBackend::KeepPrefetchForFacets,
                     base::Unretained(backend_), std::move(facet_uris)));
}

void AffiliationServiceImpl::TrimCacheForFacetURI(const FacetURI& facet_uri) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(backend_);
  backend_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&AffiliationBackend::TrimCacheForFacetURI,
                                base::Unretained(backend_), facet_uri));
}

void AffiliationServiceImpl::TrimUnusedCache(std::vector<FacetURI> facet_uris) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(backend_);
  backend_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&AffiliationBackend::TrimUnusedCache,
                     base::Unretained(backend_), std::move(facet_uris)));
}

void AffiliationServiceImpl::InjectAffiliationAndBrandingInformation(
    std::vector<std::unique_ptr<PasswordForm>> forms,
    AffiliationService::StrategyOnCacheMiss strategy_on_cache_miss,
    PasswordFormsCallback result_callback) {
  std::vector<PasswordForm*> android_credentials;
  for (const auto& form : forms) {
    if (IsValidAndroidCredential(PasswordFormDigest(*form)))
      android_credentials.push_back(form.get());
  }

  // Create a closure that runs after affiliations are injected and
  // CompleteInjectAffiliationAndBrandingInformation is called for
  // all forms in |android_credentials|.
  base::OnceClosure on_get_all_realms(
      base::BindOnce(std::move(result_callback), std::move(forms)));
  base::RepeatingClosure barrier_closure = base::BarrierClosure(
      android_credentials.size(), std::move(on_get_all_realms));

  for (auto* form : android_credentials) {
    // |forms| are not destroyed until the |barrier_closure| executes,
    // making it safe to use base::Unretained(form) below.
    GetAffiliationsAndBranding(
        FacetURI::FromPotentiallyInvalidSpec(form->signon_realm),
        strategy_on_cache_miss,
        base::BindOnce(&AffiliationServiceImpl::
                           CompleteInjectAffiliationAndBrandingInformation,
                       weak_ptr_factory_.GetWeakPtr(), base::Unretained(form),
                       barrier_closure));
  }
}

void AffiliationServiceImpl::CompleteInjectAffiliationAndBrandingInformation(
    PasswordForm* form,
    base::OnceClosure barrier_closure,
    const AffiliatedFacets& results,
    bool success) {
  const FacetURI facet_uri(
      FacetURI::FromPotentiallyInvalidSpec(form->signon_realm));

  // Facet can also be web URI, in this case we do nothing.
  if (!success || !facet_uri.IsValidAndroidFacetURI()) {
    std::move(barrier_closure).Run();
    return;
  }

  // Inject branding information into the form (e.g. the Play Store name and
  // icon URL). We expect to always find a matching facet URI in the results.
  auto facet = base::ranges::find(results, facet_uri, &Facet::uri);

  DCHECK(facet != results.end());
  form->app_display_name = facet->branding_info.name;
  form->app_icon_url = facet->branding_info.icon_url;

  // Inject the affiliated web realm into the form, if available. In case
  // multiple web realms are available, this will always choose the first
  // available web realm for injection.
  auto affiliated_facet = std::find_if(
      results.begin(), results.end(), [](const Facet& affiliated_facet) {
        return affiliated_facet.uri.IsValidWebFacetURI();
      });
  if (affiliated_facet != results.end())
    form->affiliated_web_realm = affiliated_facet->uri.canonical_spec() + "/";

  std::move(barrier_closure).Run();
}

// static
bool AffiliationServiceImpl::IsValidAndroidCredential(
    const PasswordFormDigest& form) {
  return form.scheme == PasswordForm::Scheme::kHtml &&
         IsValidAndroidFacetURI(form.signon_realm);
}

}  // namespace password_manager
