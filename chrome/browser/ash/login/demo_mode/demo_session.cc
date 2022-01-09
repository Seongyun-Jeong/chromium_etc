// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/login/demo_mode/demo_session.h"

#include <algorithm>
#include <utility>

#include "ash/constants/ash_switches.h"
#include "ash/public/cpp/locale_update_controller.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/containers/flat_set.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/post_task.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "chrome/browser/apps/app_service/app_launch_params.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/apps/app_service/browser_app_launcher.h"
#include "chrome/browser/apps/platform_apps/app_load_service.h"
#include "chrome/browser/ash/file_manager/path_util.h"
#include "chrome/browser/ash/login/demo_mode/demo_resources.h"
#include "chrome/browser/ash/login/demo_mode/demo_setup_controller.h"
#include "chrome/browser/ash/login/users/chrome_user_manager.h"
#include "chrome/browser/ash/policy/core/browser_policy_connector_ash.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/ash/system_tray_client_impl.h"
#include "chrome/browser/ui/ash/wallpaper_controller_client_impl.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/system/statistics_provider.h"
#include "chromeos/tpm/install_attributes.h"
#include "components/language/core/browser/pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/user_manager/user.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/network_service_instance.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/common/constants.h"
#include "services/network/public/cpp/network_connection_tracker.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/base/l10n/l10n_util.h"

namespace ash {
namespace {

// The splash screen should be removed either when this timeout passes or the
// screensaver app is shown, whichever comes first.
constexpr base::TimeDelta kRemoveSplashScreenTimeout = base::Seconds(10);

// Global DemoSession instance.
DemoSession* g_demo_session = nullptr;

// Type of demo config forced on for tests.
absl::optional<DemoSession::DemoModeConfig> g_force_demo_config;

// Path relative to the path at which offline demo resources are loaded that
// contains the highlights app.
constexpr char kHighlightsAppPath[] = "chrome_apps/highlights";

// Path relative to the path at which offline demo resources are loaded that
// contains sample photos.
constexpr char kPhotosPath[] = "media/photos";

// Path relative to the path at which offline demo resources are loaded that
// contains splash screen images.
constexpr char kSplashScreensPath[] = "media/splash_screens";

// Prefix for the private language tag used to indicate the device's country.
constexpr char kDemoModeCountryPrivateLanguageTagPrefix[] = "x-dm-country-";

// Returns the list of apps normally pinned by Demo Mode policy that shouldn't
// be pinned if the device is offline.
std::vector<std::string> GetIgnorePinPolicyApps() {
  return {
      // Popular third-party game preinstalled in Demo Mode that is
      // online-only, so shouldn't be featured in the shelf when offline.
      "com.pixonic.wwr.chbkdemo",
      // TODO(michaelpg): YouTube is also pinned as a *default* app.
      extension_misc::kYoutubeAppId,
  };
}

// Copies photos into the Downloads directory.
// TODO(michaelpg): Test this behavior (requires overriding the Downloads
// directory).
void InstallDemoMedia(const base::FilePath& offline_resources_path,
                      const base::FilePath& dest_path) {
  if (offline_resources_path.empty()) {
    LOG(ERROR) << "Offline resources not loaded - no media available.";
    return;
  }

  base::FilePath src_path = offline_resources_path.Append(kPhotosPath);
  if (!base::CopyDirectory(src_path, dest_path, false /* recursive */))
    LOG(ERROR) << "Failed to install demo mode media.";
}

std::string GetSwitchOrDefault(const base::StringPiece& switch_string,
                               const std::string& default_value) {
  auto* const command_line = base::CommandLine::ForCurrentProcess();

  if (command_line->HasSwitch(switch_string)) {
    return command_line->GetSwitchValueASCII(switch_string);
  }
  return default_value;
}

std::string GetHighlightsAppId() {
  return GetSwitchOrDefault(switches::kDemoModeHighlightsApp,
                            extension_misc::kHighlightsAppId);
}

// If the current locale is not the default one, ensure it is reverted to the
// default when demo session restarts (i.e. user-selected locale is only allowed
// to be used for a single session).
void RestoreDefaultLocaleForNextSession() {
  auto* user = user_manager::UserManager::Get()->GetActiveUser();
  // Tests may not have an active user.
  if (!user)
    return;
  if (!user->is_profile_created()) {
    user->AddProfileCreatedObserver(
        base::BindOnce(&RestoreDefaultLocaleForNextSession));
    return;
  }
  Profile* profile = ProfileManager::GetActiveUserProfile();
  DCHECK(profile);
  const std::string current_locale =
      profile->GetPrefs()->GetString(language::prefs::kApplicationLocale);
  if (current_locale.empty()) {
    LOG(WARNING) << "Current locale read from kApplicationLocale is empty!";
    return;
  }
  const std::string default_locale =
      g_browser_process->local_state()->GetString(
          prefs::kDemoModeDefaultLocale);
  if (default_locale.empty()) {
    // If the default locale is uninitialized, consider the current locale to be
    // the default. This is safe because users are not allowed to change the
    // locale prior to introduction of this code.
    g_browser_process->local_state()->SetString(prefs::kDemoModeDefaultLocale,
                                                current_locale);
    return;
  }
  if (current_locale != default_locale) {
    // If the user has changed the locale, request to change it back (which will
    // take effect when the session restarts).
    profile->ChangeAppLocale(default_locale,
                             Profile::APP_LOCALE_CHANGED_VIA_DEMO_SESSION);
  }
}

// Returns the list of locales (and related info) supported by demo mode.
std::vector<LocaleInfo> GetSupportedLocales() {
  const base::flat_set<std::string> kSupportedLocales(
      {"da", "de", "en-GB", "en-US", "es", "fi", "fr", "fr-CA", "it", "ja",
       "nb", "nl", "sv"});

  const std::vector<std::string>& available_locales =
      l10n_util::GetUserFacingUILocaleList();
  const std::string current_locale_iso_code =
      ProfileManager::GetActiveUserProfile()->GetPrefs()->GetString(
          language::prefs::kApplicationLocale);
  std::vector<LocaleInfo> supported_locales;
  for (const std::string& locale : available_locales) {
    if (!kSupportedLocales.contains(locale))
      continue;
    LocaleInfo locale_info;
    locale_info.iso_code = locale;
    locale_info.display_name = l10n_util::GetDisplayNameForLocale(
        locale, current_locale_iso_code, true /* is_for_ui */);
    const std::u16string native_display_name =
        l10n_util::GetDisplayNameForLocale(locale, locale,
                                           true /* is_for_ui */);
    if (locale_info.display_name != native_display_name) {
      locale_info.display_name += u" - " + native_display_name;
    }
    supported_locales.push_back(std::move(locale_info));
  }
  return supported_locales;
}

}  // namespace

// static
constexpr char DemoSession::kSupportedCountries[][3];

constexpr char DemoSession::kCountryNotSelectedId[];

// static
std::string DemoSession::DemoConfigToString(
    DemoSession::DemoModeConfig config) {
  switch (config) {
    case DemoSession::DemoModeConfig::kNone:
      return "none";
    case DemoSession::DemoModeConfig::kOnline:
      return "online";
    case DemoSession::DemoModeConfig::kOffline:
      return "offline";
  }
  NOTREACHED() << "Unknown demo mode configuration";
  return std::string();
}

// static
bool DemoSession::IsDeviceInDemoMode() {
  return GetDemoConfig() != DemoModeConfig::kNone;
}

// static
bool DemoSession::IsDemoModeOfflineEnrolled() {
  return DemoSession::IsDeviceInDemoMode() &&
         DemoSession::GetDemoConfig() == DemoSession::DemoModeConfig::kOffline;
}

// static
DemoSession::DemoModeConfig DemoSession::GetDemoConfig() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (g_force_demo_config.has_value())
    return *g_force_demo_config;

  const policy::BrowserPolicyConnectorAsh* const connector =
      g_browser_process->platform_part()->browser_policy_connector_ash();
  bool is_demo_device_mode = connector->GetInstallAttributes()->GetMode() ==
                             policy::DeviceMode::DEVICE_MODE_DEMO;
  bool is_demo_device_domain =
      connector->GetInstallAttributes()->GetDomain() == policy::kDemoModeDomain;

  // TODO(agawronska): We check device mode and domain to allow for dev/test
  // setup that is done by manual enrollment into demo domain. Device mode is
  // not set to DeviceMode::DEVICE_MODE_DEMO then. This extra condition
  // can be removed when all following conditions are fulfilled:
  // * DMServer is returning DeviceMode::DEVICE_MODE_DEMO for demo devices
  // * Offline policies specify DeviceMode::DEVICE_MODE_DEMO
  // * Demo mode setup flow is available to external developers
  bool is_demo_mode = is_demo_device_mode || is_demo_device_domain;

  const PrefService* prefs = g_browser_process->local_state();

  // The testing browser process might not have local state.
  if (!prefs)
    return DemoModeConfig::kNone;

  // Demo mode config preference is set at the end of the demo setup after
  // device is enrolled.
  auto demo_config = DemoModeConfig::kNone;
  int demo_config_pref = prefs->GetInteger(prefs::kDemoModeConfig);
  if (demo_config_pref >= static_cast<int>(DemoModeConfig::kNone) &&
      demo_config_pref <= static_cast<int>(DemoModeConfig::kLast)) {
    demo_config = static_cast<DemoModeConfig>(demo_config_pref);
  }

  if (is_demo_mode && demo_config == DemoModeConfig::kNone) {
    LOG(WARNING) << "Device mode is demo, but no demo mode config set";
  } else if (!is_demo_mode && demo_config != DemoModeConfig::kNone) {
    LOG(WARNING) << "Device mode is not demo, but demo mode config is set";
  }

  return is_demo_mode ? demo_config : DemoModeConfig::kNone;
}

// static
void DemoSession::SetDemoConfigForTesting(DemoModeConfig demo_config) {
  g_force_demo_config = demo_config;
}

// static
void DemoSession::ResetDemoConfigForTesting() {
  g_force_demo_config = absl::nullopt;
}

// static
void DemoSession::PreloadOfflineResourcesIfInDemoMode() {
  if (!IsDeviceInDemoMode())
    return;

  if (!g_demo_session)
    g_demo_session = new DemoSession();
  g_demo_session->EnsureOfflineResourcesLoaded(base::OnceClosure());
}

// static
DemoSession* DemoSession::StartIfInDemoMode() {
  if (!IsDeviceInDemoMode())
    return nullptr;

  if (g_demo_session && g_demo_session->started())
    return g_demo_session;

  if (!g_demo_session)
    g_demo_session = new DemoSession();

  g_demo_session->started_ = true;
  g_demo_session->EnsureOfflineResourcesLoaded(base::OnceClosure());
  return g_demo_session;
}

// static
void DemoSession::ShutDownIfInitialized() {
  if (!g_demo_session)
    return;

  DemoSession* demo_session = g_demo_session;
  g_demo_session = nullptr;
  delete demo_session;
}

// static
DemoSession* DemoSession::Get() {
  return g_demo_session;
}

// static
std::string DemoSession::GetAdditionalLanguageList() {
  return kDemoModeCountryPrivateLanguageTagPrefix +
         base::ToUpperASCII(g_browser_process->local_state()->GetString(
             prefs::kDemoModeCountry));
}

// static
std::string DemoSession::GetScreensaverAppId() {
  return GetSwitchOrDefault(switches::kDemoModeScreensaverApp,
                            extension_misc::kScreensaverAppId);
}

// static
bool DemoSession::ShouldShowExtensionInAppLauncher(const std::string& app_id) {
  if (!IsDeviceInDemoMode())
    return true;
  return app_id != GetScreensaverAppId() &&
         app_id != extensions::kWebStoreAppId;
}

// Static function to default region from VPD.
static std::string GetDefaultRegion() {
  std::string region_code;
  bool found_region_code =
      chromeos::system::StatisticsProvider::GetInstance()->GetMachineStatistic(
          chromeos::system::kRegionKey, &region_code);
  if (found_region_code) {
    return region_code.substr(0, region_code.find("."));
  }
  return "";
}

// static
bool DemoSession::ShouldShowWebApp(const std::string& app_id) {
  if (IsDeviceInDemoMode() &&
      content::GetNetworkConnectionTracker()->IsOffline()) {
    GURL app_id_as_url(app_id);
    return !app_id_as_url.SchemeIsHTTPOrHTTPS();
  }

  return true;
}

// static
base::Value DemoSession::GetCountryList() {
  base::Value country_list(base::Value::Type::LIST);
  std::string region(GetDefaultRegion());
  const std::string current_locale = g_browser_process->GetApplicationLocale();
  bool country_selected = false;

  // TODO(b/203105588): Use the new way of base::Value to create the country
  // list.
  for (const std::string country : kSupportedCountries) {
    base::DictionaryValue dict;
    dict.SetString("value", country);
    dict.SetString(
        "title", l10n_util::GetDisplayNameForCountry(country, current_locale));
    if (country == region) {
      dict.SetBoolean("selected", true);
      g_browser_process->local_state()->SetString(prefs::kDemoModeCountry,
                                                  country);
      country_selected = true;
    } else {
      dict.SetBoolean("selected", false);
    }
    country_list.Append(std::move(dict));
  }
  if (!country_selected) {
    base::DictionaryValue countryNotSelectedDict;
    countryNotSelectedDict.SetString("value",
                                     DemoSession::kCountryNotSelectedId);
    countryNotSelectedDict.SetString(
        "title",
        l10n_util::GetStringUTF16(
            IDS_OOBE_DEMO_SETUP_PREFERENCES_SCREEN_COUNTRY_NOT_SELECTED_TITLE));
    countryNotSelectedDict.SetBoolean("selected", true);
    country_list.Append(std::move(countryNotSelectedDict));
  }
  return country_list;
}

// static
void DemoSession::RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kDemoModeDefaultLocale, std::string());
  registry->RegisterStringPref(prefs::kDemoModeCountry, kSupportedCountries[0]);
}

void DemoSession::EnsureOfflineResourcesLoaded(
    base::OnceClosure load_callback) {
  if (!demo_resources_)
    demo_resources_ = std::make_unique<DemoResources>(GetDemoConfig());
  demo_resources_->EnsureLoaded(std::move(load_callback));
}

// static
void DemoSession::RecordAppLaunchSourceIfInDemoMode(AppLaunchSource source) {
  if (IsDeviceInDemoMode())
    UMA_HISTOGRAM_ENUMERATION("DemoMode.AppLaunchSource", source);
}

bool DemoSession::ShouldShowAndroidOrChromeAppInShelf(
    const std::string& app_id_or_package) {
  if (!g_demo_session || !g_demo_session->started())
    return true;

  // TODO(michaelpg): Update shelf when network status changes.
  // TODO(michaelpg): Also check for captive portal.
  if (!content::GetNetworkConnectionTracker()->IsOffline())
    return true;

  // Ignore for specified chrome/android apps.
  return !base::Contains(ignore_pin_policy_offline_apps_, app_id_or_package);
}

void DemoSession::SetExtensionsExternalLoader(
    scoped_refptr<DemoExtensionsExternalLoader> extensions_external_loader) {
  extensions_external_loader_ = extensions_external_loader;
  if (!offline_enrolled_)
    InstallAppFromUpdateUrl(GetScreensaverAppId());
}

void DemoSession::OverrideIgnorePinPolicyAppsForTesting(
    std::vector<std::string> apps) {
  ignore_pin_policy_offline_apps_ = std::move(apps);
}

void DemoSession::SetTimerForTesting(
    std::unique_ptr<base::OneShotTimer> timer) {
  remove_splash_screen_fallback_timer_ = std::move(timer);
}

base::OneShotTimer* DemoSession::GetTimerForTesting() {
  return remove_splash_screen_fallback_timer_.get();
}

void DemoSession::ActiveUserChanged(user_manager::User* active_user) {
  const base::RepeatingClosure hide_web_store_icon = base::BindRepeating([]() {
    ProfileManager::GetActiveUserProfile()->GetPrefs()->SetBoolean(
        prefs::kHideWebStoreIcon, true);
  });
  active_user->AddProfileCreatedObserver(hide_web_store_icon);
}

DemoSession::DemoSession()
    : offline_enrolled_(IsDemoModeOfflineEnrolled()),
      ignore_pin_policy_offline_apps_(GetIgnorePinPolicyApps()),
      remove_splash_screen_fallback_timer_(
          std::make_unique<base::OneShotTimer>()) {
  // SessionManager may be unset in unit tests.
  if (session_manager::SessionManager::Get()) {
    session_manager_observation_.Observe(
        session_manager::SessionManager::Get());
    OnSessionStateChanged();
  }
  ChromeUserManager::Get()->AddSessionStateObserver(this);
}

DemoSession::~DemoSession() {
  ChromeUserManager::Get()->RemoveSessionStateObserver(this);
}

void DemoSession::InstallDemoResources() {
  DCHECK(demo_resources_->loaded());
  if (offline_enrolled_)
    LoadAndLaunchHighlightsApp();

  Profile* const profile = ProfileManager::GetActiveUserProfile();
  DCHECK(profile);
  const base::FilePath downloads =
      file_manager::util::GetDownloadsFolderForProfile(profile);
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&InstallDemoMedia, demo_resources_->path(), downloads));
}

void DemoSession::LoadAndLaunchHighlightsApp() {
  DCHECK(demo_resources_->loaded());
  if (demo_resources_->path().empty()) {
    LOG(ERROR) << "Offline resources not loaded - no highlights app available.";
    InstallAppFromUpdateUrl(GetHighlightsAppId());
    return;
  }
  Profile* profile = ProfileManager::GetActiveUserProfile();
  DCHECK(profile);
  const base::FilePath resources_path =
      demo_resources_->path().Append(kHighlightsAppPath);
  if (!apps::AppLoadService::Get(profile)->LoadAndLaunch(
          resources_path, base::CommandLine(base::CommandLine::NO_PROGRAM),
          base::FilePath() /* cur_dir */)) {
    LOG(WARNING) << "Failed to launch highlights app from offline resources.";
    InstallAppFromUpdateUrl(GetHighlightsAppId());
  }
}

void DemoSession::InstallAppFromUpdateUrl(const std::string& id) {
  if (!extensions_external_loader_)
    return;
  auto* user = user_manager::UserManager::Get()->GetActiveUser();
  if (!user->is_profile_created()) {
    user->AddProfileCreatedObserver(
        base::BindOnce(&DemoSession::InstallAppFromUpdateUrl,
                       weak_ptr_factory_.GetWeakPtr(), id));
    return;
  }
  Profile* profile = ProfileManager::GetActiveUserProfile();
  DCHECK(profile);
  extensions::AppWindowRegistry* app_window_registry =
      extensions::AppWindowRegistry::Get(profile);
  if (!app_window_registry_observations_.IsObservingSource(app_window_registry))
    app_window_registry_observations_.AddObservation(app_window_registry);
  auto& app_registry_cache =
      apps::AppServiceProxyFactory::GetForProfile(profile)->AppRegistryCache();
  if (!app_registry_cache_observation_.IsObservingSource(&app_registry_cache))
    app_registry_cache_observation_.AddObservation(&app_registry_cache);
  extensions_external_loader_->LoadApp(id);
}

void DemoSession::OnSessionStateChanged() {
  switch (session_manager::SessionManager::Get()->session_state()) {
    case session_manager::SessionState::LOGIN_PRIMARY:
      EnsureOfflineResourcesLoaded(base::BindOnce(
          &DemoSession::ShowSplashScreen, weak_ptr_factory_.GetWeakPtr()));
      break;
    case session_manager::SessionState::ACTIVE:
      if (ShouldRemoveSplashScreen())
        RemoveSplashScreen();

      // SystemTrayClientImpl may not exist in unit tests.
      if (SystemTrayClientImpl::Get()) {
        const std::string current_locale_iso_code =
            ProfileManager::GetActiveUserProfile()->GetPrefs()->GetString(
                language::prefs::kApplicationLocale);
        SystemTrayClientImpl::Get()->SetLocaleList(GetSupportedLocales(),
                                                   current_locale_iso_code);
      }
      RestoreDefaultLocaleForNextSession();

      if (!offline_enrolled_)
        InstallAppFromUpdateUrl(GetHighlightsAppId());

      EnsureOfflineResourcesLoaded(base::BindOnce(
          &DemoSession::InstallDemoResources, weak_ptr_factory_.GetWeakPtr()));
      break;
    default:
      break;
  }
}

void DemoSession::ShowSplashScreen() {
  const std::string current_locale = g_browser_process->GetApplicationLocale();
  base::FilePath image_path = demo_resources_->path()
                                  .Append(kSplashScreensPath)
                                  .Append(current_locale + ".jpg");
  if (!base::PathExists(image_path)) {
    image_path =
        demo_resources_->path().Append(kSplashScreensPath).Append("en-US.jpg");
  }
  WallpaperControllerClientImpl::Get()->ShowAlwaysOnTopWallpaper(image_path);
  remove_splash_screen_fallback_timer_->Start(
      FROM_HERE, kRemoveSplashScreenTimeout,
      base::BindOnce(&DemoSession::RemoveSplashScreen,
                     weak_ptr_factory_.GetWeakPtr()));
}

void DemoSession::RemoveSplashScreen() {
  if (splash_screen_removed_)
    return;
  WallpaperControllerClientImpl::Get()->RemoveAlwaysOnTopWallpaper();
  remove_splash_screen_fallback_timer_.reset();
  app_window_registry_observations_.RemoveAllObservations();
  splash_screen_removed_ = true;
}

bool DemoSession::ShouldRemoveSplashScreen() {
  // TODO(crbug.com/934979): Launch screensaver after active session starts, so
  // that there's no need to check session state here.
  return session_manager::SessionManager::Get()->session_state() ==
             session_manager::SessionState::ACTIVE &&
         screensaver_activated_;
}

void DemoSession::OnAppWindowActivated(extensions::AppWindow* app_window) {
  if (app_window->extension_id() != GetScreensaverAppId())
    return;
  screensaver_activated_ = true;
  if (ShouldRemoveSplashScreen())
    RemoveSplashScreen();
}

void DemoSession::OnAppUpdate(const apps::AppUpdate& update) {
  if (update.AppId() != GetHighlightsAppId())
    return;
  Profile* profile = ProfileManager::GetActiveUserProfile();
  DCHECK(profile);
  apps::AppServiceProxyFactory::GetForProfile(profile)->LaunchAppWithParams(
      apps::AppLaunchParams(
          update.AppId(), apps::mojom::LaunchContainer::kLaunchContainerWindow,
          WindowOpenDisposition::NEW_WINDOW,
          apps::mojom::LaunchSource::kFromChromeInternal));
}

void DemoSession::OnAppRegistryCacheWillBeDestroyed(
    apps::AppRegistryCache* cache) {
  app_registry_cache_observation_.RemoveObservation(cache);
}

}  // namespace ash
