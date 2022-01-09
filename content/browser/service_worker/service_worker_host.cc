// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_worker/service_worker_host.h"

#include <utility>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/task/post_task.h"
#include "content/browser/broadcast_channel/broadcast_channel_provider.h"
#include "content/browser/broadcast_channel/broadcast_channel_service.h"
#include "content/browser/code_cache/generated_code_cache_context.h"
#include "content/browser/renderer_host/code_cache_host_impl.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/service_worker/service_worker_consts.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/browser/service_worker/service_worker_version.h"
#include "content/browser/webtransport/web_transport_connector_impl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/child_process_host.h"
#include "content/public/common/origin_util.h"
#include "mojo/public/cpp/bindings/message.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/messaging/message_port_channel.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_client.mojom.h"

namespace content {

ServiceWorkerHost::ServiceWorkerHost(
    mojo::PendingAssociatedReceiver<blink::mojom::ServiceWorkerContainerHost>
        host_receiver,
    ServiceWorkerVersion* version,
    base::WeakPtr<ServiceWorkerContextCore> context)
    : version_(version),
      broker_(this),
      container_host_(std::make_unique<content::ServiceWorkerContainerHost>(
          std::move(context))),
      host_receiver_(container_host_.get(), std::move(host_receiver)) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(version_);

  container_host_->set_service_worker_host(this);
  container_host_->UpdateUrls(
      version_->script_url(),
      net::SiteForCookies::FromUrl(version_->script_url()),
      url::Origin::Create(version_->key().top_level_site().GetURL()),
      version_->key());
}

ServiceWorkerHost::~ServiceWorkerHost() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Explicitly destroy the ServiceWorkerContainerHost to release
  // ServiceWorkerObjectHosts and ServiceWorkerRegistrationObjectHosts owned by
  // that. Otherwise, this destructor can trigger their Mojo connection error
  // handlers, which would call back into halfway destroyed |this|. This is
  // because they are associated with the ServiceWorker interface, which can be
  // destroyed while in this destructor (|version_|'s |event_dispatcher_|).
  // See https://crbug.com/854993.
  container_host_.reset();
}

void ServiceWorkerHost::CompleteStartWorkerPreparation(
    int process_id,
    mojo::PendingReceiver<blink::mojom::BrowserInterfaceBroker>
        broker_receiver) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK_EQ(ChildProcessHost::kInvalidUniqueID, worker_process_id_);
  DCHECK_NE(ChildProcessHost::kInvalidUniqueID, process_id);
  worker_process_id_ = process_id;
  broker_receiver_.Bind(std::move(broker_receiver));
}

void ServiceWorkerHost::CreateWebTransportConnector(
    mojo::PendingReceiver<blink::mojom::WebTransportConnector> receiver) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<WebTransportConnectorImpl>(
          worker_process_id_, /*frame=*/nullptr, version_->key().origin(),
          GetNetworkIsolationKey()),
      std::move(receiver));
}

void ServiceWorkerHost::BindCacheStorage(
    mojo::PendingReceiver<blink::mojom::CacheStorage> receiver) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(!base::FeatureList::IsEnabled(
      blink::features::kEagerCacheStorageSetupForServiceWorkers));
  version_->embedded_worker()->BindCacheStorage(std::move(receiver));
}

net::NetworkIsolationKey ServiceWorkerHost::GetNetworkIsolationKey() const {
  // TODO(https://crbug.com/1147281): This is the NetworkIsolationKey of a
  // top-level browsing context, which shouldn't be use for ServiceWorkers used
  // in iframes.
  return net::NetworkIsolationKey::ToDoUseTopFrameOriginAsWell(
      version_->key().origin());
}

const base::UnguessableToken& ServiceWorkerHost::GetReportingSource() const {
  return version_->reporting_source();
}

StoragePartition* ServiceWorkerHost::GetStoragePartition() const {
  // It is possible that the RenderProcessHost is gone but we receive a request
  // before we had the opportunity to Detach because the disconnect handler
  // wasn't run yet. In such cases it is is safe to ignore these messages since
  // we are about to stop the service worker.
  auto* process =
      RenderProcessHost::FromID(version_->embedded_worker()->process_id());
  if (process == nullptr)
    return nullptr;

  return process->GetStoragePartition();
}

void ServiceWorkerHost::CreateCodeCacheHost(
    mojo::PendingReceiver<blink::mojom::CodeCacheHost> receiver) {
  auto embedded_worker_status = version_->embedded_worker()->status();
  // Due to IPC races it is possible that we receive code cache host requests
  // when the worker is stopping. For ex:
  // 1) Browser starts trying to stop, sends the Stop() IPC.
  // 2) Renderer sends a CreateCodeCacheHost() IPC.
  // 3) Renderer gets the Stop() IPC and realize it should try to stop the
  // worker.
  // Given the worker is stopping it is safe to ignore these messages.
  if (embedded_worker_status == EmbeddedWorkerStatus::STOPPING)
    return;

  // Create a new CodeCacheHostImpl and bind it to the given receiver.
  StoragePartition* storage_partition = GetStoragePartition();
  if (!storage_partition) {
    return;
  }
  if (!code_cache_host_receivers_) {
    code_cache_host_receivers_ =
        std::make_unique<CodeCacheHostImpl::ReceiverSet>(
            storage_partition->GetGeneratedCodeCacheContext());
  }
  code_cache_host_receivers_->Add(version_->embedded_worker()->process_id(),
                                  GetNetworkIsolationKey(),
                                  std::move(receiver));
}

void ServiceWorkerHost::CreateBroadcastChannelProvider(
    mojo::PendingReceiver<blink::mojom::BroadcastChannelProvider> receiver) {
  auto* storage_partition_impl =
      static_cast<StoragePartitionImpl*>(GetStoragePartition());
  if (!storage_partition_impl) {
    return;
  }

  auto* broadcast_channel_service =
      storage_partition_impl->GetBroadcastChannelService();
  broadcast_channel_service->AddReceiver(
      std::make_unique<BroadcastChannelProvider>(broadcast_channel_service,
                                                 version()->key()),
      std::move(receiver));
}

base::WeakPtr<ServiceWorkerHost> ServiceWorkerHost::GetWeakPtr() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return weak_factory_.GetWeakPtr();
}

void ServiceWorkerHost::ReportNoBinderForInterface(const std::string& error) {
  broker_receiver_.ReportBadMessage(error + " for the service worker scope");
}

}  // namespace content
