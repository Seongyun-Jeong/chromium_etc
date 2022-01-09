// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/remote_open_url/remote_open_url_constants.h"

#include "build/build_config.h"

namespace remoting {

const char kRemoteOpenUrlDataChannelName[] = "remote-open-url";

#if defined(OS_WIN)

#if defined(OFFICIAL_BUILD)
const wchar_t kUrlForwarderProgId[] = L"ChromeRemoteDesktopUrlForwarder";
#else
const wchar_t kUrlForwarderProgId[] = L"ChromotingUrlForwarder";
#endif

const wchar_t kUndecidedProgId[] = L"Undecided";

#endif  // defined (OS_WIN)

}  // namespace remoting
