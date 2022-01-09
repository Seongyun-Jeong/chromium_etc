// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RENDER_FRAME_HOST_DELEGATE_H_
#define CONTENT_BROWSER_RENDERER_HOST_RENDER_FRAME_HOST_DELEGATE_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/i18n/rtl.h"
#include "build/build_config.h"
#include "components/viz/common/surfaces/surface_id.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/webui/web_ui_impl.h"
#include "content/common/content_export.h"
#include "content/public/browser/javascript_dialog_manager.h"
#include "content/public/browser/media_player_watch_time.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/visibility.h"
#include "content/public/common/javascript_dialog_type.h"
#include "media/mojo/mojom/media_player.mojom.h"
#include "media/mojo/services/media_metrics_provider.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/scoped_interface_endpoint_handle.h"
#include "net/cert/cert_status_flags.h"
#include "net/http/http_response_headers.h"
#include "services/device/public/mojom/geolocation_context.mojom.h"
#include "services/device/public/mojom/wake_lock.mojom.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/mediastream/media_stream_request.h"
#include "third_party/blink/public/mojom/choosers/popup_menu.mojom.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom.h"
#include "third_party/blink/public/mojom/favicon/favicon_url.mojom.h"
#include "third_party/blink/public/mojom/frame/blocked_navigation_types.mojom.h"
#include "third_party/blink/public/mojom/frame/frame.mojom-forward.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom.h"
#include "third_party/blink/public/mojom/media/capture_handle_config.mojom.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/base/window_open_disposition.h"

#if defined(OS_WIN)
#include "ui/gfx/native_widget_types.h"
#endif

#if defined(OS_ANDROID)
#include "base/android/scoped_java_ref.h"
#include "services/device/public/mojom/nfc.mojom.h"
#endif

class GURL;

namespace IPC {
class Message;
}

namespace gfx {
class Rect;
class Size;
}  // namespace gfx

namespace url {
class Origin;
}

namespace blink {
namespace mojom {
class DisplayCutoutHost;
class FullscreenOptions;
}
class PageState;
namespace web_pref {
struct WebPreferences;
}
}  // namespace blink

namespace device {
namespace mojom {
class ScreenOrientation;
}
}  // namespace device

namespace ui {
class ClipboardFormatType;
}

namespace content {
class AgentSchedulingGroupHost;
class FrameTreeNode;
class PrerenderHostRegistry;
class RenderFrameHostImpl;
class RenderWidgetHostImpl;
class SessionStorageNamespace;
struct AXEventNotificationDetails;
struct AXLocationChangeNotificationDetails;
struct ContextMenuParams;
struct GlobalRequestID;

namespace mojom {
class CreateNewWindowParams;
}

// An interface implemented by an object interested in knowing about the state
// of the RenderFrameHost.
//
// Layering note: Generally, WebContentsImpl should be the only implementation
// of this interface. In particular, WebContents::FromRenderFrameHost() assumes
// this. This delegate interface is useful for renderer_host/ to make requests
// to WebContentsImpl, as renderer_host/ is not permitted to know the
// WebContents type (see //renderer_host/DEPS).
class CONTENT_EXPORT RenderFrameHostDelegate {
 public:
  // Callback used with HandleClipboardPaste() method.  If the clipboard paste
  // is allowed to proceed, the callback is called with true.  Otherwise the
  // callback is called with false.
  using ClipboardPasteContentAllowed =
      RenderFrameHostImpl::ClipboardPasteContentAllowed;
  using IsClipboardPasteContentAllowedCallback =
      RenderFrameHostImpl::IsClipboardPasteContentAllowedCallback;

  using JavaScriptDialogCallback =
      content::JavaScriptDialogManager::DialogClosedCallback;

  // This is used to give the delegate a chance to filter IPC messages.
  virtual bool OnMessageReceived(RenderFrameHostImpl* render_frame_host,
                                 const IPC::Message& message);

  // Notification from the renderer host that a suspicious navigation of the
  // main frame has been blocked. Allows the delegate to provide some UI to let
  // the user know about the blocked navigation and give them the option to
  // recover from it.
  // |blocked_url| is the blocked navigation target, |initiator_url| is the URL
  // of the frame initiating the navigation, |reason| specifies why the
  // navigation was blocked.
  virtual void OnDidBlockNavigation(
      const GURL& blocked_url,
      const GURL& initiator_url,
      blink::mojom::NavigationBlockedReason reason) {}

  // Called when blink.mojom.LocalFrameHost::DidFinishLoad() is invoked.
  virtual void OnDidFinishLoad(RenderFrameHostImpl* render_frame_host,
                               const GURL& url) {}

  // Notifies that the manifest URL is updated.
  virtual void OnManifestUrlChanged(const PageImpl& page) {}

  // A message was added to to the console. |source_id| is a URL.
  // |untrusted_stack_trace| is not present for most messages; only when
  // requested in advance and only for exceptions.
  virtual bool DidAddMessageToConsole(
      RenderFrameHostImpl* source_frame,
      blink::mojom::ConsoleMessageLevel log_level,
      const std::u16string& message,
      int32_t line_no,
      const std::u16string& source_id,
      const absl::optional<std::u16string>& untrusted_stack_trace);

  // Called when a RenderFrame for |render_frame_host| is created in the
  // renderer process. Use |RenderFrameDeleted| to listen for when this
  // RenderFrame goes away.
  virtual void RenderFrameCreated(RenderFrameHostImpl* render_frame_host) {}

  // Called when a RenderFrame for |render_frame_host| is deleted or the
  // renderer process in which it runs it has died. Use |RenderFrameCreated| to
  // listen for when RenderFrame objects are created.
  virtual void RenderFrameDeleted(RenderFrameHostImpl* render_frame_host) {}

  // A context menu should be shown, to be built using the context information
  // provided in the supplied params.
  virtual void ShowContextMenu(
      RenderFrameHost& render_frame_host,
      mojo::PendingAssociatedRemote<blink::mojom::ContextMenuClient>
          context_menu_client,
      const ContextMenuParams& params) {}

  // A JavaScript alert, confirmation or prompt dialog should be shown.
  // Will only be called for active frames belonging to a primary page.
  virtual void RunJavaScriptDialog(RenderFrameHostImpl* render_frame_host,
                                   const std::u16string& message,
                                   const std::u16string& default_prompt,
                                   JavaScriptDialogType type,
                                   bool disable_third_party_subframe_suppresion,
                                   JavaScriptDialogCallback callback) {}

  // Will only be called for active frames belonging to a primary page.
  virtual void RunBeforeUnloadConfirm(RenderFrameHostImpl* render_frame_host,
                                      bool is_reload,
                                      JavaScriptDialogCallback callback) {}

  // Notifies when new blink::mojom::FaviconURLPtr candidates are received from
  // the renderer process.
  virtual void UpdateFaviconURL(
      RenderFrameHostImpl* source,
      const std::vector<blink::mojom::FaviconURLPtr>& candidates) {}

  // The frame changed its window.name property.
  virtual void DidChangeName(RenderFrameHostImpl* render_frame_host,
                             const std::string& name) {}

  // Called when a frame receives user activation. This may be called multiple
  // times for the same frame. Does not include frames activated by the
  // same-origin visibility heuristic, see `UserActivationState` for details.
  virtual void DidReceiveUserActivation(
      RenderFrameHostImpl* render_frame_host) {}

  // Binds a DisplayCutoutHost object associated to |render_frame_host|.
  virtual void BindDisplayCutoutHost(
      RenderFrameHostImpl* render_frame_host,
      mojo::PendingAssociatedReceiver<blink::mojom::DisplayCutoutHost>
          receiver) {}

  // The display style of the frame has changed.
  virtual void DidChangeDisplayState(RenderFrameHostImpl* render_frame_host,
                                     bool is_display_none) {}

  // The size of the frame has changed.
  virtual void FrameSizeChanged(RenderFrameHostImpl* render_frame_host,
                                const gfx::Size& frame_size) {}

  // The DOMContentLoaded handler in the frame has completed.
  virtual void DOMContentLoaded(RenderFrameHostImpl* render_frame_host) {}

  // The onload handler in the frame has completed. Only called for the top-
  // level frame.
  virtual void DocumentOnLoadCompleted(RenderFrameHostImpl* render_frame_host) {
  }

  // The page's title was changed and should be updated. Only called for the
  // top-level frame.
  virtual void UpdateTitle(RenderFrameHostImpl* render_frame_host,
                           const std::u16string& title,
                           base::i18n::TextDirection title_direction) {}

  // The destination URL has changed and should be updated.
  virtual void UpdateTargetURL(RenderFrameHostImpl* render_frame_host,
                               const GURL& url) {}

  // Creates a MediaPlayerHost object associated to |frame_host| via its
  // associated MediaWebContentsObserver, and binds |receiver| to it.
  virtual void CreateMediaPlayerHostForRenderFrameHost(
      RenderFrameHostImpl* frame_host,
      mojo::PendingAssociatedReceiver<media::mojom::MediaPlayerHost> receiver) {
  }

  // The render frame has requested access to media devices listed in
  // |request|, and the client should grant or deny that permission by
  // calling |callback|.
  virtual void RequestMediaAccessPermission(const MediaStreamRequest& request,
                                            MediaResponseCallback callback);

  // Checks if we have permission to access the microphone or camera. Note that
  // this does not query the user. |type| must be MEDIA_DEVICE_AUDIO_CAPTURE
  // or MEDIA_DEVICE_VIDEO_CAPTURE.
  virtual bool CheckMediaAccessPermission(
      RenderFrameHostImpl* render_frame_host,
      const url::Origin& security_origin,
      blink::mojom::MediaStreamType type);

  // Returns the ID of the default device for the given media device |type|.
  // If the returned value is an empty string, it means that there is no
  // default device for the given |type|.
  virtual std::string GetDefaultMediaDeviceID(
      blink::mojom::MediaStreamType type);

  // Setter for the capture handle config, which allows a captured application
  // to opt-in to exposing information to its capturer(s).
  virtual void SetCaptureHandleConfig(
      blink::mojom::CaptureHandleConfigPtr config) {}

  // Get the accessibility mode for the WebContents that owns this frame.
  virtual ui::AXMode GetAccessibilityMode();

  // Called whenever the AXTreeID for the topmost RenderFrameHost has changed.
  virtual void AXTreeIDForMainFrameHasChanged() {}

  // Called when accessibility events or location changes are received
  // from a render frame, when the accessibility mode has the
  // ui::AXMode::kWebContents flag set.
  virtual void AccessibilityEventReceived(
      const AXEventNotificationDetails& details) {}
  virtual void AccessibilityLocationChangesReceived(
      const std::vector<AXLocationChangeNotificationDetails>& details) {}

  // Gets the GeolocationContext associated with this delegate.
  virtual device::mojom::GeolocationContext* GetGeolocationContext();

#if defined(OS_ANDROID)
  // Gets an NFC implementation within the context of this delegate.
  virtual void GetNFC(RenderFrameHost* render_frame_host,
                      mojo::PendingReceiver<device::mojom::NFC> receiver);
#endif

  // Returns whether entering fullscreen with EnterFullscreenMode() is allowed.
  virtual bool CanEnterFullscreenMode(
      RenderFrameHostImpl* requesting_frame,
      const blink::mojom::FullscreenOptions& options);

  // Notification that the frame with the given host wants to enter fullscreen
  // mode. Must only be called if CanEnterFullscreenMode returns true.
  virtual void EnterFullscreenMode(
      RenderFrameHostImpl* requesting_frame,
      const blink::mojom::FullscreenOptions& options) {}

  // Notification that the frame wants to go out of fullscreen mode.
  // |will_cause_resize| indicates whether the fullscreen change causes a
  // view resize. e.g. This will be false when going from tab fullscreen to
  // browser fullscreen.
  virtual void ExitFullscreenMode(bool will_cause_resize) {}

  // Notification that this frame has changed fullscreen state.
  virtual void FullscreenStateChanged(
      RenderFrameHostImpl* rfh,
      bool is_fullscreen,
      blink::mojom::FullscreenOptionsPtr options);

#if defined(OS_ANDROID)
  // Updates information to determine whether a user gesture should carryover to
  // future navigations. This is needed so navigations within a certain
  // timeframe of a request initiated by a gesture will be treated as if they
  // were initiated by a gesture too, otherwise the navigation may be blocked.
  virtual void UpdateUserGestureCarryoverInfo() {}
#endif

  // Let the delegate decide whether postMessage should be delivered to
  // |target_rfh| from a source frame in the given SiteInstance.  This defaults
  // to false and overrides the RenderFrameHost's decision if true.
  virtual bool ShouldRouteMessageEvent(
      RenderFrameHostImpl* target_rfh,
      SiteInstance* source_site_instance) const;

  // Ensure that |source_rfh| has swapped-out RenderViews and
  // RenderFrameProxies for itself and for all frames on its opener chain in
  // the current frame's SiteInstance.
  //
  // TODO(alexmos): This method currently supports cross-process postMessage,
  // where we may need to create any missing proxies for the message's source
  // frame and its opener chain. It currently exists in WebContents due to a
  // special case for <webview> guests, but this logic should eventually be
  // moved down into RenderFrameProxyHost::RouteMessageEvent when <webview>
  // refactoring for --site-per-process mode is further along.  See
  // https://crbug.com/330264.
  virtual void EnsureOpenerProxiesExist(RenderFrameHostImpl* source_rfh) {}

  // Set the |node| frame as focused in the current FrameTree as well as
  // possibly changing focus in distinct but related inner/outer WebContents.
  virtual void SetFocusedFrame(FrameTreeNode* node, SiteInstance* source) {}

  // The frame called |window.focus()|.
  virtual void DidCallFocus() {}

  // Searches the WebContents for a focused frame, potentially in an inner
  // WebContents. If this WebContents has no focused frame, returns |nullptr|.
  // If there is no inner WebContents at the focused tree node, returns its
  // RenderFrameHost. If there is an inner WebContents, search it for focused
  // frames and inner contents. If an inner WebContents does not have a focused
  // frame, return its main frame, since the attachment frame in its outer
  // WebContents is not live.
  virtual RenderFrameHostImpl* GetFocusedFrameIncludingInnerWebContents();

  // Called by when |source_rfh| advances focus to a RenderFrameProxyHost.
  virtual void OnAdvanceFocus(RenderFrameHostImpl* source_rfh) {}

  // Creates a WebUI object for a frame navigating to |url|. If no WebUI
  // applies, returns null.
  virtual std::unique_ptr<WebUIImpl> CreateWebUIForRenderFrameHost(
      RenderFrameHostImpl* frame_host,
      const GURL& url);

  // Called by |frame| to notify that it has received an update on focused
  // element. |bounds_in_root_view| is the rectangle containing the element that
  // is focused and is with respect to root frame's RenderWidgetHost's
  // coordinate space.
  virtual void OnFocusedElementChangedInFrame(
      RenderFrameHostImpl* frame,
      const gfx::Rect& bounds_in_root_view,
      blink::mojom::FocusType focus_type) {}

  // The page is trying to open a new page (e.g. a popup window). The window
  // should be created associated the process of |opener|, but it should not
  // be shown yet. That should happen in response to ShowCreatedWindow.
  // |params.window_container_type| describes the type of RenderViewHost
  // container that is requested -- in particular, the window.open call may
  // have specified 'background' and 'persistent' in the feature string.
  //
  // The passed |opener| is the RenderFrameHost initiating the window creation.
  // It will never be null, even if the opener is suppressed via |params|.
  //
  // The passed |params.frame_name| parameter is the name parameter that was
  // passed to window.open(), and will be empty if none was passed.
  //
  // Note: this is not called "CreateWindow" because that will clash with
  // the Windows function which is actually a #define.
  //
  // On success, a non-owning pointer to the new FrameTree is returned.
  //
  // The caller is expected to handle cleanup if this operation fails or is
  // suppressed by checking if the return value is null.
  virtual FrameTree* CreateNewWindow(
      RenderFrameHostImpl* opener,
      const mojom::CreateNewWindowParams& params,
      bool is_new_browsing_instance,
      bool has_user_gesture,
      SessionStorageNamespace* session_storage_namespace);

  // Show a previously created page with the specified disposition and bounds.
  // The window is identified by the |main_frame_widget_route_id| passed to
  // CreateNewWindow.
  //
  // The passed |opener| is the RenderFrameHost initiating the window creation.
  // It will never be null, even if the opener is suppressed via |params|.
  //
  // Note: this is not called "ShowWindow" because that will clash with
  // the Windows function which is actually a #define.
  virtual void ShowCreatedWindow(RenderFrameHostImpl* opener,
                                 int main_frame_widget_route_id,
                                 WindowOpenDisposition disposition,
                                 const gfx::Rect& initial_rect,
                                 bool user_gesture) {}

  // The main frame document element is ready. This happens when the document
  // has finished parsing.
  virtual void DocumentAvailableInMainFrame(
      RenderFrameHost* render_frame_host) {}

  // Reports that passive mixed content was found at the specified url.
  virtual void PassiveInsecureContentFound(const GURL& resource_url) {}

  // Checks if running of active mixed content is allowed in the current tab.
  virtual bool ShouldAllowRunningInsecureContent(bool allowed_per_prefs,
                                                 const url::Origin& origin,
                                                 const GURL& resource_url);

  // Opens a new view-source tab for the last committed document in |frame|.
  virtual void ViewSource(RenderFrameHostImpl* frame) {}

#if defined(OS_ANDROID)
  virtual base::android::ScopedJavaLocalRef<jobject>
  GetJavaRenderFrameHostDelegate();
#endif

  // Notified that the render finished loading a subresource for the frame
  // associated with |render_frame_host|.
  virtual void ResourceLoadComplete(
      RenderFrameHostImpl* render_frame_host,
      const GlobalRequestID& request_id,
      blink::mojom::ResourceLoadInfoPtr resource_load_info) {}

  // Request to print a frame that is in a different process than its parent.
  virtual void PrintCrossProcessSubframe(
      const gfx::Rect& rect,
      int document_cookie,
      RenderFrameHostImpl* render_frame_host) {}

  // Request to paint preview a frame that is in a different process that its
  // parent.
  virtual void CapturePaintPreviewOfCrossProcessSubframe(
      const gfx::Rect& rect,
      const base::UnguessableToken& guid,
      RenderFrameHostImpl* render_frame_host) {}

  // Returns a copy of the current WebPreferences associated with this
  // RenderFrameHost's WebContents. If it does not exist, this will create one
  // and send the newly computed value to all renderers.
  // Note that this will not trigger a recomputation of WebPreferences if it
  // already exists - this will return the last computed/set value of
  // WebPreferences. If we want to guarantee that the value reflects the current
  // state of the WebContents, NotifyPreferencesChanged() should be called
  // before calling this.
  virtual const blink::web_pref::WebPreferences&
  GetOrCreateWebPreferences() = 0;

  // Returns the visibility of the delegate.
  virtual Visibility GetVisibility();

  // Notify observers if WebAudio AudioContext has started (or stopped) playing
  // audible sounds.
  virtual void AudioContextPlaybackStarted(RenderFrameHostImpl* host,
                                           int context_id) {}
  virtual void AudioContextPlaybackStopped(RenderFrameHostImpl* host,
                                           int context_id) {}

  // Notifies observers if the frame has changed audible state.
  virtual void OnFrameAudioStateChanged(RenderFrameHostImpl* host,
                                        bool is_audible) {}

  // Returns FrameTreeNodes that are logically owned by another frame even
  // though this relationship is not yet reflected in their frame trees. This
  // can happen, for example, with unattached guests and orphaned portals.
  virtual std::vector<FrameTreeNode*> GetUnattachedOwnedNodes(
      RenderFrameHostImpl* owner);

  // Registers a new URL handler for the given protocol.
  virtual void RegisterProtocolHandler(RenderFrameHostImpl* host,
                                       const std::string& scheme,
                                       const GURL& url,
                                       bool user_gesture) {}

  // Unregisters a given URL handler for the given protocol.
  virtual void UnregisterProtocolHandler(RenderFrameHostImpl* host,
                                         const std::string& scheme,
                                         const GURL& url,
                                         bool user_gesture) {}

  // Returns true if the delegate allows to go to the session history entry at
  // the given offset (ie, -1 will return the "back" item).
  virtual bool IsAllowedToGoToEntryAtOffset(int32_t offset);

  virtual media::MediaMetricsProvider::RecordAggregateWatchTimeCallback
  GetRecordAggregateWatchTimeCallback(
      const GURL& page_main_frame_last_committed_url);

  // Determines if a clipboard paste using |data| of type |data_type| is allowed
  // in this renderer frame.  Possible data types supported for paste can be
  // seen in the ClipboardHostImpl class.  Text based formats will use the
  // data_type ui::ClipboardFormatType::PlainTextType() unless it is known
  // to be of a more specific type, like RTF or HTML, in which case a type
  // such as ui::ClipboardFormatType::RtfType() or
  // ui::ClipboardFormatType::HtmlType() is used.
  //
  // It is also possible for the data type to be
  // ui::ClipboardFormatType::WebCustomDataType() indicating that the paste
  // uses a custom data format.  It is up to the implementation to attempt to
  // understand the type if possible.  It is acceptable to deny pastes of
  // unknown data types.
  //
  // The implementation is expected to show UX to the user if needed.  If
  // shown, the UX should be associated with the specific render frame host.
  //
  // The callback is called, possibly asynchronously, with a status indicating
  // whether the operation is allowed or not.
  virtual void IsClipboardPasteContentAllowed(
      const GURL& url,
      const ui::ClipboardFormatType& data_type,
      const std::string& data,
      IsClipboardPasteContentAllowedCallback callback);

  // Notified when the main frame of `source` adjusts the page scale.
  virtual void OnPageScaleFactorChanged(PageImpl& source) {}

  // Binds a ScreenOrientation object associated to |render_frame_host|.
  virtual void BindScreenOrientation(
      RenderFrameHost* render_frame_host,
      mojo::PendingAssociatedReceiver<device::mojom::ScreenOrientation>
          receiver) {}

  // Return true if we have seen a recent orientation change, which is used to
  // decide if we should consume user activation when entering fullscreen.
  virtual bool HasSeenRecentScreenOrientationChange();

  // Return true if the page has a transient affordance to enter fullscreen
  // without consuming user activation.
  virtual bool IsTransientAllowFullscreenActive() const;

  // Return true if the back forward cache is supported. This is not an
  // indication that the cache will be used.
  virtual bool IsBackForwardCacheSupported();

  // The page is trying to open a new widget (e.g. a select popup). The
  // widget should be created associated with the given
  // |agent_scheduling_group|, but it should not be shown yet. That should
  // happen in response to ShowCreatedWidget.
  virtual RenderWidgetHostImpl* CreateNewPopupWidget(
      AgentSchedulingGroupHost& agent_scheduling_group,
      int32_t route_id,
      mojo::PendingAssociatedReceiver<blink::mojom::PopupWidgetHost>
          blink_popup_widget_host,
      mojo::PendingAssociatedReceiver<blink::mojom::WidgetHost>
          blink_widget_host,
      mojo::PendingAssociatedRemote<blink::mojom::Widget> blink_widget);

  // Return true if the popup is shown through WebContentsObserver.
  // BrowserPluginGuest for the guest WebContents will show the popup on Mac,
  // then, we should skip to show the popup at RenderViewHostDelegateView.
  virtual bool ShowPopupMenu(
      RenderFrameHostImpl* render_frame_host,
      mojo::PendingRemote<blink::mojom::PopupMenuClient>* popup_client,
      const gfx::Rect& bounds,
      int32_t item_height,
      double font_size,
      int32_t selected_item,
      std::vector<blink::mojom::MenuItemPtr>* menu_items,
      bool right_aligned,
      bool allow_multiple_selection);

  virtual void DidLoadResourceFromMemoryCache(
      RenderFrameHostImpl* source,
      const GURL& url,
      const std::string& http_request,
      const std::string& mime_type,
      network::mojom::RequestDestination request_destination,
      bool include_credentials) {}

  // Called when the renderer sends a response via DomAutomationController.
  // For example, `window.domAutomationController.send(foo())` sends the result
  // of foo() here.
  virtual void DomOperationResponse(RenderFrameHost* render_frame_host,
                                    const std::string& json_string) {}

  virtual void OnCookiesAccessed(RenderFrameHostImpl* render_frame_host,
                                 const CookieAccessDetails& details) {}

  // Notified that the renderer responded after calling GetSavableResourceLinks.
  virtual void SavableResourceLinksResponse(
      RenderFrameHostImpl* source,
      const std::vector<GURL>& resources_list,
      blink::mojom::ReferrerPtr referrer,
      const std::vector<blink::mojom::SavableSubframePtr>& subframes) {}

  // Notified that the renderer returned an error after calling
  // GetSavableResourceLinks in case the frame contains non-savable content
  // (i.e. from a non-savable scheme) or if there were errors gathering the
  // links.
  virtual void SavableResourceLinksError(RenderFrameHostImpl* source) {}

  // Called when |RenderFrameHost::GetLifecycleState()| changes i.e.,
  // when RenderFrameHost LifecycleState changes from old_state to
  // new_state.
  virtual void RenderFrameHostStateChanged(
      RenderFrameHost* host,
      RenderFrameHost::LifecycleState old_state,
      RenderFrameHost::LifecycleState new_state) {}

  // The page is trying to move the main frame's representation in the client.
  virtual void SetWindowRect(const gfx::Rect& new_bounds) {}

  // The page's preferred size changed.
  virtual void UpdateWindowPreferredSize(const gfx::Size& pref_size) {}

  // Returns the list of top-level RenderFrameHosts hosting active documents
  // that belong to the same browsing context group as |render_frame_host|.
  virtual std::vector<RenderFrameHostImpl*>
  GetActiveTopLevelDocumentsInBrowsingContextGroup(
      RenderFrameHostImpl* render_frame_host);

  // Returns the PrerenderHostRegistry to start/cancel prerendering. This
  // doesn't return nullptr except for some tests. This should only be called
  // when blink::features::IsPrerender2Enabled() is true.
  virtual PrerenderHostRegistry* GetPrerenderHostRegistry();

#if BUILDFLAG(ENABLE_PLUGINS)
  virtual void OnPepperInstanceCreated(RenderFrameHostImpl* source,
                                       int32_t pp_instance) {}
  virtual void OnPepperInstanceDeleted(RenderFrameHostImpl* source,
                                       int32_t pp_instance) {}
  virtual void OnPepperStartsPlayback(RenderFrameHostImpl* source,
                                      int32_t pp_instance) {}
  virtual void OnPepperStopsPlayback(RenderFrameHostImpl* source,
                                     int32_t pp_instance) {}
  virtual void OnPepperPluginCrashed(RenderFrameHostImpl* source,
                                     const base::FilePath& plugin_path,
                                     base::ProcessId plugin_pid) {}
  virtual void OnPepperPluginHung(RenderFrameHostImpl* source,
                                  int plugin_child_id,
                                  const base::FilePath& path,
                                  bool is_hung) {}
#endif

 protected:
  virtual ~RenderFrameHostDelegate() = default;
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_RENDER_FRAME_HOST_DELEGATE_H_
