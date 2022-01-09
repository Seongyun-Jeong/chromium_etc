// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PERMISSIONS_PERMISSION_MANAGER_FACTORY_H_
#define CHROME_BROWSER_PERMISSIONS_PERMISSION_MANAGER_FACTORY_H_

#include "base/memory/singleton.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

namespace content {
class BrowserContext;
}

namespace permissions {
class PermissionManager;
}

class Profile;

class PermissionManagerFactory : public BrowserContextKeyedServiceFactory {
 public:
  static permissions::PermissionManager* GetForProfile(Profile* profile);
  static PermissionManagerFactory* GetInstance();

  PermissionManagerFactory(const PermissionManagerFactory&) = delete;
  PermissionManagerFactory& operator=(const PermissionManagerFactory&) = delete;

 private:
  friend struct base::DefaultSingletonTraits<PermissionManagerFactory>;

  PermissionManagerFactory();
  ~PermissionManagerFactory() override;

  // BrowserContextKeyedServiceFactory methods:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

#endif  // CHROME_BROWSER_PERMISSIONS_PERMISSION_MANAGER_FACTORY_H_
