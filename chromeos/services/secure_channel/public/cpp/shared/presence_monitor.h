// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_SECURE_CHANNEL_PUBLIC_CPP_SHARED_PRESENCE_MONITOR_H_
#define CHROMEOS_SERVICES_SECURE_CHANNEL_PUBLIC_CPP_SHARED_PRESENCE_MONITOR_H_

#include "base/callback.h"

namespace chromeos {

namespace multidevice {
struct RemoteDevice;
}  // namespace multidevice

namespace secure_channel {

// Monitors device proximity while a secure channel is active.
class PresenceMonitor {
 public:
  using ReadyCallback = base::RepeatingCallback<void()>;
  using DeviceSeenCallback = base::RepeatingCallback<void()>;

  virtual ~PresenceMonitor() = default;

  PresenceMonitor(const PresenceMonitor&) = delete;
  PresenceMonitor& operator=(const PresenceMonitor&) = delete;

  virtual void SetPresenceMonitorCallbacks(
      ReadyCallback ready_callback,
      DeviceSeenCallback device_seen_callback) = 0;
  virtual void StartMonitoring(
      const multidevice::RemoteDevice& remote_device,
      const multidevice::RemoteDevice& local_device) = 0;
  virtual void StopMonitoring() = 0;

 protected:
  PresenceMonitor() = default;
};

}  // namespace secure_channel
}  // namespace chromeos
#endif  // CHROMEOS_SERVICES_SECURE_CHANNEL_PUBLIC_CPP_SHARED_PRESENCE_MONITOR_H_
