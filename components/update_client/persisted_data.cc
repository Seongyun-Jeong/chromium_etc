// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/persisted_data.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/callback.h"
#include "base/guid.h"
#include "base/strings/stringprintf.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/values.h"
#include "base/version.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/update_client/activity_data_service.h"

const char kPersistedDataPreference[] = "updateclientdata";

namespace update_client {

PersistedData::PersistedData(PrefService* pref_service,
                             ActivityDataService* activity_data_service)
    : pref_service_(pref_service),
      activity_data_service_(activity_data_service) {}

PersistedData::~PersistedData() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

const base::Value* PersistedData::GetAppKey(const std::string& id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!pref_service_)
    return nullptr;
  const base::Value* dict = pref_service_->Get(kPersistedDataPreference);
  if (!dict || dict->type() != base::Value::Type::DICTIONARY)
    return nullptr;
  const base::Value* apps = dict->FindDictKey("apps");
  if (!apps)
    return nullptr;
  return apps->FindDictKey(id);
}

int PersistedData::GetInt(const std::string& id,
                          const std::string& key,
                          int fallback) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::Value* app_key = GetAppKey(id);
  if (!app_key)
    return fallback;
  return app_key->FindIntKey(key).value_or(fallback);
}

std::string PersistedData::GetString(const std::string& id,
                                     const std::string& key) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::Value* app_key = GetAppKey(id);
  if (!app_key)
    return {};
  const std::string* value = app_key->FindStringKey(key);
  if (!value)
    return {};
  return *value;
}

int PersistedData::GetDateLastRollCall(const std::string& id) const {
  return GetInt(id, "dlrc", kDateUnknown);
}

int PersistedData::GetDateLastActive(const std::string& id) const {
  return GetInt(id, "dla", kDateUnknown);
}

std::string PersistedData::GetPingFreshness(const std::string& id) const {
  std::string result = GetString(id, "pf");
  return !result.empty() ? base::StringPrintf("{%s}", result.c_str()) : result;
}

std::string PersistedData::GetCohort(const std::string& id) const {
  return GetString(id, "cohort");
}

std::string PersistedData::GetCohortName(const std::string& id) const {
  return GetString(id, "cohortname");
}

std::string PersistedData::GetCohortHint(const std::string& id) const {
  return GetString(id, "cohorthint");
}

base::Value* PersistedData::GetOrCreateAppKey(const std::string& id,
                                              base::Value* root) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::Value* apps = root->FindDictKey("apps");
  if (!apps)
    apps = root->SetKey("apps", base::Value(base::Value::Type::DICTIONARY));
  base::Value* app = apps->FindDictKey(id);
  if (!app)
    app = apps->SetKey(id, base::Value(base::Value::Type::DICTIONARY));
  return app;
}

void PersistedData::SetDateLastDataHelper(
    const std::vector<std::string>& ids,
    int datenum,
    base::OnceClosure callback,
    const std::set<std::string>& active_ids) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DictionaryPrefUpdate update(pref_service_, kPersistedDataPreference);
  for (const auto& id : ids) {
    base::Value* app_key = GetOrCreateAppKey(id, update.Get());
    app_key->SetIntKey("dlrc", datenum);
    app_key->SetStringKey("pf", base::GenerateGUID());
    if (active_ids.find(id) != active_ids.end()) {
      app_key->SetIntKey("dla", datenum);
    }
  }
  std::move(callback).Run();
}

void PersistedData::SetDateLastData(const std::vector<std::string>& ids,
                                    int datenum,
                                    base::OnceClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!pref_service_ || datenum < 0) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                     std::move(callback));
    return;
  }
  if (!activity_data_service_) {
    SetDateLastDataHelper(ids, datenum, std::move(callback), {});
    return;
  }
  activity_data_service_->GetAndClearActiveBits(
      ids, base::BindOnce(&PersistedData::SetDateLastDataHelper,
                          base::Unretained(this), ids, datenum,
                          std::move(callback)));
}

void PersistedData::SetString(const std::string& id,
                              const std::string& key,
                              const std::string& value) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!pref_service_)
    return;
  DictionaryPrefUpdate update(pref_service_, kPersistedDataPreference);
  GetOrCreateAppKey(id, update.Get())->SetStringKey(key, value);
}

void PersistedData::SetCohort(const std::string& id,
                              const std::string& cohort) {
  SetString(id, "cohort", cohort);
}

void PersistedData::SetCohortName(const std::string& id,
                                  const std::string& cohort_name) {
  SetString(id, "cohortname", cohort_name);
}

void PersistedData::SetCohortHint(const std::string& id,
                                  const std::string& cohort_hint) {
  SetString(id, "cohorthint", cohort_hint);
}

void PersistedData::GetActiveBits(
    const std::vector<std::string>& ids,
    base::OnceCallback<void(const std::set<std::string>&)> callback) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!activity_data_service_) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), std::set<std::string>{}));
    return;
  }
  activity_data_service_->GetActiveBits(ids, std::move(callback));
}

int PersistedData::GetDaysSinceLastRollCall(const std::string& id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return activity_data_service_
             ? activity_data_service_->GetDaysSinceLastRollCall(id)
             : kDaysUnknown;
}

int PersistedData::GetDaysSinceLastActive(const std::string& id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return activity_data_service_
             ? activity_data_service_->GetDaysSinceLastActive(id)
             : kDaysUnknown;
}

base::Version PersistedData::GetProductVersion(const std::string& id) const {
  return base::Version(GetString(id, "pv"));
}

void PersistedData::SetProductVersion(const std::string& id,
                                      const base::Version& pv) {
  DCHECK(pv.IsValid());
  SetString(id, "pv", pv.GetString());
}

std::string PersistedData::GetFingerprint(const std::string& id) const {
  return GetString(id, "fp");
}

void PersistedData::SetFingerprint(const std::string& id,
                                   const std::string& fingerprint) {
  SetString(id, "fp", fingerprint);
}

void PersistedData::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kPersistedDataPreference);
}

}  // namespace update_client
