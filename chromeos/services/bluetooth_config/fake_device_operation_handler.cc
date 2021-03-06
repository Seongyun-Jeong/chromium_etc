// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/bluetooth_config/fake_device_operation_handler.h"

namespace chromeos {
namespace bluetooth_config {

FakeDeviceOperationHandler::FakeDeviceOperationHandler(
    AdapterStateController* adapter_state_controller)
    : DeviceOperationHandler(adapter_state_controller) {}

FakeDeviceOperationHandler::~FakeDeviceOperationHandler() = default;

void FakeDeviceOperationHandler::CompleteCurrentOperation(bool success) {
  HandleFinishedOperation(success);
}

void FakeDeviceOperationHandler::PerformConnect(const std::string& device_id) {}

void FakeDeviceOperationHandler::PerformDisconnect(
    const std::string& device_id) {}

void FakeDeviceOperationHandler::PerformForget(const std::string& device_id) {}

device::BluetoothDevice* FakeDeviceOperationHandler::FindDevice(
    const std::string& device_id) const {
  return nullptr;
}

}  // namespace bluetooth_config
}  // namespace chromeos
