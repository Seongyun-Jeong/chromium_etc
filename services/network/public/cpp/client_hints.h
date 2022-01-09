// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_PUBLIC_CPP_CLIENT_HINTS_H_
#define SERVICES_NETWORK_PUBLIC_CPP_CLIENT_HINTS_H_

#include <stddef.h>
#include <string>

#include "base/component_export.h"
#include "base/containers/flat_map.h"
#include "base/time/time.h"
#include "services/network/public/mojom/web_client_hints_types.mojom-shared.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace url {
class Origin;
}  // namespace url

namespace network {

using ClientHintToNameMap =
    base::flat_map<network::mojom::WebClientHintsType, std::string>;

// Mapping from WebClientHintsType to the hint's name in Accept-CH header.
// The ordering matches the ordering of enums in
// services/network/public/mojom/web_client_hints_types.mojom
COMPONENT_EXPORT(NETWORK_CPP)
const ClientHintToNameMap& GetClientHintToNameMap();

// Tries to parse an Accept-CH header. Returns absl::nullopt if parsing
// failed and the header should be ignored; otherwise returns a (possibly
// empty) list of hints to accept.
absl::optional<std::vector<network::mojom::WebClientHintsType>>
    COMPONENT_EXPORT(NETWORK_CPP)
        ParseClientHintsHeader(const std::string& header);

struct COMPONENT_EXPORT(NETWORK_CPP) ClientHintToDelegatedThirdPartiesHeader {
  ClientHintToDelegatedThirdPartiesHeader();
  ~ClientHintToDelegatedThirdPartiesHeader();
  ClientHintToDelegatedThirdPartiesHeader(
      const ClientHintToDelegatedThirdPartiesHeader&);

  base::flat_map<network::mojom::WebClientHintsType, std::vector<url::Origin>>
      map;
  bool had_invalid_origins{false};
};

// Tries to parse an Accept-CH header w/ third-party delegation ability (i.e. a
// named meta tag). Returns absl::nullopt if parsing failed and the header
// should be ignored; otherwise returns a (possibly empty) map of hints to
// delegated third-parties.
absl::optional<const ClientHintToDelegatedThirdPartiesHeader> COMPONENT_EXPORT(
    NETWORK_CPP)
    ParseClientHintToDelegatedThirdPartiesHeader(const std::string& header);

}  // namespace network

#endif  // SERVICES_NETWORK_PUBLIC_CPP_CLIENT_HINTS_H_
