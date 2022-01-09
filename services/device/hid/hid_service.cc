// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/device/hid/hid_service.h"

#include <sstream>

#include "base/at_exit.h"
#include "base/bind.h"
#include "base/containers/contains.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "build/build_config.h"
#include "components/device_event_log/device_event_log.h"

#if (defined(OS_LINUX) || defined(OS_CHROMEOS)) && defined(USE_UDEV)
#include "services/device/hid/hid_service_linux.h"
#elif defined(OS_MAC)
#include "services/device/hid/hid_service_mac.h"
#elif defined(OS_WIN)
#include "services/device/hid/hid_service_win.h"
#endif

namespace device {

namespace {

// Formats the platform device IDs in |platform_device_id_map| into a
// comma-separated list for logging. The report IDs are not logged.
std::string PlatformDeviceIdsToString(
    const HidDeviceInfo::PlatformDeviceIdMap& platform_device_id_map) {
  std::ostringstream buf("'");
  bool first = true;
  for (const auto& entry : platform_device_id_map) {
    if (!first)
      buf << "', '";
    first = false;
    buf << entry.platform_device_id;
  }
  buf << "'";
  return buf.str();
}

}  // namespace

void HidService::Observer::OnDeviceAdded(mojom::HidDeviceInfoPtr device_info) {}

void HidService::Observer::OnDeviceRemoved(
    mojom::HidDeviceInfoPtr device_info) {}

void HidService::Observer::OnDeviceChanged(
    mojom::HidDeviceInfoPtr device_info) {}

// static
constexpr base::TaskTraits HidService::kBlockingTaskTraits;

// static
std::unique_ptr<HidService> HidService::Create() {
#if (defined(OS_LINUX) || defined(OS_CHROMEOS)) && defined(USE_UDEV)
  return base::WrapUnique(new HidServiceLinux());
#elif defined(OS_MAC)
  return base::WrapUnique(new HidServiceMac());
#elif defined(OS_WIN)
  return base::WrapUnique(new HidServiceWin());
#else
  return nullptr;
#endif
}

void HidService::GetDevices(GetDevicesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  bool was_empty = pending_enumerations_.empty();
  pending_enumerations_.push_back(std::move(callback));
  if (enumeration_ready_ && was_empty) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&HidService::RunPendingEnumerations, GetWeakPtr()));
  }
}

void HidService::AddObserver(HidService::Observer* observer) {
  observer_list_.AddObserver(observer);
}

void HidService::RemoveObserver(HidService::Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

HidService::HidService() = default;

HidService::~HidService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void HidService::AddDevice(scoped_refptr<HidDeviceInfo> device_info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // A HidDeviceInfo object may represent multiple platform devices. For
  // instance, on Windows each HID interface is split into separate platform
  // devices for each top-level collection. When adding devices to HidService,
  // callers should add each platform device as a separate HidDeviceInfo and
  // allow HidService to merge them together.
  DCHECK_EQ(device_info->platform_device_id_map().size(), 1u);
  if (FindDeviceGuidInDeviceMap(
          device_info->platform_device_id_map().front().platform_device_id)) {
    return;
  }

  // If |device_info| has an interface ID then it represents a single top-level
  // collection within a HID interface that may contain other top-level
  // collections. Check if a sibling device has already been added and, if so,
  // merge |device_info| into the sibling device.
  if (device_info->interface_id()) {
    auto sibling_device = FindSiblingDevice(*device_info);
    if (sibling_device) {
      // Merge |device_info| into |sibling_device|.
      sibling_device->AppendDeviceInfo(std::move(device_info));
      if (enumeration_ready_) {
        for (auto& observer : observer_list_)
          observer.OnDeviceChanged(sibling_device->device()->Clone());
      }
      return;
    }
  }

  devices_[device_info->device_guid()] = device_info;

  HID_LOG(USER) << "HID device " << (enumeration_ready_ ? "added" : "detected")
                << ": vendorId=" << device_info->vendor_id()
                << ", productId=" << device_info->product_id() << ", name='"
                << device_info->product_name() << "', serial='"
                << device_info->serial_number() << "', deviceIds=["
                << PlatformDeviceIdsToString(
                       device_info->platform_device_id_map())
                << "]";

  if (enumeration_ready_) {
    for (auto& observer : observer_list_)
      observer.OnDeviceAdded(device_info->device()->Clone());
  }
}

void HidService::RemoveDevice(const HidPlatformDeviceId& platform_device_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto found_guid = FindDeviceGuidInDeviceMap(platform_device_id);
  if (found_guid) {
    HID_LOG(USER) << "HID device removed: deviceId='" << platform_device_id
                  << "'";
    DCHECK(base::Contains(devices_, *found_guid));

    scoped_refptr<HidDeviceInfo> device_info = devices_[*found_guid];
    if (enumeration_ready_) {
      for (auto& observer : observer_list_)
        observer.OnDeviceRemoved(device_info->device()->Clone());
    }
    devices_.erase(*found_guid);
  }
}

void HidService::RunPendingEnumerations() {
  DCHECK(enumeration_ready_);
  DCHECK(!pending_enumerations_.empty());

  std::vector<GetDevicesCallback> callbacks;
  callbacks.swap(pending_enumerations_);

  // Clone and pass mojom::HidDeviceInfoPtr vector for each clients.
  for (auto& callback : callbacks) {
    std::vector<mojom::HidDeviceInfoPtr> devices;
    for (const auto& map_entry : devices_)
      devices.push_back(map_entry.second->device()->Clone());
    std::move(callback).Run(std::move(devices));
  }
}

void HidService::FirstEnumerationComplete() {
  enumeration_ready_ = true;
  if (!pending_enumerations_.empty()) {
    RunPendingEnumerations();
  }
}

absl::optional<std::string> HidService::FindDeviceGuidInDeviceMap(
    const HidPlatformDeviceId& platform_device_id) {
  for (const auto& device_entry : devices_) {
    const auto& platform_device_map =
        device_entry.second->platform_device_id_map();
    for (const auto& platform_device_entry : platform_device_map) {
      if (platform_device_entry.platform_device_id == platform_device_id)
        return device_entry.first;
    }
  }
  return absl::nullopt;
}

scoped_refptr<HidDeviceInfo> HidService::FindSiblingDevice(
    const HidDeviceInfo& device_info) const {
  if (!device_info.interface_id())
    return nullptr;

  for (const auto& device_entry : devices_) {
    if (device_entry.second->interface_id() == device_info.interface_id())
      return device_entry.second;
  }
  return nullptr;
}

}  // namespace device
