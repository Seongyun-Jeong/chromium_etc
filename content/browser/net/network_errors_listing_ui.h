// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_NET_NETWORK_ERRORS_LISTING_UI_H_
#define CONTENT_BROWSER_NET_NETWORK_ERRORS_LISTING_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_data_source.h"

namespace content {

class NetworkErrorsListingUI : public WebUIController {
 public:
  explicit NetworkErrorsListingUI(WebUI* web_ui);

  NetworkErrorsListingUI(const NetworkErrorsListingUI&) = delete;
  NetworkErrorsListingUI& operator=(const NetworkErrorsListingUI&) = delete;

  ~NetworkErrorsListingUI() override {}
};

}  // namespace content

#endif  // CONTENT_BROWSER_NET_NETWORK_ERRORS_LISTING_UI_H_
