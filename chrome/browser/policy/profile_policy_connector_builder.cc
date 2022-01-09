// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/profile_policy_connector_builder.h"

#include <list>
#include <utility>

#include "base/no_destructor.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/policy/schema_registry_service.h"
#include "chrome/browser/profiles/profile.h"
#include "components/policy/core/common/policy_service.h"
#include "components/policy/core/common/policy_service_impl.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chrome/browser/ash/policy/active_directory/active_directory_policy_manager.h"
#include "chrome/browser/ash/policy/core/user_cloud_policy_manager_ash.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#else  // Non-ChromeOS.
#include "components/policy/core/common/cloud/user_cloud_policy_manager.h"
#endif

namespace policy {

namespace {

std::list<ConfigurationPolicyProvider*>* GetTestProviders() {
  static base::NoDestructor<std::list<ConfigurationPolicyProvider*>> instances;
  return instances.get();
}

}  // namespace

std::unique_ptr<ProfilePolicyConnector>
CreateProfilePolicyConnectorForBrowserContext(
    SchemaRegistry* schema_registry,
    UserCloudPolicyManager* user_cloud_policy_manager,
    ConfigurationPolicyProvider* policy_provider,
    policy::ChromeBrowserPolicyConnector* browser_policy_connector,
    bool force_immediate_load,
    content::BrowserContext* context) {
  const user_manager::User* user = nullptr;
  const CloudPolicyStore* policy_store = nullptr;

#if BUILDFLAG(IS_CHROMEOS_ASH)
  Profile* const profile = Profile::FromBrowserContext(context);
  if (chromeos::ProfileHelper::IsRegularProfile(profile)) {
    user = chromeos::ProfileHelper::Get()->GetUserByProfile(profile);
    CHECK(user);
  }

  // On ChromeOS, we always pass nullptr for the |user_cloud_policy_manager|.
  // This is because the |policy_provider| could be either a
  // UserCloudPolicyManagerAsh or a ActiveDirectoryPolicyManager, both of
  // which should be obtained via UserPolicyManagerFactoryChromeOS APIs.
  CloudPolicyManager* cloud_policy_manager =
      profile->GetUserCloudPolicyManagerAsh();
  ActiveDirectoryPolicyManager* active_directory_manager =
      profile->GetActiveDirectoryPolicyManager();
  if (cloud_policy_manager) {
    policy_store = cloud_policy_manager->core()->store();
  } else if (active_directory_manager) {
    policy_store = active_directory_manager->store();
  }
#else
  if (user_cloud_policy_manager) {
    policy_store = user_cloud_policy_manager->core()->store();
  }
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  return CreateAndInitProfilePolicyConnector(
      schema_registry, browser_policy_connector, policy_provider, policy_store,
      force_immediate_load, user);
}

std::unique_ptr<ProfilePolicyConnector> CreateAndInitProfilePolicyConnector(
    SchemaRegistry* schema_registry,
    policy::ChromeBrowserPolicyConnector* browser_policy_connector,
    ConfigurationPolicyProvider* policy_provider,
    const CloudPolicyStore* policy_store,
    bool force_immediate_load,
    const user_manager::User* user) {
  auto connector = std::make_unique<ProfilePolicyConnector>();

  std::list<ConfigurationPolicyProvider*>* test_providers = GetTestProviders();
  if (test_providers->empty()) {
    connector->Init(user, schema_registry, policy_provider, policy_store,
                    browser_policy_connector, force_immediate_load);
  } else {
    PolicyServiceImpl::Providers providers;
    providers.push_back(test_providers->front());
    test_providers->pop_front();
    auto service = std::make_unique<PolicyServiceImpl>(std::move(providers));
    connector->InitForTesting(std::move(service));
  }

  return connector;
}

void PushProfilePolicyConnectorProviderForTesting(
    ConfigurationPolicyProvider* provider) {
  std::list<ConfigurationPolicyProvider*>* test_providers = GetTestProviders();
  test_providers->push_back(provider);
}

}  // namespace policy
