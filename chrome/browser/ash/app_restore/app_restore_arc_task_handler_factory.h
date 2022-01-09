// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_APP_RESTORE_APP_RESTORE_ARC_TASK_HANDLER_FACTORY_H_
#define CHROME_BROWSER_ASH_APP_RESTORE_APP_RESTORE_ARC_TASK_HANDLER_FACTORY_H_

#include "base/memory/singleton.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace ash {
namespace app_restore {

class AppRestoreArcTaskHandler;

class AppRestoreArcTaskHandlerFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  static AppRestoreArcTaskHandler* GetForProfile(Profile* profile);

  static AppRestoreArcTaskHandlerFactory* GetInstance();

 private:
  friend struct base::DefaultSingletonTraits<AppRestoreArcTaskHandlerFactory>;

  AppRestoreArcTaskHandlerFactory();
  AppRestoreArcTaskHandlerFactory(const AppRestoreArcTaskHandlerFactory&) =
      delete;
  AppRestoreArcTaskHandlerFactory& operator=(
      const AppRestoreArcTaskHandlerFactory&) = delete;
  ~AppRestoreArcTaskHandlerFactory() override;

  // BrowserContextKeyedServiceFactory.
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
};

}  // namespace app_restore
}  // namespace ash

#endif  // CHROME_BROWSER_ASH_APP_RESTORE_APP_RESTORE_ARC_TASK_HANDLER_FACTORY_H_
