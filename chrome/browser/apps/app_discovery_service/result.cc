// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_discovery_service/result.h"

#include <utility>

namespace apps {

PlayExtras* SourceExtras::AsPlayExtras() {
  return nullptr;
}

Result::Result(AppSource app_source,
               const std::string& app_id,
               const std::u16string& app_title,
               std::unique_ptr<SourceExtras> source_extras)
    : app_source_(app_source),
      app_id_(app_id),
      app_title_(app_title),
      source_extras_(std::move(source_extras)) {}

Result::Result(Result&&) = default;

Result& Result::operator=(Result&&) = default;

Result::~Result() = default;

AppSource Result::GetAppSource() const {
  return app_source_;
}

const std::string& Result::GetAppId() const {
  return app_id_;
}

const std::u16string& Result::GetAppTitle() const {
  return app_title_;
}

SourceExtras* Result::GetSourceExtras() const {
  return source_extras_.get();
}

}  // namespace apps
