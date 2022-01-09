// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_MEDIA_MEDIA_STREAM_DISPATCHER_HOST_H_
#define CONTENT_BROWSER_RENDERER_HOST_MEDIA_MEDIA_STREAM_DISPATCHER_HOST_H_

#include <string>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "build/build_config.h"
#include "content/browser/media/media_devices_util.h"
#include "content/browser/media/media_stream_web_contents_observer.h"
#include "content/common/content_export.h"
#include "content/public/browser/browser_thread.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/blink/public/common/mediastream/media_stream_controls.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"

namespace content {

class MediaStreamManager;

// MediaStreamDispatcherHost is a delegate for Media Stream API messages used by
// MediaStreamImpl.  There is one MediaStreamDispatcherHost per
// RenderFrameHost, the former owned by the latter.
class CONTENT_EXPORT MediaStreamDispatcherHost
    : public blink::mojom::MediaStreamDispatcherHost {
 public:
  MediaStreamDispatcherHost(int render_process_id,
                            int render_frame_id,
                            MediaStreamManager* media_stream_manager);

  MediaStreamDispatcherHost(const MediaStreamDispatcherHost&) = delete;
  MediaStreamDispatcherHost& operator=(const MediaStreamDispatcherHost&) =
      delete;

  ~MediaStreamDispatcherHost() override;
  static void Create(
      int render_process_id,
      int render_frame_id,
      MediaStreamManager* media_stream_manager,
      mojo::PendingReceiver<blink::mojom::MediaStreamDispatcherHost> receiver);

  void OnWebContentsFocused();
  void set_salt_and_origin_callback_for_testing(
      MediaDeviceSaltAndOriginCallback callback) {
    salt_and_origin_callback_ = std::move(callback);
  }
  void SetMediaStreamDeviceObserverForTesting(
      mojo::PendingRemote<blink::mojom::MediaStreamDeviceObserver> observer) {
    media_stream_device_observer_.Bind(std::move(observer));
  }

 private:
  friend class MockMediaStreamDispatcherHost;

  struct PendingAccessRequest;
  using RequestsQueue =
      base::circular_deque<std::unique_ptr<PendingAccessRequest>>;
  RequestsQueue pending_requests_;

  const mojo::Remote<blink::mojom::MediaStreamDeviceObserver>&
  GetMediaStreamDeviceObserver();
  void OnMediaStreamDeviceObserverConnectionError();
  void CancelAllRequests();

  // mojom::MediaStreamDispatcherHost implementation
  void GenerateStream(
      int32_t request_id,
      const blink::StreamControls& controls,
      bool user_gesture,
      blink::mojom::StreamSelectionInfoPtr audio_stream_selection_info_ptr,
      GenerateStreamCallback callback) override;
  void CancelRequest(int32_t request_id) override;
  void StopStreamDevice(
      const std::string& device_id,
      const absl::optional<base::UnguessableToken>& session_id) override;
  void OpenDevice(int32_t request_id,
                  const std::string& device_id,
                  blink::mojom::MediaStreamType type,
                  OpenDeviceCallback callback) override;
  void CloseDevice(const std::string& label) override;
  void SetCapturingLinkSecured(
      const absl::optional<base::UnguessableToken>& session_id,
      blink::mojom::MediaStreamType type,
      bool is_secure) override;
  void OnStreamStarted(const std::string& label) override;
#if !defined(OS_ANDROID)
  void FocusCapturedSurface(const std::string& label, bool focus) override;
  void Crop(const base::UnguessableToken& device_id,
            const base::Token& crop_id,
            CropCallback callback) override;

  void OnCropValidationComplete(const base::UnguessableToken& device_id,
                                const base::Token& crop_id,
                                CropCallback callback,
                                bool crop_id_passed_validation);
#endif

  void DoGenerateStream(
      int32_t request_id,
      const blink::StreamControls& controls,
      bool user_gesture,
      blink::mojom::StreamSelectionInfoPtr audio_stream_selection_info_ptr,
      GenerateStreamCallback callback,
      MediaDeviceSaltAndOrigin salt_and_origin);
  void DoOpenDevice(int32_t request_id,
                    const std::string& device_id,
                    blink::mojom::MediaStreamType type,
                    OpenDeviceCallback callback,
                    MediaDeviceSaltAndOrigin salt_and_origin);

  void OnDeviceStopped(const std::string& label,
                       const blink::MediaStreamDevice& device);
  void OnDeviceChanged(const std::string& label,
                       const blink::MediaStreamDevice& old_device,
                       const blink::MediaStreamDevice& new_device);
  void OnDeviceRequestStateChange(
      const std::string& label,
      const blink::MediaStreamDevice& device,
      const blink::mojom::MediaStreamStateChange new_state);
  void OnDeviceCaptureHandleChange(const std::string& label,
                                   const blink::MediaStreamDevice& device);

  void SetWebContentsObserver(
      std::unique_ptr<MediaStreamWebContentsObserver,
                      BrowserThread::DeleteOnUIThread> web_contents_observer);

  static int next_requester_id_;

  const int render_process_id_;
  const int render_frame_id_;
  const int requester_id_;
  raw_ptr<MediaStreamManager> media_stream_manager_;
  mojo::Remote<blink::mojom::MediaStreamDeviceObserver>
      media_stream_device_observer_;
  MediaDeviceSaltAndOriginCallback salt_and_origin_callback_;

  std::unique_ptr<MediaStreamWebContentsObserver,
                  BrowserThread::DeleteOnUIThread>
      web_contents_observer_;

  base::WeakPtrFactory<MediaStreamDispatcherHost> weak_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_MEDIA_MEDIA_STREAM_DISPATCHER_HOST_H_
