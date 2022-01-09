// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_PCIGUARD_FAKE_PCIGUARD_CLIENT_H_
#define CHROMEOS_DBUS_PCIGUARD_FAKE_PCIGUARD_CLIENT_H_

#include <map>

#include "base/component_export.h"
#include "chromeos/dbus/pciguard/pciguard_client.h"

namespace chromeos {

class COMPONENT_EXPORT(PCIGUARD) FakePciguardClient : public PciguardClient {
 public:
  FakePciguardClient();
  ~FakePciguardClient() override;

  FakePciguardClient(const FakePciguardClient&) = delete;
  FakePciguardClient& operator=(const FakePciguardClient&) = delete;

  // Simple fake to simulate emitting a D-Bus signal that a blocked deviced
  // has been plugged in.
  void EmitDeviceBlockedSignal(const std::string& device_name);

  // PciguardClient:
  void SendExternalPciDevicesPermissionState(bool permitted) override;
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_PCIGUARD_FAKE_PCIGUARD_CLIENT_H_
