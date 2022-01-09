// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_DEVICE_SYNC_FAKE_SYNCED_BLUETOOTH_ADDRESS_TRACKER_H_
#define CHROMEOS_SERVICES_DEVICE_SYNC_FAKE_SYNCED_BLUETOOTH_ADDRESS_TRACKER_H_

#include "chromeos/services/device_sync/cryptauth_v2_device_sync_test_devices.h"
#include "chromeos/services/device_sync/synced_bluetooth_address_tracker.h"
#include "chromeos/services/device_sync/synced_bluetooth_address_tracker_impl.h"

namespace chromeos {

namespace device_sync {

class FakeSyncedBluetoothAddressTracker : public SyncedBluetoothAddressTracker {
 public:
  FakeSyncedBluetoothAddressTracker();
  ~FakeSyncedBluetoothAddressTracker() override;

  void set_bluetooth_address(const std::string& bluetooth_address) {
    bluetooth_address_ = bluetooth_address;
  }

  const std::string& last_synced_bluetooth_address() const {
    return last_synced_bluetooth_address_;
  }

 private:
  // SyncedBluetoothAddressTracker:
  void GetBluetoothAddress(BluetoothAddressCallback callback) override;
  void SetLastSyncedBluetoothAddress(
      const std::string& last_synced_bluetooth_address) override;

  std::string bluetooth_address_ = kDefaultLocalDeviceBluetoothAddress;
  std::string last_synced_bluetooth_address_;
};

class FakeSyncedBluetoothAddressTrackerFactory
    : public SyncedBluetoothAddressTrackerImpl::Factory {
 public:
  FakeSyncedBluetoothAddressTrackerFactory();
  ~FakeSyncedBluetoothAddressTrackerFactory() override;

  SyncedBluetoothAddressTracker* last_created() { return last_created_; }

 private:
  // SyncedBluetoothAddressTracker::Factory:
  std::unique_ptr<SyncedBluetoothAddressTracker> CreateInstance(
      CryptAuthScheduler* cryptauth_scheduler,
      PrefService* pref_service) override;

  SyncedBluetoothAddressTracker* last_created_ = nullptr;
};

}  // namespace device_sync

}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_DEVICE_SYNC_FAKE_SYNCED_BLUETOOTH_ADDRESS_TRACKER_H_
