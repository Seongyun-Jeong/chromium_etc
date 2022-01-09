// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/settings_clear_browsing_data_handler.h"

#include <stddef.h>
#include <vector>

#include "base/bind.h"
#include "base/cxx17_backports.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/values.h"
#include "chrome/browser/browsing_data/browsing_data_important_sites_util.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "chrome/browser/browsing_data/counters/browsing_data_counter_factory.h"
#include "chrome/browser/browsing_data/counters/browsing_data_counter_utils.h"
#include "chrome/browser/engagement/important_sites_util.h"
#include "chrome/browser/history/web_history_service_factory.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/signin/account_reconcilor_factory.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/sync/sync_ui_util.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/browsing_data/content/browsing_data_helper.h"
#include "components/browsing_data/core/browsing_data_utils.h"
#include "components/browsing_data/core/history_notice_utils.h"
#include "components/browsing_data/core/pref_names.h"
#include "components/prefs/pref_member.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/search_engine_type.h"
#include "components/search_engines/template_url_service.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/bytes_formatting.h"

using BrowsingDataType = browsing_data::BrowsingDataType;

namespace {

const int kMaxTimesHistoryNoticeShown = 1;
const int kMaxInstalledAppsToWarnOf = 5;

// TODO(msramek): Get the list of deletion preferences from the JS side.
const char* kCounterPrefsAdvanced[] = {
    browsing_data::prefs::kDeleteBrowsingHistory,
    browsing_data::prefs::kDeleteCache,
    browsing_data::prefs::kDeleteCookies,
    browsing_data::prefs::kDeleteDownloadHistory,
    browsing_data::prefs::kDeleteFormData,
    browsing_data::prefs::kDeleteHostedAppsData,
    browsing_data::prefs::kDeletePasswords,
    browsing_data::prefs::kDeleteSiteSettings,
};

// Additional counters for the basic tab of CBD.
const char* kCounterPrefsBasic[] = {
    browsing_data::prefs::kDeleteCacheBasic,
};

const char kRegisterableDomainField[] = "registerableDomain";
const char kReasonBitfieldField[] = "reasonBitfield";
const char kIsCheckedField[] = "isChecked";
const char kAppName[] = "appName";

} // namespace

namespace settings {

// ClearBrowsingDataHandler ----------------------------------------------------

ClearBrowsingDataHandler::ClearBrowsingDataHandler(content::WebUI* webui,
                                                   Profile* profile)
    : profile_(profile),
      sync_service_(SyncServiceFactory::GetForProfile(profile_)),
      show_history_deletion_dialog_(false) {}

ClearBrowsingDataHandler::~ClearBrowsingDataHandler() = default;

void ClearBrowsingDataHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getInstalledApps",
      base::BindRepeating(
          &ClearBrowsingDataHandler::GetRecentlyLaunchedInstalledApps,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "clearBrowsingData",
      base::BindRepeating(&ClearBrowsingDataHandler::HandleClearBrowsingData,
                          base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "initializeClearBrowsingData",
      base::BindRepeating(&ClearBrowsingDataHandler::HandleInitialize,
                          base::Unretained(this)));
}

void ClearBrowsingDataHandler::OnJavascriptAllowed() {
  if (sync_service_)
    sync_service_observation_.Observe(sync_service_.get());

  dse_service_observation_.Observe(
      TemplateURLServiceFactory::GetForProfile(profile_));
  DCHECK(counters_.empty());
  for (const std::string& pref : kCounterPrefsBasic) {
    AddCounter(BrowsingDataCounterFactory::GetForProfileAndPref(profile_, pref),
               browsing_data::ClearBrowsingDataTab::BASIC);
  }
  for (const std::string& pref : kCounterPrefsAdvanced) {
    AddCounter(BrowsingDataCounterFactory::GetForProfileAndPref(profile_, pref),
               browsing_data::ClearBrowsingDataTab::ADVANCED);
  }
  PrefService* prefs = profile_->GetPrefs();
  period_ = std::make_unique<IntegerPrefMember>();
  period_->Init(
      browsing_data::prefs::kDeleteTimePeriod, prefs,
      base::BindRepeating(&ClearBrowsingDataHandler::HandleTimePeriodChanged,
                          base::Unretained(this)));
  periodBasic_ = std::make_unique<IntegerPrefMember>();
  periodBasic_->Init(
      browsing_data::prefs::kDeleteTimePeriodBasic, prefs,
      base::BindRepeating(&ClearBrowsingDataHandler::HandleTimePeriodChanged,
                          base::Unretained(this)));
}

void ClearBrowsingDataHandler::OnJavascriptDisallowed() {
  dse_service_observation_.Reset();
  sync_service_observation_.Reset();
  weak_ptr_factory_.InvalidateWeakPtrs();
  counters_.clear();
  period_.reset();
  periodBasic_.reset();
}

void ClearBrowsingDataHandler::HandleClearBrowsingDataForTest() {
  // HandleClearBrowsingData takes in a ListValue as its only parameter. The
  // ListValue must contain four values: web_ui callback ID, a list of data
  // types that the user cleared from the clear browsing data UI and time period
  // of the data to be cleared.

  base::Value data_types(base::Value::Type::LIST);
  data_types.Append("browser.clear_data.browsing_history");

  base::Value installed_apps(base::Value::Type::LIST);

  base::Value list_args(base::Value::Type::LIST);
  list_args.Append("webui_callback_id");
  list_args.Append(std::move(data_types));
  list_args.Append(1);
  list_args.Append(std::move(installed_apps));
  HandleClearBrowsingData(list_args.GetList());
}

void ClearBrowsingDataHandler::GetRecentlyLaunchedInstalledApps(
    base::Value::ConstListView list) {
  CHECK_EQ(2U, list.size());
  std::string webui_callback_id = list[0].GetString();
  int period_selected = list[1].GetInt();

  browsing_data::TimePeriod time_period =
      static_cast<browsing_data::TimePeriod>(period_selected);

  auto installed_apps =
      site_engagement::ImportantSitesUtil::GetInstalledRegisterableDomains(
          time_period, profile_, kMaxInstalledAppsToWarnOf);

  OnGotInstalledApps(webui_callback_id, installed_apps);
}

void ClearBrowsingDataHandler::OnGotInstalledApps(
    const std::string& webui_callback_id,
    const std::vector<site_engagement::ImportantSitesUtil::ImportantDomainInfo>&
        installed_apps) {
  base::ListValue installed_apps_list;
  for (const auto& info : installed_apps) {
    auto entry = std::make_unique<base::DictionaryValue>();
    // Used to get favicon in ClearBrowsingDataDialog and display URL next to
    // app name in the dialog.
    entry->SetString(kRegisterableDomainField, info.registerable_domain);
    // The |reason_bitfield| is only passed to Javascript to be logged
    // from |HandleClearBrowsingData|.
    entry->SetInteger(kReasonBitfieldField, info.reason_bitfield);
    // Initially all sites are selected for deletion.
    entry->SetBoolean(kIsCheckedField, true);
    // User friendly name for the installed app.
    DCHECK(info.app_name);
    entry->SetString(kAppName, info.app_name.value());
    installed_apps_list.Append(std::move(entry));
  }
  ResolveJavascriptCallback(base::Value(webui_callback_id),
                            installed_apps_list);
}

std::unique_ptr<content::BrowsingDataFilterBuilder>
ClearBrowsingDataHandler::ProcessInstalledApps(
    base::Value::ConstListView installed_apps) {
  std::vector<std::string> excluded_domains;
  std::vector<int32_t> excluded_domain_reasons;
  std::vector<std::string> ignored_domains;
  std::vector<int32_t> ignored_domain_reasons;
  for (const auto& item : installed_apps) {
    const base::DictionaryValue* site = nullptr;
    CHECK(item.GetAsDictionary(&site));
    bool is_checked = site->FindBoolPath(kIsCheckedField).value();
    std::string domain;
    CHECK(site->GetString(kRegisterableDomainField, &domain));
    absl::optional<int> domain_reason = site->FindIntKey(kReasonBitfieldField);
    CHECK(domain_reason);
    if (is_checked) {  // Selected installed apps should be deleted.
      ignored_domains.push_back(domain);
      ignored_domain_reasons.push_back(*domain_reason);
    } else {  // Unselected sites should be kept.
      excluded_domains.push_back(domain);
      excluded_domain_reasons.push_back(*domain_reason);
    }
  }
  if (!excluded_domains.empty() || !ignored_domains.empty()) {
    site_engagement::ImportantSitesUtil::RecordExcludedAndIgnoredImportantSites(
        profile_->GetOriginalProfile(), excluded_domains,
        excluded_domain_reasons, ignored_domains, ignored_domain_reasons);
  }

  std::unique_ptr<content::BrowsingDataFilterBuilder> filter_builder(
      content::BrowsingDataFilterBuilder::Create(
          content::BrowsingDataFilterBuilder::Mode::kPreserve));
  for (const std::string& domain : excluded_domains)
    filter_builder->AddRegisterableDomain(domain);
  return filter_builder;
}

void ClearBrowsingDataHandler::HandleClearBrowsingData(
    base::Value::ConstListView args_list) {
  CHECK_EQ(4U, args_list.size());
  std::string webui_callback_id = args_list[0].GetString();

  PrefService* prefs = profile_->GetPrefs();
  int site_data_mask = chrome_browsing_data_remover::DATA_TYPE_SITE_DATA;
  // Don't try to clear LSO data if it's not supported.
  if (!prefs->GetBoolean(prefs::kClearPluginLSODataEnabled))
    site_data_mask &= ~chrome_browsing_data_remover::DATA_TYPE_PLUGIN_DATA;

  uint64_t remove_mask = 0;
  uint64_t origin_mask = 0;
  std::vector<BrowsingDataType> data_type_vector;

  CHECK(args_list[1].is_list());
  base::Value::ConstListView data_type_list = args_list[1].GetList();
  for (const base::Value& type : data_type_list) {
    const std::string pref_name = type.GetString();
    BrowsingDataType data_type =
        browsing_data::GetDataTypeFromDeletionPreference(pref_name);
    data_type_vector.push_back(data_type);

    switch (data_type) {
      case BrowsingDataType::HISTORY:
        if (prefs->GetBoolean(prefs::kAllowDeletingBrowserHistory))
          remove_mask |= chrome_browsing_data_remover::DATA_TYPE_HISTORY;
        break;
      case BrowsingDataType::DOWNLOADS:
        if (prefs->GetBoolean(prefs::kAllowDeletingBrowserHistory))
          remove_mask |= content::BrowsingDataRemover::DATA_TYPE_DOWNLOADS;
        break;
      case BrowsingDataType::CACHE:
        remove_mask |= content::BrowsingDataRemover::DATA_TYPE_CACHE;
        break;
      case BrowsingDataType::COOKIES:
        remove_mask |= site_data_mask;
        origin_mask |=
            content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB;
        break;
      case BrowsingDataType::PASSWORDS:
        remove_mask |= chrome_browsing_data_remover::DATA_TYPE_PASSWORDS;
        remove_mask |=
            chrome_browsing_data_remover::DATA_TYPE_ACCOUNT_PASSWORDS;
        break;
      case BrowsingDataType::FORM_DATA:
        remove_mask |= chrome_browsing_data_remover::DATA_TYPE_FORM_DATA;
        break;
      case BrowsingDataType::SITE_SETTINGS:
        remove_mask |= chrome_browsing_data_remover::DATA_TYPE_CONTENT_SETTINGS;
        break;
      case BrowsingDataType::HOSTED_APPS_DATA:
        remove_mask |= site_data_mask;
        origin_mask |= content::BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB;
        break;
      case BrowsingDataType::BOOKMARKS:
        // Only implemented on Android.
        NOTREACHED();
        break;
      case BrowsingDataType::NUM_TYPES:
        NOTREACHED();
        break;
    }
  }

  base::flat_set<BrowsingDataType> data_types(std::move(data_type_vector));

  // Record the deletion of cookies and cache.
  content::BrowsingDataRemover::CookieOrCacheDeletionChoice choice =
      content::BrowsingDataRemover::NEITHER_COOKIES_NOR_CACHE;
  if (data_types.find(BrowsingDataType::COOKIES) != data_types.end()) {
    choice = data_types.find(BrowsingDataType::CACHE) != data_types.end()
                 ? content::BrowsingDataRemover::BOTH_COOKIES_AND_CACHE
                 : content::BrowsingDataRemover::ONLY_COOKIES;
  } else if (data_types.find(BrowsingDataType::CACHE) != data_types.end()) {
    choice = content::BrowsingDataRemover::ONLY_CACHE;
  }

  UMA_HISTOGRAM_ENUMERATION(
      "History.ClearBrowsingData.UserDeletedCookieOrCacheFromDialog", choice,
      content::BrowsingDataRemover::MAX_CHOICE_VALUE);

  // Record the circumstances under which passwords are deleted.
  if (data_types.find(BrowsingDataType::PASSWORDS) != data_types.end()) {
    static const BrowsingDataType other_types[] = {
        BrowsingDataType::HISTORY,        BrowsingDataType::DOWNLOADS,
        BrowsingDataType::CACHE,          BrowsingDataType::COOKIES,
        BrowsingDataType::FORM_DATA,      BrowsingDataType::HOSTED_APPS_DATA,
    };
    static size_t num_other_types = base::size(other_types);
    int checked_other_types =
        std::count_if(other_types, other_types + num_other_types,
                      [&data_types](BrowsingDataType type) {
                        return data_types.find(type) != data_types.end();
                      });
    base::UmaHistogramSparse(
        "History.ClearBrowsingData.PasswordsDeletion.AdditionalDatatypesCount",
        checked_other_types);
  }

  std::unique_ptr<AccountReconcilor::ScopedSyncedDataDeletion>
      scoped_data_deletion;

  // If Sync is running, prevent it from being paused during the operation.
  // However, if Sync is in error, clearing cookies should pause it.
  if (!profile_->IsGuestSession() &&
      GetSyncStatusMessageType(profile_) == SyncStatusMessageType::kSynced) {
    // Settings can not be opened in incognito windows.
    DCHECK(!profile_->IsOffTheRecord());
    scoped_data_deletion = AccountReconcilorFactory::GetForProfile(profile_)
                               ->GetScopedSyncDataDeletion();
  }

  int period_selected = args_list[2].GetInt();

  const base::Value::ConstListView installed_apps = args_list[3].GetList();
  std::unique_ptr<content::BrowsingDataFilterBuilder> filter_builder =
      ProcessInstalledApps(installed_apps);

  content::BrowsingDataRemover* remover = profile_->GetBrowsingDataRemover();

  base::OnceCallback<void(uint64_t)> callback =
      base::BindOnce(&ClearBrowsingDataHandler::OnClearingTaskFinished,
                     weak_ptr_factory_.GetWeakPtr(), webui_callback_id,
                     std::move(data_types), std::move(scoped_data_deletion));
  browsing_data::TimePeriod time_period =
      static_cast<browsing_data::TimePeriod>(period_selected);

  browsing_data_important_sites_util::Remove(
      remove_mask, origin_mask, time_period, std::move(filter_builder), remover,
      std::move(callback));
}

void ClearBrowsingDataHandler::OnClearingTaskFinished(
    const std::string& webui_callback_id,
    const base::flat_set<BrowsingDataType>& data_types,
    std::unique_ptr<AccountReconcilor::ScopedSyncedDataDeletion> deletion,
    uint64_t failed_data_types) {
  PrefService* prefs = profile_->GetPrefs();
  int history_notice_shown_times = prefs->GetInteger(
      browsing_data::prefs::kClearBrowsingDataHistoryNoticeShownTimes);

  // When the deletion is complete, we might show an additional dialog with
  // a notice about other forms of browsing history. This is the case if
  const bool show_history_notice =
      // 1. The dialog is relevant for the user.
      show_history_deletion_dialog_ &&
      // 2. The notice has been shown less than |kMaxTimesHistoryNoticeShown|.
      history_notice_shown_times < kMaxTimesHistoryNoticeShown &&
      // 3. The selected data types contained browsing history.
      data_types.find(BrowsingDataType::HISTORY) != data_types.end();

  if (show_history_notice) {
    // Increment the preference.
    prefs->SetInteger(
        browsing_data::prefs::kClearBrowsingDataHistoryNoticeShownTimes,
        history_notice_shown_times + 1);
  }

  UMA_HISTOGRAM_BOOLEAN(
      "History.ClearBrowsingData.ShownHistoryNoticeAfterClearing",
      show_history_notice);

  bool show_passwords_notice =
      (failed_data_types & chrome_browsing_data_remover::DATA_TYPE_PASSWORDS);

  base::Value result(base::Value::Type::DICTIONARY);
  result.SetBoolKey("showHistoryNotice", show_history_notice);
  result.SetBoolKey("showPasswordsNotice", show_passwords_notice);

  ResolveJavascriptCallback(base::Value(webui_callback_id), std::move(result));
}

void ClearBrowsingDataHandler::HandleInitialize(
    base::Value::ConstListView args) {
  AllowJavascript();
  const base::Value& callback_id = args[0];

  // Needed because WebUI doesn't handle renderer crashes. See crbug.com/610450.
  weak_ptr_factory_.InvalidateWeakPtrs();

  UpdateSyncState();
  RefreshHistoryNotice();

  // Restart the counters each time the dialog is reopened.
  for (const auto& counter : counters_)
    counter->Restart();

  ResolveJavascriptCallback(callback_id, base::Value() /* Promise<void> */);
}

void ClearBrowsingDataHandler::OnStateChanged(syncer::SyncService* sync) {
  UpdateSyncState();
}

void ClearBrowsingDataHandler::UpdateSyncState() {
  signin::IdentityManager* identity_manager =
      IdentityManagerFactory::GetForProfile(profile_);
  base::DictionaryValue event;
  event.SetBoolKey("signedIn",
                   identity_manager && identity_manager->HasPrimaryAccount(
                                           signin::ConsentLevel::kSignin));
  event.SetBoolKey("syncConsented",
                   identity_manager && identity_manager->HasPrimaryAccount(
                                           signin::ConsentLevel::kSync));
  event.SetBoolKey("syncingHistory",
                   sync_service_ && sync_service_->IsSyncFeatureActive() &&
                       sync_service_->GetActiveDataTypes().Has(
                           syncer::HISTORY_DELETE_DIRECTIVES));
  event.SetBoolKey(
      "shouldShowCookieException",
      browsing_data_counter_utils::ShouldShowCookieException(profile_));

  event.SetBoolKey("isNonGoogleDse", false);
  if (base::FeatureList::IsEnabled(features::kSearchHistoryLink)) {
    const TemplateURLService* template_url_service =
        TemplateURLServiceFactory::GetForProfile(profile_);
    const TemplateURL* dse = template_url_service->GetDefaultSearchProvider();
    if (dse && dse->GetEngineType(template_url_service->search_terms_data()) !=
                   SearchEngineType::SEARCH_ENGINE_GOOGLE) {
      // Non-Google DSE. Prepopulated DSEs have an ID > 0.
      event.SetBoolKey("isNonGoogleDse", true);
      event.SetStringKey(
          "nonGoogleSearchHistoryString",
          (dse->prepopulate_id() > 0)
              ? l10n_util::GetStringFUTF16(
                    IDS_SETTINGS_CLEAR_NON_GOOGLE_SEARCH_HISTORY_PREPOPULATED_DSE,
                    dse->short_name())
              : l10n_util::GetStringUTF16(
                    IDS_SETTINGS_CLEAR_NON_GOOGLE_SEARCH_HISTORY_NON_PREPOPULATED_DSE));
    }
  }
  FireWebUIListener("update-sync-state", event);
}

void ClearBrowsingDataHandler::RefreshHistoryNotice() {
  // If the dialog with history notice has been shown less than
  // |kMaxTimesHistoryNoticeShown| times, we might have to show it when the
  // user deletes history. Find out if the conditions are met.
  int notice_shown_times = profile_->GetPrefs()->GetInteger(
      browsing_data::prefs::kClearBrowsingDataHistoryNoticeShownTimes);

  if (notice_shown_times < kMaxTimesHistoryNoticeShown) {
    browsing_data::ShouldPopupDialogAboutOtherFormsOfBrowsingHistory(
        sync_service_, WebHistoryServiceFactory::GetForProfile(profile_),
        chrome::GetChannel(),
        base::BindOnce(&ClearBrowsingDataHandler::UpdateHistoryDeletionDialog,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

void ClearBrowsingDataHandler::UpdateHistoryDeletionDialog(bool show) {
  // This is used by OnClearingTaskFinished (when the deletion finishes).
  show_history_deletion_dialog_ = show;
}

void ClearBrowsingDataHandler::AddCounter(
    std::unique_ptr<browsing_data::BrowsingDataCounter> counter,
    browsing_data::ClearBrowsingDataTab tab) {
  DCHECK(counter);
  counter->Init(
      profile_->GetPrefs(), tab,
      base::BindRepeating(&ClearBrowsingDataHandler::UpdateCounterText,
                          base::Unretained(this)));
  counters_.push_back(std::move(counter));
}

void ClearBrowsingDataHandler::UpdateCounterText(
    std::unique_ptr<browsing_data::BrowsingDataCounter::Result> result) {
  FireWebUIListener(
      "update-counter-text", base::Value(result->source()->GetPrefName()),
      base::Value(browsing_data_counter_utils::GetChromeCounterTextFromResult(
          result.get(), profile_)));
}

void ClearBrowsingDataHandler::HandleTimePeriodChanged(
    const std::string& pref_name) {
  PrefService* prefs = profile_->GetPrefs();
  int period = prefs->GetInteger(pref_name);

  browsing_data::TimePeriod time_period =
      static_cast<browsing_data::TimePeriod>(period);
  browsing_data::RecordTimePeriodChange(time_period);
}

void ClearBrowsingDataHandler::OnTemplateURLServiceChanged() {
  UpdateSyncState();
}

}  // namespace settings
