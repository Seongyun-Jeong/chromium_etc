// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/dlp/dlp_notification_helper.h"

#include "chrome/browser/chromeos/policy/dlp/dlp_clipboard_bubble_constants.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/strings/grit/components_strings.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notification_types.h"
#include "ui/message_center/public/cpp/notifier_id.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/public/cpp/new_window_delegate.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/lacros/browser_service_lacros.h"
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

namespace policy {

namespace {

constexpr char kPrintBlockedNotificationId[] = "print_dlp_blocked";
constexpr char kScreenShareBlockedNotificationId[] = "screen_share_dlp_blocked";
constexpr char kScreenSharePausedNotificationPrefix[] =
    "screen_share_dlp_paused-";
constexpr char kScreenShareResumedNotificationPrefix[] =
    "screen_share_dlp_resumed-";
constexpr char kScreenCaptureBlockedNotificationId[] =
    "screen_capture_dlp_blocked";
constexpr char kVideoCaptureStoppedNotificationId[] =
    "video_capture_dlp_stopped";
constexpr char kDlpPolicyNotifierId[] = "policy.dlp";

void OnNotificationClicked(const std::string id) {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  ash::NewWindowDelegate::GetInstance()->OpenUrl(
      GURL(kDlpLearnMoreUrl), /*from_user_interaction=*/true);
#elif BUILDFLAG(IS_CHROMEOS_LACROS)
  auto browser_service = std::make_unique<BrowserServiceLacros>();
  browser_service->OpenUrl(GURL(kDlpLearnMoreUrl), base::DoNothing());
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  NotificationDisplayService::GetForProfile(
      ProfileManager::GetActiveUserProfile())
      ->Close(NotificationHandler::Type::TRANSIENT, id);
}

void ShowDlpNotification(const std::string& id,
                         const std::u16string& title,
                         const std::u16string& message) {
  message_center::Notification notification(
      message_center::NOTIFICATION_TYPE_SIMPLE, id, title, message,
      /*icon=*/gfx::Image(), /*display_source=*/std::u16string(),
      /*origin_url=*/GURL(),
      message_center::NotifierId(message_center::NotifierType::SYSTEM_COMPONENT,
                                 kDlpPolicyNotifierId),
      message_center::RichNotificationData(),
      base::MakeRefCounted<message_center::HandleNotificationClickDelegate>(
          base::BindRepeating(&OnNotificationClicked, id)));
  // Set critical warning color.
  notification.set_accent_color(gfx::kGoogleRed700);
#if BUILDFLAG(IS_CHROMEOS_ASH)
  notification.set_system_notification_warning_level(
      message_center::SystemNotificationWarningLevel::CRITICAL_WARNING);
#endif
  notification.set_vector_small_image(vector_icons::kBusinessIcon);
  notification.set_renotify(true);
  NotificationDisplayService::GetForProfile(
      ProfileManager::GetActiveUserProfile())
      ->Display(NotificationHandler::Type::TRANSIENT, notification,
                /*metadata=*/nullptr);
}

std::string GetScreenSharePausedNotificationId(const std::string& share_id) {
  return kScreenSharePausedNotificationPrefix + share_id;
}

std::string GetScreenShareResumedNotificationId(const std::string& share_id) {
  return kScreenShareResumedNotificationPrefix + share_id;
}

}  // namespace

void ShowDlpPrintDisabledNotification() {
  ShowDlpNotification(
      kPrintBlockedNotificationId,
      l10n_util::GetStringUTF16(IDS_POLICY_DLP_PRINTING_BLOCKED_TITLE),
      l10n_util::GetStringUTF16(IDS_POLICY_DLP_PRINTING_BLOCKED_MESSAGE));
}

void ShowDlpScreenShareDisabledNotification(const std::u16string& app_title) {
  ShowDlpNotification(
      kScreenShareBlockedNotificationId,
      l10n_util::GetStringUTF16(IDS_POLICY_DLP_SCREEN_SHARE_BLOCKED_TITLE),
      l10n_util::GetStringFUTF16(IDS_POLICY_DLP_SCREEN_SHARE_BLOCKED_MESSAGE,
                                 app_title));
}

void HideDlpScreenSharePausedNotification(const std::string& share_id) {
  NotificationDisplayService::GetForProfile(
      ProfileManager::GetActiveUserProfile())
      ->Close(NotificationHandler::Type::TRANSIENT,
              GetScreenSharePausedNotificationId(share_id));
}

void ShowDlpScreenSharePausedNotification(const std::string& share_id,
                                          const std::u16string& app_title) {
  ShowDlpNotification(
      GetScreenSharePausedNotificationId(share_id),
      l10n_util::GetStringUTF16(IDS_POLICY_DLP_SCREEN_SHARE_PAUSED_TITLE),
      l10n_util::GetStringFUTF16(IDS_POLICY_DLP_SCREEN_SHARE_PAUSED_MESSAGE,
                                 app_title));
}

void HideDlpScreenShareResumedNotification(const std::string& share_id) {
  NotificationDisplayService::GetForProfile(
      ProfileManager::GetActiveUserProfile())
      ->Close(NotificationHandler::Type::TRANSIENT,
              GetScreenShareResumedNotificationId(share_id));
}

void ShowDlpScreenShareResumedNotification(const std::string& share_id,
                                           const std::u16string& app_title) {
  ShowDlpNotification(
      GetScreenShareResumedNotificationId(share_id),
      l10n_util::GetStringUTF16(IDS_POLICY_DLP_SCREEN_SHARE_RESUMED_TITLE),
      l10n_util::GetStringFUTF16(IDS_POLICY_DLP_SCREEN_SHARE_RESUMED_MESSAGE,
                                 app_title));
}

void ShowDlpScreenCaptureDisabledNotification() {
  ShowDlpNotification(
      kScreenCaptureBlockedNotificationId,
      l10n_util::GetStringUTF16(IDS_POLICY_DLP_SCREEN_CAPTURE_DISABLED_TITLE),
      l10n_util::GetStringUTF16(
          IDS_POLICY_DLP_SCREEN_CAPTURE_DISABLED_MESSAGE));
}

void ShowDlpVideoCaptureStoppedNotification() {
  ShowDlpNotification(
      kVideoCaptureStoppedNotificationId,
      l10n_util::GetStringUTF16(IDS_POLICY_DLP_VIDEO_CAPTURE_STOPPED_TITLE),
      l10n_util::GetStringUTF16(IDS_POLICY_DLP_VIDEO_CAPTURE_STOPPED_MESSAGE));
}

}  // namespace policy
