// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/ipc_desktop_environment.h"

#include <stdint.h>

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_pump_type.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "build/build_config.h"
#include "ipc/ipc_channel.h"
#include "ipc/ipc_channel_proxy.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_message.h"
#include "ipc/ipc_platform_file.h"
#include "remoting/base/auto_thread.h"
#include "remoting/base/auto_thread_task_runner.h"
#include "remoting/base/constants.h"
#include "remoting/host/chromoting_messages.h"
#include "remoting/host/desktop_process.h"
#include "remoting/host/desktop_session.h"
#include "remoting/host/desktop_session_connector.h"
#include "remoting/host/desktop_session_proxy.h"
#include "remoting/host/fake_keyboard_layout_monitor.h"
#include "remoting/host/fake_mouse_cursor_monitor.h"
#include "remoting/host/host_mock_objects.h"
#include "remoting/host/mojom/desktop_session.mojom.h"
#include "remoting/protocol/fake_desktop_capturer.h"
#include "remoting/protocol/protocol_mock_objects.h"
#include "remoting/protocol/test_event_matchers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capturer.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_geometry.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_region.h"

using base::test::RunCallback;
using base::test::RunOnceCallback;
using base::test::RunOnceClosure;
using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::AtMost;
using testing::ByMove;
using testing::DeleteArg;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::ReturnRef;

namespace remoting {

using protocol::test::EqualsTouchEvent;
using protocol::test::EqualsTouchEventTypeAndId;

using SetUpUrlForwarderResponse =
    protocol::UrlForwarderControl::SetUpUrlForwarderResponse;

namespace {

class MockScreenCapturerCallback : public webrtc::DesktopCapturer::Callback {
 public:
  MockScreenCapturerCallback() = default;

  MockScreenCapturerCallback(const MockScreenCapturerCallback&) = delete;
  MockScreenCapturerCallback& operator=(const MockScreenCapturerCallback&) =
      delete;

  ~MockScreenCapturerCallback() override = default;

  MOCK_METHOD2(OnCaptureResultPtr,
               void(webrtc::DesktopCapturer::Result result,
                    std::unique_ptr<webrtc::DesktopFrame>* frame));
  void OnCaptureResult(webrtc::DesktopCapturer::Result result,
                       std::unique_ptr<webrtc::DesktopFrame> frame) override {
    OnCaptureResultPtr(result, &frame);
  }
};

// Receives messages sent from the network process to the daemon.
class FakeDaemonSender : public IPC::Sender {
 public:
  FakeDaemonSender() = default;

  FakeDaemonSender(const FakeDaemonSender&) = delete;
  FakeDaemonSender& operator=(const FakeDaemonSender&) = delete;

  ~FakeDaemonSender() override = default;

  // IPC::Sender implementation.
  bool Send(IPC::Message* message) override;

  MOCK_METHOD3(ConnectTerminal, void(int, const ScreenResolution&, bool));
  MOCK_METHOD1(DisconnectTerminal, void(int));
  MOCK_METHOD2(SetScreenResolution, void(int, const ScreenResolution&));

 private:
  void OnMessageReceived(const IPC::Message& message);
};

// Receives messages sent from the desktop process to the daemon.
class MockDaemonListener : public IPC::Listener,
                           public mojom::DesktopSessionRequestHandler {
 public:
  MockDaemonListener() = default;

  MockDaemonListener(const MockDaemonListener&) = delete;
  MockDaemonListener& operator=(const MockDaemonListener&) = delete;

  ~MockDaemonListener() override = default;

  bool OnMessageReceived(const IPC::Message& message) override;
  void OnAssociatedInterfaceRequest(
      const std::string& interface_name,
      mojo::ScopedInterfaceEndpointHandle handle) override;

  MOCK_METHOD(void,
              ConnectDesktopChannel,
              (mojo::ScopedMessagePipeHandle handle),
              (override));
  MOCK_METHOD(void, InjectSecureAttentionSequence, (), (override));
  MOCK_METHOD(void, CrashNetworkProcess, (), (override));
  MOCK_METHOD(void, OnChannelConnected, (int32_t), (override));
  MOCK_METHOD(void, OnChannelError, (), (override));

  void Disconnect();

 private:
  mojo::AssociatedReceiver<mojom::DesktopSessionRequestHandler>
      desktop_session_request_handler_{this};
};

bool FakeDaemonSender::Send(IPC::Message* message) {
  OnMessageReceived(*message);
  delete message;
  return true;
}

void FakeDaemonSender::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(FakeDaemonSender, message)
    IPC_MESSAGE_HANDLER(ChromotingNetworkHostMsg_ConnectTerminal,
                        ConnectTerminal)
    IPC_MESSAGE_HANDLER(ChromotingNetworkHostMsg_DisconnectTerminal,
                        DisconnectTerminal)
    IPC_MESSAGE_HANDLER(ChromotingNetworkDaemonMsg_SetScreenResolution,
                        SetScreenResolution)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  EXPECT_TRUE(handled);
}

bool MockDaemonListener::OnMessageReceived(const IPC::Message& message) {
  ADD_FAILURE() << "Unexpected call to OnMessageReceived()";
  return false;
}

void MockDaemonListener::OnAssociatedInterfaceRequest(
    const std::string& interface_name,
    mojo::ScopedInterfaceEndpointHandle handle) {
  EXPECT_EQ(mojom::DesktopSessionRequestHandler::Name_, interface_name);
  mojo::PendingAssociatedReceiver<mojom::DesktopSessionRequestHandler>
      pending_receiver(std::move(handle));
  desktop_session_request_handler_.Bind(std::move(pending_receiver));
}

void MockDaemonListener::Disconnect() {
  desktop_session_request_handler_.reset();
}

}  // namespace

class IpcDesktopEnvironmentTest : public testing::Test {
 public:
  IpcDesktopEnvironmentTest();
  ~IpcDesktopEnvironmentTest() override;

  void SetUp() override;
  void TearDown() override;

  void ConnectTerminal(int terminal_id,
                       const ScreenResolution& resolution,
                       bool virtual_terminal);
  void DisconnectTerminal(int terminal_id);

  // Creates a DesktopEnvironment with a fake webrtc::DesktopCapturer, to mock
  // DesktopEnvironmentFactory::Create().
  DesktopEnvironment* CreateDesktopEnvironment();

  // Creates a dummy InputInjector, to mock
  // DesktopEnvironment::CreateInputInjector().
  InputInjector* CreateInputInjector();

  // Creates a fake webrtc::DesktopCapturer, to mock
  // DesktopEnvironment::CreateVideoCapturer().
  webrtc::DesktopCapturer* CreateVideoCapturer();

  // Creates a MockMouseCursorMonitor, to mock
  // DesktopEnvironment::CreateMouseCursorMonitor
  webrtc::MouseCursorMonitor* CreateMouseCursorMonitor();

  // Creates a FakeKeyboardLayoutMonitor to mock
  // DesktopEnvironment::CreateKeyboardLayoutMonitor
  KeyboardLayoutMonitor* CreateKeyboardLayoutMonitor(
      base::RepeatingCallback<void(const protocol::KeyboardLayout&)> callback);

  void DeleteDesktopEnvironment();

  // Forwards |event| to |clipboard_stub_|.
  void ReflectClipboardEvent(const protocol::ClipboardEvent& event);

 protected:
  // Creates and starts an instance of desktop process object.
  void CreateDesktopProcess();

  // Destroys the desktop process object created by CreateDesktopProcess().
  void DestoyDesktopProcess();

  // Creates a new remote URL forwarder configurator for the desktop process.
  void ResetRemoteUrlForwarderConfigurator();

  void OnDisconnectCallback();

  // Invoked when ConnectDesktopChannel() is called over IPC.
  void ConnectDesktopChannel(mojo::ScopedMessagePipeHandle desktop_pipe);

  // Runs until there are no references to |task_runner_|. Calls after the main
  // loop has been run are no-op.
  void RunMainLoopUntilDone();

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::UI};

  // Runs until |desktop_session_proxy_| is connected to the desktop.
  std::unique_ptr<base::RunLoop> setup_run_loop_;

  scoped_refptr<AutoThreadTaskRunner> task_runner_;
  scoped_refptr<AutoThreadTaskRunner> io_task_runner_;

  std::string client_jid_;

  // Clipboard stub that receives clipboard events from the desktop process.
  raw_ptr<protocol::ClipboardStub> clipboard_stub_;

  // The daemons's end of the daemon-to-desktop channel.
  std::unique_ptr<IPC::ChannelProxy> desktop_channel_;

  // Delegate that is passed to |desktop_channel_|.
  MockDaemonListener desktop_listener_;

  FakeDaemonSender daemon_channel_;

  std::unique_ptr<IpcDesktopEnvironmentFactory> desktop_environment_factory_;
  std::unique_ptr<DesktopEnvironment> desktop_environment_;

  // The IPC input injector.
  std::unique_ptr<InputInjector> input_injector_;

  // The IPC screen controls.
  std::unique_ptr<ScreenControls> screen_controls_;

  // The IPC screen capturer.
  std::unique_ptr<webrtc::DesktopCapturer> video_capturer_;

  // Represents the desktop process running in a user session.
  std::unique_ptr<DesktopProcess> desktop_process_;

  // Input injector owned by |desktop_process_|.
  raw_ptr<MockInputInjector> remote_input_injector_;

  // Will be transferred to the caller of
  // MockDesktopEnvironment::CreateUrlForwarderConfigurator().
  // We create the configurator in advance to allow setting expectations before
  // the desktop process is being created, during which the configurator will be
  // used.
  std::unique_ptr<MockUrlForwarderConfigurator>
      owned_remote_url_forwarder_configurator_;
  raw_ptr<MockUrlForwarderConfigurator> remote_url_forwarder_configurator_;
  std::unique_ptr<UrlForwarderConfigurator> url_forwarder_configurator_;

  // The last |terminal_id| passed to ConnectTermina();
  int terminal_id_;

  MockScreenCapturerCallback desktop_capturer_callback_;

  MockClientSessionControl client_session_control_;
  base::WeakPtrFactory<ClientSessionControl> client_session_control_factory_;

  MockClientSessionEvents client_session_events_;
  base::WeakPtrFactory<MockClientSessionEvents> client_session_events_factory_{
      &client_session_events_};

 private:
  // Runs until there are no references to |task_runner_|.
  base::RunLoop main_run_loop_;
};

IpcDesktopEnvironmentTest::IpcDesktopEnvironmentTest()
    : client_jid_("user@domain/rest-of-jid"),
      clipboard_stub_(nullptr),
      remote_input_injector_(nullptr),
      terminal_id_(-1),
      client_session_control_factory_(&client_session_control_) {
}

IpcDesktopEnvironmentTest::~IpcDesktopEnvironmentTest() = default;

void IpcDesktopEnvironmentTest::SetUp() {
  // Arrange to run |message_loop_| until no components depend on it.
  task_runner_ =
      new AutoThreadTaskRunner(task_environment_.GetMainThreadTaskRunner(),
                               main_run_loop_.QuitClosure());

  io_task_runner_ = AutoThread::CreateWithType("IPC thread", task_runner_,
                                               base::MessagePumpType::IO);

  setup_run_loop_ = std::make_unique<base::RunLoop>();

  // Set expectation that the DaemonProcess will send DesktopAttached message
  // once it is ready.
  EXPECT_CALL(desktop_listener_, OnChannelConnected(_))
      .Times(AnyNumber());
  EXPECT_CALL(desktop_listener_, ConnectDesktopChannel(_))
      .Times(AnyNumber())
      .WillRepeatedly([&](mojo::ScopedMessagePipeHandle desktop_pipe) {
        ConnectDesktopChannel(std::move(desktop_pipe));
      });
  EXPECT_CALL(desktop_listener_, OnChannelError())
      .Times(AnyNumber())
      .WillOnce(Invoke(this,
                       &IpcDesktopEnvironmentTest::DestoyDesktopProcess));

  // Intercept requests to connect and disconnect a terminal.
  EXPECT_CALL(daemon_channel_, ConnectTerminal(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(this,
                             &IpcDesktopEnvironmentTest::ConnectTerminal));
  EXPECT_CALL(daemon_channel_, DisconnectTerminal(_))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(this,
                             &IpcDesktopEnvironmentTest::DisconnectTerminal));

  EXPECT_CALL(client_session_control_, client_jid())
      .Times(AnyNumber())
      .WillRepeatedly(ReturnRef(client_jid_));
  EXPECT_CALL(client_session_control_, DisconnectSession(_))
      .Times(AnyNumber())
      .WillRepeatedly(InvokeWithoutArgs(
          this, &IpcDesktopEnvironmentTest::DeleteDesktopEnvironment));
  EXPECT_CALL(client_session_control_, OnLocalPointerMoved(_, _)).Times(0);
  EXPECT_CALL(client_session_control_, SetDisableInputs(_))
      .Times(0);

  // Create a desktop environment instance.
  desktop_environment_factory_ = std::make_unique<IpcDesktopEnvironmentFactory>(
      task_runner_, task_runner_, io_task_runner_, &daemon_channel_);
  desktop_environment_ = desktop_environment_factory_->Create(
      client_session_control_factory_.GetWeakPtr(),
      client_session_events_factory_.GetWeakPtr(), DesktopEnvironmentOptions());

  screen_controls_ = desktop_environment_->CreateScreenControls();

  // Create the input injector.
  input_injector_ = desktop_environment_->CreateInputInjector();

  // Create the screen capturer.
  video_capturer_ =
      desktop_environment_->CreateVideoCapturer();

  desktop_environment_->SetCapabilities(std::string());

  url_forwarder_configurator_ =
      desktop_environment_->CreateUrlForwarderConfigurator();
  ResetRemoteUrlForwarderConfigurator();
}

void IpcDesktopEnvironmentTest::TearDown() {
  RunMainLoopUntilDone();
}

void IpcDesktopEnvironmentTest::ConnectTerminal(
    int terminal_id,
    const ScreenResolution& resolution,
    bool virtual_terminal) {
  EXPECT_NE(terminal_id_, terminal_id);

  terminal_id_ = terminal_id;
  CreateDesktopProcess();
}

void IpcDesktopEnvironmentTest::DisconnectTerminal(int terminal_id) {
  EXPECT_EQ(terminal_id_, terminal_id);

  // The IPC desktop environment is fully destroyed now. Release the remaining
  // task runners.
  desktop_environment_factory_.reset();
}

DesktopEnvironment* IpcDesktopEnvironmentTest::CreateDesktopEnvironment() {
  MockDesktopEnvironment* desktop_environment = new MockDesktopEnvironment();
  EXPECT_CALL(*desktop_environment, CreateAudioCapturerPtr())
      .Times(0);
  EXPECT_CALL(*desktop_environment, CreateInputInjectorPtr())
      .Times(AtMost(1))
      .WillOnce(Invoke(
          this, &IpcDesktopEnvironmentTest::CreateInputInjector));
  EXPECT_CALL(*desktop_environment, CreateScreenControlsPtr())
      .Times(AtMost(1));
  EXPECT_CALL(*desktop_environment, CreateVideoCapturerPtr())
      .Times(AtMost(1))
      .WillOnce(Invoke(
          this, &IpcDesktopEnvironmentTest::CreateVideoCapturer));
  EXPECT_CALL(*desktop_environment, CreateMouseCursorMonitorPtr())
      .Times(AtMost(1))
      .WillOnce(Invoke(
          this, &IpcDesktopEnvironmentTest::CreateMouseCursorMonitor));
  EXPECT_CALL(*desktop_environment, CreateKeyboardLayoutMonitorPtr(_))
      .Times(AtMost(1))
      .WillOnce(Invoke(
          this, &IpcDesktopEnvironmentTest::CreateKeyboardLayoutMonitor));
  EXPECT_CALL(*desktop_environment, GetCapabilities())
      .Times(AtMost(1));
  EXPECT_CALL(*desktop_environment, SetCapabilities(_))
      .Times(AtMost(1));
  DCHECK(owned_remote_url_forwarder_configurator_);
  EXPECT_CALL(*desktop_environment, CreateUrlForwarderConfigurator())
      .Times(AtMost(1))
      .WillOnce(
          Return(ByMove(std::move(owned_remote_url_forwarder_configurator_))));

  // Let tests know that the remote desktop environment is created.
  task_environment_.GetMainThreadTaskRunner()->PostTask(
      FROM_HERE, setup_run_loop_->QuitClosure());

  return desktop_environment;
}

InputInjector* IpcDesktopEnvironmentTest::CreateInputInjector() {
  EXPECT_TRUE(remote_input_injector_ == nullptr);
  remote_input_injector_ = new testing::StrictMock<MockInputInjector>();

  EXPECT_CALL(*remote_input_injector_, StartPtr(_));
  return remote_input_injector_;
}

webrtc::DesktopCapturer* IpcDesktopEnvironmentTest::CreateVideoCapturer() {
  return new protocol::FakeDesktopCapturer();
}

webrtc::MouseCursorMonitor*
IpcDesktopEnvironmentTest::CreateMouseCursorMonitor() {
  return new FakeMouseCursorMonitor();
}

KeyboardLayoutMonitor* IpcDesktopEnvironmentTest::CreateKeyboardLayoutMonitor(
    base::RepeatingCallback<void(const protocol::KeyboardLayout&)> callback) {
  return new FakeKeyboardLayoutMonitor();
}

void IpcDesktopEnvironmentTest::DeleteDesktopEnvironment() {
  input_injector_.reset();
  screen_controls_.reset();
  video_capturer_.reset();
  url_forwarder_configurator_.reset();

  // Trigger DisconnectTerminal().
  desktop_environment_.reset();
}

void IpcDesktopEnvironmentTest::ReflectClipboardEvent(
    const protocol::ClipboardEvent& event) {
  clipboard_stub_->InjectClipboardEvent(event);
}

void IpcDesktopEnvironmentTest::CreateDesktopProcess() {
  EXPECT_TRUE(task_runner_.get());
  EXPECT_TRUE(io_task_runner_.get());

  // Create the daemon end of the daemon-to-desktop channel.
  mojo::MessagePipe pipe;
  desktop_channel_ = IPC::ChannelProxy::Create(
      pipe.handle0.release(), IPC::Channel::MODE_SERVER, &desktop_listener_,
      io_task_runner_.get(), base::ThreadTaskRunnerHandle::Get());

  // Create and start the desktop process.
  desktop_process_ = std::make_unique<DesktopProcess>(
      task_runner_, io_task_runner_, io_task_runner_, std::move(pipe.handle1));

  std::unique_ptr<MockDesktopEnvironmentFactory> desktop_environment_factory(
      new MockDesktopEnvironmentFactory());
  EXPECT_CALL(*desktop_environment_factory, CreatePtr())
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          this, &IpcDesktopEnvironmentTest::CreateDesktopEnvironment));
  EXPECT_CALL(*desktop_environment_factory, SupportsAudioCapture())
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));

  EXPECT_TRUE(desktop_process_->Start(std::move(desktop_environment_factory)));
}

void IpcDesktopEnvironmentTest::DestoyDesktopProcess() {
  desktop_channel_.reset();
  if (desktop_process_) {
    desktop_process_->OnChannelError();
    desktop_process_.reset();
  }
  desktop_listener_.Disconnect();
  remote_input_injector_ = nullptr;
}

void IpcDesktopEnvironmentTest::ResetRemoteUrlForwarderConfigurator() {
  owned_remote_url_forwarder_configurator_ =
      std::make_unique<MockUrlForwarderConfigurator>();
  remote_url_forwarder_configurator_ =
      owned_remote_url_forwarder_configurator_.get();
  ON_CALL(*remote_url_forwarder_configurator_, IsUrlForwarderSetUp(_))
      .WillByDefault(RunOnceCallback<0>(false));
}

void IpcDesktopEnvironmentTest::OnDisconnectCallback() {
  DeleteDesktopEnvironment();
}

void IpcDesktopEnvironmentTest::ConnectDesktopChannel(
    mojo::ScopedMessagePipeHandle desktop_pipe) {
  // Instruct DesktopSessionProxy to connect to the network-to-desktop pipe.
  desktop_environment_factory_->OnDesktopSessionAgentAttached(
      terminal_id_, /*session_id=*/0, desktop_pipe.release());
}

void IpcDesktopEnvironmentTest::RunMainLoopUntilDone() {
  bool should_run_loop = task_runner_ != nullptr;
  task_runner_ = nullptr;
  io_task_runner_ = nullptr;
  if (should_run_loop) {
    main_run_loop_.Run();
  }
}

// Runs until the desktop is attached and exits immediately after that.
TEST_F(IpcDesktopEnvironmentTest, Basic) {
  std::unique_ptr<protocol::MockClipboardStub> clipboard_stub(
      new protocol::MockClipboardStub());
  EXPECT_CALL(*clipboard_stub, InjectClipboardEvent(_))
      .Times(0);

  // Start the input injector and screen capturer.
  input_injector_->Start(std::move(clipboard_stub));

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  // Stop the test.
  DeleteDesktopEnvironment();
}

// Check touchEvents capability is set when the desktop environment can
// inject touch events.
TEST_F(IpcDesktopEnvironmentTest, TouchEventsCapabilities) {
  // Create an environment with multi touch enabled.
  desktop_environment_ = desktop_environment_factory_->Create(
      client_session_control_factory_.GetWeakPtr(),
      client_session_events_factory_.GetWeakPtr(), DesktopEnvironmentOptions());

  std::unique_ptr<protocol::MockClipboardStub> clipboard_stub(
      new protocol::MockClipboardStub());
  EXPECT_CALL(*clipboard_stub, InjectClipboardEvent(_))
      .Times(0);

  std::string expected_capabilities = "rateLimitResizeRequests";
  if (InputInjector::SupportsTouchEvents())
    expected_capabilities += " touchEvents";

  EXPECT_EQ(expected_capabilities, desktop_environment_->GetCapabilities());

  // Start the input injector and screen capturer.
  input_injector_->Start(std::move(clipboard_stub));

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  // Stop the test.
  DeleteDesktopEnvironment();
}

// Tests that the video capturer receives a frame over IPC.
TEST_F(IpcDesktopEnvironmentTest, CaptureFrame) {
  std::unique_ptr<protocol::MockClipboardStub> clipboard_stub(
      new protocol::MockClipboardStub());
  EXPECT_CALL(*clipboard_stub, InjectClipboardEvent(_))
      .Times(0);

  // Start the input injector and screen capturer.
  input_injector_->Start(std::move(clipboard_stub));
  video_capturer_->Start(&desktop_capturer_callback_);

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  // Stop the test when the first frame is captured.
  EXPECT_CALL(desktop_capturer_callback_, OnCaptureResultPtr(_, _))
      .WillOnce(InvokeWithoutArgs(
          this, &IpcDesktopEnvironmentTest::DeleteDesktopEnvironment));

  // Capture a single frame.
  video_capturer_->CaptureFrame();
}

// Tests that attaching to a new desktop works.
TEST_F(IpcDesktopEnvironmentTest, Reattach) {
  std::unique_ptr<protocol::MockClipboardStub> clipboard_stub(
      new protocol::MockClipboardStub());
  EXPECT_CALL(*clipboard_stub, InjectClipboardEvent(_))
      .Times(0);

  // Start the input injector and screen capturer.
  input_injector_->Start(std::move(clipboard_stub));
  video_capturer_->Start(&desktop_capturer_callback_);

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  // Create and start a new desktop process object.
  setup_run_loop_ = std::make_unique<base::RunLoop>();
  DestoyDesktopProcess();
  ResetRemoteUrlForwarderConfigurator();
  CreateDesktopProcess();
  setup_run_loop_->Run();

  // Stop the test.
  DeleteDesktopEnvironment();
}

// Tests injection of clipboard events.
TEST_F(IpcDesktopEnvironmentTest, InjectClipboardEvent) {
  std::unique_ptr<protocol::MockClipboardStub> clipboard_stub(
      new protocol::MockClipboardStub());
  clipboard_stub_ = clipboard_stub.get();

  // Stop the test when a clipboard event is received from the desktop process.
  EXPECT_CALL(*clipboard_stub, InjectClipboardEvent(_))
      .Times(1)
      .WillOnce(InvokeWithoutArgs(
          this, &IpcDesktopEnvironmentTest::DeleteDesktopEnvironment));

  // Start the input injector and screen capturer.
  input_injector_->Start(std::move(clipboard_stub));
  video_capturer_->Start(&desktop_capturer_callback_);

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  // Expect a single clipboard event.
  EXPECT_CALL(*remote_input_injector_, InjectClipboardEvent(_))
      .Times(1)
      .WillOnce(Invoke(this,
                       &IpcDesktopEnvironmentTest::ReflectClipboardEvent));

  // Send a clipboard event.
  protocol::ClipboardEvent event;
  event.set_mime_type(kMimeTypeTextUtf8);
  event.set_data("a");
  input_injector_->InjectClipboardEvent(event);
}

// Tests injection of key events.
TEST_F(IpcDesktopEnvironmentTest, InjectKeyEvent) {
  std::unique_ptr<protocol::MockClipboardStub> clipboard_stub(
      new protocol::MockClipboardStub());
  EXPECT_CALL(*clipboard_stub, InjectClipboardEvent(_))
      .Times(0);

  // Start the input injector and screen capturer.
  input_injector_->Start(std::move(clipboard_stub));
  video_capturer_->Start(&desktop_capturer_callback_);

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  // Expect a single key event.
  EXPECT_CALL(*remote_input_injector_, InjectKeyEvent(_))
      .Times(AtLeast(1))
      .WillRepeatedly(InvokeWithoutArgs(
          this, &IpcDesktopEnvironmentTest::DeleteDesktopEnvironment));

  // Send a key event.
  protocol::KeyEvent event;
  event.set_usb_keycode(0x070004);
  event.set_pressed(true);
  input_injector_->InjectKeyEvent(event);
}

// Tests injection of text events.
TEST_F(IpcDesktopEnvironmentTest, InjectTextEvent) {
  std::unique_ptr<protocol::MockClipboardStub> clipboard_stub(
      new protocol::MockClipboardStub());
  EXPECT_CALL(*clipboard_stub, InjectClipboardEvent(_))
      .Times(0);

  // Start the input injector and screen capturer.
  input_injector_->Start(std::move(clipboard_stub));
  video_capturer_->Start(&desktop_capturer_callback_);

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  // Expect a single text event.
  EXPECT_CALL(*remote_input_injector_, InjectTextEvent(_))
      .Times(AtLeast(1))
      .WillRepeatedly(InvokeWithoutArgs(
          this, &IpcDesktopEnvironmentTest::DeleteDesktopEnvironment));

  // Send a text event.
  protocol::TextEvent event;
  event.set_text("hello");
  input_injector_->InjectTextEvent(event);
}

// Tests injection of mouse events.
TEST_F(IpcDesktopEnvironmentTest, InjectMouseEvent) {
  std::unique_ptr<protocol::MockClipboardStub> clipboard_stub(
      new protocol::MockClipboardStub());
  EXPECT_CALL(*clipboard_stub, InjectClipboardEvent(_))
      .Times(0);

  // Start the input injector and screen capturer.
  input_injector_->Start(std::move(clipboard_stub));
  video_capturer_->Start(&desktop_capturer_callback_);

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  // Expect a single mouse event.
  EXPECT_CALL(*remote_input_injector_, InjectMouseEvent(_))
      .Times(1)
      .WillOnce(InvokeWithoutArgs(
          this, &IpcDesktopEnvironmentTest::DeleteDesktopEnvironment));

  // Send a mouse event.
  protocol::MouseEvent event;
  event.set_x(0);
  event.set_y(0);
  input_injector_->InjectMouseEvent(event);
}

// Tests injection of touch events.
TEST_F(IpcDesktopEnvironmentTest, InjectTouchEvent) {
  std::unique_ptr<protocol::MockClipboardStub> clipboard_stub(
      new protocol::MockClipboardStub());
  EXPECT_CALL(*clipboard_stub, InjectClipboardEvent(_))
      .Times(0);

  // Start the input injector and screen capturer.
  input_injector_->Start(std::move(clipboard_stub));
  video_capturer_->Start(&desktop_capturer_callback_);

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  protocol::TouchEvent event;
  event.set_event_type(protocol::TouchEvent::TOUCH_POINT_START);
  protocol::TouchEventPoint* point = event.add_touch_points();
  point->set_id(0u);
  point->set_x(0.0f);
  point->set_y(0.0f);
  point->set_radius_x(0.0f);
  point->set_radius_y(0.0f);
  point->set_angle(0.0f);
  point->set_pressure(0.0f);

  ON_CALL(*remote_input_injector_, InjectTouchEvent(_))
      .WillByDefault(InvokeWithoutArgs(
          this, &IpcDesktopEnvironmentTest::DeleteDesktopEnvironment));

  InSequence s;
  // Expect that the event gets propagated to remote_input_injector_.
  // And one more call for ReleaseAll().
  EXPECT_CALL(*remote_input_injector_,
              InjectTouchEvent(EqualsTouchEvent(event)));
  EXPECT_CALL(*remote_input_injector_,
              InjectTouchEvent(EqualsTouchEventTypeAndId(
                  protocol::TouchEvent::TOUCH_POINT_CANCEL, 0u)));

  // Send the touch event.
  input_injector_->InjectTouchEvent(event);
}

// Tests that setting the desktop resolution works.
TEST_F(IpcDesktopEnvironmentTest, SetScreenResolution) {
  std::unique_ptr<protocol::MockClipboardStub> clipboard_stub(
      new protocol::MockClipboardStub());
  EXPECT_CALL(*clipboard_stub, InjectClipboardEvent(_))
      .Times(0);

  // Start the input injector and screen capturer.
  input_injector_->Start(std::move(clipboard_stub));
  video_capturer_->Start(&desktop_capturer_callback_);

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  EXPECT_CALL(daemon_channel_, SetScreenResolution(_, _))
      .Times(1)
      .WillOnce(InvokeWithoutArgs(
          this, &IpcDesktopEnvironmentTest::DeleteDesktopEnvironment));

  // Change the desktop resolution.
  screen_controls_->SetScreenResolution(ScreenResolution(
      webrtc::DesktopSize(100, 100),
      webrtc::DesktopVector(96, 96)));
}

TEST_F(IpcDesktopEnvironmentTest, CheckUrlForwarderState) {
  EXPECT_CALL(*remote_url_forwarder_configurator_, IsUrlForwarderSetUp(_))
      .WillOnce(RunOnceCallback<0>(true));
  base::MockCallback<UrlForwarderConfigurator::IsUrlForwarderSetUpCallback>
      callback;
  EXPECT_CALL(callback, Run(true))
      .WillOnce([&]() {
        // Do it again when the state is already known.
        url_forwarder_configurator_->IsUrlForwarderSetUp(callback.Get());
      })
      .WillOnce(InvokeWithoutArgs(
          this, &IpcDesktopEnvironmentTest::DeleteDesktopEnvironment));

  url_forwarder_configurator_->IsUrlForwarderSetUp(callback.Get());

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  // Run now rather than in TearDown() so that we can verify |callback|.
  RunMainLoopUntilDone();
}

TEST_F(IpcDesktopEnvironmentTest, SetUpUrlForwarderHappyPath) {
  EXPECT_CALL(*remote_url_forwarder_configurator_, IsUrlForwarderSetUp(_))
      .WillOnce(RunOnceCallback<0>(false));
  EXPECT_CALL(*remote_url_forwarder_configurator_, SetUpUrlForwarder(_))
      .WillOnce([](auto& callback) {
        callback.Run(SetUpUrlForwarderResponse::USER_INTERVENTION_REQUIRED);
        callback.Run(SetUpUrlForwarderResponse::COMPLETE);
      });
  base::MockCallback<UrlForwarderConfigurator::SetUpUrlForwarderCallback>
      setup_state_callback;
  base::MockCallback<UrlForwarderConfigurator::IsUrlForwarderSetUpCallback>
      is_set_up_callback;

  {
    InSequence s;

    EXPECT_CALL(is_set_up_callback, Run(false)).WillOnce([&]() {
      // Post task to prevent reentrant issue.
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(&UrlForwarderConfigurator::SetUpUrlForwarder,
                         base::Unretained(url_forwarder_configurator_.get()),
                         setup_state_callback.Get()));
    });
    EXPECT_CALL(setup_state_callback,
                Run(SetUpUrlForwarderResponse::USER_INTERVENTION_REQUIRED))
        .Times(1);
    EXPECT_CALL(setup_state_callback, Run(SetUpUrlForwarderResponse::COMPLETE))
        .WillOnce([&]() {
          url_forwarder_configurator_->IsUrlForwarderSetUp(
              is_set_up_callback.Get());
        });
    EXPECT_CALL(is_set_up_callback, Run(true))
        .WillOnce(InvokeWithoutArgs(
            this, &IpcDesktopEnvironmentTest::DeleteDesktopEnvironment));
  }

  url_forwarder_configurator_->IsUrlForwarderSetUp(is_set_up_callback.Get());

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  // Run now rather than in TearDown() so that we can verify |callback|.
  RunMainLoopUntilDone();
}

TEST_F(IpcDesktopEnvironmentTest, SetUpUrlForwarderFailed) {
  EXPECT_CALL(*remote_url_forwarder_configurator_, IsUrlForwarderSetUp(_))
      .WillOnce(RunOnceCallback<0>(false));
  EXPECT_CALL(*remote_url_forwarder_configurator_, SetUpUrlForwarder(_))
      .WillOnce(RunOnceCallback<0>(SetUpUrlForwarderResponse::FAILED));
  base::MockCallback<UrlForwarderConfigurator::SetUpUrlForwarderCallback>
      setup_state_callback;
  base::MockCallback<UrlForwarderConfigurator::IsUrlForwarderSetUpCallback>
      is_set_up_callback;

  {
    InSequence s;

    EXPECT_CALL(is_set_up_callback, Run(false)).WillOnce([&]() {
      // Post task to prevent reentrant issue.
      base::SequencedTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(&UrlForwarderConfigurator::SetUpUrlForwarder,
                         base::Unretained(url_forwarder_configurator_.get()),
                         setup_state_callback.Get()));
    });
    EXPECT_CALL(setup_state_callback, Run(SetUpUrlForwarderResponse::FAILED))
        .WillOnce([&]() {
          url_forwarder_configurator_->IsUrlForwarderSetUp(
              is_set_up_callback.Get());
        });
    EXPECT_CALL(is_set_up_callback, Run(false))
        .WillOnce(InvokeWithoutArgs(
            this, &IpcDesktopEnvironmentTest::DeleteDesktopEnvironment));
  }

  url_forwarder_configurator_->IsUrlForwarderSetUp(is_set_up_callback.Get());

  // Run the message loop until the desktop is attached.
  setup_run_loop_->Run();

  // Run now rather than in TearDown() so that we can verify |callback|.
  RunMainLoopUntilDone();
}

}  // namespace remoting
