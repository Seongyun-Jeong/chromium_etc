// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_DEVICE_USB_WEBUSB_DESCRIPTORS_H_
#define SERVICES_DEVICE_USB_WEBUSB_DESCRIPTORS_H_

#include <stdint.h>

#include <vector>

#include "base/callback_forward.h"
#include "base/memory/ref_counted.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

class GURL;

namespace device {

class UsbDeviceHandle;

struct WebUsbPlatformCapabilityDescriptor {
  WebUsbPlatformCapabilityDescriptor();
  ~WebUsbPlatformCapabilityDescriptor();

  bool ParseFromBosDescriptor(const std::vector<uint8_t>& bytes);

  uint16_t version;
  uint8_t vendor_code;
  uint8_t landing_page_id;
};

bool ParseWebUsbUrlDescriptor(const std::vector<uint8_t>& bytes, GURL* output);

void ReadWebUsbLandingPage(
    uint8_t vendor_code,
    uint8_t landing_page_id,
    scoped_refptr<UsbDeviceHandle> device_handle,
    base::OnceCallback<void(const GURL& landing_page)> callback);

void ReadWebUsbCapabilityDescriptor(
    scoped_refptr<UsbDeviceHandle> device_handle,
    base::OnceCallback<void(
        const absl::optional<WebUsbPlatformCapabilityDescriptor>& descriptor)>
        callback);

void ReadWebUsbDescriptors(
    scoped_refptr<UsbDeviceHandle> device_handle,
    base::OnceCallback<void(const GURL& landing_page)> callback);

}  // namespace device

#endif  // SERVICES_DEVICE_USB_WEBUSB_DESCRIPTORS_H_
