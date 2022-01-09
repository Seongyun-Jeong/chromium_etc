// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_BLUETOOTH_CONFIG_FAKE_DEVICE_OPERATION_HANDLER_H_
#define CHROMEOS_SERVICES_BLUETOOTH_CONFIG_FAKE_DEVICE_OPERATION_HANDLER_H_

#include "chromeos/services/bluetooth_config/adapter_state_controller.h"
#include "chromeos/services/bluetooth_config/device_operation_handler.h"

namespace chromeos {
namespace bluetooth_config {

class FakeDeviceOperationHandler : public DeviceOperationHandler {
 public:
  explicit FakeDeviceOperationHandler(
      AdapterStateController* adapter_state_controller);
  ~FakeDeviceOperationHandler() override;

  void CompleteCurrentOperation(bool success);

 private:
  // DeviceOperationHandler:
  void PerformConnect(const std::string& device_id) override;
  void PerformDisconnect(const std::string& device_id) override;
  void PerformForget(const std::string& device_id) override;
  void HandleOperationTimeout(const PendingOperation& operation) override {}
  device::BluetoothDevice* FindDevice(
      const std::string& device_id) const override;
  void RecordUserInitiatedReconnectionMetrics(
      const device::BluetoothTransport transport,
      absl::optional<base::Time> reconnection_attempt_start,
      absl::optional<device::BluetoothDevice::ConnectErrorCode> error_code)
      const override {}
};

}  // namespace bluetooth_config
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_BLUETOOTH_CONFIG_FAKE_DEVICE_OPERATION_HANDLER_H_
