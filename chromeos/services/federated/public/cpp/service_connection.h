// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_FEDERATED_PUBLIC_CPP_SERVICE_CONNECTION_H_
#define CHROMEOS_SERVICES_FEDERATED_PUBLIC_CPP_SERVICE_CONNECTION_H_

#include "chromeos/services/federated/public/mojom/federated_service.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"

namespace chromeos {
namespace federated {

// Encapsulates a connection to the Chrome OS Federated Service daemon via its
// Mojo interface. Usage:
//  mojo::Remote<FederatedService> federated_service;
//  chromeos::federated::ServiceConnection::GetInstance()->BindReceiver(
//        federated_service.BindNewPipeAndPassReceiver());
//  if (federated_service) {
//    chromeos::federated::mojom::ExamplePtr example = ...;
//    const std::string client_name = ...;
//    federated_service->ReportExample(client_name, std::move(example));
//  } else {
//    // error handler
//  }
//
// Sequencing: Must be used on a single sequence (may be created on another).
class ServiceConnection {
 public:
  static ServiceConnection* GetInstance();

  // Binds the receiver to the implementation in the Federated Service daemon.
  virtual void BindReceiver(
      mojo::PendingReceiver<mojom::FederatedService> receiver) = 0;

 protected:
  ServiceConnection() = default;
  virtual ~ServiceConnection() = default;
};

// Helper class that sets a global fake service_connection pointer and
// automatically clean up when it goes out of the scope.
// Used in unit_test only to inject fake to ServiceConnection::GetInstance().
class ScopedFakeServiceConnectionForTest {
 public:
  explicit ScopedFakeServiceConnectionForTest(
      ServiceConnection* fake_service_connection);
  ScopedFakeServiceConnectionForTest(
      const ScopedFakeServiceConnectionForTest&) = delete;
  ScopedFakeServiceConnectionForTest& operator=(
      const ScopedFakeServiceConnectionForTest&) = delete;

  ~ScopedFakeServiceConnectionForTest();
};

}  // namespace federated
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_FEDERATED_PUBLIC_CPP_SERVICE_CONNECTION_H_
