// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/external_protocol/external_protocol_handler.h"

#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "components/navigation_interception/intercept_navigation_delegate.h"
#include "components/navigation_interception/navigation_params.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"

using content::WebContents;

// static
void ExternalProtocolHandler::RunExternalProtocolDialog(
    const GURL& url,
    WebContents* web_contents,
    ui::PageTransition page_transition,
    bool has_user_gesture,
    const absl::optional<url::Origin>& initiating_origin,
    content::WeakDocumentPtr) {
  navigation_interception::InterceptNavigationDelegate* delegate =
      navigation_interception::InterceptNavigationDelegate::Get(web_contents);
  if (!delegate)
    return;

  navigation_interception::NavigationParams navigation_params(
      url, content::Referrer(),
      // Pass 0 as the navigation ID to specify that this instance doesn't
      // correspond to a NavigationHandle.
      0,
      has_user_gesture,  // has_user_gesture
      false,             // is_post, doesn't matter here.
      page_transition,
      false,   // is_redirect, doesn't matter here.
      true,    // is_external_protocol
      false,   // is_main_frame
      true,    // is_renderer_initiated.
      GURL(),  // base_url_for_data_url, not applicable.
      initiating_origin);
  delegate->ShouldIgnoreNavigation(navigation_params);
}
