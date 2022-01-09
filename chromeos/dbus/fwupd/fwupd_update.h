// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_FWUPD_FWUPD_UPDATE_H_
#define CHROMEOS_DBUS_FWUPD_FWUPD_UPDATE_H_

#include <string>

#include "base/component_export.h"

namespace chromeos {

// Structure to hold update details received from fwupd.
struct COMPONENT_EXPORT(CHROMEOS_DBUS_FWUPD) FwupdUpdate {
  FwupdUpdate();
  FwupdUpdate(const std::string& version,
              const std::string& description,
              int priority);
  FwupdUpdate(FwupdUpdate&& other);
  FwupdUpdate& operator=(FwupdUpdate&& other);
  ~FwupdUpdate();

  std::string version;
  std::string description;
  int priority;
};

using FwupdUpdateList = std::vector<FwupdUpdate>;

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_FWUPD_FWUPD_UPDATE_H_
