// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/metrics/android_session_durations_service_factory.h"

#include "base/no_destructor.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/android/metrics/android_session_durations_service.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"

namespace {
std::vector<AndroidSessionDurationsService*> GetForAllActiveProfiles() {
  std::vector<AndroidSessionDurationsService*> services;

  Profile* profile = ProfileManager::GetActiveUserProfile();
  DCHECK(profile);
  services.push_back(
      AndroidSessionDurationsServiceFactory::GetForProfile(profile));

  Profile* otr_profile =
      profile->GetPrimaryOTRProfile(/*create_if_needed=*/false);
  if (otr_profile) {
    services.push_back(
        AndroidSessionDurationsServiceFactory::GetForProfile(otr_profile));
  }
  return services;
}
}  // namespace

// static
AndroidSessionDurationsService*
AndroidSessionDurationsServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<AndroidSessionDurationsService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
AndroidSessionDurationsServiceFactory*
AndroidSessionDurationsServiceFactory::GetInstance() {
  static base::NoDestructor<AndroidSessionDurationsServiceFactory> instance;
  return instance.get();
}

// static
void AndroidSessionDurationsServiceFactory::OnAppEnterForeground(
    base::TimeTicks session_start) {
  for (auto* service : GetForAllActiveProfiles()) {
    service->OnAppEnterForeground(session_start);
  }
}

// static
void AndroidSessionDurationsServiceFactory::OnAppEnterBackground(
    base::TimeDelta session_length) {
  for (auto* service : GetForAllActiveProfiles()) {
    service->OnAppEnterBackground(session_length);
  }
}

AndroidSessionDurationsServiceFactory::AndroidSessionDurationsServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "AndroidSessionDurationsService",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(SyncServiceFactory::GetInstance());
  DependsOn(IdentityManagerFactory::GetInstance());
}

AndroidSessionDurationsServiceFactory::
    ~AndroidSessionDurationsServiceFactory() = default;

KeyedService* AndroidSessionDurationsServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (profile->IsIncognitoProfile()) {
    auto* service = new AndroidSessionDurationsService();
    service->InitializeForIncognitoProfile();
    return service;
  }

  // Lifetime metric is not recorded for non-incognito off the record profiles.
  if (profile->IsOffTheRecord())
    return nullptr;

  auto* service = new AndroidSessionDurationsService();
  service->InitializeForRegularProfile(
      SyncServiceFactory::GetForProfile(profile),
      IdentityManagerFactory::GetForProfile(profile));
  return service;
}

content::BrowserContext*
AndroidSessionDurationsServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextOwnInstanceInIncognito(context);
}

bool AndroidSessionDurationsServiceFactory::ServiceIsNULLWhileTesting() const {
  return true;
}

bool AndroidSessionDurationsServiceFactory::ServiceIsCreatedWithBrowserContext()
    const {
  return true;
}
