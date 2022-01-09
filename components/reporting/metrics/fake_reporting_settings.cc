// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reporting/metrics/fake_reporting_settings.h"

#include "base/containers/contains.h"
#include "base/run_loop.h"
#include "base/threading/sequenced_task_runner_handle.h"

namespace reporting {
namespace test {

FakeReportingSettings::FakeReportingSettings() = default;

FakeReportingSettings::~FakeReportingSettings() = default;

base::CallbackListSubscription FakeReportingSettings::AddSettingsObserver(
    const std::string& path,
    base::RepeatingClosure callback) {
  if (!base::Contains(settings_callbacks_map_, path)) {
    settings_callbacks_map_[path] =
        std::make_unique<base::RepeatingClosureList>();
  }
  return settings_callbacks_map_.at(path)->Add(std::move(callback));
}

bool FakeReportingSettings::PrepareTrustedValues(base::OnceClosure callback) {
  if (!is_trusted_) {
    trusted_callbacks_.push(std::move(callback));
  }
  return is_trusted_;
}

bool FakeReportingSettings::GetBoolean(const std::string& path,
                                       bool* out_value) const {
  if (!base::Contains(bool_map_, path)) {
    return false;
  }
  *out_value = bool_map_.at(path);
  return true;
}

bool FakeReportingSettings::GetInteger(const std::string& path,
                                       int* out_value) const {
  if (!base::Contains(int_map_, path)) {
    return false;
  }
  *out_value = int_map_.at(path);
  return true;
}

void FakeReportingSettings::SetBoolean(const std::string& path,
                                       bool bool_value) {
  bool_map_[path] = bool_value;
  if (base::Contains(settings_callbacks_map_, path)) {
    settings_callbacks_map_.at(path)->Notify();
  }
}

void FakeReportingSettings::SetInteger(const std::string& path, int int_value) {
  int_map_[path] = int_value;
  if (base::Contains(settings_callbacks_map_, path)) {
    settings_callbacks_map_.at(path)->Notify();
  }
}

void FakeReportingSettings::SetIsTrusted(bool is_trusted) {
  base::RunLoop run_loop;
  is_trusted_ = is_trusted;
  while (!trusted_callbacks_.empty()) {
    std::move(trusted_callbacks_.front()).Run();
    trusted_callbacks_.pop();
  }
  base::SequencedTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                   run_loop.QuitClosure());
  run_loop.Run();
}
}  // namespace test
}  // namespace reporting
