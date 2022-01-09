// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/arc/session/arc_service_launcher.h"

#include <string>
#include <utility>

#include "ash/components/arc/appfuse/arc_appfuse_bridge.h"
#include "ash/components/arc/arc_features.h"
#include "ash/components/arc/arc_util.h"
#include "ash/components/arc/audio/arc_audio_bridge.h"
#include "ash/components/arc/camera/arc_camera_bridge.h"
#include "ash/components/arc/clipboard/arc_clipboard_bridge.h"
#include "ash/components/arc/compat_mode/arc_resize_lock_manager.h"
#include "ash/components/arc/crash_collector/arc_crash_collector_bridge.h"
#include "ash/components/arc/dark_theme/arc_dark_theme_bridge.h"
#include "ash/components/arc/disk_quota/arc_disk_quota_bridge.h"
#include "ash/components/arc/ime/arc_ime_service.h"
#include "ash/components/arc/keyboard_shortcut/arc_keyboard_shortcut_bridge.h"
#include "ash/components/arc/lock_screen/arc_lock_screen_bridge.h"
#include "ash/components/arc/media_session/arc_media_session_bridge.h"
#include "ash/components/arc/memory_pressure/arc_memory_pressure_bridge.h"
#include "ash/components/arc/metrics/arc_metrics_service.h"
#include "ash/components/arc/midis/arc_midis_bridge.h"
#include "ash/components/arc/net/arc_net_host_impl.h"
#include "ash/components/arc/obb_mounter/arc_obb_mounter_bridge.h"
#include "ash/components/arc/pay/arc_digital_goods_bridge.h"
#include "ash/components/arc/pay/arc_payment_app_bridge.h"
#include "ash/components/arc/power/arc_power_bridge.h"
#include "ash/components/arc/property/arc_property_bridge.h"
#include "ash/components/arc/rotation_lock/arc_rotation_lock_bridge.h"
#include "ash/components/arc/sensor/arc_iio_sensor_bridge.h"
#include "ash/components/arc/sensor/arc_sensor_bridge.h"
#include "ash/components/arc/session/arc_service_manager.h"
#include "ash/components/arc/session/arc_session.h"
#include "ash/components/arc/session/arc_session_runner.h"
#include "ash/components/arc/storage_manager/arc_storage_manager.h"
#include "ash/components/arc/timer/arc_timer_bridge.h"
#include "ash/components/arc/usb/usb_host_bridge.h"
#include "ash/components/arc/volume_mounter/arc_volume_mounter_bridge.h"
#include "ash/components/arc/wake_lock/arc_wake_lock_bridge.h"
#include "ash/constants/ash_features.h"
#include "base/bind.h"
#include "base/check_op.h"
#include "base/files/file_util.h"
#include "chrome/browser/apps/app_service/publishers/arc_apps_factory.h"
#include "chrome/browser/ash/app_restore/app_restore_arc_task_handler.h"
#include "chrome/browser/ash/apps/apk_web_app_service.h"
#include "chrome/browser/ash/arc/accessibility/arc_accessibility_helper_bridge.h"
#include "chrome/browser/ash/arc/adbd/arc_adbd_monitor_bridge.h"
#include "chrome/browser/ash/arc/arc_util.h"
#include "chrome/browser/ash/arc/auth/arc_auth_service.h"
#include "chrome/browser/ash/arc/bluetooth/arc_bluetooth_bridge.h"
#include "chrome/browser/ash/arc/boot_phase_monitor/arc_boot_phase_monitor_bridge.h"
#include "chrome/browser/ash/arc/cast_receiver/arc_cast_receiver_service.h"
#include "chrome/browser/ash/arc/enterprise/arc_enterprise_reporting_service.h"
#include "chrome/browser/ash/arc/enterprise/cert_store/cert_store_service.h"
#include "chrome/browser/ash/arc/file_system_watcher/arc_file_system_watcher_service.h"
#include "chrome/browser/ash/arc/fileapi/arc_file_system_bridge.h"
#include "chrome/browser/ash/arc/fileapi/arc_file_system_mounter.h"
#include "chrome/browser/ash/arc/input_method_manager/arc_input_method_manager_service.h"
#include "chrome/browser/ash/arc/input_overlay/arc_input_overlay_manager.h"
#include "chrome/browser/ash/arc/instance_throttle/arc_instance_throttle.h"
#include "chrome/browser/ash/arc/intent_helper/arc_settings_service.h"
#include "chrome/browser/ash/arc/intent_helper/factory_reset_delegate.h"
#include "chrome/browser/ash/arc/keymaster/arc_keymaster_bridge.h"
#include "chrome/browser/ash/arc/kiosk/arc_kiosk_bridge.h"
#include "chrome/browser/ash/arc/metrics/arc_metrics_service_proxy.h"
#include "chrome/browser/ash/arc/nearby_share/arc_nearby_share_bridge.h"
#include "chrome/browser/ash/arc/notification/arc_boot_error_notification.h"
#include "chrome/browser/ash/arc/notification/arc_provision_notification_service.h"
#include "chrome/browser/ash/arc/oemcrypto/arc_oemcrypto_bridge.h"
#include "chrome/browser/ash/arc/pip/arc_pip_bridge.h"
#include "chrome/browser/ash/arc/policy/arc_policy_bridge.h"
#include "chrome/browser/ash/arc/print_spooler/arc_print_spooler_bridge.h"
#include "chrome/browser/ash/arc/process/arc_process_service.h"
#include "chrome/browser/ash/arc/screen_capture/arc_screen_capture_bridge.h"
#include "chrome/browser/ash/arc/session/arc_demo_mode_preference_handler.h"
#include "chrome/browser/ash/arc/session/arc_play_store_enabled_preference_handler.h"
#include "chrome/browser/ash/arc/session/arc_session_manager.h"
#include "chrome/browser/ash/arc/sharesheet/arc_sharesheet_bridge.h"
#include "chrome/browser/ash/arc/survey/arc_survey_service.h"
#include "chrome/browser/ash/arc/tracing/arc_app_performance_tracing.h"
#include "chrome/browser/ash/arc/tracing/arc_tracing_bridge.h"
#include "chrome/browser/ash/arc/tts/arc_tts_service.h"
#include "chrome/browser/ash/arc/usb/arc_usb_host_bridge_delegate.h"
#include "chrome/browser/ash/arc/user_session/arc_user_session_service.h"
#include "chrome/browser/ash/arc/video/gpu_arc_video_service_host.h"
#include "chrome/browser/ash/arc/wallpaper/arc_wallpaper_service.h"
#include "chrome/browser/ash/login/startup_utils.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/arc/arc_usb_host_permission_manager.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/common/channel_info.h"
#include "components/arc/intent_helper/arc_intent_helper_bridge.h"
#include "components/prefs/pref_member.h"
#include "mojo/public/cpp/bindings/pending_remote.h"

namespace arc {
namespace {

// `ChromeBrowserMainPartsAsh` owns.
ArcServiceLauncher* g_arc_service_launcher = nullptr;

std::unique_ptr<ArcSessionManager> CreateArcSessionManager(
    ArcBridgeService* arc_bridge_service,
    version_info::Channel channel,
    chromeos::SchedulerConfigurationManagerBase*
        scheduler_configuration_manager) {
  auto delegate = std::make_unique<AdbSideloadingAvailabilityDelegateImpl>();
  auto runner = std::make_unique<ArcSessionRunner>(
      base::BindRepeating(ArcSession::Create, arc_bridge_service, channel,
                          scheduler_configuration_manager, delegate.get()));
  return std::make_unique<ArcSessionManager>(std::move(runner),
                                             std::move(delegate));
}

}  // namespace

ArcServiceLauncher::ArcServiceLauncher(
    chromeos::SchedulerConfigurationManagerBase*
        scheduler_configuration_manager)
    : arc_service_manager_(std::make_unique<ArcServiceManager>()),
      arc_session_manager_(
          CreateArcSessionManager(arc_service_manager_->arc_bridge_service(),
                                  chrome::GetChannel(),
                                  scheduler_configuration_manager)),
      scheduler_configuration_manager_(scheduler_configuration_manager) {
  DCHECK(g_arc_service_launcher == nullptr);
  g_arc_service_launcher = this;

  if (!ash::StartupUtils::IsOobeCompleted()) {
    arc_demo_mode_preference_handler_ =
        ArcDemoModePreferenceHandler::Create(arc_session_manager_.get());
  }
}

ArcServiceLauncher::~ArcServiceLauncher() {
  DCHECK_EQ(g_arc_service_launcher, this);
  g_arc_service_launcher = nullptr;
}

// static
ArcServiceLauncher* ArcServiceLauncher::Get() {
  return g_arc_service_launcher;
}

void ArcServiceLauncher::Initialize() {
  arc_session_manager_->ExpandPropertyFilesAndReadSalt();
}

void ArcServiceLauncher::MaybeSetProfile(Profile* profile) {
  if (!IsArcAllowedForProfile(profile))
    return;

  // Do not expect it in real use case, but it is used for testing.
  // Because the ArcService instances tied to the old profile is kept,
  // and ones tied to the new profile are added, which is unexpected situation.
  // For compatibility, call Shutdown() here in case |profile| is not
  // allowed for ARC.
  // TODO(hidehiko): DCHECK(!arc_session_manager_->IsAllowed()) here, and
  // get rid of Shutdown().
  if (arc_session_manager_->profile())
    arc_session_manager_->Shutdown();

  arc_session_manager_->SetProfile(profile);
  arc_service_manager_->set_browser_context(profile);
  arc_service_manager_->set_account_id(
      multi_user_util::GetAccountIdFromProfile(profile));
}

void ArcServiceLauncher::OnPrimaryUserProfilePrepared(Profile* profile) {
  DCHECK(arc_service_manager_);
  DCHECK(arc_session_manager_);

  if (arc_session_manager_->profile() != profile) {
    // Profile is not matched, so the given |profile| is not allowed to use
    // ARC.
    return;
  }

  // Instantiate ARC related BrowserContextKeyedService classes which need
  // to be running at the beginning of the container run.
  // Note that, to keep this list as small as possible, services which don't
  // need to be initialized at the beginning should not be listed here.
  // Those services will be initialized lazily.
  // List in lexicographical order.
  ArcAccessibilityHelperBridge::GetForBrowserContext(profile);
  ArcAdbdMonitorBridge::GetForBrowserContext(profile);
  ArcAppPerformanceTracing::GetForBrowserContext(profile);
  ArcAudioBridge::GetForBrowserContext(profile);
  ArcAuthService::GetForBrowserContext(profile);
  ArcBluetoothBridge::GetForBrowserContext(profile);
  ArcBootErrorNotification::GetForBrowserContext(profile);
  ArcBootPhaseMonitorBridge::GetForBrowserContext(profile);
  ArcCameraBridge::GetForBrowserContext(profile);
  ArcCastReceiverService::GetForBrowserContext(profile);
  ArcClipboardBridge::GetForBrowserContext(profile);
  ArcCrashCollectorBridge::GetForBrowserContext(profile);
  ArcDarkThemeBridge::GetForBrowserContext(profile);
  ArcDigitalGoodsBridge::GetForBrowserContext(profile);
  ArcDiskQuotaBridge::GetForBrowserContext(profile)->SetAccountId(
      multi_user_util::GetAccountIdFromProfile(profile));
  ArcEnterpriseReportingService::GetForBrowserContext(profile);
  ArcFileSystemBridge::GetForBrowserContext(profile);
  ArcFileSystemMounter::GetForBrowserContext(profile);
  ArcFileSystemWatcherService::GetForBrowserContext(profile);
  ArcIioSensorBridge::GetForBrowserContext(profile);
  ArcImeService::GetForBrowserContext(profile);
  ArcInputMethodManagerService::GetForBrowserContext(profile);
  if (ash::features::IsArcInputOverlayEnabled())
    ArcInputOverlayManager::GetForBrowserContext(profile);
  ArcInstanceThrottle::GetForBrowserContext(profile);
  ArcIntentHelperBridge::GetForBrowserContext(profile)->SetDelegate(
      std::make_unique<FactoryResetDelegate>());
  ArcKeyboardShortcutBridge::GetForBrowserContext(profile);
  ArcKeymasterBridge::GetForBrowserContext(profile);
  ArcKioskBridge::GetForBrowserContext(profile);
  ArcLockScreenBridge::GetForBrowserContext(profile);
  ArcMediaSessionBridge::GetForBrowserContext(profile);
  ArcMetricsService::GetForBrowserContext(profile)->SetHistogramNamer(
      base::BindRepeating([](const std::string& base_name) {
        return GetHistogramNameByUserTypeForPrimaryProfile(base_name);
      }));
  ArcMetricsServiceProxy::GetForBrowserContext(profile);
  ArcMidisBridge::GetForBrowserContext(profile);
  ArcNearbyShareBridge::GetForBrowserContext(profile);
  ArcNetHostImpl::GetForBrowserContext(profile)->SetPrefService(
      profile->GetPrefs());
  ArcOemCryptoBridge::GetForBrowserContext(profile);
  ArcPaymentAppBridge::GetForBrowserContext(profile);
  ArcPipBridge::GetForBrowserContext(profile);
  ArcPolicyBridge::GetForBrowserContext(profile);
  ArcPowerBridge::GetForBrowserContext(profile)->SetUserIdHash(
      chromeos::ProfileHelper::GetUserIdHashFromProfile(profile));
  ArcPrintSpoolerBridge::GetForBrowserContext(profile);
  ArcProcessService::GetForBrowserContext(profile);
  ArcPropertyBridge::GetForBrowserContext(profile);
  ArcProvisionNotificationService::GetForBrowserContext(profile);
  ArcResizeLockManager::GetForBrowserContext(profile);
  ArcRotationLockBridge::GetForBrowserContext(profile);
  ArcScreenCaptureBridge::GetForBrowserContext(profile);
  ArcSensorBridge::GetForBrowserContext(profile);
  ArcSettingsService::GetForBrowserContext(profile);
  ArcSharesheetBridge::GetForBrowserContext(profile);
  ArcSurveyService::GetForBrowserContext(profile);
  ArcTimerBridge::GetForBrowserContext(profile);
  ArcTracingBridge::GetForBrowserContext(profile);
  ArcTtsService::GetForBrowserContext(profile);
  ArcUsbHostBridge::GetForBrowserContext(profile)->SetDelegate(
      std::make_unique<ArcUsbHostBridgeDelegate>());
  ArcUsbHostPermissionManager::GetForBrowserContext(profile);
  ArcUserSessionService::GetForBrowserContext(profile);
  ArcVolumeMounterBridge::GetForBrowserContext(profile);
  ArcWakeLockBridge::GetForBrowserContext(profile);
  ArcWallpaperService::GetForBrowserContext(profile);
  GpuArcVideoKeyedService::GetForBrowserContext(profile);
  CertStoreService::GetForBrowserContext(profile);
  apps::ArcAppsFactory::GetForProfile(profile);
  ash::ApkWebAppService::Get(profile);
  ash::app_restore::AppRestoreArcTaskHandler::GetForProfile(profile);

  if (arc::IsArcVmEnabled()) {
    // ARCVM-only services.
    if (base::FeatureList::IsEnabled(kVmBalloonPolicy))
      ArcMemoryPressureBridge::GetForBrowserContext(profile);
  } else {
    // ARC Container-only services.
    ArcAppfuseBridge::GetForBrowserContext(profile);
    ArcObbMounterBridge::GetForBrowserContext(profile);
  }

  arc_session_manager_->Initialize();
  arc_play_store_enabled_preference_handler_ =
      std::make_unique<ArcPlayStoreEnabledPreferenceHandler>(
          profile, arc_session_manager_.get());
  arc_play_store_enabled_preference_handler_->Start();
}

void ArcServiceLauncher::Shutdown() {
  arc_play_store_enabled_preference_handler_.reset();
  arc_session_manager_->Shutdown();
}

void ArcServiceLauncher::ResetForTesting() {
  // First destroy the internal states, then re-initialize them.
  // These are for workaround of singletonness DCHECK in their ctors/dtors.
  Shutdown();
  arc_session_manager_.reset();

  // No recreation of arc_service_manager. Pointers to its ArcBridgeService
  // may be referred from existing KeyedService, so destoying it would cause
  // unexpected behavior, specifically on test teardown.
  arc_session_manager_ = CreateArcSessionManager(
      arc_service_manager_->arc_bridge_service(), chrome::GetChannel(),
      scheduler_configuration_manager_);
}

}  // namespace arc
