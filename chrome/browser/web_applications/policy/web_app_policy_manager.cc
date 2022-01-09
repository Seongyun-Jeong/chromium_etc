// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/policy/web_app_policy_manager.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/containers/contains.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/syslog_logging.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/external_install_options.h"
#include "chrome/browser/web_applications/os_integration_manager.h"
#include "chrome/browser/web_applications/policy/pre_redirection_url_observer.h"
#include "chrome/browser/web_applications/policy/web_app_policy_constants.h"
#include "chrome/browser/web_applications/system_web_apps/system_web_app_manager.h"
#include "chrome/browser/web_applications/web_app_constants.h"
#include "chrome/browser/web_applications/web_app_id_constants.h"
#include "chrome/browser/web_applications/web_app_install_utils.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_sync_bridge.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/blink/public/common/manifest/manifest.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/browser_process.h"
#include "chrome/browser/policy/system_features_disable_list_policy_handler.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "components/policy/core/common/policy_pref_names.h"
#endif  // defined(OS_CHROMEOS)

namespace web_app {

const char WebAppPolicyManager::kInstallResultHistogramName[];

WebAppPolicyManager::WebAppPolicyManager(Profile* profile)
    : profile_(profile),
      pref_service_(profile_->GetPrefs()),
      default_settings_(
          std::make_unique<WebAppPolicyManager::WebAppSetting>()) {}

WebAppPolicyManager::~WebAppPolicyManager() = default;

void WebAppPolicyManager::SetSubsystems(
    ExternallyManagedAppManager* externally_managed_app_manager,
    WebAppRegistrar* app_registrar,
    WebAppSyncBridge* sync_bridge,
    SystemWebAppManager* web_app_manager,
    OsIntegrationManager* os_integration_manager) {
  DCHECK(externally_managed_app_manager);
  DCHECK(app_registrar);
  DCHECK(sync_bridge);
  DCHECK(os_integration_manager);

  externally_managed_app_manager_ = externally_managed_app_manager;
  app_registrar_ = app_registrar;
  sync_bridge_ = sync_bridge;
  web_app_manager_ = web_app_manager;
  os_integration_manager_ = os_integration_manager;
}

void WebAppPolicyManager::Start() {
  // When Lacros is enabled, don't run PWA-specific logic in Ash.
  // TODO(crbug.com/1251491): Consider factoring out logic that should only run
  // in Ash into a separate class. This way, when running in Ash, we won't need
  // to construct a WebAppPolicyManager.
  bool enable_pwa_support = true;
#if BUILDFLAG(IS_CHROMEOS_ASH)
  enable_pwa_support = !IsWebAppsCrosapiEnabled();
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
  content::GetUIThreadTaskRunner({base::TaskPriority::BEST_EFFORT})
      ->PostTask(FROM_HERE,
                 base::BindOnce(
                     &WebAppPolicyManager::InitChangeRegistrarAndRefreshPolicy,
                     weak_ptr_factory_.GetWeakPtr(), enable_pwa_support));
}

void WebAppPolicyManager::ReinstallPlaceholderAppIfNecessary(const GURL& url) {
  const base::Value* web_apps =
      pref_service_->GetList(prefs::kWebAppInstallForceList);
  const auto& web_apps_list = web_apps->GetList();

  const auto it =
      std::find_if(web_apps_list.begin(), web_apps_list.end(),
                   [&url](const base::Value& entry) {
                     return entry.FindKey(kUrlKey)->GetString() == url.spec();
                   });

  if (it == web_apps_list.end())
    return;

  ExternalInstallOptions install_options = ParseInstallPolicyEntry(*it);

  if (!install_options.install_url.is_valid())
    return;

  // No need to install a placeholder because there should be one already.
  install_options.wait_for_windows_closed = true;
  install_options.reinstall_placeholder = true;
  install_options.run_on_os_login =
      (GetUrlRunOnOsLoginPolicy(install_options.install_url) ==
       RunOnOsLoginPolicy::kRunWindowed);

  // If the app is not a placeholder app, ExternallyManagedAppManager will
  // ignore the request.
  externally_managed_app_manager_->InstallNow(std::move(install_options),
                                              base::DoNothing());
}

// static
void WebAppPolicyManager::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(prefs::kWebAppInstallForceList);
  registry->RegisterDictionaryPref(prefs::kWebAppSettings);
}

void WebAppPolicyManager::InitChangeRegistrarAndRefreshPolicy(
    bool enable_pwa_support) {
  pref_change_registrar_.Init(pref_service_);
  if (enable_pwa_support) {
    pref_change_registrar_.Add(
        prefs::kWebAppInstallForceList,
        base::BindRepeating(&WebAppPolicyManager::RefreshPolicyInstalledApps,
                            weak_ptr_factory_.GetWeakPtr()));
    pref_change_registrar_.Add(
        prefs::kWebAppSettings,
        base::BindRepeating(&WebAppPolicyManager::RefreshPolicySettings,
                            weak_ptr_factory_.GetWeakPtr()));

    RefreshPolicySettings();
    RefreshPolicyInstalledApps();
  }
  ObserveDisabledSystemFeaturesPolicy();
}

void WebAppPolicyManager::OnDisableListPolicyChanged() {
#if defined(OS_CHROMEOS)
  PopulateDisabledWebAppsIdsLists();
  std::vector<web_app::AppId> app_ids = app_registrar_->GetAppIds();
  for (const auto& id : app_ids) {
    const bool is_disabled = base::Contains(disabled_web_apps_, id);
    sync_bridge_->SetAppIsDisabled(id, is_disabled);
  }
#endif  // defined(OS_CHROMEOS)
}

const std::set<SystemAppType>& WebAppPolicyManager::GetDisabledSystemWebApps()
    const {
  return disabled_system_apps_;
}

const std::set<AppId>& WebAppPolicyManager::GetDisabledWebAppsIds() const {
  return disabled_web_apps_;
}

bool WebAppPolicyManager::IsWebAppInDisabledList(const AppId& app_id) const {
  return base::Contains(GetDisabledWebAppsIds(), app_id);
}

bool WebAppPolicyManager::IsDisabledAppsModeHidden() const {
#if defined(OS_CHROMEOS)
  PrefService* const local_state = g_browser_process->local_state();
  if (!local_state)  // Sometimes it's not available in tests.
    return false;

  std::string disabled_mode =
      local_state->GetString(policy::policy_prefs::kSystemFeaturesDisableMode);
  if (disabled_mode == policy::kHiddenDisableMode)
    return true;
#endif  // defined(OS_CHROMEOS)
  return false;
}

void WebAppPolicyManager::RefreshPolicyInstalledApps() {
  // If this is called again while in progress, we will run it again once the
  // |SynchronizeInstalledApps| call is finished.
  if (is_refreshing_) {
    needs_refresh_ = true;
    return;
  }

  is_refreshing_ = true;
  needs_refresh_ = false;

  custom_manifest_values_by_url_.clear();

  const base::Value* web_apps =
      pref_service_->GetList(prefs::kWebAppInstallForceList);
  std::vector<ExternalInstallOptions> install_options_list;
  // No need to validate the types or values of the policy members because we
  // are using a SimpleSchemaValidatingPolicyHandler which should validate them
  // for us.
  for (const base::Value& entry : web_apps->GetList()) {
    ExternalInstallOptions install_options = ParseInstallPolicyEntry(entry);

    if (!install_options.install_url.is_valid())
      continue;

    install_options.install_placeholder = true;
    // When the policy gets refreshed, we should try to reinstall placeholder
    // apps but only if they are not being used.
    install_options.wait_for_windows_closed = true;
    install_options.reinstall_placeholder = true;
    install_options.run_on_os_login =
        (GetUrlRunOnOsLoginPolicy(install_options.install_url) ==
         RunOnOsLoginPolicy::kRunWindowed);

    install_options_list.push_back(std::move(install_options));
  }

  externally_managed_app_manager_->SynchronizeInstalledApps(
      std::move(install_options_list), ExternalInstallSource::kExternalPolicy,
      base::BindOnce(&WebAppPolicyManager::OnAppsSynchronized,
                     weak_ptr_factory_.GetWeakPtr()));
}

void WebAppPolicyManager::RefreshPolicySettings() {
  // No need to validate the types or values of the policy members because we
  // are using a SimpleSchemaValidatingPolicyHandler which should validate them
  // for us.
  const base::Value* web_app_dict =
      pref_service_->GetDictionary(prefs::kWebAppSettings);

  settings_by_url_.clear();
  default_settings_ = std::make_unique<WebAppPolicyManager::WebAppSetting>();

  if (!web_app_dict)
    return;

  // Read default policy, if provided.
  const base::Value* default_settings_dict =
      web_app_dict->FindDictKey(kWildcard);
  if (default_settings_dict) {
    if (!default_settings_->Parse(*default_settings_dict, true)) {
      SYSLOG(WARNING) << "Malformed default web app management setting.";
      default_settings_->ResetSettings();
    }
  }

  // Read policy for individual web apps
  for (const auto iter : web_app_dict->DictItems()) {
    if (iter.first == kWildcard)
      continue;

    if (!iter.second.is_dict())
      continue;

    GURL url = GURL(iter.first);
    if (!url.is_valid()) {
      LOG(WARNING) << "Invalid URL: " << iter.first;
      continue;
    }

    WebAppPolicyManager::WebAppSetting by_url(*default_settings_);
    if (by_url.Parse(iter.second, false)) {
      settings_by_url_[url] = by_url;
    } else {
      LOG(WARNING) << "Malformed web app settings for " << url;
    }
  }

  ApplyPolicySettings();

  if (refresh_policy_settings_completed_)
    std::move(refresh_policy_settings_completed_).Run();
}

void WebAppPolicyManager::ApplyPolicySettings() {
  std::map<AppId, GURL> policy_installed_apps =
      app_registrar_->GetExternallyInstalledApps(
          ExternalInstallSource::kExternalPolicy);
  for (const AppId& app_id : app_registrar_->GetAppIds()) {
    RunOnOsLoginPolicy policy =
        GetUrlRunOnOsLoginPolicy(policy_installed_apps[app_id]);
    if (policy == RunOnOsLoginPolicy::kBlocked) {
      sync_bridge_->SetAppRunOnOsLoginMode(app_id, RunOnOsLoginMode::kNotRun);
      OsHooksOptions os_hooks;
      os_hooks[OsHookType::kRunOnOsLogin] = true;
      os_integration_manager_->UninstallOsHooks(app_id, os_hooks,
                                                base::DoNothing());
    } else if (policy == RunOnOsLoginPolicy::kRunWindowed) {
      sync_bridge_->SetAppRunOnOsLoginMode(app_id, RunOnOsLoginMode::kWindowed);
      InstallOsHooksOptions options;
      options.os_hooks[OsHookType::kRunOnOsLogin] = true;
      os_integration_manager_->InstallOsHooks(app_id, base::DoNothing(),
                                              nullptr, options);
    }
  }

  for (WebAppPolicyManagerObserver& observer : observers_)
    observer.OnPolicyChanged();
}

ExternalInstallOptions WebAppPolicyManager::ParseInstallPolicyEntry(
    const base::Value& entry) {
  const base::Value* url = entry.FindKey(kUrlKey);
  // url is a required field and is validated by
  // SimpleSchemaValidatingPolicyHandler. It is guaranteed to exist.
  const GURL gurl(url->GetString());
  const base::Value* default_launch_container =
      entry.FindKey(kDefaultLaunchContainerKey);
  const base::Value* create_desktop_shortcut =
      entry.FindKey(kCreateDesktopShortcutKey);
  const base::Value* fallback_app_name = entry.FindKey(kFallbackAppNameKey);
  const base::Value* custom_name = entry.FindKey(kCustomNameKey);
  const base::Value* custom_icon = entry.FindKey(kCustomIconKey);

  DCHECK(!default_launch_container ||
         default_launch_container->GetString() ==
             kDefaultLaunchContainerWindowValue ||
         default_launch_container->GetString() ==
             kDefaultLaunchContainerTabValue);

  if (!gurl.is_valid()) {
    LOG(WARNING) << "Policy-installed web app has invalid URL " << url;
  }

  DisplayMode user_display_mode;
  if (!default_launch_container) {
    user_display_mode = DisplayMode::kBrowser;
  } else if (default_launch_container->GetString() ==
             kDefaultLaunchContainerTabValue) {
    user_display_mode = DisplayMode::kBrowser;
  } else {
    user_display_mode = DisplayMode::kStandalone;
  }

  ExternalInstallOptions install_options{
      gurl, user_display_mode, ExternalInstallSource::kExternalPolicy};

  install_options.add_to_applications_menu = true;
  install_options.add_to_desktop =
      create_desktop_shortcut ? create_desktop_shortcut->GetBool() : false;
  // Pinning apps to the ChromeOS shelf is done through the PinnedLauncherApps
  // policy.
  install_options.add_to_quick_launch_bar = false;

  // Allow administrators to override the name of the placeholder app, as well
  // as the permanent name for Web Apps without a manifest.
  if (fallback_app_name)
    install_options.fallback_app_name = fallback_app_name->GetString();

  if (custom_name) {
    install_options.placeholder_name = custom_name->GetString();
    if (gurl.is_valid())
      custom_manifest_values_by_url_[gurl].SetName(custom_name->GetString());
  }

  if (custom_icon && custom_icon->is_dict() && gurl.is_valid()) {
    const std::string* icon_url = custom_icon->FindStringKey(kCustomIconURLKey);
    if (icon_url) {
      custom_manifest_values_by_url_[gurl].SetIcon(*icon_url);
    }
  }

  return install_options;
}

void WebAppPolicyManager::AddObserver(WebAppPolicyManagerObserver* observer) {
  observers_.AddObserver(observer);
}

void WebAppPolicyManager::RemoveObserver(
    WebAppPolicyManagerObserver* observer) {
  observers_.RemoveObserver(observer);
}

RunOnOsLoginPolicy WebAppPolicyManager::GetUrlRunOnOsLoginPolicy(
    absl::optional<GURL> url) const {
  if (url) {
    auto it = settings_by_url_.find(url.value());
    if (it != settings_by_url_.end())
      return it->second.run_on_os_login_policy;
  }
  return default_settings_->run_on_os_login_policy;
}

void WebAppPolicyManager::SetOnAppsSynchronizedCompletedCallbackForTesting(
    base::OnceClosure callback) {
  on_apps_synchronized_ = std::move(callback);
}

void WebAppPolicyManager::SetRefreshPolicySettingsCompletedCallbackForTesting(
    base::OnceClosure callback) {
  refresh_policy_settings_completed_ = std::move(callback);
}

// TODO(crbug.com/1243711): Add browser-test for this.
void WebAppPolicyManager::MaybeOverrideManifest(
    content::RenderFrameHost* frame_host,
    blink::mojom::ManifestPtr& manifest) {
#if defined(OS_CHROMEOS)
  if (!manifest)
    return;
  const webapps::PreRedirectionURLObserver* const pre_redirect =
      webapps::PreRedirectionURLObserver::FromWebContents(
          content::WebContents::FromRenderFrameHost(frame_host));
  if (!pre_redirect)
    return;
  GURL last_url = pre_redirect->last_url();
  if (!base::Contains(custom_manifest_values_by_url_, last_url))
    return;
  CustomManifestValues& custom_values =
      custom_manifest_values_by_url_[last_url];
  if (custom_values.name) {
    manifest->name = custom_values.name.value();
  }
  if (custom_values.icons) {
    manifest->icons = custom_values.icons.value();
  }
#endif
}

void WebAppPolicyManager::OnAppsSynchronized(
    std::map<GURL, ExternallyManagedAppManager::InstallResult> install_results,
    std::map<GURL, bool> uninstall_results) {
  is_refreshing_ = false;

  if (!install_results.empty())
    ApplyPolicySettings();

  if (needs_refresh_)
    RefreshPolicyInstalledApps();

  for (const auto& url_and_result : install_results) {
    base::UmaHistogramEnumeration(kInstallResultHistogramName,
                                  url_and_result.second.code);
  }

  if (on_apps_synchronized_)
    std::move(on_apps_synchronized_).Run();
}

WebAppPolicyManager::WebAppSetting::WebAppSetting() {
  ResetSettings();
}

bool WebAppPolicyManager::WebAppSetting::Parse(const base::Value& dict,
                                               bool for_default_settings) {
  const std::string* run_on_os_login_str = dict.FindStringKey(kRunOnOsLogin);
  if (run_on_os_login_str) {
    if (*run_on_os_login_str == kAllowed) {
      run_on_os_login_policy = RunOnOsLoginPolicy::kAllowed;
    } else if (*run_on_os_login_str == kBlocked) {
      run_on_os_login_policy = RunOnOsLoginPolicy::kBlocked;
    } else if (!for_default_settings && *run_on_os_login_str == kRunWindowed) {
      run_on_os_login_policy = RunOnOsLoginPolicy::kRunWindowed;
    } else {
      SYSLOG(WARNING) << "Malformed web app run on os login preference.";
      return false;
    }
  }

  return true;
}

void WebAppPolicyManager::WebAppSetting::ResetSettings() {
  run_on_os_login_policy = RunOnOsLoginPolicy::kAllowed;
}

WebAppPolicyManager::CustomManifestValues::CustomManifestValues() = default;
WebAppPolicyManager::CustomManifestValues::CustomManifestValues(
    const WebAppPolicyManager::CustomManifestValues&) = default;
WebAppPolicyManager::CustomManifestValues::~CustomManifestValues() = default;

void WebAppPolicyManager::CustomManifestValues::SetName(
    const std::string& utf8_name) {
  name = base::UTF8ToUTF16(utf8_name);
}

void WebAppPolicyManager::CustomManifestValues::SetIcon(
    const std::string& icon_url) {
  blink::Manifest::ImageResource icon;

  icon.src = GURL(icon_url);
  icon.sizes.emplace_back(0, 0);  // Represents size "any".
  icon.purpose.push_back(blink::mojom::ManifestImageResource::Purpose::ANY);

  // Initialize icons to only contain icon, possibly resetting icons:
  icons.emplace(1, icon);
}

void WebAppPolicyManager::ObserveDisabledSystemFeaturesPolicy() {
#if defined(OS_CHROMEOS)
  PrefService* const local_state = g_browser_process->local_state();
  if (!local_state) {  // Sometimes it's not available in tests.
    return;
  }
  local_state_pref_change_registrar_.Init(local_state);

  local_state_pref_change_registrar_.Add(
      policy::policy_prefs::kSystemFeaturesDisableList,
      base::BindRepeating(&WebAppPolicyManager::OnDisableListPolicyChanged,
                          base::Unretained(this)));
  local_state_pref_change_registrar_.Add(
      policy::policy_prefs::kSystemFeaturesDisableMode,
      base::BindRepeating(&WebAppPolicyManager::OnDisableModePolicyChanged,
                          base::Unretained(this)));
  // Make sure we get the right disabled mode in case it was changed before
  // policy registration.
  OnDisableModePolicyChanged();
#endif  // defined(OS_CHROMEOS)
}

void WebAppPolicyManager::OnDisableModePolicyChanged() {
#if defined(OS_CHROMEOS)
  sync_bridge_->UpdateAppsDisableMode();
#endif  // defined(OS_CHROMEOS)
}

void WebAppPolicyManager::PopulateDisabledWebAppsIdsLists() {
  disabled_system_apps_.clear();
  disabled_web_apps_.clear();
#if defined(OS_CHROMEOS)
  PrefService* const local_state = g_browser_process->local_state();
  if (!local_state)  // Sometimes it's not available in tests.
    return;

  const base::Value* disabled_system_features_pref =
      local_state->GetList(policy::policy_prefs::kSystemFeaturesDisableList);
  if (!disabled_system_features_pref)
    return;

  for (const auto& entry : disabled_system_features_pref->GetList()) {
    switch (entry.GetInt()) {
      case policy::SystemFeature::kCamera:
        disabled_system_apps_.insert(SystemAppType::CAMERA);
        break;
      case policy::SystemFeature::kOsSettings:
        disabled_system_apps_.insert(SystemAppType::SETTINGS);
        break;
      case policy::SystemFeature::kScanning:
        disabled_system_apps_.insert(SystemAppType::SCANNING);
        break;
      case policy::SystemFeature::kExplore:
        disabled_system_apps_.insert(SystemAppType::HELP);
        break;
      case policy::SystemFeature::kCanvas:
        disabled_web_apps_.insert(web_app::kCanvasAppId);
        break;
    }
  }

  for (const auto& app_type : disabled_system_apps_) {
    absl::optional<AppId> app_id =
        web_app_manager_->GetAppIdForSystemApp(app_type);
    if (app_id.has_value()) {
      disabled_web_apps_.insert(app_id.value());
    }
  }
#endif  // defined(OS_CHROMEOS)
}

}  // namespace web_app
