// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/quick_pair/quick_pair_browser_delegate_impl.h"

#include "ash/quick_pair/common/logging.h"
#include "ash/services/quick_pair/public/mojom/quick_pair_service.mojom.h"
#include "base/memory/scoped_refptr.h"
#include "base/notreached.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/service_process_host.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace ash {
namespace quick_pair {

QuickPairBrowserDelegateImpl::QuickPairBrowserDelegateImpl() = default;

QuickPairBrowserDelegateImpl::~QuickPairBrowserDelegateImpl() = default;

scoped_refptr<network::SharedURLLoaderFactory>
QuickPairBrowserDelegateImpl::GetURLLoaderFactory() {
  Profile* profile = GetActiveProfile();
  if (!profile) {
    return nullptr;
  }

  return profile->GetURLLoaderFactory();
}

signin::IdentityManager* QuickPairBrowserDelegateImpl::GetIdentityManager() {
  Profile* profile = GetActiveProfile();
  if (!profile) {
    return nullptr;
  }

  return IdentityManagerFactory::GetForProfile(profile);
}

PrefService* QuickPairBrowserDelegateImpl::GetActivePrefService() {
  Profile* profile = GetActiveProfile();
  if (!profile) {
    return nullptr;
  }

  return profile->GetPrefs();
}

void QuickPairBrowserDelegateImpl::RequestService(
    mojo::PendingReceiver<mojom::QuickPairService> receiver) {
  content::ServiceProcessHost::Launch<mojom::QuickPairService>(
      std::move(receiver), content::ServiceProcessHost::Options()
                               .WithDisplayName("QuickPair Service")
                               .Pass());
}

Profile* QuickPairBrowserDelegateImpl::GetActiveProfile() {
  if (!user_manager::UserManager::Get()->IsUserLoggedIn()) {
    NOTREACHED();
    return nullptr;
  }

  user_manager::User* active_user =
      user_manager::UserManager::Get()->GetActiveUser();
  DCHECK(active_user);

  return chromeos::ProfileHelper::Get()->GetProfileByUser(active_user);
}

}  // namespace quick_pair
}  // namespace ash
