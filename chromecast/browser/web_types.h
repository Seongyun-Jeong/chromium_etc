// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_BROWSER_WEB_TYPES_H_
#define CHROMECAST_BROWSER_WEB_TYPES_H_

#include <ostream>

#include "chromecast/browser/webview/proto/webview.pb.h"

namespace chromecast {

enum class BackgroundColor {
  NONE,
  WHITE,
  BLACK,
  TRANSPARENT,
};

// Page state for the main frame. These values *must* be kept in sync with
// AsyncPageEvent::State in chromecast/browser/webview/proto/webview.proto.
enum class PageState {
  IDLE = 0,       // Main frame has not started yet.
  LOADING = 1,    // Main frame is loading resources.
  LOADED = 2,     // Main frame is loaded, but sub-frames may still be loading.
  CLOSED = 3,     // Page is closed and should be cleaned up.
  DESTROYED = 4,  // The WebContents is destroyed and can no longer be used.
  ERROR = 5,      // Main frame is in an error state.
};

std::ostream& operator<<(std::ostream& os, PageState state);

webview::AsyncPageEvent_State ToGrpcPageState(PageState state);

}  // namespace chromecast

#endif  // CHROMECAST_BROWSER_WEB_TYPES_H_
