// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_CHROMEOS_CHROMEOS_ENTERPRISE_PARAMS_H_
#define REMOTING_HOST_CHROMEOS_CHROMEOS_ENTERPRISE_PARAMS_H_

namespace remoting {

// ChromeOS enterprise specific parameters.
// These parameters are not exposed through the public Mojom APIs, for security
// reasons.
struct ChromeOsEnterpriseParams {
  bool suppress_user_dialogs = false;
  bool suppress_notifications = false;
  bool terminate_upon_input = false;
};

}  // namespace remoting

#endif  // REMOTING_HOST_CHROMEOS_CHROMEOS_ENTERPRISE_PARAMS_H_
