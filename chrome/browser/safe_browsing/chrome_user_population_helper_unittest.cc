// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/chrome_user_population_helper.h"

#include "base/feature_list.h"
#include "chrome/browser/safe_browsing/advanced_protection_status_manager.h"
#include "chrome/browser/safe_browsing/advanced_protection_status_manager_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/buildflags.h"
#include "components/safe_browsing/core/common/features.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/sync/base/model_type.h"
#include "components/sync/driver/test_sync_service.h"
#include "components/unified_consent/pref_names.h"
#include "components/version_info/version_info.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace safe_browsing {

namespace {

std::unique_ptr<KeyedService> CreateTestSyncService(
    content::BrowserContext* context) {
  return std::make_unique<syncer::TestSyncService>();
}

}  // namespace

TEST(GetUserPopulationForProfileTest, PopulatesPopulation) {
  content::BrowserTaskEnvironment task_environment;
  TestingProfile profile;
  SetSafeBrowsingState(profile.GetPrefs(),
                       SafeBrowsingState::STANDARD_PROTECTION);
  ChromeUserPopulation population = GetUserPopulationForProfile(&profile);
  EXPECT_EQ(population.user_population(), ChromeUserPopulation::SAFE_BROWSING);

  SetSafeBrowsingState(profile.GetPrefs(),
                       SafeBrowsingState::ENHANCED_PROTECTION);
  population = GetUserPopulationForProfile(&profile);
  EXPECT_EQ(population.user_population(),
            ChromeUserPopulation::ENHANCED_PROTECTION);

  SetSafeBrowsingState(profile.GetPrefs(),
                       SafeBrowsingState::STANDARD_PROTECTION);
  SetExtendedReportingPrefForTests(profile.GetPrefs(), true);
  population = GetUserPopulationForProfile(&profile);
  EXPECT_EQ(population.user_population(),
            ChromeUserPopulation::EXTENDED_REPORTING);
}

TEST(GetUserPopulationForProfileTest, PopulatesMBB) {
  content::BrowserTaskEnvironment task_environment;
  TestingProfile profile;
  profile.GetPrefs()->SetBoolean(
      unified_consent::prefs::kUrlKeyedAnonymizedDataCollectionEnabled, false);
  ChromeUserPopulation population = GetUserPopulationForProfile(&profile);
  EXPECT_FALSE(population.is_mbb_enabled());

  profile.GetPrefs()->SetBoolean(
      unified_consent::prefs::kUrlKeyedAnonymizedDataCollectionEnabled, true);
  population = GetUserPopulationForProfile(&profile);
  EXPECT_TRUE(population.is_mbb_enabled());
}

TEST(GetUserPopulationForProfileTest, PopulatesIncognito) {
  content::BrowserTaskEnvironment task_environment;
  TestingProfile profile;
  ChromeUserPopulation population = GetUserPopulationForProfile(&profile);
  EXPECT_FALSE(population.is_incognito());

  Profile* incognito_profile = profile.GetOffTheRecordProfile(
      Profile::OTRProfileID::CreateUniqueForTesting(),
      /*create_if_needed=*/true);
  population = GetUserPopulationForProfile(incognito_profile);
  EXPECT_TRUE(population.is_incognito());
}

TEST(GetUserPopulationForProfileTest, PopulatesSync) {
  content::BrowserTaskEnvironment task_environment;
  TestingProfile profile;
  syncer::TestSyncService* sync_service = static_cast<syncer::TestSyncService*>(
      SyncServiceFactory::GetInstance()->SetTestingFactoryAndUse(
          &profile, base::BindRepeating(&CreateTestSyncService)));

  {
    sync_service->SetTransportState(
        syncer::SyncService::TransportState::ACTIVE);
    sync_service->SetLocalSyncEnabled(false);
    sync_service->SetActiveDataTypes(syncer::ModelTypeSet::All());

    ChromeUserPopulation population = GetUserPopulationForProfile(&profile);
    EXPECT_TRUE(population.is_history_sync_enabled());
  }

  {
    sync_service->SetTransportState(
        syncer::SyncService::TransportState::DISABLED);
    sync_service->SetLocalSyncEnabled(false);
    sync_service->SetActiveDataTypes(syncer::ModelTypeSet::All());

    ChromeUserPopulation population = GetUserPopulationForProfile(&profile);
    EXPECT_FALSE(population.is_history_sync_enabled());
  }

  {
    sync_service->SetTransportState(
        syncer::SyncService::TransportState::ACTIVE);
    sync_service->SetLocalSyncEnabled(true);
    sync_service->SetActiveDataTypes(syncer::ModelTypeSet::All());

    ChromeUserPopulation population = GetUserPopulationForProfile(&profile);
    EXPECT_FALSE(population.is_history_sync_enabled());
  }

  {
    sync_service->SetTransportState(
        syncer::SyncService::TransportState::ACTIVE);
    sync_service->SetLocalSyncEnabled(false);
    sync_service->SetActiveDataTypes(syncer::ModelTypeSet());

    ChromeUserPopulation population = GetUserPopulationForProfile(&profile);
    EXPECT_FALSE(population.is_history_sync_enabled());
  }
}

#if BUILDFLAG(FULL_SAFE_BROWSING)
TEST(GetUserPopulationForProfileTest, PopulatesAdvancedProtection) {
  content::BrowserTaskEnvironment task_environment;
  TestingProfile profile;

  AdvancedProtectionStatusManagerFactory::GetForProfile(&profile)
      ->SetAdvancedProtectionStatusForTesting(true);
  ChromeUserPopulation population = GetUserPopulationForProfile(&profile);
  EXPECT_TRUE(population.is_under_advanced_protection());

  AdvancedProtectionStatusManagerFactory::GetForProfile(&profile)
      ->SetAdvancedProtectionStatusForTesting(false);
  population = GetUserPopulationForProfile(&profile);
  EXPECT_FALSE(population.is_under_advanced_protection());
}
#endif

TEST(GetUserPopulationForProfileTest, PopulatesUserAgent) {
  content::BrowserTaskEnvironment task_environment;
  TestingProfile profile;

  {
    base::test::ScopedFeatureList feature_list;
    feature_list.InitWithFeatures(
        /* enabled_features = */ {},
        /* disabled_features = */ {kBetterTelemetryAcrossReports});
    ChromeUserPopulation population = GetUserPopulationForProfile(&profile);
    EXPECT_EQ(population.user_agent(), "");
  }
  {
    base::test::ScopedFeatureList feature_list;
    feature_list.InitWithFeatures(
        /* enabled_features = */ {kBetterTelemetryAcrossReports},
        /* disabled_features = */ {});
    std::string user_agent =
        version_info::GetProductNameAndVersionForUserAgent() + "/" +
        version_info::GetOSType();
    ChromeUserPopulation population = GetUserPopulationForProfile(&profile);
    EXPECT_EQ(population.user_agent(), user_agent);
  }
}

}  // namespace safe_browsing
