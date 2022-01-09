// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_CDM_FACTORY_DAEMON_CDM_FACTORY_DAEMON_CLIENT_H_
#define CHROMEOS_DBUS_CDM_FACTORY_DAEMON_CDM_FACTORY_DAEMON_CLIENT_H_

#include "base/callback.h"
#include "base/component_export.h"
#include "base/files/scoped_file.h"

namespace dbus {
class Bus;
}

namespace chromeos {

// CdmFactoryDaemonClient is used to communicate with the CdmFactoryDaemon
// service which provides a Content Decryption Module implementation for Chrome.
// The only purpose of the D-Bus service is to bootstrap a Mojo IPC connection.
class COMPONENT_EXPORT(CDM_FACTORY_DAEMON) CdmFactoryDaemonClient {
 public:
  // Creates and initializes the global instance. |bus| must not be null.
  static void Initialize(dbus::Bus* bus);

  // Creates and initializes a fake global instance if not already created.
  static void InitializeFake();

  // Destroys the global instance.
  static void Shutdown();

  // Returns the global instance which may be null if not initialized.
  static CdmFactoryDaemonClient* Get();

  CdmFactoryDaemonClient(const CdmFactoryDaemonClient&) = delete;
  CdmFactoryDaemonClient& operator=(const CdmFactoryDaemonClient&) = delete;

  // CdmFactoryDaemon D-Bus method calls. See org.chromium.CdmFactoryDaemon.xml
  // in Chromium OS code for the documentation of the methods and
  // request/response messages.
  virtual void BootstrapMojoConnection(
      base::ScopedFD fd,
      base::OnceCallback<void(bool success)> callback) = 0;

 protected:
  // Initialize/Shutdown should be used instead.
  CdmFactoryDaemonClient();
  virtual ~CdmFactoryDaemonClient();
};

}  // namespace chromeos

// TODO(https://crbug.com/1164001): remove when moved to ash.
namespace ash {
using ::chromeos::CdmFactoryDaemonClient;
}  // namespace ash

#endif  // CHROMEOS_DBUS_CDM_FACTORY_DAEMON_CDM_FACTORY_DAEMON_CLIENT_H_
