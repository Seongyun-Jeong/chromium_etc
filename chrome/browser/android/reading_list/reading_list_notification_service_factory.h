// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_READING_LIST_READING_LIST_NOTIFICATION_SERVICE_FACTORY_H_
#define CHROME_BROWSER_ANDROID_READING_LIST_READING_LIST_NOTIFICATION_SERVICE_FACTORY_H_

#include "base/memory/singleton.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class ReadingListNotificationService;

// A factory to create the ReadingListManager singleton.
class ReadingListNotificationServiceFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  static ReadingListNotificationServiceFactory* GetInstance();
  static ReadingListNotificationService* GetForBrowserContext(
      content::BrowserContext* context);

 private:
  friend struct base::DefaultSingletonTraits<
      ReadingListNotificationServiceFactory>;

  ReadingListNotificationServiceFactory();
  ~ReadingListNotificationServiceFactory() override;

  ReadingListNotificationServiceFactory(
      const ReadingListNotificationServiceFactory&) = delete;
  ReadingListNotificationServiceFactory& operator=(
      const ReadingListNotificationServiceFactory&) = delete;

  // BrowserContextKeyedServiceFactory overrides.
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

#endif  // CHROME_BROWSER_ANDROID_READING_LIST_READING_LIST_NOTIFICATION_SERVICE_FACTORY_H_
