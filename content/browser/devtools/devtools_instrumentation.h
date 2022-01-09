// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CONTENT_BROWSER_DEVTOOLS_DEVTOOLS_INSTRUMENTATION_H_
#define CONTENT_BROWSER_DEVTOOLS_DEVTOOLS_INSTRUMENTATION_H_

/*
  The functions in this file are for routing instrumentation signals
  to the relevant set of devtools protocol handlers.
*/

#include <vector>

#include "content/browser/devtools/devtools_throttle_handle.h"
#include "content/browser/renderer_host/back_forward_cache_impl.h"
#include "content/common/content_export.h"
#include "content/public/browser/certificate_request_result_type.h"
#include "content/public/browser/global_routing_id.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "net/filter/source_stream.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom-forward.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom.h"
#include "third_party/blink/public/mojom/devtools/inspector_issue.mojom-forward.h"
#include "third_party/blink/public/mojom/navigation/navigation_params.mojom-forward.h"
#include "third_party/blink/public/mojom/page/widget.mojom.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom-forward.h"

class GURL;

namespace base {
class UnguessableToken;
}

namespace blink {
struct UserAgentMetadata;
}

namespace net {
class SSLInfo;
class X509Certificate;
struct WebTransportError;
}  // namespace net

namespace download {
struct DownloadCreateInfo;
class DownloadItem;
}  // namespace download

namespace content {
class BackForwardCacheCanStoreDocumentResult;
class BrowserContext;
class DevToolsAgentHostImpl;
class FencedFrame;
class FrameTreeNode;
class NavigationHandle;
class NavigationRequest;
class NavigationThrottle;
class RenderFrameHost;
class RenderFrameHostImpl;
class RenderProcessHost;
class SharedWorkerHost;
class ServiceWorkerContextWrapper;
class SignedExchangeEnvelope;
class StoragePartition;
class WebContents;

struct SignedExchangeError;

namespace protocol {
namespace Audits {
class InspectorIssue;
}  // namespace Audits
}  // namespace protocol

namespace devtools_instrumentation {

// If this function caused the User-Agent header to be overridden,
// `devtools_user_agent_overridden` will be set to true; otherwise, it will be
// set to false.
void ApplyNetworkRequestOverrides(
    FrameTreeNode* frame_tree_node,
    blink::mojom::BeginNavigationParams* begin_params,
    bool* report_raw_headers,
    absl::optional<std::vector<net::SourceStream::SourceType>>*
        devtools_accepted_stream_types,
    bool* devtools_user_agent_overridden);

// Returns true if devtools want |*override_out| to be used.
// (A true return and |*override_out| being nullopt means no user agent client
//  hints should be sent; a false return means devtools doesn't want to affect
//  the behavior).
bool ApplyUserAgentMetadataOverrides(
    FrameTreeNode* frame_tree_node,
    absl::optional<blink::UserAgentMetadata>* override_out);

bool WillCreateURLLoaderFactory(
    RenderFrameHostImpl* rfh,
    bool is_navigation,
    bool is_download,
    mojo::PendingReceiver<network::mojom::URLLoaderFactory>*
        target_factory_receiver,
    network::mojom::URLLoaderFactoryOverridePtr* factory_override);

bool WillCreateURLLoaderFactoryForServiceWorker(
    RenderProcessHost* rph,
    int routing_id,
    network::mojom::URLLoaderFactoryOverridePtr* factory_override);

bool WillCreateURLLoaderFactoryForServiceWorkerMainScript(
    const ServiceWorkerContextWrapper* context_wrapper,
    int64_t version_id,
    mojo::PendingReceiver<network::mojom::URLLoaderFactory>*
        loader_factory_receiver);

bool WillCreateURLLoaderFactoryForSharedWorker(
    SharedWorkerHost* host,
    network::mojom::URLLoaderFactoryOverridePtr* factory_override);

bool WillCreateURLLoaderFactoryForWorkerMainScript(
    DevToolsAgentHostImpl* host,
    const base::UnguessableToken& worker_token,
    network::mojom::URLLoaderFactoryOverridePtr* factory_override);

bool WillCreateURLLoaderFactory(
    RenderFrameHostImpl* rfh,
    bool is_navigation,
    bool is_download,
    std::unique_ptr<network::mojom::URLLoaderFactory>* factory);

bool WillCreateURLLoaderFactoryInternal(
    DevToolsAgentHostImpl* agent_host,
    const base::UnguessableToken& devtools_token,
    int process_id,
    StoragePartition* storage_partition,
    bool is_navigation,
    bool is_download,
    mojo::PendingReceiver<network::mojom::URLLoaderFactory>*
        target_factory_receiver,
    network::mojom::URLLoaderFactoryOverridePtr* factory_override);

void OnResetNavigationRequest(NavigationRequest* navigation_request);
void OnNavigationRequestWillBeSent(const NavigationRequest& navigation_request);
void OnNavigationResponseReceived(
    const NavigationRequest& nav_request,
    const network::mojom::URLResponseHead& response);
void OnNavigationRequestFailed(
    const NavigationRequest& nav_request,
    const network::URLLoaderCompletionStatus& status);
bool ShouldBypassCSP(const NavigationRequest& nav_request);

void WillBeginDownload(download::DownloadCreateInfo* info,
                       download::DownloadItem* item);

void BackForwardCacheNotUsed(
    const NavigationRequest* nav_request,
    const BackForwardCacheCanStoreDocumentResult* result,
    const BackForwardCacheCanStoreTreeResult* tree_result);

void OnSignedExchangeReceived(
    FrameTreeNode* frame_tree_node,
    absl::optional<const base::UnguessableToken> devtools_navigation_token,
    const GURL& outer_request_url,
    const network::mojom::URLResponseHead& outer_response,
    const absl::optional<SignedExchangeEnvelope>& header,
    const scoped_refptr<net::X509Certificate>& certificate,
    const absl::optional<net::SSLInfo>& ssl_info,
    const std::vector<SignedExchangeError>& errors);
void OnSignedExchangeCertificateRequestSent(
    FrameTreeNode* frame_tree_node,
    const base::UnguessableToken& request_id,
    const base::UnguessableToken& loader_id,
    const network::ResourceRequest& request,
    const GURL& signed_exchange_url);
void OnSignedExchangeCertificateResponseReceived(
    FrameTreeNode* frame_tree_node,
    const base::UnguessableToken& request_id,
    const base::UnguessableToken& loader_id,
    const GURL& url,
    const network::mojom::URLResponseHead& head);
void OnSignedExchangeCertificateRequestCompleted(
    FrameTreeNode* frame_tree_node,
    const base::UnguessableToken& request_id,
    const network::URLLoaderCompletionStatus& status);

std::vector<std::unique_ptr<NavigationThrottle>> CreateNavigationThrottles(
    NavigationHandle* navigation_handle);

// When registering a new ServiceWorker with PlzServiceWorker, the main script
// fetch happens before starting the worker. This means that we need to give
// TargetHandlers the opportunity to attach to newly created ServiceWorker
// before the script fetch begins if they specified blocking auto-attach
// properties. The `throttle` controls when the script fetch resumes.
//
// Note on the input parameters:
// - `wrapper` and `version_id` are used to identify an existing newly
//   installing service worker agent. It is expected to exist.
// - `requesting_frame_id` is required, because the auto attacher is the one of
//   the frame registering the worker.
void ThrottleServiceWorkerMainScriptFetch(
    ServiceWorkerContextWrapper* wrapper,
    int64_t version_id,
    const GlobalRenderFrameHostId& requesting_frame_id,
    scoped_refptr<DevToolsThrottleHandle> throttle_handle);

// For PlzDedicatedWorker. When creating a new DedicatedWorker with
// PlzDedicatedWorker, the worker script fetch happens before starting the
// worker. This function is called when DedicatedWorkerHost, which is the
// representation of a worker in the browser process, is created.
// `throttle_handle` controls when the script fetch resumes.
void ThrottleWorkerMainScriptFetch(
    const base::UnguessableToken& devtools_worker_token,
    const GlobalRenderFrameHostId& ancestor_render_frame_host_id,
    scoped_refptr<DevToolsThrottleHandle> throttle_handle);

bool ShouldWaitForDebuggerInWindowOpen();

void WillStartDragging(FrameTreeNode* main_frame_tree_node,
                       const blink::mojom::DragDataPtr drag_data,
                       blink::DragOperationsMask drag_operations_mask,
                       bool* intercepted);

// Asks any interested agents to handle the given certificate error. Returns
// |true| if the error was handled, |false| otherwise.
using CertErrorCallback =
    base::RepeatingCallback<void(content::CertificateRequestResultType)>;
bool HandleCertificateError(WebContents* web_contents,
                            int cert_error,
                            const GURL& request_url,
                            CertErrorCallback callback);

void PortalAttached(RenderFrameHostImpl* render_frame_host_impl);
void PortalDetached(RenderFrameHostImpl* render_frame_host_impl);
void PortalActivated(RenderFrameHostImpl* render_frame_host_impl);

void FencedFrameCreated(
    base::SafeRef<RenderFrameHostImpl> owner_render_frame_host,
    FencedFrame* fenced_frame);

void ReportSameSiteCookieIssue(
    RenderFrameHostImpl* render_frame_host_impl,
    const network::mojom::CookieOrLineWithAccessResultPtr& excluded_cookie,
    const GURL& url,
    const net::SiteForCookies& site_for_cookies,
    blink::mojom::SameSiteCookieOperation operation,
    const absl::optional<std::string>& devtools_request_id);

enum class AttributionReportingIssueType {
  kAttributionTriggerDataTooLarge,
  kAttributionEventSourceTriggerDataTooLarge,
};

void ReportAttributionReportingIssue(
    RenderFrameHost* render_frame_host,
    AttributionReportingIssueType issue_type,
    const absl::optional<std::string>& request_id,
    const absl::optional<std::string>& invalid_parameter);

// This function works similar to RenderFrameHostImpl::AddInspectorIssue, in
// that it reports an InspectorIssue to DevTools clients. The difference is that
// |ReportBrowserInitiatedIssue| sends issues directly to clients instead of
// going through the issue storage in the renderer process. Sending issues
// directly prevents them from being (potentially) lost during navigations.
//
// DevTools must be attached, otherwise issues reported through
// |ReportBrowserInitiatedIssue| are lost.
void CONTENT_EXPORT
ReportBrowserInitiatedIssue(RenderFrameHostImpl* frame,
                            protocol::Audits::InspectorIssue* issue);

// Produces an inspector issue and sends it to the client with
// |ReportBrowserInitiatedIssue|.
// This only support TrustedWebActivityIssue for now.
void BuildAndReportBrowserInitiatedIssue(
    RenderFrameHostImpl* frame,
    blink::mojom::InspectorIssueInfoPtr info);

// Produces a Heavy Ad Issue based on the parameters passed in.
std::unique_ptr<protocol::Audits::InspectorIssue> GetHeavyAdIssue(
    RenderFrameHostImpl* frame,
    blink::mojom::HeavyAdResolutionStatus resolution,
    blink::mojom::HeavyAdReason reason);

void OnWebTransportHandshakeFailed(
    RenderFrameHostImpl* frame_host,
    const GURL& url,
    const absl::optional<net::WebTransportError>& error);

void OnServiceWorkerMainScriptFetchingFailed(
    const GlobalRenderFrameHostId& requesting_frame_id,
    const std::string& error);

// Fires `Network.onLoadingFailed` event for a dedicated worker main script.
// Used for PlzDedicatedWorker.
void OnWorkerMainScriptLoadingFailed(
    const GURL& url,
    const base::UnguessableToken& worker_token,
    FrameTreeNode* ftn,
    RenderFrameHostImpl* ancestor_rfh,
    const network::URLLoaderCompletionStatus& status);

// Fires `Network.onLoadingFinished` event for a dedicated worker main script.
// Used for PlzDedicatedWorker.
void OnWorkerMainScriptLoadingFinished(
    FrameTreeNode* ftn,
    const base::UnguessableToken& worker_token,
    const network::URLLoaderCompletionStatus& status);

// Fires `Network.onRequestWillBeSent` event for a dedicated worker and shared
// worker main script. Used for PlzDedicatedWorker/PlzSharedWorker.
void OnWorkerMainScriptRequestWillBeSent(
    FrameTreeNode* ftn,
    const base::UnguessableToken& worker_token,
    const network::ResourceRequest& request);

// Adds a message from a worklet to the devtools console. This is specific to
// FLEDGE auction worklet and shared storage worklet where the message may
// contain sensitive cross-origin information, and therefore the devtools
// logging needs to bypass the usual path through the renderer.
void CONTENT_EXPORT
LogWorkletMessage(RenderFrameHostImpl& frame_host,
                  blink::mojom::ConsoleMessageLevel log_level,
                  const std::string& message);

void ApplyNetworkContextParamsOverrides(
    BrowserContext* browser_context,
    network::mojom::NetworkContextParams* network_context_params);

void DidRejectCrossOriginPortalMessage(
    RenderFrameHostImpl* render_frame_host_impl);

}  // namespace devtools_instrumentation

}  // namespace content

#endif  // CONTENT_BROWSER_DEVTOOLS_DEVTOOLS_INSTRUMENTATION_H_
