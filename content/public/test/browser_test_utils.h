// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_TEST_BROWSER_TEST_UTILS_H_
#define CONTENT_PUBLIC_TEST_BROWSER_TEST_UTILS_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/containers/flat_set.h"
#include "base/containers/queue.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/template_util.h"
#include "base/test/metrics/histogram_tester.h"
#include "build/build_config.h"
#include "cc/test/pixel_test_utils.h"
#include "components/viz/common/quads/compositor_frame.h"
#include "content/public/browser/browser_message_filter.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/render_frame_metadata_provider.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_process_host_observer.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/common/page_type.h"
#include "content/public/test/fake_frame_widget.h"
#include "ipc/message_filter.h"
#include "net/base/load_flags.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key_collection.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "storage/common/file_system/file_system_types.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/context_menu_data/untrustworthy_context_menu_params.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "third_party/blink/public/mojom/frame/frame.mojom-test-utils.h"
#include "third_party/blink/public/mojom/frame/user_activation_update_types.mojom.h"
#include "third_party/perfetto/include/perfetto/tracing/traced_value_forward.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/display/display_switches.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "url/gurl.h"

#if defined(OS_WIN)
#include "base/win/scoped_handle.h"
#endif

namespace gfx {
class Point;
}  // namespace gfx

namespace net {
class CanonicalCookie;
namespace test_server {
class EmbeddedTestServer;
}  // namespace test_server

// TODO(svaldez): Remove typedef once EmbeddedTestServer has been migrated
// out of net::test_server.
using test_server::EmbeddedTestServer;
}  // namespace net

namespace ui {
class AXPlatformNodeDelegate;
class AXTreeID;
}  // namespace ui

#if defined(OS_WIN)
namespace Microsoft {
namespace WRL {
template <typename>
class ComPtr;
}  // namespace WRL
}  // namespace Microsoft

typedef int PROPERTYID;
#endif

// A collections of functions designed for use with content_browsertests and
// browser_tests.
// TO BE CLEAR: any function here must work against both binaries. If it only
// works with browser_tests, it should be in chrome\test\base\ui_test_utils.h.
// If it only works with content_browsertests, it should be in
// content\test\content_browser_test_utils.h.

namespace blink {
class StorageKey;
struct FrameVisualProperties;
}  // namespace blink

namespace content {

class BoundingBoxUpdateWaiterImpl;
class BrowserContext;
class FileSystemAccessPermissionContext;
class FrameTreeNode;
class NavigationHandle;
class NavigationRequest;
class RenderFrameHostImpl;
class RenderFrameMetadataProviderImpl;
class RenderFrameProxyHost;
class RenderWidgetHost;
class RenderWidgetHostView;
class ScopedAllowRendererCrashes;
class ToRenderFrameHost;
class WebContents;

// Navigates |web_contents| to |url|, blocking until the navigation finishes.
// Returns true if the page was loaded successfully and the last committed URL
// matches |url|.  This is a browser-initiated navigation that simulates a user
// typing |url| into the address bar.
WARN_UNUSED_RESULT bool NavigateToURL(WebContents* web_contents,
                                      const GURL& url);

// Same as above, but takes in an additional URL, |expected_commit_url|, to
// which the navigation should eventually commit.  This is useful for cases
// like redirects, where navigation starts on one URL but ends up committing a
// different URL.  This function will return true if navigating to |url|
// results in a successful commit to |expected_commit_url|.
WARN_UNUSED_RESULT bool NavigateToURL(WebContents* web_contents,
                                      const GURL& url,
                                      const GURL& expected_commit_url);

// Navigates |web_contents| to |url|, blocking until the given number of
// navigations finishes. If |ignore_uncommitted_navigations| is true, then an
// aborted navigation also counts toward |number_of_navigations| being complete.
void NavigateToURLBlockUntilNavigationsComplete(
    WebContents* web_contents,
    const GURL& url,
    int number_of_navigations,
    bool ignore_uncommitted_navigations = true);

// Perform a renderer-initiated navigation of |window| to |url|, blocking
// until the navigation finishes.  The navigation is done by assigning
// location.href in the frame |adapter|. Returns true if the page was loaded
// successfully and the last committed URL matches |url|.
WARN_UNUSED_RESULT bool NavigateToURLFromRenderer(
    const ToRenderFrameHost& adapter,
    const GURL& url);
// Similar to above but takes in an additional URL, |expected_commit_url|, to
// which the navigation should eventually commit. (See the browser-initiated
// counterpart for more details).
WARN_UNUSED_RESULT bool NavigateToURLFromRenderer(
    const ToRenderFrameHost& adapter,
    const GURL& url,
    const GURL& expected_commit_url);
WARN_UNUSED_RESULT bool NavigateToURLFromRendererWithoutUserGesture(
    const ToRenderFrameHost& adapter,
    const GURL& url);

// Perform a renderer-initiated navigation of |window| to |url|. Unlike the
// previous set of helpers, does not block. The navigation is done by assigning
// location.href in the frame |adapter|. Returns the result of executing the IPC
// to evaluate the JS that assigns location.href.
WARN_UNUSED_RESULT bool BeginNavigateToURLFromRenderer(
    const ToRenderFrameHost& adapter,
    const GURL& url);

// Navigate a frame with ID |iframe_id| to |url|, blocking until the navigation
// finishes.  Uses a renderer-initiated navigation from script code in the
// main frame.
//
// This method does not trigger a user activation before the navigation.  If
// necessary, a user activation can be triggered right before calling this
// method, e.g. by calling |ExecuteScript(frame_tree_node, "")|.
bool NavigateIframeToURL(WebContents* web_contents,
                         const std::string& iframe_id,
                         const GURL& url);

// Similar to |NavigateIframeToURL()| but returns as soon as the navigation is
// initiated.
bool BeginNavigateIframeToURL(WebContents* web_contents,
                              const std::string& iframe_id,
                              const GURL& url);

// Generate a URL for a file path including a query string.
GURL GetFileUrlWithQuery(const base::FilePath& path,
                         const std::string& query_string);

// Checks whether the page type of the last committed navigation entry matches
// |page_type|.
bool IsLastCommittedEntryOfPageType(WebContents* web_contents,
                                    content::PageType page_type);

// Waits for |web_contents| to stop loading.  If |web_contents| is not loading
// returns immediately.  Tests should use WaitForLoadStop instead and check that
// last navigation succeeds, and this function should only be used if the
// navigation leads to web_contents being destroyed.
void WaitForLoadStopWithoutSuccessCheck(WebContents* web_contents);

// Waits for |web_contents| to stop loading.  If |web_contents| is not loading
// returns immediately.  Returns true if the last navigation succeeded (resulted
// in a committed navigation entry of type PAGE_TYPE_NORMAL).
// TODO(alexmos): tests that use this function to wait for successful
// navigations should be refactored to do EXPECT_TRUE(WaitForLoadStop()).
bool WaitForLoadStop(WebContents* web_contents);

// If a test uses a beforeunload dialog, it must be prepared to avoid flakes.
// This function collects everything that needs to be done, except for user
// activation which is triggered only when |trigger_user_activation| is true.
// Note that beforeunload dialog attempts are ignored unless the frame has
// received a user activation.
void PrepContentsForBeforeUnloadTest(WebContents* web_contents,
                                     bool trigger_user_activation = true);

#if defined(USE_AURA) || defined(OS_ANDROID)
// If WebContent's view is currently being resized, this will wait for the ack
// from the renderer that the resize is complete and for the
// WindowEventDispatcher to release the pointer moves. If there's no resize in
// progress, the method will return right away.
void WaitForResizeComplete(WebContents* web_contents);
#endif  // defined(USE_AURA) || defined(OS_ANDROID)

// Allows tests to set the last committed origin of |render_frame_host|, to
// simulate a scenario that might happen with a compromised renderer or might
// not otherwise be possible.
void OverrideLastCommittedOrigin(RenderFrameHost* render_frame_host,
                                 const url::Origin& origin);

// Causes the specified web_contents to crash. Blocks until it is crashed.
void CrashTab(WebContents* web_contents);

// Sets up a commit interceptor to alter commits for |target_url| to change
// their commit URL to |new_url| and origin to |new_origin|. This will happen
// for all commits in |web_contents|.
void PwnCommitIPC(WebContents* web_contents,
                  const GURL& target_url,
                  const GURL& new_url,
                  const url::Origin& new_origin);

// Causes the specified web_contents to issue an OnUnresponsiveRenderer event
// to its observers.
void SimulateUnresponsiveRenderer(WebContents* web_contents,
                                  RenderWidgetHost* widget);

// Simulates clicking at the center of the given tab asynchronously; modifiers
// may contain bits from WebInputEvent::Modifiers. Sends the event through
// RenderWidgetHostInputEventRouter and thus can target OOPIFs.
void SimulateMouseClick(WebContents* web_contents,
                        int modifiers,
                        blink::WebMouseEvent::Button button);

// Simulates clicking at the point |point| of the given tab asynchronously;
// modifiers may contain bits from WebInputEvent::Modifiers. Sends the event
// through RenderWidgetHostInputEventRouter and thus can target OOPIFs.
void SimulateMouseClickAt(WebContents* web_contents,
                          int modifiers,
                          blink::WebMouseEvent::Button button,
                          const gfx::Point& point);

// Retrieves the center coordinates of the element with id |id|.
// ATTENTION: When using these coordinates to simulate a click or tap make sure
// that the viewport is not zoomed as the coordinates returned by this method
// are relative to the page not the viewport. In particular for Android make
// sure the page has the meta tag
// <meta name="viewport" content="width=device-width,minimum-scale=1">
// TODO(https://crbug.com/1199644): Make the Simulate* methods more user
// friendly by taking zooming into account.
gfx::PointF GetCenterCoordinatesOfElementWithId(
    content::WebContents* web_contents,
    const std::string& id);

// Retrieves the center coordinates of the element with id |id| and simulates a
// mouse click there using SimulateMouseClickAt().
void SimulateMouseClickOrTapElementWithId(content::WebContents* web_contents,
                                          const std::string& id);

// Simulates asynchronously a mouse enter/move/leave event. The mouse event is
// routed through RenderWidgetHostInputEventRouter and thus can target OOPIFs.
void SimulateMouseEvent(WebContents* web_contents,
                        blink::WebInputEvent::Type type,
                        const gfx::Point& point);
void SimulateMouseEvent(WebContents* web_contents,
                        blink::WebInputEvent::Type type,
                        blink::WebMouseEvent::Button button,
                        const gfx::Point& point);

// Simulate a mouse wheel event.
void SimulateMouseWheelEvent(WebContents* web_contents,
                             const gfx::Point& point,
                             const gfx::Vector2d& delta,
                             const blink::WebMouseWheelEvent::Phase phase);

#if !defined(OS_MAC)
// Simulate a mouse wheel event with the ctrl modifier set.
void SimulateMouseWheelCtrlZoomEvent(WebContents* web_contents,
                                     const gfx::Point& point,
                                     bool zoom_in,
                                     blink::WebMouseWheelEvent::Phase phase);

void SimulateTouchscreenPinch(WebContents* web_contents,
                              const gfx::PointF& anchor,
                              float scale_change,
                              base::OnceClosure on_complete);

#endif  // !defined(OS_MAC)

// Sends a GesturePinch Begin/Update/End sequence.
void SimulateGesturePinchSequence(WebContents* web_contents,
                                  const gfx::Point& point,
                                  float scale,
                                  blink::WebGestureDevice source_device);

// Sends a simple, three-event (Begin/Update/End) gesture scroll.
void SimulateGestureScrollSequence(WebContents* web_contents,
                                   const gfx::Point& point,
                                   const gfx::Vector2dF& delta);

void SimulateGestureEvent(WebContents* web_contents,
                          const blink::WebGestureEvent& gesture_event,
                          const ui::LatencyInfo& latency);

// Taps the screen at |point|, using gesture Tap or TapDown.
void SimulateTapAt(WebContents* web_contents, const gfx::Point& point);
void SimulateTapDownAt(WebContents* web_contents, const gfx::Point& point);

// A helper function for SimulateTap(Down)At.
void SimulateTouchGestureAt(WebContents* web_contents,
                            const gfx::Point& point,
                            blink::WebInputEvent::Type type);

#if defined(USE_AURA)
// Generates a TouchEvent of |event_type| at |point|.
void SimulateTouchEventAt(WebContents* web_contents,
                          ui::EventType event_type,
                          const gfx::Point& point);

void SimulateLongTapAt(WebContents* web_contents, const gfx::Point& point);

// Can be used to wait for updates to the bounding box (i.e. the rectangle
// enclosing the selection region) associated with `web_contents`.
class BoundingBoxUpdateWaiter {
 public:
  explicit BoundingBoxUpdateWaiter(WebContents* web_contents);
  BoundingBoxUpdateWaiter(const BoundingBoxUpdateWaiter&) = delete;
  BoundingBoxUpdateWaiter& operator=(const BoundingBoxUpdateWaiter&) = delete;
  ~BoundingBoxUpdateWaiter();

  void Wait();

 private:
  std::unique_ptr<BoundingBoxUpdateWaiterImpl> impl_;
};
#endif

// Taps the screen with modifires at |point|.
void SimulateTapWithModifiersAt(WebContents* web_contents,
                                unsigned Modifiers,
                                const gfx::Point& point);

// Sends a key press asynchronously.
// |key| specifies the UIEvents (aka: DOM4Events) value of the key.
// |code| specifies the UIEvents (aka: DOM4Events) value of the physical key.
// |key_code| alone is good enough for scenarios that only need the char
// value represented by a key event and not the physical key on the keyboard
// or the keyboard layout.
// If set to true, the modifiers |control|, |shift|, |alt|, and |command| are
// pressed down first before the key event, and released after.
void SimulateKeyPress(WebContents* web_contents,
                      ui::DomKey key,
                      ui::DomCode code,
                      ui::KeyboardCode key_code,
                      bool control,
                      bool shift,
                      bool alt,
                      bool command);

// Like SimulateKeyPress(), but does not send the char (AKA keypress) event.
// This is useful for arrow keys and other key presses that do not generate
// characters.
void SimulateKeyPressWithoutChar(WebContents* web_contents,
                                 ui::DomKey key,
                                 ui::DomCode code,
                                 ui::KeyboardCode key_code,
                                 bool control,
                                 bool shift,
                                 bool alt,
                                 bool command);

// Reset touch action for the embedder of a BrowserPluginGuest.
void ResetTouchAction(RenderWidgetHost* host);

// Spins a run loop until effects of previously forwarded input are fully
// realized.
void RunUntilInputProcessed(RenderWidgetHost* host);

// Returns a string representation of a given |referrer_policy|. This is used to
// setup <meta name=referrer> tags in documents used for referrer-policy-based
// tests. The value `no-meta` indicates no tag should be created.
std::string ReferrerPolicyToString(
    network::mojom::ReferrerPolicy referrer_policy);

// For testing, bind FakeFrameWidget to a RenderWidgetHost associated
// with a given RenderFrameHost
mojo::PendingAssociatedReceiver<blink::mojom::FrameWidget>
BindFakeFrameWidgetInterfaces(RenderFrameHost* frame);

// Set |active| state for a RenderWidgetHost associated with a given
// RenderFrameHost
void SimulateActiveStateForWidget(RenderFrameHost* frame, bool active);

// Holds down modifier keys for the duration of its lifetime and releases them
// upon destruction. This allows simulating multiple input events without
// simulating modifier key releases in between.
class ScopedSimulateModifierKeyPress {
 public:
  ScopedSimulateModifierKeyPress(WebContents* web_contents,
                                 bool control,
                                 bool shift,
                                 bool alt,
                                 bool command);

  ScopedSimulateModifierKeyPress(const ScopedSimulateModifierKeyPress&) =
      delete;
  ScopedSimulateModifierKeyPress& operator=(
      const ScopedSimulateModifierKeyPress&) = delete;

  ~ScopedSimulateModifierKeyPress();

  // Similar to SimulateMouseClickAt().
  void MouseClickAt(int additional_modifiers,
                    blink::WebMouseEvent::Button button,
                    const gfx::Point& point);

  // Similar to SimulateKeyPress().
  void KeyPress(ui::DomKey key, ui::DomCode code, ui::KeyboardCode key_code);

  // Similar to SimulateKeyPressWithoutChar().
  void KeyPressWithoutChar(ui::DomKey key,
                           ui::DomCode code,
                           ui::KeyboardCode key_code);

 private:
  const raw_ptr<WebContents> web_contents_;
  int modifiers_;
  const bool control_;
  const bool shift_;
  const bool alt_;
  const bool command_;
};

// Method to check what devices we have on the system.
bool IsWebcamAvailableOnSystem(WebContents* web_contents);

// Allow ExecuteScript* methods to target either a WebContents or a
// RenderFrameHost.  Targeting a WebContents means executing the script in the
// RenderFrameHost returned by WebContents::GetMainFrame(), which is the main
// frame.  Pass a specific RenderFrameHost to target it. Embedders may declare
// additional ConvertToRenderFrameHost functions for convenience.
class ToRenderFrameHost {
 public:
  template <typename T>
  ToRenderFrameHost(T* frame_convertible_value)
      : render_frame_host_(ConvertToRenderFrameHost(frame_convertible_value)) {}

  // Extract the underlying frame.
  RenderFrameHost* render_frame_host() const { return render_frame_host_; }

 private:
  RenderFrameHost* render_frame_host_;
};

RenderFrameHost* ConvertToRenderFrameHost(RenderFrameHost* render_view_host);
RenderFrameHost* ConvertToRenderFrameHost(WebContents* web_contents);

// Deprecated: in new code, prefer ExecJs() -- it works the same, but has
// better error handling. (Note: still use ExecuteScript() on pages with a
// Content Security Policy).
//
// Executes the passed |script| in the specified frame with the user gesture.
//
// Appends |domAutomationController.send(...)| to the end of |script| and waits
// until the response comes back (pumping the message loop while waiting).  The
// |script| itself should not invoke domAutomationController.send(); if you want
// to call domAutomationController.send(...) yourself and extract the result,
// then use one of ExecuteScriptAndExtract... functions).
//
// Returns true on success (if the renderer responded back with the expected
// value).  Returns false otherwise (e.g. if the script threw an exception
// before calling the appended |domAutomationController.send(...)|, or if the
// renderer died or if the renderer called |domAutomationController.send(...)|
// with a malformed or unexpected value).
//
// See also:
// - ExecJs (preferred replacement with better error handling)
// - EvalJs (if you want to retrieve a value)
// - ExecuteScriptAsync (if you don't want to block for |script| completion)
// - DOMMessageQueue (to manually wait for domAutomationController.send(...))
bool ExecuteScript(const ToRenderFrameHost& adapter,
                   const std::string& script) WARN_UNUSED_RESULT;

// Same as content::ExecuteScript but doesn't send a user gesture to the
// renderer.
bool ExecuteScriptWithoutUserGesture(const ToRenderFrameHost& adapter,
                                     const std::string& script)
    WARN_UNUSED_RESULT;

// Similar to ExecuteScript above, but
// - Doesn't modify the |script|.
// - Kicks off execution of the |script| in the specified frame and returns
//   immediately (without waiting for a response from the renderer and/or
//   without checking that the script succeeded).
void ExecuteScriptAsync(const ToRenderFrameHost& adapter,
                        const std::string& script);

// The following methods execute the passed |script| in the specified frame and
// sets |result| to the value passed to "window.domAutomationController.send" by
// the executed script. They return true on success, false if the script
// execution failed or did not evaluate to the expected type.
//
// Deprecated: Use EvalJs().
bool ExecuteScriptAndExtractDouble(const ToRenderFrameHost& adapter,
                                   const std::string& script,
                                   double* result) WARN_UNUSED_RESULT;
bool ExecuteScriptAndExtractInt(const ToRenderFrameHost& adapter,
                                const std::string& script,
                                int* result) WARN_UNUSED_RESULT;
bool ExecuteScriptAndExtractBool(const ToRenderFrameHost& adapter,
                                 const std::string& script,
                                 bool* result) WARN_UNUSED_RESULT;
bool ExecuteScriptAndExtractString(const ToRenderFrameHost& adapter,
                                   const std::string& script,
                                   std::string* result) WARN_UNUSED_RESULT;

// Same as above but the script executed without user gesture.
bool ExecuteScriptWithoutUserGestureAndExtractBool(
    const ToRenderFrameHost& adapter,
    const std::string& script,
    bool* result) WARN_UNUSED_RESULT;
bool ExecuteScriptWithoutUserGestureAndExtractString(
    const ToRenderFrameHost& adapter,
    const std::string& script,
    std::string* result) WARN_UNUSED_RESULT;

// JsLiteralHelper is a helper class that determines what types are legal to
// pass to StringifyJsLiteral. Legal types include int, string, StringPiece,
// char*, bool, double, GURL, url::Origin, and base::Value&&.
template <typename T>
struct JsLiteralHelper {
  // This generic version enables passing any type from which base::Value can be
  // instantiated. This covers int, string, double, bool, base::Value&&, etc.
  template <typename U>
  static base::Value Convert(U&& arg) {
    return base::Value(std::forward<U>(arg));
  }

  static base::Value Convert(const base::Value& value) {
    return value.Clone();
  }

  static base::Value Convert(const base::ListValue& value) {
    return value.Clone();
  }
};

// Specialization allowing GURL to be passed to StringifyJsLiteral.
template <>
struct JsLiteralHelper<GURL> {
  static base::Value Convert(const GURL& url) {
    return base::Value(url.spec());
  }
};

// Specialization allowing url::Origin to be passed to StringifyJsLiteral.
template <>
struct JsLiteralHelper<url::Origin> {
  static base::Value Convert(const url::Origin& url) {
    return base::Value(url.Serialize());
  }
};

// Helper for variadic ListValueOf() -- zero-argument base case.
inline void ConvertToBaseValueList(base::Value::ListStorage* list) {}

// Helper for variadic ListValueOf() -- case with at least one argument.
//
// |first| can be any type explicitly convertible to base::Value
// (including int/string/StringPiece/char*/double/bool), or any type that
// JsLiteralHelper is specialized for -- like URL and url::Origin, which emit
// string literals.
template <typename T, typename... Args>
void ConvertToBaseValueList(base::Value::ListStorage* list,
                            T&& first,
                            Args&&... rest) {
  using ValueType = base::remove_cvref_t<T>;
  list->push_back(JsLiteralHelper<ValueType>::Convert(std::forward<T>(first)));
  ConvertToBaseValueList(list, std::forward<Args>(rest)...);
}

// Construct a list-type base::Value from a mix of arguments.
//
// Each |arg| can be any type explicitly convertible to base::Value
// (including int/string/StringPiece/char*/double/bool), or any type that
// JsLiteralHelper is specialized for -- like URL and url::Origin, which emit
// string literals. |args| can be a mix of different types.
template <typename... Args>
base::ListValue ListValueOf(Args&&... args) {
  base::Value::ListStorage values;
  ConvertToBaseValueList(&values, std::forward<Args>(args)...);
  return base::ListValue(std::move(values));
}

// Replaces $1, $2, $3, etc in |script_template| with JS literal values
// constructed from |args|, similar to base::ReplaceStringPlaceholders.
//
// Unlike StringPrintf or manual concatenation, this version will properly
// escape string content, even if it contains slashes or quotation marks.
//
// Each |arg| can be any type explicitly convertible to base::Value
// (including int/string/StringPiece/char*/double/bool), or any type that
// JsLiteralHelper is specialized for -- like URL and url::Origin, which emit
// string literals. |args| can be a mix of different types.
//
// Example 1:
//
//   GURL page_url("http://example.com");
//   EXPECT_TRUE(ExecuteScript(
//       shell(), JsReplace("window.open($1, '_blank');", page_url)));
//
// $1 is replaced with a double-quoted JS string literal:
// "http://example.com". Note that quotes around $1 are not required.
//
// Example 2:
//
//   bool forced_reload = true;
//   EXPECT_TRUE(ExecuteScript(
//       shell(), JsReplace("window.location.reload($1);", forced_reload)));
//
// This becomes "window.location.reload(true);" -- because bool values are
// supported by base::Value. Numbers, lists, and dicts also work.
template <typename... Args>
std::string JsReplace(base::StringPiece script_template, Args&&... args) {
  base::Value::ListStorage values;
  ConvertToBaseValueList(&values, std::forward<Args>(args)...);
  std::vector<std::string> replacements(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    CHECK(base::JSONWriter::Write(values[i], &replacements[i]));
  }
  return base::ReplaceStringPlaceholders(script_template, replacements,
                                         nullptr);
}

// The return value of EvalJs. Captures the value (or the error) arising from
// script execution. When used with gtest assertions, EvalJsResult generally
// behaves like its wrapped value.
//
// An EvalJsResult can be consumed in two ways:
//
//  (1) [preferred] Pass it directly to an EXPECT_EQ() macro. It has
//      overloaded operator== against std::string, bool, int, double,
//      nullptr_t, and base::Value. This will produce readable assertion
//      failures if there is a type mismatch, or if an exception was thrown --
//      errors are never equal to anything.
//
//      For boolean results, note that EXPECT_TRUE(..) and EXPECT_FALSE()
//      won't compile; use EXPECT_EQ(true, ...) instead. This is intentional,
//      since EXPECT_TRUE() could be read ambiguously as either "expect
//      successful execution", "expect truthy value of any type", or "expect
//      boolean value 'true'".
//
//  (2) [use when necessary] Extract the underlying value of an expected type,
//      by calling ExtractString(), ExtractInt(), etc. This will produce a
//      CHECK failure if the execution didn't result in the appropriate type
//      of result, or if an exception was thrown.
struct EvalJsResult {
  const base::Value value;  // Value; if things went well.
  const std::string error;  // Error; if things went badly.

  // Creates an ExecuteScript result. If |error| is non-empty, |value| will be
  // ignored.
  EvalJsResult(base::Value value, const std::string& error);

  // Copy ctor.
  EvalJsResult(const EvalJsResult& value);

  // Extract a result value of the requested type, or die trying.
  //
  // If there was an error, or if returned value is of a different type, these
  // will fail with a CHECK. Use Extract methods only when accessing the
  // result value is necessary; prefer operator== and EXPECT_EQ() instead:
  // they don't CHECK, and give better error messages.
  const std::string& ExtractString() const WARN_UNUSED_RESULT;
  int ExtractInt() const WARN_UNUSED_RESULT;
  bool ExtractBool() const WARN_UNUSED_RESULT;
  double ExtractDouble() const WARN_UNUSED_RESULT;
  base::ListValue ExtractList() const WARN_UNUSED_RESULT;
};

// Enables EvalJsResult to be used directly in ASSERT/EXPECT macros:
//
//    ASSERT_EQ("ab", EvalJs(rfh, "'a' + 'b'"))
//    ASSERT_EQ(2, EvalJs(rfh, "1 + 1"))
//    ASSERT_EQ(nullptr, EvalJs(rfh, "var a = 1 + 1"))
//
// Error values never return true for any comparison operator.
template <typename T>
bool operator==(const T& a, const EvalJsResult& b) {
  return b.error.empty() && (JsLiteralHelper<T>::Convert(a) == b.value);
}

template <typename T>
bool operator!=(const T& a, const EvalJsResult& b) {
  return b.error.empty() && (JsLiteralHelper<T>::Convert(a) != b.value);
}

template <typename T>
bool operator>=(const T& a, const EvalJsResult& b) {
  return b.error.empty() && (JsLiteralHelper<T>::Convert(a) >= b.value);
}

template <typename T>
bool operator<=(const T& a, const EvalJsResult& b) {
  return b.error.empty() && (JsLiteralHelper<T>::Convert(a) <= b.value);
}

template <typename T>
bool operator<(const T& a, const EvalJsResult& b) {
  return b.error.empty() && (JsLiteralHelper<T>::Convert(a) < b.value);
}

template <typename T>
bool operator>(const T& a, const EvalJsResult& b) {
  return b.error.empty() && (JsLiteralHelper<T>::Convert(a) > b.value);
}

inline bool operator==(std::nullptr_t a, const EvalJsResult& b) {
  return b.error.empty() && (base::Value() == b.value);
}

// Provides informative failure messages when the result of EvalJs() is
// used in a failing ASSERT_EQ or EXPECT_EQ.
void PrintTo(const EvalJsResult& bar, ::std::ostream* os);

enum EvalJsOptions {
  EXECUTE_SCRIPT_DEFAULT_OPTIONS = 0,

  // By default, EvalJs runs with a user gesture. This bit flag disables
  // that.
  EXECUTE_SCRIPT_NO_USER_GESTURE = (1 << 0),

  // This bit controls how the result is obtained. By default, EvalJs's runner
  // script will call domAutomationController.send() with the completion
  // value. Setting this bit will disable that, requiring |script| to provide
  // its own call to domAutomationController.send() instead.
  EXECUTE_SCRIPT_USE_MANUAL_REPLY = (1 << 1),

  // By default, when the script passed to EvalJs evaluates to a Promise, the
  // execution continues until the Promise resolves, and the resolved value is
  // returned. Setting this bit disables such Promise resolution.
  EXECUTE_SCRIPT_NO_RESOLVE_PROMISES = (1 << 2),
};

// EvalJs() -- run |script| in |execution_target| and return its value or error.
//
// Example simple usage:
//
//   EXPECT_EQ("https://abcd.com", EvalJs(render_frame_host, "self.origin"));
//   EXPECT_EQ(5, EvalJs(render_frame_host, "history.length"));
//   EXPECT_EQ(false, EvalJs(render_frame_host, "history.length > 5"));
//
// The result value of |script| is its "statement completion value" -- the same
// semantics used by Javascript's own eval() function. If |script|
// raises exceptions, or is syntactically invalid, an error is captured instead,
// including a full stack trace.
//
// The return value of EvalJs() may be used directly in EXPECT_EQ()
// macros, and compared for against std::string, int, or any other type for
// which base::Value has a constructor.  If an error was thrown by the script,
// any comparison operators will always return false.
//
// If |script|'s captured completion value is a Promise, this function blocks
// until the Promise is resolved. This enables a usage pattern where |script|
// may call an async function, and use the await keyword to wait for
// events to fire. For example:
//
//   EXPECT_EQ(200, EvalJs(rfh, "(async () => { var resp = (await fetch(url));"
//                              "               return resp.status; })()");
//
// In the above example, the immediately-invoked function expression results in
// a Promise (that's what async functions do); EvalJs will continue blocking
// until the Promise resolves, which happens when the async function returns
// the HTTP status code -- which is expected, in this case, to be 200.
//
// Quick migration guide for users of the classic ExecuteScriptAndExtract*():
//  - If your page has a Content SecurityPolicy, don't migrate [yet]; CSP can
//    interfere with the internal mechanism used here.
//  - Get rid of the out-param. You call EvalJs no matter what your return
//    type is.
//  - If possible, pass the result of EvalJs() into the second argument of an
//    EXPECT_EQ macro. This will trigger failure (and a nice message) if an
//    error occurs.
//  - Eliminate calls to domAutomationController.send() in |script|. In simple
//    cases, |script| is just an expression you want the value of.
//  - When a script previously installed a callback or event listener that
//    invoked domAutomationController.send(x) asynchronously, there is a choice:
//     * Preferred, but more rewriting: Use EvalJs with a Promise which
//       resolves to the value you previously passed to send().
//     * Less rewriting of |script|, but with some drawbacks: Use
//       EXECUTE_SCRIPT_USE_MANUAL_REPLY in |options|. When specified, this
//       means that |script| must continue to call
//       domAutomationController.send(). Note that this option option disables
//       some error-catching safeguards, but you still get the benefit of having
//       an EvalJsResult that can be passed to EXPECT.
//
// Why prefer EvalJs over ExecuteScriptAndExtractString(), etc? Because:
//
//  - It's one function, that does everything, and more succinctly.
//  - Can be used directly in EXPECT_EQ macros (no out- param pointers like
//    ExecuteScriptAndExtractBool()) -- no temporary variable is required,
//    usually resulting in fewer lines of code.
//  - JS exceptions are reliably captured and will appear as C++ assertion
//    failures.
//  - JS stack traces arising from exceptions are annotated with the
//    corresponding source code; this also appears in C++ assertion failures.
//  - Delayed response is supported via Promises and JS async/await.
//  - |script| doesn't need to call domAutomationController.send directly.
//  - When a script doesn't produce a result, it's likely an assertion
//    failure rather than a hang.  Doesn't get confused by crosstalk with
//    other callers of domAutomationController.send() -- script results carry
//    a GUID.
//  - Lists, dicts, null values, etc. can be returned as base::Values.
//
// It is guaranteed that EvalJs works even when the target frame is frozen.
EvalJsResult EvalJs(const ToRenderFrameHost& execution_target,
                    const std::string& script,
                    int options = EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                    int32_t world_id = ISOLATED_WORLD_ID_GLOBAL)
    WARN_UNUSED_RESULT;

// Like EvalJs(), but runs |raf_script| inside a requestAnimationFrame handler,
// and runs |script| after the rendering update has completed. By the time
// this method returns, any IPCs sent from the renderer process to the browser
// process during the lifecycle update should already have been received and
// processed by the browser.
EvalJsResult EvalJsAfterLifecycleUpdate(
    const ToRenderFrameHost& execution_target,
    const std::string& raf_script,
    const std::string& script,
    int options = EXECUTE_SCRIPT_DEFAULT_OPTIONS,
    int32_t world_id = ISOLATED_WORLD_ID_GLOBAL) WARN_UNUSED_RESULT;

// Run a script exactly the same as EvalJs(), but ignore the resulting value.
//
// Returns AssertionSuccess() if |script| ran successfully, and
// AssertionFailure() if |script| contained a syntax error or threw an
// exception.
//
// As with EvalJs(), if the script passed evaluates to a Promise, this waits
// until it resolves.
//
// Unlike ExecuteScript(), this catches syntax errors and uncaught exceptions,
// and gives more useful error messages when things go wrong. Prefer ExecJs to
// ExecuteScript(), unless your page has a CSP.
::testing::AssertionResult ExecJs(const ToRenderFrameHost& execution_target,
                                  const std::string& script,
                                  int options = EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                                  int32_t world_id = ISOLATED_WORLD_ID_GLOBAL)
    WARN_UNUSED_RESULT;

// Walks the frame tree of the specified `page`, also descending into any inner
// frame-trees (e.g. GuestView), and returns the sole frame that matches the
// specified predicate function. This function will DCHECK if no frames match
// the specified predicate, or if more than one frame matches.
RenderFrameHost* FrameMatchingPredicate(
    Page& page,
    base::RepeatingCallback<bool(RenderFrameHost*)> predicate);

// Predicates for use with FrameMatchingPredicate.
bool FrameMatchesName(const std::string& name, RenderFrameHost* frame);
bool FrameIsChildOfMainFrame(RenderFrameHost* frame);
bool FrameHasSourceUrl(const GURL& url, RenderFrameHost* frame);

// Finds the child frame at the specified |index| for |adapter| and returns its
// RenderFrameHost.  Returns nullptr if such child frame does not exist.
RenderFrameHost* ChildFrameAt(const ToRenderFrameHost& adapter, size_t index);

// Returns true if |frame| has origin-keyed process isolation due to the
// OriginAgentCluster header.
bool HasOriginKeyedProcess(RenderFrameHost* frame);

// Returns the frames visited by |RenderFrameHost::ForEachRenderFrameHost| in
// the same order.
std::vector<RenderFrameHost*> CollectAllRenderFrameHosts(
    RenderFrameHost* starting_rfh);
// Returns the frames visited by |RenderFrameHost::ForEachRenderFrameHost| on
// |page|'s main document in the same order.
std::vector<RenderFrameHost*> CollectAllRenderFrameHosts(Page& page);
// Returns the frames visited by |WebContents::ForEachRenderFrameHost| in
// the same order.
std::vector<RenderFrameHost*> CollectAllRenderFrameHosts(
    WebContents* web_contents);

// Executes the WebUI resource tests. Injects the test runner script prior to
// executing the tests.
//
// Returns true if tests ran successfully, false otherwise.
bool ExecuteWebUIResourceTest(WebContents* web_contents);

// Returns the serialized cookie string for the given url. Uses an inclusive
// SameSiteCookieContext by default, which gets cookies regardless of their
// SameSite attribute.
std::string GetCookies(
    BrowserContext* browser_context,
    const GURL& url,
    net::CookieOptions::SameSiteCookieContext context =
        net::CookieOptions::SameSiteCookieContext::MakeInclusive(),
    net::CookiePartitionKeyCollection key_collection =
        net::CookiePartitionKeyCollection::ContainsAll());

// Returns the canonical cookies for the given url.
std::vector<net::CanonicalCookie> GetCanonicalCookies(
    BrowserContext* browser_context,
    const GURL& url,
    net::CookiePartitionKeyCollection key_collection =
        net::CookiePartitionKeyCollection::ContainsAll());

// Sets a cookie for the given url. Uses inclusive SameSiteCookieContext and
// SamePartyContext::Type by default, which get cookies regardless of their
// SameSite and SameParty attributes. Returns true on success.
bool SetCookie(BrowserContext* browser_context,
               const GURL& url,
               const std::string& value,
               net::CookieOptions::SameSiteCookieContext context =
                   net::CookieOptions::SameSiteCookieContext::MakeInclusive(),
               net::SamePartyContext::Type party_context =
                   net::SamePartyContext::Type::kSameParty);

// Deletes cookies matching the provided filter. Returns the number of cookies
// that were deleted.
uint32_t DeleteCookies(BrowserContext* browser_context,
                       network::mojom::CookieDeletionFilter filter);

// Fetches the histograms data from other processes.
//
// This function should be called after a child process has logged the
// histogram/metric being tested, to ensure that base::HistogramTester sees all
// the data from the child process.
//
// The caller should ensure that there is no race between 1) the call to
// FetchHistogramsFromChildProcesses() and 2) the child process shutting down.
// See also https://crbug.com/1246137 which describes how this is a test-only
// problem.
void FetchHistogramsFromChildProcesses();

// Registers a request handler which redirects to a different host, based
// on the request path. The format of the path should be
// "/cross-site/hostname/rest/of/path" to redirect the request to
// "<scheme>://hostname:<port>/rest/of/path", where <scheme> and <port>
// are the values for the instance of EmbeddedTestServer.
//
// By default, redirection will be done using HTTP 302 response, but in some
// cases (e.g. to preserve HTTP method and POST body across redirects as
// prescribed by https://tools.ietf.org/html/rfc7231#section-6.4.7) a test might
// want to use HTTP 307 response instead.  This can be accomplished by replacing
// "/cross-site/" URL substring above with "/cross-site-307/".
//
// |embedded_test_server| should not be running when passing it to this function
// because adding the request handler won't be thread safe.
void SetupCrossSiteRedirector(net::EmbeddedTestServer* embedded_test_server);

// Sets the access permission context in FileSystemAccessManagerImpl.
void SetFileSystemAccessPermissionContext(
    BrowserContext* browser_context,
    FileSystemAccessPermissionContext* permission_context);

// Waits until all resources have loaded in the given RenderFrameHost.
// When the load completes, this function sends a "pageLoadComplete" message
// via domAutomationController. The caller should make sure this extra
// message is handled properly.
bool WaitForRenderFrameReady(RenderFrameHost* rfh) WARN_UNUSED_RESULT;

// Enable accessibility support for all of the frames in this WebContents
void EnableAccessibilityForWebContents(WebContents* web_contents);

// Wait until the focused accessible node changes in any WebContents.
void WaitForAccessibilityFocusChange();

// Retrieve information about the node that's focused in the accessibility tree.
ui::AXNodeData GetFocusedAccessibilityNodeInfo(WebContents* web_contents);

// This is intended to be a robust way to assert that the accessibility
// tree eventually gets into the correct state, without worrying about
// the exact ordering of events received while getting there. Blocks
// until any change happens to the accessibility tree.
void WaitForAccessibilityTreeToChange(WebContents* web_contents);

// Searches the accessibility tree to see if any node's accessible name
// is equal to the given name. If not, repeatedly calls
// WaitForAccessibilityTreeToChange, above, and then checks again.
// Keeps looping until the text is found (or the test times out).
void WaitForAccessibilityTreeToContainNodeWithName(WebContents* web_contents,
                                                   const std::string& name);

// Get a snapshot of a web page's accessibility tree.
ui::AXTreeUpdate GetAccessibilityTreeSnapshot(WebContents* web_contents);

// Get a snapshot of an accessibility tree given a `tree_id`.
ui::AXTreeUpdate GetAccessibilityTreeSnapshotFromId(
    const ui::AXTreeID& tree_id);

// Returns the root accessibility node for the given WebContents.
ui::AXPlatformNodeDelegate* GetRootAccessibilityNode(WebContents* web_contents);

// Finds an accessibility node matching the given criteria.
struct FindAccessibilityNodeCriteria {
  FindAccessibilityNodeCriteria();
  ~FindAccessibilityNodeCriteria();
  absl::optional<ax::mojom::Role> role;
  absl::optional<std::string> name;
};
ui::AXPlatformNodeDelegate* FindAccessibilityNode(
    WebContents* web_contents,
    const FindAccessibilityNodeCriteria& criteria);
ui::AXPlatformNodeDelegate* FindAccessibilityNodeInSubtree(
    ui::AXPlatformNodeDelegate* node,
    const FindAccessibilityNodeCriteria& criteria);

#if defined(OS_WIN)
// Retrieve the specified interface from an accessibility node.
template <typename T>
Microsoft::WRL::ComPtr<T> QueryInterfaceFromNode(
    ui::AXPlatformNodeDelegate* node);

// Call GetPropertyValue with the given UIA property id with variant type
// VT_ARRAY | VT_UNKNOWN  on the target browser accessibility node to retrieve
// an array of automation elements, then validate the name property of the
// automation elements with the expected names.
void UiaGetPropertyValueVtArrayVtUnknownValidate(
    PROPERTYID property_id,
    ui::AXPlatformNodeDelegate* target_node,
    const std::vector<std::string>& expected_names);
#endif

// Returns the RenderWidgetHost that holds the keyboard lock.
RenderWidgetHost* GetKeyboardLockWidget(WebContents* web_contents);

// Allows tests to drive keyboard lock functionality without requiring access
// to the RenderWidgetHostImpl header or setting up an HTTP test server.
// |codes| represents the set of keys to lock.  If |codes| has no value, then
// all keys will be considered locked.  If |codes| has a value, then at least
// one key must be specified.
bool RequestKeyboardLock(WebContents* web_contents,
                         absl::optional<base::flat_set<ui::DomCode>> codes);
void CancelKeyboardLock(WebContents* web_contents);

// Returns the screen orientation provider that's been set via
// WebContents::SetScreenOrientationDelegate(). May return null.
ScreenOrientationDelegate* GetScreenOrientationDelegate();

// Returns all the RenderWidgetHostViews inside the |web_contents| that are
// registered in the RenderWidgetHostInputEventRouter.
std::vector<RenderWidgetHostView*> GetInputEventRouterRenderWidgetHostViews(
    WebContents* web_contents);

// Returns the focused RenderWidgetHost.
RenderWidgetHost* GetFocusedRenderWidgetHost(WebContents* web_contents);

// Returns whether or not the RenderWidgetHost thinks it is focused.
bool IsRenderWidgetHostFocused(const RenderWidgetHost*);

// Returns the focused WebContents.
WebContents* GetFocusedWebContents(WebContents* web_contents);

// Watches title changes on a WebContents, blocking until an expected title is
// set.
class TitleWatcher : public WebContentsObserver {
 public:
  // |web_contents| must be non-NULL and needs to stay alive for the
  // entire lifetime of |this|. |expected_title| is the title that |this|
  // will wait for.
  TitleWatcher(WebContents* web_contents, const std::u16string& expected_title);

  TitleWatcher(const TitleWatcher&) = delete;
  TitleWatcher& operator=(const TitleWatcher&) = delete;

  ~TitleWatcher() override;

  // Adds another title to watch for.
  void AlsoWaitForTitle(const std::u16string& expected_title);

  // Waits until the title matches either expected_title or one of the titles
  // added with AlsoWaitForTitle. Returns the value of the most recently
  // observed matching title.
  const std::u16string& WaitAndGetTitle() WARN_UNUSED_RESULT;

 private:
  // Overridden WebContentsObserver methods.
  void DidStopLoading() override;
  void TitleWasSet(NavigationEntry* entry) override;

  void TestTitle();

  std::vector<std::u16string> expected_titles_;
  base::RunLoop run_loop_;

  // The most recently observed expected title, if any.
  std::u16string observed_title_;
};

// Watches a RenderProcessHost and waits for a specified lifecycle event.
class RenderProcessHostWatcher : public RenderProcessHostObserver {
 public:
  enum WatchType {
    WATCH_FOR_PROCESS_READY,
    WATCH_FOR_PROCESS_EXIT,
    WATCH_FOR_HOST_DESTRUCTION
  };

  RenderProcessHostWatcher(RenderProcessHost* render_process_host,
                           WatchType type);
  // Waits for the renderer process that contains the specified web contents.
  RenderProcessHostWatcher(WebContents* web_contents, WatchType type);

  RenderProcessHostWatcher(const RenderProcessHostWatcher&) = delete;
  RenderProcessHostWatcher& operator=(const RenderProcessHostWatcher&) = delete;

  ~RenderProcessHostWatcher() override;

  // Waits until the expected event is triggered. This may only be called once.
  void Wait();

  // Returns true if a renderer process exited cleanly (without hitting
  // RenderProcessExited with an abnormal TerminationStatus). This should be
  // called after Wait().
  bool did_exit_normally() { return did_exit_normally_; }

 private:
  // Quit the run loop and clean up.
  void QuitRunLoop();

  // Overridden RenderProcessHost::LifecycleObserver methods.
  void RenderProcessReady(RenderProcessHost* host) override;
  void RenderProcessExited(RenderProcessHost* host,
                           const ChildProcessTerminationInfo& info) override;
  void RenderProcessHostDestroyed(RenderProcessHost* host) override;

  base::ScopedObservation<RenderProcessHost, RenderProcessHostObserver>
      observation_{this};
  WatchType type_;
  bool did_exit_normally_;

  std::unique_ptr<ScopedAllowRendererCrashes> allow_renderer_crashes_;

  base::RunLoop run_loop_;
  base::OnceClosure quit_closure_;
};

// Implementation helper for:
// *) content-internal content::RenderProcessHostBadIpcMessageWaiter
//    (declared in //content/test/content_browser_test_utils_internal.h)
// *) content-public content::RenderProcessHostBadMojoMessageWaiter
//    (declared below)
// *) maybe in the future: similar helpers for chrome-layer BadMessageReason
class RenderProcessHostKillWaiter {
 public:
  // |uma_name| is the name of the histogram from which the |bad_message_reason|
  // can be extracted.
  RenderProcessHostKillWaiter(RenderProcessHost* render_process_host,
                              const std::string& uma_name);

  RenderProcessHostKillWaiter(const RenderProcessHostKillWaiter&) = delete;
  RenderProcessHostKillWaiter& operator=(const RenderProcessHostKillWaiter&) =
      delete;

  // Waits until the renderer process exits.  Extracts and returns the bad
  // message reason that should be logged in the |uma_name_| histogram.
  // Returns |absl::nullopt| if the renderer exited normally or didn't log
  // the |uma_name_| histogram.
  absl::optional<int> Wait() WARN_UNUSED_RESULT;

 private:
  RenderProcessHostWatcher exit_watcher_;
  base::HistogramTester histogram_tester_;
  std::string uma_name_;
};

// Helps tests to wait until the given renderer process is terminated because of
// a bad/invalid mojo message.
//
// Example usage:
//   RenderProcessHostBadMojoMessageWaiter kill_waiter(render_process_host);
//   ... test code that triggers a renderer kill ...
//   EXPECT_EQ("expected error message", kill_waiter.Wait());
class RenderProcessHostBadMojoMessageWaiter {
 public:
  explicit RenderProcessHostBadMojoMessageWaiter(
      RenderProcessHost* render_process_host);
  ~RenderProcessHostBadMojoMessageWaiter();

  RenderProcessHostBadMojoMessageWaiter(
      const RenderProcessHostBadMojoMessageWaiter&) = delete;
  RenderProcessHostBadMojoMessageWaiter& operator=(
      const RenderProcessHostBadMojoMessageWaiter&) = delete;

  // Waits until |render_process_host| from the constructor is terminated
  // because of a bad/invalid mojo message and returns the associated error
  // string.  Returns absl::nullopt if the process was terminated for an
  // unrelated reason.
  absl::optional<std::string> Wait() WARN_UNUSED_RESULT;

 private:
  void OnBadMojoMessage(int render_process_id, const std::string& error);

  int monitored_render_process_id_;
  absl::optional<std::string> observed_mojo_error_;
  RenderProcessHostKillWaiter kill_waiter_;
};

// Watches for responses from the DOMAutomationController and keeps them in a
// queue. Useful for waiting for a message to be received.
class DOMMessageQueue : public NotificationObserver,
                        public WebContentsObserver {
 public:
  // Constructs a DOMMessageQueue and begins listening for messages from the
  // DOMAutomationController. Do not construct this until the browser has
  // started.
  DOMMessageQueue();

  // Same as the default constructor, but only listens for messages
  // sent from a particular |web_contents|.
  explicit DOMMessageQueue(WebContents* web_contents);

  // Same as the constructor with a WebContents, but observes the
  // RenderFrameHost deletion.
  explicit DOMMessageQueue(RenderFrameHost* render_frame_host);

  DOMMessageQueue(const DOMMessageQueue&) = delete;
  DOMMessageQueue& operator=(const DOMMessageQueue&) = delete;

  ~DOMMessageQueue() override;

  // Removes all messages in the message queue.
  void ClearQueue();

  // Wait for the next message to arrive. |message| will be set to the next
  // message. Returns true on success.
  bool WaitForMessage(std::string* message) WARN_UNUSED_RESULT;

  // If there is a message in the queue, then copies it to |message| and returns
  // true.  Otherwise (if the queue is empty), returns false.
  bool PopMessage(std::string* message) WARN_UNUSED_RESULT;

  // Overridden NotificationObserver methods.
  void Observe(int type,
               const NotificationSource& source,
               const NotificationDetails& details) override;

  // Overridden WebContentsObserver methods.
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;
  void RenderFrameDeleted(RenderFrameHost* render_frame_host) override;

 private:
  NotificationRegistrar registrar_;
  base::queue<std::string> message_queue_;
  base::OnceClosure quit_closure_;
  bool renderer_crashed_ = false;
  RenderFrameHost* render_frame_host_ = nullptr;
};

// Used to wait for a new WebContents to be created. Instantiate this object
// before the operation that will create the window.
class WebContentsAddedObserver {
 public:
  WebContentsAddedObserver();

  WebContentsAddedObserver(const WebContentsAddedObserver&) = delete;
  WebContentsAddedObserver& operator=(const WebContentsAddedObserver&) = delete;

  ~WebContentsAddedObserver();

  // Will run a message loop to wait for the new window if it hasn't been
  // created since the constructor
  WebContents* GetWebContents();

 private:
  void WebContentsCreated(WebContents* web_contents);

  // Callback to WebContentCreated(). Cached so that we can unregister it.
  base::RepeatingCallback<void(WebContents*)> web_contents_created_callback_;

  raw_ptr<WebContents> web_contents_;
  base::OnceClosure quit_closure_;
};

// Request a new frame be drawn, returns false if request fails.
bool RequestFrame(WebContents* web_contents);

// This class is intended to synchronize upon the submission of compositor
// frames from the renderer to the display compositor.
//
// This class enables observation of the provided
// RenderFrameMetadataProvider. Which notifies this of every
// subsequent frame submission. Observation ends upon the destruction of this
// class.
//
// Calling Wait will block the browser ui thread until the next time the
// renderer submits a frame.
//
// Tests interested in the associated RenderFrameMetadata will find it cached
// in the RenderFrameMetadataProvider.
class RenderFrameSubmissionObserver
    : public RenderFrameMetadataProvider::Observer {
 public:
  explicit RenderFrameSubmissionObserver(
      RenderFrameMetadataProviderImpl* render_frame_metadata_provider);
  explicit RenderFrameSubmissionObserver(FrameTreeNode* node);
  explicit RenderFrameSubmissionObserver(WebContents* web_contents);
  ~RenderFrameSubmissionObserver() override;

  // Resets the current |render_frame_count|;
  void ResetCounter() { render_frame_count_ = 0; }

  // Blocks the browser ui thread until the next OnRenderFrameSubmission.
  void WaitForAnyFrameSubmission();

  // Blocks the browser ui thread until the next
  // OnRenderFrameMetadataChangedAfterActivation.
  void WaitForMetadataChange();

  // Blocks the browser ui thread until RenderFrameMetadata arrives with
  // page scale factor matching |expected_page_scale_factor|.
  void WaitForPageScaleFactor(float expected_page_scale_factor,
                              const float tolerance);

  // Blocks the browser ui thread until RenderFrameMetadata arrives with
  // external page scale factor matching |expected_external_page_scale_factor|.
  void WaitForExternalPageScaleFactor(float expected_external_page_scale_factor,
                                      const float tolerance);

  // Blocks the browser ui thread until RenderFrameMetadata arrives where its
  // scroll offset matches |expected_offset|.
  void WaitForScrollOffset(const gfx::PointF& expected_offset);

  // Blocks the browser ui thread until RenderFrameMetadata arrives where its
  // scroll offset at top matches |expected_scroll_offset_at_top|.
  void WaitForScrollOffsetAtTop(bool expected_scroll_offset_at_top);

  const cc::RenderFrameMetadata& LastRenderFrameMetadata() const;

  // Returns the number of frames submitted since the observer's creation.
  int render_frame_count() const { return render_frame_count_; }

  // Runs |closure| the next time metadata changes.
  void NotifyOnNextMetadataChange(base::OnceClosure closure);

 private:
  // Exits |run_loop_| unblocking the UI thread. Execution will resume in Wait.
  void Quit();

  // Blocks the browser ui thread.
  void Wait();

  // RenderFrameMetadataProvider::Observer
  void OnRenderFrameMetadataChangedBeforeActivation(
      const cc::RenderFrameMetadata& metadata) override;
  void OnRenderFrameMetadataChangedAfterActivation(
      base::TimeTicks activation_time) override;
  void OnRenderFrameSubmission() override;
  void OnLocalSurfaceIdChanged(
      const cc::RenderFrameMetadata& metadata) override;

  // If true then the next OnRenderFrameSubmission will cancel the blocking
  // |run_loop_| otherwise the blocking will continue until the next
  // OnRenderFrameMetadataChangedAfterActivation.
  bool break_on_any_frame_ = false;

  raw_ptr<RenderFrameMetadataProviderImpl> render_frame_metadata_provider_ =
      nullptr;
  base::OnceClosure quit_closure_;
  // If non-null, run when metadata changes.
  base::OnceClosure metadata_change_closure_;
  int render_frame_count_ = 0;
};

// This class is intended to synchronize the renderer main thread, renderer impl
// thread and the browser main thread.
//
// This is accomplished by sending an IPC to RenderWidget, then blocking until
// the ACK is received and processed.
//
// The ACK is sent from compositor thread, when the CompositorFrame is submited
// to the display compositor
// TODO(danakj): This class seems to provide the same as
// RenderFrameSubmissionObserver, consider using that instead.
class MainThreadFrameObserver {
 public:
  explicit MainThreadFrameObserver(RenderWidgetHost* render_widget_host);

  MainThreadFrameObserver(const MainThreadFrameObserver&) = delete;
  MainThreadFrameObserver& operator=(const MainThreadFrameObserver&) = delete;

  ~MainThreadFrameObserver();

  // Synchronizes the browser main thread with the renderer main thread and impl
  // thread.
  void Wait();

 private:
  void Quit(bool);

  raw_ptr<RenderWidgetHost> render_widget_host_;
  base::OnceClosure quit_closure_;
  int routing_id_;
};

// Watches for an input msg to be consumed.
class InputMsgWatcher : public RenderWidgetHost::InputEventObserver {
 public:
  InputMsgWatcher(RenderWidgetHost* render_widget_host,
                  blink::WebInputEvent::Type type);

  InputMsgWatcher(const InputMsgWatcher&) = delete;
  InputMsgWatcher& operator=(const InputMsgWatcher&) = delete;

  ~InputMsgWatcher() override;

  bool HasReceivedAck() const;

  // Wait until ack message occurs, returning the ack result from
  // the message.
  blink::mojom::InputEventResultState WaitForAck();

  // Wait for the ack if it hasn't been received, if it has been
  // received return the result immediately.
  blink::mojom::InputEventResultState GetAckStateWaitIfNecessary();

  blink::mojom::InputEventResultSource last_event_ack_source() const {
    return ack_source_;
  }

 private:
  // Overridden InputEventObserver methods.
  void OnInputEventAck(blink::mojom::InputEventResultSource source,
                       blink::mojom::InputEventResultState state,
                       const blink::WebInputEvent&) override;

  raw_ptr<RenderWidgetHost> render_widget_host_;
  blink::WebInputEvent::Type wait_for_type_;
  blink::mojom::InputEventResultState ack_result_;
  blink::mojom::InputEventResultSource ack_source_;
  base::OnceClosure quit_closure_;
};

// Used to wait for a desired input event ack.
class InputEventAckWaiter : public RenderWidgetHost::InputEventObserver {
 public:
  // A function determining if a given |event| and its ack are what we're
  // waiting for.
  using InputEventAckPredicate =
      base::RepeatingCallback<bool(blink::mojom::InputEventResultSource source,
                                   blink::mojom::InputEventResultState state,
                                   const blink::WebInputEvent& event)>;

  // Wait for an event satisfying |predicate|.
  InputEventAckWaiter(RenderWidgetHost* render_widget_host,
                      InputEventAckPredicate predicate);
  // Wait for any event of the given |type|.
  InputEventAckWaiter(RenderWidgetHost* render_widget_host,
                      blink::WebInputEvent::Type type);

  InputEventAckWaiter(const InputEventAckWaiter&) = delete;
  InputEventAckWaiter& operator=(const InputEventAckWaiter&) = delete;

  ~InputEventAckWaiter() override;

  void Wait();
  void Reset();

  // RenderWidgetHost::InputEventObserver:
  void OnInputEventAck(blink::mojom::InputEventResultSource source,
                       blink::mojom::InputEventResultState state,
                       const blink::WebInputEvent& event) override;

 private:
  raw_ptr<RenderWidgetHost> render_widget_host_;
  InputEventAckPredicate predicate_;
  bool event_received_;
  base::OnceClosure quit_closure_;
};

// Sets up a ui::TestClipboard for use in browser tests. On Windows,
// clipboard is handled on the IO thread, BrowserTestClipboardScope
// hops messages onto the right thread.
class BrowserTestClipboardScope {
 public:
  // Sets up a ui::TestClipboard.
  BrowserTestClipboardScope();

  BrowserTestClipboardScope(const BrowserTestClipboardScope&) = delete;
  BrowserTestClipboardScope& operator=(const BrowserTestClipboardScope&) =
      delete;

  // Tears down the clipboard.
  ~BrowserTestClipboardScope();

  // Puts text/rtf |rtf| on the clipboard.
  void SetRtf(const std::string& rtf);

  // Puts plain text |text| on the clipboard.
  void SetText(const std::string& text);

  // Gets plain text from the clipboard, if any.
  void GetText(std::string* text);
};

// This observer is used to wait for its owner Frame to become focused.
class FrameFocusedObserver {
 public:
  explicit FrameFocusedObserver(RenderFrameHost* owner_host);

  FrameFocusedObserver(const FrameFocusedObserver&) = delete;
  FrameFocusedObserver& operator=(const FrameFocusedObserver&) = delete;

  ~FrameFocusedObserver();

  void Wait();

 private:
  // Private impl struct which hides non public types including FrameTreeNode.
  class FrameTreeNodeObserverImpl;

  // FrameTreeNode::Observer
  std::unique_ptr<FrameTreeNodeObserverImpl> impl_;
};

// This observer is used to wait for its owner FrameTreeNode to become deleted.
class FrameDeletedObserver {
 public:
  explicit FrameDeletedObserver(RenderFrameHost* owner_host);

  FrameDeletedObserver(const FrameDeletedObserver&) = delete;
  FrameDeletedObserver& operator=(const FrameDeletedObserver&) = delete;

  ~FrameDeletedObserver();

  void Wait();

 private:
  // Private impl struct which hides non public types including FrameTreeNode.
  class FrameTreeNodeObserverImpl;

  // FrameTreeNode::Observer
  std::unique_ptr<FrameTreeNodeObserverImpl> impl_;
};

// This class can be used to pause and resume navigations, based on a URL
// match. Note that it only keeps track of one navigation at a time.
// Navigations are paused automatically before hitting the network, and are
// resumed automatically if a Wait method is called for a future event.
//
// Note: This class is one time use only! After it successfully tracks a
// navigation it will ignore all subsequent navigations. Explicitly create
// multiple instances of this class if you want to pause multiple navigations.
//
// Note2: For a BFCache restore navigation, the navigation will not run
// NavigationThrottles. The manager in this case uses a CommitDeferringCondition
// for pausing the navigation at the equivalent of WillProcessResponse. However,
// in this navigation you cannot use WaitForRequestStart; if you want to yield
// before WillProcessResponse, use WaitForFirstYieldAfterDidStartNavigation.
//
// Note3: For a prerender activation, this class cannot pause the navigation as
// the prerender activation doesn't run NavigationThrottles and runs
// CommitDeferringConditions before StartNavigation() that WebContentsObserver
// cannot observe.
// TODO(nhiroki): Provide a way to pause the prerender activation. We could do
// the similar thing as MockCommitDeferringConditionInstaller does like adding a
// closure that generates a deferring condition to pause the navigation in
// CommitDeferringConditionRunner.
class TestNavigationManager : public WebContentsObserver {
 public:
  // Monitors any frame in WebContents.
  TestNavigationManager(WebContents* web_contents, const GURL& url);

  TestNavigationManager(const TestNavigationManager&) = delete;
  TestNavigationManager& operator=(const TestNavigationManager&) = delete;

  ~TestNavigationManager() override;

  // Waits until the first yield point after DidStartNavigation. Unlike
  // WaitForRequestStart, this can be used to wait for a pause in cases where a
  // test expects a NavigationThrottle to defer in WillStartRequest. In cases
  // where throttles run and none defer, this will break at the same time as
  // WaitForRequestStart. Also unlike WaitForRequestStart, this can be used to
  // wait on a page activating navigation to start. Note: since we won't know
  // which throttle deferred, don't use ResumeNavigation() after this call since
  // it assumes we paused from the TestNavigationManagerThrottle.
  void WaitForFirstYieldAfterDidStartNavigation();

  // Waits until the navigation request is ready to be sent to the network
  // stack. This will wait until all NavigationThrottles have proceeded through
  // WillStartRequest. Returns false if the request was aborted before starting.
  // Note: RequestStart is never reached for page activating navigations (e.g.
  // prerender activation, BFCache restore). In those cases you should either
  // use WaitForFirstYieldAfterDidStartNavigation or WaitForResponse. See
  // TestNavigationManager class comment for more detail.
  WARN_UNUSED_RESULT bool WaitForRequestStart();

  // Waits until the navigation response's headers have been received. This
  // will wait until all NavigationThrottles have proceeded through
  // WillProcessResponse. Returns false if the request was aborted before
  // getting a response.
  WARN_UNUSED_RESULT bool WaitForResponse();

  // Waits until the navigation has been finished. Will automatically resume
  // navigations paused before this point.
  void WaitForNavigationFinished();

  // Resume the navigation.
  // * Called after |WaitForRequestStart|, it causes the request to be sent.
  // * Called after |WaitForResponse|, it causes the response to be committed.
  void ResumeNavigation();

  // Returns the NavigationHandle associated with the navigation. It is non-null
  // only in between DidStartNavigation(...) and DidFinishNavigation(...).
  NavigationHandle* GetNavigationHandle();

  // Whether the navigation successfully committed.
  bool was_committed() const { return was_committed_; }

  // Whether the navigation successfully committed and was not an error page.
  bool was_successful() const { return was_successful_; }

  // Whether the navigation activated a prerendered page.
  bool was_prerendered_page_activation() const {
    return was_prerendered_page_activation_.value();
  }

  // Allows nestable tasks when running a message loop in the Wait* functions.
  // This is useful for utilizing this class from within another message loop.
  void AllowNestableTasks();

  // Write a representation of this class into trace.
  void WriteIntoTrace(perfetto::TracedValue ctx) const;

 protected:
  // Derived classes can override if they want to filter out navigations. This
  // is called from DidStartNavigation.
  virtual bool ShouldMonitorNavigation(NavigationHandle* handle);

 private:
  enum class NavigationState {
    INITIAL = 0,
    WILL_START = 1,
    STARTED = 2,
    RESPONSE = 3,
    FINISHED = 4,
  };

  // WebContentsObserver:
  void DidStartNavigation(NavigationHandle* handle) override;
  void DidFinishNavigation(NavigationHandle* handle) override;

  // Called when the NavigationThrottle pauses the navigation in
  // WillStartRequest.
  void OnWillStartRequest();

  // Called when the NavigationThrottle pauses the navigation in
  // WillProcessResponse.
  void OnWillProcessResponse();

  // Called when the navigation pauses in the MockCommitDeferringCondition. This
  // happens only for page activating navigations like a prerender activation.
  void OnRunningCommitDeferringConditions(base::OnceClosure resume_closure);

  // Waits for the desired state. Returns false if the desired state cannot be
  // reached (eg the navigation finishes before reaching this state).
  bool WaitForDesiredState();

  // Called when the state of the navigation has changed. This will either stop
  // the message loop if the state specified by the user has been reached, or
  // resume the navigation if it hasn't been reached yet.
  void OnNavigationStateChanged();

  void ResumeIfPaused();

  const GURL url_;
  NavigationRequest* request_ = nullptr;
  bool navigation_paused_ = false;
  NavigationState current_state_ = NavigationState::INITIAL;
  NavigationState desired_state_ = NavigationState::WILL_START;
  bool was_committed_ = false;
  bool was_successful_ = false;
  absl::optional<bool> was_prerendered_page_activation_;
  base::OnceClosure quit_closure_;
  base::RunLoop::Type message_loop_type_ = base::RunLoop::Type::kDefault;

  // In a page activating navigation (prerender activation, back-forward cache
  // activation), the navigation will be stopped in a commit deferring condition
  // (since NavigationThrottles aren't run in a page activation). When that
  // happens, the navigation can be resumed using this closure.
  base::OnceClosure commit_deferring_condition_resume_closure_;

  base::WeakPtrFactory<TestNavigationManager> weak_factory_{this};
};

class NavigationHandleCommitObserver : public content::WebContentsObserver {
 public:
  NavigationHandleCommitObserver(content::WebContents* web_contents,
                                 const GURL& url);

  bool has_committed() const { return has_committed_; }
  bool was_same_document() const { return was_same_document_; }
  bool was_renderer_initiated() const { return was_renderer_initiated_; }

 private:
  void DidFinishNavigation(content::NavigationHandle* handle) override;

  const GURL url_;
  bool has_committed_;
  bool was_same_document_;
  bool was_renderer_initiated_;
};

// A test utility that monitors console messages sent to a WebContents. This
// can be used to wait for a message that matches a specific filter, an
// arbitrary message, or monitor all messages sent to the WebContents' console.
class WebContentsConsoleObserver : public WebContentsObserver {
 public:
  struct Message {
    raw_ptr<RenderFrameHost> source_frame;
    blink::mojom::ConsoleMessageLevel log_level;
    std::u16string message;
    int32_t line_no;
    std::u16string source_id;
  };

  // A filter to apply to incoming console messages to determine whether to
  // record them. The filter should return `true` if the observer should record
  // the message, and stop waiting (if it was waiting).
  using Filter = base::RepeatingCallback<bool(const Message& message)>;

  explicit WebContentsConsoleObserver(content::WebContents* web_contents);
  ~WebContentsConsoleObserver() override;

  // Waits for a message to come in that matches the set filter, if any. If no
  // filter is set, waits for the first message that comes in.
  void Wait();

  // Sets a custom filter to be used while waiting for a message, allowing
  // more custom filtering (e.g. based on source).
  void SetFilter(Filter filter);

  // A convenience method to just match the message against a string pattern.
  void SetPattern(std::string pattern);

  // A helper method to return the string content (in UTF8) of the message at
  // the given |index|. This will cause a test failure if there is no such
  // message.
  std::string GetMessageAt(size_t index) const;

  const std::vector<Message>& messages() const { return messages_; }

 private:
  // WebContentsObserver:
  void OnDidAddMessageToConsole(
      RenderFrameHost* source_frame,
      blink::mojom::ConsoleMessageLevel log_level,
      const std::u16string& message,
      int32_t line_no,
      const std::u16string& source_id,
      const absl::optional<std::u16string>& untrusted_stack_trace) override;

  Filter filter_;
  std::string pattern_;
  base::RunLoop run_loop_;
  std::vector<Message> messages_;
};

// Static methods that simulates Mojo methods as if they were called by a
// renderer. Used to simulate a compromised renderer.
class PwnMessageHelper {
 public:
  PwnMessageHelper(const PwnMessageHelper&) = delete;
  PwnMessageHelper& operator=(const PwnMessageHelper&) = delete;

  // Calls Create method in FileSystemHost Mojo interface.
  static void FileSystemCreate(RenderProcessHost* process,
                               int request_id,
                               GURL path,
                               bool exclusive,
                               bool is_directory,
                               bool recursive,
                               const blink::StorageKey& storage_key);

  // Calls Write method in FileSystemHost Mojo interface.
  static void FileSystemWrite(RenderProcessHost* process,
                              int request_id,
                              GURL file_path,
                              std::string blob_uuid,
                              int64_t position,
                              const blink::StorageKey& storage_key);

  // Calls OpenURL method in FrameHost Mojo interface.
  static void OpenURL(RenderFrameHost* render_frame_host, const GURL& url);

 private:
  PwnMessageHelper();  // Not instantiable.
};

#if defined(USE_AURA)

// Tests that a |render_widget_host_view| stores a stale content when its frame
// gets evicted. |render_widget_host_view| has to be a RenderWidgetHostViewAura.
void VerifyStaleContentOnFrameEviction(
    RenderWidgetHostView* render_widget_host_view);

#endif  // defined(USE_AURA)

// This class intercepts for ShowContextMenu Mojo method called from a
// renderer process, and allows observing the UntrustworthyContextMenuParams as
// sent by the renderer.
class ContextMenuInterceptor
    : public blink::mojom::LocalFrameHostInterceptorForTesting {
 public:
  // Whether or not the ContextMenu should be prevented from performing
  // its default action, preventing the context menu from showing.
  enum ShowBehavior { kShow, kPreventShow };

  explicit ContextMenuInterceptor(content::RenderFrameHost* render_frame_host,
                                  ShowBehavior behavior = ShowBehavior::kShow);
  ContextMenuInterceptor(const ContextMenuInterceptor&) = delete;
  ContextMenuInterceptor& operator=(const ContextMenuInterceptor&) = delete;
  ~ContextMenuInterceptor() override;

  blink::mojom::LocalFrameHost* GetForwardingInterface() override;

  void ShowContextMenu(
      mojo::PendingAssociatedRemote<blink::mojom::ContextMenuClient>
          context_menu_client,
      const blink::UntrustworthyContextMenuParams& params) override;

  void Wait();
  void Reset();

  blink::UntrustworthyContextMenuParams get_params() { return last_params_; }

 private:
  raw_ptr<content::RenderFrameHostImpl> render_frame_host_impl_;
  raw_ptr<blink::mojom::LocalFrameHost> impl_;
  std::unique_ptr<base::RunLoop> run_loop_;
  base::OnceClosure quit_closure_;
  blink::UntrustworthyContextMenuParams last_params_;
  const ShowBehavior show_behavior_;
};

class UpdateUserActivationStateInterceptor
    : public blink::mojom::LocalFrameHostInterceptorForTesting {
 public:
  explicit UpdateUserActivationStateInterceptor(
      content::RenderFrameHost* render_frame_host);
  UpdateUserActivationStateInterceptor(
      const UpdateUserActivationStateInterceptor&) = delete;
  UpdateUserActivationStateInterceptor& operator=(
      const UpdateUserActivationStateInterceptor&) = delete;
  ~UpdateUserActivationStateInterceptor() override;

  void set_quit_handler(base::OnceClosure handler);
  bool update_user_activation_state() { return update_user_activation_state_; }

  blink::mojom::LocalFrameHost* GetForwardingInterface() override;
  void UpdateUserActivationState(
      blink::mojom::UserActivationUpdateType update_type,
      blink::mojom::UserActivationNotificationType notification_type) override;

 private:
  raw_ptr<content::RenderFrameHostImpl> render_frame_host_impl_;
  raw_ptr<blink::mojom::LocalFrameHost> impl_;
  base::OnceClosure quit_handler_;
  bool update_user_activation_state_ = false;
};

WebContents* GetEmbedderForGuest(content::WebContents* guest);

// Load the given |url| with |network_context| and return the |net::Error| code.
//
// This overload simulates loading through a URLLoaderFactory created for a
// Browser process.
int LoadBasicRequest(network::mojom::NetworkContext* network_context,
                     const GURL& url,
                     int load_flags = net::LOAD_NORMAL);

// Load the given |url| via URLLoaderFactory created by |frame|.  Return the
// |net::Error| code.
//
// This overload simulates loading through a URLLoaderFactory created for a
// Renderer process (the factory is driven from the Test/Browser process, but
// has the same properties as factories vended to the Renderer process that
// hosts the |frame|).
int LoadBasicRequest(RenderFrameHost* frame, const GURL& url);

// Ensures that all StoragePartitions for the given BrowserContext have their
// cookies flushed to disk.
void EnsureCookiesFlushed(BrowserContext* browser_context);

// Performs a simple auto-resize flow and ensures that the embedder gets a
// single response messages back from the guest, with the expected values.
bool TestGuestAutoresize(WebContents* embedder_web_contents,
                         WebContents* guest_web_contents);

// Class to intercept SynchronizeVisualProperties method. This allows the
// message to continue to the target child so that processing can be verified by
// tests. It also monitors for GesturePinchBegin/End events.
class SynchronizeVisualPropertiesInterceptor
    : public blink::mojom::RemoteFrameHostInterceptorForTesting {
 public:
  explicit SynchronizeVisualPropertiesInterceptor(
      RenderFrameProxyHost* render_frame_proxy_host);

  SynchronizeVisualPropertiesInterceptor(
      const SynchronizeVisualPropertiesInterceptor&) = delete;
  SynchronizeVisualPropertiesInterceptor& operator=(
      const SynchronizeVisualPropertiesInterceptor&) = delete;

  ~SynchronizeVisualPropertiesInterceptor() override;

  blink::mojom::RemoteFrameHost* GetForwardingInterface() override;

  void SynchronizeVisualProperties(
      const blink::FrameVisualProperties& visual_properties) override;

  gfx::Rect last_rect() const { return last_rect_; }

  void WaitForRect();
  void ResetRectRunLoop();

  // Waits for the next viz::LocalSurfaceId be received and returns it.
  viz::LocalSurfaceId WaitForSurfaceId();

  bool pinch_gesture_active_set() { return pinch_gesture_active_set_; }
  bool pinch_gesture_active_cleared() { return pinch_gesture_active_cleared_; }

  void WaitForPinchGestureEnd();

 private:
  // |rect| is in DIPs.
  void OnUpdatedFrameRectOnUI(const gfx::Rect& rect);
  void OnUpdatedFrameSinkIdOnUI();
  void OnUpdatedSurfaceIdOnUI(viz::LocalSurfaceId surface_id);

  base::RunLoop run_loop_;

  raw_ptr<RenderFrameProxyHost> render_frame_proxy_host_;

  std::unique_ptr<base::RunLoop> screen_space_rect_run_loop_;
  bool screen_space_rect_received_ = false;
  gfx::Rect last_rect_;

  viz::LocalSurfaceId last_surface_id_;
  std::unique_ptr<base::RunLoop> surface_id_run_loop_;

  bool pinch_gesture_active_set_ = false;
  bool pinch_gesture_active_cleared_ = false;
  bool last_pinch_gesture_active_ = false;
  std::unique_ptr<base::RunLoop> pinch_end_run_loop_;

  raw_ptr<blink::mojom::RemoteFrameHost> impl_;

  base::WeakPtrFactory<SynchronizeVisualPropertiesInterceptor> weak_factory_{
      this};
};

// This class allows monitoring of mouse events received by a specific
// RenderWidgetHost.
class RenderWidgetHostMouseEventMonitor {
 public:
  explicit RenderWidgetHostMouseEventMonitor(RenderWidgetHost* host);

  RenderWidgetHostMouseEventMonitor(const RenderWidgetHostMouseEventMonitor&) =
      delete;
  RenderWidgetHostMouseEventMonitor& operator=(
      const RenderWidgetHostMouseEventMonitor&) = delete;

  ~RenderWidgetHostMouseEventMonitor();
  bool EventWasReceived() const { return event_received_; }
  void ResetEventReceived() { event_received_ = false; }
  const blink::WebMouseEvent& event() const { return event_; }

 private:
  bool MouseEventCallback(const blink::WebMouseEvent& event) {
    event_received_ = true;
    event_ = event;
    return false;
  }
  RenderWidgetHost::MouseEventCallback mouse_callback_;
  raw_ptr<RenderWidgetHost> host_;
  bool event_received_;
  blink::WebMouseEvent event_;
};

// Helper class to track and allow waiting for navigation start events.
class DidStartNavigationObserver : public WebContentsObserver {
 public:
  explicit DidStartNavigationObserver(WebContents* web_contents);

  DidStartNavigationObserver(const DidStartNavigationObserver&) = delete;
  DidStartNavigationObserver& operator=(const DidStartNavigationObserver&) =
      delete;

  ~DidStartNavigationObserver() override;

  void Wait() { run_loop_.Run(); }
  bool observed() { return observed_; }

  // If the navigation was observed and is still not finished yet, this returns
  // its handle, otherwise it returns nullptr.
  NavigationHandle* navigation_handle() { return navigation_handle_; }

  // WebContentsObserver override:
  void DidStartNavigation(NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(NavigationHandle* navigation_handle) override;

 private:
  bool observed_ = false;
  base::RunLoop run_loop_;
  raw_ptr<NavigationHandle> navigation_handle_ = nullptr;
};

// Tracks the creation of RenderFrameProxyHosts that have
// CrossProcessFrameConnectors, and records the initial (post-construction)
// device scale factor in the CrossProcessFrameConnector.
class ProxyDSFObserver {
 public:
  ProxyDSFObserver();
  ~ProxyDSFObserver();

  // Waits until a single RenderFrameProxyHost with a CrossProcessFrameConnector
  // has been created. If a creation occurs before this function is called, it
  // returns immediately.
  void WaitForOneProxyHostCreation();

  size_t num_creations() { return proxy_host_created_dsf_.size(); }

  float get_proxy_host_dsf(unsigned index) {
    return proxy_host_created_dsf_[index];
  }

 private:
  void OnCreation(RenderFrameProxyHost* rfph);

  // Make this a vector, just in case we encounter multiple creations prior to
  // calling WaitForOneProxyHostCreation(). That way we can confirm we're
  // getting the device scale factor we expect.
  // Note: We can modify the vector to collect a void* id for each
  // RenderFrameProxyHost if we want to expand to cases where multiple creations
  // must be observed.
  std::vector<float> proxy_host_created_dsf_;
  std::unique_ptr<base::RunLoop> runner_;
};

// Helper used by the wrapper class below. Compares the output of the given
// |web_contents| to the PNG file at |expected_path| across the region defined
// by |snapshot_size| and returns true if the images are equivalent. Uses a
// ManhattanDistancePixelComparator which allows some small differences.  If
// the flag switches::kRebaselinePixelTests (--rebaseline-pixel-tests) is set,
// this function will (over)write the reference file with the produced output.
bool CompareWebContentsOutputToReference(
    WebContents* web_contents,
    const base::FilePath& expected_path,
    const gfx::Size& snapshot_size,
    const cc::PixelComparator& comparator =
        cc::ManhattanDistancePixelComparator());

typedef base::OnceCallback<void(RenderFrameHost*, RenderFrameHost*)>
    RenderFrameHostChangedCallback;

// Runs callback at RenderFrameHostChanged time. On cross-RFH navigations, this
// will run the callback after the new RenderFrameHost committed and is set as
// the current RenderFrameHost, etc. but before the old RenderFrameHost gets
// unloaded.
class RenderFrameHostChangedCallbackRunner : public WebContentsObserver {
 public:
  explicit RenderFrameHostChangedCallbackRunner(
      WebContents* content,
      RenderFrameHostChangedCallback callback);

  RenderFrameHostChangedCallbackRunner(
      const RenderFrameHostChangedCallbackRunner&) = delete;
  RenderFrameHostChangedCallbackRunner& operator=(
      const RenderFrameHostChangedCallbackRunner&) = delete;

  ~RenderFrameHostChangedCallbackRunner() override;

 private:
  void RenderFrameHostChanged(RenderFrameHost* old_host,
                              RenderFrameHost* new_host) override;

  RenderFrameHostChangedCallback callback_;
};

// Calls |callback| whenever a navigation finishes in |render_frame_host|.
class DidFinishNavigationObserver : public WebContentsObserver {
 public:
  DidFinishNavigationObserver(
      WebContents* web_contents,
      base::RepeatingCallback<void(NavigationHandle*)> callback);

  DidFinishNavigationObserver(
      RenderFrameHost* render_frame_host,
      base::RepeatingCallback<void(NavigationHandle*)> callback);

  ~DidFinishNavigationObserver() override;

  DidFinishNavigationObserver(const DidFinishNavigationObserver&) = delete;
  DidFinishNavigationObserver& operator=(const DidFinishNavigationObserver&) =
      delete;

 protected:
  // WebContentsObserver:
  void DidFinishNavigation(NavigationHandle* navigation_handle) override;

 private:
  base::RepeatingCallback<void(NavigationHandle*)> callback_;
};

// Functions to traverse history and wait until the traversal completes. These
// are wrappers around the same-named methods of the `NavigationController`.
WARN_UNUSED_RESULT bool HistoryGoToIndex(WebContents* wc, int index);
WARN_UNUSED_RESULT bool HistoryGoToOffset(WebContents* wc, int offset);
WARN_UNUSED_RESULT bool HistoryGoBack(WebContents* wc);
WARN_UNUSED_RESULT bool HistoryGoForward(WebContents* wc);

}  // namespace content

#endif  // CONTENT_PUBLIC_TEST_BROWSER_TEST_UTILS_H_
