// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_DEVICE_SYNC_CRYPTAUTH_V2_DEVICE_MANAGER_IMPL_H_
#define CHROMEOS_SERVICES_DEVICE_SYNC_CRYPTAUTH_V2_DEVICE_MANAGER_IMPL_H_

#include <memory>
#include <ostream>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chromeos/services/device_sync/cryptauth_device_registry.h"
#include "chromeos/services/device_sync/cryptauth_device_sync_result.h"
#include "chromeos/services/device_sync/cryptauth_gcm_manager.h"
#include "chromeos/services/device_sync/cryptauth_scheduler.h"
#include "chromeos/services/device_sync/cryptauth_v2_device_manager.h"
#include "chromeos/services/device_sync/proto/cryptauth_client_app_metadata.pb.h"
#include "chromeos/services/device_sync/proto/cryptauth_common.pb.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

class PrefService;

namespace chromeos {

namespace device_sync {

class CryptAuthClientFactory;
class CryptAuthDeviceSyncer;
class CryptAuthKeyRegistry;
class SyncedBluetoothAddressTracker;

// Implementation of CryptAuthV2DeviceManager that considers three sources of
// DeviceSync requests:
//  1) The scheduler requests a DeviceSync to recover from a failed attempt or
//     after receiving an InvokeNext instruction from CryptAuth in a
//     ClientDirective.
//  2) The device manager listens to the GCM manager for re-sync requests.
//  3) The ForceDeviceSyncNow() method allows for immediate requests.
class CryptAuthV2DeviceManagerImpl
    : public CryptAuthV2DeviceManager,
      public CryptAuthScheduler::DeviceSyncDelegate,
      public CryptAuthGCMManager::Observer {
 public:
  class Factory {
   public:
    static std::unique_ptr<CryptAuthV2DeviceManager> Create(
        const cryptauthv2::ClientAppMetadata& client_app_metadata,
        CryptAuthDeviceRegistry* device_registry,
        CryptAuthKeyRegistry* key_registry,
        CryptAuthClientFactory* client_factory,
        CryptAuthGCMManager* gcm_manager,
        CryptAuthScheduler* scheduler,
        PrefService* pref_service);
    static void SetFactoryForTesting(Factory* test_factory);

   protected:
    virtual ~Factory();
    virtual std::unique_ptr<CryptAuthV2DeviceManager> CreateInstance(
        const cryptauthv2::ClientAppMetadata& client_app_metadata,
        CryptAuthDeviceRegistry* device_registry,
        CryptAuthKeyRegistry* key_registry,
        CryptAuthClientFactory* client_factory,
        CryptAuthGCMManager* gcm_manager,
        CryptAuthScheduler* scheduler,
        PrefService* pref_service) = 0;

   private:
    static Factory* test_factory_;
  };

  CryptAuthV2DeviceManagerImpl(const CryptAuthV2DeviceManagerImpl&) = delete;
  CryptAuthV2DeviceManagerImpl& operator=(const CryptAuthV2DeviceManagerImpl&) =
      delete;

  ~CryptAuthV2DeviceManagerImpl() override;

 protected:
  CryptAuthV2DeviceManagerImpl(
      const cryptauthv2::ClientAppMetadata& client_app_metadata,
      CryptAuthDeviceRegistry* device_registry,
      CryptAuthKeyRegistry* key_registry,
      CryptAuthClientFactory* client_factory,
      CryptAuthGCMManager* gcm_manager,
      CryptAuthScheduler* scheduler,
      PrefService* pref_service);

 private:
  // CryptAuthV2DeviceManager:
  void Start() override;
  const CryptAuthDeviceRegistry::InstanceIdToDeviceMap& GetSyncedDevices()
      const override;
  void ForceDeviceSyncNow(
      const cryptauthv2::ClientMetadata::InvocationReason&,
      const absl::optional<std::string>& session_id) override;
  bool IsDeviceSyncInProgress() const override;
  bool IsRecoveringFromFailure() const override;
  absl::optional<base::Time> GetLastDeviceSyncTime() const override;
  absl::optional<base::TimeDelta> GetTimeToNextAttempt() const override;

  // CryptAuthScheduler::DeviceSyncDelegate:
  void OnDeviceSyncRequested(
      const cryptauthv2::ClientMetadata& client_metadata) override;

  // CryptAuthGCMManager::Observer:
  void OnResyncMessage(
      const absl::optional<std::string>& session_id,
      const absl::optional<CryptAuthFeatureType>& feature_type) override;

  void OnDeviceSyncFinished(CryptAuthDeviceSyncResult device_sync_result);

  absl::optional<cryptauthv2::ClientMetadata> current_client_metadata_;
  std::unique_ptr<SyncedBluetoothAddressTracker>
      synced_bluetooth_address_tracker_;
  std::unique_ptr<CryptAuthDeviceSyncer> device_syncer_;

  cryptauthv2::ClientAppMetadata client_app_metadata_;
  CryptAuthDeviceRegistry* device_registry_ = nullptr;
  CryptAuthKeyRegistry* key_registry_ = nullptr;
  CryptAuthClientFactory* client_factory_ = nullptr;
  CryptAuthGCMManager* gcm_manager_ = nullptr;
  CryptAuthScheduler* scheduler_ = nullptr;
  PrefService* pref_service_ = nullptr;

  // For sending a weak pointer to the scheduler, whose lifetime exceeds that of
  // CryptAuthV2DeviceManagerImpl.
  base::WeakPtrFactory<CryptAuthV2DeviceManagerImpl>
      scheduler_weak_ptr_factory_{this};
};

}  // namespace device_sync

}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_DEVICE_SYNC_CRYPTAUTH_V2_DEVICE_MANAGER_IMPL_H_
