// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SERVICES_APP_SERVICE_PUBLIC_CPP_CAPABILITY_ACCESS_UPDATE_H_
#define COMPONENTS_SERVICES_APP_SERVICE_PUBLIC_CPP_CAPABILITY_ACCESS_UPDATE_H_

#include <string>

#include "base/component_export.h"
#include "base/memory/raw_ptr.h"
#include "components/account_id/account_id.h"
#include "components/services/app_service/public/mojom/types.mojom.h"

namespace apps {

// Wraps two apps::mojom::CapabilityAccessPtr's, a prior state and a delta on
// top of that state. The state is conceptually the "sum" of all of the previous
// deltas, with "addition" or "merging" simply being that the most recent
// version of each field "wins".
//
// The state may be nullptr, meaning that there are no previous deltas.
// Alternatively, the delta may be nullptr, meaning that there is no change in
// state. At least one of state and delta must be non-nullptr.
//
// The combination of the two (state and delta) can answer questions such as:
//  - Whether the app is accessing the camera? If the delta knows, that's the
//  answer. Otherwise, ask the state.
//  - Whether the app is accessing the microphone? If the delta knows, that's
//  the answer. Otherwise, ask the state.
//
// An CapabilityAccessUpdate is read-only once constructed. All of its fields
// and methods are const. The constructor caller must guarantee that the
// CapabilityAccessPtr references remain valid for the lifetime of the
// CapabilityAccessUpdate.
//
// See components/services/app_service/README.md for more details.
class COMPONENT_EXPORT(APP_UPDATE) CapabilityAccessUpdate {
 public:
  // Modifies |state| by copying over all of |delta|'s known fields: those
  // fields whose values aren't "unknown". The |state| may not be nullptr.
  static void Merge(apps::mojom::CapabilityAccess* state,
                    const apps::mojom::CapabilityAccess* delta);

  // At most one of |state| or |delta| may be nullptr.
  CapabilityAccessUpdate(const apps::mojom::CapabilityAccess* state,
                         const apps::mojom::CapabilityAccess* delta,
                         const AccountId& account_id);

  CapabilityAccessUpdate(const CapabilityAccessUpdate&) = delete;
  CapabilityAccessUpdate& operator=(const CapabilityAccessUpdate&) = delete;

  // Returns whether this is the first update for the given AppId.
  // Equivalently, there are no previous deltas for the AppId.
  bool StateIsNull() const;

  const std::string& AppId() const;

  apps::mojom::OptionalBool Camera() const;
  bool CameraChanged() const;

  apps::mojom::OptionalBool Microphone() const;
  bool MicrophoneChanged() const;

  const ::AccountId& AccountId() const;

 private:
  raw_ptr<const apps::mojom::CapabilityAccess> state_;
  raw_ptr<const apps::mojom::CapabilityAccess> delta_;

  const ::AccountId& account_id_;
};

}  // namespace apps

#endif  // COMPONENTS_SERVICES_APP_SERVICE_PUBLIC_CPP_CAPABILITY_ACCESS_UPDATE_H_
