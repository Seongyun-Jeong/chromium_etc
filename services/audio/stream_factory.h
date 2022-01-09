// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_AUDIO_STREAM_FACTORY_H_
#define SERVICES_AUDIO_STREAM_FACTORY_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/containers/flat_set.h"
#include "base/containers/unique_ptr_adapters.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/threading/thread.h"
#include "media/mojo/mojom/audio_logging.mojom.h"
#include "media/mojo/mojom/audio_output_stream.mojom.h"
#include "media/mojo/mojom/audio_stream_factory.mojom.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "services/audio/buildflags.h"
#include "services/audio/concurrent_stream_metric_reporter.h"
#include "services/audio/loopback_coordinator.h"

#if BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)
#include "services/audio/output_device_mixer_manager.h"
#endif

namespace base {
class UnguessableToken;
}

namespace media {
class AudioManager;
class AudioParameters;
}  // namespace media

namespace audio {

class InputStream;
class LocalMuter;
class LoopbackStream;
class OutputStream;

// This class is used to provide the AudioStreamFactory interface. It will
// typically be instantiated when needed and remain for the lifetime of the
// service. Destructing the factory will also destroy all the streams it has
// created. |audio_manager| must outlive the factory.
class StreamFactory final : public media::mojom::AudioStreamFactory {
 public:
  explicit StreamFactory(media::AudioManager* audio_manager);

  StreamFactory(const StreamFactory&) = delete;
  StreamFactory& operator=(const StreamFactory&) = delete;

  ~StreamFactory() final;

  void Bind(mojo::PendingReceiver<media::mojom::AudioStreamFactory> receiver);

  // StreamFactory implementation.
  void CreateInputStream(
      mojo::PendingReceiver<media::mojom::AudioInputStream> stream_receiver,
      mojo::PendingRemote<media::mojom::AudioInputStreamClient> client,
      mojo::PendingRemote<media::mojom::AudioInputStreamObserver> observer,
      mojo::PendingRemote<media::mojom::AudioLog> log,
      const std::string& device_id,
      const media::AudioParameters& params,
      uint32_t shared_memory_count,
      bool enable_agc,
      base::ReadOnlySharedMemoryRegion key_press_count_buffer,
      CreateInputStreamCallback created_callback) final;

  void AssociateInputAndOutputForAec(
      const base::UnguessableToken& input_stream_id,
      const std::string& output_device_id) final;

  void CreateOutputStream(
      mojo::PendingReceiver<media::mojom::AudioOutputStream> receiver,
      mojo::PendingAssociatedRemote<media::mojom::AudioOutputStreamObserver>
          observer,
      mojo::PendingRemote<media::mojom::AudioLog> log,
      const std::string& output_device_id,
      const media::AudioParameters& params,
      const base::UnguessableToken& group_id,
      CreateOutputStreamCallback created_callback) final;
  void BindMuter(
      mojo::PendingAssociatedReceiver<media::mojom::LocalMuter> receiver,
      const base::UnguessableToken& group_id) final;
  void CreateLoopbackStream(
      mojo::PendingReceiver<media::mojom::AudioInputStream> stream_receiver,
      mojo::PendingRemote<media::mojom::AudioInputStreamClient> client,
      mojo::PendingRemote<media::mojom::AudioInputStreamObserver> observer,
      const media::AudioParameters& params,
      uint32_t shared_memory_count,
      const base::UnguessableToken& group_id,
      CreateLoopbackStreamCallback created_callback) final;

 private:
  using InputStreamSet =
      base::flat_set<std::unique_ptr<InputStream>, base::UniquePtrComparator>;
  using OutputStreamSet =
      base::flat_set<std::unique_ptr<OutputStream>, base::UniquePtrComparator>;

  void DestroyInputStream(InputStream* stream);
  void DestroyOutputStream(OutputStream* stream);
  void DestroyMuter(LocalMuter* muter);
  void DestroyLoopbackStream(LoopbackStream* stream);

  SEQUENCE_CHECKER(owning_sequence_);

  media::AudioManager* const audio_manager_;

  mojo::ReceiverSet<media::mojom::AudioStreamFactory> receivers_;

  ConcurrentStreamMetricReporter stream_count_metric_reporter_;

  // Order of the following members is important for a clean shutdown.
#if BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)
  const std::unique_ptr<OutputDeviceMixerManager> output_device_mixer_manager_;
#endif
  LoopbackCoordinator coordinator_;
  std::vector<std::unique_ptr<LocalMuter>> muters_;
  base::Thread loopback_worker_thread_;
  std::vector<std::unique_ptr<LoopbackStream>> loopback_streams_;
  InputStreamSet input_streams_;
  OutputStreamSet output_streams_;

  base::WeakPtrFactory<StreamFactory> weak_ptr_factory_{this};
};

}  // namespace audio

#endif  // SERVICES_AUDIO_STREAM_FACTORY_H_
