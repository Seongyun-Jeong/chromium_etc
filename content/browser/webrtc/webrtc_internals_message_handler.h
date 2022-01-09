// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_WEBRTC_WEBRTC_INTERNALS_MESSAGE_HANDLER_H_
#define CONTENT_BROWSER_WEBRTC_WEBRTC_INTERNALS_MESSAGE_HANDLER_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "content/browser/webrtc/webrtc_internals_ui_observer.h"
#include "content/common/content_export.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace base {
class ListValue;
}  // namespace base

namespace content {

class RenderFrameHost;
class WebRTCInternals;

// This class handles messages to and from WebRTCInternalsUI.
// It delegates all its work to WebRTCInternalsProxy on the IO thread.
class CONTENT_EXPORT WebRTCInternalsMessageHandler
    : public WebUIMessageHandler,
      public WebRTCInternalsUIObserver {
 public:
  WebRTCInternalsMessageHandler();

  WebRTCInternalsMessageHandler(const WebRTCInternalsMessageHandler&) = delete;
  WebRTCInternalsMessageHandler& operator=(
      const WebRTCInternalsMessageHandler&) = delete;

  ~WebRTCInternalsMessageHandler() override;

  // WebUIMessageHandler implementation.
  void RegisterMessages() override;

 protected:
  // The WebRTCInternals to use. Always WebRTCInternals::GetInstance()
  // except for testing.
  explicit WebRTCInternalsMessageHandler(WebRTCInternals* webrtc_internals);
  const raw_ptr<WebRTCInternals> webrtc_internals_;

 private:
  // Returns a pointer to the RFH iff it is currently hosting the
  // webrtc-internals page.
  RenderFrameHost* GetWebRTCInternalsHost();

  // Javascript message handler.
  void OnGetStandardStats(const base::ListValue* list);
  void OnGetLegacyStats(const base::ListValue* list);
  void OnSetAudioDebugRecordingsEnabled(bool enable,
                                        const base::ListValue* list);
  void OnSetEventLogRecordingsEnabled(bool enable, const base::ListValue* list);
  void OnDOMLoadDone(const base::ListValue* list);

  // WebRTCInternalsUIObserver override.
  void OnUpdate(const std::string& event_name,
                const base::Value* event_data) override;

  // Executes Javascript command.
  void ExecuteJavascriptCommand(const char* command, const base::Value* args);
};

}  // namespace content

#endif  // CONTENT_BROWSER_WEBRTC_WEBRTC_INTERNALS_MESSAGE_HANDLER_H_
