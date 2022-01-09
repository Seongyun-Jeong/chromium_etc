// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_BLUETOOTH_CONFIG_PUBLIC_CPP_DEVICE_IMAGE_INFO_H_
#define CHROMEOS_SERVICES_BLUETOOTH_CONFIG_PUBLIC_CPP_DEVICE_IMAGE_INFO_H_

#include <string>

#include "base/values.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace ash {
namespace quick_pair {

class DeviceImageStore;

}  // namespace quick_pair
}  // namespace ash

namespace chromeos {
namespace bluetooth_config {

// Stores images as base64 encoded data URLs that can be displayed in UX.
// Provides convenience methods to convert to and from a dictionary so that
// it can be stored on disk.
class DeviceImageInfo {
 public:
  // Returns null if the provided value does not have the required dictionary
  // properties. Should be provided a dictionary created via
  // ToDictionaryValue().
  static absl::optional<DeviceImageInfo> FromDictionaryValue(
      const base::Value& value);

  DeviceImageInfo(const std::string& default_image,
                  const std::string& left_bud_image,
                  const std::string& right_bud_image,
                  const std::string& case_image);
  DeviceImageInfo();
  DeviceImageInfo(const DeviceImageInfo&);
  DeviceImageInfo& operator=(const DeviceImageInfo&);
  ~DeviceImageInfo();

  base::Value ToDictionaryValue() const;

  const std::string& default_image() const { return default_image_; }
  const std::string& left_bud_image() const { return left_bud_image_; }
  const std::string& right_bud_image() const { return right_bud_image_; }
  const std::string& case_image() const { return case_image_; }

 private:
  friend class ash::quick_pair::DeviceImageStore;
  std::string default_image_;
  std::string left_bud_image_;
  std::string right_bud_image_;
  std::string case_image_;
};

}  // namespace bluetooth_config
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_BLUETOOTH_CONFIG_PUBLIC_CPP_DEVICE_IMAGE_INFO_H_