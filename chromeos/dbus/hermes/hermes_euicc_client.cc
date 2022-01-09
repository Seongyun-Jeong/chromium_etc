// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/hermes/hermes_euicc_client.h"

#include "base/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chromeos/dbus/hermes/constants.h"
#include "chromeos/dbus/hermes/fake_hermes_euicc_client.h"
#include "chromeos/dbus/hermes/hermes_response_status.h"
#include "components/device_event_log/device_event_log.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "dbus/property.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/cros_system_api/dbus/hermes/dbus-constants.h"

namespace chromeos {

namespace {
HermesEuiccClient* g_instance = nullptr;
}  // namespace

HermesEuiccClient::Properties::Properties(
    dbus::ObjectProxy* object_proxy,
    const PropertyChangedCallback& callback)
    : dbus::PropertySet(object_proxy, hermes::kHermesEuiccInterface, callback) {
  RegisterProperty(hermes::euicc::kEidProperty, &eid_);
  RegisterProperty(hermes::euicc::kIsActiveProperty, &is_active_);
  RegisterProperty(hermes::euicc::kInstalledProfilesProperty,
                   &installed_carrier_profiles_);
  RegisterProperty(hermes::euicc::kPendingProfilesProperty,
                   &pending_carrier_profiles_);
  RegisterProperty(hermes::euicc::kPhysicalSlotProperty, &physical_slot_);
}

HermesEuiccClient::Properties::~Properties() = default;

class HermesEuiccClientImpl : public HermesEuiccClient {
 public:
  explicit HermesEuiccClientImpl(dbus::Bus* bus) : bus_(bus) {}
  explicit HermesEuiccClientImpl(const HermesEuiccClient&) = delete;
  ~HermesEuiccClientImpl() override = default;

  using ProxyPropertiesPair =
      std::pair<dbus::ObjectProxy*, std::unique_ptr<Properties>>;
  using ObjectMap = std::map<dbus::ObjectPath, ProxyPropertiesPair>;

  // HermesEuiccClient:
  void InstallProfileFromActivationCode(
      const dbus::ObjectPath& euicc_path,
      const std::string& activation_code,
      const std::string& confirmation_code,
      InstallCarrierProfileCallback callback) override {
    dbus::MethodCall method_call(
        hermes::kHermesEuiccInterface,
        hermes::euicc::kInstallProfileFromActivationCode);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(activation_code);
    writer.AppendString(confirmation_code);
    dbus::ObjectProxy* object_proxy = GetOrCreateProperties(euicc_path).first;
    object_proxy->CallMethodWithErrorResponse(
        &method_call, hermes_constants::kHermesNetworkOperationTimeoutMs,
        base::BindOnce(&HermesEuiccClientImpl::OnProfileInstallResponse,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
  }

  void InstallPendingProfile(const dbus::ObjectPath& euicc_path,
                             const dbus::ObjectPath& carrier_profile_path,
                             const std::string& confirmation_code,
                             HermesResponseCallback callback) override {
    dbus::MethodCall method_call(hermes::kHermesEuiccInterface,
                                 hermes::euicc::kInstallPendingProfile);
    dbus::MessageWriter writer(&method_call);
    writer.AppendObjectPath(carrier_profile_path);
    writer.AppendString(confirmation_code);
    dbus::ObjectProxy* object_proxy = GetOrCreateProperties(euicc_path).first;
    object_proxy->CallMethodWithErrorResponse(
        &method_call, hermes_constants::kHermesNetworkOperationTimeoutMs,
        base::BindOnce(&HermesEuiccClientImpl::OnHermesStatusResponse,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
  }

  void RequestInstalledProfiles(const dbus::ObjectPath& euicc_path,
                                HermesResponseCallback callback) override {
    dbus::MethodCall method_call(hermes::kHermesEuiccInterface,
                                 hermes::euicc::kRequestInstalledProfiles);
    dbus::ObjectProxy* object_proxy = GetOrCreateProperties(euicc_path).first;
    object_proxy->CallMethodWithErrorResponse(
        &method_call, hermes_constants::kHermesNetworkOperationTimeoutMs,
        base::BindOnce(&HermesEuiccClientImpl::OnHermesStatusResponse,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
  }

  void RequestPendingProfiles(const dbus::ObjectPath& euicc_path,
                              const std::string& root_smds,
                              HermesResponseCallback callback) override {
    dbus::MethodCall method_call(hermes::kHermesEuiccInterface,
                                 hermes::euicc::kRequestPendingProfiles);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(root_smds);
    dbus::ObjectProxy* object_proxy = GetOrCreateProperties(euicc_path).first;
    object_proxy->CallMethodWithErrorResponse(
        &method_call, hermes_constants::kHermesNetworkOperationTimeoutMs,
        base::BindOnce(&HermesEuiccClientImpl::OnHermesStatusResponse,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
  }

  void UninstallProfile(const dbus::ObjectPath& euicc_path,
                        const dbus::ObjectPath& carrier_profile_path,
                        HermesResponseCallback callback) override {
    dbus::MethodCall method_call(hermes::kHermesEuiccInterface,
                                 hermes::euicc::kUninstallProfile);
    dbus::MessageWriter writer(&method_call);
    writer.AppendObjectPath(carrier_profile_path);
    dbus::ObjectProxy* object_proxy = GetOrCreateProperties(euicc_path).first;
    object_proxy->CallMethodWithErrorResponse(
        &method_call, hermes_constants::kHermesNetworkOperationTimeoutMs,
        base::BindOnce(&HermesEuiccClientImpl::OnHermesStatusResponse,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
  }

  void ResetMemory(const dbus::ObjectPath& euicc_path,
                   hermes::euicc::ResetOptions reset_option,
                   HermesResponseCallback callback) override {
    dbus::MethodCall method_call(hermes::kHermesEuiccInterface,
                                 hermes::euicc::kResetMemory);
    dbus::MessageWriter writer(&method_call);
    writer.AppendInt32(static_cast<int32_t>(reset_option));
    dbus::ObjectProxy* object_proxy = GetOrCreateProperties(euicc_path).first;
    object_proxy->CallMethodWithErrorResponse(
        &method_call, hermes_constants::kHermesNetworkOperationTimeoutMs,
        base::BindOnce(&HermesEuiccClientImpl::OnResetMemoryResponse,
                       weak_ptr_factory_.GetWeakPtr(), euicc_path,
                       std::move(callback)));
  }

  Properties* GetProperties(const dbus::ObjectPath& euicc_path) override {
    return GetOrCreateProperties(euicc_path).second.get();
  }

  TestInterface* GetTestInterface() override { return nullptr; }

  HermesEuiccClient& operator=(const HermesEuiccClient&) = delete;

 private:
  const ProxyPropertiesPair& GetOrCreateProperties(
      const dbus::ObjectPath& euicc_path) {
    auto it = object_map_.find(euicc_path);
    if (it != object_map_.end())
      return it->second;

    dbus::ObjectProxy* object_proxy =
        bus_->GetObjectProxy(hermes::kHermesServiceName, euicc_path);

    auto properties = std::make_unique<Properties>(
        object_proxy,
        base::BindRepeating(&HermesEuiccClientImpl::OnPropertyChanged,
                            weak_ptr_factory_.GetWeakPtr(), euicc_path));
    properties->ConnectSignals();
    properties->GetAll();

    object_map_[euicc_path] =
        std::make_pair(object_proxy, std::move(properties));
    return object_map_[euicc_path];
  }

  void OnPropertyChanged(const dbus::ObjectPath& euicc_path,
                         const std::string& property_name) {
    for (auto& observer : observers()) {
      observer.OnEuiccPropertyChanged(euicc_path, property_name);
    }
  }

  void OnProfileInstallResponse(InstallCarrierProfileCallback callback,
                                dbus::Response* response,
                                dbus::ErrorResponse* error_response) {
    if (error_response) {
      NET_LOG(ERROR) << "Profile install failed with error: "
                     << error_response->GetErrorName();
      std::move(callback).Run(
          HermesResponseStatusFromErrorName(error_response->GetErrorName()),
          nullptr);
      return;
    }

    if (!response) {
      // No Error or Response received.
      NET_LOG(ERROR) << "Carrier profile installation Error: No error or "
                        "response received.";
      std::move(callback).Run(HermesResponseStatus::kErrorNoResponse, nullptr);
      return;
    }

    dbus::MessageReader reader(response);
    dbus::ObjectPath profile_path;
    reader.PopObjectPath(&profile_path);
    std::move(callback).Run(HermesResponseStatus::kSuccess, &profile_path);
  }

  void OnHermesStatusResponse(HermesResponseCallback callback,
                              dbus::Response* response,
                              dbus::ErrorResponse* error_response) {
    if (error_response) {
      NET_LOG(ERROR) << "Hermes Euicc operation failed with error: "
                     << error_response->GetErrorName();
      std::move(callback).Run(
          HermesResponseStatusFromErrorName(error_response->GetErrorName()));
      return;
    }
    std::move(callback).Run(HermesResponseStatus::kSuccess);
  }

  void OnResetMemoryResponse(const dbus::ObjectPath& euicc_path,
                             HermesResponseCallback callback,
                             dbus::Response* response,
                             dbus::ErrorResponse* error_response) {
    OnHermesStatusResponse(std::move(callback), response, error_response);

    if (error_response) {
      return;
    }

    for (auto& observer : observers()) {
      observer.OnEuiccReset(euicc_path);
    }
  }

  dbus::Bus* bus_;
  ObjectMap object_map_;
  base::WeakPtrFactory<HermesEuiccClientImpl> weak_ptr_factory_{this};
};

HermesEuiccClient::HermesEuiccClient() {
  DCHECK(!g_instance);
  g_instance = this;
}

HermesEuiccClient::~HermesEuiccClient() {
  DCHECK_EQ(g_instance, this);
  g_instance = nullptr;
}

void HermesEuiccClient::AddObserver(Observer* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void HermesEuiccClient::RemoveObserver(Observer* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

// static
void HermesEuiccClient::Initialize(dbus::Bus* bus) {
  DCHECK(bus);
  DCHECK(!g_instance);
  new HermesEuiccClientImpl(bus);
}

// static
void HermesEuiccClient::InitializeFake() {
  new FakeHermesEuiccClient();
}

// static
void HermesEuiccClient::Shutdown() {
  DCHECK(g_instance);
  delete g_instance;
}

// static
HermesEuiccClient* HermesEuiccClient::Get() {
  return g_instance;
}

}  // namespace chromeos
