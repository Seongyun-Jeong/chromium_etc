// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/reporting/extension_request/extension_request_report_generator.h"

#include <string>

#include "base/json/values_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/enterprise/reporting/prefs.h"
#include "chrome/browser/extensions/extension_management.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/pref_names.h"
#include "components/enterprise/common/proto/extensions_workflow_events.pb.h"
#include "components/policy/core/common/cloud/cloud_policy_util.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "extensions/common/extension_urls.h"

namespace enterprise_reporting {
namespace {

bool IsRequestInDict(const std::string& extension_id,
                     const base::Value* requests) {
  return requests->FindKey(extension_id) != nullptr;
}

// Create the ExtensionsWorkflowEvent based on the |extension_id| and
// |request_data|. |request_data| is nullptr for remove-request and used for
// add-request.
std::unique_ptr<ExtensionsWorkflowEvent> GenerateReport(
    const std::string& extension_id,
    const base::Value* request_data) {
  auto report = std::make_unique<ExtensionsWorkflowEvent>();
  report->set_id(extension_id);
  if (request_data) {
    if (request_data->is_dict()) {
      absl::optional<base::Time> timestamp = ::base::ValueToTime(
          request_data->FindKey(extension_misc::kExtensionRequestTimestamp));
      if (timestamp)
        report->set_request_timestamp_millis(timestamp->ToJavaTime());
      if (base::FeatureList::IsEnabled(
              features::kExtensionWorkflowJustification)) {
        const std::string* justification = request_data->FindStringKey(
            extension_misc::kExtensionWorkflowJustification);
        if (justification)
          report->set_justification(*justification);
      }
    }
    report->set_removed(false);
  } else {
    report->set_removed(true);
  }
#if BUILDFLAG(IS_CHROMEOS_ASH)
  report->set_client_type(ExtensionsWorkflowEvent::CHROME_OS_USER);
#else
  report->set_client_type(ExtensionsWorkflowEvent::BROWSER_DEVICE);
  report->set_device_name(policy::GetMachineName());
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
  return report;
}

}  // namespace

// static
bool ExtensionRequestReportGenerator::ShouldUploadExtensionRequest(
    const std::string& extension_id,
    const std::string& webstore_update_url,
    extensions::ExtensionManagement* extension_management) {
  auto mode = extension_management->GetInstallationMode(extension_id,
                                                        webstore_update_url);
  return (mode == extensions::ExtensionManagement::INSTALLATION_BLOCKED ||
          mode == extensions::ExtensionManagement::INSTALLATION_REMOVED) &&
         !extension_management->IsInstallationExplicitlyBlocked(extension_id);
}

ExtensionRequestReportGenerator::ExtensionRequestReportGenerator() = default;
ExtensionRequestReportGenerator::~ExtensionRequestReportGenerator() = default;

std::vector<std::unique_ptr<ExtensionsWorkflowEvent>>
ExtensionRequestReportGenerator::Generate(
    const RealTimeReportGenerator::Data& data) {
  return GenerateForProfile(
      static_cast<const ExtensionRequestData&>(data).profile);
}

std::vector<std::unique_ptr<ExtensionsWorkflowEvent>>
ExtensionRequestReportGenerator::GenerateForProfile(Profile* profile) {
  DCHECK(profile);

  std::vector<std::unique_ptr<ExtensionsWorkflowEvent>> reports;

  extensions::ExtensionManagement* extension_management =
      extensions::ExtensionManagementFactory::GetForBrowserContext(profile);
  std::string webstore_update_url =
      extension_urls::GetDefaultWebstoreUpdateUrl().spec();

  const base::Value* pending_requests =
      profile->GetPrefs()->GetDictionary(prefs::kCloudExtensionRequestIds);
  const base::Value* uploaded_requests =
      profile->GetPrefs()->GetDictionary(kCloudExtensionRequestUploadedIds);

  for (auto it : pending_requests->DictItems()) {
    const std::string& extension_id = it.first;
    if (!ShouldUploadExtensionRequest(extension_id, webstore_update_url,
                                      extension_management)) {
      continue;
    }

    // Request has already been uploaded.
    if (IsRequestInDict(extension_id, uploaded_requests))
      continue;

    reports.push_back(
        GenerateReport(extension_id, /*request_data=*/&it.second));
  }

  for (auto it : uploaded_requests->DictItems()) {
    const std::string& extension_id = it.first;

    // Request is still pending, no need to send remove request.
    if (IsRequestInDict(extension_id, pending_requests))
      continue;

    reports.push_back(GenerateReport(extension_id, /*request_data=*/nullptr));
  }

  // Update the preference in the end.
  DictionaryPrefUpdate uploaded_requests_update(
      profile->GetPrefs(), kCloudExtensionRequestUploadedIds);

  for (const auto& report : reports) {
    std::string id = report.get()->id();
    if (!report.get()->removed()) {
      uploaded_requests_update->SetPath(id + ".upload_timestamp",
                                        ::base::TimeToValue(base::Time::Now()));
    } else {
      uploaded_requests_update->RemoveKey(id);
    }
  }

  return reports;
}

}  // namespace enterprise_reporting
