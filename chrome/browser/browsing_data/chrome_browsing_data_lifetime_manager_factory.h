// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BROWSING_DATA_CHROME_BROWSING_DATA_LIFETIME_MANAGER_FACTORY_H_
#define CHROME_BROWSER_BROWSING_DATA_CHROME_BROWSING_DATA_LIFETIME_MANAGER_FACTORY_H_

#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

namespace base {
template <typename T>
struct DefaultSingletonTraits;
}

class ChromeBrowsingDataLifetimeManager;
class Profile;

class ChromeBrowsingDataLifetimeManagerFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  ChromeBrowsingDataLifetimeManagerFactory(
      const ChromeBrowsingDataLifetimeManagerFactory&) = delete;
  ChromeBrowsingDataLifetimeManagerFactory& operator=(
      const ChromeBrowsingDataLifetimeManagerFactory&) = delete;

  // Returns the singleton instance of ChromeBrowsingDataLifetimeManagerFactory.
  static ChromeBrowsingDataLifetimeManagerFactory* GetInstance();

  // Returns the ChromeBrowsingDataLifetimeManager associated with |profile|.
  static ChromeBrowsingDataLifetimeManager* GetForProfile(Profile* profile);

 private:
  friend struct base::DefaultSingletonTraits<
      ChromeBrowsingDataLifetimeManagerFactory>;

  ChromeBrowsingDataLifetimeManagerFactory();
  ~ChromeBrowsingDataLifetimeManagerFactory() override;

  // BrowserContextKeyedServiceFactory overrides:
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

#endif  // CHROME_BROWSER_BROWSING_DATA_CHROME_BROWSING_DATA_LIFETIME_MANAGER_FACTORY_H_
