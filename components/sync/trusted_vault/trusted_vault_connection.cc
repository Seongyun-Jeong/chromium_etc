// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/trusted_vault/trusted_vault_connection.h"

namespace syncer {

TrustedVaultKeyAndVersion::TrustedVaultKeyAndVersion(
    const std::vector<uint8_t>& key,
    int version)
    : key(key), version(version) {}

TrustedVaultKeyAndVersion::TrustedVaultKeyAndVersion(
    const TrustedVaultKeyAndVersion& other) = default;

TrustedVaultKeyAndVersion& TrustedVaultKeyAndVersion::operator=(
    const TrustedVaultKeyAndVersion& other) = default;

TrustedVaultKeyAndVersion::~TrustedVaultKeyAndVersion() = default;

}  // namespace syncer
