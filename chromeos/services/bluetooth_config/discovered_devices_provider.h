// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_BLUETOOTH_CONFIG_DISCOVERED_DEVICES_PROVIDER_H_
#define CHROMEOS_SERVICES_BLUETOOTH_CONFIG_DISCOVERED_DEVICES_PROVIDER_H_

#include <vector>

#include "chromeos/services/bluetooth_config/public/mojom/cros_bluetooth_config.mojom.h"

namespace chromeos {
namespace bluetooth_config {

// Provides clients with the list of unpaired devices found during a discovery
// session.
class DiscoveredDevicesProvider {
 public:
  class Observer : public base::CheckedObserver {
   public:
    ~Observer() override = default;

    // Invoked when the list of discovered devices has changed. This callback is
    // used when a device has been added/removed from the list, or when one or
    // more properties of a device in the list has changed.
    virtual void OnDiscoveredDevicesListChanged() = 0;
  };

  virtual ~DiscoveredDevicesProvider();

  // Returns the list of discovered devices.
  virtual std::vector<mojom::BluetoothDevicePropertiesPtr>
  GetDiscoveredDevices() const = 0;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 protected:
  DiscoveredDevicesProvider();

  void NotifyDiscoveredDevicesListChanged();

  base::ObserverList<Observer> observers_;
};

}  // namespace bluetooth_config
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_BLUETOOTH_CONFIG_DISCOVERED_DEVICES_PROVIDER_H_
