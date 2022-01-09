// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/host_resolver_mojom_traits.h"

#include "mojo/public/cpp/base/time_mojom_traits.h"
#include "net/dns/public/dns_over_https_server_config.h"
#include "net/dns/public/host_resolver_source.h"
#include "net/dns/public/mdns_listener_update_type.h"
#include "net/dns/public/resolve_error_info.h"
#include "net/dns/public/secure_dns_mode.h"
#include "net/dns/public/secure_dns_policy.h"
#include "services/network/public/cpp/ip_address_mojom_traits.h"
#include "services/network/public/cpp/ip_endpoint_mojom_traits.h"
#include "services/network/public/mojom/host_resolver.mojom-shared.h"
#include "services/network/public/mojom/host_resolver.mojom.h"

namespace mojo {

using network::mojom::DnsConfigOverrides;
using network::mojom::DnsConfigOverridesDataView;
using network::mojom::DnsOverHttpsServer;
using network::mojom::DnsOverHttpsServerDataView;
using network::mojom::DnsOverHttpsServerPtr;
using network::mojom::DnsQueryType;
using network::mojom::MdnsListenClient;
using network::mojom::OptionalSecureDnsMode;
using network::mojom::ResolveHostParameters;

namespace {

DnsConfigOverrides::Tristate ToTristate(absl::optional<bool> optional) {
  if (!optional)
    return DnsConfigOverrides::Tristate::NO_OVERRIDE;
  if (optional.value())
    return DnsConfigOverrides::Tristate::TRISTATE_TRUE;
  return DnsConfigOverrides::Tristate::TRISTATE_FALSE;
}

absl::optional<bool> FromTristate(DnsConfigOverrides::Tristate tristate) {
  switch (tristate) {
    case DnsConfigOverrides::Tristate::NO_OVERRIDE:
      return absl::nullopt;
    case DnsConfigOverrides::Tristate::TRISTATE_TRUE:
      return true;
    case DnsConfigOverrides::Tristate::TRISTATE_FALSE:
      return false;
  }
}

OptionalSecureDnsMode ToOptionalSecureDnsMode(
    absl::optional<net::SecureDnsMode> optional) {
  if (!optional)
    return OptionalSecureDnsMode::NO_OVERRIDE;
  switch (optional.value()) {
    case net::SecureDnsMode::kOff:
      return OptionalSecureDnsMode::OFF;
    case net::SecureDnsMode::kAutomatic:
      return OptionalSecureDnsMode::AUTOMATIC;
    case net::SecureDnsMode::kSecure:
      return OptionalSecureDnsMode::SECURE;
  }
}

}  // namespace

absl::optional<net::SecureDnsMode> FromOptionalSecureDnsMode(
    OptionalSecureDnsMode mode) {
  switch (mode) {
    case OptionalSecureDnsMode::NO_OVERRIDE:
      return absl::nullopt;
    case OptionalSecureDnsMode::OFF:
      return net::SecureDnsMode::kOff;
    case OptionalSecureDnsMode::AUTOMATIC:
      return net::SecureDnsMode::kAutomatic;
    case OptionalSecureDnsMode::SECURE:
      return net::SecureDnsMode::kSecure;
  }
}

// static
bool StructTraits<network::mojom::DnsOverHttpsServerDataView,
                  net::DnsOverHttpsServerConfig>::
    Read(network::mojom::DnsOverHttpsServerDataView data,
         net::DnsOverHttpsServerConfig* out_config) {
  std::string server_template;
  if (!data.ReadServerTemplate(&server_template))
    return false;
  auto parsed =
      net::DnsOverHttpsServerConfig::FromString(std::move(server_template));
  if (!parsed)
    return false;
  *out_config = std::move(*parsed);
  return true;
}

// static
DnsConfigOverrides::Tristate
StructTraits<DnsConfigOverridesDataView, net::DnsConfigOverrides>::
    append_to_multi_label_name(const net::DnsConfigOverrides& overrides) {
  return ToTristate(overrides.append_to_multi_label_name);
}

// static
DnsConfigOverrides::Tristate
StructTraits<DnsConfigOverridesDataView, net::DnsConfigOverrides>::rotate(
    const net::DnsConfigOverrides& overrides) {
  return ToTristate(overrides.rotate);
}

// static
DnsConfigOverrides::Tristate
StructTraits<DnsConfigOverridesDataView, net::DnsConfigOverrides>::
    use_local_ipv6(const net::DnsConfigOverrides& overrides) {
  return ToTristate(overrides.use_local_ipv6);
}

// static
OptionalSecureDnsMode
StructTraits<DnsConfigOverridesDataView, net::DnsConfigOverrides>::
    secure_dns_mode(const net::DnsConfigOverrides& overrides) {
  return ToOptionalSecureDnsMode(overrides.secure_dns_mode);
}

// static
DnsConfigOverrides::Tristate
StructTraits<DnsConfigOverridesDataView, net::DnsConfigOverrides>::
    allow_dns_over_https_upgrade(const net::DnsConfigOverrides& overrides) {
  return ToTristate(overrides.allow_dns_over_https_upgrade);
}

// static
bool StructTraits<DnsConfigOverridesDataView, net::DnsConfigOverrides>::Read(
    DnsConfigOverridesDataView data,
    net::DnsConfigOverrides* out) {
  if (!data.ReadNameservers(&out->nameservers))
    return false;
  if (!data.ReadSearch(&out->search))
    return false;

  out->append_to_multi_label_name =
      FromTristate(data.append_to_multi_label_name());

  if (data.ndots() < -1)
    return false;
  if (data.ndots() >= 0)
    out->ndots = data.ndots();
  // if == -1, leave nullopt.

  if (!data.ReadFallbackPeriod(&out->fallback_period))
    return false;

  if (data.attempts() < -1)
    return false;
  if (data.attempts() >= 0)
    out->attempts = data.attempts();
  // if == -1, leave nullopt.

  out->rotate = FromTristate(data.rotate());
  out->use_local_ipv6 = FromTristate(data.use_local_ipv6());

  if (!data.ReadDnsOverHttpsServers(&out->dns_over_https_servers))
    return false;

  out->secure_dns_mode = FromOptionalSecureDnsMode(data.secure_dns_mode());

  out->allow_dns_over_https_upgrade =
      FromTristate(data.allow_dns_over_https_upgrade());
  if (!data.ReadDisabledUpgradeProviders(&out->disabled_upgrade_providers))
    return false;

  out->clear_hosts = data.clear_hosts();

  return true;
}

// static
DnsQueryType EnumTraits<DnsQueryType, net::DnsQueryType>::ToMojom(
    net::DnsQueryType input) {
  switch (input) {
    case net::DnsQueryType::UNSPECIFIED:
      return DnsQueryType::UNSPECIFIED;
    case net::DnsQueryType::A:
      return DnsQueryType::A;
    case net::DnsQueryType::AAAA:
      return DnsQueryType::AAAA;
    case net::DnsQueryType::TXT:
      return DnsQueryType::TXT;
    case net::DnsQueryType::PTR:
      return DnsQueryType::PTR;
    case net::DnsQueryType::SRV:
      return DnsQueryType::SRV;
    case net::DnsQueryType::HTTPS:
      return DnsQueryType::HTTPS;
    case net::DnsQueryType::INTEGRITY:
    case net::DnsQueryType::HTTPS_EXPERIMENTAL:
      NOTIMPLEMENTED();
      return DnsQueryType::UNSPECIFIED;
  }
}

// static
bool EnumTraits<DnsQueryType, net::DnsQueryType>::FromMojom(
    DnsQueryType input,
    net::DnsQueryType* output) {
  switch (input) {
    case DnsQueryType::UNSPECIFIED:
      *output = net::DnsQueryType::UNSPECIFIED;
      return true;
    case DnsQueryType::A:
      *output = net::DnsQueryType::A;
      return true;
    case DnsQueryType::AAAA:
      *output = net::DnsQueryType::AAAA;
      return true;
    case DnsQueryType::TXT:
      *output = net::DnsQueryType::TXT;
      return true;
    case DnsQueryType::PTR:
      *output = net::DnsQueryType::PTR;
      return true;
    case DnsQueryType::SRV:
      *output = net::DnsQueryType::SRV;
      return true;
    case DnsQueryType::HTTPS:
      *output = net::DnsQueryType::HTTPS;
      return true;
  }
}

// static
ResolveHostParameters::Source
EnumTraits<ResolveHostParameters::Source, net::HostResolverSource>::ToMojom(
    net::HostResolverSource input) {
  switch (input) {
    case net::HostResolverSource::ANY:
      return ResolveHostParameters::Source::ANY;
    case net::HostResolverSource::SYSTEM:
      return ResolveHostParameters::Source::SYSTEM;
    case net::HostResolverSource::DNS:
      return ResolveHostParameters::Source::DNS;
    case net::HostResolverSource::MULTICAST_DNS:
      return ResolveHostParameters::Source::MULTICAST_DNS;
    case net::HostResolverSource::LOCAL_ONLY:
      return ResolveHostParameters::Source::LOCAL_ONLY;
  }
}

// static
bool EnumTraits<ResolveHostParameters::Source, net::HostResolverSource>::
    FromMojom(ResolveHostParameters::Source input,
              net::HostResolverSource* output) {
  switch (input) {
    case ResolveHostParameters::Source::ANY:
      *output = net::HostResolverSource::ANY;
      return true;
    case ResolveHostParameters::Source::SYSTEM:
      *output = net::HostResolverSource::SYSTEM;
      return true;
    case ResolveHostParameters::Source::DNS:
      *output = net::HostResolverSource::DNS;
      return true;
    case ResolveHostParameters::Source::MULTICAST_DNS:
      *output = net::HostResolverSource::MULTICAST_DNS;
      return true;
    case ResolveHostParameters::Source::LOCAL_ONLY:
      *output = net::HostResolverSource::LOCAL_ONLY;
      return true;
  }
}

// static
MdnsListenClient::UpdateType
EnumTraits<MdnsListenClient::UpdateType, net::MdnsListenerUpdateType>::ToMojom(
    net::MdnsListenerUpdateType input) {
  switch (input) {
    case net::MdnsListenerUpdateType::kAdded:
      return MdnsListenClient::UpdateType::ADDED;
    case net::MdnsListenerUpdateType::kChanged:
      return MdnsListenClient::UpdateType::CHANGED;
    case net::MdnsListenerUpdateType::kRemoved:
      return MdnsListenClient::UpdateType::REMOVED;
  }
}

// static
bool EnumTraits<MdnsListenClient::UpdateType, net::MdnsListenerUpdateType>::
    FromMojom(MdnsListenClient::UpdateType input,
              net::MdnsListenerUpdateType* output) {
  switch (input) {
    case MdnsListenClient::UpdateType::ADDED:
      *output = net::MdnsListenerUpdateType::kAdded;
      return true;
    case MdnsListenClient::UpdateType::CHANGED:
      *output = net::MdnsListenerUpdateType::kChanged;
      return true;
    case MdnsListenClient::UpdateType::REMOVED:
      *output = net::MdnsListenerUpdateType::kRemoved;
      return true;
  }
}

// static
network::mojom::SecureDnsMode
EnumTraits<network::mojom::SecureDnsMode, net::SecureDnsMode>::ToMojom(
    net::SecureDnsMode secure_dns_mode) {
  switch (secure_dns_mode) {
    case net::SecureDnsMode::kOff:
      return network::mojom::SecureDnsMode::OFF;
    case net::SecureDnsMode::kAutomatic:
      return network::mojom::SecureDnsMode::AUTOMATIC;
    case net::SecureDnsMode::kSecure:
      return network::mojom::SecureDnsMode::SECURE;
  }
}

// static
bool EnumTraits<network::mojom::SecureDnsMode, net::SecureDnsMode>::FromMojom(
    network::mojom::SecureDnsMode in,
    net::SecureDnsMode* out) {
  switch (in) {
    case network::mojom::SecureDnsMode::OFF:
      *out = net::SecureDnsMode::kOff;
      return true;
    case network::mojom::SecureDnsMode::AUTOMATIC:
      *out = net::SecureDnsMode::kAutomatic;
      return true;
    case network::mojom::SecureDnsMode::SECURE:
      *out = net::SecureDnsMode::kSecure;
      return true;
  }
}

// static
network::mojom::SecureDnsPolicy
EnumTraits<network::mojom::SecureDnsPolicy, net::SecureDnsPolicy>::ToMojom(
    net::SecureDnsPolicy secure_dns_policy) {
  switch (secure_dns_policy) {
    case net::SecureDnsPolicy::kAllow:
      return network::mojom::SecureDnsPolicy::ALLOW;
    case net::SecureDnsPolicy::kDisable:
      return network::mojom::SecureDnsPolicy::DISABLE;
    case net::SecureDnsPolicy::kBootstrap:
      NOTREACHED();  // The bootstrap policy is only for use within the net
                     // component.
      return network::mojom::SecureDnsPolicy::DISABLE;
  }
}

// static
bool EnumTraits<network::mojom::SecureDnsPolicy, net::SecureDnsPolicy>::
    FromMojom(network::mojom::SecureDnsPolicy in, net::SecureDnsPolicy* out) {
  switch (in) {
    case network::mojom::SecureDnsPolicy::ALLOW:
      *out = net::SecureDnsPolicy::kAllow;
      return true;
    case network::mojom::SecureDnsPolicy::DISABLE:
      *out = net::SecureDnsPolicy::kDisable;
      return true;
  }
}

}  // namespace mojo
