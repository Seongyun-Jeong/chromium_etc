// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/onc/network_onc_utils.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <utility>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chromeos/components/onc/onc_mapper.h"
#include "chromeos/components/onc/onc_signature.h"
#include "chromeos/components/onc/onc_utils.h"
#include "chromeos/components/onc/onc_validator.h"
#include "chromeos/network/managed_network_configuration_handler.h"
#include "chromeos/network/network_configuration_handler.h"
#include "chromeos/network/network_event_log.h"
#include "chromeos/network/network_profile.h"
#include "chromeos/network/network_profile_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/network_ui_data.h"
#include "chromeos/network/onc/network_onc_utils.h"
#include "chromeos/network/onc/onc_normalizer.h"
#include "chromeos/network/onc/onc_translator.h"
#include "chromeos/network/tether_constants.h"
#include "components/account_id/account_id.h"
#include "components/device_event_log/device_event_log.h"
#include "components/onc/onc_constants.h"
#include "components/onc/onc_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/url_formatter/url_fixer.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#include "crypto/encryptor.h"
#include "crypto/hmac.h"
#include "crypto/symmetric_key.h"
#include "net/base/host_port_pair.h"
#include "net/base/proxy_server.h"
#include "net/base/proxy_string_util.h"
#include "net/cert/pem.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util_nss.h"
#include "net/proxy_resolution/proxy_bypass_rules.h"
#include "net/proxy_resolution/proxy_config.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace chromeos {
namespace onc {

namespace {

// Scheme strings for supported |net::ProxyServer::SCHEME_*| enum values.
constexpr char kDirectScheme[] = "direct";
constexpr char kQuicScheme[] = "quic";
constexpr char kSocksScheme[] = "socks";
constexpr char kSocks4Scheme[] = "socks4";
constexpr char kSocks5Scheme[] = "socks5";

std::string GetString(const base::Value& dict, const char* key) {
  const base::Value* value = dict.FindKeyOfType(key, base::Value::Type::STRING);
  if (!value)
    return std::string();
  return value->GetString();
}

int GetInt(const base::Value& dict, const char* key, int default_value) {
  const base::Value* value =
      dict.FindKeyOfType(key, base::Value::Type::INTEGER);
  if (!value)
    return default_value;
  return value->GetInt();
}

net::ProxyServer ConvertOncProxyLocationToHostPort(
    net::ProxyServer::Scheme default_proxy_scheme,
    const base::Value& onc_proxy_location) {
  std::string host = GetString(onc_proxy_location, ::onc::proxy::kHost);
  // Parse |host| according to the format [<scheme>"://"]<server>[":"<port>].
  net::ProxyServer proxy_server =
      net::ProxyUriToProxyServer(host, default_proxy_scheme);
  int port = GetInt(onc_proxy_location, ::onc::proxy::kPort, 0);

  // Replace the port parsed from |host| by the provided |port|.
  return net::ProxyServer(
      proxy_server.scheme(),
      net::HostPortPair(proxy_server.host_port_pair().host(),
                        static_cast<uint16_t>(port)));
}

void AppendProxyServerForScheme(const base::Value& onc_manual,
                                const std::string& onc_scheme,
                                std::string* spec) {
  const base::Value* onc_proxy_location = onc_manual.FindKey(onc_scheme);
  if (!onc_proxy_location)
    return;

  net::ProxyServer::Scheme default_proxy_scheme = net::ProxyServer::SCHEME_HTTP;
  std::string url_scheme;
  if (onc_scheme == ::onc::proxy::kFtp) {
    url_scheme = url::kFtpScheme;
  } else if (onc_scheme == ::onc::proxy::kHttp) {
    url_scheme = url::kHttpScheme;
  } else if (onc_scheme == ::onc::proxy::kHttps) {
    url_scheme = url::kHttpsScheme;
  } else if (onc_scheme == ::onc::proxy::kSocks) {
    default_proxy_scheme = net::ProxyServer::SCHEME_SOCKS4;
    url_scheme = kSocksScheme;
  } else {
    NOTREACHED();
  }

  net::ProxyServer proxy_server = ConvertOncProxyLocationToHostPort(
      default_proxy_scheme, *onc_proxy_location);

  ProxyConfigDictionary::EncodeAndAppendProxyServer(url_scheme, proxy_server,
                                                    spec);
}

net::ProxyBypassRules ConvertOncExcludeDomainsToBypassRules(
    const base::Value& onc_exclude_domains) {
  net::ProxyBypassRules rules;
  for (const base::Value& value : onc_exclude_domains.GetList()) {
    if (!value.is_string()) {
      LOG(ERROR) << "Badly formatted ONC exclude domains";
      continue;
    }
    rules.AddRuleFromString(value.GetString());
  }
  return rules;
}

std::string SchemeToString(net::ProxyServer::Scheme scheme) {
  switch (scheme) {
    case net::ProxyServer::SCHEME_DIRECT:
      return kDirectScheme;
    case net::ProxyServer::SCHEME_HTTP:
      return url::kHttpScheme;
    case net::ProxyServer::SCHEME_SOCKS4:
      return kSocks4Scheme;
    case net::ProxyServer::SCHEME_SOCKS5:
      return kSocks5Scheme;
    case net::ProxyServer::SCHEME_HTTPS:
      return url::kHttpsScheme;
    case net::ProxyServer::SCHEME_QUIC:
      return kQuicScheme;
    case net::ProxyServer::SCHEME_INVALID:
      break;
  }
  NOTREACHED();
  return "";
}

void SetProxyForScheme(const net::ProxyConfig::ProxyRules& proxy_rules,
                       const std::string& scheme,
                       const std::string& onc_scheme,
                       base::Value* dict) {
  DCHECK(dict->is_dict());
  const net::ProxyList* proxy_list = nullptr;
  if (proxy_rules.type == net::ProxyConfig::ProxyRules::Type::PROXY_LIST) {
    proxy_list = &proxy_rules.single_proxies;
  } else if (proxy_rules.type ==
             net::ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME) {
    proxy_list = proxy_rules.MapUrlSchemeToProxyList(scheme);
  }
  if (!proxy_list || proxy_list->IsEmpty())
    return;
  const net::ProxyServer& server = proxy_list->Get();
  std::string host = server.host_port_pair().host();

  // For all proxy types except SOCKS, the default scheme of the proxy host is
  // HTTP.
  net::ProxyServer::Scheme default_scheme =
      (onc_scheme == ::onc::proxy::kSocks) ? net::ProxyServer::SCHEME_SOCKS4
                                           : net::ProxyServer::SCHEME_HTTP;
  // Only prefix the host with a non-default scheme.
  if (server.scheme() != default_scheme)
    host = SchemeToString(server.scheme()) + "://" + host;
  base::Value url_dict(base::Value::Type::DICTIONARY);
  url_dict.SetKey(::onc::proxy::kHost, base::Value(host));
  url_dict.SetKey(::onc::proxy::kPort,
                  base::Value(server.host_port_pair().port()));
  dict->SetKey(onc_scheme, std::move(url_dict));
}

// Returns the NetworkConfiugration with |guid| from |network_configs|, or
// nullptr if no such NetworkConfiguration is found.
const base::Value* GetNetworkConfigByGUID(const base::Value& network_configs,
                                          const std::string& guid) {
  for (const auto& network : network_configs.GetList()) {
    DCHECK(network.is_dict());

    std::string current_guid = GetString(network, ::onc::network_config::kGUID);
    if (current_guid == guid)
      return &network;
  }
  return nullptr;
}

// Returns the first Ethernet NetworkConfiguration from |network_configs| with
// "Authentication: None", or nullptr if no such NetworkConfiguration is found.
const base::Value* GetNetworkConfigForEthernetWithoutEAP(
    const base::Value& network_configs) {
  VLOG(2) << "Search for ethernet policy without EAP.";
  for (const auto& network : network_configs.GetList()) {
    DCHECK(network.is_dict());

    std::string type = GetString(network, ::onc::network_config::kType);
    if (type != ::onc::network_type::kEthernet)
      continue;

    const base::Value* ethernet =
        network.FindDictKey(::onc::network_config::kEthernet);
    if (!ethernet)
      continue;

    std::string auth = GetString(*ethernet, ::onc::ethernet::kAuthentication);
    if (auth == ::onc::ethernet::kAuthenticationNone)
      return &network;
  }
  return nullptr;
}

// Returns the NetworkConfiguration object for |network| from
// |network_configs| or nullptr if no matching NetworkConfiguration is found. If
// |network| is a non-Ethernet network, performs a lookup by GUID. If |network|
// is an Ethernet network, tries lookup of the GUID of the shared EthernetEAP
// service, or otherwise returns the first Ethernet NetworkConfiguration with
// "Authentication: None".
const base::Value* GetNetworkConfigForNetworkFromOnc(
    const base::Value& network_configs,
    const NetworkState& network) {
  // In all cases except Ethernet, we use the GUID of |network|.
  if (!network.Matches(NetworkTypePattern::Ethernet()))
    return GetNetworkConfigByGUID(network_configs, network.guid());

  // Ethernet is always shared and thus cannot store a GUID per user. Thus we
  // search for any Ethernet policy instead of a matching GUID.
  // EthernetEAP service contains only the EAP parameters and stores the GUID of
  // the respective ONC policy. The EthernetEAP service itself is however never
  // in state "connected". An EthernetEAP policy must be applied, if an Ethernet
  // service is connected using the EAP parameters.
  const NetworkState* ethernet_eap = nullptr;
  if (NetworkHandler::IsInitialized()) {
    ethernet_eap =
        NetworkHandler::Get()->network_state_handler()->GetEAPForEthernet(
            network.path(), /*connected_only=*/true);
  }

  // The GUID associated with the EthernetEAP service refers to the ONC policy
  // with "Authentication: 8021X".
  if (ethernet_eap)
    return GetNetworkConfigByGUID(network_configs, ethernet_eap->guid());

  // Otherwise, EAP is not used and instead the Ethernet policy with
  // "Authentication: None" applies.
  return GetNetworkConfigForEthernetWithoutEAP(network_configs);
}

// Expects |pref_name| in |pref_service| to be a pref holding an ONC blob.
// Returns the NetworkConfiguration ONC object for |network| from this ONC, or
// nullptr if no configuration is found. See |GetNetworkConfigForNetworkFromOnc|
// for the NetworkConfiguration lookup rules.
const base::Value* GetPolicyForNetworkFromPref(const PrefService* pref_service,
                                               const char* pref_name,
                                               const NetworkState& network) {
  if (!pref_service) {
    VLOG(2) << "No pref service";
    return nullptr;
  }

  const PrefService::Preference* preference =
      pref_service->FindPreference(pref_name);
  if (!preference) {
    VLOG(2) << "No preference " << pref_name;
    // The preference may not exist in tests.
    return nullptr;
  }

  // User prefs are not stored in this Preference yet but only the policy.
  //
  // The policy server incorrectly configures the OpenNetworkConfiguration user
  // policy as Recommended. To work around that, we handle the Recommended and
  // the Mandatory value in the same way.
  // TODO(pneubeck): Remove this workaround, once the server is fixed. See
  // http://crbug.com/280553 .
  if (preference->IsDefaultValue()) {
    VLOG(2) << "Preference has no recommended or mandatory value.";
    // No policy set.
    return nullptr;
  }
  VLOG(2) << "Preference with policy found.";
  const base::Value* onc_policy_value = preference->GetValue();
  DCHECK(onc_policy_value);

  return GetNetworkConfigForNetworkFromOnc(*onc_policy_value, network);
}

// Returns the global network configuration dictionary from the ONC policy of
// the active user if |for_active_user| is true, or from device policy if it is
// false.
const base::Value* GetGlobalConfigFromPolicy(bool for_active_user) {
  std::string username_hash;
  if (for_active_user) {
    const user_manager::User* user =
        user_manager::UserManager::Get()->GetActiveUser();
    if (!user) {
      LOG(ERROR) << "No user logged in yet.";
      return nullptr;
    }
    username_hash = user->username_hash();
  }
  return NetworkHandler::Get()
      ->managed_network_configuration_handler()
      ->GetGlobalConfigFromPolicy(username_hash);
}

}  // namespace

NetworkTypePattern NetworkTypePatternFromOncType(const std::string& type) {
  if (type == ::onc::network_type::kAllTypes)
    return NetworkTypePattern::Default();
  if (type == ::onc::network_type::kCellular)
    return NetworkTypePattern::Cellular();
  if (type == ::onc::network_type::kEthernet)
    return NetworkTypePattern::Ethernet();
  if (type == ::onc::network_type::kTether)
    return NetworkTypePattern::Tether();
  if (type == ::onc::network_type::kVPN)
    return NetworkTypePattern::VPN();
  if (type == ::onc::network_type::kWiFi)
    return NetworkTypePattern::WiFi();
  if (type == ::onc::network_type::kWireless)
    return NetworkTypePattern::Wireless();
  NET_LOG(ERROR) << "Unrecognized ONC type: " << type;
  return NetworkTypePattern::Default();
}

base::Value ConvertOncProxySettingsToProxyConfig(
    const base::Value& onc_proxy_settings) {
  std::string type = GetString(onc_proxy_settings, ::onc::proxy::kType);

  if (type == ::onc::proxy::kDirect) {
    return ProxyConfigDictionary::CreateDirect();
  }
  if (type == ::onc::proxy::kWPAD) {
    return ProxyConfigDictionary::CreateAutoDetect();
  }
  if (type == ::onc::proxy::kPAC) {
    std::string pac_url = GetString(onc_proxy_settings, ::onc::proxy::kPAC);
    GURL url(url_formatter::FixupURL(pac_url, std::string()));
    return ProxyConfigDictionary::CreatePacScript(
        url.is_valid() ? url.spec() : std::string(), false);
  }
  if (type == ::onc::proxy::kManual) {
    const base::Value* manual_dict =
        onc_proxy_settings.FindKey(::onc::proxy::kManual);
    if (!manual_dict) {
      NET_LOG(ERROR) << "Manual proxy missing dictionary";
      return base::Value();
    }
    std::string manual_spec;
    AppendProxyServerForScheme(*manual_dict, ::onc::proxy::kFtp, &manual_spec);
    AppendProxyServerForScheme(*manual_dict, ::onc::proxy::kHttp, &manual_spec);
    AppendProxyServerForScheme(*manual_dict, ::onc::proxy::kSocks,
                               &manual_spec);
    AppendProxyServerForScheme(*manual_dict, ::onc::proxy::kHttps,
                               &manual_spec);

    net::ProxyBypassRules bypass_rules;
    const base::Value* exclude_domains = onc_proxy_settings.FindKeyOfType(
        ::onc::proxy::kExcludeDomains, base::Value::Type::LIST);
    if (exclude_domains)
      bypass_rules = ConvertOncExcludeDomainsToBypassRules(*exclude_domains);
    return ProxyConfigDictionary::CreateFixedServers(manual_spec,
                                                     bypass_rules.ToString());
  }
  NOTREACHED();
  return base::Value();
}

base::Value ConvertProxyConfigToOncProxySettings(
    const base::Value& proxy_config_value) {
  DCHECK(proxy_config_value.is_dict());

  // Create a ProxyConfigDictionary from the dictionary.
  ProxyConfigDictionary proxy_config(proxy_config_value.Clone());

  // Create the result Value and populate it.
  base::Value proxy_settings(base::Value::Type::DICTIONARY);
  ProxyPrefs::ProxyMode mode;
  if (!proxy_config.GetMode(&mode))
    return base::Value();
  switch (mode) {
    case ProxyPrefs::MODE_DIRECT: {
      proxy_settings.SetKey(::onc::proxy::kType,
                            base::Value(::onc::proxy::kDirect));
      break;
    }
    case ProxyPrefs::MODE_AUTO_DETECT: {
      proxy_settings.SetKey(::onc::proxy::kType,
                            base::Value(::onc::proxy::kWPAD));
      break;
    }
    case ProxyPrefs::MODE_PAC_SCRIPT: {
      proxy_settings.SetKey(::onc::proxy::kType,
                            base::Value(::onc::proxy::kPAC));
      std::string pac_url;
      proxy_config.GetPacUrl(&pac_url);
      proxy_settings.SetKey(::onc::proxy::kPAC, base::Value(pac_url));
      break;
    }
    case ProxyPrefs::MODE_FIXED_SERVERS: {
      proxy_settings.SetKey(::onc::proxy::kType,
                            base::Value(::onc::proxy::kManual));
      base::Value manual(base::Value::Type::DICTIONARY);
      std::string proxy_rules_string;
      if (proxy_config.GetProxyServer(&proxy_rules_string)) {
        net::ProxyConfig::ProxyRules proxy_rules;
        proxy_rules.ParseFromString(proxy_rules_string);
        SetProxyForScheme(proxy_rules, url::kFtpScheme, ::onc::proxy::kFtp,
                          &manual);
        SetProxyForScheme(proxy_rules, url::kHttpScheme, ::onc::proxy::kHttp,
                          &manual);
        SetProxyForScheme(proxy_rules, url::kHttpsScheme, ::onc::proxy::kHttps,
                          &manual);
        SetProxyForScheme(proxy_rules, kSocksScheme, ::onc::proxy::kSocks,
                          &manual);
      }
      proxy_settings.SetKey(::onc::proxy::kManual, std::move(manual));

      // Convert the 'bypass_list' string into dictionary entries.
      std::string bypass_rules_string;
      if (proxy_config.GetBypassList(&bypass_rules_string)) {
        net::ProxyBypassRules bypass_rules;
        bypass_rules.ParseFromString(bypass_rules_string);
        base::Value exclude_domains(base::Value::Type::LIST);
        for (const auto& rule : bypass_rules.rules())
          exclude_domains.Append(rule->ToString());
        if (!exclude_domains.GetList().empty()) {
          proxy_settings.SetKey(::onc::proxy::kExcludeDomains,
                                std::move(exclude_domains));
        }
      }
      break;
    }
    default: {
      LOG(ERROR) << "Unexpected proxy mode in Shill config: " << mode;
      return base::Value();
    }
  }
  return proxy_settings;
}

void ExpandStringPlaceholdersInNetworksForUser(const user_manager::User* user,
                                               base::Value* network_configs) {
  DCHECK(network_configs->is_list());
  if (!user) {
    // In tests no user may be logged in. It's not harmful if we just don't
    // expand the strings.
    return;
  }

  // Note: It is OK for the placeholders to be replaced with empty strings if
  // that is what the getters on |user| provide.
  std::map<std::string, std::string> substitutions;
  substitutions[::onc::substitutes::kLoginID] = user->GetAccountName(false);
  substitutions[::onc::substitutes::kLoginEmail] =
      user->GetAccountId().GetUserEmail();
  VariableExpander variable_expander(std::move(substitutions));
  chromeos::onc::ExpandStringsInNetworks(variable_expander, network_configs);
}

int ImportNetworksForUser(const user_manager::User* user,
                          const base::Value& network_configs,
                          std::string* error) {
  DCHECK(network_configs.is_list());
  error->clear();

  base::Value expanded_networks(network_configs.Clone());
  ExpandStringPlaceholdersInNetworksForUser(user, &expanded_networks);

  const NetworkProfile* profile =
      NetworkHandler::Get()->network_profile_handler()->GetProfileForUserhash(
          user->username_hash());
  if (!profile) {
    *error = "User profile doesn't exist for: " + user->display_email();
    return 0;
  }

  bool ethernet_not_found = false;
  int networks_created = 0;
  for (const auto& entry : expanded_networks.GetList()) {
    // TODO(crbug.com/1226202): Remove DictionaryValue conversion once
    // onc::Normalizer is converted.
    const base::DictionaryValue* network = nullptr;
    entry.GetAsDictionary(&network);
    DCHECK(network);

    // Remove irrelevant fields.
    onc::Normalizer normalizer(true /* remove recommended fields */);
    std::unique_ptr<base::DictionaryValue> normalized_network =
        normalizer.NormalizeObject(&onc::kNetworkConfigurationSignature,
                                   *network);

    // TODO(pneubeck): Use ONC and ManagedNetworkConfigurationHandler instead.
    // crbug.com/457936
    std::unique_ptr<base::DictionaryValue> shill_dict =
        onc::TranslateONCObjectToShill(&onc::kNetworkConfigurationSignature,
                                       *normalized_network);

    std::unique_ptr<NetworkUIData> ui_data(
        NetworkUIData::CreateFromONC(::onc::ONC_SOURCE_USER_IMPORT));
    shill_dict->SetKey(shill::kUIDataProperty,
                       base::Value(ui_data->GetAsJson()));
    shill_dict->SetKey(shill::kProfileProperty, base::Value(profile->path));

    std::string type = GetString(*shill_dict, shill::kTypeProperty);
    NetworkConfigurationHandler* config_handler =
        NetworkHandler::Get()->network_configuration_handler();
    if (NetworkTypePattern::Ethernet().MatchesType(type)) {
      // Ethernet has to be configured using an existing Ethernet service.
      const NetworkState* ethernet =
          NetworkHandler::Get()->network_state_handler()->FirstNetworkByType(
              NetworkTypePattern::Ethernet());
      if (ethernet) {
        config_handler->SetShillProperties(ethernet->path(), *shill_dict,
                                           base::OnceClosure(),
                                           network_handler::ErrorCallback());
      } else {
        ethernet_not_found = true;
      }

    } else {
      config_handler->CreateShillConfiguration(
          *shill_dict, network_handler::ServiceResultCallback(),
          network_handler::ErrorCallback());
      ++networks_created;
    }
  }

  if (ethernet_not_found)
    *error = "No Ethernet available to configure.";
  return networks_created;
}

bool PolicyAllowsOnlyPolicyNetworksToAutoconnect(bool for_active_user) {
  const base::Value* global_config = GetGlobalConfigFromPolicy(for_active_user);
  if (!global_config)
    return false;  // By default, all networks are allowed to autoconnect.

  return global_config
      ->FindBoolKey(
          ::onc::global_network_config::kAllowOnlyPolicyNetworksToAutoconnect)
      .value_or(false);
}

const base::Value* GetPolicyForNetwork(const PrefService* profile_prefs,
                                       const PrefService* local_state_prefs,
                                       const NetworkState& network,
                                       ::onc::ONCSource* onc_source) {
  VLOG(2) << "GetPolicyForNetwork: " << network.path();
  *onc_source = ::onc::ONC_SOURCE_NONE;

  const base::Value* network_policy = GetPolicyForNetworkFromPref(
      profile_prefs, ::onc::prefs::kOpenNetworkConfiguration, network);
  if (network_policy) {
    VLOG(1) << "Network " << network.path() << " is managed by user policy.";
    *onc_source = ::onc::ONC_SOURCE_USER_POLICY;
    return network_policy;
  }
  network_policy = GetPolicyForNetworkFromPref(
      local_state_prefs, ::onc::prefs::kDeviceOpenNetworkConfiguration,
      network);
  if (network_policy) {
    VLOG(1) << "Network " << network.path() << " is managed by device policy.";
    *onc_source = ::onc::ONC_SOURCE_DEVICE_POLICY;
    return network_policy;
  }
  VLOG(2) << "Network " << network.path() << " is unmanaged.";
  return nullptr;
}

bool HasPolicyForNetwork(const PrefService* profile_prefs,
                         const PrefService* local_state_prefs,
                         const NetworkState& network) {
  ::onc::ONCSource ignored_onc_source;
  const base::Value* policy = onc::GetPolicyForNetwork(
      profile_prefs, local_state_prefs, network, &ignored_onc_source);
  return policy != nullptr;
}

bool HasUserPasswordSubsitutionVariable(const OncValueSignature& signature,
                                        base::Value* onc_object) {
  DCHECK(onc_object->is_dict());
  if (&signature == &kEAPSignature) {
    std::string* password_field =
        onc_object->FindStringKey(::onc::eap::kPassword);
    if (!password_field)
      return false;
    if (*password_field == ::onc::substitutes::kPasswordPlaceholderVerbatim)
      return true;
  }

  // Recurse into nested objects.
  for (auto it : onc_object->DictItems()) {
    if (!it.second.is_dict())
      continue;

    const OncFieldSignature* field_signature =
        GetFieldSignature(signature, it.first);
    if (!field_signature)
      continue;

    bool result = HasUserPasswordSubsitutionVariable(
        *field_signature->value_signature, &it.second);
    if (result)
      return true;
  }

  return false;
}

bool HasUserPasswordSubsitutionVariable(base::Value* network_configs) {
  for (auto& network : network_configs->GetList()) {
    DCHECK(network.is_dict());
    bool result = HasUserPasswordSubsitutionVariable(
        kNetworkConfigurationSignature, &network);
    if (result)
      return true;
  }
  return false;
}

}  // namespace onc
}  // namespace chromeos
