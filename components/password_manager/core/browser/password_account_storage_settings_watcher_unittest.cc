// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/password_account_storage_settings_watcher.h"

#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "components/password_manager/core/browser/password_feature_manager_impl.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/sync/driver/test_sync_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace password_manager {

TEST(PasswordAccountStorageSettingsWatcherTest, NotifiesOnChanges) {
  base::test::ScopedFeatureList feature;
  feature.InitAndEnableFeature(features::kEnablePasswordsAccountStorage);

  TestingPrefServiceSimple pref_service;
  syncer::TestSyncService sync_service;

  PasswordFeatureManagerImpl feature_manager(&pref_service, &sync_service);

  pref_service.registry()->RegisterDictionaryPref(
      prefs::kAccountStoragePerAccountSettings);

  base::MockRepeatingClosure change_callback;
  PasswordAccountStorageSettingsWatcher watcher(&pref_service, &sync_service,
                                                change_callback.Get());

  // Initial state: Not opted in, and saving to the profile store (because not
  // signed in).
  ASSERT_FALSE(feature_manager.IsOptedInForAccountStorage());
  ASSERT_EQ(feature_manager.GetDefaultPasswordStore(),
            PasswordForm::Store::kProfileStore);

  // Sign in (but don't enable Sync-the-feature). Note that the TestSyncService
  // doesn't automatically notify observers of the change.
  CoreAccountInfo account;
  account.gaia = "gaia_id";
  account.email = "email@test.com";
  account.account_id = CoreAccountId::FromGaiaId(account.gaia);
  sync_service.SetAccountInfo(account);
  sync_service.SetHasSyncConsent(false);
  ASSERT_FALSE(sync_service.IsSyncFeatureEnabled());

  // Once the SyncService notifies its observers, the watcher should run the
  // callback: Still not opted in, and the default store now depends on whether
  // the revised opt-in flow is active.
  EXPECT_CALL(change_callback, Run()).WillOnce([&]() {
    EXPECT_FALSE(feature_manager.IsOptedInForAccountStorage());
    EXPECT_FALSE(feature_manager.IsDefaultPasswordStoreSet());
    if (base::FeatureList::IsEnabled(
            features::kPasswordsAccountStorageRevisedOptInFlow)) {
      EXPECT_EQ(feature_manager.GetDefaultPasswordStore(),
                PasswordForm::Store::kProfileStore);
    } else {
      EXPECT_EQ(feature_manager.GetDefaultPasswordStore(),
                PasswordForm::Store::kAccountStore);
    }
  });
  sync_service.FireStateChanged();

  // Opt in. The watcher should run the callback.
  EXPECT_CALL(change_callback, Run()).WillOnce([&]() {
    EXPECT_TRUE(feature_manager.IsOptedInForAccountStorage());
    EXPECT_FALSE(feature_manager.IsDefaultPasswordStoreSet());
  });
  feature_manager.OptInToAccountStorage();

  // Switch to saving to the profile store. The watcher should run the callback.
  EXPECT_CALL(change_callback, Run()).WillOnce([&]() {
    EXPECT_TRUE(feature_manager.IsOptedInForAccountStorage());
    EXPECT_EQ(feature_manager.GetDefaultPasswordStore(),
              PasswordForm::Store::kProfileStore);
  });
  feature_manager.SetDefaultPasswordStore(PasswordForm::Store::kProfileStore);

  // Switch to saving to the account store. The watcher should run the callback.
  EXPECT_CALL(change_callback, Run()).WillOnce([&]() {
    EXPECT_TRUE(feature_manager.IsOptedInForAccountStorage());
    EXPECT_EQ(feature_manager.GetDefaultPasswordStore(),
              PasswordForm::Store::kAccountStore);
  });
  feature_manager.SetDefaultPasswordStore(PasswordForm::Store::kAccountStore);
}

}  // namespace password_manager
