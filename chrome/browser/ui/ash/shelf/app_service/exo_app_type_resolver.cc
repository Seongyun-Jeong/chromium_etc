// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/shelf/app_service/exo_app_type_resolver.h"

#include "ash/components/arc/arc_util.h"
#include "ash/constants/app_types.h"
#include "base/strings/string_piece.h"
#include "chrome/browser/ash/borealis/borealis_window_manager.h"
#include "chromeos/crosapi/cpp/crosapi_constants.h"
#include "chromeos/ui/base/window_properties.h"
#include "components/app_restore/app_restore_utils.h"
#include "components/app_restore/window_properties.h"
#include "components/exo/permission.h"
#include "components/exo/shell_surface_util.h"
#include "components/exo/window_properties.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/base/class_property.h"

namespace {

// Returns true, if the given ID represents Lacros.
bool IsLacrosAppId(base::StringPiece app_id) {
  return base::StartsWith(app_id, crosapi::kLacrosAppIdPrefix);
}

}  // namespace

void ExoAppTypeResolver::PopulateProperties(
    const Params& params,
    ui::PropertyHandler& out_properties_container) {
  if (IsLacrosAppId(params.app_id)) {
    out_properties_container.SetProperty(
        aura::client::kAppType, static_cast<int>(ash::AppType::LACROS));
    // Make sure Lacros is treated as opaque for occlusion tracking purposes.
    out_properties_container.SetProperty(
        chromeos::kWindowManagerManagesOpacityKey, true);
    // Lacros is trusted not to abuse window activation, so grant it a
    // non-expiring permission to activate.
    out_properties_container.SetProperty(
        exo::kPermissionKey,
        new exo::Permission(exo::Permission::Capability::kActivate));
    // Only Lacros windows should allow restore/fullscreen to kick windows out
    // of fullscreen.
    out_properties_container.SetProperty(exo::kRestoreOrMaximizeExitsFullscreen,
                                         true);
    out_properties_container.SetProperty(app_restore::kLacrosWindowId,
                                         params.app_id);
  } else if (borealis::BorealisWindowManager::IsBorealisWindowId(
                 params.app_id.empty() ? params.startup_id : params.app_id)) {
    // TODO(b/165865831): Stop using CROSTINI_APP for borealis windows.
    out_properties_container.SetProperty(
        aura::client::kAppType, static_cast<int>(ash::AppType::CROSTINI_APP));

    // Auto-maximize causes compatibility issues, and we don't need it anyway.
    out_properties_container.SetProperty(chromeos::kAutoMaximizeXdgShellEnabled,
                                         false);

    // In some instances we don't want new borealis windows to steal focus,
    // instead they are created as minimized windows.
    // TODO(b/210569001): this is intended to be a temporary solution.
    if (borealis::BorealisWindowManager::ShouldNewWindowBeMinimized()) {
      out_properties_container.SetProperty(aura::client::kShowStateKey,
                                           ui::SHOW_STATE_MINIMIZED);
    }
  }

  auto task_id = arc::GetTaskIdFromWindowAppId(params.app_id);
  auto session_id = arc::GetSessionIdFromWindowAppId(params.app_id);

  // If neither |task_id| nor |session_id| are valid, this is not an ARC window.
  if (!task_id.has_value() && !session_id.has_value())
    return;

  out_properties_container.SetProperty(aura::client::kAppType,
                                       static_cast<int>(ash::AppType::ARC_APP));

  if (task_id.has_value())
    out_properties_container.SetProperty(app_restore::kWindowIdKey, *task_id);

  int32_t restore_window_id = 0;
  if (task_id.has_value()) {
    restore_window_id = app_restore::GetArcRestoreWindowIdForTaskId(*task_id);
  } else {
    DCHECK(session_id.has_value());
    out_properties_container.SetProperty(app_restore::kGhostWindowSessionIdKey,
                                         *session_id);
    restore_window_id =
        app_restore::GetArcRestoreWindowIdForSessionId(*session_id);
  }

  out_properties_container.SetProperty(app_restore::kRestoreWindowIdKey,
                                       restore_window_id);

  if (restore_window_id == app_restore::kParentToHiddenContainer) {
    out_properties_container.SetProperty(
        app_restore::kParentToHiddenContainerKey, true);
  }
}
