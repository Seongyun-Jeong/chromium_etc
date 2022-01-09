// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/app_restore/app_launch_handler.h"

#include <utility>
#include <vector>

#include "apps/launcher.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/apps/app_service/browser_app_launcher.h"
#include "chrome/browser/apps/app_service/metrics/app_platform_metrics.h"
#include "chrome/browser/profiles/profile.h"
#include "components/app_restore/full_restore_read_handler.h"
#include "components/services/app_service/public/cpp/types_util.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"

namespace ash {

namespace {

// Returns apps::AppTypeName used for metrics.
apps::AppTypeName GetHistogrameAppType(apps::mojom::AppType app_type) {
  switch (app_type) {
    case apps::mojom::AppType::kUnknown:
      return apps::AppTypeName::kUnknown;
    case apps::mojom::AppType::kArc:
      return apps::AppTypeName::kArc;
    case apps::mojom::AppType::kBuiltIn:
    case apps::mojom::AppType::kCrostini:
      return apps::AppTypeName::kUnknown;
    case apps::mojom::AppType::kChromeApp:
      return apps::AppTypeName::kChromeApp;
    case apps::mojom::AppType::kWeb:
      return apps::AppTypeName::kWeb;
    case apps::mojom::AppType::kMacOs:
    case apps::mojom::AppType::kPluginVm:
    case apps::mojom::AppType::kStandaloneBrowser:
    case apps::mojom::AppType::kStandaloneBrowserChromeApp:
    case apps::mojom::AppType::kRemote:
    case apps::mojom::AppType::kBorealis:
    case apps::mojom::AppType::kExtension:
      return apps::AppTypeName::kUnknown;
    case apps::mojom::AppType::kSystemWeb:
      return apps::AppTypeName::kSystemWeb;
  }
}

}  // namespace

AppLaunchHandler::AppLaunchHandler(Profile* profile) : profile_(profile) {}

AppLaunchHandler::~AppLaunchHandler() = default;

bool AppLaunchHandler::HasRestoreData() {
  return restore_data_ && !restore_data_->app_id_to_launch_list().empty();
}

void AppLaunchHandler::OnAppUpdate(const apps::AppUpdate& update) {
  if (update.AppId() == extension_misc::kChromeAppId || !restore_data_ ||
      !update.ReadinessChanged()) {
    return;
  }

  if (!apps_util::IsInstalled(update.Readiness())) {
    restore_data_->RemoveApp(update.AppId());
    return;
  }

  // If the app is not ready, don't launch the app for the restoration.
  if (update.Readiness() != apps::mojom::Readiness::kReady)
    return;

  // If there is no restore data or the launch list for the app is empty, don't
  // launch the app.
  const auto& app_id_to_launch_list = restore_data_->app_id_to_launch_list();
  if (app_id_to_launch_list.find(update.AppId()) ==
      app_id_to_launch_list.end()) {
    return;
  }

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&AppLaunchHandler::LaunchApp, GetWeakPtrAppLaunchHandler(),
                     update.AppType(), update.AppId()));
}

void AppLaunchHandler::OnAppTypeInitialized(apps::mojom::AppType app_type) {
  // Do nothing: overridden by subclasses.
}

void AppLaunchHandler::OnAppRegistryCacheWillBeDestroyed(
    apps::AppRegistryCache* cache) {
  apps::AppRegistryCache::Observer::Observe(nullptr);
}

void AppLaunchHandler::LaunchApps() {
  // If there is no launch list from the restore data, we don't need to handle
  // launching.
  const auto& launch_list = restore_data_->app_id_to_launch_list();
  if (launch_list.empty())
    return;

  // Observe AppRegistryCache to get the notification when the app is ready.
  DCHECK(
      apps::AppServiceProxyFactory::IsAppServiceAvailableForProfile(profile_));
  auto* cache = &apps::AppServiceProxyFactory::GetForProfile(profile_)
                     ->AppRegistryCache();
  Observe(cache);
  for (const auto app_type : cache->GetInitializedAppTypes()) {
    OnAppTypeInitialized(app_type);
  }

  // Add the app to `app_ids` if there is a launch list from the restore data
  // for the app.
  std::set<std::string> app_ids;
  cache->ForEachApp([&app_ids, &launch_list](const apps::AppUpdate& update) {
    if (update.Readiness() == apps::mojom::Readiness::kReady &&
        launch_list.find(update.AppId()) != launch_list.end()) {
      app_ids.insert(update.AppId());
    }
  });

  for (const auto& app_id : app_ids) {
    // Chrome browser web pages are restored separately, so we don't need to
    // launch browser windows.
    if (app_id == extension_misc::kChromeAppId)
      continue;

    LaunchApp(cache->GetAppType(app_id), app_id);
  }
}

bool AppLaunchHandler::ShouldLaunchSystemWebAppOrChromeApp(
    const std::string& app_id,
    const ::app_restore::RestoreData::LaunchList& launch_list) {
  return true;
}

void AppLaunchHandler::LaunchApp(apps::mojom::AppType app_type,
                                 const std::string& app_id) {
  DCHECK(restore_data_);
  DCHECK_NE(app_id, extension_misc::kChromeAppId);

  const auto it = restore_data_->app_id_to_launch_list().find(app_id);
  if (it == restore_data_->app_id_to_launch_list().end() ||
      it->second.empty()) {
    restore_data_->RemoveApp(app_id);
    return;
  }

  switch (app_type) {
    case apps::mojom::AppType::kArc:
      // ArcAppLaunchHandler handles ARC apps restoration and ARC apps
      // restoration could be delayed, so return to preserve the restore data
      // for ARC apps.
      return;
    case apps::mojom::AppType::kChromeApp:
    case apps::mojom::AppType::kWeb:
    case apps::mojom::AppType::kSystemWeb:
    case apps::mojom::AppType::kStandaloneBrowserChromeApp:
      if (ShouldLaunchSystemWebAppOrChromeApp(app_id, it->second))
        LaunchSystemWebAppOrChromeApp(app_type, app_id, it->second);
      break;
    case apps::mojom::AppType::kBuiltIn:
    case apps::mojom::AppType::kCrostini:
    case apps::mojom::AppType::kPluginVm:
    case apps::mojom::AppType::kUnknown:
    case apps::mojom::AppType::kMacOs:
    case apps::mojom::AppType::kStandaloneBrowser:
    case apps::mojom::AppType::kRemote:
    case apps::mojom::AppType::kBorealis:
    case apps::mojom::AppType::kExtension:
      NOTREACHED();
      break;
  }
  restore_data_->RemoveApp(app_id);
}

void AppLaunchHandler::LaunchSystemWebAppOrChromeApp(
    apps::mojom::AppType app_type,
    const std::string& app_id,
    const app_restore::RestoreData::LaunchList& launch_list) {
  auto* proxy = apps::AppServiceProxyFactory::GetForProfile(profile_);
  DCHECK(proxy);

  if (app_type == apps::mojom::AppType::kChromeApp)
    OnExtensionLaunching(app_id);

  for (const auto& it : launch_list) {
    RecordRestoredAppLaunch(GetHistogrameAppType(app_type));

    if (it.second->handler_id.has_value()) {
      const extensions::Extension* extension =
          extensions::ExtensionRegistry::Get(profile_)->GetInstalledExtension(
              app_id);
      if (extension) {
        DCHECK(it.second->file_paths.has_value());
        apps::LaunchPlatformAppWithFileHandler(profile_, extension,
                                               it.second->handler_id.value(),
                                               it.second->file_paths.value());
      }
      continue;
    }

    // Desk templates may have partial data. See http://crbug/1232520
    if (!it.second->container.has_value() ||
        !it.second->disposition.has_value() ||
        !it.second->display_id.has_value()) {
      continue;
    }
    apps::mojom::IntentPtr intent;
    apps::AppLaunchParams params(
        app_id,
        static_cast<apps::mojom::LaunchContainer>(it.second->container.value()),
        static_cast<WindowOpenDisposition>(it.second->disposition.value()),
        apps::mojom::LaunchSource::kFromFullRestore,
        it.second->display_id.value(),
        it.second->file_paths.has_value() ? it.second->file_paths.value()
                                          : std::vector<base::FilePath>{},
        it.second->intent.has_value() ? it.second->intent.value() : intent);
    params.restore_id = it.first;
    proxy->LaunchAppWithParams(std::move(params));
  }
}

}  // namespace ash
