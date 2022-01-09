// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_WEBAUTH_CLIENT_DATA_JSON_H_
#define CONTENT_BROWSER_WEBAUTH_CLIENT_DATA_JSON_H_

#include <stdint.h>

#include <string>

#include "base/containers/span.h"
#include "content/common/content_export.h"
#include "third_party/blink/public/mojom/webauthn/authenticator.mojom.h"

namespace content {

// ClientDataRequestType enumerates different request types that
// CollectedClientData can be built for. See
// |BuildClientDataJson|.
// GENERATED_JAVA_ENUM_PACKAGE: org.chromium.content_public.browser
// GENERATED_JAVA_CLASS_NAME_OVERRIDE: ClientDataRequestType
enum class ClientDataRequestType {
  kU2fRegister,
  kU2fSign,
  kWebAuthnCreate,
  kWebAuthnGet,
  kPaymentGet,
};

// Builds the CollectedClientData[1] dictionary with the given values,
// serializes it to JSON, and returns the resulting string.
// [1] https://w3c.github.io/webauthn/#dictdef-collectedclientdata
CONTENT_EXPORT std::string BuildClientDataJson(
    ClientDataRequestType type,
    const std::string& origin,
    base::span<const uint8_t> challenge,
    bool is_cross_origin,
    blink::mojom::PaymentOptionsPtr payment_options = nullptr,
    const std::string& payment_rp = "",
    const std::string& payment_top_origin = "");

}  // namespace content

#endif  // CONTENT_BROWSER_WEBAUTH_CLIENT_DATA_JSON_H_
