// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/assistant/test_support/fully_initialized_assistant_state.h"

namespace chromeos {
namespace assistant {

FullyInitializedAssistantState::FullyInitializedAssistantState() {
  InitializeAllValues();
}

void FullyInitializedAssistantState::SetAssistantEnabled(bool enabled) {
  settings_enabled_ = enabled;

  for (auto& observer : observers_)
    observer.OnAssistantSettingsEnabled(settings_enabled_.value());
}

void FullyInitializedAssistantState::InitializeAllValues() {
  settings_enabled_ = true;
  consent_status_ = prefs::ConsentStatus::kActivityControlAccepted;
  context_enabled_ = true;
  hotword_enabled_ = true;
  hotword_always_on_ = true;
  launch_with_mic_open_ = true;
  notification_enabled_ = true;
  allowed_state_ = AssistantAllowedState::ALLOWED;
  locale_ = "en_US";
  arc_play_store_enabled_ = true;
  locked_full_screen_enabled_ = true;
}

}  // namespace assistant
}  // namespace chromeos
