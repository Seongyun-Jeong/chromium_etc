// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/linux/linux_desktop.h"

#include <vector>

#include "base/environment.h"
#include "base/nix/xdg_util.h"
#include "base/strings/string_piece.h"
#include "base/values.h"
#include "ui/display/util/gpu_info_util.h"

namespace ui {

std::vector<base::Value> GetDesktopEnvironmentInfo() {
  std::vector<base::Value> result;
  auto env(base::Environment::Create());
  std::string value;
  if (env->GetVar(base::nix::kXdgCurrentDesktopEnvVar, &value)) {
    result.push_back(
        display::BuildGpuInfoEntry(base::nix::kXdgCurrentDesktopEnvVar, value));
  }
  if (env->GetVar(base::nix::kXdgSessionTypeEnvVar, &value)) {
    result.push_back(
        display::BuildGpuInfoEntry(base::nix::kXdgSessionTypeEnvVar, value));
  }
  constexpr char kGDMSession[] = "GDMSESSION";
  if (env->GetVar(kGDMSession, &value))
    result.push_back(display::BuildGpuInfoEntry(kGDMSession, value));
  return result;
}

}  // namespace ui
