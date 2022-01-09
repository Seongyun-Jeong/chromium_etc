// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/render_frame_host_delegate.h"

#include <stddef.h>
#include <memory>
#include <string>
#include <utility>

#include "base/callback.h"
#include "base/callback_helpers.h"
#include "build/build_config.h"
#include "ipc/ipc_message.h"
#include "third_party/blink/public/mojom/frame/fullscreen.mojom.h"
#include "third_party/blink/public/mojom/frame/text_autosizer_page_info.mojom.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom-shared.h"
#include "ui/gfx/native_widget_types.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace content {

bool RenderFrameHostDelegate::OnMessageReceived(
    RenderFrameHostImpl* render_frame_host,
    const IPC::Message& message) {
  return false;
}

bool RenderFrameHostDelegate::DidAddMessageToConsole(
    RenderFrameHostImpl* source_frame,
    blink::mojom::ConsoleMessageLevel log_level,
    const std::u16string& message,
    int32_t line_no,
    const std::u16string& source_id,
    const absl::optional<std::u16string>& untrusted_stack_trace) {
  return false;
}

void RenderFrameHostDelegate::RequestMediaAccessPermission(
    const MediaStreamRequest& request,
    MediaResponseCallback callback) {
  LOG(ERROR) << "RenderFrameHostDelegate::RequestMediaAccessPermission: "
             << "Not supported.";
  std::move(callback).Run(blink::MediaStreamDevices(),
                          blink::mojom::MediaStreamRequestResult::NOT_SUPPORTED,
                          std::unique_ptr<MediaStreamUI>());
}

bool RenderFrameHostDelegate::CheckMediaAccessPermission(
    RenderFrameHostImpl* render_frame_host,
    const url::Origin& security_origin,
    blink::mojom::MediaStreamType type) {
  LOG(ERROR) << "RenderFrameHostDelegate::CheckMediaAccessPermission: "
             << "Not supported.";
  return false;
}

std::string RenderFrameHostDelegate::GetDefaultMediaDeviceID(
    blink::mojom::MediaStreamType type) {
  return std::string();
}

ui::AXMode RenderFrameHostDelegate::GetAccessibilityMode() {
  return ui::AXMode();
}

device::mojom::GeolocationContext*
RenderFrameHostDelegate::GetGeolocationContext() {
  return nullptr;
}

#if defined(OS_ANDROID)
void RenderFrameHostDelegate::GetNFC(
    RenderFrameHost* render_frame_host,
    mojo::PendingReceiver<device::mojom::NFC> receiver) {}
#endif

bool RenderFrameHostDelegate::CanEnterFullscreenMode(
    RenderFrameHostImpl* requesting_frame,
    const blink::mojom::FullscreenOptions& options) {
  return true;
}

void RenderFrameHostDelegate::FullscreenStateChanged(
    RenderFrameHostImpl* rfh,
    bool is_fullscreen,
    blink::mojom::FullscreenOptionsPtr options) {}

bool RenderFrameHostDelegate::ShouldRouteMessageEvent(
    RenderFrameHostImpl* target_rfh,
    SiteInstance* source_site_instance) const {
  return false;
}

RenderFrameHostImpl*
RenderFrameHostDelegate::GetFocusedFrameIncludingInnerWebContents() {
  return nullptr;
}

std::unique_ptr<WebUIImpl>
RenderFrameHostDelegate::CreateWebUIForRenderFrameHost(
    RenderFrameHostImpl* frame_host,
    const GURL& url) {
  return nullptr;
}

FrameTree* RenderFrameHostDelegate::CreateNewWindow(
    RenderFrameHostImpl* opener,
    const mojom::CreateNewWindowParams& params,
    bool is_new_browsing_instance,
    bool has_user_gesture,
    SessionStorageNamespace* session_storage_namespace) {
  return nullptr;
}

bool RenderFrameHostDelegate::ShouldAllowRunningInsecureContent(
    bool allowed_per_prefs,
    const url::Origin& origin,
    const GURL& resource_url) {
  return false;
}

#if defined(OS_ANDROID)
base::android::ScopedJavaLocalRef<jobject>
RenderFrameHostDelegate::GetJavaRenderFrameHostDelegate() {
  return nullptr;
}
#endif

Visibility RenderFrameHostDelegate::GetVisibility() {
  return Visibility::HIDDEN;
}

std::vector<FrameTreeNode*> RenderFrameHostDelegate::GetUnattachedOwnedNodes(
    RenderFrameHostImpl* owner) {
  return {};
}

media::MediaMetricsProvider::RecordAggregateWatchTimeCallback
RenderFrameHostDelegate::GetRecordAggregateWatchTimeCallback(
    const GURL& page_main_frame_last_committed_url) {
  return base::NullCallback();
}

void RenderFrameHostDelegate::IsClipboardPasteContentAllowed(
    const GURL& url,
    const ui::ClipboardFormatType& data_type,
    const std::string& data,
    IsClipboardPasteContentAllowedCallback callback) {
  std::move(callback).Run(ClipboardPasteContentAllowed(true));
}

bool RenderFrameHostDelegate::HasSeenRecentScreenOrientationChange() {
  return false;
}

bool RenderFrameHostDelegate::IsTransientAllowFullscreenActive() const {
  return false;
}

bool RenderFrameHostDelegate::IsBackForwardCacheSupported() {
  return false;
}

RenderWidgetHostImpl* RenderFrameHostDelegate::CreateNewPopupWidget(
    AgentSchedulingGroupHost& agent_scheduling_group,
    int32_t route_id,
    mojo::PendingAssociatedReceiver<blink::mojom::PopupWidgetHost>
        blink_popup_widget_host,
    mojo::PendingAssociatedReceiver<blink::mojom::WidgetHost> blink_widget_host,
    mojo::PendingAssociatedRemote<blink::mojom::Widget> blink_widget) {
  return nullptr;
}

bool RenderFrameHostDelegate::ShowPopupMenu(
    RenderFrameHostImpl* render_frame_host,
    mojo::PendingRemote<blink::mojom::PopupMenuClient>* popup_client,
    const gfx::Rect& bounds,
    int32_t item_height,
    double font_size,
    int32_t selected_item,
    std::vector<blink::mojom::MenuItemPtr>* menu_items,
    bool right_aligned,
    bool allow_multiple_selection) {
  return false;
}

std::vector<RenderFrameHostImpl*>
RenderFrameHostDelegate::GetActiveTopLevelDocumentsInBrowsingContextGroup(
    RenderFrameHostImpl* render_frame_host) {
  return std::vector<RenderFrameHostImpl*>();
}

PrerenderHostRegistry* RenderFrameHostDelegate::GetPrerenderHostRegistry() {
  return nullptr;
}

bool RenderFrameHostDelegate::IsAllowedToGoToEntryAtOffset(int32_t offset) {
  return true;
}

}  // namespace content
