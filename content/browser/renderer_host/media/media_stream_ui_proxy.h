// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_MEDIA_MEDIA_STREAM_UI_PROXY_H_
#define CONTENT_BROWSER_RENDERER_HOST_MEDIA_MEDIA_STREAM_UI_PROXY_H_

#include <memory>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/media_stream_request.h"
#include "third_party/blink/public/common/mediastream/media_stream_request.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom-shared.h"
#include "ui/gfx/native_widget_types.h"

namespace content {

struct MediaStreamRequest;
class RenderFrameHostDelegate;

// MediaStreamUIProxy proxies calls to media stream UI between IO thread and UI
// thread. One instance of this class is create per MediaStream object. It must
// be created, used and destroyed on IO thread.
class CONTENT_EXPORT MediaStreamUIProxy {
 public:
  using ResponseCallback =
      base::OnceCallback<void(const blink::MediaStreamDevices& devices,
                              blink::mojom::MediaStreamRequestResult result)>;

  using WindowIdCallback =
      base::OnceCallback<void(gfx::NativeViewId window_id)>;

  static std::unique_ptr<MediaStreamUIProxy> Create();
  static std::unique_ptr<MediaStreamUIProxy> CreateForTests(
      RenderFrameHostDelegate* render_delegate);

  MediaStreamUIProxy(const MediaStreamUIProxy&) = delete;
  MediaStreamUIProxy& operator=(const MediaStreamUIProxy&) = delete;

  virtual ~MediaStreamUIProxy();

  // Requests access for the MediaStream by calling
  // WebContentsDelegate::RequestMediaAccessPermission(). The specified
  // |response_callback| is called when the WebContentsDelegate approves or
  // denies request.
  virtual void RequestAccess(std::unique_ptr<MediaStreamRequest> request,
                             ResponseCallback response_callback);

  // Notifies the UI that the MediaStream has been started. Must be called after
  // access has been approved using RequestAccess().
  // |stop_callback| is be called on the IO thread after the user has requests
  // the stream to be stopped.
  // |source_callback| is be called on the IO thread after the user has requests
  // the stream source to be changed.
  // |window_id_callback| is called on the IO thread with the platform-
  // dependent window ID of the UI.
  // |label| is the unique label of the stream's request.
  // |screen_share_ids| is a list of media IDs of the started desktop captures.
  // |state_change_callback| is called on the IO thread when the stream should
  // be paused on unpaused.
  virtual void OnStarted(
      base::OnceClosure stop_callback,
      MediaStreamUI::SourceCallback source_callback,
      WindowIdCallback window_id_callback,
      const std::string& label,
      std::vector<DesktopMediaID> screen_share_ids,
      MediaStreamUI::StateChangeCallback state_change_callback);

  virtual void OnDeviceStopped(const std::string& label,
                               const DesktopMediaID& media_id);

#if !defined(OS_ANDROID)
  // Determines whether the captured display surface represented by |media_id|
  // should be focused or not.
  // Only the first call to this method on a given object has an effect; the
  // rest are ignored.
  //
  // |is_from_microtask| and |is_from_timer| are used to distinguish:
  // a. Explicit calls from the Web-application.
  // b. Implicit calls resulting from the focusability-window-closing microtask.
  // c. The browser-side timer.
  // This distinction is reflected by UMA.
  virtual void SetFocus(const DesktopMediaID& media_id,
                        bool focus,
                        bool is_from_microtask,
                        bool is_from_timer);
#endif

 protected:
  explicit MediaStreamUIProxy(RenderFrameHostDelegate* test_render_delegate);

 private:
  class Core;
  friend class Core;
  friend class FakeMediaStreamUIProxy;

  void ProcessAccessRequestResponse(
      const blink::MediaStreamDevices& devices,
      blink::mojom::MediaStreamRequestResult result);
  void ProcessStopRequestFromUI();
  void ProcessChangeSourceRequestFromUI(const DesktopMediaID& media_id);
  void ProcessStateChangeFromUI(const DesktopMediaID& media_id,
                                blink::mojom::MediaStreamStateChange new_state);
  void OnWindowId(WindowIdCallback window_id_callback,
                  gfx::NativeViewId* window_id);

  std::unique_ptr<Core, content::BrowserThread::DeleteOnUIThread> core_;
  ResponseCallback response_callback_;
  base::OnceClosure stop_callback_;
  MediaStreamUI::SourceCallback source_callback_;
  MediaStreamUI::StateChangeCallback state_change_callback_;

  base::WeakPtrFactory<MediaStreamUIProxy> weak_factory_{this};
};

class CONTENT_EXPORT FakeMediaStreamUIProxy : public MediaStreamUIProxy {
 public:
  // Set |tests_use_fake_render_frame_hosts| to false if the test that's
  // creating the FakeMediaStreamUIProxy creates real RFH objects or true if it
  // just passes in dummy IDs to refer to RFHs.
  explicit FakeMediaStreamUIProxy(bool tests_use_fake_render_frame_hosts);

  FakeMediaStreamUIProxy(const FakeMediaStreamUIProxy&) = delete;
  FakeMediaStreamUIProxy& operator=(const FakeMediaStreamUIProxy&) = delete;

  ~FakeMediaStreamUIProxy() override;

  void SetAvailableDevices(const blink::MediaStreamDevices& devices);
  void SetMicAccess(bool access);
  void SetCameraAccess(bool access);

  // MediaStreamUIProxy overrides.
  void RequestAccess(std::unique_ptr<MediaStreamRequest> request,
                     ResponseCallback response_callback) override;
  void OnStarted(
      base::OnceClosure stop_callback,
      MediaStreamUI::SourceCallback source_callback,
      WindowIdCallback window_id_callback,
      const std::string& label,
      std::vector<DesktopMediaID> screen_share_ids,
      MediaStreamUI::StateChangeCallback state_change_callback) override;
  void OnDeviceStopped(const std::string& label,
                       const DesktopMediaID& media_id) override;

 private:
  // This is used for RequestAccess().
  blink::MediaStreamDevices devices_;

  // These are used for CheckAccess().
  bool mic_access_;
  bool camera_access_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_MEDIA_MEDIA_STREAM_UI_PROXY_H_
