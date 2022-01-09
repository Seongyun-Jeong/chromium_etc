// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/incompatible_applications_handler_win.h"

#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/check_op.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "base/win/registry.h"
#include "chrome/browser/win/conflicts/incompatible_applications_updater.h"
#include "chrome/browser/win/conflicts/registry_key_watcher.h"
#include "chrome/browser/win/conflicts/uninstall_application.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace settings {

IncompatibleApplicationsHandler::IncompatibleApplicationsHandler() = default;

IncompatibleApplicationsHandler::~IncompatibleApplicationsHandler() = default;

void IncompatibleApplicationsHandler::RegisterMessages() {
  web_ui()->RegisterDeprecatedMessageCallback(
      "requestIncompatibleApplicationsList",
      base::BindRepeating(&IncompatibleApplicationsHandler::
                              HandleRequestIncompatibleApplicationsList,
                          base::Unretained(this)));
  web_ui()->RegisterDeprecatedMessageCallback(
      "startApplicationUninstallation",
      base::BindRepeating(&IncompatibleApplicationsHandler::
                              HandleStartApplicationUninstallation,
                          base::Unretained(this)));
  web_ui()->RegisterDeprecatedMessageCallback(
      "getSubtitlePluralString",
      base::BindRepeating(
          &IncompatibleApplicationsHandler::HandleGetSubtitlePluralString,
          base::Unretained(this)));
  web_ui()->RegisterDeprecatedMessageCallback(
      "getSubtitleNoAdminRightsPluralString",
      base::BindRepeating(&IncompatibleApplicationsHandler::
                              HandleGetSubtitleNoAdminRightsPluralString,
                          base::Unretained(this)));
  web_ui()->RegisterDeprecatedMessageCallback(
      "getListTitlePluralString",
      base::BindRepeating(
          &IncompatibleApplicationsHandler::HandleGetListTitlePluralString,
          base::Unretained(this)));
}

void IncompatibleApplicationsHandler::OnJavascriptAllowed() {}

void IncompatibleApplicationsHandler::OnJavascriptDisallowed() {
  registry_key_watchers_.clear();
}

void IncompatibleApplicationsHandler::HandleRequestIncompatibleApplicationsList(
    const base::ListValue* args) {
  CHECK_EQ(1u, args->GetList().size());

  AllowJavascript();

  // Reset the registry watchers, to correctly handle repeated calls to
  // requestIncompatibleApplicationsList().
  registry_key_watchers_.clear();

  std::vector<IncompatibleApplicationsUpdater::IncompatibleApplication>
      incompatible_applications =
          IncompatibleApplicationsUpdater::GetCachedApplications();

  base::Value application_list(base::Value::Type::LIST);

  for (const auto& application : incompatible_applications) {
    // Set up a registry watcher for each problem application.
    // Since this instance owns the watcher, it is safe to use
    // base::Unretained() because the callback won't be invoked when the watcher
    // gets deleted.
    auto registry_key_watcher = RegistryKeyWatcher::Create(
        application.info.registry_root,
        application.info.registry_key_path.c_str(),
        application.info.registry_wow64_access,
        base::BindOnce(&IncompatibleApplicationsHandler::OnApplicationRemoved,
                       base::Unretained(this), application.info));

    // Only keep the watcher if it was successfully initialized. A failure here
    // is unlikely, but the worst that can happen is that the |application| will
    // not get removed from the list automatically in the Incompatible
    // Applications subpage.
    if (registry_key_watcher) {
      registry_key_watchers_.insert(
          {application.info, std::move(registry_key_watcher)});
    }

    // Also add the application to the list that is passed to the javascript.
    base::Value dict(base::Value::Type::DICTIONARY);
    dict.SetKey("name", base::Value(base::WideToUTF8(application.info.name)));
    dict.SetKey("type",
                base::Value(application.blocklist_action->message_type()));
    dict.SetKey("url",
                base::Value(application.blocklist_action->message_url()));
    application_list.Append(std::move(dict));
  }

  UMA_HISTOGRAM_COUNTS_100("IncompatibleApplicationsPage.NumApplications",
                           incompatible_applications.size());

  const base::Value& callback_id = args->GetList().front();
  ResolveJavascriptCallback(callback_id, application_list);
}

void IncompatibleApplicationsHandler::HandleStartApplicationUninstallation(
    const base::ListValue* args) {
  CHECK_EQ(1u, args->GetList().size());
  base::RecordAction(base::UserMetricsAction(
      "IncompatibleApplicationsPage.UninstallationStarted"));

  // Open the Apps & Settings page with the application name highlighted.
  uninstall_application::LaunchUninstallFlow(
      base::UTF8ToWide(args->GetList()[0].GetString()));
}

void IncompatibleApplicationsHandler::HandleGetSubtitlePluralString(
    const base::ListValue* args) {
  GetPluralString(IDS_SETTINGS_INCOMPATIBLE_APPLICATIONS_SUBPAGE_SUBTITLE,
                  args);
}

void IncompatibleApplicationsHandler::
    HandleGetSubtitleNoAdminRightsPluralString(const base::ListValue* args) {
  GetPluralString(
      IDS_SETTINGS_INCOMPATIBLE_APPLICATIONS_SUBPAGE_SUBTITLE_NO_ADMIN_RIGHTS,
      args);
}

void IncompatibleApplicationsHandler::HandleGetListTitlePluralString(
    const base::ListValue* args) {
  GetPluralString(IDS_SETTINGS_INCOMPATIBLE_APPLICATIONS_LIST_TITLE, args);
}

void IncompatibleApplicationsHandler::GetPluralString(
    int id,
    const base::ListValue* args) {
  CHECK_EQ(2U, args->GetList().size());

  const base::Value& callback_id = args->GetList()[0];
  int num_applications = args->GetList()[1].GetInt();
  DCHECK_GT(num_applications, 0);

  ResolveJavascriptCallback(
      callback_id,
      base::Value(l10n_util::GetPluralStringFUTF16(id, num_applications)));
}

void IncompatibleApplicationsHandler::OnApplicationRemoved(
    const InstalledApplications::ApplicationInfo& application) {
  base::RecordAction(base::UserMetricsAction(
      "IncompatibleApplicationsPage.ApplicationRemoved"));

  registry_key_watchers_.erase(application);
  FireWebUIListener("incompatible-application-removed",
                    base::Value(base::WideToUTF8(application.name)));
}

}  // namespace settings
