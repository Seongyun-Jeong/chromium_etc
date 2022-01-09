// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/language/core/common/language_experiments.h"

#include <map>
#include <string>

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"

namespace language {
// Features:
const base::Feature kOverrideTranslateTriggerInIndia{
    "OverrideTranslateTriggerInIndia", base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kExplicitLanguageAsk{"ExplicitLanguageAsk",
                                         base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kAppLanguagePrompt{"AppLanguagePrompt",
                                       base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kForceAppLanguagePrompt{"ForceAppLanguagePrompt",
                                            base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kUseFluentLanguageModel{"UseFluentLanguageModel",
                                            base::FEATURE_ENABLED_BY_DEFAULT};
const base::Feature kNotifySyncOnLanguageDetermined{
    "NotifySyncOnLanguageDetermined", base::FEATURE_ENABLED_BY_DEFAULT};
const base::Feature kDetailedLanguageSettings{
    "DetailedLanguageSettings", base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kDesktopRestructuredLanguageSettings{
    "DesktopRestructuredLanguageSettings", base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kDesktopDetailedLanguageSettings{
    "DesktopDetailedLanguageSettings", base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kTranslateAssistContent{"TranslateAssistContent",
                                            base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kTranslateIntent{"TranslateIntent",
                                     base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kDetectedSourceLanguageOption{
    "DetectedSourceLanguageOption", base::FEATURE_ENABLED_BY_DEFAULT};
const base::Feature kContentLanguagesInLanguagePicker{
    "ContentLanguagesInLanguagePicker", base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kUseULPLanguagesInChrome{"UseULPLanguagesInChrome",
                                             base::FEATURE_ENABLED_BY_DEFAULT};

// Params:
const char kBackoffThresholdKey[] = "backoff_threshold";
const char kOverrideModelKey[] = "override_model";
const char kEnforceRankerKey[] = "enforce_ranker";
const char kOverrideModelGeoValue[] = "geo";
const char kOverrideModelDefaultValue[] = "default";
const char kContentLanguagesDisableObserversParam[] = "disable_observers";

OverrideLanguageModel GetOverrideLanguageModel() {
  std::map<std::string, std::string> params;
  bool should_override_model = base::GetFieldTrialParamsByFeature(
      kOverrideTranslateTriggerInIndia, &params);

  // The model overrides ordering is important as it allows us to
  // have concurrent overrides in experiment without having to partition them
  // explicitly. For example, we may have a FLUENT experiment globally and a
  // GEO experiment in India only.

  if (should_override_model &&
      params[kOverrideModelKey] == kOverrideModelGeoValue) {
    return OverrideLanguageModel::GEO;
  }

  if (base::FeatureList::IsEnabled(kUseFluentLanguageModel)) {
    return OverrideLanguageModel::FLUENT;
  }

  return OverrideLanguageModel::DEFAULT;
}

bool ShouldForceTriggerTranslateOnEnglishPages(int force_trigger_count) {
  if (!base::FeatureList::IsEnabled(kOverrideTranslateTriggerInIndia))
    return false;

  return !IsForceTriggerBackoffThresholdReached(force_trigger_count);
}

bool ShouldPreventRankerEnforcementInIndia(int force_trigger_count) {
  std::map<std::string, std::string> params;
  return base::FeatureList::IsEnabled(kOverrideTranslateTriggerInIndia) &&
         !IsForceTriggerBackoffThresholdReached(force_trigger_count) &&
         base::GetFieldTrialParamsByFeature(kOverrideTranslateTriggerInIndia,
                                            &params) &&
         params[kEnforceRankerKey] == "false";
}

bool IsForceTriggerBackoffThresholdReached(int force_trigger_count) {
  int threshold;
  std::map<std::string, std::string> params;
  if (!base::GetFieldTrialParamsByFeature(kOverrideTranslateTriggerInIndia,
                                          &params) ||
      !base::StringToInt(params[kBackoffThresholdKey], &threshold)) {
    return false;
  }

  return force_trigger_count >= threshold;
}

}  // namespace language
