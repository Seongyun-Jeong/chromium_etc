// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_WEBRTC_DESKTOP_CAPTURE_DEVICES_UTIL_H_
#define CHROME_BROWSER_MEDIA_WEBRTC_DESKTOP_CAPTURE_DEVICES_UTIL_H_

#include <memory>

#include "base/strings/string_util.h"
#include "content/public/browser/desktop_media_id.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/mediastream/media_stream_request.h"

// Helper to get the list of media stream devices for desktop capture and store
// them in |out_devices|. Registers to display notification if
// |display_notification| is true. Returns an instance of MediaStreamUI to be
// passed to content layer.
std::unique_ptr<content::MediaStreamUI> GetDevicesForDesktopCapture(
    const content::MediaStreamRequest& request,
    content::WebContents* web_contents,
    const content::DesktopMediaID& media_id,
    bool capture_audio,
    bool disable_local_echo,
    bool display_notification,
    const std::u16string& application_title,
    blink::MediaStreamDevices* out_devices);

#endif  // CHROME_BROWSER_MEDIA_WEBRTC_DESKTOP_CAPTURE_DEVICES_UTIL_H_
