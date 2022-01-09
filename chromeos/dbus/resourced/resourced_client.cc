// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/resourced/resourced_client.h"

#include "base/check_op.h"
#include "base/logging.h"
#include "base/observer_list.h"
#include "base/process/process_metrics.h"
#include "chromeos/dbus/resourced/fake_resourced_client.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_proxy.h"
#include "third_party/cros_system_api/dbus/resource_manager/dbus-constants.h"

namespace chromeos {
namespace {

// Resource manager D-Bus method calls are all simple operations and should
// not take more than 1 second.
constexpr int kResourcedDBusTimeoutMilliseconds = 1000;

ResourcedClient* g_instance = nullptr;

class ResourcedClientImpl : public ResourcedClient {
 public:
  ResourcedClientImpl();
  ~ResourcedClientImpl() override = default;
  ResourcedClientImpl(const ResourcedClientImpl&) = delete;
  ResourcedClientImpl& operator=(const ResourcedClientImpl&) = delete;

  void Init(dbus::Bus* bus) {
    proxy_ = bus->GetObjectProxy(
        resource_manager::kResourceManagerServiceName,
        dbus::ObjectPath(resource_manager::kResourceManagerServicePath));
    proxy_->ConnectToSignal(
        resource_manager::kResourceManagerInterface,
        resource_manager::kMemoryPressureChrome,
        base::BindRepeating(&ResourcedClientImpl::MemoryPressureReceived,
                            weak_factory_.GetWeakPtr()),
        base::BindOnce(&ResourcedClientImpl::MemoryPressureConnected,
                       weak_factory_.GetWeakPtr()));
    proxy_->ConnectToSignal(
        resource_manager::kResourceManagerInterface,
        resource_manager::kMemoryPressureArcvm,
        base::BindRepeating(&ResourcedClientImpl::MemoryPressureArcVmReceived,
                            weak_factory_.GetWeakPtr()),
        base::BindOnce(&ResourcedClientImpl::MemoryPressureConnected,
                       weak_factory_.GetWeakPtr()));
  }

  // ResourcedClient interface.
  void SetGameModeWithTimeout(bool state,
                              uint32_t refresh_seconds,
                              DBusMethodCallback<bool> callback) override;

  void AddObserver(Observer* observer) override;

  void RemoveObserver(Observer* observer) override;

  void AddArcVmObserver(ArcVmObserver* observer) override;

  void RemoveArcVmObserver(ArcVmObserver* observer) override;

 private:
  // D-Bus response handlers.
  void HandleSetGameModeWithTimeoutResponse(DBusMethodCallback<bool> callback,
                                            dbus::Response* response);

  // D-Bus signal handlers.
  void MemoryPressureReceived(dbus::Signal* signal);
  void MemoryPressureConnected(const std::string& interface_name,
                               const std::string& signal_name,
                               bool success);

  void MemoryPressureArcVmReceived(dbus::Signal* signal);

  // Member variables.

  dbus::ObjectProxy* proxy_ = nullptr;

  // Caches the total memory for reclaim_target_kb sanity check. The default
  // value is 32 GiB in case reading total memory failed.
  uint64_t total_memory_kb_ = 32 * 1024 * 1024;

  // A list of observers that are listening on state changes, etc.
  base::ObserverList<Observer> observers_;

  // A list of observers listening for ARCVM memory pressure signals.
  base::ObserverList<ArcVmObserver> arcvm_observers_;

  base::WeakPtrFactory<ResourcedClientImpl> weak_factory_{this};
};

ResourcedClientImpl::ResourcedClientImpl() {
  base::SystemMemoryInfoKB info;
  if (base::GetSystemMemoryInfo(&info)) {
    total_memory_kb_ = static_cast<uint64_t>(info.total);
  } else {
    PLOG(ERROR) << "Error reading total memory.";
  }
}

void ResourcedClientImpl::MemoryPressureReceived(dbus::Signal* signal) {
  dbus::MessageReader signal_reader(signal);

  uint8_t pressure_level_byte;
  PressureLevel pressure_level;
  uint64_t reclaim_target_kb;

  if (!signal_reader.PopByte(&pressure_level_byte) ||
      !signal_reader.PopUint64(&reclaim_target_kb)) {
    LOG(ERROR) << "Error reading signal from resourced: " << signal->ToString();
    return;
  }

  if (pressure_level_byte == resource_manager::PressureLevelChrome::NONE) {
    pressure_level = PressureLevel::NONE;
  } else if (pressure_level_byte ==
             resource_manager::PressureLevelChrome::MODERATE) {
    pressure_level = PressureLevel::MODERATE;
  } else if (pressure_level_byte ==
             resource_manager::PressureLevelChrome::CRITICAL) {
    pressure_level = PressureLevel::CRITICAL;
  } else {
    LOG(ERROR) << "Unknown memory pressure level: " << pressure_level_byte;
    return;
  }

  if (reclaim_target_kb > total_memory_kb_) {
    LOG(ERROR) << "reclaim_target_kb is too large: " << reclaim_target_kb;
    return;
  }

  for (auto& observer : observers_) {
    observer.OnMemoryPressure(pressure_level, reclaim_target_kb);
  }
}

void ResourcedClientImpl::MemoryPressureArcVmReceived(dbus::Signal* signal) {
  dbus::MessageReader signal_reader(signal);

  uint8_t pressure_level_byte;
  PressureLevelArcVm pressure_level;
  uint64_t reclaim_target_kb;

  if (!signal_reader.PopByte(&pressure_level_byte) ||
      !signal_reader.PopUint64(&reclaim_target_kb)) {
    LOG(ERROR) << "Error reading signal from resourced: " << signal->ToString();
    return;
  }
  switch (
      static_cast<resource_manager::PressureLevelArcvm>(pressure_level_byte)) {
    case resource_manager::PressureLevelArcvm::NONE:
      pressure_level = PressureLevelArcVm::NONE;
      break;

    case resource_manager::PressureLevelArcvm::CACHED:
      pressure_level = PressureLevelArcVm::CACHED;
      break;

    case resource_manager::PressureLevelArcvm::PERCEPTIBLE:
      pressure_level = PressureLevelArcVm::PERCEPTIBLE;
      break;

    case resource_manager::PressureLevelArcvm::FOREGROUND:
      pressure_level = PressureLevelArcVm::FOREGROUND;
      break;

    default:
      LOG(ERROR) << "Unknown memory pressure level: " << pressure_level_byte;
      return;
  }

  if (reclaim_target_kb > total_memory_kb_) {
    LOG(ERROR) << "reclaim_target_kb is too large: " << reclaim_target_kb;
    return;
  }

  for (auto& observer : arcvm_observers_) {
    observer.OnMemoryPressure(pressure_level, reclaim_target_kb);
  }
}

void ResourcedClientImpl::MemoryPressureConnected(
    const std::string& interface_name,
    const std::string& signal_name,
    bool success) {
  PLOG_IF(ERROR, !success) << "Failed to connect to signal: " << signal_name;
}

// Response will be true if game mode was on previously, false otherwise.
void ResourcedClientImpl::HandleSetGameModeWithTimeoutResponse(
    DBusMethodCallback<bool> callback,
    dbus::Response* response) {
  dbus::MessageReader reader(response);
  uint8_t previous;
  if (!reader.PopByte(&previous)) {
    std::move(callback).Run(absl::nullopt);
    return;
  }
  std::move(callback).Run(previous);
}

void ResourcedClientImpl::SetGameModeWithTimeout(
    bool status,
    uint32_t refresh_seconds,
    DBusMethodCallback<bool> callback) {
  dbus::MethodCall method_call(resource_manager::kResourceManagerInterface,
                               resource_manager::kSetGameModeWithTimeoutMethod);
  dbus::MessageWriter writer(&method_call);
  writer.AppendByte(status);
  writer.AppendUint32(refresh_seconds);

  proxy_->CallMethod(
      &method_call, kResourcedDBusTimeoutMilliseconds,
      base::BindOnce(&ResourcedClientImpl::HandleSetGameModeWithTimeoutResponse,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void ResourcedClientImpl::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ResourcedClientImpl::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ResourcedClientImpl::AddArcVmObserver(ArcVmObserver* observer) {
  arcvm_observers_.AddObserver(observer);
}

void ResourcedClientImpl::RemoveArcVmObserver(ArcVmObserver* observer) {
  arcvm_observers_.RemoveObserver(observer);
}

}  // namespace

ResourcedClient::ResourcedClient() {
  CHECK(!g_instance);
  g_instance = this;
}

ResourcedClient::~ResourcedClient() {
  CHECK_EQ(this, g_instance);
  g_instance = nullptr;
}

// static
void ResourcedClient::Initialize(dbus::Bus* bus) {
  CHECK(bus);
  (new ResourcedClientImpl())->Init(bus);
}

// static
void ResourcedClient::InitializeFake() {
  new FakeResourcedClient();
}

// static
void ResourcedClient::Shutdown() {
  CHECK(g_instance);
  delete g_instance;
  // The destructor resets |g_instance|.
  DCHECK(!g_instance);
}

// static
ResourcedClient* ResourcedClient::Get() {
  return g_instance;
}

}  // namespace chromeos
