// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/core/browser/safe_browsing_metrics_collector.h"
#include <memory>
#include <utility>

#include "base/base64.h"
#include "base/json/values_util.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/task_environment.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace safe_browsing {

using EventType = SafeBrowsingMetricsCollector::EventType;
using UserState = SafeBrowsingMetricsCollector::UserState;

class SafeBrowsingMetricsCollectorTest : public ::testing::Test {
 public:
  SafeBrowsingMetricsCollectorTest() = default;

  void SetUp() override {
    RegisterPrefs();
    metrics_collector_ =
        std::make_unique<SafeBrowsingMetricsCollector>(&pref_service_);
  }

  void TearDown() override { metrics_collector_->Shutdown(); }

 protected:
  void SetSafeBrowsingMetricsLastLogTime(base::Time time) {
    pref_service_.SetInt64(prefs::kSafeBrowsingMetricsLastLogTime,
                           time.ToDeltaSinceWindowsEpoch().InSeconds());
  }

  const base::Value* GetTsFromUserStateAndEventType(UserState state,
                                                    EventType event_type) {
    const base::Value* state_dict =
        pref_service_.GetDictionary(prefs::kSafeBrowsingEventTimestamps);
    const base::Value* event_dict =
        state_dict->FindDictKey(base::NumberToString(static_cast<int>(state)));
    DCHECK(event_dict);
    DCHECK(event_dict->is_dict());
    const base::Value* timestamps = event_dict->FindListKey(
        base::NumberToString(static_cast<int>(event_type)));
    DCHECK(timestamps);
    DCHECK(timestamps->is_list());
    return timestamps;
  }

  bool IsSortedInChronologicalOrder(const base::Value* ts) {
    return std::is_sorted(ts->GetList().begin(), ts->GetList().end(),
                          [](const base::Value& ts_a, const base::Value& ts_b) {
                            return base::ValueToInt64(ts_a).value_or(0) <
                                   base::ValueToInt64(ts_b).value_or(0);
                          });
  }

  void FastForwardAndAddEvent(base::TimeDelta time_delta,
                              EventType event_type) {
    task_environment_.FastForwardBy(time_delta);
    metrics_collector_->AddSafeBrowsingEventToPref(event_type);
  }

  std::unique_ptr<SafeBrowsingMetricsCollector> metrics_collector_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  TestingPrefServiceSimple pref_service_;

 private:
  void RegisterPrefs() {
    pref_service_.registry()->RegisterInt64Pref(
        prefs::kSafeBrowsingMetricsLastLogTime, 0);
    pref_service_.registry()->RegisterBooleanPref(prefs::kSafeBrowsingEnabled,
                                                  true);
    pref_service_.registry()->RegisterBooleanPref(prefs::kSafeBrowsingEnhanced,
                                                  false);
    pref_service_.registry()->RegisterBooleanPref(
        prefs::kSafeBrowsingScoutReportingEnabled, false);
    pref_service_.registry()->RegisterDictionaryPref(
        prefs::kSafeBrowsingEventTimestamps);
    pref_service_.registry()->RegisterBooleanPref(
        prefs::kEnhancedProtectionEnabledViaTailoredSecurity, false);
  }
};

TEST_F(SafeBrowsingMetricsCollectorTest,
       StartLogging_LastLoggingIntervalLongerThanScheduleInterval) {
  base::HistogramTester histograms;
  SetSafeBrowsingMetricsLastLogTime(base::Time::Now() - base::Hours(25));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  SetExtendedReportingPrefForTests(&pref_service_, true);
  metrics_collector_->StartLogging();
  // Should log immediately.
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.SafeBrowsingState",
                               /* sample */ 1, /* expected_count */ 1);
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.Extended",
                               /* sample */ 1, /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Pref.Daily.SafeBrowsingModeManaged",
      /* sample */ 0, /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Pref.Daily.SafeBrowsingModeManaged",
      /* sample */ 1, /* expected_count */ 0);
  task_environment_.FastForwardBy(base::Hours(23));
  // Shouldn't log new data before the scheduled time.
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.SafeBrowsingState",
                               /* sample */ 1, /* expected_count */ 1);
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.Extended",
                               /* sample */ 1, /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Pref.Daily.SafeBrowsingModeManaged",
      /* sample */ 0, /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Pref.Daily.SafeBrowsingModeManaged",
      /* sample */ 1, /* expected_count */ 0);
  task_environment_.FastForwardBy(base::Hours(1));
  // Should log when the scheduled time arrives.
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.SafeBrowsingState",
                               /* sample */ 1, /* expected_count */ 2);
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.Extended",
                               /* sample */ 1, /* expected_count */ 2);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Pref.Daily.SafeBrowsingModeManaged",
      /* sample */ 0, /* expected_count */ 2);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Pref.Daily.SafeBrowsingModeManaged",
      /* sample */ 1, /* expected_count */ 0);
  task_environment_.FastForwardBy(base::Hours(24));
  // Should log when the scheduled time arrives.
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.SafeBrowsingState",
                               /* sample */ 1, /* expected_count */ 3);
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.Extended",
                               /* sample */ 1, /* expected_count */ 3);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Pref.Daily.SafeBrowsingModeManaged",
      /* sample */ 0, /* expected_count */ 3);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Pref.Daily.SafeBrowsingModeManaged",
      /* sample */ 1, /* expected_count */ 0);

  // Should now detect SafeBrowsing as Managed.
  pref_service_.SetManagedPref(prefs::kSafeBrowsingEnabled,
                               std::make_unique<base::Value>(true));
  task_environment_.FastForwardBy(base::Hours(24));
  histograms.ExpectBucketCount(
      "SafeBrowsing.Pref.Daily.SafeBrowsingModeManaged",
      /* sample */ 0, /* expected_count */ 3);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Pref.Daily.SafeBrowsingModeManaged",
      /* sample */ 1, /* expected_count */ 1);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       StartLogging_LastLoggingIntervalShorterThanScheduleInterval) {
  base::HistogramTester histograms;
  SetSafeBrowsingMetricsLastLogTime(base::Time::Now() - base::Hours(1));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  metrics_collector_->StartLogging();
  // Should not log immediately because the last logging interval is shorter
  // than the interval.
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.SafeBrowsingState",
                               /* sample */ 1, /* expected_count */ 0);
  task_environment_.FastForwardBy(base::Hours(23));
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.SafeBrowsingState",
                               /* sample */ 1, /* expected_count */ 1);
  task_environment_.FastForwardBy(base::Hours(24));
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.SafeBrowsingState",
                               /* sample */ 1, /* expected_count */ 2);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       StartLogging_PrefChangeBetweenLogging) {
  base::HistogramTester histograms;
  SetSafeBrowsingMetricsLastLogTime(base::Time::Now() - base::Hours(25));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  metrics_collector_->StartLogging();
  histograms.ExpectTotalCount("SafeBrowsing.Pref.Daily.SafeBrowsingState",
                              /* expected_count */ 1);
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.SafeBrowsingState",
                               /* sample */ 1, /* expected_count */ 1);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::NO_SAFE_BROWSING);
  task_environment_.FastForwardBy(base::Hours(24));
  histograms.ExpectTotalCount("SafeBrowsing.Pref.Daily.SafeBrowsingState",
                              /* expected_count */ 2);
  histograms.ExpectBucketCount("SafeBrowsing.Pref.Daily.SafeBrowsingState",
                               /* sample */ 0, /* expected_count */ 1);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       AddSafeBrowsingEventToPref_OldestTsRemoved) {
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  metrics_collector_->AddSafeBrowsingEventToPref(
      EventType::DATABASE_INTERSTITIAL_BYPASS);

  task_environment_.FastForwardBy(base::Days(1));
  for (int i = 0; i < 29; i++) {
    metrics_collector_->AddSafeBrowsingEventToPref(
        EventType::DATABASE_INTERSTITIAL_BYPASS);
  }

  const base::Value* timestamps = GetTsFromUserStateAndEventType(
      UserState::kEnhancedProtection, EventType::DATABASE_INTERSTITIAL_BYPASS);
  EXPECT_EQ(30u, timestamps->GetList().size());
  EXPECT_TRUE(IsSortedInChronologicalOrder(timestamps));

  task_environment_.FastForwardBy(base::Days(1));
  metrics_collector_->AddSafeBrowsingEventToPref(
      EventType::DATABASE_INTERSTITIAL_BYPASS);

  EXPECT_EQ(30u, timestamps->GetList().size());
  EXPECT_TRUE(IsSortedInChronologicalOrder(timestamps));
  // The oldest timestamp should be removed.
  EXPECT_EQ(timestamps->GetList()[0], timestamps->GetList()[1]);
  // The newest timestamp should be added as the last element.
  EXPECT_NE(timestamps->GetList()[28], timestamps->GetList()[29]);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       AddSafeBrowsingEventToPref_SafeBrowsingManaged) {
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  metrics_collector_->AddSafeBrowsingEventToPref(
      EventType::DATABASE_INTERSTITIAL_BYPASS);
  pref_service_.SetManagedPref(prefs::kSafeBrowsingEnabled,
                               std::make_unique<base::Value>(true));
  metrics_collector_->AddSafeBrowsingEventToPref(
      EventType::DATABASE_INTERSTITIAL_BYPASS);
  metrics_collector_->AddSafeBrowsingEventToPref(
      EventType::DATABASE_INTERSTITIAL_BYPASS);

  const base::Value* enhanced_timestamps = GetTsFromUserStateAndEventType(
      UserState::kEnhancedProtection, EventType::DATABASE_INTERSTITIAL_BYPASS);
  EXPECT_EQ(1u, enhanced_timestamps->GetList().size());
  const base::Value* managed_timestamps = GetTsFromUserStateAndEventType(
      UserState::kManaged, EventType::DATABASE_INTERSTITIAL_BYPASS);
  EXPECT_EQ(2u, managed_timestamps->GetList().size());
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       LogEnhancedProtectionDisabledMetrics_GetLastBypassEventType) {
  base::HistogramTester histograms;
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);

  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);
  FastForwardAndAddEvent(base::Hours(1), EventType::CSD_INTERSTITIAL_BYPASS);
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);
  FastForwardAndAddEvent(base::Hours(1), EventType::CSD_INTERSTITIAL_BYPASS);

  task_environment_.FastForwardBy(base::Hours(1));
  // Changing enhanced protection to standard protection should log the metric.
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  histograms.ExpectUniqueSample("SafeBrowsing.EsbDisabled.LastBypassEventType",
                                /* sample */ EventType::CSD_INTERSTITIAL_BYPASS,
                                /* expected_count */ 1);
  histograms.ExpectUniqueTimeSample(
      "SafeBrowsing.EsbDisabled.LastBypassEventInterval.CsdInterstitialBypass",
      /* sample */ base::Hours(1),
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.EsbDisabled.BypassCountLast28Days."
      "DatabaseInterstitialBypass",
      /* sample */ 2,
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.EsbDisabled.BypassCountLast28Days."
      "CsdInterstitialBypass",
      /* sample */ 2,
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.EsbDisabled.BypassCountLast28Days."
      "RealTimeInterstitialBypass",
      /* sample */ 0,
      /* expected_count */ 1);

  // Changing standard protection to enhanced protection shouldn't log the
  // metric.
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  histograms.ExpectUniqueSample("SafeBrowsing.EsbDisabled.LastBypassEventType",
                                /* sample */ EventType::CSD_INTERSTITIAL_BYPASS,
                                /* expected_count */ 1);

  // Changing enhanced protection to no protection should log the metric.
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::REAL_TIME_INTERSTITIAL_BYPASS);
  task_environment_.FastForwardBy(base::Days(1));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::NO_SAFE_BROWSING);
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastBypassEventType",
                              /* expected_count */ 2);
  histograms.ExpectBucketCount(
      "SafeBrowsing.EsbDisabled.LastBypassEventType",
      /* sample */ EventType::REAL_TIME_INTERSTITIAL_BYPASS,
      /* expected_count */ 1);
  histograms.ExpectTimeBucketCount(
      "SafeBrowsing.EsbDisabled.LastBypassEventInterval."
      "RealTimeInterstitialBypass",
      /* sample */ base::Days(1),
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.EsbDisabled.BypassCountLast28Days."
      "DatabaseInterstitialBypass",
      /* sample */ 2,
      /* expected_count */ 2);
  histograms.ExpectBucketCount(
      "SafeBrowsing.EsbDisabled.BypassCountLast28Days."
      "CsdInterstitialBypass",
      /* sample */ 2,
      /* expected_count */ 2);
  histograms.ExpectBucketCount(
      "SafeBrowsing.EsbDisabled.BypassCountLast28Days."
      "RealTimeInterstitialBypass",
      /* sample */ 1,
      /* expected_count */ 1);

  // Changing no protection to enhanced protection shouldn't log the metric.
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastBypassEventType",
                              /* expected_count */ 2);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       LogEnhancedProtectionDisabledMetrics_GetLastSecuritySensitiveEventType) {
  base::HistogramTester histograms;
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);

  FastForwardAndAddEvent(
      base::Hours(1), EventType::SECURITY_SENSITIVE_SAFE_BROWSING_INTERSTITIAL);

  task_environment_.FastForwardBy(base::Hours(1));
  // Changing enhanced protection to standard protection should log the metric.
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  histograms.ExpectUniqueSample(
      "SafeBrowsing.EsbDisabled.LastSecuritySensitiveEventType",
      /* sample */ EventType::SECURITY_SENSITIVE_SAFE_BROWSING_INTERSTITIAL,
      /* expected_count */ 1);
  histograms.ExpectUniqueTimeSample(
      "SafeBrowsing.EsbDisabled.LastSecuritySensitiveEventInterval."
      "SafeBrowsingInterstitial",
      /* sample */ base::Hours(1),
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.EsbDisabled.SecuritySensitiveCountLast28Days."
      "SafeBrowsingInterstitial",
      /* sample */ 1,
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.EsbDisabled.SecuritySensitiveCountLast28Days."
      "SSLInterstitial",
      /* sample */ 0,
      /* expected_count */ 1);

  // Changing standard protection to enhanced protection shouldn't log the
  // metric.
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  histograms.ExpectUniqueSample(
      "SafeBrowsing.EsbDisabled.LastSecuritySensitiveEventType",
      /* sample */ EventType::SECURITY_SENSITIVE_SAFE_BROWSING_INTERSTITIAL,
      /* expected_count */ 1);

  // Changing enhanced protection to no protection should log the metric.
  FastForwardAndAddEvent(
      base::Hours(1), EventType::SECURITY_SENSITIVE_SAFE_BROWSING_INTERSTITIAL);
  task_environment_.FastForwardBy(base::Days(1));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::NO_SAFE_BROWSING);
  histograms.ExpectTotalCount(
      "SafeBrowsing.EsbDisabled.LastSecuritySensitiveEventType",
      /* expected_count */ 2);
  histograms.ExpectTimeBucketCount(
      "SafeBrowsing.EsbDisabled.LastSecuritySensitiveEventInterval."
      "SafeBrowsingInterstitial",
      /* sample */ base::Days(1),
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.EsbDisabled.SecuritySensitiveCountLast28Days."
      "SafeBrowsingInterstitial",
      /* sample */ 2,
      /* expected_count */ 1);

  // Changing no protection to enhanced protection shouldn't log the metric.
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  histograms.ExpectTotalCount(
      "SafeBrowsing.EsbDisabled.LastSecuritySensitiveEventType",
      /* expected_count */ 2);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       LogEnhancedProtectionDisabledMetrics_GetLastEnabledInterval) {
  base::HistogramTester histograms;
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);

  task_environment_.FastForwardBy(base::Hours(1));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  histograms.ExpectBucketCount("SafeBrowsing.EsbDisabled.LastEnabledInterval",
                               /* sample */ 0,
                               /* expected count */ 1);

  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastEnabledInterval",
                              /* expected_count */ 1);

  task_environment_.FastForwardBy(base::Days(1));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::NO_SAFE_BROWSING);
  histograms.ExpectBucketCount("SafeBrowsing.EsbDisabled.LastEnabledInterval",
                               /* sample */ 1,
                               /* expected count */ 1);
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastEnabledInterval",
                              /* expected_count */ 2);

  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastEnabledInterval",
                              /* expected_count */ 2);

  task_environment_.FastForwardBy(base::Days(7));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  histograms.ExpectBucketCount("SafeBrowsing.EsbDisabled.LastEnabledInterval",
                               /* sample */ 7,
                               /* expected count */ 1);
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastEnabledInterval",
                              /* expected_count */ 3);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       LogEnhancedProtectionDisabledMetrics_TimesDisabledLast28Days_Suffixes) {
  base::HistogramTester histograms;

  auto validate_total_counts =
      [](base::HistogramTester* histogram_tester, int never_enabled_count,
         int short_enabled_count, int medium_enabled_count,
         int long_enabled_count) {
        histogram_tester->ExpectTotalCount(
            "SafeBrowsing.EsbDisabled.TimesDisabledLast28Days.NeverEnabled",
            never_enabled_count);
        histogram_tester->ExpectTotalCount(
            "SafeBrowsing.EsbDisabled.TimesDisabledLast28Days.ShortEnabled",
            short_enabled_count);
        histogram_tester->ExpectTotalCount(
            "SafeBrowsing.EsbDisabled.TimesDisabledLast28Days.MediumEnabled",
            medium_enabled_count);
        histogram_tester->ExpectTotalCount(
            "SafeBrowsing.EsbDisabled.TimesDisabledLast28Days.LongEnabled",
            long_enabled_count);
      };

  pref_service_.SetManagedPref(prefs::kSafeBrowsingEnabled,
                               std::make_unique<base::Value>(true));
  pref_service_.RemoveManagedPref(prefs::kSafeBrowsingEnabled);
  validate_total_counts(&histograms, 0, 0, 0, 0);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  validate_total_counts(&histograms, 1, 0, 0, 0);

  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  validate_total_counts(&histograms, 1, 0, 0, 0);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  validate_total_counts(&histograms, 1, 1, 0, 0);

  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  task_environment_.FastForwardBy(base::Minutes(59));
  validate_total_counts(&histograms, 1, 1, 0, 0);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  validate_total_counts(&histograms, 1, 2, 0, 0);

  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  task_environment_.FastForwardBy(base::Hours(1));
  validate_total_counts(&histograms, 1, 2, 0, 0);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  validate_total_counts(&histograms, 1, 2, 1, 0);

  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  task_environment_.FastForwardBy(base::Hours(23));
  validate_total_counts(&histograms, 1, 2, 1, 0);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  validate_total_counts(&histograms, 1, 2, 2, 0);

  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  task_environment_.FastForwardBy(base::Days(1));
  validate_total_counts(&histograms, 1, 2, 2, 0);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  validate_total_counts(&histograms, 1, 2, 2, 1);

  EXPECT_THAT(
      histograms.GetAllSamples(
          "SafeBrowsing.EsbDisabled.TimesDisabledLast28Days.NeverEnabled"),
      testing::ElementsAre(base::Bucket(1, 1)));
  EXPECT_THAT(
      histograms.GetAllSamples(
          "SafeBrowsing.EsbDisabled.TimesDisabledLast28Days.ShortEnabled"),
      testing::ElementsAre(base::Bucket(2, 1), base::Bucket(3, 1)));
  EXPECT_THAT(
      histograms.GetAllSamples(
          "SafeBrowsing.EsbDisabled.TimesDisabledLast28Days.MediumEnabled"),
      testing::ElementsAre(base::Bucket(4, 1), base::Bucket(5, 1)));
  EXPECT_THAT(
      histograms.GetAllSamples(
          "SafeBrowsing.EsbDisabled.TimesDisabledLast28Days.LongEnabled"),
      testing::ElementsAre(base::Bucket(6, 1)));
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       LogEnhancedProtectionDisabledMetrics_TimesDisabledLast28Days_Resets) {
  base::HistogramTester histograms;

  for (int i = 0; i < 3; i++) {
    SetSafeBrowsingState(&pref_service_,
                         SafeBrowsingState::ENHANCED_PROTECTION);
    SetSafeBrowsingState(&pref_service_,
                         SafeBrowsingState::STANDARD_PROTECTION);
  }
  task_environment_.FastForwardBy(base::Days(27));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  EXPECT_THAT(
      histograms.GetAllSamples(
          "SafeBrowsing.EsbDisabled.TimesDisabledLast28Days.ShortEnabled"),
      testing::ElementsAre(base::Bucket(1, 1), base::Bucket(2, 1),
                           base::Bucket(3, 1), base::Bucket(4, 1)));

  // When we increase one more day, the first 3 disables get out of the range
  // of the past 28 days, so now we log that there have only been 2 disables
  // (the one yesterday and the one we're doing now)
  task_environment_.FastForwardBy(base::Days(1));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  EXPECT_THAT(
      histograms.GetAllSamples(
          "SafeBrowsing.EsbDisabled.TimesDisabledLast28Days.ShortEnabled"),
      testing::ElementsAre(base::Bucket(1, 1), base::Bucket(2, 2),
                           base::Bucket(3, 1), base::Bucket(4, 1)));

  // Increasing by 28 days removes all past disables from the range, so now we
  // log that there has only been 1 disable in the past 28 days (the one we're
  // doing that is causing this log)
  task_environment_.FastForwardBy(base::Days(28));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  EXPECT_THAT(
      histograms.GetAllSamples(
          "SafeBrowsing.EsbDisabled.TimesDisabledLast28Days.ShortEnabled"),
      testing::ElementsAre(base::Bucket(1, 2), base::Bucket(2, 2),
                           base::Bucket(3, 1), base::Bucket(4, 1)));
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       LogEnhancedProtectionDisabledMetrics_NotLoggedIfNoEvent) {
  base::HistogramTester histograms;
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastBypassEventType",
                              /* expected_count */ 0);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       LogEnhancedProtectionDisabledMetrics_NotLoggedIfHitQuotaLimit) {
  base::HistogramTester histograms;
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);

  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastBypassEventType",
                              /* expected_count */ 1);

  task_environment_.FastForwardBy(base::Days(1));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastBypassEventType",
                              /* expected_count */ 2);

  task_environment_.FastForwardBy(base::Days(1));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastBypassEventType",
                              /* expected_count */ 3);

  task_environment_.FastForwardBy(base::Days(1));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  // The metric is not logged because it is already logged 3 times in a week.
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastBypassEventType",
                              /* expected_count */ 3);

  task_environment_.FastForwardBy(base::Days(7));
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  // The metric is logged again because the oldest entry is more than 7 days
  // ago.
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastBypassEventType",
                              /* expected_count */ 4);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       LogEnhancedProtectionDisabledMetrics_NotLoggedIfManaged) {
  base::HistogramTester histograms;
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);

  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);

  pref_service_.SetManagedPref(prefs::kSafeBrowsingEnabled,
                               std::make_unique<base::Value>(false));
  histograms.ExpectTotalCount("SafeBrowsing.EsbDisabled.LastBypassEventType",
                              /* expected_count */ 0);
}

TEST_F(SafeBrowsingMetricsCollectorTest, LogDailyEventMetrics_LoggedDaily) {
  base::HistogramTester histograms;
  SetSafeBrowsingMetricsLastLogTime(base::Time::Now());
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  metrics_collector_->StartLogging();
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);
  FastForwardAndAddEvent(base::Hours(1), EventType::CSD_INTERSTITIAL_BYPASS);
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::REAL_TIME_INTERSTITIAL_BYPASS);
  FastForwardAndAddEvent(
      base::Hours(1), EventType::SECURITY_SENSITIVE_SAFE_BROWSING_INTERSTITIAL);
  FastForwardAndAddEvent(
      base::Hours(1), EventType::SECURITY_SENSITIVE_SAFE_BROWSING_INTERSTITIAL);

  task_environment_.FastForwardBy(base::Days(1));
  histograms.ExpectTotalCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection.AllEvents",
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection.AllEvents",
      /* sample */ 4,
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection."
      "DatabaseInterstitialBypass",
      /* sample */ 2,
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection."
      "CsdInterstitialBypass",
      /* sample */ 1,
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection."
      "RealTimeInterstitialBypass",
      /* sample */ 1,
      /* expected_count */ 1);
  histograms.ExpectTotalCount(
      "SafeBrowsing.Daily.SecuritySensitiveCountLast28Days.EnhancedProtection."
      "AllEvents",
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.SecuritySensitiveCountLast28Days.EnhancedProtection."
      "AllEvents",
      /* sample */ 2,
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.SecuritySensitiveCountLast28Days.EnhancedProtection."
      "SafeBrowsingInterstitial",
      /* sample */ 2,
      /* expected_count */ 1);

  FastForwardAndAddEvent(base::Hours(1), EventType::CSD_INTERSTITIAL_BYPASS);
  task_environment_.FastForwardBy(base::Days(1));
  histograms.ExpectTotalCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection.AllEvents",
      /* expected_count */ 2);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection.AllEvents",
      /* sample */ 5,
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection."
      "CsdInterstitialBypass",
      /* sample */ 2,
      /* expected_count */ 1);

  task_environment_.FastForwardBy(base::Days(1));
  histograms.ExpectTotalCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection.AllEvents",
      /* expected_count */ 3);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection.AllEvents",
      /* sample */ 5,
      /* expected_count */ 2);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection."
      "CsdInterstitialBypass",
      /* sample */ 2,
      /* expected_count */ 2);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       LogDailyEventMetrics_DoesNotCountOldEvent) {
  base::HistogramTester histograms;
  SetSafeBrowsingMetricsLastLogTime(base::Time::Now());
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  metrics_collector_->StartLogging();
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);

  task_environment_.FastForwardBy(base::Days(1));
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection.AllEvents",
      /* sample */ 0,
      /* expected_count */ 0);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection.AllEvents",
      /* sample */ 1,
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection."
      "DatabaseInterstitialBypass",
      /* sample */ 0,
      /* expected_count */ 0);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection."
      "DatabaseInterstitialBypass",
      /* sample */ 1,
      /* expected_count */ 1);

  task_environment_.FastForwardBy(base::Days(28));
  // The event is older than 28 days, so it shouldn't be counted.
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection.AllEvents",
      /* sample */ 0,
      /* expected_count */ 1);
  histograms.ExpectBucketCount(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection."
      "DatabaseInterstitialBypass",
      /* sample */ 0,
      /* expected_count */ 1);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       LogDailyEventMetrics_SwitchBetweenDifferentUserState) {
  base::HistogramTester histograms;
  SetSafeBrowsingMetricsLastLogTime(base::Time::Now());
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  metrics_collector_->StartLogging();
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);

  task_environment_.FastForwardBy(base::Days(1));
  histograms.ExpectUniqueSample(
      "SafeBrowsing.Daily.BypassCountLast28Days.EnhancedProtection.AllEvents",
      /* sample */ 1,
      /* expected_count */ 1);

  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);

  task_environment_.FastForwardBy(base::Days(1));
  histograms.ExpectUniqueSample(
      "SafeBrowsing.Daily.BypassCountLast28Days.StandardProtection.AllEvents",
      /* sample */ 2,
      /* expected_count */ 1);
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       RemoveOldEventsFromPref_OldEventsRemoved) {
  base::HistogramTester histograms;
  SetSafeBrowsingMetricsLastLogTime(base::Time::Now());
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  metrics_collector_->StartLogging();
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);
  FastForwardAndAddEvent(base::Days(1), EventType::CSD_INTERSTITIAL_BYPASS);

  task_environment_.FastForwardBy(base::Days(30));
  const base::Value* db_timestamps = GetTsFromUserStateAndEventType(
      UserState::kStandardProtection, EventType::DATABASE_INTERSTITIAL_BYPASS);
  // The event is removed from pref because it was logged more than 30 days.
  EXPECT_EQ(0u, db_timestamps->GetList().size());
  const base::Value* csd_timestamps = GetTsFromUserStateAndEventType(
      UserState::kStandardProtection, EventType::CSD_INTERSTITIAL_BYPASS);
  // The CSD event is still in pref because it was logged less than 30 days.
  EXPECT_EQ(1u, csd_timestamps->GetList().size());

  task_environment_.FastForwardBy(base::Days(1));
  // The CSD event is also removed because it was logged more than 30 days now.
  EXPECT_EQ(0u, csd_timestamps->GetList().size());

  histograms.ExpectUniqueSample("SafeBrowsing.MetricsCollector.IsPrefValid",
                                /* sample */ 1,
                                /* expected_count */ 32);
}

TEST_F(SafeBrowsingMetricsCollectorTest, GetUserState) {
  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::ENHANCED_PROTECTION);
  EXPECT_EQ(UserState::kEnhancedProtection, metrics_collector_->GetUserState());

  SetSafeBrowsingState(&pref_service_, SafeBrowsingState::STANDARD_PROTECTION);
  EXPECT_EQ(UserState::kStandardProtection, metrics_collector_->GetUserState());

  pref_service_.SetManagedPref(prefs::kSafeBrowsingEnabled,
                               std::make_unique<base::Value>(true));
  EXPECT_EQ(UserState::kManaged, metrics_collector_->GetUserState());

  pref_service_.RemoveManagedPref(prefs::kSafeBrowsingEnabled);
  pref_service_.SetManagedPref(prefs::kSafeBrowsingEnhanced,
                               std::make_unique<base::Value>(true));
  EXPECT_EQ(UserState::kManaged, metrics_collector_->GetUserState());
}

TEST_F(SafeBrowsingMetricsCollectorTest, GetLatestEventTimestamp) {
  EXPECT_EQ(absl::nullopt, metrics_collector_->GetLatestEventTimestamp(
                               EventType::DATABASE_INTERSTITIAL_BYPASS));
  // Timestamps are rounded to second when stored in prefs.
  base::Time rounded_time = base::Time::FromDeltaSinceWindowsEpoch(
      base::Seconds(base::Time::Now().ToDeltaSinceWindowsEpoch().InSeconds()));
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::DATABASE_INTERSTITIAL_BYPASS);
  EXPECT_EQ(rounded_time + base::Hours(1),
            metrics_collector_->GetLatestEventTimestamp(
                EventType::DATABASE_INTERSTITIAL_BYPASS));
  task_environment_.FastForwardBy(base::Days(1));
  EXPECT_EQ(rounded_time + base::Hours(1),
            metrics_collector_->GetLatestEventTimestamp(
                EventType::DATABASE_INTERSTITIAL_BYPASS));
}

TEST_F(SafeBrowsingMetricsCollectorTest,
       GetLatestSecuritySensitiveEventTimestamp) {
  EXPECT_EQ(absl::nullopt,
            metrics_collector_->GetLatestSecuritySensitiveEventTimestamp());
  // Timestamps are rounded to second when stored in prefs.
  base::Time rounded_time = base::Time::FromDeltaSinceWindowsEpoch(
      base::Seconds(base::Time::Now().ToDeltaSinceWindowsEpoch().InSeconds()));

  // Add one security sensitive event.
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::SECURITY_SENSITIVE_DOWNLOAD);
  EXPECT_EQ(rounded_time + base::Hours(1),
            metrics_collector_->GetLatestSecuritySensitiveEventTimestamp());

  // Add another security sensitive event.
  FastForwardAndAddEvent(base::Hours(1),
                         EventType::SECURITY_SENSITIVE_PASSWORD_PROTECTION);
  EXPECT_EQ(rounded_time + base::Hours(2),
            metrics_collector_->GetLatestSecuritySensitiveEventTimestamp());

  task_environment_.FastForwardBy(base::Days(1));
  EXPECT_EQ(rounded_time + base::Hours(2),
            metrics_collector_->GetLatestSecuritySensitiveEventTimestamp());
}

}  // namespace safe_browsing
