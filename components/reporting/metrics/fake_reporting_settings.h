// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REPORTING_METRICS_FAKE_REPORTING_SETTINGS_H_
#define COMPONENTS_REPORTING_METRICS_FAKE_REPORTING_SETTINGS_H_

#include <memory>
#include <string>

#include "base/callback_forward.h"
#include "base/callback_list.h"
#include "base/containers/flat_map.h"
#include "base/containers/queue.h"
#include "components/reporting/metrics/reporting_settings.h"

namespace reporting {
namespace test {

// Fake reporting settings for testing.
class FakeReportingSettings : public ReportingSettings {
 public:
  FakeReportingSettings();

  FakeReportingSettings(const FakeReportingSettings& other) = delete;
  FakeReportingSettings& operator=(const FakeReportingSettings& other) = delete;

  ~FakeReportingSettings() override;

  base::CallbackListSubscription AddSettingsObserver(
      const std::string& path,
      base::RepeatingClosure callback) override;

  bool PrepareTrustedValues(base::OnceClosure callback) override;

  bool GetBoolean(const std::string& path, bool* out_value) const override;
  bool GetInteger(const std::string& path, int* out_value) const override;

  void SetBoolean(const std::string& path, bool bool_value);

  void SetInteger(const std::string& path, int int_value);

  void SetIsTrusted(bool is_trusted);

 private:
  base::flat_map<std::string, std::unique_ptr<base::RepeatingClosureList>>
      settings_callbacks_map_;

  base::queue<base::OnceClosure> trusted_callbacks_;

  base::flat_map<std::string, bool> bool_map_;
  base::flat_map<std::string, int> int_map_;

  bool is_trusted_ = true;
};
}  // namespace test
}  // namespace reporting

#endif  // COMPONENTS_REPORTING_METRICS_FAKE_REPORTING_SETTINGS_H_
