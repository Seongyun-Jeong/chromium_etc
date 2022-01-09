// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/webui/web_ui_impl.h"

#include <stddef.h>

#include <string>
#include <utility>

#include "base/callback_helpers.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/json/json_writer.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/build_config.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/renderer_host/dip_util.h"
#include "content/browser/renderer_host/frame_tree.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/browser/web_contents/web_contents_view.h"
#include "content/browser/webui/web_ui_controller_factory_registry.h"
#include "content/browser/webui/web_ui_main_frame_observer.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/common/content_client.h"
#include "services/service_manager/public/cpp/interface_provider.h"

namespace content {

const WebUI::TypeID WebUI::kNoWebUI = nullptr;

// static
std::u16string WebUI::GetJavascriptCall(
    const std::string& function_name,
    const std::vector<const base::Value*>& arg_list) {
  std::u16string result(base::ASCIIToUTF16(function_name));
  result.push_back('(');

  std::string json;
  for (size_t i = 0; i < arg_list.size(); ++i) {
    if (i > 0)
      result.push_back(',');

    base::JSONWriter::Write(*arg_list[i], &json);
    result.append(base::UTF8ToUTF16(json));
  }

  result.push_back(')');
  result.push_back(';');
  return result;
}

WebUIImpl::WebUIImpl(WebContentsImpl* contents, RenderFrameHostImpl* frame_host)
    : bindings_(BINDINGS_POLICY_WEB_UI),
      requestable_schemes_({kChromeUIScheme, url::kFileScheme}),
      frame_host_(frame_host),
      web_contents_(contents),
      web_contents_observer_(new WebUIMainFrameObserver(this, contents)) {
  DCHECK(contents);

  // Assert that we can only open webui for the active or speculative pages.
  DCHECK(frame_host->lifecycle_state() ==
             RenderFrameHostImpl::LifecycleStateImpl::kActive ||
         frame_host->lifecycle_state() ==
             RenderFrameHostImpl::LifecycleStateImpl::kSpeculative);
}

WebUIImpl::~WebUIImpl() {
  // Delete the controller first, since it may also be keeping a pointer to some
  // of the handlers and can call them at destruction.
  controller_.reset();
  remote_.reset();
  receiver_.reset();
}

void WebUIImpl::SetProperty(const std::string& name, const std::string& value) {
  DCHECK(remote_);
  remote_->SetProperty(name, value);
}

void WebUIImpl::Send(const std::string& message, base::Value args) {
  const GURL& source_url = frame_host_->GetLastCommittedURL();
  if (!ChildProcessSecurityPolicyImpl::GetInstance()->HasWebUIBindings(
          frame_host_->GetProcess()->GetID()) ||
      !WebUIControllerFactoryRegistry::GetInstance()->IsURLAcceptableForWebUI(
          web_contents_->GetBrowserContext(), source_url)) {
    bad_message::ReceivedBadMessage(
        frame_host_->GetProcess(),
        bad_message::WEBUI_SEND_FROM_UNAUTHORIZED_PROCESS);
    return;
  }

  if (base::EndsWith(message, "RequiringGesture",
                     base::CompareCase::SENSITIVE) &&
      !web_contents_->HasRecentInteractiveInputEvent()) {
    LOG(ERROR) << message << " received without recent user interaction";
    return;
  }

  ProcessWebUIMessage(source_url, message, base::Value::AsListValue(args));
}

void WebUIImpl::WebUIRenderFrameCreated(RenderFrameHost* render_frame_host) {
  controller_->WebUIRenderFrameCreated(render_frame_host);
}

void WebUIImpl::RenderFrameReused(RenderFrameHost* render_frame_host) {
  // This is expected to be called only for outermost main frames.
  if (!render_frame_host->GetParentOrOuterDocument()) {
    GURL site_url = render_frame_host->GetSiteInstance()->GetSiteURL();
    GetContentClient()->browser()->LogWebUIUrl(site_url);
  }
}

void WebUIImpl::RenderFrameHostUnloading() {
  DisallowJavascriptOnAllHandlers();
}

void WebUIImpl::RenderFrameDeleted() {
  DisallowJavascriptOnAllHandlers();
}

void WebUIImpl::SetUpMojoConnection() {
  // TODO(nasko): WebUI mojo might be useful to be registered for
  // subframes as well, though at this time there is no such usage but currently
  // this is expected to be called only for outermost main frames.
  if (frame_host_->GetParentOrOuterDocument())
    return;

  frame_host_->GetFrameBindingsControl()->BindWebUI(
      remote_.BindNewEndpointAndPassReceiver(),
      receiver_.BindNewEndpointAndPassRemote());
}

void WebUIImpl::TearDownMojoConnection() {
  // This is expected to be called only for outermost main frames.
  if (frame_host_->GetParentOrOuterDocument())
    return;

  remote_.reset();
  receiver_.reset();
}

WebContents* WebUIImpl::GetWebContents() {
  return web_contents_;
}

float WebUIImpl::GetDeviceScaleFactor() {
  return GetScaleFactorForView(web_contents_->GetRenderWidgetHostView());
}

const std::u16string& WebUIImpl::GetOverriddenTitle() {
  return overridden_title_;
}

void WebUIImpl::OverrideTitle(const std::u16string& title) {
  overridden_title_ = title;
}

int WebUIImpl::GetBindings() {
  return bindings_;
}

void WebUIImpl::SetBindings(int bindings) {
  bindings_ = bindings;
}

const std::vector<std::string>& WebUIImpl::GetRequestableSchemes() {
  return requestable_schemes_;
}

void WebUIImpl::AddRequestableScheme(const char* scheme) {
  requestable_schemes_.push_back(scheme);
}

WebUIController* WebUIImpl::GetController() {
  return controller_.get();
}

void WebUIImpl::SetController(std::unique_ptr<WebUIController> controller) {
  DCHECK(controller);
  controller_ = std::move(controller);
}

bool WebUIImpl::CanCallJavascript() {
  return (ChildProcessSecurityPolicyImpl::GetInstance()->HasWebUIBindings(
              frame_host_->GetProcess()->GetID()) ||
          // It's possible to load about:blank in a Web UI renderer.
          // See http://crbug.com/42547
          frame_host_->GetLastCommittedURL().spec() == url::kAboutBlankURL);
}

void WebUIImpl::CallJavascriptFunctionUnsafe(const std::string& function_name) {
  DCHECK(base::IsStringASCII(function_name));
  std::u16string javascript = base::ASCIIToUTF16(function_name + "();");
  ExecuteJavascript(javascript);
}

void WebUIImpl::CallJavascriptFunctionUnsafe(const std::string& function_name,
                                             const base::Value& arg) {
  DCHECK(base::IsStringASCII(function_name));
  std::vector<const base::Value*> args;
  args.push_back(&arg);
  ExecuteJavascript(GetJavascriptCall(function_name, args));
}

void WebUIImpl::CallJavascriptFunctionUnsafe(const std::string& function_name,
                                             const base::Value& arg1,
                                             const base::Value& arg2) {
  DCHECK(base::IsStringASCII(function_name));
  std::vector<const base::Value*> args;
  args.push_back(&arg1);
  args.push_back(&arg2);
  ExecuteJavascript(GetJavascriptCall(function_name, args));
}

void WebUIImpl::CallJavascriptFunctionUnsafe(const std::string& function_name,
                                             const base::Value& arg1,
                                             const base::Value& arg2,
                                             const base::Value& arg3) {
  DCHECK(base::IsStringASCII(function_name));
  std::vector<const base::Value*> args;
  args.push_back(&arg1);
  args.push_back(&arg2);
  args.push_back(&arg3);
  ExecuteJavascript(GetJavascriptCall(function_name, args));
}

void WebUIImpl::CallJavascriptFunctionUnsafe(const std::string& function_name,
                                             const base::Value& arg1,
                                             const base::Value& arg2,
                                             const base::Value& arg3,
                                             const base::Value& arg4) {
  DCHECK(base::IsStringASCII(function_name));
  std::vector<const base::Value*> args;
  args.push_back(&arg1);
  args.push_back(&arg2);
  args.push_back(&arg3);
  args.push_back(&arg4);
  ExecuteJavascript(GetJavascriptCall(function_name, args));
}

void WebUIImpl::CallJavascriptFunctionUnsafe(
    const std::string& function_name,
    const std::vector<const base::Value*>& args) {
  DCHECK(base::IsStringASCII(function_name));
  ExecuteJavascript(GetJavascriptCall(function_name, args));
}

void WebUIImpl::RegisterMessageCallback(base::StringPiece message,
                                        MessageCallback callback) {
  message_callbacks_.emplace(std::string(message), std::move(callback));
}

void WebUIImpl::RegisterDeprecatedMessageCallback(
    base::StringPiece message,
    const DeprecatedMessageCallback& callback) {
  deprecated_message_callbacks_.emplace(std::string(message), callback);
}

void WebUIImpl::ProcessWebUIMessage(const GURL& source_url,
                                    const std::string& message,
                                    const base::ListValue& args) {
  // Crash keys for https://crbug.com/1275766
  SCOPED_CRASH_KEY_STRING32("WebUI", "URL", source_url.spec());
  SCOPED_CRASH_KEY_STRING64("WebUI", "message", message);

  if (controller_->OverrideHandleWebUIMessage(source_url, message, args))
    return;

  // Look up the callback for this message.
  auto callback_pair = message_callbacks_.find(message);
  if (callback_pair != message_callbacks_.end()) {
    // Forward this message and content on.
    callback_pair->second.Run(args.GetList());
    return;
  }

  // Look up the deprecated callback for this message.
  auto deprecated_callback_pair = deprecated_message_callbacks_.find(message);
  if (deprecated_callback_pair != deprecated_message_callbacks_.end()) {
    // Forward this message and content on.
    deprecated_callback_pair->second.Run(&args);
    return;
  }

  NOTREACHED() << "Unhandled chrome.send(\"" << message << "\");";
}

std::vector<std::unique_ptr<WebUIMessageHandler>>*
WebUIImpl::GetHandlersForTesting() {
  return &handlers_;
}

// WebUIImpl, protected: -------------------------------------------------------

void WebUIImpl::AddMessageHandler(
    std::unique_ptr<WebUIMessageHandler> handler) {
  DCHECK(!handler->web_ui());
  handler->set_web_ui(this);
  handler->RegisterMessages();
  handlers_.push_back(std::move(handler));
}

void WebUIImpl::ExecuteJavascript(const std::u16string& javascript) {
  // Silently ignore the request. Would be nice to clean-up WebUI so we
  // could turn this into a CHECK(). http://crbug.com/516690.
  if (!CanCallJavascript())
    return;

  frame_host_->ExecuteJavaScript(javascript, base::NullCallback());
}

void WebUIImpl::DisallowJavascriptOnAllHandlers() {
  for (const std::unique_ptr<WebUIMessageHandler>& handler : handlers_)
    handler->DisallowJavascript();
}

}  // namespace content
