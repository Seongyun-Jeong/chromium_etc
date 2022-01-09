// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_HOST_MOCK_OBJECTS_H_
#define REMOTING_HOST_HOST_MOCK_OBJECTS_H_

#include <cstdint>
#include <memory>
#include <string>

#include "net/base/ip_endpoint.h"
#include "remoting/host/action_executor.h"
#include "remoting/host/base/screen_controls.h"
#include "remoting/host/base/screen_resolution.h"
#include "remoting/host/chromoting_host_context.h"
#include "remoting/host/chromoting_host_services_provider.h"
#include "remoting/host/client_session.h"
#include "remoting/host/client_session_control.h"
#include "remoting/host/client_session_details.h"
#include "remoting/host/client_session_events.h"
#include "remoting/host/desktop_environment.h"
#include "remoting/host/host_status_observer.h"
#include "remoting/host/input_injector.h"
#include "remoting/host/mojom/chromoting_host_services.mojom.h"
#include "remoting/host/remote_open_url/url_forwarder_configurator.h"
#include "remoting/host/security_key/security_key_auth_handler.h"
#include "remoting/proto/control.pb.h"
#include "remoting/proto/event.pb.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_frame.h"
#include "third_party/webrtc/modules/desktop_capture/mouse_cursor_monitor.h"
#include "ui/events/event.h"

namespace base {
class TimeDelta;
}  // namespace base

namespace remoting {

class MockDesktopEnvironment : public DesktopEnvironment {
 public:
  MockDesktopEnvironment();
  ~MockDesktopEnvironment() override;

  // TODO(yuweih): Use new MOCK_METHOD style and remove the Ptr dance:
  //   MOCK_METHOD(ReturnType, MethodName, (Args...), (const, override))
  MOCK_METHOD0(CreateActionExecutorPtr, ActionExecutor*());
  MOCK_METHOD0(CreateAudioCapturerPtr, AudioCapturer*());
  MOCK_METHOD0(CreateInputInjectorPtr, InputInjector*());
  MOCK_METHOD0(CreateScreenControlsPtr, ScreenControls*());
  MOCK_METHOD0(CreateVideoCapturerPtr, webrtc::DesktopCapturer*());
  MOCK_METHOD0(CreateMouseCursorMonitorPtr, webrtc::MouseCursorMonitor*());
  MOCK_METHOD1(
      CreateKeyboardLayoutMonitorPtr,
      KeyboardLayoutMonitor*(
          base::RepeatingCallback<void(const protocol::KeyboardLayout&)>));
  MOCK_METHOD0(CreateFileOperationsPtr, FileOperations*());
  MOCK_CONST_METHOD0(GetCapabilities, std::string());
  MOCK_METHOD1(SetCapabilities, void(const std::string&));
  MOCK_CONST_METHOD0(GetDesktopSessionId, uint32_t());
  MOCK_METHOD0(CreateComposingVideoCapturerPtr,
               DesktopAndCursorConditionalComposer*());

  MOCK_METHOD(std::unique_ptr<UrlForwarderConfigurator>,
              CreateUrlForwarderConfigurator,
              (),
              (override));

  // DesktopEnvironment implementation.
  std::unique_ptr<ActionExecutor> CreateActionExecutor() override;
  std::unique_ptr<AudioCapturer> CreateAudioCapturer() override;
  std::unique_ptr<InputInjector> CreateInputInjector() override;
  std::unique_ptr<ScreenControls> CreateScreenControls() override;
  std::unique_ptr<webrtc::DesktopCapturer> CreateVideoCapturer() override;
  std::unique_ptr<webrtc::MouseCursorMonitor> CreateMouseCursorMonitor()
      override;
  std::unique_ptr<KeyboardLayoutMonitor> CreateKeyboardLayoutMonitor(
      base::RepeatingCallback<void(const protocol::KeyboardLayout&)> callback)
      override;
  std::unique_ptr<FileOperations> CreateFileOperations() override;
  std::unique_ptr<DesktopAndCursorConditionalComposer>
  CreateComposingVideoCapturer() override;
};

class MockClientSessionControl : public ClientSessionControl {
 public:
  MockClientSessionControl();

  MockClientSessionControl(const MockClientSessionControl&) = delete;
  MockClientSessionControl& operator=(const MockClientSessionControl&) = delete;

  ~MockClientSessionControl() override;

  MOCK_CONST_METHOD0(client_jid, const std::string&());
  MOCK_METHOD1(DisconnectSession, void(protocol::ErrorCode error));
  MOCK_METHOD2(OnLocalPointerMoved,
               void(const webrtc::DesktopVector&, ui::EventType));
  MOCK_METHOD1(OnLocalKeyPressed, void(uint32_t));
  MOCK_METHOD1(SetDisableInputs, void(bool));
  MOCK_METHOD0(ResetVideoPipeline, void());
  MOCK_METHOD1(OnDesktopDisplayChanged,
               void(std::unique_ptr<protocol::VideoLayout>));
};

class MockClientSessionDetails : public ClientSessionDetails {
 public:
  MockClientSessionDetails();

  MockClientSessionDetails(const MockClientSessionDetails&) = delete;
  MockClientSessionDetails& operator=(const MockClientSessionDetails&) = delete;

  ~MockClientSessionDetails() override;

  MOCK_METHOD0(session_control, ClientSessionControl*());
  MOCK_CONST_METHOD0(desktop_session_id, uint32_t());
};

class MockClientSessionEvents : public ClientSessionEvents {
 public:
  MockClientSessionEvents();
  ~MockClientSessionEvents() override;

  MOCK_METHOD(void, OnDesktopAttached, (uint32_t session_id), (override));
  MOCK_METHOD(void, OnDesktopDetached, (), (override));
};

class MockClientSessionEventHandler : public ClientSession::EventHandler {
 public:
  MockClientSessionEventHandler();

  MockClientSessionEventHandler(const MockClientSessionEventHandler&) = delete;
  MockClientSessionEventHandler& operator=(
      const MockClientSessionEventHandler&) = delete;

  ~MockClientSessionEventHandler() override;

  MOCK_METHOD1(OnSessionAuthenticating, void(ClientSession* client));
  MOCK_METHOD1(OnSessionAuthenticated, void(ClientSession* client));
  MOCK_METHOD1(OnSessionChannelsConnected, void(ClientSession* client));
  MOCK_METHOD1(OnSessionAuthenticationFailed, void(ClientSession* client));
  MOCK_METHOD1(OnSessionClosed, void(ClientSession* client));
  MOCK_METHOD3(OnSessionRouteChange, void(
      ClientSession* client,
      const std::string& channel_name,
      const protocol::TransportRoute& route));
};

class MockDesktopEnvironmentFactory : public DesktopEnvironmentFactory {
 public:
  MockDesktopEnvironmentFactory();

  MockDesktopEnvironmentFactory(const MockDesktopEnvironmentFactory&) = delete;
  MockDesktopEnvironmentFactory& operator=(
      const MockDesktopEnvironmentFactory&) = delete;

  ~MockDesktopEnvironmentFactory() override;

  MOCK_METHOD0(CreatePtr, DesktopEnvironment*());
  MOCK_CONST_METHOD0(SupportsAudioCapture, bool());

  std::unique_ptr<DesktopEnvironment> Create(
      base::WeakPtr<ClientSessionControl> client_session_control,
      base::WeakPtr<ClientSessionEvents> client_session_events,
      const DesktopEnvironmentOptions& options) override;
};

class MockInputInjector : public InputInjector {
 public:
  MockInputInjector();

  MockInputInjector(const MockInputInjector&) = delete;
  MockInputInjector& operator=(const MockInputInjector&) = delete;

  ~MockInputInjector() override;

  MOCK_METHOD1(InjectClipboardEvent,
               void(const protocol::ClipboardEvent& event));
  MOCK_METHOD1(InjectKeyEvent, void(const protocol::KeyEvent& event));
  MOCK_METHOD1(InjectTextEvent, void(const protocol::TextEvent& event));
  MOCK_METHOD1(InjectMouseEvent, void(const protocol::MouseEvent& event));
  MOCK_METHOD1(InjectTouchEvent, void(const protocol::TouchEvent& event));
  MOCK_METHOD1(StartPtr,
               void(protocol::ClipboardStub* client_clipboard));

  void Start(
      std::unique_ptr<protocol::ClipboardStub> client_clipboard) override;
};

class MockHostStatusObserver : public HostStatusObserver {
 public:
  MockHostStatusObserver();
  ~MockHostStatusObserver() override;

  MOCK_METHOD1(OnAccessDenied, void(const std::string& jid));
  MOCK_METHOD1(OnClientAuthenticated, void(const std::string& jid));
  MOCK_METHOD1(OnClientConnected, void(const std::string& jid));
  MOCK_METHOD1(OnClientDisconnected, void(const std::string& jid));
  MOCK_METHOD3(OnClientRouteChange,
               void(const std::string& jid,
                    const std::string& channel_name,
                    const protocol::TransportRoute& route));
  MOCK_METHOD1(OnStart, void(const std::string& xmpp_login));
  MOCK_METHOD0(OnShutdown, void());
};

class MockSecurityKeyAuthHandler : public SecurityKeyAuthHandler {
 public:
  MockSecurityKeyAuthHandler();

  MockSecurityKeyAuthHandler(const MockSecurityKeyAuthHandler&) = delete;
  MockSecurityKeyAuthHandler& operator=(const MockSecurityKeyAuthHandler&) =
      delete;

  ~MockSecurityKeyAuthHandler() override;

  MOCK_METHOD0(CreateSecurityKeyConnection, void());
  MOCK_CONST_METHOD1(IsValidConnectionId, bool(int connection_id));
  MOCK_METHOD2(SendClientResponse,
               void(int connection_id, const std::string& response));
  MOCK_METHOD1(SendErrorAndCloseConnection, void(int connection_id));
  MOCK_CONST_METHOD0(GetActiveConnectionCountForTest, size_t());
  MOCK_METHOD1(SetRequestTimeoutForTest, void(base::TimeDelta timeout));

  void SetSendMessageCallback(
      const SecurityKeyAuthHandler::SendMessageCallback& callback) override;
  const SecurityKeyAuthHandler::SendMessageCallback& GetSendMessageCallback();

 private:
  SecurityKeyAuthHandler::SendMessageCallback callback_;
};

class MockMouseCursorMonitor : public webrtc::MouseCursorMonitor {
 public:
  MockMouseCursorMonitor();

  MockMouseCursorMonitor(const MockMouseCursorMonitor&) = delete;
  MockMouseCursorMonitor& operator=(const MockMouseCursorMonitor&) = delete;

  ~MockMouseCursorMonitor() override;

  MOCK_METHOD2(Init, void(Callback* callback, Mode mode));
  MOCK_METHOD0(Capture, void());
};

class MockUrlForwarderConfigurator final : public UrlForwarderConfigurator {
 public:
  MockUrlForwarderConfigurator();
  ~MockUrlForwarderConfigurator() override;

  MOCK_METHOD(void,
              IsUrlForwarderSetUp,
              (IsUrlForwarderSetUpCallback callback),
              (override));
  MOCK_METHOD(void,
              SetUpUrlForwarder,
              (const SetUpUrlForwarderCallback& callback),
              (override));
};

class MockChromotingSessionServices : public mojom::ChromotingSessionServices {
 public:
  MockChromotingSessionServices();
  ~MockChromotingSessionServices() override;

  MOCK_METHOD(void,
              BindRemoteUrlOpener,
              (mojo::PendingReceiver<mojom::RemoteUrlOpener> receiver),
              (override));
  MOCK_METHOD(void,
              BindWebAuthnProxy,
              (mojo::PendingReceiver<mojom::WebAuthnProxy> receiver),
              (override));
};

class MockChromotingHostServicesProvider
    : public ChromotingHostServicesProvider {
 public:
  MockChromotingHostServicesProvider();
  ~MockChromotingHostServicesProvider() override;

  MOCK_METHOD(mojom::ChromotingSessionServices*,
              GetSessionServices,
              (),
              (const, override));
};

}  // namespace remoting

#endif  // REMOTING_HOST_HOST_MOCK_OBJECTS_H_
