// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PERMISSIONS_PREDICTION_SERVICE_FACTORY_H_
#define CHROME_BROWSER_PERMISSIONS_PREDICTION_SERVICE_FACTORY_H_

#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace base {
template <typename T>
struct DefaultSingletonTraits;
}
namespace permissions {
class PredictionService;
}

class PredictionServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  static permissions::PredictionService* GetForProfile(Profile* profile);
  static PredictionServiceFactory* GetInstance();

  PredictionServiceFactory(const PredictionServiceFactory&) = delete;
  PredictionServiceFactory& operator=(const PredictionServiceFactory&) = delete;

 private:
  friend struct base::DefaultSingletonTraits<PredictionServiceFactory>;

  PredictionServiceFactory();
  ~PredictionServiceFactory() override;

  // BrowserContextKeyedServiceFactory
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;

  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

#endif  // CHROME_BROWSER_PERMISSIONS_PREDICTION_SERVICE_FACTORY_H_
