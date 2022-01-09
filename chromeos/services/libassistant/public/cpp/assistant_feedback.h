// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_LIBASSISTANT_PUBLIC_CPP_ASSISTANT_FEEDBACK_H_
#define CHROMEOS_SERVICES_LIBASSISTANT_PUBLIC_CPP_ASSISTANT_FEEDBACK_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/component_export.h"

namespace chromeos {
namespace assistant {

//  Details for Assistant feedback.
struct COMPONENT_EXPORT(LIBASSISTANT_PUBLIC_STRUCTS) AssistantFeedback {
  AssistantFeedback();
  ~AssistantFeedback();

  // User input to be sent with the feedback report.
  std::string description;

  // Whether user consent to send debug info.
  bool assistant_debug_info_allowed{false};

  // Screenshot if allowed by user.
  // Raw data (non-encoded binary octets)
  std::vector<uint8_t> screenshot_png;
};

}  // namespace assistant
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_LIBASSISTANT_PUBLIC_CPP_ASSISTANT_FEEDBACK_H_
