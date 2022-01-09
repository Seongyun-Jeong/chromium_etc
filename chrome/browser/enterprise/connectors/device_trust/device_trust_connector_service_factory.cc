// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/connectors/device_trust/device_trust_connector_service_factory.h"

#include "base/memory/singleton.h"
#include "build/build_config.h"
#include "chrome/browser/enterprise/connectors/device_trust/device_trust_connector_service.h"
#include "chrome/browser/enterprise/connectors/device_trust/device_trust_features.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/keyed_service/core/keyed_service.h"

#if defined(OS_LINUX) || defined(OS_WIN) || defined(OS_MAC)
#include "chrome/browser/browser_process.h"
#include "chrome/browser/enterprise/connectors/device_trust/browser/browser_device_trust_connector_service.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "components/enterprise/browser/controller/chrome_browser_cloud_management_controller.h"
#include "components/enterprise/browser/device_trust/device_trust_key_manager.h"
#endif  // defined(OS_LINUX) || defined(OS_WIN) || defined(OS_MAC)

namespace enterprise_connectors {

// static
DeviceTrustConnectorServiceFactory*
DeviceTrustConnectorServiceFactory::GetInstance() {
  return base::Singleton<DeviceTrustConnectorServiceFactory>::get();
}

// static
DeviceTrustConnectorService* DeviceTrustConnectorServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<DeviceTrustConnectorService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

bool DeviceTrustConnectorServiceFactory::ServiceIsCreatedWithBrowserContext()
    const {
#if defined(OS_LINUX) || defined(OS_WIN) || defined(OS_MAC)
  return IsDeviceTrustConnectorFeatureEnabled();
#else
  return false;
#endif  // defined(OS_LINUX) || defined(OS_WIN) || defined(OS_MAC)
}

DeviceTrustConnectorServiceFactory::DeviceTrustConnectorServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "DeviceTrustConnectorService",
          BrowserContextDependencyManager::GetInstance()) {}

DeviceTrustConnectorServiceFactory::~DeviceTrustConnectorServiceFactory() =
    default;

KeyedService* DeviceTrustConnectorServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  auto* profile = Profile::FromBrowserContext(context);

  DeviceTrustConnectorService* service = nullptr;

#if defined(OS_LINUX) || defined(OS_WIN) || defined(OS_MAC)
  if (IsDeviceTrustConnectorFeatureEnabled()) {
    auto* key_manager = g_browser_process->browser_policy_connector()
                            ->chrome_browser_cloud_management_controller()
                            ->GetDeviceTrustKeyManager();
    service = new BrowserDeviceTrustConnectorService(key_manager,
                                                     profile->GetPrefs());
  }
#else
  service = new DeviceTrustConnectorService(profile->GetPrefs());
#endif  // defined(OS_LINUX) || defined(OS_WIN) || defined(OS_MAC)

  if (service)
    service->Initialize();

  return service;
}

}  // namespace enterprise_connectors
