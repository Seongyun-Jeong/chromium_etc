// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "chromecast/crash/linux/dump_info.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"

namespace chromecast {

namespace {

// "%Y-%m-%d %H:%M:%S";
const char kDumpTimeFormat[] = "%04d-%02d-%02d %02d:%02d:%02d";

const int kNumRequiredParams = 4;

const char kNameKey[] = "name";
const char kDumpTimeKey[] = "dump_time";
const char kDumpKey[] = "dump";
const char kUptimeKey[] = "uptime";
const char kLogfileKey[] = "logfile";
const char kAttachmentsKey[] = "attachments";
const char kSuffixKey[] = "suffix";
const char kPrevAppNameKey[] = "prev_app_name";
const char kCurAppNameKey[] = "cur_app_name";
const char kLastAppNameKey[] = "last_app_name";
const char kReleaseVersionKey[] = "release_version";
const char kBuildNumberKey[] = "build_number";
const char kReasonKey[] = "reason";
const char kStadiaSessionIdKey[] = "stadia_session_id";
const char kCrashProductNameKey[] = "crash_product_name";
const char kExecNameKey[] = "exec_name";
const char kSignatureKey[] = "signature";
const char kExtraInfoKey[] = "extra_info";

}  // namespace

DumpInfo::DumpInfo(const base::Value* entry) : valid_(ParseEntry(entry)) {}

DumpInfo::DumpInfo(const std::string& crashed_process_dump,
                   const std::string& crashed_process_logfile,
                   const base::Time& dump_time,
                   const MinidumpParams& params,
                   const std::vector<std::string>* attachments)
    : crashed_process_dump_(crashed_process_dump),
      logfile_(crashed_process_logfile),
      dump_time_(dump_time),
      params_(params),
      valid_(true) {
  if (attachments) {
    attachments_ = *attachments;
  }
}

DumpInfo::~DumpInfo() {}

std::unique_ptr<base::Value> DumpInfo::GetAsValue() const {
  std::unique_ptr<base::Value> result =
      std::make_unique<base::DictionaryValue>();
  base::DictionaryValue* entry;
  result->GetAsDictionary(&entry);

  base::Time::Exploded ex;
  dump_time_.LocalExplode(&ex);
  std::string dump_time =
      base::StringPrintf(kDumpTimeFormat, ex.year, ex.month, ex.day_of_month,
                         ex.hour, ex.minute, ex.second);
  entry->SetString(kDumpTimeKey, dump_time);

  entry->SetString(kDumpKey, crashed_process_dump_);
  std::string uptime = std::to_string(params_.process_uptime);
  entry->SetString(kUptimeKey, uptime);
  entry->SetString(kLogfileKey, logfile_);

  std::unique_ptr<base::ListValue> attachments_list(new base::ListValue());
  for (const auto& attachment : attachments_) {
    attachments_list->Append(attachment);
  }
  entry->SetList(kAttachmentsKey, std::move(attachments_list));
  entry->SetString(kSuffixKey, params_.suffix);
  entry->SetString(kPrevAppNameKey, params_.previous_app_name);
  entry->SetString(kCurAppNameKey, params_.current_app_name);
  entry->SetString(kLastAppNameKey, params_.last_app_name);
  entry->SetString(kReleaseVersionKey, params_.cast_release_version);
  entry->SetString(kBuildNumberKey, params_.cast_build_number);
  entry->SetString(kReasonKey, params_.reason);
  entry->SetString(kStadiaSessionIdKey, params_.stadia_session_id);
  entry->SetString(kExecNameKey, params_.exec_name);
  entry->SetString(kSignatureKey, params_.signature);
  entry->SetString(kExtraInfoKey, params_.extra_info);
  entry->SetString(kCrashProductNameKey, params_.crash_product_name);

  return result;
}

bool DumpInfo::ParseEntry(const base::Value* entry) {
  valid_ = false;

  if (!entry)
    return false;

  const base::DictionaryValue* dict;
  if (!entry->GetAsDictionary(&dict))
    return false;

  // Extract required fields.
  std::string dump_time;
  if (!dict->GetString(kDumpTimeKey, &dump_time))
    return false;
  if (!SetDumpTimeFromString(dump_time))
    return false;

  if (!dict->GetString(kDumpKey, &crashed_process_dump_))
    return false;

  std::string uptime;
  if (!dict->GetString(kUptimeKey, &uptime))
    return false;
  errno = 0;
  params_.process_uptime = strtoull(uptime.c_str(), nullptr, 0);
  if (errno != 0)
    return false;

  if (!dict->GetString(kLogfileKey, &logfile_))
    return false;
  size_t num_params = kNumRequiredParams;

  // Extract all other optional fields.
  const base::ListValue* attachments_list;
  if (dict->GetList(kAttachmentsKey, &attachments_list)) {
    ++num_params;
    for (const auto& attachment : attachments_list->GetList()) {
      attachments_.push_back(attachment.GetString());
    }
  }

  std::string unused_process_name;
  if (dict->GetString(kNameKey, &unused_process_name))
    ++num_params;
  if (dict->GetString(kSuffixKey, &params_.suffix))
    ++num_params;
  if (dict->GetString(kPrevAppNameKey, &params_.previous_app_name))
    ++num_params;
  if (dict->GetString(kCurAppNameKey, &params_.current_app_name))
    ++num_params;
  if (dict->GetString(kLastAppNameKey, &params_.last_app_name))
    ++num_params;
  if (dict->GetString(kReleaseVersionKey, &params_.cast_release_version))
    ++num_params;
  if (dict->GetString(kBuildNumberKey, &params_.cast_build_number))
    ++num_params;
  if (dict->GetString(kReasonKey, &params_.reason))
    ++num_params;
  if (dict->GetString(kStadiaSessionIdKey, &params_.stadia_session_id))
    ++num_params;
  if (dict->GetString(kExecNameKey, &params_.exec_name))
    ++num_params;
  if (dict->GetString(kSignatureKey, &params_.signature))
    ++num_params;
  if (dict->GetString(kExtraInfoKey, &params_.extra_info))
    ++num_params;
  if (dict->GetString(kCrashProductNameKey, &params_.crash_product_name))
    ++num_params;

  // Disallow extraneous params
  if (dict->DictSize() != num_params)
    return false;

  valid_ = true;
  return true;
}

bool DumpInfo::SetDumpTimeFromString(const std::string& timestr) {
  base::Time::Exploded ex = {0};
  if (sscanf(timestr.c_str(), kDumpTimeFormat, &ex.year, &ex.month,
             &ex.day_of_month, &ex.hour, &ex.minute, &ex.second) < 6) {
    LOG(INFO) << "Failed to convert dump time invalid";
    return false;
  }

  return base::Time::FromLocalExploded(ex, &dump_time_);
}

}  // namespace chromecast
