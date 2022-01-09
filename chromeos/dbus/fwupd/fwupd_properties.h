// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_FWUPD_FWUPD_PROPERTIES_H_
#define CHROMEOS_DBUS_FWUPD_FWUPD_PROPERTIES_H_

#include "base/component_export.h"
#include "dbus/object_proxy.h"
#include "dbus/property.h"

// Wrapper class of dbus::PropertySet that holds Dbus property fields
// pertaining to an FWUPD Dbus object. Properties of this class gets updated
// whenever dbus::PropertiesChanged() is called for the Fwupd interface.
namespace chromeos {

class COMPONENT_EXPORT(CHROMEOS_DBUS_FWUPD) FwupdProperties
    : public dbus::PropertySet {
 public:
  FwupdProperties(dbus::ObjectProxy* object_proxy,
                  const PropertyChangedCallback& callback);
  ~FwupdProperties() override;

  dbus::Property<uint32_t> percentage;
  dbus::Property<uint32_t> status;
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_FWUPD_FWUPD_PROPERTIES_H_