// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_worker/embedded_worker_instance.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/containers/contains.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_macros.h"
#include "base/trace_event/trace_event.h"
#include "content/browser/bad_message.h"
#include "content/browser/data_url_loader_factory.h"
#include "content/browser/devtools/devtools_instrumentation.h"
#include "content/browser/devtools/network_service_devtools_observer.h"
#include "content/browser/devtools/service_worker_devtools_agent_host.h"
#include "content/browser/devtools/service_worker_devtools_manager.h"
#include "content/browser/net/cross_origin_embedder_policy_reporter.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/service_worker/embedded_worker_status.h"
#include "content/browser/service_worker/service_worker_consts.h"
#include "content/browser/service_worker/service_worker_content_settings_proxy_impl.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"
#include "content/browser/service_worker/service_worker_host.h"
#include "content/browser/service_worker/service_worker_script_loader_factory.h"
#include "content/browser/storage_partition_impl.h"
#include "content/browser/url_loader_factory_getter.h"
#include "content/browser/url_loader_factory_params_helper.h"
#include "content/common/content_switches_internal.h"
#include "content/common/url_schemes.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/child_process_host.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "ipc/ipc_message.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "net/base/isolation_info.h"
#include "net/base/network_isolation_key.h"
#include "net/cookies/site_for_cookies.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/mojom/loader/url_loader_factory_bundle.mojom.h"
#include "third_party/blink/public/mojom/renderer_preference_watcher.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_object.mojom.h"
#include "url/gurl.h"

// TODO(crbug.com/824858): Much of this file, which dealt with thread hops
// between UI and IO, can likely be simplified when the service worker core
// thread moves to the UI thread.

namespace content {

namespace {

// When a service worker version's failure count exceeds
// |kMaxSameProcessFailureCount|, the embedded worker is forced to start in a
// new process.
const int kMaxSameProcessFailureCount = 2;

const char kServiceWorkerTerminationCanceledMesage[] =
    "Service Worker termination by a timeout timer was canceled because "
    "DevTools is attached.";

bool HasSentStartWorker(EmbeddedWorkerInstance::StartingPhase phase) {
  switch (phase) {
    case EmbeddedWorkerInstance::NOT_STARTING:
    case EmbeddedWorkerInstance::ALLOCATING_PROCESS:
      return false;
    case EmbeddedWorkerInstance::SENT_START_WORKER:
    case EmbeddedWorkerInstance::SCRIPT_DOWNLOADING:
    case EmbeddedWorkerInstance::SCRIPT_STREAMING:
    case EmbeddedWorkerInstance::SCRIPT_LOADED:
    case EmbeddedWorkerInstance::SCRIPT_EVALUATION:
      return true;
    case EmbeddedWorkerInstance::STARTING_PHASE_MAX_VALUE:
      NOTREACHED();
  }
  return false;
}

void NotifyForegroundServiceWorker(bool added, int process_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  RenderProcessHost* rph = RenderProcessHost::FromID(process_id);
  if (!rph)
    return;

  if (added)
    rph->OnForegroundServiceWorkerAdded();
  else
    rph->OnForegroundServiceWorkerRemoved();
}

}  // namespace

// Created when a renderer process is allocated for the worker. It is destroyed
// when the worker stops, and this proxies notifications to DevToolsManager.
// Owned by EmbeddedWorkerInstance.
//
// TODO(https://crbug.com/1138155): Remove this because we no longer need
// proxying the notifications because there's no thread hopping thanks to
// ServiceWorkerOnUI.
class EmbeddedWorkerInstance::DevToolsProxy {
 public:
  DevToolsProxy(int process_id,
                int agent_route_id,
                const base::UnguessableToken& devtools_id)
      : process_id_(process_id),
        agent_route_id_(agent_route_id),
        devtools_id_(devtools_id) {}

  DevToolsProxy(const DevToolsProxy&) = delete;
  DevToolsProxy& operator=(const DevToolsProxy&) = delete;

  ~DevToolsProxy() {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    ServiceWorkerDevToolsManager::GetInstance()->WorkerStopped(process_id_,
                                                               agent_route_id_);
  }

  void NotifyWorkerReadyForInspection(
      mojo::PendingRemote<blink::mojom::DevToolsAgent> agent_remote,
      mojo::PendingReceiver<blink::mojom::DevToolsAgentHost> host_receiver) {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    ServiceWorkerDevToolsManager::GetInstance()->WorkerReadyForInspection(
        process_id_, agent_route_id_, std::move(agent_remote),
        std::move(host_receiver));
  }

  void NotifyWorkerVersionInstalled() {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    ServiceWorkerDevToolsManager::GetInstance()->WorkerVersionInstalled(
        process_id_, agent_route_id_);
  }

  bool ShouldNotifyWorkerStopIgnored() const {
    return !worker_stop_ignored_notified_;
  }

  void WorkerStopIgnoredNotified() { worker_stop_ignored_notified_ = true; }

  int agent_route_id() const { return agent_route_id_; }

  const base::UnguessableToken& devtools_id() const { return devtools_id_; }

 private:
  const int process_id_;
  const int agent_route_id_;
  const base::UnguessableToken devtools_id_;
  bool worker_stop_ignored_notified_ = false;
};

// A handle for a renderer process managed by ServiceWorkerProcessManager.
//
// TODO(https://crbug.com/1138155): Remove this as a clean up of
// ServiceWorkerOnUI.
class EmbeddedWorkerInstance::WorkerProcessHandle {
 public:
  WorkerProcessHandle(
      const base::WeakPtr<ServiceWorkerProcessManager>& process_manager,
      int embedded_worker_id,
      int process_id)
      : process_manager_(process_manager),
        embedded_worker_id_(embedded_worker_id),
        process_id_(process_id) {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    DCHECK_NE(ChildProcessHost::kInvalidUniqueID, process_id_);
  }

  WorkerProcessHandle(const WorkerProcessHandle&) = delete;
  WorkerProcessHandle& operator=(const WorkerProcessHandle&) = delete;

  ~WorkerProcessHandle() {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    process_manager_->ReleaseWorkerProcess(embedded_worker_id_);
  }

  int process_id() const { return process_id_; }

 private:
  base::WeakPtr<ServiceWorkerProcessManager> process_manager_;

  const int embedded_worker_id_;
  const int process_id_;
};

// Info that is recorded as UMA on OnStarted().
struct EmbeddedWorkerInstance::StartInfo {
  StartInfo(bool is_installed,
            bool skip_recording_startup_time,
            base::TimeTicks start_time)
      : is_installed(is_installed),
        skip_recording_startup_time(skip_recording_startup_time),
        start_time(start_time) {}
  ~StartInfo() = default;

  // Used for UMA.
  const bool is_installed;
  bool skip_recording_startup_time;
  const base::TimeTicks start_time;
  base::TimeTicks start_worker_sent_time;
};

EmbeddedWorkerInstance::~EmbeddedWorkerInstance() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  ReleaseProcess();
}

void EmbeddedWorkerInstance::Start(
    blink::mojom::EmbeddedWorkerStartParamsPtr params,
    StatusCallback callback) {
  TRACE_EVENT1("ServiceWorker", "EmbeddedWorkerInstance::Start", "script_url",
               params->script_url.spec());

  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(context_);
  restart_count_++;
  DCHECK_EQ(EmbeddedWorkerStatus::STOPPED, status_);

  DCHECK_NE(blink::mojom::kInvalidServiceWorkerVersionId,
            params->service_worker_version_id);

  auto start_time = base::TimeTicks::Now();
  status_ = EmbeddedWorkerStatus::STARTING;
  starting_phase_ = ALLOCATING_PROCESS;
  network_accessed_for_script_ = false;
  token_ = blink::ServiceWorkerToken();

  for (auto& observer : listener_list_)
    observer.OnStarting();

  // service_worker_route_id will be set later in SetupOnUIThread
  params->service_worker_route_id = MSG_ROUTING_NONE;
  params->wait_for_debugger = false;
  params->subresource_loader_updater =
      subresource_loader_updater_.BindNewPipeAndPassReceiver();
  params->service_worker_token = token_.value();

  // TODO(https://crbug.com/978694): Consider a reset flow since new mojo types
  // check is_bound strictly.
  client_.reset();

  auto process_info =
      std::make_unique<ServiceWorkerProcessManager::AllocatedProcessInfo>();
  std::unique_ptr<EmbeddedWorkerInstance::DevToolsProxy> devtools_proxy;
  std::unique_ptr<blink::PendingURLLoaderFactoryBundle>
      factory_bundle_for_new_scripts;
  std::unique_ptr<blink::PendingURLLoaderFactoryBundle>
      factory_bundle_for_renderer;
  mojo::PendingReceiver<blink::mojom::ReportingObserver>
      reporting_observer_receiver;

  ServiceWorkerProcessManager* process_manager = context_->process_manager();
  if (!process_manager) {
    OnSetupFailed(std::move(callback),
                  blink::ServiceWorkerStatusCode::kErrorAbort);
    return;
  }

  // Get a process.
  bool can_use_existing_process =
      context_->GetVersionFailureCount(params->service_worker_version_id) <
      kMaxSameProcessFailureCount;
  blink::ServiceWorkerStatusCode status =
      process_manager->AllocateWorkerProcess(
          embedded_worker_id(), params->script_url,
          owner_version_->cross_origin_embedder_policy(),
          can_use_existing_process, process_info.get());
  if (status != blink::ServiceWorkerStatusCode::kOk) {
    OnSetupFailed(std::move(callback), status);
    return;
  }
  const int process_id = process_info->process_id;
  RenderProcessHost* rph = RenderProcessHost::FromID(process_id);
  // TODO(falken): This CHECK should no longer fail, so turn to a DCHECK it if
  // crash reports agree. Consider also checking for
  // rph->IsInitializedAndNotDead().
  CHECK(rph);

  GetContentClient()->browser()->WillStartServiceWorker(
      process_manager->browser_context(), params->script_url, rph);

  rph->BindReceiver(client_.BindNewPipeAndPassReceiver());
  client_.set_disconnect_handler(
      base::BindOnce(&EmbeddedWorkerInstance::Detach, base::Unretained(this)));

  {
    // Create COEP reporter if COEP value is already available (= this worker is
    // not a worker which is going to be newly registered). The Mojo remote
    // `coep_reporter_` has the onwership of the instance. The `coep_reporter`
    // might be kept null when the COEP value is not known because the main
    // script has not been loaded yet. In that case, it will be bound after the
    // main script is loaded.
    mojo::PendingRemote<network::mojom::CrossOriginEmbedderPolicyReporter>
        coep_reporter_for_devtools;
    mojo::PendingRemote<network::mojom::CrossOriginEmbedderPolicyReporter>
        coep_reporter_for_scripts;
    mojo::PendingRemote<network::mojom::CrossOriginEmbedderPolicyReporter>
        coep_reporter_for_subresources;
    if (owner_version_->cross_origin_embedder_policy()) {
      mojo::PendingRemote<blink::mojom::ReportingObserver>
          reporting_observer_remote;
      owner_version_->set_reporting_observer_receiver(
          reporting_observer_remote.InitWithNewPipeAndPassReceiver());
      auto* storage_partition =
          static_cast<StoragePartitionImpl*>(rph->GetStoragePartition());
      coep_reporter_ = std::make_unique<CrossOriginEmbedderPolicyReporter>(
          storage_partition->GetWeakPtr(), params->script_url,
          owner_version_->cross_origin_embedder_policy()->reporting_endpoint,
          owner_version_->cross_origin_embedder_policy()
              ->report_only_reporting_endpoint,
          owner_version_->reporting_source(),
          // TODO(https://crbug.com/1147281): This is the NetworkIsolationKey of
          // a top-level browsing context, which shouldn't be use for
          // ServiceWorkers used in iframes.
          net::NetworkIsolationKey::ToDoUseTopFrameOriginAsWell(
              url::Origin::Create(params->script_url)));
      coep_reporter_->BindObserver(std::move(reporting_observer_remote));

      coep_reporter_->Clone(
          coep_reporter_for_devtools.InitWithNewPipeAndPassReceiver());
      coep_reporter_->Clone(
          coep_reporter_for_scripts.InitWithNewPipeAndPassReceiver());
      coep_reporter_->Clone(
          coep_reporter_for_subresources.InitWithNewPipeAndPassReceiver());
    }

    // Initialize the global scope now if the worker won't be paused. Otherwise,
    // delay initialization until the main script is loaded.
    if (!owner_version_->initialize_global_scope_after_main_script_loaded()) {
      owner_version_->InitializeGlobalScope(
          /*script_loader_factories=*/nullptr,
          /*subresource_loader_factories=*/nullptr);
    }

    // Register to DevTools and update params accordingly.
    const int routing_id = rph->GetNextRoutingID();
    ServiceWorkerDevToolsManager::GetInstance()->WorkerStarting(
        process_id, routing_id, context_->wrapper(),
        params->service_worker_version_id, params->script_url, params->scope,
        params->is_installed, owner_version_->cross_origin_embedder_policy(),
        std::move(coep_reporter_for_devtools), &params->devtools_worker_token,
        &params->wait_for_debugger);
    params->service_worker_route_id = routing_id;
    // Create DevToolsProxy here to ensure that the WorkerCreated() call is
    // balanced by DevToolsProxy's destructor calling WorkerStopped().
    devtools_proxy = std::make_unique<EmbeddedWorkerInstance::DevToolsProxy>(
        process_id, routing_id, params->devtools_worker_token);

    // Create factory bundles for this worker to do loading. These bundles don't
    // support reconnection to the network service, see below comments.
    const url::Origin origin = url::Origin::Create(params->script_url);

    // The bundle for new scripts is passed to ServiceWorkerScriptLoaderFactory
    // and used to request non-installed service worker scripts. It's only
    // needed for non-installed workers. It's OK to not support reconnection to
    // the network service because it can only used until the service worker
    // reaches the 'installed' state.
    if (!params->is_installed) {
      factory_bundle_for_new_scripts =
          EmbeddedWorkerInstance::CreateFactoryBundle(
              rph, routing_id, origin,
              owner_version_->cross_origin_embedder_policy(),
              std::move(coep_reporter_for_scripts),
              ContentBrowserClient::URLLoaderFactoryType::kServiceWorkerScript,
              params->devtools_worker_token.ToString());
    }

    // The bundle for the renderer is passed to the service worker, and
    // used for subresource loading from the service worker (i.e., fetch()).
    // It's OK to not support reconnection to the network service because the
    // service worker terminates itself when the connection breaks, so a new
    // instance can be started.
    factory_bundle_for_renderer = EmbeddedWorkerInstance::CreateFactoryBundle(
        rph, routing_id, origin, owner_version_->cross_origin_embedder_policy(),
        std::move(coep_reporter_for_subresources),
        ContentBrowserClient::URLLoaderFactoryType::kServiceWorkerSubResource,
        params->devtools_worker_token.ToString());
  }

  // TODO(crbug.com/862854): Support changes to blink::RendererPreferences while
  // the worker is running.
  DCHECK(process_manager->browser_context() || process_manager->IsShutdown());
  params->renderer_preferences = blink::RendererPreferences();
  GetContentClient()->browser()->UpdateRendererPreferencesForWorker(
      process_manager->browser_context(), &params->renderer_preferences);

  {
    // Create a RendererPreferenceWatcher to observe updates in the preferences.
    mojo::PendingRemote<blink::mojom::RendererPreferenceWatcher> watcher_remote;
    params->preference_watcher_receiver =
        watcher_remote.InitWithNewPipeAndPassReceiver();
    GetContentClient()->browser()->RegisterRendererPreferenceWatcher(
        process_manager->browser_context(), std::move(watcher_remote));
  }

  // If we allocated a process, WorkerProcessHandle has to be created before
  // returning to ensure the process is eventually released.
  auto process_handle = std::make_unique<WorkerProcessHandle>(
      process_manager->AsWeakPtr(), embedded_worker_id(),
      process_info->process_id);

  ServiceWorkerMetrics::StartSituation start_situation =
      process_info->start_situation;
  if (!GetContentClient()->browser()->IsBrowserStartupComplete())
    start_situation = ServiceWorkerMetrics::StartSituation::DURING_STARTUP;

  // Notify the instance that a process is allocated.
  OnProcessAllocated(std::move(process_handle), start_situation);

  // Notify the instance that it is registered to the DevTools manager.
  OnRegisteredToDevToolsManager(std::move(devtools_proxy));

  // Send the factory bundle for subresource loading from the service worker
  // (i.e. fetch()).
  DCHECK(factory_bundle_for_renderer);
  params->subresource_loader_factories = std::move(factory_bundle_for_renderer);

  // Build the URLLoaderFactory for loading new scripts, it's only needed if
  // this is a non-installed service worker.
  DCHECK(factory_bundle_for_new_scripts || params->is_installed);
  if (factory_bundle_for_new_scripts) {
    params->provider_info->script_loader_factory_remote =
        MakeScriptLoaderFactoryRemote(
            std::move(factory_bundle_for_new_scripts));
  }

  // Create cache storage now as an optimization, so the service worker can
  // use the Cache Storage API immediately on startup.
  if (base::FeatureList::IsEnabled(
          blink::features::kEagerCacheStorageSetupForServiceWorkers)) {
    BindCacheStorage(
        params->provider_info->cache_storage.InitWithNewPipeAndPassReceiver());
  }

  inflight_start_info_ = std::make_unique<StartInfo>(
      params->is_installed, params->wait_for_debugger, start_time);

  SendStartWorker(std::move(params));
  std::move(callback).Run(blink::ServiceWorkerStatusCode::kOk);
}

void EmbeddedWorkerInstance::Stop() {
  TRACE_EVENT1("ServiceWorker", "EmbeddedWorkerInstance::Stop", "script_url",
               owner_version_->script_url().spec());
  DCHECK(status_ == EmbeddedWorkerStatus::STARTING ||
         status_ == EmbeddedWorkerStatus::RUNNING)
      << static_cast<int>(status_);

  // Discard the info for starting a worker because this worker is going to be
  // stopped.
  inflight_start_info_.reset();

  // Don't send the StopWorker message if the StartWorker message hasn't
  // been sent.
  if (status_ == EmbeddedWorkerStatus::STARTING &&
      !HasSentStartWorker(starting_phase())) {
    ReleaseProcess();
    for (auto& observer : listener_list_)
      observer.OnStopped(EmbeddedWorkerStatus::STARTING /* old_status */);
    return;
  }

  client_->StopWorker();
  status_ = EmbeddedWorkerStatus::STOPPING;
  for (auto& observer : listener_list_)
    observer.OnStopping();
}

void EmbeddedWorkerInstance::StopIfNotAttachedToDevTools() {
  if (devtools_attached_) {
    if (devtools_proxy_) {
      // Check ShouldNotifyWorkerStopIgnored not to show the same message
      // multiple times in DevTools.
      if (devtools_proxy_->ShouldNotifyWorkerStopIgnored()) {
        owner_version_->MaybeReportConsoleMessageToInternals(
            blink::mojom::ConsoleMessageLevel::kVerbose,
            kServiceWorkerTerminationCanceledMesage);
        devtools_proxy_->WorkerStopIgnoredNotified();
      }
    }
    return;
  }
  Stop();
}

EmbeddedWorkerInstance::EmbeddedWorkerInstance(
    ServiceWorkerVersion* owner_version)
    : context_(owner_version->context()),
      owner_version_(owner_version),
      embedded_worker_id_(context_->GetNextEmbeddedWorkerId()),
      status_(EmbeddedWorkerStatus::STOPPED),
      starting_phase_(NOT_STARTING),
      restart_count_(0),
      thread_id_(ServiceWorkerConsts::kInvalidEmbeddedWorkerThreadId),
      devtools_attached_(false),
      network_accessed_for_script_(false),
      foreground_notified_(false) {
  DCHECK(owner_version_);
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(context_);
}

void EmbeddedWorkerInstance::OnProcessAllocated(
    std::unique_ptr<WorkerProcessHandle> handle,
    ServiceWorkerMetrics::StartSituation start_situation) {
  DCHECK_EQ(EmbeddedWorkerStatus::STARTING, status_);
  DCHECK(!process_handle_);

  process_handle_ = std::move(handle);

  UpdateForegroundPriority();

  start_situation_ = start_situation;
  for (auto& observer : listener_list_)
    observer.OnProcessAllocated();
}

void EmbeddedWorkerInstance::OnRegisteredToDevToolsManager(
    std::unique_ptr<DevToolsProxy> devtools_proxy) {
  if (devtools_proxy) {
    DCHECK(!devtools_proxy_);
    devtools_proxy_ = std::move(devtools_proxy);
  }
  for (auto& observer : listener_list_)
    observer.OnRegisteredToDevToolsManager();
}

void EmbeddedWorkerInstance::SendStartWorker(
    blink::mojom::EmbeddedWorkerStartParamsPtr params) {
  DCHECK(context_);
  DCHECK(params->service_worker_receiver.is_valid());
  DCHECK(params->controller_receiver.is_valid());
  DCHECK(!instance_host_receiver_.is_bound());

  instance_host_receiver_.Bind(
      params->instance_host.InitWithNewEndpointAndPassReceiver());

  content_settings_ =
      base::SequenceBound<ServiceWorkerContentSettingsProxyImpl>(
          GetUIThreadTaskRunner({}), params->script_url,
          scoped_refptr<ServiceWorkerContextWrapper>(context_->wrapper()),
          params->content_settings_proxy.InitWithNewPipeAndPassReceiver());

  const bool is_script_streaming = !params->installed_scripts_info.is_null();
  inflight_start_info_->start_worker_sent_time = base::TimeTicks::Now();

  // The host must be alive as long as |params->provider_info| is alive.
  owner_version_->worker_host()->CompleteStartWorkerPreparation(
      process_id(), params->provider_info->browser_interface_broker
                        .InitWithNewPipeAndPassReceiver());

  // TODO(bashi): Always pass a valid outside fetch client settings object.
  // See crbug.com/937177.
  if (!params->outside_fetch_client_settings_object) {
    params->outside_fetch_client_settings_object =
        blink::mojom::FetchClientSettingsObject::New(
            network::mojom::ReferrerPolicy::kDefault,
            /*outgoing_referrer=*/params->script_url,
            blink::mojom::InsecureRequestsPolicy::kDoNotUpgrade);
  }

  client_->StartWorker(std::move(params));

  starting_phase_ = is_script_streaming ? SCRIPT_STREAMING : SENT_START_WORKER;
  for (auto& observer : listener_list_)
    observer.OnStartWorkerMessageSent();
}

void EmbeddedWorkerInstance::RequestTermination(
    RequestTerminationCallback callback) {
  if (status() != EmbeddedWorkerStatus::RUNNING &&
      status() != EmbeddedWorkerStatus::STOPPING) {
    mojo::ReportBadMessage(
        "Invalid termination request: Termination should be requested during "
        "running or stopping");
    std::move(callback).Run(true /* will_be_terminated */);
    return;
  }
  const bool will_be_terminated = owner_version_->OnRequestTermination();
  TRACE_EVENT1("ServiceWorker", "EmbeddedWorkerInstance::RequestTermination",
               "will_be_terminated", will_be_terminated);

  std::move(callback).Run(will_be_terminated);
}

void EmbeddedWorkerInstance::CountFeature(blink::mojom::WebFeature feature) {
  owner_version_->CountFeature(feature);
}

void EmbeddedWorkerInstance::OnReadyForInspection(
    mojo::PendingRemote<blink::mojom::DevToolsAgent> agent_remote,
    mojo::PendingReceiver<blink::mojom::DevToolsAgentHost> host_receiver) {
  if (!devtools_proxy_)
    return;
  devtools_proxy_->NotifyWorkerReadyForInspection(std::move(agent_remote),
                                                  std::move(host_receiver));
}

void EmbeddedWorkerInstance::OnScriptLoaded() {
  if (!inflight_start_info_)
    return;

  // Renderer side has started to launch the worker thread.
  starting_phase_ = SCRIPT_LOADED;
  owner_version_->OnMainScriptLoaded();
}

void EmbeddedWorkerInstance::OnWorkerVersionInstalled() {
  if (devtools_proxy_)
    devtools_proxy_->NotifyWorkerVersionInstalled();
}

void EmbeddedWorkerInstance::OnWorkerVersionDoomed() {
  ServiceWorkerDevToolsManager::GetInstance()->WorkerVersionDoomed(
      process_id(), worker_devtools_agent_route_id(),
      base::WrapRefCounted(context_->wrapper()), owner_version_->version_id());
}

void EmbeddedWorkerInstance::OnScriptEvaluationStart() {
  if (!inflight_start_info_)
    return;

  starting_phase_ = SCRIPT_EVALUATION;
  for (auto& observer : listener_list_)
    observer.OnScriptEvaluationStart();
}

void EmbeddedWorkerInstance::OnStarted(
    blink::mojom::ServiceWorkerStartStatus start_status,
    bool has_fetch_handler,
    int thread_id,
    blink::mojom::EmbeddedWorkerStartTimingPtr start_timing) {
  TRACE_EVENT0("ServiceWorker", "EmbeddedWorkerInstance::OnStarted");
  if (!(start_timing->start_worker_received_time <=
            start_timing->script_evaluation_start_time &&
        start_timing->script_evaluation_start_time <=
            start_timing->script_evaluation_end_time)) {
    mojo::ReportBadMessage("EWI_BAD_START_TIMING");
    return;
  }

  // Stop was requested before OnStarted was sent back from the worker. Just
  // pretend startup didn't happen, so observers don't try to use the running
  // worker as it will stop soon.
  if (status_ == EmbeddedWorkerStatus::STOPPING)
    return;

  if (inflight_start_info_->is_installed &&
      !inflight_start_info_->skip_recording_startup_time) {
    ServiceWorkerMetrics::StartTimes times;
    times.local_start = inflight_start_info_->start_time;
    times.local_start_worker_sent =
        inflight_start_info_->start_worker_sent_time;
    times.remote_start_worker_received =
        start_timing->start_worker_received_time;
    times.remote_script_evaluation_start =
        start_timing->script_evaluation_start_time;
    times.remote_script_evaluation_end =
        start_timing->script_evaluation_end_time;
    times.local_end = base::TimeTicks::Now();

    ServiceWorkerMetrics::RecordStartWorkerTiming(times, start_situation_);
  }

  DCHECK_EQ(EmbeddedWorkerStatus::STARTING, status_);
  status_ = EmbeddedWorkerStatus::RUNNING;
  thread_id_ = thread_id;
  inflight_start_info_.reset();
  for (auto& observer : listener_list_) {
    observer.OnStarted(start_status, has_fetch_handler);
    // |this| may be destroyed here. Fortunately we know there is only one
    // observer in production code.
  }
}

void EmbeddedWorkerInstance::OnStopped() {
  EmbeddedWorkerStatus old_status = status_;
  ReleaseProcess();
  for (auto& observer : listener_list_)
    observer.OnStopped(old_status);
}

void EmbeddedWorkerInstance::Detach() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (status() == EmbeddedWorkerStatus::STOPPED)
    return;

  EmbeddedWorkerStatus old_status = status_;
  ReleaseProcess();
  for (auto& observer : listener_list_)
    observer.OnDetached(old_status);
}

void EmbeddedWorkerInstance::UpdateForegroundPriority() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (process_handle_ &&
      owner_version_->ShouldRequireForegroundPriority(process_id())) {
    NotifyForegroundServiceWorkerAdded();
  } else {
    NotifyForegroundServiceWorkerRemoved();
  }
}

void EmbeddedWorkerInstance::UpdateLoaderFactories(
    std::unique_ptr<blink::PendingURLLoaderFactoryBundle> script_bundle,
    std::unique_ptr<blink::PendingURLLoaderFactoryBundle> subresource_bundle) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(subresource_loader_updater_.is_bound());

  // It's set to nullptr when the caller wants to update script bundle only.
  if (subresource_bundle) {
    subresource_loader_updater_->UpdateSubresourceLoaderFactories(
        std::move(subresource_bundle));
  }

  if (script_loader_factory_) {
    static_cast<ServiceWorkerScriptLoaderFactory*>(
        script_loader_factory_->impl())
        ->Update(base::MakeRefCounted<blink::URLLoaderFactoryBundle>(
            std::move(script_bundle)));
  }
}

void EmbeddedWorkerInstance::BindCacheStorage(
    mojo::PendingReceiver<blink::mojom::CacheStorage> receiver) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  pending_cache_storage_receivers_.push_back(std::move(receiver));
  BindCacheStorageInternal();
}

base::WeakPtr<EmbeddedWorkerInstance> EmbeddedWorkerInstance::AsWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

// Returns a factory bundle for doing loads on behalf of the specified |rph| and
// |origin|. The returned bundle has a default factory that goes to network and
// it may also include scheme-specific factories that don't go to network.
//
// The network factory does not support reconnection to the network service.
std::unique_ptr<blink::PendingURLLoaderFactoryBundle>
EmbeddedWorkerInstance::CreateFactoryBundle(
    RenderProcessHost* rph,
    int routing_id,
    const url::Origin& origin,
    const absl::optional<network::CrossOriginEmbedderPolicy>&
        cross_origin_embedder_policy,
    mojo::PendingRemote<network::mojom::CrossOriginEmbedderPolicyReporter>
        coep_reporter,
    ContentBrowserClient::URLLoaderFactoryType factory_type,
    const std::string& devtools_worker_token) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  auto factory_bundle =
      std::make_unique<blink::PendingURLLoaderFactoryBundle>();
  mojo::PendingReceiver<network::mojom::URLLoaderFactory>
      default_factory_receiver = factory_bundle->pending_default_factory()
                                     .InitWithNewPipeAndPassReceiver();
  // TODO(crbug.com/1231019): make sure client_security_state is no longer
  // nullptr anywhere.
  network::mojom::URLLoaderFactoryParamsPtr factory_params =
      URLLoaderFactoryParamsHelper::CreateForWorker(
          rph, origin,
          net::IsolationInfo::Create(net::IsolationInfo::RequestType::kOther,
                                     origin, origin,
                                     net::SiteForCookies::FromOrigin(origin)),
          std::move(coep_reporter),
          static_cast<StoragePartitionImpl*>(rph->GetStoragePartition())
              ->CreateAuthCertObserverForServiceWorker(),
          NetworkServiceDevToolsObserver::MakeSelfOwned(devtools_worker_token),
          /*client_security_state=*/nullptr,
          "EmbeddedWorkerInstance::CreateFactoryBundle");
  bool bypass_redirect_checks = false;

  DCHECK(factory_type ==
             ContentBrowserClient::URLLoaderFactoryType::kServiceWorkerScript ||
         factory_type == ContentBrowserClient::URLLoaderFactoryType::
                             kServiceWorkerSubResource);

  // See if the default factory needs to be tweaked by the embedder.
  GetContentClient()->browser()->WillCreateURLLoaderFactory(
      rph->GetBrowserContext(), nullptr /* frame_host */, rph->GetID(),
      factory_type, origin, absl::nullopt /* navigation_id */,
      ukm::kInvalidSourceIdObj, &default_factory_receiver,
      &factory_params->header_client, &bypass_redirect_checks,
      nullptr /* disable_secure_dns */, &factory_params->factory_override);
  devtools_instrumentation::WillCreateURLLoaderFactoryForServiceWorker(
      rph, routing_id, &factory_params->factory_override);

  factory_params->client_security_state =
      network::mojom::ClientSecurityState::New();

  // Without PlzServiceWorker, the COEP header might no be known initially for
  // new ServiceWorker. The default COEP header is used instead here. Later, the
  // subresource loader factories will be updated with the correct COEP header.
  // See: https://chromium-review.googlesource.com/c/chromium/src/+/2029403
  factory_params->client_security_state->cross_origin_embedder_policy =
      cross_origin_embedder_policy ? cross_origin_embedder_policy.value()
                                   : network::CrossOriginEmbedderPolicy();

  rph->CreateURLLoaderFactory(std::move(default_factory_receiver),
                              std::move(factory_params));

  factory_bundle->set_bypass_redirect_checks(bypass_redirect_checks);

  ContentBrowserClient::NonNetworkURLLoaderFactoryMap non_network_factories;
  non_network_factories[url::kDataScheme] = DataURLLoaderFactory::Create();
  GetContentClient()
      ->browser()
      ->RegisterNonNetworkSubresourceURLLoaderFactories(
          rph->GetID(), MSG_ROUTING_NONE, origin, &non_network_factories);

  for (auto& pair : non_network_factories) {
    const std::string& scheme = pair.first;
    mojo::PendingRemote<network::mojom::URLLoaderFactory>& pending_remote =
        pair.second;

    // To be safe, ignore schemes that aren't allowed to register service
    // workers. We assume that importScripts and fetch() should fail on such
    // schemes.
    if (!base::Contains(GetServiceWorkerSchemes(), scheme))
      continue;

    factory_bundle->pending_scheme_specific_factories().emplace(
        scheme, std::move(pending_remote));
  }

  return factory_bundle;
}

EmbeddedWorkerInstance::CreateFactoryBundlesResult::
    CreateFactoryBundlesResult() = default;
EmbeddedWorkerInstance::CreateFactoryBundlesResult::
    ~CreateFactoryBundlesResult() = default;
EmbeddedWorkerInstance::CreateFactoryBundlesResult::CreateFactoryBundlesResult(
    CreateFactoryBundlesResult&& other) = default;

EmbeddedWorkerInstance::CreateFactoryBundlesResult
EmbeddedWorkerInstance::CreateFactoryBundles() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  CreateFactoryBundlesResult result;

  auto* rph = RenderProcessHost::FromID(process_id());
  if (!rph) {
    // Return nullptr because we can't create a factory bundle because of
    // missing renderer.
    return result;
  }

  // Create mojo::Remote which is connected to and owns a COEP reporter.
  mojo::PendingRemote<network::mojom::CrossOriginEmbedderPolicyReporter>
      coep_reporter_for_devtools;
  mojo::PendingRemote<network::mojom::CrossOriginEmbedderPolicyReporter>
      coep_reporter_for_scripts;
  mojo::PendingRemote<network::mojom::CrossOriginEmbedderPolicyReporter>
      coep_reporter_for_subresources;

  // |cross_origin_embedder_policy| is nullopt in some unittests.
  // TODO(shimazu): Set COEP in those tests.
  if (owner_version_->cross_origin_embedder_policy()) {
    mojo::PendingRemote<blink::mojom::ReportingObserver>
        reporting_observer_remote;
    owner_version_->set_reporting_observer_receiver(
        reporting_observer_remote.InitWithNewPipeAndPassReceiver());

    auto* storage_partition =
        static_cast<StoragePartitionImpl*>(rph->GetStoragePartition());
    coep_reporter_ = std::make_unique<CrossOriginEmbedderPolicyReporter>(
        storage_partition->GetWeakPtr(), owner_version_->script_url(),
        owner_version_->cross_origin_embedder_policy()->reporting_endpoint,
        owner_version_->cross_origin_embedder_policy()
            ->report_only_reporting_endpoint,
        owner_version_->reporting_source(),
        // TODO(https://crbug.com/1147281): This is the NetworkIsolationKey of a
        // top-level browsing context, which shouldn't be use for ServiceWorkers
        // used in iframes.
        net::NetworkIsolationKey::ToDoUseTopFrameOriginAsWell(
            url::Origin::Create(owner_version_->script_url())));
    coep_reporter_->BindObserver(std::move(reporting_observer_remote));
    coep_reporter_->Clone(
        coep_reporter_for_devtools.InitWithNewPipeAndPassReceiver());
    coep_reporter_->Clone(
        coep_reporter_for_scripts.InitWithNewPipeAndPassReceiver());
    coep_reporter_->Clone(
        coep_reporter_for_subresources.InitWithNewPipeAndPassReceiver());

    ServiceWorkerDevToolsManager::GetInstance()
        ->UpdateCrossOriginEmbedderPolicy(
            process_id(), worker_devtools_agent_route_id(),
            owner_version_->cross_origin_embedder_policy().value(),
            std::move(coep_reporter_for_devtools));
  }

  const url::Origin origin = url::Origin::Create(owner_version_->script_url());
  result.script_bundle = EmbeddedWorkerInstance::CreateFactoryBundle(
      rph, worker_devtools_agent_route_id(), origin,
      owner_version_->cross_origin_embedder_policy(),
      std::move(coep_reporter_for_scripts),
      ContentBrowserClient::URLLoaderFactoryType::kServiceWorkerScript,
      WorkerDevtoolsId().ToString());
  result.subresource_bundle = EmbeddedWorkerInstance::CreateFactoryBundle(
      rph, worker_devtools_agent_route_id(), origin,
      owner_version_->cross_origin_embedder_policy(),
      std::move(coep_reporter_for_subresources),
      ContentBrowserClient::URLLoaderFactoryType::kServiceWorkerSubResource,
      WorkerDevtoolsId().ToString());

  BindCacheStorageInternal();

  return result;
}

void EmbeddedWorkerInstance::OnReportException(
    const std::u16string& error_message,
    int line_number,
    int column_number,
    const GURL& source_url) {
  for (auto& observer : listener_list_) {
    observer.OnReportException(error_message, line_number, column_number,
                               source_url);
  }
}

void EmbeddedWorkerInstance::OnReportConsoleMessage(
    blink::mojom::ConsoleMessageSource source,
    blink::mojom::ConsoleMessageLevel message_level,
    const std::u16string& message,
    int line_number,
    const GURL& source_url) {
  for (auto& observer : listener_list_) {
    observer.OnReportConsoleMessage(source, message_level, message, line_number,
                                    source_url);
  }
}

int EmbeddedWorkerInstance::process_id() const {
  if (process_handle_)
    return process_handle_->process_id();
  return ChildProcessHost::kInvalidUniqueID;
}

int EmbeddedWorkerInstance::worker_devtools_agent_route_id() const {
  if (devtools_proxy_)
    return devtools_proxy_->agent_route_id();
  return MSG_ROUTING_NONE;
}

base::UnguessableToken EmbeddedWorkerInstance::WorkerDevtoolsId() const {
  if (devtools_proxy_)
    return devtools_proxy_->devtools_id();
  return base::UnguessableToken();
}

void EmbeddedWorkerInstance::AddObserver(Listener* listener) {
  listener_list_.AddObserver(listener);
}

void EmbeddedWorkerInstance::RemoveObserver(Listener* listener) {
  listener_list_.RemoveObserver(listener);
}

void EmbeddedWorkerInstance::SetDevToolsAttached(bool attached) {
  devtools_attached_ = attached;
  if (!attached)
    return;
  if (inflight_start_info_)
    inflight_start_info_->skip_recording_startup_time = true;
}

void EmbeddedWorkerInstance::OnNetworkAccessedForScriptLoad() {
  starting_phase_ = SCRIPT_DOWNLOADING;
  network_accessed_for_script_ = true;
}

void EmbeddedWorkerInstance::ReleaseProcess() {
  // Abort an inflight start task.
  inflight_start_info_.reset();

  NotifyForegroundServiceWorkerRemoved();

  instance_host_receiver_.reset();
  devtools_proxy_.reset();
  process_handle_.reset();
  subresource_loader_updater_.reset();
  coep_reporter_.reset();
  status_ = EmbeddedWorkerStatus::STOPPED;
  starting_phase_ = NOT_STARTING;
  thread_id_ = ServiceWorkerConsts::kInvalidEmbeddedWorkerThreadId;
  token_ = absl::nullopt;
}

void EmbeddedWorkerInstance::OnSetupFailed(
    StatusCallback callback,
    blink::ServiceWorkerStatusCode status) {
  EmbeddedWorkerStatus old_status = status_;
  ReleaseProcess();
  base::WeakPtr<EmbeddedWorkerInstance> weak_this = weak_factory_.GetWeakPtr();
  std::move(callback).Run(status);
  if (weak_this && old_status != EmbeddedWorkerStatus::STOPPED) {
    for (auto& observer : weak_this->listener_list_)
      observer.OnStopped(old_status);
  }
}

// static
std::string EmbeddedWorkerInstance::StatusToString(
    EmbeddedWorkerStatus status) {
  switch (status) {
    case EmbeddedWorkerStatus::STOPPED:
      return "STOPPED";
    case EmbeddedWorkerStatus::STARTING:
      return "STARTING";
    case EmbeddedWorkerStatus::RUNNING:
      return "RUNNING";
    case EmbeddedWorkerStatus::STOPPING:
      return "STOPPING";
  }
  NOTREACHED() << static_cast<int>(status);
  return std::string();
}

// static
std::string EmbeddedWorkerInstance::StartingPhaseToString(StartingPhase phase) {
  switch (phase) {
    case NOT_STARTING:
      return "Not in STARTING status";
    case ALLOCATING_PROCESS:
      return "Allocating process";
    case SENT_START_WORKER:
      return "Sent StartWorker message to renderer";
    case SCRIPT_DOWNLOADING:
      return "Script downloading";
    case SCRIPT_LOADED:
      return "Script loaded";
    case SCRIPT_STREAMING:
      return "Script streaming";
    case SCRIPT_EVALUATION:
      return "Script evaluation";
    case STARTING_PHASE_MAX_VALUE:
      NOTREACHED();
  }
  NOTREACHED() << phase;
  return std::string();
}

void EmbeddedWorkerInstance::NotifyForegroundServiceWorkerAdded() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!process_handle_ || foreground_notified_)
    return;

  foreground_notified_ = true;
  NotifyForegroundServiceWorker(true /* added */, process_id());
}

void EmbeddedWorkerInstance::NotifyForegroundServiceWorkerRemoved() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!process_handle_ || !foreground_notified_)
    return;

  foreground_notified_ = false;
  NotifyForegroundServiceWorker(false /* added */, process_id());
}

mojo::PendingRemote<network::mojom::URLLoaderFactory>
EmbeddedWorkerInstance::MakeScriptLoaderFactoryRemote(
    std::unique_ptr<blink::PendingURLLoaderFactoryBundle> script_bundle) {
  mojo::PendingRemote<network::mojom::URLLoaderFactory>
      script_loader_factory_remote;

  auto script_bundle_factory =
      base::MakeRefCounted<blink::URLLoaderFactoryBundle>(
          std::move(script_bundle));
  script_loader_factory_ = mojo::MakeSelfOwnedReceiver(
      std::make_unique<ServiceWorkerScriptLoaderFactory>(
          context_, owner_version_->worker_host()->GetWeakPtr(),
          std::move(script_bundle_factory)),
      script_loader_factory_remote.InitWithNewPipeAndPassReceiver());

  return script_loader_factory_remote;
}

void EmbeddedWorkerInstance::BindCacheStorageInternal() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Without PlzServiceWorker, the COEP header might not be known initially.
  // The in-flight CacheStorage requests are kept until the main script has
  // loaded the headers and the COEP one is known.
  if (!owner_version_->cross_origin_embedder_policy())
    return;

  network::CrossOriginEmbedderPolicy coep =
      owner_version_->cross_origin_embedder_policy().value();

  for (auto& receiver : pending_cache_storage_receivers_) {
    mojo::PendingRemote<network::mojom::CrossOriginEmbedderPolicyReporter>
        coep_reporter_remote;
    if (coep_reporter_) {
      coep_reporter_->Clone(
          coep_reporter_remote.InitWithNewPipeAndPassReceiver());
    }

    auto* rph = RenderProcessHost::FromID(process_id());
    if (!rph)
      return;

    rph->BindCacheStorage(coep, std::move(coep_reporter_remote),
                          owner_version_->key(), std::move(receiver));
  }
  pending_cache_storage_receivers_.clear();
}

}  // namespace content
