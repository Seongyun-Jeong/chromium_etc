// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/mirroring/service/rtp_stream.h"

#include "base/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/mock_callback.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/test/task_environment.h"
#include "media/base/video_frame.h"
#include "media/cast/cast_config.h"
#include "media/cast/cast_environment.h"
#include "media/cast/sender/audio_sender.h"
#include "media/cast/sender/video_sender.h"
#include "media/cast/test/mock_cast_transport.h"
#include "media/cast/test/utility/audio_utility.h"
#include "media/cast/test/utility/default_config.h"
#include "media/cast/test/utility/video_utility.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::InvokeWithoutArgs;
using ::testing::_;
using media::cast::TestAudioBusFactory;

namespace mirroring {

namespace {

class StreamClient final : public RtpStreamClient {
 public:
  StreamClient(base::SimpleTestTickClock* clock) : clock_(clock) {}

  StreamClient(const StreamClient&) = delete;
  StreamClient& operator=(const StreamClient&) = delete;

  ~StreamClient() override = default;

  void SetVideoRtpStream(VideoRtpStream* stream) { video_stream_ = stream; }

  // RtpStreamClient implementation.
  void OnError(const std::string& message) override {}
  void RequestRefreshFrame() override {
    if (video_stream_) {
      video_stream_->InsertVideoFrame(CreateVideoFrame());
    }
  }
  void CreateVideoEncodeAccelerator(
      media::cast::ReceiveVideoEncodeAcceleratorCallback callback) override {}

  scoped_refptr<media::VideoFrame> CreateVideoFrame() {
    constexpr gfx::Size kFrameSize(640, 480);

    base::TimeDelta frame_timestamp;
    if (first_frame_time_.is_null()) {
      first_frame_time_ = clock_->NowTicks();
      frame_timestamp = base::TimeDelta();
    } else {
      clock_->Advance(base::Milliseconds(10));
      frame_timestamp = clock_->NowTicks() - first_frame_time_;
    }

    auto frame = media::VideoFrame::CreateFrame(
        media::PIXEL_FORMAT_I420, kFrameSize, gfx::Rect(kFrameSize), kFrameSize,
        frame_timestamp);
    media::cast::PopulateVideoFrame(frame.get(), 1);
    frame->metadata().reference_time = clock_->NowTicks();
    return frame;
  }

  base::WeakPtr<RtpStreamClient> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  raw_ptr<VideoRtpStream> video_stream_ = nullptr;
  base::TimeTicks first_frame_time_;
  raw_ptr<base::SimpleTestTickClock> clock_;
  base::WeakPtrFactory<StreamClient> weak_factory_{this};
};

}  // namespace

class RtpStreamTest : public ::testing::Test {
 public:
  RtpStreamTest()
      : cast_environment_(new media::cast::CastEnvironment(
            &testing_clock_,
            task_environment_.GetMainThreadTaskRunner(),
            task_environment_.GetMainThreadTaskRunner(),
            task_environment_.GetMainThreadTaskRunner())),
        client_(&testing_clock_) {
    testing_clock_.Advance(base::TimeTicks::Now() - base::TimeTicks());
  }

  RtpStreamTest(const RtpStreamTest&) = delete;
  RtpStreamTest& operator=(const RtpStreamTest&) = delete;

  ~RtpStreamTest() override { task_environment_.RunUntilIdle(); }

 protected:
  base::test::TaskEnvironment task_environment_;
  base::SimpleTestTickClock testing_clock_;
  const scoped_refptr<media::cast::CastEnvironment> cast_environment_;
  StreamClient client_;

  // We currently don't care about sender reports, so we have a nick
  // mock for the transport.
  testing::NiceMock<media::cast::MockCastTransport> transport_;
};

// Test the video streaming pipeline.
TEST_F(RtpStreamTest, VideoStreaming) {
  auto video_sender = std::make_unique<media::cast::VideoSender>(
      cast_environment_, media::cast::GetDefaultVideoSenderConfig(),
      base::DoNothing(), base::DoNothing(), &transport_, base::DoNothing(),
      base::DoNothing());
  VideoRtpStream video_stream(std::move(video_sender), client_.GetWeakPtr());
  {
    base::RunLoop run_loop;
    // Expect the video frame is sent to video sender for encoding, and the
    // encoded frame is sent to the transport.
    EXPECT_CALL(transport_, InsertFrame(_, _))
        .WillOnce(InvokeWithoutArgs(&run_loop, &base::RunLoop::Quit));
    video_stream.InsertVideoFrame(client_.CreateVideoFrame());
    run_loop.Run();
  }

  task_environment_.RunUntilIdle();
}

TEST_F(RtpStreamTest, VideoStreamEmitsFramesWhenNoUpdates) {
  auto video_sender = std::make_unique<media::cast::VideoSender>(
      cast_environment_, media::cast::GetDefaultVideoSenderConfig(),
      base::DoNothing(), base::DoNothing(), &transport_, base::DoNothing(),
      base::DoNothing());
  VideoRtpStream video_stream(std::move(video_sender), client_.GetWeakPtr());
  client_.SetVideoRtpStream(&video_stream);
  {
    base::RunLoop run_loop;
    int loop_count = 0;
    // Expect the video frame is sent to video sender for encoding, and the
    // encoded frame is sent to the transport.
    EXPECT_CALL(transport_, InsertFrame(_, _))
        .WillRepeatedly(InvokeWithoutArgs([&run_loop, &loop_count] {
          if (loop_count++ == 5) {
            run_loop.Quit();
          }
        }));

    // We start with one valid frame, then the rest should be update requests.
    video_stream.InsertVideoFrame(client_.CreateVideoFrame());
    run_loop.Run();
  }

  task_environment_.RunUntilIdle();
}

// Test the audio streaming pipeline.
TEST_F(RtpStreamTest, AudioStreaming) {
  // Create audio data.
  const base::TimeDelta kDuration = base::Milliseconds(10);
  media::cast::FrameSenderConfig audio_config =
      media::cast::GetDefaultAudioSenderConfig();
  std::unique_ptr<media::AudioBus> audio_bus =
      TestAudioBusFactory(audio_config.channels, audio_config.rtp_timebase,
                          TestAudioBusFactory::kMiddleANoteFreq, 0.5f)
          .NextAudioBus(kDuration);
  auto audio_sender = std::make_unique<media::cast::AudioSender>(
      cast_environment_, audio_config, base::DoNothing(), &transport_);
  AudioRtpStream audio_stream(std::move(audio_sender), client_.GetWeakPtr());
  {
    base::RunLoop run_loop;
    // Expect the audio data is sent to audio sender for encoding, and the
    // encoded frame is sent to the transport.
    EXPECT_CALL(transport_, InsertFrame(_, _))
        .WillOnce(InvokeWithoutArgs(&run_loop, &base::RunLoop::Quit));
    audio_stream.InsertAudio(std::move(audio_bus), testing_clock_.NowTicks());
    run_loop.Run();
  }

  task_environment_.RunUntilIdle();
}

}  // namespace mirroring
