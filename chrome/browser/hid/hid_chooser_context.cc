// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/hid/hid_chooser_context.h"

#include <utility>

#include "base/containers/contains.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "content/public/browser/device_service.h"
#include "extensions/buildflags/buildflags.h"
#include "services/device/public/cpp/hid/hid_blocklist.h"
#include "ui/base/l10n/l10n_util.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "base/containers/contains.h"
#include "base/containers/fixed_flat_set.h"
#include "base/strings/string_piece.h"
#include "extensions/common/constants.h"
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

namespace {

constexpr char kHidDeviceNameKey[] = "name";
constexpr char kHidGuidKey[] = "guid";
constexpr char kHidVendorIdKey[] = "vendor-id";
constexpr char kHidProductIdKey[] = "product-id";
constexpr char kHidSerialNumberKey[] = "serial-number";

base::Value DeviceInfoToValue(const device::mojom::HidDeviceInfo& device) {
  base::Value value(base::Value::Type::DICTIONARY);
  value.SetStringKey(
      kHidDeviceNameKey,
      base::UTF16ToUTF8(HidChooserContext::DisplayNameFromDeviceInfo(device)));
  value.SetIntKey(kHidVendorIdKey, device.vendor_id);
  value.SetIntKey(kHidProductIdKey, device.product_id);
  if (HidChooserContext::CanStorePersistentEntry(device)) {
    // Use the USB serial number as a persistent identifier. If it is
    // unavailable, only ephemeral permissions may be granted.
    value.SetStringKey(kHidSerialNumberKey, device.serial_number);
  } else {
    // The GUID is a temporary ID created on connection that remains valid until
    // the device is disconnected. Ephemeral permissions are keyed by this ID
    // and must be granted again each time the device is connected.
    value.SetStringKey(kHidGuidKey, device.guid);
  }
  return value;
}

}  // namespace

void HidChooserContext::DeviceObserver::OnDeviceAdded(
    const device::mojom::HidDeviceInfo& device) {}

void HidChooserContext::DeviceObserver::OnDeviceRemoved(
    const device::mojom::HidDeviceInfo& device) {}

void HidChooserContext::DeviceObserver::OnDeviceChanged(
    const device::mojom::HidDeviceInfo& device) {}

void HidChooserContext::DeviceObserver::OnHidManagerConnectionError() {}

HidChooserContext::HidChooserContext(Profile* profile)
    : ObjectPermissionContextBase(
          ContentSettingsType::HID_GUARD,
          ContentSettingsType::HID_CHOOSER_DATA,
          HostContentSettingsMapFactory::GetForProfile(profile)),
      is_incognito_(profile->IsOffTheRecord()) {}

HidChooserContext::~HidChooserContext() {
  // Notify observers that the chooser context is about to be destroyed.
  // Observers must remove themselves from the observer lists.
  for (auto& observer : device_observer_list_) {
    observer.OnHidChooserContextShutdown();
    DCHECK(!device_observer_list_.HasObserver(&observer));
  }
  DCHECK(permission_observer_list_.empty());
}

// static
std::u16string HidChooserContext::DisplayNameFromDeviceInfo(
    const device::mojom::HidDeviceInfo& device) {
  if (device.product_name.empty()) {
    auto device_id_string = base::ASCIIToUTF16(
        base::StringPrintf("%04X:%04X", device.vendor_id, device.product_id));
    return l10n_util::GetStringFUTF16(IDS_HID_CHOOSER_ITEM_WITHOUT_NAME,
                                      device_id_string);
  }
  return base::UTF8ToUTF16(device.product_name);
}

// static
bool HidChooserContext::CanStorePersistentEntry(
    const device::mojom::HidDeviceInfo& device) {
  return !device.serial_number.empty() && !device.product_name.empty();
}

std::u16string HidChooserContext::GetObjectDisplayName(
    const base::Value& object) {
  const std::string* name = object.FindStringKey(kHidDeviceNameKey);
  DCHECK(name);
  return base::UTF8ToUTF16(*name);
}

std::string HidChooserContext::GetKeyForObject(const base::Value& object) {
  if (!IsValidObject(object))
    return std::string();
  return base::JoinString(
      {base::NumberToString(*(object.FindIntKey(kHidVendorIdKey))),
       base::NumberToString(*(object.FindIntKey(kHidProductIdKey))),
       *(object.FindStringKey(kHidSerialNumberKey))},
      "|");
}

bool HidChooserContext::IsValidObject(const base::Value& object) {
  if (!object.is_dict() || object.DictSize() != 4 ||
      !object.FindStringKey(kHidDeviceNameKey) ||
      !object.FindIntKey(kHidProductIdKey) ||
      !object.FindIntKey(kHidVendorIdKey)) {
    return false;
  }

  const std::string* guid = object.FindStringKey(kHidGuidKey);
  const std::string* serial_number = object.FindStringKey(kHidSerialNumberKey);
  return (guid && !guid->empty()) || (serial_number && !serial_number->empty());
}

std::vector<std::unique_ptr<permissions::ObjectPermissionContextBase::Object>>
HidChooserContext::GetGrantedObjects(const url::Origin& origin) {
  std::vector<std::unique_ptr<Object>> objects =
      ObjectPermissionContextBase::GetGrantedObjects(origin);

  if (CanRequestObjectPermission(origin)) {
    auto it = ephemeral_devices_.find(origin);
    if (it != ephemeral_devices_.end()) {
      for (const std::string& guid : it->second) {
        // |devices_| should be initialized when |ephemeral_devices_| is filled.
        // Because |ephemeral_devices_| is filled by GrantDevicePermission()
        // which is called in HidChooserController::Select(), this method will
        // always be called after device initialization in HidChooserController
        // which always returns after the device list initialization in this
        // class.
        DCHECK(base::Contains(devices_, guid));
        objects.push_back(std::make_unique<Object>(
            origin, DeviceInfoToValue(*devices_[guid]),
            content_settings::SettingSource::SETTING_SOURCE_USER,
            is_incognito_));
      }
    }
  }

  // TODO(crbug.com/1049825): Include policy-granted objects.

  return objects;
}

std::vector<std::unique_ptr<permissions::ObjectPermissionContextBase::Object>>
HidChooserContext::GetAllGrantedObjects() {
  std::vector<std::unique_ptr<Object>> objects =
      ObjectPermissionContextBase::GetAllGrantedObjects();

  for (const auto& map_entry : ephemeral_devices_) {
    const url::Origin& origin = map_entry.first;

    if (!CanRequestObjectPermission(origin))
      continue;

    for (const auto& guid : map_entry.second) {
      DCHECK(base::Contains(devices_, guid));
      objects.push_back(std::make_unique<Object>(
          origin, DeviceInfoToValue(*devices_[guid]),
          content_settings::SettingSource::SETTING_SOURCE_USER, is_incognito_));
    }
  }

  // TODO(crbug.com/1049825): Include policy-granted objects.

  return objects;
}

void HidChooserContext::RevokeObjectPermission(const url::Origin& origin,
                                               const base::Value& object) {
  const std::string* guid = object.FindStringKey(kHidGuidKey);

  if (!guid) {
    ObjectPermissionContextBase::RevokeObjectPermission(origin, object);
    // TODO(crbug.com/964041): Record UMA (WEBHID_PERMISSION_REVOKED).
    return;
  }

  auto it = ephemeral_devices_.find(origin);
  if (it != ephemeral_devices_.end()) {
    std::set<std::string>& devices = it->second;

    DCHECK(IsValidObject(object));
    devices.erase(*guid);
    if (devices.empty())
      ephemeral_devices_.erase(it);
    NotifyPermissionRevoked(origin);
  }

  // TODO(crbug.com/964041): Record UMA (WEBHID_PERMISSION_REVOKED_EPHEMERAL).
}

void HidChooserContext::GrantDevicePermission(
    const url::Origin& origin,
    const device::mojom::HidDeviceInfo& device) {
  DCHECK(base::Contains(devices_, device.guid));
  if (CanStorePersistentEntry(device)) {
    GrantObjectPermission(origin, DeviceInfoToValue(device));
  } else {
    ephemeral_devices_[origin].insert(device.guid);
    NotifyPermissionChanged();
  }
}

void HidChooserContext::RevokeDevicePermission(
    const url::Origin& origin,
    const device::mojom::HidDeviceInfo& device) {
  DCHECK(base::Contains(devices_, device.guid));
  if (CanStorePersistentEntry(device)) {
    RevokePersistentDevicePermission(origin, device);
  } else {
    RevokeEphemeralDevicePermission(origin, device);
  }
}

void HidChooserContext::RevokePersistentDevicePermission(
    const url::Origin& origin,
    const device::mojom::HidDeviceInfo& device) {
  std::vector<std::unique_ptr<Object>> object_list = GetGrantedObjects(origin);
  for (const auto& object : object_list) {
    const base::Value& device_value = object->value;
    DCHECK(IsValidObject(device_value));

    const auto* serial_number = device_value.FindStringKey(kHidSerialNumberKey);
    if (device.vendor_id == *device_value.FindIntKey(kHidVendorIdKey) &&
        device.product_id == *device_value.FindIntKey(kHidProductIdKey) &&
        serial_number && device.serial_number == *serial_number) {
      RevokeObjectPermission(origin, device_value);
    }
  }
}

void HidChooserContext::RevokeEphemeralDevicePermission(
    const url::Origin& origin,
    const device::mojom::HidDeviceInfo& device) {
  auto it = ephemeral_devices_.find(origin);
  if (it != ephemeral_devices_.end()) {
    std::set<std::string>& devices = it->second;
    for (auto guid = devices.begin(); guid != devices.end();) {
      DCHECK(base::Contains(devices_, *guid));

      if (devices_[*guid]->physical_device_id != device.physical_device_id) {
        ++guid;
        continue;
      }

      guid = devices.erase(guid);
      if (devices.empty())
        ephemeral_devices_.erase(it);
      NotifyPermissionRevoked(origin);
    }
  }
}

bool HidChooserContext::HasDevicePermission(
    const url::Origin& origin,
    const device::mojom::HidDeviceInfo& device) {
  if (device::HidBlocklist::IsDeviceExcluded(device))
    return false;

  if (!CanRequestObjectPermission(origin))
    return false;

  auto it = ephemeral_devices_.find(origin);
  if (it != ephemeral_devices_.end() &&
      base::Contains(it->second, device.guid)) {
    return true;
  }

  std::vector<std::unique_ptr<Object>> object_list = GetGrantedObjects(origin);
  for (const auto& object : object_list) {
    const base::Value& device_value = object->value;
    DCHECK(IsValidObject(device_value));

    if (device.vendor_id != *device_value.FindIntKey(kHidVendorIdKey) ||
        device.product_id != *device_value.FindIntKey(kHidProductIdKey)) {
      continue;
    }

    const auto* serial_number = device_value.FindStringKey(kHidSerialNumberKey);
    if (serial_number && device.serial_number == *serial_number)
      return true;
  }
  return false;
}

bool HidChooserContext::IsFidoAllowedForOrigin(const url::Origin& origin) {
#if BUILDFLAG(ENABLE_EXTENSIONS)
  static constexpr auto kPrivilegedExtensionIds =
      base::MakeFixedFlatSet<base::StringPiece>({
          "ckcendljdlmgnhghiaomidhiiclmapok",  // gnubbyd-v3 dev
          "lfboplenmmjcmpbkeemecobbadnmpfhi",  // gnubbyd-v3 prod
      });

  if (origin.scheme() == extensions::kExtensionScheme &&
      base::Contains(kPrivilegedExtensionIds, origin.host())) {
    return true;
  }
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

  return false;
}

void HidChooserContext::AddDeviceObserver(DeviceObserver* observer) {
  EnsureHidManagerConnection();
  device_observer_list_.AddObserver(observer);
}

void HidChooserContext::RemoveDeviceObserver(DeviceObserver* observer) {
  device_observer_list_.RemoveObserver(observer);
}

void HidChooserContext::GetDevices(
    device::mojom::HidManager::GetDevicesCallback callback) {
  if (!is_initialized_) {
    EnsureHidManagerConnection();
    pending_get_devices_requests_.push(std::move(callback));
    return;
  }

  std::vector<device::mojom::HidDeviceInfoPtr> device_list;
  device_list.reserve(devices_.size());
  for (const auto& pair : devices_)
    device_list.push_back(pair.second->Clone());
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(device_list)));
}

const device::mojom::HidDeviceInfo* HidChooserContext::GetDeviceInfo(
    const std::string& guid) {
  DCHECK(is_initialized_);
  auto it = devices_.find(guid);
  return it == devices_.end() ? nullptr : it->second.get();
}

device::mojom::HidManager* HidChooserContext::GetHidManager() {
  EnsureHidManagerConnection();
  return hid_manager_.get();
}

void HidChooserContext::SetHidManagerForTesting(
    mojo::PendingRemote<device::mojom::HidManager> manager,
    device::mojom::HidManager::GetDevicesCallback callback) {
  hid_manager_.Bind(std::move(manager));
  hid_manager_.set_disconnect_handler(base::BindOnce(
      &HidChooserContext::OnHidManagerConnectionError, base::Unretained(this)));

  hid_manager_->GetDevicesAndSetClient(
      client_receiver_.BindNewEndpointAndPassRemote(),
      base::BindOnce(&HidChooserContext::OnHidManagerInitializedForTesting,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void HidChooserContext::OnHidManagerInitializedForTesting(
    device::mojom::HidManager::GetDevicesCallback callback,
    std::vector<device::mojom::HidDeviceInfoPtr> devices) {
  DCHECK(devices.empty());
  DCHECK(pending_get_devices_requests_.empty());
  is_initialized_ = true;
  std::move(callback).Run({});
}

base::WeakPtr<HidChooserContext> HidChooserContext::AsWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void HidChooserContext::DeviceAdded(device::mojom::HidDeviceInfoPtr device) {
  DCHECK(device);

  // Update the device list.
  if (!base::Contains(devices_, device->guid))
    devices_.insert({device->guid, device->Clone()});

  // Notify all observers.
  for (auto& observer : device_observer_list_)
    observer.OnDeviceAdded(*device);
}

void HidChooserContext::DeviceRemoved(device::mojom::HidDeviceInfoPtr device) {
  DCHECK(device);
  DCHECK(base::Contains(devices_, device->guid));

  // Update the device list.
  devices_.erase(device->guid);

  // Notify all device observers.
  for (auto& observer : device_observer_list_)
    observer.OnDeviceRemoved(*device);

  // Next we'll notify observers for revoked permissions. If the device does not
  // support persistent permissions then device permissions are revoked on
  // disconnect.
  if (CanStorePersistentEntry(*device))
    return;

  std::vector<url::Origin> revoked_origins;
  for (auto& map_entry : ephemeral_devices_) {
    if (map_entry.second.erase(device->guid) > 0)
      revoked_origins.push_back(map_entry.first);
  }
  if (revoked_origins.empty())
    return;

  for (auto& observer : permission_observer_list_) {
    observer.OnObjectPermissionChanged(guard_content_settings_type_,
                                       data_content_settings_type_);
    for (auto& origin : revoked_origins) {
      observer.OnPermissionRevoked(origin);
    }
  }
}

void HidChooserContext::DeviceChanged(device::mojom::HidDeviceInfoPtr device) {
  DCHECK(device);
  DCHECK(base::Contains(devices_, device->guid));

  // Update the device list.
  devices_[device->guid] = device->Clone();

  // Notify all observers.
  for (auto& observer : device_observer_list_)
    observer.OnDeviceChanged(*device);
}

void HidChooserContext::EnsureHidManagerConnection() {
  if (hid_manager_)
    return;

  mojo::PendingRemote<device::mojom::HidManager> manager;
  content::GetDeviceService().BindHidManager(
      manager.InitWithNewPipeAndPassReceiver());
  SetUpHidManagerConnection(std::move(manager));
}

void HidChooserContext::SetUpHidManagerConnection(
    mojo::PendingRemote<device::mojom::HidManager> manager) {
  hid_manager_.Bind(std::move(manager));
  hid_manager_.set_disconnect_handler(base::BindOnce(
      &HidChooserContext::OnHidManagerConnectionError, base::Unretained(this)));

  hid_manager_->GetDevicesAndSetClient(
      client_receiver_.BindNewEndpointAndPassRemote(),
      base::BindOnce(&HidChooserContext::InitDeviceList,
                     weak_factory_.GetWeakPtr()));
}

void HidChooserContext::InitDeviceList(
    std::vector<device::mojom::HidDeviceInfoPtr> devices) {
  for (auto& device : devices)
    devices_.insert({device->guid, std::move(device)});

  is_initialized_ = true;

  while (!pending_get_devices_requests_.empty()) {
    std::vector<device::mojom::HidDeviceInfoPtr> device_list;
    device_list.reserve(devices.size());
    for (const auto& entry : devices_)
      device_list.push_back(entry.second->Clone());
    std::move(pending_get_devices_requests_.front())
        .Run(std::move(device_list));
    pending_get_devices_requests_.pop();
  }
}

void HidChooserContext::OnHidManagerConnectionError() {
  hid_manager_.reset();
  client_receiver_.reset();
  devices_.clear();

  std::vector<url::Origin> revoked_origins;
  revoked_origins.reserve(ephemeral_devices_.size());
  for (const auto& map_entry : ephemeral_devices_)
    revoked_origins.push_back(map_entry.first);
  ephemeral_devices_.clear();

  // Notify all device observers.
  for (auto& observer : device_observer_list_)
    observer.OnHidManagerConnectionError();

  // Notify permission observers that all ephemeral permissions have been
  // revoked.
  for (auto& observer : permission_observer_list_) {
    observer.OnObjectPermissionChanged(guard_content_settings_type_,
                                       data_content_settings_type_);
    for (const auto& origin : revoked_origins)
      observer.OnPermissionRevoked(origin);
  }
}
