// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_JS_FEATURES_CONTEXT_MENU_CONTEXT_MENU_PARAMS_UTILS_H_
#define IOS_WEB_JS_FEATURES_CONTEXT_MENU_CONTEXT_MENU_PARAMS_UTILS_H_

#import "ios/web/public/ui/context_menu_params.h"

namespace base {
class Value;
}  // namespace base

namespace web {

// Returns true if the |params| contain enough information to present a context
// menu. (A valid url for either link_url or src_url must exist in the params.)
bool CanShowContextMenuForParams(const ContextMenuParams& params);

// Creates a ContextMenuParams from a base::Value dictionary representing an
// HTML element. The fields "href", "src", "title", "referrerPolicy" and
// "innerText" will be used (if present) to generate the ContextMenuParams.
// If set, all these fields must have String values.
// This constructor does not set fields relative to the touch event (view and
// location).
ContextMenuParams ContextMenuParamsFromElementDictionary(base::Value* element);

}  // namespace web

#endif  // IOS_WEB_JS_FEATURES_CONTEXT_MENU_CONTEXT_MENU_PARAMS_UTILS_H_
