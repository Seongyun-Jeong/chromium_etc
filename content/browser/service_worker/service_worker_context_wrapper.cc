// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_worker/service_worker_context_wrapper.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/services/storage/service_worker/service_worker_storage_control_impl.h"
#include "content/browser/blob_storage/chrome_blob_storage_context.h"
#include "content/browser/devtools/devtools_instrumentation.h"
#include "content/browser/loader/navigation_url_loader_impl.h"
#include "content/browser/service_worker/embedded_worker_status.h"
#include "content/browser/service_worker/service_worker_container_host.h"
#include "content/browser/service_worker/service_worker_host.h"
#include "content/browser/service_worker/service_worker_object_host.h"
#include "content/browser/service_worker/service_worker_process_manager.h"
#include "content/browser/service_worker/service_worker_quota_client.h"
#include "content/browser/service_worker/service_worker_version.h"
#include "content/browser/storage_partition_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/service_worker_context_observer.h"
#include "content/public/browser/storage_usage_info.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_features.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "net/base/url_util.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "storage/browser/quota/quota_client_type.h"
#include "storage/browser/quota/quota_manager_proxy.h"
#include "storage/browser/quota/special_storage_policy.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_study_settings.h"
#include "third_party/blink/public/common/service_worker/service_worker_scope_match.h"
#include "third_party/blink/public/common/service_worker/service_worker_status_code.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_registration.mojom.h"

namespace content {

namespace {

void DidFindRegistrationForStartActiveWorker(
    ServiceWorkerContextWrapper::StatusCallback callback,
    blink::ServiceWorkerStatusCode status,
    scoped_refptr<ServiceWorkerRegistration> registration) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (status != blink::ServiceWorkerStatusCode::kOk ||
      !registration->active_version()) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       blink::ServiceWorkerStatusCode::kErrorNotFound));
    return;
  }

  registration->active_version()->StartWorker(
      ServiceWorkerMetrics::EventType::UNKNOWN,
      base::BindOnce(
          [](ServiceWorkerContextWrapper::StatusCallback callback,
             blink::ServiceWorkerStatusCode status) {
            base::SequencedTaskRunnerHandle::Get()->PostTask(
                FROM_HERE, base::BindOnce(std::move(callback), status));
          },
          std::move(callback)));
}

void DidStartWorker(scoped_refptr<ServiceWorkerVersion> version,
                    ServiceWorkerContext::StartWorkerCallback info_callback,
                    ServiceWorkerContext::StatusCodeCallback failure_callback,
                    blink::ServiceWorkerStatusCode start_worker_status) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (start_worker_status != blink::ServiceWorkerStatusCode::kOk) {
    std::move(failure_callback).Run(start_worker_status);
    return;
  }
  EmbeddedWorkerInstance* instance = version->embedded_worker();
  std::move(info_callback)
      .Run(version->version_id(), instance->process_id(),
           instance->thread_id());
}

void FoundRegistrationForStartWorker(
    ServiceWorkerContext::StartWorkerCallback info_callback,
    ServiceWorkerContext::StatusCodeCallback failure_callback,
    blink::ServiceWorkerStatusCode service_worker_status,
    scoped_refptr<ServiceWorkerRegistration> registration) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (service_worker_status != blink::ServiceWorkerStatusCode::kOk) {
    std::move(failure_callback).Run(service_worker_status);
    return;
  }

  ServiceWorkerVersion* version_ptr = registration->active_version()
                                          ? registration->active_version()
                                          : registration->installing_version();
  // Since FindRegistrationForScope returned
  // blink::ServiceWorkerStatusCode::kOk, there must have been either:
  // - an active version, which optionally might have activated from a waiting
  //   version (as DidFindRegistrationForFindImpl will activate any waiting
  //   version).
  // - or an installing version.
  // However, if the installation is rejected, the installing version can go
  // away by the time we reach here from DidFindRegistrationForFindImpl.
  if (!version_ptr) {
    std::move(failure_callback).Run(service_worker_status);
    return;
  }

  // Note: There might be a remote possibility that |registration|'s |version|
  // might change between here and DidStartWorker, so bind |version| to
  // RunAfterStartWorker.
  scoped_refptr<ServiceWorkerVersion> version =
      base::WrapRefCounted(version_ptr);
  version->RunAfterStartWorker(
      ServiceWorkerMetrics::EventType::EXTERNAL_REQUEST,
      base::BindOnce(&DidStartWorker, version, std::move(info_callback),
                     std::move(failure_callback)));
}

void RunOnceClosure(scoped_refptr<ServiceWorkerContextWrapper> ref_holder,
                    base::OnceClosure task) {
  std::move(task).Run();
}

// Helper class to create a callback that takes blink::ServiceWorkerStatusCode
// as the first parameter and calls the original callback with a boolean of
// whether the status is blink::ServiceWorkerStatusCode::kOk or not.
class WrapResultCallbackToTakeStatusCode {
 public:
  explicit WrapResultCallbackToTakeStatusCode(
      ServiceWorkerContext::ResultCallback callback)
      : callback_(std::move(callback)) {}

  template <typename... Args>
  operator base::OnceCallback<void(blink::ServiceWorkerStatusCode, Args...)>() {
    return Take<Args...>();
  }

 private:
  template <typename... Args>
  base::OnceCallback<void(blink::ServiceWorkerStatusCode, Args...)> Take() {
    return base::BindOnce(
        [](ServiceWorkerContext::ResultCallback callback,
           blink::ServiceWorkerStatusCode status, Args...) {
          std::move(callback).Run(status ==
                                  blink::ServiceWorkerStatusCode::kOk);
        },
        std::move(callback_));
  }

  ServiceWorkerContext::ResultCallback callback_;
};

}  // namespace


// static
bool ServiceWorkerContext::ScopeMatches(const GURL& scope, const GURL& url) {
  return blink::ServiceWorkerScopeMatches(scope, url);
}

// static
void ServiceWorkerContext::RunTask(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    const base::Location& from_here,
    ServiceWorkerContext* service_worker_context,
    base::OnceClosure task) {
  auto ref = base::WrapRefCounted(
      static_cast<ServiceWorkerContextWrapper*>(service_worker_context));
  task_runner->PostTask(
      from_here,
      base::BindOnce(&RunOnceClosure, std::move(ref), std::move(task)));
}

ServiceWorkerContextWrapper::ServiceWorkerContextWrapper(
    BrowserContext* browser_context)
    : core_observer_list_(
          base::MakeRefCounted<ServiceWorkerContextObserverList>()),
      process_manager_(
          std::make_unique<ServiceWorkerProcessManager>(browser_context)) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Add this object as an observer of the wrapped |context_core_|. This lets us
  // forward observer methods to observers outside of content.
  core_observer_list_->AddObserver(this);

  if (blink::IdentifiabilityStudySettings::Get()->IsActive()) {
    identifiability_metrics_ =
        std::make_unique<ServiceWorkerIdentifiabilityMetrics>();
    core_observer_list_->AddObserver(identifiability_metrics_.get());
  }
}

void ServiceWorkerContextWrapper::Init(
    const base::FilePath& user_data_directory,
    storage::QuotaManagerProxy* quota_manager_proxy,
    storage::SpecialStoragePolicy* special_storage_policy,
    ChromeBlobStorageContext* blob_context) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(storage_partition_);

  is_incognito_ = user_data_directory.empty();

  user_data_directory_ = user_data_directory;
  quota_manager_proxy_ = quota_manager_proxy;

  InitInternal(quota_manager_proxy, special_storage_policy, blob_context,
               storage_partition_->browser_context());
}

void ServiceWorkerContextWrapper::InitInternal(
    storage::QuotaManagerProxy* quota_manager_proxy,
    storage::SpecialStoragePolicy* special_storage_policy,
    ChromeBlobStorageContext* blob_context,
    BrowserContext* browser_context) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  std::unique_ptr<blink::PendingURLLoaderFactoryBundle>
      non_network_pending_loader_factory_bundle_for_update_check;
  non_network_pending_loader_factory_bundle_for_update_check =
      CreateNonNetworkPendingURLLoaderFactoryBundleForUpdateCheck(
          browser_context);

  context_core_ = std::make_unique<ServiceWorkerContextCore>(
      quota_manager_proxy, special_storage_policy,
      std::move(non_network_pending_loader_factory_bundle_for_update_check),
      core_observer_list_.get(), this);

  if (storage_partition_) {
    context()->registry()->GetRegisteredStorageKeys(base::BindOnce(
        &ServiceWorkerContextWrapper::DidGetRegisteredStorageKeys, this));
  }
}

void ServiceWorkerContextWrapper::Shutdown() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  ClearRunningServiceWorkers();
  storage_partition_ = nullptr;
  process_manager_->Shutdown();
  storage_control_.reset();
  context_core_.reset();
}

void ServiceWorkerContextWrapper::DeleteAndStartOver() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_) {
    // The context could be null due to system shutdown or restart failure. In
    // either case, we should not have to recover the system, so just return
    // here.
    return;
  }
  context_core_->DeleteAndStartOver(base::BindOnce(
      &ServiceWorkerContextWrapper::DidDeleteAndStartOver, this));
}

StoragePartitionImpl* ServiceWorkerContextWrapper::storage_partition() const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return storage_partition_;
}

void ServiceWorkerContextWrapper::set_storage_partition(
    StoragePartitionImpl* storage_partition) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  storage_partition_ = storage_partition;
  process_manager_->set_storage_partition(storage_partition_);
}

BrowserContext* ServiceWorkerContextWrapper::browser_context() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return process_manager()->browser_context();
}

void ServiceWorkerContextWrapper::OnRegistrationCompleted(
    int64_t registration_id,
    const GURL& scope,
    const blink::StorageKey& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (auto& observer : observer_list_)
    observer.OnRegistrationCompleted(scope);
}

void ServiceWorkerContextWrapper::OnRegistrationStored(
    int64_t registration_id,
    const GURL& scope,
    const blink::StorageKey& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  registered_storage_keys_.insert(key);

  for (auto& observer : observer_list_)
    observer.OnRegistrationStored(registration_id, scope);
}

void ServiceWorkerContextWrapper::OnAllRegistrationsDeletedForStorageKey(
    const blink::StorageKey& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  registered_storage_keys_.erase(key);
}

void ServiceWorkerContextWrapper::OnErrorReported(
    int64_t version_id,
    const GURL& scope,
    const blink::StorageKey& key,
    const ServiceWorkerContextObserver::ErrorInfo& info) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (auto& observer : observer_list_)
    observer.OnErrorReported(version_id, scope, info);
}

void ServiceWorkerContextWrapper::OnReportConsoleMessage(
    int64_t version_id,
    const GURL& scope,
    const blink::StorageKey& key,
    const ConsoleMessage& message) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (auto& observer : observer_list_)
    observer.OnReportConsoleMessage(version_id, scope, message);
}

void ServiceWorkerContextWrapper::OnControlleeAdded(
    int64_t version_id,
    const std::string& client_uuid,
    const ServiceWorkerClientInfo& client_info) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (auto& observer : observer_list_)
    observer.OnControlleeAdded(version_id, client_uuid, client_info);
}

void ServiceWorkerContextWrapper::OnControlleeRemoved(
    int64_t version_id,
    const std::string& client_uuid) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (auto& observer : observer_list_)
    observer.OnControlleeRemoved(version_id, client_uuid);
}

void ServiceWorkerContextWrapper::OnNoControllees(
    int64_t version_id,
    const GURL& scope,
    const blink::StorageKey& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (auto& observer : observer_list_)
    observer.OnNoControllees(version_id, scope);
}

void ServiceWorkerContextWrapper::OnControlleeNavigationCommitted(
    int64_t version_id,
    const std::string& uuid,
    GlobalRenderFrameHostId render_frame_host_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (auto& observer : observer_list_)
    observer.OnControlleeNavigationCommitted(version_id, uuid,
                                             render_frame_host_id);
}

void ServiceWorkerContextWrapper::OnStarted(
    int64_t version_id,
    const GURL& scope,
    int process_id,
    const GURL& script_url,
    const blink::ServiceWorkerToken& token,
    const blink::StorageKey& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (is_deleting_and_starting_over_)
    return;

  // TODO(crbug.com/1199077): Update this when ServiceWorkerContextCoreObserver
  // implements StorageKey.
  auto insertion_result = running_service_workers_.insert(std::make_pair(
      version_id,
      ServiceWorkerRunningInfo(script_url, scope, key, process_id, token)));
  DCHECK(insertion_result.second);

  const auto& running_info = insertion_result.first->second;
  for (auto& observer : observer_list_)
    observer.OnVersionStartedRunning(version_id, running_info);
}

void ServiceWorkerContextWrapper::OnStopped(int64_t version_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  auto it = running_service_workers_.find(version_id);
  if (it != running_service_workers_.end()) {
    running_service_workers_.erase(it);
    for (auto& observer : observer_list_)
      observer.OnVersionStoppedRunning(version_id);
  }
}

void ServiceWorkerContextWrapper::OnDeleteAndStartOver() {
  is_deleting_and_starting_over_ = true;
  ClearRunningServiceWorkers();
}

void ServiceWorkerContextWrapper::OnVersionStateChanged(
    int64_t version_id,
    const GURL& scope,
    const blink::StorageKey& key,
    ServiceWorkerVersion::Status status) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (status == ServiceWorkerVersion::Status::ACTIVATED) {
    for (auto& observer : observer_list_)
      observer.OnVersionActivated(version_id, scope);
  } else if (status == ServiceWorkerVersion::Status::REDUNDANT) {
    for (auto& observer : observer_list_)
      observer.OnVersionRedundant(version_id, scope);
  }
}

void ServiceWorkerContextWrapper::AddObserver(
    ServiceWorkerContextObserver* observer) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  observer_list_.AddObserver(observer);
}

void ServiceWorkerContextWrapper::RemoveObserver(
    ServiceWorkerContextObserver* observer) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  observer_list_.RemoveObserver(observer);
}

void ServiceWorkerContextWrapper::RegisterServiceWorker(
    const GURL& script_url,
    const blink::StorageKey& key,
    const blink::mojom::ServiceWorkerRegistrationOptions& options,
    StatusCodeCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_) {
    GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(callback),
            blink::ServiceWorkerStatusCode::kErrorStartWorkerFailed));
    return;
  }
  blink::mojom::ServiceWorkerRegistrationOptions options_to_pass(
      net::SimplifyUrlForRequest(options.scope), options.type,
      options.update_via_cache);
  // TODO(bashi): Pass a valid outside fetch client settings object. Perhaps
  // changing this method to take a settings object.
  context()->RegisterServiceWorker(
      net::SimplifyUrlForRequest(script_url), key, options_to_pass,
      blink::mojom::FetchClientSettingsObject::New(
          network::mojom::ReferrerPolicy::kDefault,
          /*outgoing_referrer=*/script_url,
          blink::mojom::InsecureRequestsPolicy::kDoNotUpgrade),
      base::BindOnce(
          [](StatusCodeCallback callback, blink::ServiceWorkerStatusCode status,
             const std::string&, int64_t) { std::move(callback).Run(status); },
          std::move(callback)),
      /*requesting_frame_id=*/GlobalRenderFrameHostId());
}

void ServiceWorkerContextWrapper::UnregisterServiceWorker(
    const GURL& scope,
    const blink::StorageKey& key,
    ResultCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!context_core_) {
    GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }
  context()->UnregisterServiceWorker(
      net::SimplifyUrlForRequest(scope), key, /*is_immediate=*/false,
      WrapResultCallbackToTakeStatusCode(std::move(callback)));
}

ServiceWorkerExternalRequestResult
ServiceWorkerContextWrapper::StartingExternalRequest(
    int64_t service_worker_version_id,
    const std::string& request_uuid) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context())
    return ServiceWorkerExternalRequestResult::kNullContext;
  scoped_refptr<ServiceWorkerVersion> version =
      context()->GetLiveVersion(service_worker_version_id);
  if (!version)
    return ServiceWorkerExternalRequestResult::kWorkerNotFound;
  return version->StartExternalRequest(request_uuid);
}

bool ServiceWorkerContextWrapper::ExecuteScriptForTest(
    const std::string& script,
    int64_t service_worker_version_id,
    ServiceWorkerScriptExecutionCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context())
    return false;
  scoped_refptr<ServiceWorkerVersion> version =
      context()->GetLiveVersion(service_worker_version_id);
  if (!version)
    return false;
  version->ExecuteScriptForTest(script, std::move(callback));  // IN-TEST
  return true;
}

ServiceWorkerExternalRequestResult
ServiceWorkerContextWrapper::FinishedExternalRequest(
    int64_t service_worker_version_id,
    const std::string& request_uuid) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context())
    return ServiceWorkerExternalRequestResult::kNullContext;
  scoped_refptr<ServiceWorkerVersion> version =
      context()->GetLiveVersion(service_worker_version_id);
  if (!version)
    return ServiceWorkerExternalRequestResult::kWorkerNotFound;
  return version->FinishExternalRequest(request_uuid);
}

size_t ServiceWorkerContextWrapper::CountExternalRequestsForTest(
    const blink::StorageKey& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  std::vector<ServiceWorkerVersionInfo> live_version_info =
      GetAllLiveVersionInfo();
  for (const ServiceWorkerVersionInfo& info : live_version_info) {
    ServiceWorkerVersion* version = GetLiveVersion(info.version_id);
    if (version && version->key() == key) {
      return version->GetExternalRequestCountForTest();  // IN-TEST
    }
  }

  return 0u;
}

bool ServiceWorkerContextWrapper::MaybeHasRegistrationForStorageKey(
    const blink::StorageKey& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!registrations_initialized_) {
    return true;
  }
  if (registered_storage_keys_.find(key) != registered_storage_keys_.end()) {
    return true;
  }
  return false;
}

void ServiceWorkerContextWrapper::GetAllOriginsInfo(
    GetUsageInfoCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), std::vector<StorageUsageInfo>()));
    return;
  }
  context()->registry()->GetAllRegistrationsInfos(base::BindOnce(
      &ServiceWorkerContextWrapper::DidGetAllRegistrationsForGetAllOrigins,
      this, std::move(callback)));
}

void ServiceWorkerContextWrapper::DeleteForStorageKey(
    const blink::StorageKey& key,
    ResultCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Ensure the callback is called asynchronously.
  scoped_refptr<base::TaskRunner> callback_runner = GetUIThreadTaskRunner({});
  if (!context_core_) {
    callback_runner->PostTask(FROM_HERE,
                              base::BindOnce(std::move(callback), false));
    return;
  }
  context()->DeleteForStorageKey(
      key,
      base::BindOnce(
          [](ResultCallback callback,
             scoped_refptr<base::TaskRunner> callback_runner,
             blink::ServiceWorkerStatusCode status) {
            callback_runner->PostTask(
                FROM_HERE,
                base::BindOnce(std::move(callback),
                               status == blink::ServiceWorkerStatusCode::kOk));
          },
          std::move(callback), std::move(callback_runner)));
}

void ServiceWorkerContextWrapper::CheckHasServiceWorker(
    const GURL& url,
    const blink::StorageKey& key,
    CheckHasServiceWorkerCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!context_core_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  ServiceWorkerCapability::NO_SERVICE_WORKER));
    return;
  }
  context()->CheckHasServiceWorker(net::SimplifyUrlForRequest(url), key,
                                   std::move(callback));
}

void ServiceWorkerContextWrapper::CheckOfflineCapability(
    const GURL& url,
    const blink::StorageKey& key,
    CheckOfflineCapabilityCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!context_core_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), OfflineCapability::kUnsupported,
                       blink::mojom::kInvalidServiceWorkerRegistrationId));
    return;
  }
  context()->CheckOfflineCapability(net::SimplifyUrlForRequest(url), key,
                                    std::move(callback));
}

void ServiceWorkerContextWrapper::ClearAllServiceWorkersForTest(
    base::OnceClosure callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!context_core_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                  std::move(callback));
    return;
  }
  context_core_->ClearAllServiceWorkersForTest(std::move(callback));
}

void ServiceWorkerContextWrapper::StartWorkerForScope(
    const GURL& scope,
    const blink::StorageKey& key,
    StartWorkerCallback info_callback,
    StatusCodeCallback failure_callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  FindRegistrationForScopeImpl(
      scope, key,
      /*include_installing_version=*/true,
      base::BindOnce(&FoundRegistrationForStartWorker, std::move(info_callback),
                     std::move(failure_callback)));
}

void ServiceWorkerContextWrapper::StartServiceWorkerAndDispatchMessage(
    const GURL& scope,
    const blink::StorageKey& key,
    blink::TransferableMessage message,
    ResultCallback result_callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Ensure the callback is called asynchronously.
  auto wrapped_callback = base::BindOnce(
      [](ResultCallback callback, bool success) {
        GetUIThreadTaskRunner({})->PostTask(
            FROM_HERE, base::BindOnce(std::move(callback), success));
      },
      std::move(result_callback));

  FindRegistrationForScopeImpl(
      net::SimplifyUrlForRequest(scope), key,
      /*include_installing_version=*/false,
      base::BindOnce(
          &ServiceWorkerContextWrapper::DidFindRegistrationForMessageDispatch,
          this, std::move(message), scope, std::move(wrapped_callback)));
}

void ServiceWorkerContextWrapper::DidFindRegistrationForMessageDispatch(
    blink::TransferableMessage message,
    const GURL& source_origin,
    ResultCallback result_callback,
    blink::ServiceWorkerStatusCode service_worker_status,
    scoped_refptr<ServiceWorkerRegistration> registration) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (service_worker_status != blink::ServiceWorkerStatusCode::kOk) {
    LOG(WARNING) << "No registration available, status: "
                 << static_cast<int>(service_worker_status);
    std::move(result_callback).Run(/*success=*/false);
    return;
  }
  registration->active_version()->StartWorker(
      ServiceWorkerMetrics::EventType::MESSAGE,
      base::BindOnce(
          &ServiceWorkerContextWrapper::DidStartServiceWorkerForMessageDispatch,
          this, std::move(message), source_origin, registration,
          std::move(result_callback)));
}

void ServiceWorkerContextWrapper::DidStartServiceWorkerForMessageDispatch(
    blink::TransferableMessage message,
    const GURL& source_origin,
    scoped_refptr<ServiceWorkerRegistration> registration,
    ResultCallback result_callback,
    blink::ServiceWorkerStatusCode status) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (status != blink::ServiceWorkerStatusCode::kOk) {
    std::move(result_callback).Run(/*success=*/false);
    return;
  }

  scoped_refptr<ServiceWorkerVersion> version = registration->active_version();

  blink::mojom::ExtendableMessageEventPtr event =
      blink::mojom::ExtendableMessageEvent::New();
  event->message = std::move(message);
  event->source_origin = url::Origin::Create(source_origin);
  event->source_info_for_service_worker =
      version->worker_host()
          ->container_host()
          ->GetOrCreateServiceWorkerObjectHost(version)
          ->CreateCompleteObjectInfoToSend();

  int request_id = version->StartRequest(
      ServiceWorkerMetrics::EventType::MESSAGE,
      WrapResultCallbackToTakeStatusCode(std::move(result_callback)));
  version->endpoint()->DispatchExtendableMessageEvent(
      std::move(event), version->CreateSimpleEventCallback(request_id));
}

void ServiceWorkerContextWrapper::StartServiceWorkerForNavigationHint(
    const GURL& document_url,
    const blink::StorageKey& key,
    StartServiceWorkerForNavigationHintCallback callback) {
  TRACE_EVENT1("ServiceWorker", "StartServiceWorkerForNavigationHint",
               "document_url", document_url.spec());
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!context_core_) {
    std::move(callback).Run(StartServiceWorkerForNavigationHintResult::FAILED);
    return;
  }
  context_core_->registry()->FindRegistrationForClientUrl(
      net::SimplifyUrlForRequest(document_url), key,
      base::BindOnce(
          &ServiceWorkerContextWrapper::DidFindRegistrationForNavigationHint,
          this, std::move(callback)));
}

void ServiceWorkerContextWrapper::StopAllServiceWorkersForStorageKey(
    const blink::StorageKey& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_.get()) {
    return;
  }
  std::vector<ServiceWorkerVersionInfo> live_versions = GetAllLiveVersionInfo();
  for (const ServiceWorkerVersionInfo& info : live_versions) {
    ServiceWorkerVersion* version = GetLiveVersion(info.version_id);
    if (version && version->key() == key)
      version->StopWorker(base::DoNothing());
  }
}

void ServiceWorkerContextWrapper::StopAllServiceWorkers(
    base::OnceClosure callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_.get()) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                  std::move(callback));
    return;
  }

  std::vector<ServiceWorkerVersionInfo> live_versions = GetAllLiveVersionInfo();
  base::RepeatingClosure barrier =
      base::BarrierClosure(live_versions.size(), std::move(callback));
  for (const ServiceWorkerVersionInfo& info : live_versions) {
    ServiceWorkerVersion* version = GetLiveVersion(info.version_id);
    DCHECK(version);
    version->StopWorker(barrier);
  }
}

const base::flat_map<int64_t, ServiceWorkerRunningInfo>&
ServiceWorkerContextWrapper::GetRunningServiceWorkerInfos() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return running_service_workers_;
}

scoped_refptr<ServiceWorkerRegistration>
ServiceWorkerContextWrapper::GetLiveRegistration(int64_t registration_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_)
    return nullptr;
  return context_core_->GetLiveRegistration(registration_id);
}

ServiceWorkerVersion* ServiceWorkerContextWrapper::GetLiveVersion(
    int64_t version_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_)
    return nullptr;
  return context_core_->GetLiveVersion(version_id);
}

std::vector<ServiceWorkerRegistrationInfo>
ServiceWorkerContextWrapper::GetAllLiveRegistrationInfo() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_)
    return std::vector<ServiceWorkerRegistrationInfo>();
  return context_core_->GetAllLiveRegistrationInfo();
}

std::vector<ServiceWorkerVersionInfo>
ServiceWorkerContextWrapper::GetAllLiveVersionInfo() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_)
    return std::vector<ServiceWorkerVersionInfo>();
  return context_core_->GetAllLiveVersionInfo();
}

void ServiceWorkerContextWrapper::HasMainFrameWindowClient(
    const blink::StorageKey& key,
    BoolCallback callback) const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!context_core_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }
  context_core_->HasMainFrameWindowClient(key, std::move(callback));
}

std::unique_ptr<std::vector<GlobalRenderFrameHostId>>
ServiceWorkerContextWrapper::GetWindowClientFrameRoutingIds(
    const blink::StorageKey& key) const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  std::unique_ptr<std::vector<GlobalRenderFrameHostId>> rfh_ids(
      new std::vector<GlobalRenderFrameHostId>());
  if (!context_core_)
    return rfh_ids;
  for (std::unique_ptr<ServiceWorkerContextCore::ContainerHostIterator> it =
           context_core_->GetWindowClientContainerHostIterator(
               key,
               /*include_reserved_clients=*/false);
       !it->IsAtEnd(); it->Advance()) {
    ServiceWorkerContainerHost* container_host = it->GetContainerHost();
    DCHECK(container_host->IsContainerForWindowClient());
    rfh_ids->push_back(container_host->GetRenderFrameHostId());
  }

  return rfh_ids;
}

void ServiceWorkerContextWrapper::FindReadyRegistrationForClientUrl(
    const GURL& client_url,
    const blink::StorageKey& key,
    FindRegistrationCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_) {
    std::move(callback).Run(blink::ServiceWorkerStatusCode::kErrorAbort,
                            nullptr);
    return;
  }
  context_core_->registry()->FindRegistrationForClientUrl(
      net::SimplifyUrlForRequest(client_url), key,
      base::BindOnce(
          &ServiceWorkerContextWrapper::DidFindRegistrationForFindImpl, this,
          /*include_installing_version=*/false, std::move(callback)));
}

void ServiceWorkerContextWrapper::FindReadyRegistrationForScope(
    const GURL& scope,
    const blink::StorageKey& key,
    FindRegistrationCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_) {
    std::move(callback).Run(blink::ServiceWorkerStatusCode::kErrorAbort,
                            nullptr);
    return;
  }
  const bool include_installing_version = false;
  context_core_->registry()->FindRegistrationForScope(
      net::SimplifyUrlForRequest(scope), key,
      base::BindOnce(
          &ServiceWorkerContextWrapper::DidFindRegistrationForFindImpl, this,
          include_installing_version, std::move(callback)));
}

void ServiceWorkerContextWrapper::FindRegistrationForScope(
    const GURL& scope,
    const blink::StorageKey& key,
    FindRegistrationCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  const bool include_installing_version = true;
  FindRegistrationForScopeImpl(scope, key, include_installing_version,
                               std::move(callback));
}

void ServiceWorkerContextWrapper::FindReadyRegistrationForId(
    int64_t registration_id,
    const blink::StorageKey& key,
    FindRegistrationCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_) {
    std::move(callback).Run(blink::ServiceWorkerStatusCode::kErrorAbort,
                            nullptr);
    return;
  }
  context_core_->registry()->FindRegistrationForId(
      registration_id, key,
      base::BindOnce(
          &ServiceWorkerContextWrapper::DidFindRegistrationForFindImpl, this,
          /*include_installing_version=*/false, std::move(callback)));
}

void ServiceWorkerContextWrapper::FindReadyRegistrationForIdOnly(
    int64_t registration_id,
    FindRegistrationCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_) {
    std::move(callback).Run(blink::ServiceWorkerStatusCode::kErrorAbort,
                            nullptr);
    return;
  }
  context_core_->registry()->FindRegistrationForIdOnly(
      registration_id,
      base::BindOnce(
          &ServiceWorkerContextWrapper::DidFindRegistrationForFindImpl, this,
          /*include_installing_version=*/false, std::move(callback)));
}

void ServiceWorkerContextWrapper::GetAllRegistrations(
    GetRegistrationsInfosCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       blink::ServiceWorkerStatusCode::kErrorAbort,
                       std::vector<ServiceWorkerRegistrationInfo>()));
    return;
  }
  context_core_->registry()->GetAllRegistrationsInfos(std::move(callback));
}

void ServiceWorkerContextWrapper::GetRegistrationsForStorageKey(
    const blink::StorageKey& key,
    GetRegistrationsCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(callback), blink::ServiceWorkerStatusCode::kErrorAbort,
            std::vector<scoped_refptr<ServiceWorkerRegistration>>()));
    return;
  }
  context_core_->registry()->GetRegistrationsForStorageKey(key,
                                                           std::move(callback));
}

void ServiceWorkerContextWrapper::GetRegistrationUserData(
    int64_t registration_id,
    const std::vector<std::string>& keys,
    GetUserDataCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!context_core_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), std::vector<std::string>(),
                       blink::ServiceWorkerStatusCode::kErrorAbort));
    return;
  }
  context_core_->registry()->GetUserData(registration_id, keys,
                                         std::move(callback));
}

void ServiceWorkerContextWrapper::GetRegistrationUserDataByKeyPrefix(
    int64_t registration_id,
    const std::string& key_prefix,
    GetUserDataCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!context_core_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), std::vector<std::string>(),
                       blink::ServiceWorkerStatusCode::kErrorAbort));
    return;
  }
  context_core_->registry()->GetUserDataByKeyPrefix(registration_id, key_prefix,
                                                    std::move(callback));
}

void ServiceWorkerContextWrapper::GetRegistrationUserKeysAndDataByKeyPrefix(
    int64_t registration_id,
    const std::string& key_prefix,
    GetUserKeysAndDataCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!context_core_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  blink::ServiceWorkerStatusCode::kErrorAbort,
                                  base::flat_map<std::string, std::string>()));
    return;
  }
  context_core_->registry()->GetUserKeysAndDataByKeyPrefix(
      registration_id, key_prefix, std::move(callback));
}

void ServiceWorkerContextWrapper::StoreRegistrationUserData(
    int64_t registration_id,
    const blink::StorageKey& key,
    const std::vector<std::pair<std::string, std::string>>& key_value_pairs,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!context_core_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  blink::ServiceWorkerStatusCode::kErrorAbort));
    return;
  }
  context_core_->registry()->StoreUserData(
      registration_id, key, key_value_pairs, std::move(callback));
}

void ServiceWorkerContextWrapper::ClearRegistrationUserData(
    int64_t registration_id,
    const std::vector<std::string>& keys,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Ensure the callback is called asynchronously.
  if (!context_core_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  blink::ServiceWorkerStatusCode::kErrorAbort));
    return;
  }
  context_core_->registry()->ClearUserData(registration_id, keys,
                                           std::move(callback));
}

void ServiceWorkerContextWrapper::ClearRegistrationUserDataByKeyPrefixes(
    int64_t registration_id,
    const std::vector<std::string>& key_prefixes,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!context_core_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  blink::ServiceWorkerStatusCode::kErrorAbort));
    return;
  }
  context_core_->registry()->ClearUserDataByKeyPrefixes(
      registration_id, key_prefixes, std::move(callback));
}

void ServiceWorkerContextWrapper::GetUserDataForAllRegistrations(
    const std::string& key,
    GetUserDataForAllRegistrationsCallback callback) {
  if (!context_core_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       std::vector<std::pair<int64_t, std::string>>(),
                       blink::ServiceWorkerStatusCode::kErrorAbort));
    return;
  }
  context_core_->registry()->GetUserDataForAllRegistrations(
      key, std::move(callback));
}

void ServiceWorkerContextWrapper::GetUserDataForAllRegistrationsByKeyPrefix(
    const std::string& key_prefix,
    GetUserDataForAllRegistrationsCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Ensure the callback is called asynchronously.
  if (!context_core_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       std::vector<std::pair<int64_t, std::string>>(),
                       blink::ServiceWorkerStatusCode::kErrorAbort));
    return;
  }

  context_core_->registry()->GetUserDataForAllRegistrationsByKeyPrefix(
      key_prefix, std::move(callback));
}

void ServiceWorkerContextWrapper::ClearUserDataForAllRegistrationsByKeyPrefix(
    const std::string& key_prefix,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Ensure the callback is called asynchronously.
  if (!context_core_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  blink::ServiceWorkerStatusCode::kErrorAbort));
    return;
  }
  context_core_->registry()->ClearUserDataForAllRegistrationsByKeyPrefix(
      key_prefix, std::move(callback));
}

void ServiceWorkerContextWrapper::StartActiveServiceWorker(
    const GURL& scope,
    const blink::StorageKey& key,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  blink::ServiceWorkerStatusCode::kErrorAbort));
    return;
  }
  context_core_->registry()->FindRegistrationForScope(
      net::SimplifyUrlForRequest(scope), key,
      base::BindOnce(&DidFindRegistrationForStartActiveWorker,
                     std::move(callback)));
}

void ServiceWorkerContextWrapper::SkipWaitingWorker(
    const GURL& scope,
    const blink::StorageKey& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_)
    return;
  context_core_->registry()->FindRegistrationForScope(
      net::SimplifyUrlForRequest(scope), key,
      base::BindOnce([](blink::ServiceWorkerStatusCode status,
                        scoped_refptr<ServiceWorkerRegistration> registration) {
        if (status != blink::ServiceWorkerStatusCode::kOk ||
            !registration->waiting_version())
          return;

        registration->waiting_version()->set_skip_waiting(true);
        registration->ActivateWaitingVersionWhenReady();
      }));
}

void ServiceWorkerContextWrapper::UpdateRegistration(
    const GURL& scope,
    const blink::StorageKey& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_)
    return;
  context_core_->registry()->FindRegistrationForScope(
      net::SimplifyUrlForRequest(scope), key,
      base::BindOnce(&ServiceWorkerContextWrapper::DidFindRegistrationForUpdate,
                     this));
}

void ServiceWorkerContextWrapper::SetForceUpdateOnPageLoad(
    bool force_update_on_page_load) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_)
    return;
  context_core_->set_force_update_on_page_load(force_update_on_page_load);
}

void ServiceWorkerContextWrapper::AddObserver(
    ServiceWorkerContextCoreObserver* observer) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  core_observer_list_->AddObserver(observer);
}

void ServiceWorkerContextWrapper::RemoveObserver(
    ServiceWorkerContextCoreObserver* observer) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  core_observer_list_->RemoveObserver(observer);
}

ServiceWorkerContextWrapper::~ServiceWorkerContextWrapper() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (auto& observer : observer_list_)
    observer.OnDestruct(static_cast<ServiceWorkerContext*>(this));

  // Explicitly remove this object as an observer to avoid use-after-frees in
  // tests where this object is not guaranteed to outlive the
  // ServiceWorkerContextCore it wraps.
  core_observer_list_->RemoveObserver(this);
  if (identifiability_metrics_)
    core_observer_list_->RemoveObserver(identifiability_metrics_.get());
}

void ServiceWorkerContextWrapper::FindRegistrationForScopeImpl(
    const GURL& scope,
    const blink::StorageKey& key,
    bool include_installing_version,
    FindRegistrationCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!context_core_) {
    std::move(callback).Run(blink::ServiceWorkerStatusCode::kErrorAbort,
                            nullptr);
    return;
  }
  context_core_->registry()->FindRegistrationForScope(
      net::SimplifyUrlForRequest(scope), key,
      base::BindOnce(
          &ServiceWorkerContextWrapper::DidFindRegistrationForFindImpl, this,
          include_installing_version, std::move(callback)));
}

void ServiceWorkerContextWrapper::DidFindRegistrationForFindImpl(
    bool include_installing_version,
    FindRegistrationCallback callback,
    blink::ServiceWorkerStatusCode status,
    scoped_refptr<ServiceWorkerRegistration> registration) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (status != blink::ServiceWorkerStatusCode::kOk) {
    std::move(callback).Run(status, nullptr);
    return;
  }

  // Attempt to activate the waiting version because the registration retrieved
  // from the disk might have only the waiting version.
  if (registration->waiting_version())
    registration->ActivateWaitingVersionWhenReady();

  scoped_refptr<ServiceWorkerVersion> active_version =
      registration->active_version();
  if (active_version) {
    if (active_version->status() == ServiceWorkerVersion::ACTIVATING) {
      // Wait until the version is activated.
      active_version->RegisterStatusChangeCallback(base::BindOnce(
          &ServiceWorkerContextWrapper::OnStatusChangedForFindReadyRegistration,
          this, std::move(callback), std::move(registration)));
      return;
    }
    DCHECK_EQ(ServiceWorkerVersion::ACTIVATED, active_version->status());
    std::move(callback).Run(blink::ServiceWorkerStatusCode::kOk,
                            std::move(registration));
    return;
  }

  if (include_installing_version && registration->installing_version()) {
    std::move(callback).Run(blink::ServiceWorkerStatusCode::kOk,
                            std::move(registration));
    return;
  }

  std::move(callback).Run(blink::ServiceWorkerStatusCode::kErrorNotFound,
                          nullptr);
}

void ServiceWorkerContextWrapper::OnStatusChangedForFindReadyRegistration(
    FindRegistrationCallback callback,
    scoped_refptr<ServiceWorkerRegistration> registration) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  scoped_refptr<ServiceWorkerVersion> active_version =
      registration->active_version();
  if (!active_version ||
      active_version->status() != ServiceWorkerVersion::ACTIVATED) {
    std::move(callback).Run(blink::ServiceWorkerStatusCode::kErrorNotFound,
                            nullptr);
    return;
  }
  std::move(callback).Run(blink::ServiceWorkerStatusCode::kOk, registration);
}

void ServiceWorkerContextWrapper::DidDeleteAndStartOver(
    blink::ServiceWorkerStatusCode status) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(running_service_workers_.empty());
  is_deleting_and_starting_over_ = false;
  storage_control_.reset();
  if (status != blink::ServiceWorkerStatusCode::kOk) {
    context_core_.reset();
    return;
  }
  context_core_ =
      std::make_unique<ServiceWorkerContextCore>(context_core_.get(), this);
  DVLOG(1) << "Restarted ServiceWorkerContextCore successfully.";
  context_core_->OnStorageWiped();
}

void ServiceWorkerContextWrapper::DidGetAllRegistrationsForGetAllOrigins(
    GetUsageInfoCallback callback,
    blink::ServiceWorkerStatusCode status,
    const std::vector<ServiceWorkerRegistrationInfo>& registrations) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  std::vector<StorageUsageInfo> usage_infos;

  std::map<GURL, StorageUsageInfo> origins;
  for (const auto& registration_info : registrations) {
    GURL origin = registration_info.scope.DeprecatedGetOriginAsURL();

    auto it = origins.find(origin);
    if (it == origins.end()) {
      origins[origin] = StorageUsageInfo(
          url::Origin::Create(origin),
          registration_info.stored_version_size_bytes, base::Time());
    } else {
      it->second.total_size_bytes +=
          registration_info.stored_version_size_bytes;
    }
  }

  for (const auto& origin_info_pair : origins) {
    usage_infos.push_back(origin_info_pair.second);
  }

  std::move(callback).Run(usage_infos);
}

void ServiceWorkerContextWrapper::DidFindRegistrationForUpdate(
    blink::ServiceWorkerStatusCode status,
    scoped_refptr<ServiceWorkerRegistration> registration) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (status != blink::ServiceWorkerStatusCode::kOk)
    return;
  if (!context_core_)
    return;
  DCHECK(registration);
  // TODO(jungkees): |force_bypass_cache| is set to true because the call stack
  // is initiated by an update button on DevTools that expects the cache is
  // bypassed. However, in order to provide options for callers to choose the
  // cache bypass mode, plumb |force_bypass_cache| through to
  // UpdateRegistration().
  context_core_->UpdateServiceWorker(registration.get(),
                                     true /* force_bypass_cache */);
}

void ServiceWorkerContextWrapper::DidFindRegistrationForNavigationHint(
    StartServiceWorkerForNavigationHintCallback callback,
    blink::ServiceWorkerStatusCode status,
    scoped_refptr<ServiceWorkerRegistration> registration) {
  TRACE_EVENT1("ServiceWorker", "DidFindRegistrationForNavigationHint",
               "status", blink::ServiceWorkerStatusToString(status));
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!registration) {
    DCHECK_NE(status, blink::ServiceWorkerStatusCode::kOk);
    std::move(callback).Run(StartServiceWorkerForNavigationHintResult::
                                NO_SERVICE_WORKER_REGISTRATION);
    return;
  }
  if (!registration->active_version()) {
    std::move(callback).Run(StartServiceWorkerForNavigationHintResult::
                                NO_ACTIVE_SERVICE_WORKER_VERSION);
    return;
  }
  if (registration->active_version()->fetch_handler_existence() ==
      ServiceWorkerVersion::FetchHandlerExistence::DOES_NOT_EXIST) {
    std::move(callback).Run(
        StartServiceWorkerForNavigationHintResult::NO_FETCH_HANDLER);
    return;
  }
  if (registration->active_version()->running_status() ==
      EmbeddedWorkerStatus::RUNNING) {
    std::move(callback).Run(
        StartServiceWorkerForNavigationHintResult::ALREADY_RUNNING);
    return;
  }

  registration->active_version()->StartWorker(
      ServiceWorkerMetrics::EventType::NAVIGATION_HINT,
      base::BindOnce(
          &ServiceWorkerContextWrapper::DidStartServiceWorkerForNavigationHint,
          this, registration->scope(), std::move(callback)));
}

void ServiceWorkerContextWrapper::DidStartServiceWorkerForNavigationHint(
    const GURL& scope,
    StartServiceWorkerForNavigationHintCallback callback,
    blink::ServiceWorkerStatusCode code) {
  TRACE_EVENT2("ServiceWorker", "DidStartServiceWorkerForNavigationHint", "url",
               scope.spec(), "code", blink::ServiceWorkerStatusToString(code));
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  std::move(callback).Run(
      code == blink::ServiceWorkerStatusCode::kOk
          ? StartServiceWorkerForNavigationHintResult::STARTED
          : StartServiceWorkerForNavigationHintResult::FAILED);
}

ServiceWorkerContextCore* ServiceWorkerContextWrapper::context() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return context_core_.get();
}

std::unique_ptr<blink::PendingURLLoaderFactoryBundle>
ServiceWorkerContextWrapper::
    CreateNonNetworkPendingURLLoaderFactoryBundleForUpdateCheck(
        BrowserContext* browser_context) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  ContentBrowserClient::NonNetworkURLLoaderFactoryMap non_network_factories;
  GetContentClient()
      ->browser()
      ->RegisterNonNetworkServiceWorkerUpdateURLLoaderFactories(
          browser_context, &non_network_factories);

  auto factory_bundle =
      std::make_unique<blink::PendingURLLoaderFactoryBundle>();
  for (auto& pair : non_network_factories) {
    const std::string& scheme = pair.first;
    mojo::PendingRemote<network::mojom::URLLoaderFactory>& factory_remote =
        pair.second;

    factory_bundle->pending_scheme_specific_factories().emplace(
        scheme, std::move(factory_remote));
  }

  return factory_bundle;
}

void ServiceWorkerContextWrapper::BindStorageControl(
    mojo::PendingReceiver<storage::mojom::ServiceWorkerStorageControl>
        receiver) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (storage_control_binder_for_test_) {
    storage_control_binder_for_test_.Run(std::move(receiver));
  } else if (base::FeatureList::IsEnabled(
                 features::kStorageServiceOutOfProcess)) {
    // TODO(crbug.com/1055677): Use storage_partition() to bind the control when
    // ServiceWorkerStorageControl is sandboxed in the Storage Service.
    DCHECK(!storage_control_);

    // The database task runner is BLOCK_SHUTDOWN in order to support
    // ClearSessionOnlyOrigins() (called due to the "clear on browser exit"
    // content setting).
    // TODO(falken): Only block shutdown for that particular task, when someday
    // task runners support mixing task shutdown behaviors.
    scoped_refptr<base::SequencedTaskRunner> database_task_runner =
        base::ThreadPool::CreateSequencedTaskRunner(
            {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
    storage_control_ =
        std::make_unique<storage::ServiceWorkerStorageControlImpl>(
            user_data_directory_, std::move(database_task_runner),
            std::move(receiver));
  } else {
    // Drop `receiver` when the browser is shutting down.
    if (!storage_partition())
      return;
    DCHECK(storage_partition()->GetStorageServicePartition());
    storage_partition()
        ->GetStorageServicePartition()
        ->BindServiceWorkerStorageControl(std::move(receiver));
  }
}

void ServiceWorkerContextWrapper::SetStorageControlBinderForTest(
    StorageControlBinder binder) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  storage_control_binder_for_test_ = std::move(binder);
}

void ServiceWorkerContextWrapper::SetLoaderFactoryForUpdateCheckForTest(
    scoped_refptr<network::SharedURLLoaderFactory> loader_factory) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  loader_factory_for_test_ = std::move(loader_factory);
}

scoped_refptr<network::SharedURLLoaderFactory>
ServiceWorkerContextWrapper::GetLoaderFactoryForUpdateCheck(const GURL& scope) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // TODO(https://crbug.com/1211361): Do we want to instrument this with
  // devtools? It is currently not recorded at all.
  return GetLoaderFactoryForBrowserInitiatedRequest(
      scope,
      /*version_id=*/absl::nullopt);
}

scoped_refptr<network::SharedURLLoaderFactory>
ServiceWorkerContextWrapper::GetLoaderFactoryForMainScriptFetch(
    const GURL& scope,
    int64_t version_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return GetLoaderFactoryForBrowserInitiatedRequest(scope, version_id);
}

scoped_refptr<network::SharedURLLoaderFactory>
ServiceWorkerContextWrapper::GetLoaderFactoryForBrowserInitiatedRequest(
    const GURL& scope,
    absl::optional<int64_t> version_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // TODO(falken): Replace this with URLLoaderInterceptor.
  if (loader_factory_for_test_)
    return loader_factory_for_test_;

  if (!storage_partition()) {
    return nullptr;
  }

  mojo::PendingRemote<network::mojom::URLLoaderFactory> remote;
  mojo::PendingReceiver<network::mojom::URLLoaderFactory> pending_receiver =
      remote.InitWithNewPipeAndPassReceiver();
  mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>
      header_client;
  bool bypass_redirect_checks = false;
  // Here we give nullptr for |factory_override|, because CORS is no-op for
  // requests for this factory.
  // TODO(yhirano): Use |factory_override| because someday not just CORS but
  // CORB/CORP will use the factory and those are not no-ops for it
  GetContentClient()->browser()->WillCreateURLLoaderFactory(
      storage_partition_->browser_context(), /*frame=*/nullptr,
      ChildProcessHost::kInvalidUniqueID,
      ContentBrowserClient::URLLoaderFactoryType::kServiceWorkerScript,
      url::Origin::Create(scope), /*navigation_id=*/absl::nullopt,
      ukm::kInvalidSourceIdObj, &pending_receiver, &header_client,
      &bypass_redirect_checks,
      /*disable_secure_dns=*/nullptr,
      /*factory_override=*/nullptr);

  // If we have a version_id, we are fetching a worker main script. We have a
  // DevtoolsAgentHost ready for the worker and we can add the devtools override
  // before instantiating the URLFactoryLoader.
  if (version_id.has_value()) {
    devtools_instrumentation::
        WillCreateURLLoaderFactoryForServiceWorkerMainScript(
            this, version_id.value(), &pending_receiver);
  }

  bool use_client_header_factory = header_client.is_valid();
  if (use_client_header_factory) {
    NavigationURLLoaderImpl::CreateURLLoaderFactoryWithHeaderClient(
        std::move(header_client), std::move(pending_receiver),
        storage_partition());
  } else {
    // Set up a Mojo connection to the network loader factory if it's not been
    // created yet.
    DCHECK(storage_partition());
    scoped_refptr<network::SharedURLLoaderFactory> network_factory =
        storage_partition_->GetURLLoaderFactoryForBrowserProcess();
    network_factory->Clone(std::move(pending_receiver));
  }

  // Clone context()->loader_factory_bundle_for_update_check() and set up the
  // default factory.
  std::unique_ptr<network::PendingSharedURLLoaderFactory>
      loader_factory_bundle_info =
          context()->loader_factory_bundle_for_update_check()->Clone();
  static_cast<blink::PendingURLLoaderFactoryBundle*>(
      loader_factory_bundle_info.get())
      ->pending_default_factory() = std::move(remote);
  static_cast<blink::PendingURLLoaderFactoryBundle*>(
      loader_factory_bundle_info.get())
      ->set_bypass_redirect_checks(bypass_redirect_checks);
  return network::SharedURLLoaderFactory::Create(
      std::move(loader_factory_bundle_info));
}

void ServiceWorkerContextWrapper::WaitForRegistrationsInitializedForTest() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (registrations_initialized_)
    return;
  base::RunLoop loop;
  on_registrations_initialized_ = loop.QuitClosure();
  loop.Run();
}

void ServiceWorkerContextWrapper::DidGetRegisteredStorageKeys(
    const std::vector<blink::StorageKey>& storage_keys) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  for (const blink::StorageKey& storage_key : storage_keys)
    registered_storage_keys_.insert(storage_key);
  registrations_initialized_ = true;
  if (on_registrations_initialized_)
    std::move(on_registrations_initialized_).Run();
}

void ServiceWorkerContextWrapper::ClearRunningServiceWorkers() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (const auto& kv : running_service_workers_) {
    int64_t version_id = kv.first;
    for (auto& observer : observer_list_)
      observer.OnVersionStoppedRunning(version_id);
  }
  running_service_workers_.clear();
}

}  // namespace content
