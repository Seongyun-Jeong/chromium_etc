// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/security_interstitials/content/https_only_mode_blocking_page.h"

#include "base/logging.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/security_interstitials/content/security_interstitial_controller_client.h"
#include "components/security_interstitials/core/common_string_util.h"
#include "components/security_interstitials/core/controller_client.h"
#include "components/security_interstitials/core/metrics_helper.h"
#include "components/security_interstitials/core/pref_names.h"
#include "components/strings/grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"

namespace security_interstitials {

namespace {
const char kLearnMoreLink[] = "https://support.google.com/chrome?p=first_mode";
}  // namespace

// static
const SecurityInterstitialPage::TypeID
    HttpsOnlyModeBlockingPage::kTypeForTesting =
        &HttpsOnlyModeBlockingPage::kTypeForTesting;

HttpsOnlyModeBlockingPage::HttpsOnlyModeBlockingPage(
    content::WebContents* web_contents,
    const GURL& request_url,
    std::unique_ptr<SecurityInterstitialControllerClient> controller_client)
    : SecurityInterstitialPage(web_contents,
                               request_url,
                               std::move(controller_client)) {
  controller()->metrics_helper()->RecordUserDecision(MetricsHelper::SHOW);
  controller()->metrics_helper()->RecordUserInteraction(
      MetricsHelper::TOTAL_VISITS);
}

HttpsOnlyModeBlockingPage::~HttpsOnlyModeBlockingPage() = default;

void HttpsOnlyModeBlockingPage::OnInterstitialClosing() {
  // If the page is closing without an explicit decision, record it as not
  // proceeding.
  if (!user_made_decision_) {
    controller()->metrics_helper()->RecordUserDecision(
        MetricsHelper::DONT_PROCEED);
  }
}

SecurityInterstitialPage::TypeID
HttpsOnlyModeBlockingPage::GetTypeForTesting() {
  return HttpsOnlyModeBlockingPage::kTypeForTesting;
}

void HttpsOnlyModeBlockingPage::CommandReceived(const std::string& command) {
  if (command == "\"pageLoadComplete\"") {
    // content::WaitForRenderFrameReady sends this message when the page
    // load completes. Ignore it.
    return;
  }
  int cmd = 0;
  bool retval = base::StringToInt(command, &cmd);
  DCHECK(retval);
  switch (cmd) {
    case security_interstitials::CMD_DONT_PROCEED:
      user_made_decision_ = true;
      controller()->metrics_helper()->RecordUserDecision(
          MetricsHelper::DONT_PROCEED);
      controller()->GoBack();
      break;
    case security_interstitials::CMD_PROCEED:
      user_made_decision_ = true;
      controller()->metrics_helper()->RecordUserDecision(
          MetricsHelper::PROCEED);
      controller()->Proceed();
      break;
    case security_interstitials::CMD_OPEN_HELP_CENTER: {
      controller()->metrics_helper()->RecordUserInteraction(
          security_interstitials::MetricsHelper::SHOW_LEARN_MORE);
      controller()->OpenUrlInNewForegroundTab(GURL(kLearnMoreLink));
      break;
    }
    case security_interstitials::CMD_DO_REPORT:
    case security_interstitials::CMD_DONT_REPORT:
    case security_interstitials::CMD_SHOW_MORE_SECTION:
    case security_interstitials::CMD_OPEN_DATE_SETTINGS:
    case security_interstitials::CMD_OPEN_REPORTING_PRIVACY:
    case security_interstitials::CMD_OPEN_WHITEPAPER:
    case security_interstitials::CMD_RELOAD:
    case security_interstitials::CMD_OPEN_DIAGNOSTIC:
    case security_interstitials::CMD_OPEN_LOGIN:
    case security_interstitials::CMD_REPORT_PHISHING_ERROR:
      // Not supported by the HTTPS-only mode blocking page.
      NOTREACHED() << "Unsupported command: " << command;
      break;
    case security_interstitials::CMD_ERROR:
    case security_interstitials::CMD_TEXT_FOUND:
    case security_interstitials::CMD_TEXT_NOT_FOUND:
      // Commands are for testing.
      break;
  }
}

void HttpsOnlyModeBlockingPage::PopulateInterstitialStrings(
    base::Value* load_time_data) {
  PopulateValuesForSharedHTML(load_time_data);

  load_time_data->SetStringKey(
      "tabTitle", l10n_util::GetStringUTF16(IDS_HTTPS_ONLY_MODE_TITLE));
  load_time_data->SetStringKey(
      "heading", l10n_util::GetStringFUTF16(
                     IDS_HTTPS_ONLY_MODE_HEADING,
                     common_string_util::GetFormattedHostName(request_url())));
  load_time_data->SetStringKey(
      "primaryParagraph",
      l10n_util::GetStringUTF16(IDS_HTTPS_ONLY_MODE_PRIMARY_PARAGRAPH));
  load_time_data->SetStringKey(
      "proceedButtonText",
      l10n_util::GetStringUTF16(IDS_HTTPS_ONLY_MODE_SUBMIT_BUTTON));
  load_time_data->SetStringKey(
      "primaryButtonText",
      l10n_util::GetStringUTF16(IDS_HTTPS_ONLY_MODE_BACK_BUTTON));
  load_time_data->SetStringKey(
      "optInLink",
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_SCOUT_REPORTING_AGREE));
  load_time_data->SetStringKey(
      "enhancedProtectionMessage",
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_ENHANCED_PROTECTION_MESSAGE));
}

void HttpsOnlyModeBlockingPage::PopulateValuesForSharedHTML(
    base::Value* load_time_data) {
  load_time_data->SetStringKey("type", "HTTPS_ONLY");
  load_time_data->SetBoolKey("overridable", false);
  load_time_data->SetBoolKey("hide_primary_button", false);
  load_time_data->SetBoolKey("show_recurrent_error_paragraph", false);
  load_time_data->SetStringKey("recurrentErrorParagraph", "");
  load_time_data->SetStringKey("openDetails", "");
  load_time_data->SetStringKey("explanationParagraph", "");
  load_time_data->SetStringKey("finalParagraph", "");
}

}  // namespace security_interstitials
