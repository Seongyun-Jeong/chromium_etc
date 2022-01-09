// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_BROWSER_AGENT_INTERACTION_HANDLERS_TRANSLATE_TRANSLATE_INFOBAR_INTERACTION_HANDLER_H_
#define IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_BROWSER_AGENT_INTERACTION_HANDLERS_TRANSLATE_TRANSLATE_INFOBAR_INTERACTION_HANDLER_H_

#import "ios/chrome/browser/infobars/overlays/browser_agent/interaction_handlers/infobar_interaction_handler.h"

// An InfobarInteractionHandler that updates the model layer for interaction
// events with the UI for Translate infobars.
class TranslateInfobarInteractionHandler : public InfobarInteractionHandler {
 public:
  TranslateInfobarInteractionHandler();
  ~TranslateInfobarInteractionHandler() override;
};

#endif  // IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_BROWSER_AGENT_INTERACTION_HANDLERS_TRANSLATE_TRANSLATE_INFOBAR_INTERACTION_HANDLER_H_
