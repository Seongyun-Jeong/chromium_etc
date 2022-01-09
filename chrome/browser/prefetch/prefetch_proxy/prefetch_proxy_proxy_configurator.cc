// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_proxy_configurator.h"

#include "base/barrier_closure.h"
#include "base/metrics/histogram_functions.h"
#include "base/rand_util.h"
#include "base/time/default_clock.h"
#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_params.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_features.h"
#include "google_apis/google_api_keys.h"
#include "net/base/host_port_pair.h"
#include "net/base/proxy_server.h"
#include "net/base/proxy_string_util.h"
#include "net/http/http_status_code.h"
#include "net/proxy_resolution/proxy_config.h"
#include "url/gurl.h"

PrefetchProxyProxyConfigurator::PrefetchProxyProxyConfigurator()
    : prefetch_proxy_server_(net::ProxyServer(
          net::GetSchemeFromUriScheme(PrefetchProxyProxyHost().scheme()),
          net::HostPortPair::FromURL(PrefetchProxyProxyHost()))),
      clock_(base::DefaultClock::GetInstance()) {
  DCHECK(PrefetchProxyProxyHost().is_valid());

  std::string header_value = "key=" + google_apis::GetAPIKey();
  std::string server_experiment_group = PrefetchProxyServerExperimentGroup();
  if (server_experiment_group != "") {
    header_value += ",exp=" + server_experiment_group;
  }

  connect_tunnel_headers_.SetHeader(PrefetchProxyProxyHeaderKey(),
                                    header_value);
}

PrefetchProxyProxyConfigurator::~PrefetchProxyProxyConfigurator() = default;

void PrefetchProxyProxyConfigurator::SetClockForTesting(
    const base::Clock* clock) {
  clock_ = clock;
}

void PrefetchProxyProxyConfigurator::AddCustomProxyConfigClient(
    mojo::Remote<network::mojom::CustomProxyConfigClient> config_client,
    base::OnceCallback<void()> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  proxy_config_clients_.Add(std::move(config_client));
  UpdateCustomProxyConfig(std::move(callback));
}

void PrefetchProxyProxyConfigurator::UpdateCustomProxyConfig(
    base::OnceCallback<void()> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!PrefetchProxyIsEnabled()) {
    std::move(callback).Run();
    return;
  }

  base::RepeatingClosure repeating_closure =
      base::BarrierClosure(proxy_config_clients_.size(), std::move(callback));
  network::mojom::CustomProxyConfigPtr config = CreateCustomProxyConfig();
  for (auto& client : proxy_config_clients_) {
    client->OnCustomProxyConfigUpdated(config->Clone(), repeating_closure);
  }
}

network::mojom::CustomProxyConfigPtr
PrefetchProxyProxyConfigurator::CreateCustomProxyConfig() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto config = network::mojom::CustomProxyConfig::New();
  config->rules.type =
      net::ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME;

  // DIRECT is intentionally not added here because we want the proxy to always
  // be used in order to mask the user's IP address during the prerender.
  config->rules.proxies_for_https.AddProxyServer(prefetch_proxy_server_);

  // This ensures that the user's set proxy is honored, although we also disable
  // the feature is such cases.
  config->should_override_existing_config = false;
  config->allow_non_idempotent_methods = false;
  config->connect_tunnel_headers = connect_tunnel_headers_;
  return config;
}

mojo::PendingRemote<network::mojom::CustomProxyConnectionObserver>
PrefetchProxyProxyConfigurator::NewProxyConnectionObserverRemote() {
  mojo::PendingRemote<network::mojom::CustomProxyConnectionObserver>
      observer_remote;
  observer_receivers_.Add(this,
                          observer_remote.InitWithNewPipeAndPassReceiver());
  // The disconnect handler is intentionally not set since ReceiverSet manages
  // connection clean up on disconnect.
  return observer_remote;
}

void PrefetchProxyProxyConfigurator::OnFallback(
    const net::ProxyServer& bad_proxy,
    int net_error) {
  if (bad_proxy != prefetch_proxy_server_) {
    return;
  }

  base::UmaHistogramSparse("PrefetchProxy.Proxy.Fallback.NetError",
                           std::abs(net_error));

  OnTunnelProxyConnectionError(absl::nullopt);
}

void PrefetchProxyProxyConfigurator::OnTunnelHeadersReceived(
    const net::ProxyServer& proxy_server,
    const scoped_refptr<net::HttpResponseHeaders>& response_headers) {
  DCHECK(response_headers);

  if (proxy_server != prefetch_proxy_server_) {
    return;
  }

  base::UmaHistogramSparse("PrefetchProxy.Proxy.RespCode",
                           response_headers->response_code());

  if (response_headers->response_code() == net::HTTP_OK) {
    return;
  }

  std::string retry_after_string;
  if (response_headers->EnumerateHeader(nullptr, "Retry-After",
                                        &retry_after_string)) {
    base::TimeDelta retry_after;
    if (net::HttpUtil::ParseRetryAfterHeader(retry_after_string, clock_->Now(),
                                             &retry_after)) {
      OnTunnelProxyConnectionError(retry_after);
      return;
    }
  }

  OnTunnelProxyConnectionError(absl::nullopt);
}

bool PrefetchProxyProxyConfigurator::IsPrefetchProxyAvailable() const {
  if (!prefetch_proxy_not_available_until_) {
    return true;
  }

  return prefetch_proxy_not_available_until_.value() <= clock_->Now();
}

void PrefetchProxyProxyConfigurator::OnTunnelProxyConnectionError(
    absl::optional<base::TimeDelta> retry_after) {
  base::Time retry_proxy_at;
  if (retry_after) {
    retry_proxy_at = clock_->Now() + *retry_after;
  } else {
    // Pick a random value between 1-5 mins if the proxy didn't give us a
    // Retry-After value. The randomness will help ensure there is no sudden
    // wave of requests following a proxy error.
    retry_proxy_at = clock_->Now() + base::Seconds(base::RandInt(
                                         base::Time::kSecondsPerMinute,
                                         5 * base::Time::kSecondsPerMinute));
  }
  DCHECK(!retry_proxy_at.is_null());

  // If there is already a value in |prefetch_proxy_not_available_until_|,
  // probably due to some race, take the max.
  if (prefetch_proxy_not_available_until_) {
    prefetch_proxy_not_available_until_ =
        std::max(*prefetch_proxy_not_available_until_, retry_proxy_at);
  } else {
    prefetch_proxy_not_available_until_ = retry_proxy_at;
  }
  DCHECK(prefetch_proxy_not_available_until_);

  // TODO(crbug/1136114): Consider saving persisting to prefs.
}
