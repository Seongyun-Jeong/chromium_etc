// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/bluetooth_config/device_operation_handler_impl.h"

#include "base/time/default_clock.h"
#include "base/time/time.h"
#include "components/device_event_log/device_event_log.h"
#include "device/bluetooth/bluetooth_common.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/chromeos/bluetooth_utils.h"

namespace chromeos {
namespace {

device::ConnectionFailureReason GetConnectionFailureReason(
    device::BluetoothDevice::ConnectErrorCode error_code) {
  switch (error_code) {
    case device::BluetoothDevice::ConnectErrorCode::ERROR_AUTH_FAILED:
      return device::ConnectionFailureReason::kAuthFailed;
    case device::BluetoothDevice::ConnectErrorCode::ERROR_AUTH_TIMEOUT:
      return device::ConnectionFailureReason::kAuthTimeout;
    case device::BluetoothDevice::ConnectErrorCode::ERROR_FAILED:
      return device::ConnectionFailureReason::kFailed;
    case device::BluetoothDevice::ConnectErrorCode::ERROR_UNKNOWN:
      return device::ConnectionFailureReason::kUnknownConnectionError;
    case device::BluetoothDevice::ConnectErrorCode::ERROR_UNSUPPORTED_DEVICE:
      return device::ConnectionFailureReason::kUnsupportedDevice;
    default:
      return device::ConnectionFailureReason::kUnknownError;
  }
}

}  // namespace
namespace bluetooth_config {

DeviceOperationHandlerImpl::DeviceOperationHandlerImpl(
    AdapterStateController* adapter_state_controller,
    scoped_refptr<device::BluetoothAdapter> bluetooth_adapter,
    DeviceNameManager* device_name_manager)
    : DeviceOperationHandler(adapter_state_controller),
      bluetooth_adapter_(std::move(bluetooth_adapter)),
      device_name_manager_(device_name_manager) {}

DeviceOperationHandlerImpl::~DeviceOperationHandlerImpl() = default;

void DeviceOperationHandlerImpl::PerformConnect(const std::string& device_id) {
  device::BluetoothDevice* device = FindDevice(device_id);
  last_reconnection_attempt_start_ = base::DefaultClock::GetInstance()->Now();
  if (!device) {
    BLUETOOTH_LOG(ERROR) << "Connect failed due to device not being "
                            "found, device id: "
                         << device_id;
    RecordUserInitiatedReconnectionMetrics(
        device::BluetoothTransport::BLUETOOTH_TRANSPORT_INVALID,
        /*reconnection_attempt_start=*/absl::nullopt,
        device::BluetoothDevice::ConnectErrorCode::ERROR_FAILED);
    HandleFinishedOperation(/*success=*/false);
    return;
  }

  device->Connect(
      /*delegate=*/nullptr,
      base::BindOnce(&DeviceOperationHandlerImpl::OnDeviceConnect,
                     weak_ptr_factory_.GetWeakPtr(), device->GetType()));
}

void DeviceOperationHandlerImpl::PerformDisconnect(
    const std::string& device_id) {
  device::BluetoothDevice* device = FindDevice(device_id);
  if (!device) {
    BLUETOOTH_LOG(ERROR) << "Disconnect failed due to device not being "
                            "found, device id: "
                         << device_id;
    HandleFinishedOperation(/*success=*/false);
    return;
  }

  device->Disconnect(
      base::BindOnce(&DeviceOperationHandlerImpl::HandleFinishedOperation,
                     weak_ptr_factory_.GetWeakPtr(), /*success=*/true),
      base::BindOnce(&DeviceOperationHandlerImpl::HandleFinishedOperation,
                     weak_ptr_factory_.GetWeakPtr(), /*success=*/false));
}

void DeviceOperationHandlerImpl::PerformForget(const std::string& device_id) {
  device::BluetoothDevice* device = FindDevice(device_id);
  if (!device) {
    BLUETOOTH_LOG(ERROR) << "Forget failed due to device not being "
                            "found, device id: "
                         << device_id;
    HandleFinishedOperation(/*success=*/false);
    return;
  }

  // We do not expect "Forget" operations to ever fail, so don't bother passing
  // success and failure callbacks here.
  device->Forget(base::DoNothing(), base::BindOnce(
                                        [](const std::string device_id) {
                                          BLUETOOTH_LOG(ERROR)
                                              << "Forget failed, device id: "
                                              << device_id;
                                        },
                                        device_id));

  device_name_manager_->RemoveDeviceNickname(device_id);
  HandleFinishedOperation(/*success=*/true);
}

void DeviceOperationHandlerImpl::HandleOperationTimeout(
    const PendingOperation& operation) {
  // Invalidate all BluetoothDevice callbacks for the current operation.
  weak_ptr_factory_.InvalidateWeakPtrs();

  if (operation.operation != Operation::kConnect) {
    return;
  }

  RecordUserInitiatedReconnectionMetrics(
      operation.transport_type, last_reconnection_attempt_start_,
      device::BluetoothDevice::ConnectErrorCode::ERROR_FAILED);
}

device::BluetoothDevice* DeviceOperationHandlerImpl::FindDevice(
    const std::string& device_id) const {
  for (auto* device : bluetooth_adapter_->GetDevices()) {
    if (device->GetIdentifier() != device_id)
      continue;
    return device;
  }
  return nullptr;
}

void DeviceOperationHandlerImpl::RecordUserInitiatedReconnectionMetrics(
    const device::BluetoothTransport transport,
    absl::optional<base::Time> reconnection_attempt_start,
    absl::optional<device::BluetoothDevice::ConnectErrorCode> error_code)
    const {
  absl::optional<device::ConnectionFailureReason> failure_reason =
      error_code ? absl::make_optional(GetConnectionFailureReason(*error_code))
                 : absl::nullopt;
  device::RecordUserInitiatedReconnectionAttemptResult(
      failure_reason, device::UserInitiatedReconnectionUISurfaces::kSettings);
  if (reconnection_attempt_start) {
    device::RecordUserInitiatedReconnectionAttemptDuration(
        failure_reason, transport,
        base::DefaultClock::GetInstance()->Now() -
            reconnection_attempt_start.value());
  }
}

void DeviceOperationHandlerImpl::OnDeviceConnect(
    device::BluetoothTransport transport,
    absl::optional<device::BluetoothDevice::ConnectErrorCode> error_code) {
  if (error_code.has_value()) {
    BLUETOOTH_LOG(ERROR) << "Connect failed with error code: "
                         << error_code.value();
  }

  RecordUserInitiatedReconnectionMetrics(
      transport, last_reconnection_attempt_start_, error_code);

  HandleFinishedOperation(!error_code.has_value());
}

}  // namespace bluetooth_config
}  // namespace chromeos
