// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/passwords/ios_chrome_affiliation_service_factory.h"

#include <memory>
#include <utility>

#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "components/keyed_service/ios/browser_state_dependency_manager.h"
#include "components/keyed_service/ios/browser_state_keyed_service_factory.h"
#include "components/password_manager/core/browser/password_manager_constants.h"
#include "components/password_manager/core/browser/site_affiliation/affiliation_service_impl.h"
#include "ios/chrome/browser/application_context.h"
#include "ios/chrome/browser/browser_state/browser_state_otr_helper.h"
#include "ios/chrome/browser/browser_state/chrome_browser_state.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

// static
IOSChromeAffiliationServiceFactory*
IOSChromeAffiliationServiceFactory::GetInstance() {
  static base::NoDestructor<IOSChromeAffiliationServiceFactory> instance;
  return instance.get();
}

// static
password_manager::AffiliationService*
IOSChromeAffiliationServiceFactory::GetForBrowserState(
    web::BrowserState* browser_state) {
  return static_cast<password_manager::AffiliationService*>(
      GetInstance()->GetServiceForBrowserState(browser_state, true));
}

IOSChromeAffiliationServiceFactory::IOSChromeAffiliationServiceFactory()
    : BrowserStateKeyedServiceFactory(
          "AffiliationService",
          BrowserStateDependencyManager::GetInstance()) {}

IOSChromeAffiliationServiceFactory::~IOSChromeAffiliationServiceFactory() =
    default;

std::unique_ptr<KeyedService>
IOSChromeAffiliationServiceFactory::BuildServiceInstanceFor(
    web::BrowserState* context) const {
  scoped_refptr<base::SequencedTaskRunner> backend_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE});
  auto affiliation_service =
      std::make_unique<password_manager::AffiliationServiceImpl>(
          context->GetSharedURLLoaderFactory(), backend_task_runner);

  base::FilePath database_path = context->GetStatePath().Append(
      password_manager::kAffiliationDatabaseFileName);
  affiliation_service->Init(
      GetApplicationContext()->GetNetworkConnectionTracker(), database_path);

  return affiliation_service;
}
