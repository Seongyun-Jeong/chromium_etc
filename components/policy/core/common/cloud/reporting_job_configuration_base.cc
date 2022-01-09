// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/common/cloud/reporting_job_configuration_base.h"

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "components/policy/core/common/cloud/cloud_policy_client.h"
#include "components/policy/core/common/cloud/cloud_policy_util.h"
#include "components/policy/core/common/cloud/device_management_service.h"
#include "components/policy/core/common/cloud/dm_auth.h"
#include "components/policy/policy_export.h"
#include "components/version_info/version_info.h"
#include "google_apis/google_api_keys.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"

namespace policy {

// Strings for |DeviceDictionaryBuilder|.
const char
    ReportingJobConfigurationBase::DeviceDictionaryBuilder::kDeviceKey[] =
        "device";
const char ReportingJobConfigurationBase::DeviceDictionaryBuilder::kDMToken[] =
    "dmToken";
const char ReportingJobConfigurationBase::DeviceDictionaryBuilder::kClientId[] =
    "clientId";
const char
    ReportingJobConfigurationBase::DeviceDictionaryBuilder::kOSVersion[] =
        "osVersion";
const char
    ReportingJobConfigurationBase::DeviceDictionaryBuilder::kOSPlatform[] =
        "osPlatform";
const char ReportingJobConfigurationBase::DeviceDictionaryBuilder::kName[] =
    "name";

// static
base::Value
ReportingJobConfigurationBase::DeviceDictionaryBuilder::BuildDeviceDictionary(
    const std::string& dm_token,
    const std::string& client_id) {
  base::Value device_dictionary{base::Value::Type::DICTIONARY};
  device_dictionary.SetStringKey(kDMToken, dm_token);
  device_dictionary.SetStringKey(kClientId, client_id);
  device_dictionary.SetStringKey(kOSVersion, GetOSVersion());
  device_dictionary.SetStringKey(kOSPlatform, GetOSPlatform());
  device_dictionary.SetStringKey(kName, GetDeviceName());
  return device_dictionary;
}

// static
std::string
ReportingJobConfigurationBase::DeviceDictionaryBuilder::GetDMTokenPath() {
  return GetStringPath(kDMToken);
}

// static
std::string
ReportingJobConfigurationBase::DeviceDictionaryBuilder::GetClientIdPath() {
  return GetStringPath(kClientId);
}

// static
std::string
ReportingJobConfigurationBase::DeviceDictionaryBuilder::GetOSVersionPath() {
  return GetStringPath(kOSVersion);
}

// static
std::string
ReportingJobConfigurationBase::DeviceDictionaryBuilder::GetOSPlatformPath() {
  return GetStringPath(kOSPlatform);
}

// static
std::string
ReportingJobConfigurationBase::DeviceDictionaryBuilder::GetNamePath() {
  return GetStringPath(kName);
}

// static
std::string
ReportingJobConfigurationBase::DeviceDictionaryBuilder::GetStringPath(
    base::StringPiece leaf_name) {
  return base::JoinString({kDeviceKey, leaf_name}, ".");
}

// Strings for |BrowserDictionaryBuilder|.
const char
    ReportingJobConfigurationBase::BrowserDictionaryBuilder::kBrowserKey[] =
        "browser";
const char
    ReportingJobConfigurationBase::BrowserDictionaryBuilder::kBrowserId[] =
        "browserId";
const char
    ReportingJobConfigurationBase::BrowserDictionaryBuilder::kUserAgent[] =
        "userAgent";
const char
    ReportingJobConfigurationBase::BrowserDictionaryBuilder::kMachineUser[] =
        "machineUser";
const char
    ReportingJobConfigurationBase::BrowserDictionaryBuilder::kChromeVersion[] =
        "chromeVersion";

// static
base::Value
ReportingJobConfigurationBase::BrowserDictionaryBuilder::BuildBrowserDictionary(
    bool include_device_info) {
  base::Value browser_dictionary{base::Value::Type::DICTIONARY};

  base::FilePath browser_id;
  if (base::PathService::Get(base::DIR_EXE, &browser_id)) {
    browser_dictionary.SetStringKey(kBrowserId, browser_id.AsUTF8Unsafe());
  }

  if (include_device_info)
    browser_dictionary.SetStringKey(kMachineUser, GetOSUsername());

  browser_dictionary.SetStringKey(kChromeVersion,
                                  version_info::GetVersionNumber());
  return browser_dictionary;
}

// static
std::string
ReportingJobConfigurationBase::BrowserDictionaryBuilder::GetBrowserIdPath() {
  return GetStringPath(kBrowserId);
}

// static
std::string
ReportingJobConfigurationBase::BrowserDictionaryBuilder::GetUserAgentPath() {
  return GetStringPath(kUserAgent);
}

// static
std::string
ReportingJobConfigurationBase::BrowserDictionaryBuilder::GetMachineUserPath() {
  return GetStringPath(kMachineUser);
}

// static
std::string ReportingJobConfigurationBase::BrowserDictionaryBuilder::
    GetChromeVersionPath() {
  return GetStringPath(kChromeVersion);
}

// static
std::string
ReportingJobConfigurationBase::BrowserDictionaryBuilder::GetStringPath(
    base::StringPiece leaf_name) {
  return base::JoinString({kBrowserKey, leaf_name}, ".");
}

std::string ReportingJobConfigurationBase::GetPayload() {
  // Move context keys to the payload.
  if (context_.has_value()) {
    payload_.MergeDictionary(&context_.value());
    context_.reset();
  }

  // Allow children to mutate the payload if need be.
  UpdatePayloadBeforeGetInternal();

  std::string payload_string;
  base::JSONWriter::Write(payload_, &payload_string);
  return payload_string;
}

std::string ReportingJobConfigurationBase::GetUmaName() {
  return GetUmaString() + GetJobTypeAsString(GetType());
}

DeviceManagementService::Job::RetryMethod
ReportingJobConfigurationBase::ShouldRetry(int response_code,
                                           const std::string& response_body) {
  // If the request wasn't successfully processed at all, resending it won't do
  // anything. Don't retry.
  if (response_code != DeviceManagementService::kSuccess) {
    return DeviceManagementService::Job::NO_RETRY;
  }

  // Allow child to determine if any portion of the message should be retried.
  return ShouldRetryInternal(response_code, response_body);
}

void ReportingJobConfigurationBase::OnBeforeRetry(
    int response_code,
    const std::string& response_body) {
  // If the request wasn't successful, don't try to retry.
  if (response_code != DeviceManagementService::kSuccess) {
    return;
  }

  OnBeforeRetryInternal(response_code, response_body);
}

void ReportingJobConfigurationBase::OnURLLoadComplete(
    DeviceManagementService::Job* job,
    int net_error,
    int response_code,
    const std::string& response_body) {
  absl::optional<base::Value> response = base::JSONReader::Read(response_body);

  // Parse the response even if |response_code| is not a success since the
  // response data may contain an error message.
  // Map the net_error/response_code to a DeviceManagementStatus.
  DeviceManagementStatus code;
  if (net_error != net::OK) {
    code = DM_STATUS_REQUEST_FAILED;
  } else {
    switch (response_code) {
      case DeviceManagementService::kSuccess:
        code = DM_STATUS_SUCCESS;
        break;
      case DeviceManagementService::kInvalidArgument:
        code = DM_STATUS_REQUEST_INVALID;
        break;
      case DeviceManagementService::kInvalidAuthCookieOrDMToken:
        code = DM_STATUS_SERVICE_MANAGEMENT_TOKEN_INVALID;
        break;
      case DeviceManagementService::kDeviceManagementNotAllowed:
        code = DM_STATUS_SERVICE_MANAGEMENT_NOT_SUPPORTED;
        break;
      default:
        // Handle all unknown 5xx HTTP error codes as temporary and any other
        // unknown error as one that needs more time to recover.
        if (response_code >= 500 && response_code <= 599)
          code = DM_STATUS_TEMPORARY_UNAVAILABLE;
        else
          code = DM_STATUS_HTTP_STATUS_ERROR;
        break;
    }
  }

  base::Value response_value = response ? std::move(*response) : base::Value();
  std::move(callback_).Run(job, code, net_error, response_value);
}

DeviceManagementService::Job::RetryMethod
ReportingJobConfigurationBase::ShouldRetryInternal(
    int response_code,
    const std::string& response_body) {
  return JobConfigurationBase::ShouldRetry(response_code, response_body);
}

void ReportingJobConfigurationBase::OnBeforeRetryInternal(
    int response_code,
    const std::string& response_body) {}

void ReportingJobConfigurationBase::UpdatePayloadBeforeGetInternal() {}

GURL ReportingJobConfigurationBase::GetURL(int last_error) const {
  return GURL(server_url_);
}

ReportingJobConfigurationBase::ReportingJobConfigurationBase(
    JobType type,
    scoped_refptr<network::SharedURLLoaderFactory> factory,
    CloudPolicyClient* client,
    const std::string& server_url,
    bool include_device_info,
    UploadCompleteCallback callback)
    : JobConfigurationBase(type,
                           DMAuth::FromDMToken(client->dm_token()),
                           /*oauth_token=*/absl::nullopt,
                           factory),
      payload_(base::Value::Type::DICTIONARY),
      callback_(std::move(callback)),
      server_url_(server_url) {
  DCHECK(GetAuth().has_dm_token());
  InitializePayload(client, include_device_info);
}

ReportingJobConfigurationBase::~ReportingJobConfigurationBase() = default;

void ReportingJobConfigurationBase::InitializePayload(
    CloudPolicyClient* client,
    bool include_device_info) {
  AddParameter("key", google_apis::GetAPIKey());

  if (include_device_info) {
    payload_.SetKey(DeviceDictionaryBuilder::kDeviceKey,
                    DeviceDictionaryBuilder::BuildDeviceDictionary(
                        client->dm_token(), client->client_id()));
  }
  payload_.SetKey(
      BrowserDictionaryBuilder::kBrowserKey,
      BrowserDictionaryBuilder::BuildBrowserDictionary(include_device_info));
}

}  // namespace policy
