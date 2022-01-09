// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/browser/cast_media_blocker.h"

#include <memory>
#include <utility>

#include "base/time/time.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_content_client_initializer.h"
#include "content/public/test/test_renderer_host.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gl/test/gl_surface_test_support.h"

namespace chromecast {
namespace shell {

using ::testing::_;
using ::testing::Invoke;

namespace {

class MockMediaSession : public content::MediaSession {
 public:
  MockMediaSession() {}

  MockMediaSession(const MockMediaSession&) = delete;
  MockMediaSession& operator=(const MockMediaSession&) = delete;

  ~MockMediaSession() override {}

  MOCK_METHOD1(Resume, void(content::MediaSession::SuspendType));
  MOCK_METHOD1(Suspend, void(content::MediaSession::SuspendType));
  MOCK_METHOD1(Stop, void(content::MediaSession::SuspendType));
  MOCK_METHOD1(Seek, void(base::TimeDelta));
  MOCK_METHOD0(StartDucking, void());
  MOCK_METHOD0(StopDucking, void());
  MOCK_METHOD1(SetDuckingVolumeMultiplier, void(double));
  MOCK_METHOD1(DidReceiveAction,
               void(media_session::mojom::MediaSessionAction));
  MOCK_METHOD1(
      AddObserver,
      void(mojo::PendingRemote<media_session::mojom::MediaSessionObserver>));
  MOCK_METHOD1(GetMediaSessionInfo, void(GetMediaSessionInfoCallback));
  MOCK_METHOD1(GetDebugInfo, void(GetDebugInfoCallback));
  MOCK_METHOD0(PreviousTrack, void());
  MOCK_METHOD0(NextTrack, void());
  MOCK_METHOD0(SkipAd, void());
  MOCK_METHOD1(SetAudioFocusGroupId, void(const base::UnguessableToken&));
  MOCK_METHOD4(GetMediaImageBitmap,
               void(const media_session::MediaImage&,
                    int minimum_size_px,
                    int desired_size_px,
                    GetMediaImageBitmapCallback callback));
  MOCK_METHOD1(SeekTo, void(base::TimeDelta));
  MOCK_METHOD1(ScrubTo, void(base::TimeDelta));
  MOCK_METHOD0(EnterPictureInPicture, void());
  MOCK_METHOD0(ExitPictureInPicture, void());
  MOCK_METHOD1(SetAudioSinkId, void(const absl::optional<std::string>& id));
  MOCK_METHOD0(ToggleMicrophone, void());
  MOCK_METHOD0(ToggleCamera, void());
  MOCK_METHOD0(HangUp, void());
  MOCK_METHOD0(Raise, void());
  MOCK_METHOD1(SetMute, void(bool));
};

}  // namespace

class CastMediaBlockerTest : public content::RenderViewHostTestHarness {
 public:
  CastMediaBlockerTest() {}

  CastMediaBlockerTest(const CastMediaBlockerTest&) = delete;
  CastMediaBlockerTest& operator=(const CastMediaBlockerTest&) = delete;

  ~CastMediaBlockerTest() override {}

  void SetUp() override {
    gl::GLSurfaceTestSupport::InitializeOneOff();
    initializer_ = std::make_unique<content::TestContentClientInitializer>();
    content::RenderViewHostTestHarness::SetUp();
    web_contents_ = CreateTestWebContents();
    media_session_ = std::make_unique<MockMediaSession>();
    media_blocker_ = std::make_unique<CastMediaBlocker>(web_contents_.get());
    media_blocker_->SetMediaSessionForTesting(media_session_.get());
  }

  void MediaSessionChanged(bool controllable, bool suspended) {
    media_session::mojom::MediaSessionInfoPtr session_info(
        media_session::mojom::MediaSessionInfo::New());
    session_info->is_controllable = controllable;
    session_info->playback_state =
        suspended ? media_session::mojom::MediaPlaybackState::kPaused
                  : media_session::mojom::MediaPlaybackState::kPlaying;

    media_blocker_->MediaSessionInfoChanged(std::move(session_info));
  }

  void TearDown() override {
    media_blocker_.reset();
    web_contents_.reset();
    content::RenderViewHostTestHarness::TearDown();
  }

 protected:
  std::unique_ptr<content::TestContentClientInitializer> initializer_;
  std::unique_ptr<MockMediaSession> media_session_;
  std::unique_ptr<CastMediaBlocker> media_blocker_;
  std::unique_ptr<content::WebContents> web_contents_;
};

TEST_F(CastMediaBlockerTest, Block_Unblock_Suspended) {
  // Testing block/unblock operations do nothing if media never plays.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaLoading(true);
  media_blocker_->BlockMediaLoading(false);

  MediaSessionChanged(true, true);
  media_blocker_->BlockMediaLoading(true);
  media_blocker_->BlockMediaLoading(false);

  media_blocker_->BlockMediaLoading(true);
  MediaSessionChanged(false, true);
  media_blocker_->BlockMediaLoading(false);
}

TEST_F(CastMediaBlockerTest, No_Block) {
  // Tests CastMediaBlocker does nothing if block/unblock is not called.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);

  // Media becomes controllable/uncontrollable.
  MediaSessionChanged(true, true);
  MediaSessionChanged(false, true);

  // Media starts and stops.
  MediaSessionChanged(false, false);
  MediaSessionChanged(false, true);

  // Media starts, changes controllability and stops.
  MediaSessionChanged(false, false);
  MediaSessionChanged(true, false);
  MediaSessionChanged(false, false);
  MediaSessionChanged(false, true);

  // Media starts, changes controllability and stops.
  MediaSessionChanged(false, false);
  MediaSessionChanged(true, false);
  MediaSessionChanged(true, true);
}

TEST_F(CastMediaBlockerTest, Block_Before_Controllable) {
  // Tests CastMediaBlocker only suspends when controllable.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaLoading(true);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  // Session becomes controllable
  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
}

TEST_F(CastMediaBlockerTest, Block_After_Controllable) {
  // Tests CastMediaBlocker suspends immediately on block if controllable.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  // Block when media is playing
  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaLoading(true);
  MediaSessionChanged(true, true);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  // Unblock
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(1);
  media_blocker_->BlockMediaLoading(false);
}

TEST_F(CastMediaBlockerTest, Block_Multiple) {
  // Tests CastMediaBlocker repeatively suspends when blocked.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaLoading(true);
  MediaSessionChanged(false, false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
  MediaSessionChanged(true, true);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());
  MediaSessionChanged(true, true);

  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(false, true);
  MediaSessionChanged(false, false);
  MediaSessionChanged(false, true);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());
}

TEST_F(CastMediaBlockerTest, Block_Unblock_Uncontrollable) {
  // Tests CastMediaBlocker does not suspend or resume when uncontrollable.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaLoading(true);
  MediaSessionChanged(false, false);
  media_blocker_->BlockMediaLoading(false);
  media_blocker_->BlockMediaLoading(true);
  MediaSessionChanged(false, true);
  media_blocker_->BlockMediaLoading(false);
  media_blocker_->BlockMediaLoading(true);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());
}

TEST_F(CastMediaBlockerTest, Block_Unblock_Uncontrollable2) {
  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, true);
  media_blocker_->BlockMediaLoading(true);
  MediaSessionChanged(false, true);
  MediaSessionChanged(true, true);
  MediaSessionChanged(true, false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(false, false);
  MediaSessionChanged(false, true);
  MediaSessionChanged(true, true);
  MediaSessionChanged(true, false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaLoading(false);
}

TEST_F(CastMediaBlockerTest, Resume_When_Controllable) {
  // Tests CastMediaBlocker will only resume after unblock when controllable.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
  media_blocker_->BlockMediaLoading(true);
  MediaSessionChanged(true, true);
  MediaSessionChanged(false, true);
  media_blocker_->BlockMediaLoading(false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(1);
  MediaSessionChanged(true, true);
}

TEST_F(CastMediaBlockerTest, No_Resume) {
  // Tests CastMediaBlocker will not resume if media starts playing by itself
  // after unblock.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
  media_blocker_->BlockMediaLoading(true);
  MediaSessionChanged(true, true);
  MediaSessionChanged(false, true);
  media_blocker_->BlockMediaLoading(false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(false, false);
}

TEST_F(CastMediaBlockerTest, Block_Before_Resume) {
  // Tests CastMediaBlocker does not resume if blocked again after an unblock.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
  media_blocker_->BlockMediaLoading(true);
  MediaSessionChanged(true, true);
  MediaSessionChanged(false, true);
  media_blocker_->BlockMediaLoading(false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaLoading(true);
  MediaSessionChanged(true, true);
}

TEST_F(CastMediaBlockerTest, Unblocked_Already_Playing) {
  // Tests CastMediaBlocker does not resume if unblocked and media is playing.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
  media_blocker_->BlockMediaLoading(true);
  media_blocker_->BlockMediaLoading(false);
}

TEST_F(CastMediaBlockerTest, BlockStarting_UnblockStarting_Suspended) {
  // Testing block/unblock operations do nothing if media never plays.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaStarting(true);
  media_blocker_->BlockMediaStarting(false);

  MediaSessionChanged(true, true);
  media_blocker_->BlockMediaStarting(true);
  media_blocker_->BlockMediaStarting(false);

  media_blocker_->BlockMediaStarting(true);
  MediaSessionChanged(false, true);
  media_blocker_->BlockMediaStarting(false);
}

TEST_F(CastMediaBlockerTest, BlockStarting_Before_Controllable) {
  // Tests CastMediaBlocker only suspends when controllable.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaStarting(true);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  // Session becomes controllable
  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
}

TEST_F(CastMediaBlockerTest, BlockStarting_After_Controllable) {
  // Tests CastMediaBlocker suspends immediately on block if controllable.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  // Block when media is playing
  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaStarting(true);
  MediaSessionChanged(true, true);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  // Unblock
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(1);
  media_blocker_->BlockMediaStarting(false);
}

TEST_F(CastMediaBlockerTest, BlockStarting_Unblock_Suspended) {
  // Testing block/unblock operations do nothing if media never plays.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaStarting(true);
  media_blocker_->BlockMediaStarting(false);

  MediaSessionChanged(true, true);
  media_blocker_->BlockMediaStarting(true);
  media_blocker_->BlockMediaStarting(false);

  media_blocker_->BlockMediaStarting(true);
  MediaSessionChanged(false, true);
  media_blocker_->BlockMediaStarting(false);
}

TEST_F(CastMediaBlockerTest, BlockLoading_BlockStarting_After_Controllable) {
  // Tests CastMediaBlocker suspends immediately on block if controllable.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  // Block when media is playing
  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaLoading(true);
  MediaSessionChanged(true, true);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaStarting(true);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  // Unblock
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaLoading(false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(1);
  media_blocker_->BlockMediaStarting(false);
}

TEST_F(CastMediaBlockerTest, BlockStarting_BlockLoading_After_Controllable) {
  // Tests CastMediaBlocker suspends immediately on block if controllable.
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  MediaSessionChanged(true, false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  // Block when media is playing
  EXPECT_CALL(*media_session_, Suspend(_)).Times(1);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaStarting(true);
  MediaSessionChanged(true, true);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaLoading(true);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  // Unblock
  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(0);
  media_blocker_->BlockMediaStarting(false);
  testing::Mock::VerifyAndClearExpectations(media_session_.get());

  EXPECT_CALL(*media_session_, Suspend(_)).Times(0);
  EXPECT_CALL(*media_session_, Resume(_)).Times(1);
  media_blocker_->BlockMediaLoading(false);
}

}  // namespace shell
}  // namespace chromecast
