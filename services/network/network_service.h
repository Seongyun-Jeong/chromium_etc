// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_NETWORK_SERVICE_H_
#define SERVICES_NETWORK_NETWORK_SERVICE_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/component_export.h"
#include "base/containers/flat_set.h"
#include "base/containers/span.h"
#include "base/containers/unique_ptr_adapters.h"
#include "base/feature_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/dns/host_resolver.h"
#include "net/dns/public/secure_dns_mode.h"
#include "net/log/net_log.h"
#include "net/log/trace_net_log_observer.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/first_party_sets/first_party_sets.h"
#include "services/network/keepalive_statistics_recorder.h"
#include "services/network/network_change_manager.h"
#include "services/network/network_quality_estimator_manager.h"
#include "services/network/public/cpp/network_service_buildflags.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "services/network/public/mojom/net_log.mojom.h"
#include "services/network/public/mojom/network_change_manager.mojom.h"
#include "services/network/public/mojom/network_quality_estimator_manager.mojom.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "services/network/public/mojom/trust_tokens.mojom.h"
#include "services/network/public/mojom/url_loader_network_service_observer.mojom.h"
#include "services/network/trust_tokens/trust_token_key_commitments.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

#if BUILDFLAG(IS_CT_SUPPORTED)
#include "services/network/public/mojom/ct_log_info.mojom.h"
#endif

namespace net {
class FileNetLogObserver;
class HostResolverManager;
class HttpAuthHandlerFactory;
class LoggingNetworkChangeObserver;
class NetworkQualityEstimator;
class URLRequestContext;
}  // namespace net

namespace network {

class CRLSetDistributor;
class CtLogListDistributor;
class DnsConfigChangeManager;
class HttpAuthCacheCopier;
class NetLogProxySink;
class NetworkContext;
class NetworkService;
class SCTAuditingCache;

class COMPONENT_EXPORT(NETWORK_SERVICE) NetworkService
    : public mojom::NetworkService {
 public:
  static const base::TimeDelta kInitialDohProbeTimeout;

  NetworkService(std::unique_ptr<service_manager::BinderRegistry> registry,
                 mojo::PendingReceiver<mojom::NetworkService> receiver =
                     mojo::NullReceiver(),
                 bool delay_initialization_until_set_client = false);

  NetworkService(const NetworkService&) = delete;
  NetworkService& operator=(const NetworkService&) = delete;

  ~NetworkService() override;

  // Allows late binding if the mojo receiver wasn't specified in the
  // constructor.
  void Bind(mojo::PendingReceiver<mojom::NetworkService> receiver);

  // Allows the browser process to synchronously initialize the NetworkService.
  // TODO(jam): remove this once the old path is gone.
  void Initialize(mojom::NetworkServiceParamsPtr params,
                  bool mock_network_change_notifier = false);

  // Creates a NetworkService instance on the current thread.
  static std::unique_ptr<NetworkService> Create(
      mojo::PendingReceiver<mojom::NetworkService> receiver);

  // Creates a testing instance of NetworkService not bound to an actual
  // Service pipe. This instance must be driven by direct calls onto the
  // NetworkService object.
  static std::unique_ptr<NetworkService> CreateForTesting();

  // These are called by NetworkContexts as they are being created and
  // destroyed.
  // TODO(mmenke):  Remove once all NetworkContexts are owned by the
  // NetworkService.
  void RegisterNetworkContext(NetworkContext* network_context);
  void DeregisterNetworkContext(NetworkContext* network_context);

  // Invokes net::CreateNetLogEntriesForActiveObjects(observer) on all
  // URLRequestContext's known to |this|.
  void CreateNetLogEntriesForActiveObjects(
      net::NetLog::ThreadSafeObserver* observer);

  // mojom::NetworkService implementation:
  void SetParams(mojom::NetworkServiceParamsPtr params) override;
#if BUILDFLAG(IS_CHROMEOS_ASH)
  void ReinitializeLogging(mojom::LoggingSettingsPtr settings) override;
#endif
  void StartNetLog(base::File file,
                   net::NetLogCaptureMode capture_mode,
                   base::Value constants) override;
  void AttachNetLogProxy(
      mojo::PendingRemote<mojom::NetLogProxySource> proxy_source,
      mojo::PendingReceiver<mojom::NetLogProxySink>) override;
  void SetSSLKeyLogFile(base::File file) override;
  void CreateNetworkContext(
      mojo::PendingReceiver<mojom::NetworkContext> receiver,
      mojom::NetworkContextParamsPtr params) override;
  void ConfigureStubHostResolver(
      bool insecure_dns_client_enabled,
      net::SecureDnsMode secure_dns_mode,
      const std::vector<net::DnsOverHttpsServerConfig>& dns_over_https_servers,
      bool additional_dns_types_enabled) override;
  void DisableQuic() override;
  void SetUpHttpAuth(
      mojom::HttpAuthStaticParamsPtr http_auth_static_params) override;
  void ConfigureHttpAuthPrefs(
      mojom::HttpAuthDynamicParamsPtr http_auth_dynamic_params) override;
  void SetRawHeadersAccess(int32_t process_id,
                           const std::vector<url::Origin>& origins) override;
  void SetMaxConnectionsPerProxy(int32_t max_connections) override;
  void GetNetworkChangeManager(
      mojo::PendingReceiver<mojom::NetworkChangeManager> receiver) override;
  void GetNetworkQualityEstimatorManager(
      mojo::PendingReceiver<mojom::NetworkQualityEstimatorManager> receiver)
      override;
  void GetDnsConfigChangeManager(
      mojo::PendingReceiver<mojom::DnsConfigChangeManager> receiver) override;
  void GetNetworkList(
      uint32_t policy,
      mojom::NetworkService::GetNetworkListCallback callback) override;
  void UpdateCRLSet(
      base::span<const uint8_t> crl_set,
      mojom::NetworkService::UpdateCRLSetCallback callback) override;
  void OnCertDBChanged() override;
  void SetEncryptionKey(const std::string& encryption_key) override;
  void AddAllowedRequestInitiatorForPlugin(
      int32_t process_id,
      const url::Origin& allowed_request_initiator) override;
  void RemoveSecurityExceptionsForPlugin(int32_t process_id) override;
  void OnMemoryPressure(base::MemoryPressureListener::MemoryPressureLevel
                            memory_pressure_level) override;
  void OnPeerToPeerConnectionsCountChange(uint32_t count) override;
#if defined(OS_ANDROID)
  void OnApplicationStateChange(base::android::ApplicationState state) override;
#endif
  void SetEnvironment(
      std::vector<mojom::EnvironmentVariablePtr> environment) override;
  void SetTrustTokenKeyCommitments(const std::string& raw_commitments,
                                   base::OnceClosure done) override;
  void ParseHeaders(const GURL& url,
                    const scoped_refptr<net::HttpResponseHeaders>& headers,
                    ParseHeadersCallback callback) override;
#if BUILDFLAG(IS_CT_SUPPORTED)
  void ClearSCTAuditingCache() override;
  void ConfigureSCTAuditing(bool enabled,
                            double sampling_rate,
                            const GURL& reporting_uri,
                            const net::MutableNetworkTrafficAnnotationTag&
                                traffic_annotation) override;
  void UpdateCtLogList(std::vector<mojom::CTLogInfoPtr> log_list,
                       base::Time update_time) override;
  void SetCtEnforcementEnabled(bool enabled) override;
#endif

#if defined(OS_ANDROID)
  void DumpWithoutCrashing(base::Time dump_request_time) override;
#endif
  void BindTestInterface(
      mojo::PendingReceiver<mojom::NetworkServiceTest> receiver) override;
  void SetFirstPartySets(base::File sets_file) override;
  void SetPersistedFirstPartySetsAndGetCurrentSets(
      const std::string& persisted_sets,
      mojom::NetworkService::SetPersistedFirstPartySetsAndGetCurrentSetsCallback
          callback) override;
  void SetExplicitlyAllowedPorts(const std::vector<uint16_t>& ports) override;

  // Returns an HttpAuthHandlerFactory for the given NetworkContext.
  std::unique_ptr<net::HttpAuthHandlerFactory> CreateHttpAuthHandlerFactory(
      NetworkContext* network_context);

  bool quic_disabled() const { return quic_disabled_; }
  bool HasRawHeadersAccess(int32_t process_id, const GURL& resource_url) const;

  bool IsInitiatorAllowedForPlugin(int process_id,
                                   const url::Origin& request_initiator);

  net::NetworkQualityEstimator* network_quality_estimator() {
    return network_quality_estimator_manager_->GetNetworkQualityEstimator();
  }
  net::NetLog* net_log() const;
  KeepaliveStatisticsRecorder* keepalive_statistics_recorder() {
    return &keepalive_statistics_recorder_;
  }
  net::HostResolverManager* host_resolver_manager() {
    return host_resolver_manager_.get();
  }
  net::HostResolver::Factory* host_resolver_factory() {
    return host_resolver_factory_.get();
  }
  HttpAuthCacheCopier* http_auth_cache_copier() {
    return http_auth_cache_copier_.get();
  }

  CRLSetDistributor* crl_set_distributor() {
    return crl_set_distributor_.get();
  }

#if BUILDFLAG(IS_CT_SUPPORTED)
  CtLogListDistributor* ct_log_list_distributor() {
    return ct_log_list_distributor_.get();
  }
#endif

  FirstPartySets* first_party_sets() const { return first_party_sets_.get(); }

  void set_host_resolver_factory_for_testing(
      std::unique_ptr<net::HostResolver::Factory> host_resolver_factory) {
    host_resolver_factory_ = std::move(host_resolver_factory);
  }

  bool split_auth_cache_by_network_isolation_key() const {
    return split_auth_cache_by_network_isolation_key_;
  }

  // From initialization on, this will be non-null and will always point to the
  // same object (although the object's state can change on updates to the
  // commitments). As a consequence, it's safe to store long-lived copies of the
  // pointer.
  const TrustTokenKeyCommitments* trust_token_key_commitments() const {
    return trust_token_key_commitments_.get();
  }

#if BUILDFLAG(IS_CT_SUPPORTED)
  SCTAuditingCache* sct_auditing_cache() { return sct_auditing_cache_.get(); }

  const std::vector<mojom::CTLogInfoPtr>& log_list() const { return log_list_; }

  base::Time ct_log_list_update_time() const {
    return ct_log_list_update_time_;
  }
#endif

  mojom::URLLoaderNetworkServiceObserver*
  GetDefaultURLLoaderNetworkServiceObserver();

  static NetworkService* GetNetworkServiceForTesting();

 private:
  class DelayedDohProbeActivator;

  void DestroyNetworkContexts();

  // Called by a NetworkContext when its mojo pipe is closed. Deletes the
  // context.
  void OnNetworkContextConnectionClosed(NetworkContext* network_context);

  // Sets First-Party Set data after having read it from a file.
  void OnReadFirstPartySetsFile(const std::string& raw_sets);

  bool initialized_ = false;

  raw_ptr<net::NetLog> net_log_;

  std::unique_ptr<NetLogProxySink> net_log_proxy_sink_;

  std::unique_ptr<net::FileNetLogObserver> file_net_log_observer_;
  net::TraceNetLogObserver trace_net_log_observer_;

  KeepaliveStatisticsRecorder keepalive_statistics_recorder_;

  std::unique_ptr<NetworkChangeManager> network_change_manager_;

  // Observer that logs network changes to the NetLog. Must be below the NetLog
  // and the NetworkChangeNotifier (Once this class creates it), so it's
  // destroyed before them. Must be below the |network_change_manager_|, which
  // it references.
  std::unique_ptr<net::LoggingNetworkChangeObserver> network_change_observer_;

  std::unique_ptr<service_manager::BinderRegistry> registry_;

  // Globally-scoped state for First-Party Sets. Must be above the `receiver_`
  // so it's destroyed after, to make sure even when the reply callback owned by
  // the `first_party_sets_` is never run when destroyed, the receiver which the
  // reply callback associated with is already disconnected.
  std::unique_ptr<FirstPartySets> first_party_sets_;

  mojo::Receiver<mojom::NetworkService> receiver_{this};

  mojo::Remote<mojom::URLLoaderNetworkServiceObserver>
      default_url_loader_network_service_observer_;

  std::unique_ptr<NetworkQualityEstimatorManager>
      network_quality_estimator_manager_;

  std::unique_ptr<DnsConfigChangeManager> dns_config_change_manager_;

  std::unique_ptr<net::HostResolverManager> host_resolver_manager_;
  std::unique_ptr<net::HostResolver::Factory> host_resolver_factory_;
  std::unique_ptr<HttpAuthCacheCopier> http_auth_cache_copier_;

  // Members that would store the http auth network_service related params.
  // These Params are later used by NetworkContext to create
  // HttpAuthPreferences.
  mojom::HttpAuthDynamicParamsPtr http_auth_dynamic_network_service_params_;
  mojom::HttpAuthStaticParamsPtr http_auth_static_network_service_params_;

  // NetworkContexts created by CreateNetworkContext(). They call into the
  // NetworkService when their connection is closed so that it can delete
  // them.  It will also delete them when the NetworkService itself is torn
  // down, as NetworkContexts share global state owned by the NetworkService, so
  // must be destroyed first.
  //
  // NetworkContexts created by CreateNetworkContextWithBuilder() are not owned
  // by the NetworkService, and must be destroyed by their owners before the
  // NetworkService itself is.
  std::set<std::unique_ptr<NetworkContext>, base::UniquePtrComparator>
      owned_network_contexts_;

  // List of all NetworkContexts that are associated with the NetworkService,
  // including ones it does not own.
  // TODO(mmenke): Once the NetworkService always owns NetworkContexts, merge
  // this with |owned_network_contexts_|.
  std::set<NetworkContext*> network_contexts_;

  // A per-process_id map of origins that are white-listed to allow
  // them to request raw headers for resources they request.
  std::map<int32_t, base::flat_set<url::Origin>>
      raw_headers_access_origins_by_pid_;

  bool quic_disabled_ = false;

  std::unique_ptr<CRLSetDistributor> crl_set_distributor_;

  // Whether new NetworkContexts will be configured to partition their
  // HttpAuthCaches by NetworkIsolationKey.
  bool split_auth_cache_by_network_isolation_key_ = false;

  // Globally-scoped cryptographic state for the Trust Tokens protocol
  // (https://github.com/wicg/trust-token-api), updated via a Mojo IPC and
  // provided to NetworkContexts via the getter.
  std::unique_ptr<TrustTokenKeyCommitments> trust_token_key_commitments_;

  std::unique_ptr<DelayedDohProbeActivator> doh_probe_activator_;

#if BUILDFLAG(IS_CT_SUPPORTED)
  std::unique_ptr<SCTAuditingCache> sct_auditing_cache_;

  std::vector<mojom::CTLogInfoPtr> log_list_;

  std::unique_ptr<CtLogListDistributor> ct_log_list_distributor_;

  base::Time ct_log_list_update_time_;
#endif

  // Map from a renderer process id, to the set of plugin origins embedded by
  // that renderer process (the renderer will proxy requests from PPAPI - such
  // requests should have their initiator origin within the set stored here).
  std::map<int, std::set<url::Origin>> plugin_origins_;
};

}  // namespace network

#endif  // SERVICES_NETWORK_NETWORK_SERVICE_H_
