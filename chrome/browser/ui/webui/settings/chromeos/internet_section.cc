// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/internet_section.h"

#include "ash/constants/ash_features.h"
#include "ash/public/cpp/network_config_service.h"
#include "base/bind.h"
#include "base/containers/contains.h"
#include "base/metrics/histogram_functions.h"
#include "base/no_destructor.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/ui/webui/chromeos/cellular_setup/cellular_setup_localized_strings_provider.h"
#include "chrome/browser/ui/webui/settings/chromeos/constants/routes.mojom.h"
#include "chrome/browser/ui/webui/settings/chromeos/internet_handler.h"
#include "chrome/browser/ui/webui/settings/chromeos/search/search_tag_registry.h"
#include "chrome/browser/ui/webui/webui_util.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/url_constants.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/dbus/hermes/hermes_manager_client.h"
#include "chromeos/strings/grit/chromeos_strings.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/chromeos/strings/grit/ui_chromeos_strings.h"
#include "ui/chromeos/strings/network_element_localized_strings_provider.h"

namespace chromeos {
namespace settings {
namespace {

// These values are persisted to logs. Entries should not be renumbered
// and numeric values should never be reused. They describe the discovery
// state of a network.
enum class NetworkDiscoveryState {
  kExistingNetwork = 0,
  kNewNetwork = 1,
  kMaxValue = kNewNetwork,
};

const std::vector<SearchConcept>& GetNetworkSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_NETWORK_SETTINGS,
       mojom::kNetworkSectionPath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSection,
       {.section = mojom::Section::kNetwork},
       {IDS_OS_SETTINGS_TAG_NETWORK_SETTINGS_ALT1, SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetEthernetConnectedSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_ETHERNET_CONFIGURE,
       mojom::kEthernetDetailsSubpagePath,
       mojom::SearchResultIcon::kEthernet,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kConfigureEthernet}},
      {IDS_OS_SETTINGS_TAG_ETHERNET,
       mojom::kEthernetDetailsSubpagePath,
       mojom::SearchResultIcon::kEthernet,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSubpage,
       {.subpage = mojom::Subpage::kEthernetDetails}},
      {IDS_OS_SETTINGS_TAG_CONFIGURE_IP_AUTOMATICALLY,
       mojom::kEthernetDetailsSubpagePath,
       mojom::SearchResultIcon::kEthernet,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kEthernetAutoConfigureIp},
       {IDS_OS_SETTINGS_TAG_CONFIGURE_IP_AUTOMATICALLY_ALT1,
        IDS_OS_SETTINGS_TAG_CONFIGURE_IP_AUTOMATICALLY_ALT2,
        SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_NAME_SERVERS,
       mojom::kEthernetDetailsSubpagePath,
       mojom::SearchResultIcon::kEthernet,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kEthernetDns},
       {IDS_OS_SETTINGS_TAG_NAME_SERVERS_ALT1,
        IDS_OS_SETTINGS_TAG_NAME_SERVERS_ALT2, SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_PROXY,
       mojom::kEthernetDetailsSubpagePath,
       mojom::SearchResultIcon::kEthernet,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kEthernetProxy},
       {IDS_OS_SETTINGS_TAG_PROXY_ALT1, IDS_OS_SETTINGS_TAG_PROXY_ALT2,
        IDS_OS_SETTINGS_TAG_PROXY_ALT3, IDS_OS_SETTINGS_TAG_PROXY_ALT4,
        SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetEthernetNotConnectedSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_ETHERNET,
       mojom::kNetworkSectionPath,
       mojom::SearchResultIcon::kEthernet,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSection,
       {.section = mojom::Section::kNetwork}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetWifiSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_WIFI,
       mojom::kWifiNetworksSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kHigh,
       mojom::SearchResultType::kSubpage,
       {.subpage = mojom::Subpage::kWifiNetworks},
       {IDS_OS_SETTINGS_TAG_WIFI_ALT1, SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_KNOWN_NETWORKS,
       mojom::kKnownNetworksSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSubpage,
       {.subpage = mojom::Subpage::kKnownNetworks},
       {IDS_OS_SETTINGS_TAG_KNOWN_NETWORKS_ALT1,
        IDS_OS_SETTINGS_TAG_KNOWN_NETWORKS_ALT2, SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetWifiOnSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_WIFI_TURN_OFF,
       mojom::kWifiNetworksSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kWifiOnOff},
       {IDS_OS_SETTINGS_TAG_WIFI_TURN_OFF_ALT1, SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_ADD_WIFI,
       mojom::kWifiNetworksSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kWifiAddNetwork}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetWifiOffSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_WIFI_TURN_ON,
       mojom::kWifiNetworksSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kWifiOnOff},
       {IDS_OS_SETTINGS_TAG_WIFI_TURN_ON_ALT1, SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetWifiConnectedSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_DISCONNECT_WIFI,
       mojom::kWifiDetailsSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kDisconnectWifiNetwork}},
      {IDS_OS_SETTINGS_TAG_PREFER_WIFI_NETWORK,
       mojom::kWifiDetailsSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kPreferWifiNetwork},
       {IDS_OS_SETTINGS_TAG_PREFER_WIFI_NETWORK_ALT1,
        SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_FORGET_WIFI,
       mojom::kWifiDetailsSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kForgetWifiNetwork}},
      {IDS_OS_SETTINGS_TAG_CONFIGURE_IP_AUTOMATICALLY,
       mojom::kWifiDetailsSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kWifiAutoConfigureIp},
       {IDS_OS_SETTINGS_TAG_CONFIGURE_IP_AUTOMATICALLY_ALT1,
        IDS_OS_SETTINGS_TAG_CONFIGURE_IP_AUTOMATICALLY_ALT2,
        SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_NAME_SERVERS,
       mojom::kWifiDetailsSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kWifiDns},
       {IDS_OS_SETTINGS_TAG_NAME_SERVERS_ALT1,
        IDS_OS_SETTINGS_TAG_NAME_SERVERS_ALT2, SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_PROXY,
       mojom::kWifiDetailsSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kWifiProxy},
       {IDS_OS_SETTINGS_TAG_PROXY_ALT1, IDS_OS_SETTINGS_TAG_PROXY_ALT2,
        IDS_OS_SETTINGS_TAG_PROXY_ALT3, IDS_OS_SETTINGS_TAG_PROXY_ALT4,
        SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_AUTO_CONNECT_NETWORK,
       mojom::kWifiDetailsSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kWifiAutoConnectToNetwork},
       {IDS_OS_SETTINGS_TAG_AUTO_CONNECT_NETWORK_ALT1,
        SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetWifiMeteredSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_SETTINGS_INTERNET_NETWORK_METERED,
       mojom::kWifiDetailsSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kWifiMetered}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetWifiHiddenSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_HIDDEN_NETWORK,
       mojom::kWifiDetailsSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kWifiHidden},
       {IDS_OS_SETTINGS_TAG_HIDDEN_NETWORK_ALT1, SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetCellularSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_CELLULAR,
       mojom::kCellularNetworksSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSubpage,
       {.subpage = mojom::Subpage::kMobileDataNetworks},
       {IDS_OS_SETTINGS_TAG_CELLULAR_ALT1, IDS_OS_SETTINGS_TAG_CELLULAR_ALT2,
        IDS_OS_SETTINGS_TAG_CELLULAR_ALT3, SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_CELLULAR_SIM_LOCK,
       mojom::kCellularDetailsSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kCellularSimLock},
       {IDS_OS_SETTINGS_TAG_CELLULAR_SIM_LOCK_ALT1, SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_CELLULAR_ROAMING,
       mojom::kCellularDetailsSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kCellularRoaming}},
      {IDS_OS_SETTINGS_TAG_CELLULAR_APN,
       mojom::kCellularDetailsSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kCellularApn}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetCellularOnSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_CELLULAR_TURN_OFF,
       mojom::kNetworkSectionPath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kMobileOnOff},
       {IDS_OS_SETTINGS_TAG_CELLULAR_TURN_OFF_ALT1, SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetCellularOffSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_CELLULAR_TURN_ON,
       mojom::kNetworkSectionPath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kMobileOnOff},
       {IDS_OS_SETTINGS_TAG_CELLULAR_TURN_ON_ALT1, SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetCellularConnectedSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_CELLULAR_DISCONNECT,
       mojom::kCellularDetailsSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kDisconnectCellularNetwork}},
      {IDS_OS_SETTINGS_TAG_CONFIGURE_IP_AUTOMATICALLY,
       mojom::kCellularDetailsSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kCellularAutoConfigureIp},
       {IDS_OS_SETTINGS_TAG_CONFIGURE_IP_AUTOMATICALLY_ALT1,
        IDS_OS_SETTINGS_TAG_CONFIGURE_IP_AUTOMATICALLY_ALT2,
        SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_NAME_SERVERS,
       mojom::kCellularDetailsSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kCellularDns},
       {IDS_OS_SETTINGS_TAG_NAME_SERVERS_ALT1,
        IDS_OS_SETTINGS_TAG_NAME_SERVERS_ALT2, SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_PROXY,
       mojom::kCellularDetailsSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kCellularProxy},
       {IDS_OS_SETTINGS_TAG_PROXY_ALT1, IDS_OS_SETTINGS_TAG_PROXY_ALT2,
        IDS_OS_SETTINGS_TAG_PROXY_ALT3, IDS_OS_SETTINGS_TAG_PROXY_ALT4,
        SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_AUTO_CONNECT_NETWORK,
       mojom::kCellularDetailsSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kCellularAutoConnectToNetwork},
       {IDS_OS_SETTINGS_TAG_AUTO_CONNECT_NETWORK_ALT1,
        SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetCellularESimCapableSearchTerms() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_ADD_ESIM,
       mojom::kMobileDataNetworksSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kAddESimNetwork},
       {IDS_OS_SETTINGS_TAG_ADD_ESIM_ALT1, IDS_OS_SETTINGS_TAG_ADD_ESIM_ALT2,
        SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetCellularPrimaryIsESimSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_CELLULAR_REMOVE_PROFILE,
       mojom::kCellularDetailsSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kCellularRemoveESimNetwork},
       {IDS_OS_SETTINGS_TAG_CELLULAR_REMOVE_PROFILE_ALT1,
        SearchConcept::kAltTagEnd}},
      {IDS_OS_SETTINGS_TAG_CELLULAR_RENAME_PROFILE,
       mojom::kCellularDetailsSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kCellularRenameESimNetwork},
       {IDS_OS_SETTINGS_TAG_CELLULAR_RENAME_PROFILE_ALT1,
        SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetCellularMeteredSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_SETTINGS_INTERNET_NETWORK_METERED,
       mojom::kCellularDetailsSubpagePath,
       mojom::SearchResultIcon::kCellular,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kCellularMetered}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetInstantTetheringSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_INSTANT_MOBILE_NETWORKS,
       mojom::kMobileDataNetworksSubpagePath,
       mojom::SearchResultIcon::kInstantTethering,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSubpage,
       {.subpage = mojom::Subpage::kMobileDataNetworks}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetInstantTetheringOnSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_INSTANT_TETHERING_TURN_OFF,
       mojom::kMobileDataNetworksSubpagePath,
       mojom::SearchResultIcon::kInstantTethering,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kInstantTetheringOnOff},
       {IDS_OS_SETTINGS_TAG_INSTANT_TETHERING_TURN_OFF_ALT1,
        SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetInstantTetheringOffSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_INSTANT_TETHERING_TURN_ON,
       mojom::kMobileDataNetworksSubpagePath,
       mojom::SearchResultIcon::kInstantTethering,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kInstantTetheringOnOff},
       {IDS_OS_SETTINGS_TAG_INSTANT_TETHERING_TURN_ON_ALT1,
        SearchConcept::kAltTagEnd}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetInstantTetheringConnectedSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_INSTANT_TETHERING_DISCONNECT,
       mojom::kTetherDetailsSubpagePath,
       mojom::SearchResultIcon::kInstantTethering,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSetting,
       {.setting = mojom::Setting::kDisconnectTetherNetwork}},
      {IDS_OS_SETTINGS_TAG_INSTANT_TETHERING,
       mojom::kTetherDetailsSubpagePath,
       mojom::SearchResultIcon::kInstantTethering,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSubpage,
       {.subpage = mojom::Subpage::kTetherDetails}},
  });
  return *tags;
}

const std::vector<SearchConcept>& GetVpnConnectedSearchConcepts() {
  static const base::NoDestructor<std::vector<SearchConcept>> tags({
      {IDS_OS_SETTINGS_TAG_VPN,
       mojom::kVpnDetailsSubpagePath,
       mojom::SearchResultIcon::kWifi,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSubpage,
       {.subpage = mojom::Subpage::kVpnDetails}},
  });
  return *tags;
}

const std::vector<mojom::Setting>& GetEthernetDetailsSettings() {
  static const base::NoDestructor<std::vector<mojom::Setting>> settings({
      mojom::Setting::kConfigureEthernet,
      mojom::Setting::kEthernetAutoConfigureIp,
      mojom::Setting::kEthernetDns,
      mojom::Setting::kEthernetProxy,
  });
  return *settings;
}

const std::vector<mojom::Setting>& GetWifiDetailsSettings() {
  static const base::NoDestructor<std::vector<mojom::Setting>> settings({
      mojom::Setting::kDisconnectWifiNetwork,
      mojom::Setting::kPreferWifiNetwork,
      mojom::Setting::kForgetWifiNetwork,
      mojom::Setting::kWifiAutoConfigureIp,
      mojom::Setting::kWifiDns,
      mojom::Setting::kWifiProxy,
      mojom::Setting::kWifiAutoConnectToNetwork,
      mojom::Setting::kWifiMetered,
      mojom::Setting::kWifiHidden,
  });
  return *settings;
}

const std::vector<mojom::Setting>& GetCellularDetailsSettings() {
  static const base::NoDestructor<std::vector<mojom::Setting>> settings({
      mojom::Setting::kCellularSimLock,
      mojom::Setting::kCellularRoaming,
      mojom::Setting::kCellularApn,
      mojom::Setting::kDisconnectCellularNetwork,
      mojom::Setting::kCellularAutoConfigureIp,
      mojom::Setting::kCellularDns,
      mojom::Setting::kCellularProxy,
      mojom::Setting::kCellularAutoConnectToNetwork,
      mojom::Setting::kCellularMetered,
      mojom::Setting::kCellularRemoveESimNetwork,
      mojom::Setting::kCellularRenameESimNetwork,
  });
  return *settings;
}

const std::vector<mojom::Setting>& GetTetherDetailsSettings() {
  static const base::NoDestructor<std::vector<mojom::Setting>> settings({
      mojom::Setting::kDisconnectTetherNetwork,
  });
  return *settings;
}

bool IsConnected(network_config::mojom::ConnectionStateType connection_state) {
  return connection_state ==
             network_config::mojom::ConnectionStateType::kOnline ||
         connection_state ==
             network_config::mojom::ConnectionStateType::kConnected;
}

bool IsPartOfDetailsSubpage(mojom::SearchResultType type,
                            OsSettingsIdentifier id,
                            mojom::Subpage details_subpage) {
  switch (type) {
    case mojom::SearchResultType::kSection:
      // Applies to a section, not a details subpage.
      return false;

    case mojom::SearchResultType::kSubpage:
      return id.subpage == details_subpage;

    case mojom::SearchResultType::kSetting: {
      const mojom::Setting& setting = id.setting;
      switch (details_subpage) {
        case mojom::Subpage::kEthernetDetails:
          return base::Contains(GetEthernetDetailsSettings(), setting);
        case mojom::Subpage::kWifiDetails:
          return base::Contains(GetWifiDetailsSettings(), setting);
        case mojom::Subpage::kCellularDetails:
          return base::Contains(GetCellularDetailsSettings(), setting);
        case mojom::Subpage::kTetherDetails:
          return base::Contains(GetTetherDetailsSettings(), setting);
        default:
          return false;
      }
    }
  }
}

std::string GetDetailsSubpageUrl(const std::string& url_to_modify,
                                 const std::string& guid) {
  return base::StringPrintf(
      "%s%sguid=%s", url_to_modify.c_str(),
      url_to_modify.find('?') == std::string::npos ? "?" : "&", guid.c_str());
}

bool IsESimCapable() {
  return HermesManagerClient::Get()->GetAvailableEuiccs().size() != 0;
}

absl::optional<std::string> GetCellularActiveSimIccid(
    const network_config::mojom::DeviceStatePropertiesPtr& device) {
  for (const auto& sim_info : *device->sim_infos) {
    if (sim_info->is_primary)
      return sim_info->iccid;
  }
  return absl::nullopt;
}

}  // namespace

InternetSection::InternetSection(Profile* profile,
                                 SearchTagRegistry* search_tag_registry)
    : OsSettingsSection(profile, search_tag_registry) {
  SearchTagRegistry::ScopedTagUpdater updater = registry()->StartUpdate();

  // General network search tags are always added.
  updater.AddSearchTags(GetNetworkSearchConcepts());

  // Receive updates when devices (e.g., Ethernet, Wi-Fi) go on/offline.
  ash::GetNetworkConfigService(
      cros_network_config_.BindNewPipeAndPassReceiver());
  cros_network_config_->AddObserver(receiver_.BindNewPipeAndPassRemote());

  // Fetch initial list of devices and active networks.
  FetchDeviceList();
  FetchNetworkList();
}

InternetSection::~InternetSection() = default;

void InternetSection::AddLoadTimeData(content::WebUIDataSource* html_source) {
  static constexpr webui::LocalizedString kLocalizedStrings[] = {
      {"internetAddCellular", IDS_SETTINGS_INTERNET_ADD_CELLULAR},
      {"internetAddConnection", IDS_SETTINGS_INTERNET_ADD_CONNECTION},
      {"internetAddConnectionExpandA11yLabel",
       IDS_SETTINGS_INTERNET_ADD_CONNECTION_EXPAND_ACCESSIBILITY_LABEL},
      {"internetAddConnectionNotAllowed",
       IDS_SETTINGS_INTERNET_ADD_CONNECTION_NOT_ALLOWED},
      {"internetAddThirdPartyVPN", IDS_SETTINGS_INTERNET_ADD_THIRD_PARTY_VPN},
      {"internetAddVPN", IDS_SETTINGS_INTERNET_ADD_VPN},
      {"internetAddWiFi", IDS_SETTINGS_INTERNET_ADD_WIFI},
      {"internetConfigName", IDS_SETTINGS_INTERNET_CONFIG_NAME},
      {"internetDetailPageTitle", IDS_SETTINGS_INTERNET_DETAIL},
      {"internetDeviceEnabling", IDS_SETTINGS_INTERNET_DEVICE_ENABLING},
      {"internetDeviceBusy", IDS_SETTINGS_INTERNET_DEVICE_BUSY},
      {"internetJoinType", IDS_SETTINGS_INTERNET_JOIN_TYPE},
      {"internetKnownNetworksPageTitle", IDS_SETTINGS_INTERNET_KNOWN_NETWORKS},
      {"internetNoNetworks", IDS_SETTINGS_INTERNET_NO_NETWORKS},
      {"internetPageTitle", IDS_SETTINGS_INTERNET},
      {"internetSummaryButtonA11yLabel",
       IDS_SETTINGS_INTERNET_SUMMARY_BUTTON_ACCESSIBILITY_LABEL},
      {"internetToggleMobileA11yLabel",
       IDS_SETTINGS_INTERNET_TOGGLE_MOBILE_ACCESSIBILITY_LABEL},
      {"internetToggleWiFiA11yLabel",
       IDS_SETTINGS_INTERNET_TOGGLE_WIFI_ACCESSIBILITY_LABEL},
      {"knownNetworksAll", IDS_SETTINGS_INTERNET_KNOWN_NETWORKS_ALL},
      {"knownNetworksButton", IDS_SETTINGS_INTERNET_KNOWN_NETWORKS_BUTTON},
      {"knownNetworksMessage", IDS_SETTINGS_INTERNET_KNOWN_NETWORKS_MESSAGE},
      {"knownNetworksPreferred",
       IDS_SETTINGS_INTERNET_KNOWN_NETWORKS_PREFFERED},
      {"knownNetworksMenuAddPreferred",
       IDS_SETTINGS_INTERNET_KNOWN_NETWORKS_MENU_ADD_PREFERRED},
      {"knownNetworksMenuRemovePreferred",
       IDS_SETTINGS_INTERNET_KNOWN_NETWORKS_MENU_REMOVE_PREFERRED},
      {"knownNetworksMenuForget",
       IDS_SETTINGS_INTERNET_KNOWN_NETWORKS_MENU_FORGET},
      {"mobileNetworkScanningLabel", IDS_MOBILE_NETWORK_SCANNING_MESSAGE},
      {"networkAllowDataRoaming",
       IDS_SETTINGS_SETTINGS_NETWORK_ALLOW_DATA_ROAMING},
      {"networkAllowDataRoamingRequired",
       IDS_SETTINGS_SETTINGS_NETWORK_ALLOW_DATA_ROAMING_REQUIRED},
      {"networkAllowDataRoamingEnabledHome",
       IDS_SETTINGS_SETTINGS_NETWORK_ALLOW_DATA_ROAMING_ENABLED_HOME},
      {"networkAllowDataRoamingEnabledRoaming",
       IDS_SETTINGS_SETTINGS_NETWORK_ALLOW_DATA_ROAMING_ENABLED_ROAMING},
      {"networkAllowDataRoamingDisabled",
       IDS_SETTINGS_SETTINGS_NETWORK_ALLOW_DATA_ROAMING_DISABLED},
      {"networkVpnPreferences", IDS_SETTINGS_INTERNET_NETWORK_VPN_PREFERENCES},
      {"networkAlwaysOnVpn", IDS_SETTINGS_INTERNET_NETWORK_ALWAYS_ON_VPN},
      {"networkAlwaysOnVpnEnableSublabel",
       IDS_SETTINGS_INTERNET_NETWORK_ALWAYS_ON_VPN_ENABLE_SUBLABEL},
      {"networkAlwaysOnVpnEnableLabel",
       IDS_SETTINGS_INTERNET_NETWORK_ALWAYS_ON_VPN_ENABLE_LABEL},
      {"networkAlwaysOnVpnLockdownLabel",
       IDS_SETTINGS_INTERNET_NETWORK_ALWAYS_ON_VPN_LOCKDOWN_LABEL},
      {"networkAlwaysOnVpnLockdownSublabel",
       IDS_SETTINGS_INTERNET_NETWORK_ALWAYS_ON_VPN_LOCKDOWN_SUBLABEL},
      {"networkAlwaysOnVpnService",
       IDS_SETTINGS_INTERNET_NETWORK_ALWAYS_ON_VPN_SERVICE},
      {"networkAutoConnect", IDS_SETTINGS_INTERNET_NETWORK_AUTO_CONNECT},
      {"networkAutoConnectCellular",
       IDS_SETTINGS_INTERNET_NETWORK_AUTO_CONNECT_CELLULAR},
      {"networkButtonActivate", IDS_SETTINGS_INTERNET_BUTTON_ACTIVATE},
      {"networkButtonConfigure", IDS_SETTINGS_INTERNET_BUTTON_CONFIGURE},
      {"networkButtonConnect", IDS_SETTINGS_INTERNET_BUTTON_CONNECT},
      {"networkButtonDisconnect", IDS_SETTINGS_INTERNET_BUTTON_DISCONNECT},
      {"networkButtonForget", IDS_SETTINGS_INTERNET_BUTTON_FORGET},
      {"networkButtonViewAccount", IDS_SETTINGS_INTERNET_BUTTON_VIEW_ACCOUNT},
      {"networkConnectNotAllowed", IDS_SETTINGS_INTERNET_CONNECT_NOT_ALLOWED},
      {"networkHidden", IDS_SETTINGS_INTERNET_NETWORK_HIDDEN},
      {"networkHiddenSublabel", IDS_SETTINGS_INTERNET_NETWORK_HIDDEN_SUBLABEL},
      {"networkIPAddress", IDS_SETTINGS_INTERNET_NETWORK_IP_ADDRESS},
      {"networkIPConfigAuto", IDS_SETTINGS_INTERNET_NETWORK_IP_CONFIG_AUTO},
      {"networkMetered", IDS_SETTINGS_INTERNET_NETWORK_METERED},
      {"networkMeteredDesc", IDS_SETTINGS_INTERNET_NETWORK_METERED_DESC},
      {"networkNameserversLearnMore", IDS_LEARN_MORE},
      {"networkPrefer", IDS_SETTINGS_INTERNET_NETWORK_PREFER},
      {"networkPrimaryUserControlled",
       IDS_SETTINGS_INTERNET_NETWORK_PRIMARY_USER_CONTROLLED},
      {"networkDetailMenuRemoveESim",
       IDS_SETTINGS_INTERNET_NETWORK_MENU_REMOVE},
      {"networkDetailMenuRenameESim",
       IDS_SETTINGS_INTERNET_NETWORK_MENU_RENAME},
      {"networkScanningLabel", IDS_NETWORK_SCANNING_MESSAGE},
      {"networkSectionAdvanced",
       IDS_SETTINGS_INTERNET_NETWORK_SECTION_ADVANCED},
      {"networkSectionAdvancedA11yLabel",
       IDS_SETTINGS_INTERNET_NETWORK_SECTION_ADVANCED_ACCESSIBILITY_LABEL},
      {"networkSectionNetwork", IDS_SETTINGS_INTERNET_NETWORK_SECTION_NETWORK},
      {"networkSectionNetworkExpandA11yLabel",
       IDS_SETTINGS_INTERNET_NETWORK_SECTION_NETWORK_ACCESSIBILITY_LABEL},
      {"networkSectionProxy", IDS_SETTINGS_INTERNET_NETWORK_SECTION_PROXY},
      {"networkSectionProxyExpandA11yLabel",
       IDS_SETTINGS_INTERNET_NETWORK_SECTION_PROXY_ACCESSIBILITY_LABEL},
      {"networkShared", IDS_SETTINGS_INTERNET_NETWORK_SHARED},
      {"networkSharedOwner", IDS_SETTINGS_INTERNET_NETWORK_SHARED_OWNER},
      {"networkSharedNotOwner", IDS_SETTINGS_INTERNET_NETWORK_SHARED_NOT_OWNER},
      {"networkVpnBuiltin", IDS_NETWORK_TYPE_VPN_BUILTIN},
      {"networkOutOfRange", IDS_SETTINGS_INTERNET_WIFI_NETWORK_OUT_OF_RANGE},
      {"cellularSetupDialogTitle",
       IDS_SETTINGS_INTERNET_CELLULAR_SETUP_DIALOG_TITLE},
      {"tetherPhoneOutOfRange",
       IDS_SETTINGS_INTERNET_TETHER_PHONE_OUT_OF_RANGE},
      {"gmscoreNotificationsTitle",
       IDS_SETTINGS_INTERNET_GMSCORE_NOTIFICATIONS_TITLE},
      {"gmscoreNotificationsOneDeviceSubtitle",
       IDS_SETTINGS_INTERNET_GMSCORE_NOTIFICATIONS_ONE_DEVICE_SUBTITLE},
      {"gmscoreNotificationsTwoDevicesSubtitle",
       IDS_SETTINGS_INTERNET_GMSCORE_NOTIFICATIONS_TWO_DEVICES_SUBTITLE},
      {"gmscoreNotificationsManyDevicesSubtitle",
       IDS_SETTINGS_INTERNET_GMSCORE_NOTIFICATIONS_MANY_DEVICES_SUBTITLE},
      {"gmscoreNotificationsFirstStep",
       IDS_SETTINGS_INTERNET_GMSCORE_NOTIFICATIONS_FIRST_STEP},
      {"gmscoreNotificationsSecondStep",
       IDS_SETTINGS_INTERNET_GMSCORE_NOTIFICATIONS_SECOND_STEP},
      {"gmscoreNotificationsThirdStep",
       IDS_SETTINGS_INTERNET_GMSCORE_NOTIFICATIONS_THIRD_STEP},
      {"gmscoreNotificationsFourthStep",
       IDS_SETTINGS_INTERNET_GMSCORE_NOTIFICATIONS_FOURTH_STEP},
      {"tetherConnectionDialogTitle",
       IDS_SETTINGS_INTERNET_TETHER_CONNECTION_DIALOG_TITLE},
      {"tetherConnectionAvailableDeviceTitle",
       IDS_SETTINGS_INTERNET_TETHER_CONNECTION_AVAILABLE_DEVICE_TITLE},
      {"tetherConnectionBatteryPercentage",
       IDS_SETTINGS_INTERNET_TETHER_CONNECTION_BATTERY_PERCENTAGE},
      {"tetherConnectionExplanation",
       IDS_SETTINGS_INTERNET_TETHER_CONNECTION_EXPLANATION},
      {"tetherConnectionCarrierWarning",
       IDS_SETTINGS_INTERNET_TETHER_CONNECTION_CARRIER_WARNING},
      {"tetherConnectionDescriptionTitle",
       IDS_SETTINGS_INTERNET_TETHER_CONNECTION_DESCRIPTION_TITLE},
      {"tetherConnectionDescriptionMobileData",
       IDS_SETTINGS_INTERNET_TETHER_CONNECTION_DESCRIPTION_MOBILE_DATA},
      {"tetherConnectionDescriptionBattery",
       IDS_SETTINGS_INTERNET_TETHER_CONNECTION_DESCRIPTION_BATTERY},
      {"tetherConnectionDescriptionWiFi",
       IDS_SETTINGS_INTERNET_TETHER_CONNECTION_DESCRIPTION_WIFI},
      {"tetherConnectionNotNowButton",
       IDS_SETTINGS_INTERNET_TETHER_CONNECTION_NOT_NOW_BUTTON},
      {"tetherConnectionConnectButton",
       IDS_SETTINGS_INTERNET_TETHER_CONNECTION_CONNECT_BUTTON},
      {"tetherEnableBluetooth", IDS_ENABLE_BLUETOOTH},
      {"cellularNetworkEsimLabel", IDS_SETTINGS_INTERNET_ESIM_LABEL},
      {"eidPopupMenuItemTitle", IDS_CELLULAR_SETUP_EID_MENU_ITEM_TITLE},
      {"cellularNetworkPsimLabel", IDS_SETTINGS_INTERNET_PSIM_LABEL},
      {"pSimNotInsertedLabel", IDS_SETTINGS_INTERNET_PSIM_NOT_INSERTED_LABEL},
      {"eSimNetworkNotSetup",
       IDS_SETTINGS_INTERNET_ESIM_NOT_SETUP_WITH_SETUP_LINK},
      {"cellularNetworkTetherLabel", IDS_SETTINGS_INTERNET_TETHER_LABEL},
      {"showEidPopupButtonLabel",
       IDS_SETTINGS_INTERNET_SHOW_EID_POPUP_BUTTON_LABEL},
      {"eSimNoConnectionErrorToast",
       IDS_SETTINGS_INTERNET_ESIM_NO_CONNECTION_ERROR_TOAST},
      {"eSimMobileDataNotEnabledErrorToast",
       IDS_SETTINGS_INTERNET_ESIM_MOBILE_DATA_NOT_ENABLED_ERROR_TOAST},
      {"eSimProfileLimitReachedErrorToast",
       IDS_SETTINGS_INTERNET_ESIM_PROFILE_LIMIT_REACHED_ERROR_TOAST},
      {"eSimInstallErrorDialogTitle",
       IDS_SETTINGS_INTERNET_NETWORK_INSTALL_ERROR_DIALOG_TITLE},
      {"eSimInstallErrorDialogConfirmationCodeMessage",
       IDS_SETTINGS_INTERNET_NETWORK_INSTALL_ERROR_DIALOG_CONFIRMATION_CODE_MESSAGE},
      {"eSimInstallErrorDialogConfirmationCodeError",
       IDS_CELLULAR_SETUP_ESIM_PAGE_INSTALL_ERROR_DIALOG_CONFIRMATION_CODE_ERROR},
      {"eSimInstallErrorDialogGenericErrorMessage",
       IDS_SETTINGS_INTERNET_NETWORK_INSTALL_ERROR_DIALOG_GENERIC_ERROR_MESSAGE},
      {"eSimRenameProfileDialogLabel",
       IDS_SETTINGS_INTERNET_NETWORK_RENAME_DIALOG_RENAME_PROFILE},
      {"eSimRenameProfileDialogDone",
       IDS_SETTINGS_INTERNET_NETWORK_RENAME_DIALOG_DONE},
      {"eSimRenameProfileDialogCancel",
       IDS_SETTINGS_INTERNET_NETWORK_RENAME_DIALOG_CANCEL},
      {"eSimRenameProfileInputTitle",
       IDS_SETTINGS_INTERNET_NETWORK_RENAME_INPUT_TITLE},
      {"eSimRenameProfileInputSubtitle",
       IDS_SETTINGS_INTERNET_NETWORK_RENAME_INPUT_SUBTITLE},
      {"eSimRenameProfileInputCharacterCount",
       IDS_SETTINGS_INTERNET_NETWORK_RENAME_INPUT_CHARACTER_COUNT},
      {"eSimRenameProfileDoneBtnA11yLabel",
       IDS_SETTINGS_INTERNET_NETWORK_RENAME_DONE_BUTTON_A11Y_LABEL},
      {"eSimRenameProfileInputA11yLabel",
       IDS_SETTINGS_INTERNET_NETWORK_RENAME_INPUT_A11Y_LABEL},
      {"eSimRenameProfileDialogError",
       IDS_SETTINGS_INTERNET_NETWORK_RENAME_DIALOG_ERROR_MESSAGE},
      {"eSimRenameProfileDialogErrorToast",
       IDS_SETTINGS_INTERNET_NETWORK_RENAME_DIALOG_ERROR_TOAST},
      {"eSimRemoveProfileDialogCancel",
       IDS_SETTINGS_INTERNET_NETWORK_REMOVE_PROFILE_DIALOG_CANCEL},
      {"esimRemoveProfileDialogTitle",
       IDS_SETTINGS_INTERNET_NETWORK_REMOVE_PROFILE_DIALOG_TITLE},
      {"eSimRemoveProfileDialogDescription",
       IDS_SETTINGS_INTERNET_NETWORK_REMOVE_PROFILE_DIALOG_DESCRIPTION},
      {"eSimRemoveProfileDialogRemove",
       IDS_SETTINGS_INTERNET_NETWORK_REMOVE_PROFILE_DIALOG_REMOVE},
      {"eSimRemoveProfileDialogError",
       IDS_SETTINGS_INTERNET_NETWORK_REMOVE_PROFILE_DIALOG_ERROR_MESSAGE},
      {"eSimRemoveProfileDialogOkay",
       IDS_SETTINGS_INTERNET_NETWORK_REMOVE_PROFILE_DIALOG_OKAY},
      {"eSimRemoveProfileCancelA11yLabel",
       IDS_SETTINGS_INTERNET_NETWORK_REMOVE_PROFILE_DIALOG_A11Y_CANCEL},
      {"eSimRemoveProfileRemoveA11yLabel",
       IDS_SETTINGS_INTERNET_NETWORK_REMOVE_PROFILE_DIALOG_A11Y_REMOVE},
      {"eSimDialogConnectionWarning",
       IDS_SETTINGS_INTERNET_ESIM_DIALOG_CONNECTION_WARNING},
      {"cellularNetworkInstallingProfile",
       IDS_SETTINGS_INTERNET_NETWORK_CELLULAR_INSTALLING_PROFILE},
      {"cellularNetworkRemovingProfile",
       IDS_SETTINGS_INTERNET_NETWORK_CELLULAR_REMOVING_PROFILE},
      {"cellularNetworkRenamingProfile",
       IDS_SETTINGS_INTERNET_NETWORK_CELLULAR_RENAMING_PROFILE},
      {"cellularNetworkConnectingToProfile",
       IDS_SETTINGS_INTERNET_NETWORK_CELLULAR_CONNECTING_TO_PROFILE},
      {"cellularNetworRefreshingProfileListProfile",
       IDS_SETTINGS_INTERNET_NETWORK_CELLULAR_REFRESHING_PROFILE_LIST},
      {"cellularNetworkResettingESim",
       IDS_SETTINGS_INTERNET_NETWORK_CELLULAR_RESETTING_ESIM},
  };
  html_source->AddLocalizedStrings(kLocalizedStrings);

  ui::network_element::AddLocalizedStrings(html_source);
  ui::network_element::AddOncLocalizedStrings(html_source);
  ui::network_element::AddDetailsLocalizedStrings(html_source);
  ui::network_element::AddConfigLocalizedStrings(html_source);
  ui::network_element::AddErrorLocalizedStrings(html_source);
  cellular_setup::AddNonStringLoadTimeData(html_source);
  cellular_setup::AddLocalizedStrings(html_source);

  html_source->AddBoolean(
      "bypassConnectivityCheck",
      base::FeatureList::IsEnabled(
          ash::features::kCellularBypassESimInstallationConnectivityCheck));
  html_source->AddBoolean("showTechnologyBadge",
                          !ash::features::IsSeparateNetworkIconsEnabled());
  html_source->AddBoolean(
      "showMeteredToggle",
      base::FeatureList::IsEnabled(::features::kMeteredShowToggle));
  html_source->AddBoolean(
      "showHiddenToggle",
      base::FeatureList::IsEnabled(::features::kShowHiddenNetworkToggle));

  html_source->AddString("networkGoogleNameserversLearnMoreUrl",
                         chrome::kGoogleNameserversLearnMoreURL);

  html_source->AddString("wifiHiddenNetworkLearnMoreUrl",
                         chrome::kWifiHiddenNetworkURL);

  html_source->AddString(
      "networkNotSynced",
      l10n_util::GetStringFUTF16(
          IDS_SETTINGS_INTERNET_NETWORK_NOT_SYNCED,
          GetHelpUrlWithBoard(chrome::kWifiSyncLearnMoreURL)));
  html_source->AddString(
      "networkSyncedUser",
      l10n_util::GetStringFUTF16(
          IDS_SETTINGS_INTERNET_NETWORK_SYNCED_USER,
          GetHelpUrlWithBoard(chrome::kWifiSyncLearnMoreURL)));
  html_source->AddString(
      "networkSyncedDevice",
      l10n_util::GetStringFUTF16(
          IDS_SETTINGS_INTERNET_NETWORK_SYNCED_DEVICE,
          GetHelpUrlWithBoard(chrome::kWifiSyncLearnMoreURL)));
  html_source->AddString(
      "internetNoNetworksMobileData",
      l10n_util::GetStringFUTF16(
          IDS_SETTINGS_INTERNET_LOOKING_FOR_MOBILE_NETWORK,
          GetHelpUrlWithBoard(chrome::kInstantTetheringLearnMoreURL)));
  html_source->AddString(
      "tetherNetworkNotSetup",
      l10n_util::GetStringFUTF16(
          IDS_SETTINGS_INTERNET_TETHER_NOT_SETUP_WITH_LEARN_MORE_LINK,
          GetHelpUrlWithBoard(chrome::kInstantTetheringLearnMoreURL)));
}

void InternetSection::AddHandlers(content::WebUI* web_ui) {
  web_ui->AddMessageHandler(std::make_unique<InternetHandler>(profile()));
}

int InternetSection::GetSectionNameMessageId() const {
  return IDS_SETTINGS_INTERNET;
}

mojom::Section InternetSection::GetSection() const {
  return mojom::Section::kNetwork;
}

mojom::SearchResultIcon InternetSection::GetSectionIcon() const {
  return mojom::SearchResultIcon::kWifi;
}

std::string InternetSection::GetSectionPath() const {
  return mojom::kNetworkSectionPath;
}

bool InternetSection::LogMetric(mojom::Setting setting,
                                base::Value& value) const {
  switch (setting) {
    case mojom::Setting::kWifiHidden:
      base::UmaHistogramBoolean("ChromeOS.Settings.Wifi.Hidden",
                                value.GetBool());
      return true;
    case mojom::Setting::kWifiAddNetwork:
      // An added wifi network an empty GUID means the user manually
      // configured and added a new wifi.
      base::UmaHistogramEnumeration(
          "ChromeOS.Settings.Wifi.AddNetwork",
          value.GetString().empty() ? NetworkDiscoveryState::kNewNetwork
                                    : NetworkDiscoveryState::kExistingNetwork);
      return true;
    default:
      return false;
  }
}

void InternetSection::RegisterHierarchy(HierarchyGenerator* generator) const {
  // Ethernet details.
  generator->RegisterTopLevelSubpage(IDS_SETTINGS_INTERNET_ETHERNET_DETAILS,
                                     mojom::Subpage::kEthernetDetails,
                                     mojom::SearchResultIcon::kEthernet,
                                     mojom::SearchResultDefaultRank::kMedium,
                                     mojom::kEthernetDetailsSubpagePath);
  RegisterNestedSettingBulk(mojom::Subpage::kEthernetDetails,
                            GetEthernetDetailsSettings(), generator);

  // Wi-Fi networks.
  generator->RegisterTopLevelSubpage(
      IDS_SETTINGS_INTERNET_WIFI_NETWORKS, mojom::Subpage::kWifiNetworks,
      mojom::SearchResultIcon::kWifi, mojom::SearchResultDefaultRank::kMedium,
      mojom::kWifiNetworksSubpagePath);
  static constexpr mojom::Setting kWifiNetworksSettings[] = {
      mojom::Setting::kWifiOnOff,
      mojom::Setting::kWifiAddNetwork,
  };
  RegisterNestedSettingBulk(mojom::Subpage::kWifiNetworks,
                            kWifiNetworksSettings, generator);
  generator->RegisterTopLevelAltSetting(mojom::Setting::kWifiOnOff);

  // Wi-Fi details.
  generator->RegisterNestedSubpage(
      IDS_SETTINGS_INTERNET_WIFI_DETAILS, mojom::Subpage::kWifiDetails,
      mojom::Subpage::kWifiNetworks, mojom::SearchResultIcon::kWifi,
      mojom::SearchResultDefaultRank::kMedium, mojom::kWifiDetailsSubpagePath);
  RegisterNestedSettingBulk(mojom::Subpage::kWifiDetails,
                            GetWifiDetailsSettings(), generator);

  // Known networks.
  generator->RegisterNestedSubpage(
      IDS_SETTINGS_INTERNET_KNOWN_NETWORKS, mojom::Subpage::kKnownNetworks,
      mojom::Subpage::kWifiNetworks, mojom::SearchResultIcon::kWifi,
      mojom::SearchResultDefaultRank::kMedium,
      mojom::kKnownNetworksSubpagePath);
  generator->RegisterNestedAltSetting(mojom::Setting::kPreferWifiNetwork,
                                      mojom::Subpage::kKnownNetworks);
  generator->RegisterNestedAltSetting(mojom::Setting::kForgetWifiNetwork,
                                      mojom::Subpage::kKnownNetworks);

  // Mobile data. Used for both Cellular and Instant Tethering networks.
  generator->RegisterTopLevelSubpage(IDS_SETTINGS_INTERNET_MOBILE_DATA_NETWORKS,
                                     mojom::Subpage::kMobileDataNetworks,
                                     mojom::SearchResultIcon::kCellular,
                                     mojom::SearchResultDefaultRank::kMedium,
                                     mojom::kMobileDataNetworksSubpagePath);
  static constexpr mojom::Setting kMobileDataNetworksSettings[] = {
      mojom::Setting::kMobileOnOff, mojom::Setting::kInstantTetheringOnOff,
      mojom::Setting::kAddESimNetwork};
  RegisterNestedSettingBulk(mojom::Subpage::kMobileDataNetworks,
                            kMobileDataNetworksSettings, generator);
  generator->RegisterTopLevelAltSetting(mojom::Setting::kMobileOnOff);

  // Cellular details. Cellular details are considered a child of the mobile
  // data subpage. However, note that if Instant Tethering is not available,
  // clicking on "Mobile data" at the Network section navigates users directly
  // to the cellular details page and skips over the mobile data subpage.
  generator->RegisterNestedSubpage(
      IDS_SETTINGS_INTERNET_CELLULAR_DETAILS, mojom::Subpage::kCellularDetails,
      mojom::Subpage::kMobileDataNetworks, mojom::SearchResultIcon::kCellular,
      mojom::SearchResultDefaultRank::kMedium,
      mojom::kCellularDetailsSubpagePath);
  RegisterNestedSettingBulk(mojom::Subpage::kCellularDetails,
                            GetCellularDetailsSettings(), generator);

  // Instant Tethering. Although this is a multi-device feature, its UI resides
  // in the network section.
  generator->RegisterNestedSubpage(
      IDS_SETTINGS_INTERNET_INSTANT_TETHERING_DETAILS,
      mojom::Subpage::kTetherDetails, mojom::Subpage::kMobileDataNetworks,
      mojom::SearchResultIcon::kInstantTethering,
      mojom::SearchResultDefaultRank::kMedium,
      mojom::kTetherDetailsSubpagePath);
  RegisterNestedSettingBulk(mojom::Subpage::kTetherDetails,
                            GetTetherDetailsSettings(), generator);

  // VPN.
  generator->RegisterTopLevelSubpage(
      IDS_SETTINGS_INTERNET_VPN_DETAILS, mojom::Subpage::kVpnDetails,
      mojom::SearchResultIcon::kWifi, mojom::SearchResultDefaultRank::kMedium,
      mojom::kVpnDetailsSubpagePath);
}

std::string InternetSection::ModifySearchResultUrl(
    mojom::SearchResultType type,
    OsSettingsIdentifier id,
    const std::string& url_to_modify) const {
  std::string modified_url =
      OsSettingsSection::ModifySearchResultUrl(type, id, url_to_modify);

  if (IsPartOfDetailsSubpage(type, id, mojom::Subpage::kEthernetDetails))
    return GetDetailsSubpageUrl(modified_url, *connected_ethernet_guid_);

  if (IsPartOfDetailsSubpage(type, id, mojom::Subpage::kWifiDetails))
    return GetDetailsSubpageUrl(modified_url, *connected_wifi_guid_);

  if (IsPartOfDetailsSubpage(type, id, mojom::Subpage::kCellularDetails))
    return GetDetailsSubpageUrl(modified_url, *active_cellular_guid_);

  if (IsPartOfDetailsSubpage(type, id, mojom::Subpage::kTetherDetails))
    return GetDetailsSubpageUrl(modified_url, *connected_tether_guid_);

  if (IsPartOfDetailsSubpage(type, id, mojom::Subpage::kVpnDetails))
    return GetDetailsSubpageUrl(modified_url, *connected_vpn_guid_);

  // Use default implementation.
  return modified_url;
}

void InternetSection::OnDeviceStateListChanged() {
  FetchDeviceList();
}

void InternetSection::OnActiveNetworksChanged(
    std::vector<network_config::mojom::NetworkStatePropertiesPtr> networks) {
  FetchNetworkList();
}

void InternetSection::FetchDeviceList() {
  cros_network_config_->GetDeviceStateList(
      base::BindOnce(&InternetSection::OnDeviceList, base::Unretained(this)));
}

void InternetSection::OnDeviceList(
    std::vector<network_config::mojom::DeviceStatePropertiesPtr> devices) {
  using network_config::mojom::DeviceStateType;
  using network_config::mojom::NetworkType;

  SearchTagRegistry::ScopedTagUpdater updater = registry()->StartUpdate();

  updater.RemoveSearchTags(GetWifiSearchConcepts());
  updater.RemoveSearchTags(GetWifiOnSearchConcepts());
  updater.RemoveSearchTags(GetWifiOffSearchConcepts());
  updater.RemoveSearchTags(GetCellularOnSearchConcepts());
  updater.RemoveSearchTags(GetCellularOffSearchConcepts());
  updater.RemoveSearchTags(GetCellularESimCapableSearchTerms());
  updater.RemoveSearchTags(GetInstantTetheringSearchConcepts());
  updater.RemoveSearchTags(GetInstantTetheringOnSearchConcepts());
  updater.RemoveSearchTags(GetInstantTetheringOffSearchConcepts());

  // Keep track of ethernet devices to handle an edge case where Ethernet device
  // is present but no network is connected.
  does_ethernet_device_exist_ = false;

  active_cellular_iccid_.reset();

  for (const auto& device : devices) {
    switch (device->type) {
      case NetworkType::kWiFi:
        updater.AddSearchTags(GetWifiSearchConcepts());
        if (device->device_state == DeviceStateType::kEnabled)
          updater.AddSearchTags(GetWifiOnSearchConcepts());
        else if (device->device_state == DeviceStateType::kDisabled)
          updater.AddSearchTags(GetWifiOffSearchConcepts());
        break;

      case NetworkType::kCellular:
        active_cellular_iccid_ = GetCellularActiveSimIccid(device);

        // Note: Cellular search concepts all point to the cellular details
        // page, which is only available if a cellular network exists. This
        // check is in OnNetworkList().
        if (device->device_state == DeviceStateType::kEnabled) {
          updater.AddSearchTags(GetCellularOnSearchConcepts());
          if (IsESimCapable())
            updater.AddSearchTags(GetCellularESimCapableSearchTerms());
        } else if (device->device_state == DeviceStateType::kDisabled) {
          updater.AddSearchTags(GetCellularOffSearchConcepts());
        }
        break;

      case NetworkType::kTether:
        updater.AddSearchTags(GetInstantTetheringSearchConcepts());
        if (device->device_state == DeviceStateType::kEnabled)
          updater.AddSearchTags(GetInstantTetheringOnSearchConcepts());
        else if (device->device_state == DeviceStateType::kDisabled)
          updater.AddSearchTags(GetInstantTetheringOffSearchConcepts());
        break;

      case NetworkType::kEthernet:
        does_ethernet_device_exist_ = true;
        break;

      default:
        // Note: Ethernet and VPN only show search tags when connected, and
        // categories such as Mobile/Wireless do not have search tags.
        break;
    }
  }
}

void InternetSection::FetchNetworkList() {
  cros_network_config_->GetNetworkStateList(
      network_config::mojom::NetworkFilter::New(
          network_config::mojom::FilterType::kVisible,
          network_config::mojom::NetworkType::kAll,
          network_config::mojom::kNoLimit),
      base::BindOnce(&InternetSection::OnNetworkList, base::Unretained(this)));
}

void InternetSection::OnNetworkList(
    std::vector<network_config::mojom::NetworkStatePropertiesPtr> networks) {
  using network_config::mojom::NetworkType;

  SearchTagRegistry::ScopedTagUpdater updater = registry()->StartUpdate();

  updater.RemoveSearchTags(GetEthernetConnectedSearchConcepts());
  updater.RemoveSearchTags(GetEthernetNotConnectedSearchConcepts());
  updater.RemoveSearchTags(GetWifiConnectedSearchConcepts());
  updater.RemoveSearchTags(GetWifiMeteredSearchConcepts());
  updater.RemoveSearchTags(GetWifiHiddenSearchConcepts());
  updater.RemoveSearchTags(GetCellularSearchConcepts());
  updater.RemoveSearchTags(GetCellularConnectedSearchConcepts());
  updater.RemoveSearchTags(GetCellularPrimaryIsESimSearchConcepts());
  updater.RemoveSearchTags(GetCellularMeteredSearchConcepts());
  updater.RemoveSearchTags(GetInstantTetheringConnectedSearchConcepts());
  updater.RemoveSearchTags(GetVpnConnectedSearchConcepts());

  active_cellular_guid_.reset();

  connected_ethernet_guid_.reset();
  connected_wifi_guid_.reset();
  connected_tether_guid_.reset();
  connected_vpn_guid_.reset();

  for (const auto& network : networks) {
    // Special case: Some cellular search functionality is available even if the
    // primary cellular network is not connected.
    if (network->type == NetworkType::kCellular) {
      bool is_primary_cellular_network =
          active_cellular_iccid_.has_value() &&
          network->type_state->get_cellular()->iccid == *active_cellular_iccid_;

      if (is_primary_cellular_network) {
        active_cellular_guid_ = network->guid;
        updater.AddSearchTags(GetCellularSearchConcepts());

        // If the primary cellular network is ESim.
        if (!network->type_state->get_cellular()->eid.empty())
          updater.AddSearchTags(GetCellularPrimaryIsESimSearchConcepts());
      }
    }

    if (!IsConnected(network->connection_state))
      continue;

    switch (network->type) {
      case NetworkType::kEthernet:
        connected_ethernet_guid_ = network->guid;
        updater.AddSearchTags(GetEthernetConnectedSearchConcepts());
        break;

      case NetworkType::kWiFi:
        connected_wifi_guid_ = network->guid;
        updater.AddSearchTags(GetWifiConnectedSearchConcepts());
        if (base::FeatureList::IsEnabled(::features::kMeteredShowToggle))
          updater.AddSearchTags(GetWifiMeteredSearchConcepts());
        if (base::FeatureList::IsEnabled(::features::kShowHiddenNetworkToggle))
          updater.AddSearchTags(GetWifiHiddenSearchConcepts());
        break;

      case NetworkType::kCellular:
        updater.AddSearchTags(GetCellularConnectedSearchConcepts());
        if (base::FeatureList::IsEnabled(::features::kMeteredShowToggle))
          updater.AddSearchTags(GetCellularMeteredSearchConcepts());
        break;

      case NetworkType::kTether:
        connected_tether_guid_ = network->guid;
        updater.AddSearchTags(GetInstantTetheringConnectedSearchConcepts());
        break;

      case NetworkType::kVPN:
        connected_vpn_guid_ = network->guid;
        updater.AddSearchTags(GetVpnConnectedSearchConcepts());
        break;

      default:
        // Note: Category types such as Mobile/Wireless do not have search tags.
        break;
    }
  }

  // Edge case where Ethernet device is present but no network is connected,
  // i.e. on Chromeboxes. http://crbug.com/1096768
  if (does_ethernet_device_exist_ && !connected_ethernet_guid_.has_value()) {
    updater.AddSearchTags(GetEthernetNotConnectedSearchConcepts());
  }
}

}  // namespace settings
}  // namespace chromeos
