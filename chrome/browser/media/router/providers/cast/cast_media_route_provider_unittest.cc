// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/router/providers/cast/cast_media_route_provider.h"

#include <utility>

#include "base/bind.h"
#include "base/run_loop.h"
#include "base/test/test_simple_task_runner.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "chrome/browser/media/router/providers/cast/cast_session_tracker.h"
#include "chrome/browser/media/router/test/mock_mojo_media_router.h"
#include "chrome/browser/media/router/test/provider_test_helpers.h"
#include "components/cast_channel/cast_test_util.h"
#include "components/media_router/common/test/test_helper.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/browser_task_environment.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/data_decoder/public/cpp/test_support/in_process_data_decoder.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::NiceMock;
using testing::WithArg;

namespace media_router {

namespace {
static constexpr char kAppId[] = "ABCDEFGH";

constexpr char kCastSource[] =
    "cast:ABCDEFGH?clientId=theClientId&appParams={\"credentialsType\":"
    "\"mobile\"}";
static constexpr char kPresentationId[] = "presentationId";
static constexpr char kOrigin[] = "https://www.youtube.com";
static constexpr int kTabId = 1;
static constexpr base::TimeDelta kRouteTimeout = base::Seconds(30);

base::Value MakeReceiverStatus() {
  return base::test::ParseJson(R"({
        "applications": [{
          "appId": "ABCDEFGH",
          "displayName": "theDisplayName",
          "namespaces": [
            {"name": "urn:x-cast:com.google.cast.media"},
            {"name": "urn:x-cast:com.google.foo"},
          ],
          "sessionId": "theSessionId",
          "statusText": "theAppStatus",
          "transportId": "theTransportId",
        }],
      })");
}
}  // namespace

class CastMediaRouteProviderTest : public testing::Test {
 public:
  CastMediaRouteProviderTest()
      : socket_service_(content::GetUIThreadTaskRunner({})),
        message_handler_(&socket_service_) {}
  CastMediaRouteProviderTest(CastMediaRouteProviderTest&) = delete;
  CastMediaRouteProviderTest& operator=(CastMediaRouteProviderTest&) = delete;
  ~CastMediaRouteProviderTest() override = default;

  void SetUp() override {
    mojo::PendingRemote<mojom::MediaRouter> router_remote;
    router_receiver_ = std::make_unique<mojo::Receiver<mojom::MediaRouter>>(
        &mock_router_, router_remote.InitWithNewPipeAndPassReceiver());

    session_tracker_ = std::unique_ptr<CastSessionTracker>(
        new CastSessionTracker(&media_sink_service_, &message_handler_,
                               socket_service_.task_runner()));
    CastSessionTracker::SetInstanceForTest(session_tracker_.get());

    provider_ = std::make_unique<CastMediaRouteProvider>(
        provider_remote_.BindNewPipeAndPassReceiver(), std::move(router_remote),
        &media_sink_service_, &app_discovery_service_, &message_handler_,
        "hash-token", base::SequencedTaskRunnerHandle::Get());

    base::RunLoop().RunUntilIdle();
  }

  void TearDown() override {
    provider_.reset();
    CastSessionTracker::SetInstanceForTest(nullptr);
    session_tracker_.reset();
  }

  void ExpectCreateRouteSuccessAndSetRoute(
      const absl::optional<MediaRoute>& route,
      mojom::RoutePresentationConnectionPtr presentation_connections,
      const absl::optional<std::string>& error,
      RouteRequestResult::ResultCode result) {
    EXPECT_TRUE(route);
    EXPECT_TRUE(presentation_connections);
    EXPECT_FALSE(error);
    EXPECT_EQ(RouteRequestResult::ResultCode::OK, result);
    route_ = std::make_unique<MediaRoute>(*route);
  }

  void ExpectCreateRouteFailure(
      RouteRequestResult::ResultCode expected_result,
      const absl::optional<MediaRoute>& route,
      mojom::RoutePresentationConnectionPtr presentation_connections,
      const absl::optional<std::string>& error,
      RouteRequestResult::ResultCode result) {
    EXPECT_FALSE(route);
    EXPECT_FALSE(presentation_connections);
    EXPECT_TRUE(error);
    EXPECT_EQ(expected_result, result);
  }

  void ExpectTerminateRouteSuccess(const absl::optional<std::string>& error,
                                   RouteRequestResult::ResultCode result) {
    EXPECT_FALSE(error);
    EXPECT_EQ(RouteRequestResult::ResultCode::OK, result);
    route_.reset();
  }

  void SendLaunchSessionResponseSuccess() {
    cast_channel::LaunchSessionResponse response;
    response.result = cast_channel::LaunchSessionResponse::Result::kOk;
    response.receiver_status = MakeReceiverStatus();
    std::move(launch_session_callback_).Run(std::move(response));
    base::RunLoop().RunUntilIdle();
  }

  void SendLaunchSessionResponseFailure() {
    cast_channel::LaunchSessionResponse response;
    response.result = cast_channel::LaunchSessionResponse::Result::kError;
    std::move(launch_session_callback_).Run(std::move(response));
    base::RunLoop().RunUntilIdle();
  }

 protected:
  content::BrowserTaskEnvironment task_environment_;
  data_decoder::test::InProcessDataDecoder in_process_data_decoder_;

  mojo::Remote<mojom::MediaRouteProvider> provider_remote_;
  NiceMock<MockMojoMediaRouter> mock_router_;
  std::unique_ptr<mojo::Receiver<mojom::MediaRouter>> router_receiver_;

  cast_channel::MockCastSocketService socket_service_;
  NiceMock<cast_channel::MockCastMessageHandler> message_handler_;

  std::unique_ptr<CastSessionTracker> session_tracker_;
  TestMediaSinkService media_sink_service_;
  MockCastAppDiscoveryService app_discovery_service_;
  std::unique_ptr<CastMediaRouteProvider> provider_;

  cast_channel::LaunchSessionCallback launch_session_callback_;

  url::Origin origin_ = url::Origin::Create(GURL(kOrigin));
  std::unique_ptr<MediaRoute> route_;
};

TEST_F(CastMediaRouteProviderTest, StartObservingMediaSinks) {
  MediaSource::Id non_cast_source("not-a-cast-source:foo");
  EXPECT_CALL(app_discovery_service_, DoStartObservingMediaSinks(_)).Times(0);
  provider_->StartObservingMediaSinks(non_cast_source);

  EXPECT_CALL(app_discovery_service_, DoStartObservingMediaSinks(_));
  provider_->StartObservingMediaSinks(kCastSource);
  EXPECT_FALSE(app_discovery_service_.callbacks().empty());

  provider_->StopObservingMediaSinks(kCastSource);
  EXPECT_TRUE(app_discovery_service_.callbacks().empty());
}

TEST_F(CastMediaRouteProviderTest, BroadcastRequest) {
  media_sink_service_.AddOrUpdateSink(CreateCastSink(1));
  media_sink_service_.AddOrUpdateSink(CreateCastSink(2));
  MediaSource::Id source_id(
      "cast:ABCDEFAB?capabilities=video_out,audio_out"
      "&clientId=123"
      "&broadcastNamespace=namespace"
      "&broadcastMessage=message");

  std::vector<std::string> app_ids = {"ABCDEFAB"};
  cast_channel::BroadcastRequest request("namespace", "message");
  EXPECT_CALL(message_handler_, SendBroadcastMessage(1, app_ids, request));
  EXPECT_CALL(message_handler_, SendBroadcastMessage(2, app_ids, request));
  EXPECT_CALL(app_discovery_service_, DoStartObservingMediaSinks(_)).Times(0);
  provider_->StartObservingMediaSinks(source_id);
  EXPECT_TRUE(app_discovery_service_.callbacks().empty());
}

TEST_F(CastMediaRouteProviderTest, CreateRouteFailsInvalidSink) {
  // Sink does not exist.
  provider_->CreateRoute(
      kCastSource, "sinkId", kPresentationId, origin_, kTabId, kRouteTimeout,
      /* incognito */ false,
      base::BindOnce(&CastMediaRouteProviderTest::ExpectCreateRouteFailure,
                     base::Unretained(this),
                     RouteRequestResult::ResultCode::SINK_NOT_FOUND));
}

TEST_F(CastMediaRouteProviderTest, CreateRouteFailsInvalidSource) {
  MediaSinkInternal sink = CreateCastSink(1);
  media_sink_service_.AddOrUpdateSink(sink);

  provider_->CreateRoute(
      "invalidSource", sink.sink().id(), kPresentationId, origin_, kTabId,
      kRouteTimeout, /* incognito */ false,
      base::BindOnce(&CastMediaRouteProviderTest::ExpectCreateRouteFailure,
                     base::Unretained(this),
                     RouteRequestResult::ResultCode::NO_SUPPORTED_PROVIDER));
}

TEST_F(CastMediaRouteProviderTest, CreateRoute) {
  MediaSinkInternal sink = CreateCastSink(1);
  media_sink_service_.AddOrUpdateSink(sink);

  std::vector<std::string> default_supported_app_types = {"WEB"};
  EXPECT_CALL(
      message_handler_,
      LaunchSession(sink.cast_data().cast_channel_id, kAppId,
                    kDefaultLaunchTimeout, default_supported_app_types, _, _))
      .WillOnce(WithArg<5>([this](auto callback) {
        launch_session_callback_ = std::move(callback);
      }));
  provider_->CreateRoute(
      kCastSource, sink.sink().id(), kPresentationId, origin_, kTabId,
      kRouteTimeout, /* incognito */ false,
      base::BindOnce(
          &CastMediaRouteProviderTest::ExpectCreateRouteSuccessAndSetRoute,
          base::Unretained(this)));
  base::RunLoop().RunUntilIdle();
  SendLaunchSessionResponseSuccess();
  ASSERT_TRUE(route_);
}

TEST_F(CastMediaRouteProviderTest, TerminateRoute) {
  MediaSinkInternal sink = CreateCastSink(1);
  media_sink_service_.AddOrUpdateSink(sink);

  EXPECT_CALL(message_handler_, LaunchSession)
      .WillOnce(WithArg<5>([this](auto callback) {
        launch_session_callback_ = std::move(callback);
      }));
  provider_->CreateRoute(
      kCastSource, sink.sink().id(), kPresentationId, origin_, kTabId,
      kRouteTimeout, /* incognito */ false,
      base::BindOnce(
          &CastMediaRouteProviderTest::ExpectCreateRouteSuccessAndSetRoute,
          base::Unretained(this)));
  base::RunLoop().RunUntilIdle();
  SendLaunchSessionResponseSuccess();

  ASSERT_TRUE(route_);
  EXPECT_CALL(message_handler_, StopSession)
      .WillOnce(WithArg<3>([](auto callback) {
        std::move(callback).Run(cast_channel::Result::kOk);
      }));
  provider_->TerminateRoute(
      route_->media_route_id(),
      base::BindOnce(&CastMediaRouteProviderTest::ExpectTerminateRouteSuccess,
                     base::Unretained(this)));
  ASSERT_FALSE(route_);
}

TEST_F(CastMediaRouteProviderTest, GetState) {
  MediaSinkInternal sink = CreateCastSink(1);
  media_sink_service_.AddOrUpdateSink(sink);
  session_tracker_->HandleReceiverStatusMessage(sink, base::test::ParseJson(R"({
    "status": {
      "applications": [{
        "appId": "ABCDEFGH",
        "displayName": "App display name",
        "namespaces": [
          {"name": "urn:x-cast:com.google.cast.media"},
          {"name": "urn:x-cast:com.google.foo"}
        ],
        "sessionId": "theSessionId",
        "statusText":"App status",
        "transportId":"theTransportId"
      }]
    }
  })"));

  provider_->GetState(base::BindOnce([](mojom::ProviderStatePtr state) {
    ASSERT_TRUE(state);
    ASSERT_TRUE(state->is_cast_provider_state());
    const mojom::CastProviderState& cast_state =
        *(state->get_cast_provider_state());
    ASSERT_EQ(cast_state.session_state.size(), 1UL);
    const mojom::CastSessionState& session_state =
        *(cast_state.session_state[0]);
    EXPECT_EQ(session_state.sink_id, "cast:<id1>");
    EXPECT_EQ(session_state.app_id, "ABCDEFGH");
    EXPECT_EQ(session_state.session_id, "theSessionId");
    EXPECT_EQ(session_state.route_description, "App status");
  }));
}

}  // namespace media_router
