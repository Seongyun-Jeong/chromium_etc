// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include "ash/public/cpp/notification_utils.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/i18n/message_formatter.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chromeos/ui/vector_icons/vector_icons.h"
#include "remoting/base/string_resources.h"
#include "remoting/host/chromeos/message_box.h"
#include "remoting/host/it2me/it2me_confirmation_dialog.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/public/cpp/notification.h"

namespace remoting {

namespace {

constexpr char kEnterpriseNotificationId[] = "CRD_ENTERPRISE_NOTIFICATION";
constexpr char kEnterpriseNotifierId[] = "crd.enterprise_notification";

std::u16string FormatMessage(const std::string& remote_user_email,
                             It2MeConfirmationDialog::DialogStyle style) {
  int message_id = (style == It2MeConfirmationDialog::DialogStyle::kEnterprise
                        ? IDS_SHARE_CONFIRM_DIALOG_MESSAGE_ADMIN_INITIATED
                        : IDS_SHARE_CONFIRM_DIALOG_MESSAGE_WITH_USERNAME);

  return base::i18n::MessageFormatter::FormatWithNumberedArgs(
      l10n_util::GetStringUTF16(message_id),
      base::UTF8ToUTF16(remote_user_email),
      l10n_util::GetStringUTF16(IDS_SHARE_CONFIRM_DIALOG_DECLINE),
      l10n_util::GetStringUTF16(IDS_SHARE_CONFIRM_DIALOG_CONFIRM));
}

}  // namespace

class It2MeConfirmationDialogChromeOS : public It2MeConfirmationDialog {
 public:
  explicit It2MeConfirmationDialogChromeOS(DialogStyle style);

  It2MeConfirmationDialogChromeOS(const It2MeConfirmationDialogChromeOS&) =
      delete;
  It2MeConfirmationDialogChromeOS& operator=(
      const It2MeConfirmationDialogChromeOS&) = delete;

  ~It2MeConfirmationDialogChromeOS() override;

  // It2MeConfirmationDialog implementation.
  void Show(const std::string& remote_user_email,
            ResultCallback callback) override;

 private:
  void ShowConsumerDialog(const std::string& remote_user_email);
  void ShowEnterpriseDialog(const std::string& remote_user_email);

  // Handles result from |message_box_|.
  void OnMessageBoxResult(MessageBox::Result result);
  // Handles result from enterprise notification.
  void OnEnterpriseNotificationResult(absl::optional<int> button_index);

  std::unique_ptr<MessageBox> message_box_;
  ResultCallback callback_;

  DialogStyle style_;
};

It2MeConfirmationDialogChromeOS::It2MeConfirmationDialogChromeOS(
    DialogStyle style)
    : style_(style) {}

It2MeConfirmationDialogChromeOS::~It2MeConfirmationDialogChromeOS() {
  message_center::MessageCenter::Get()->RemoveNotification(
      kEnterpriseNotificationId,
      /*by_user=*/false);
}

void It2MeConfirmationDialogChromeOS::Show(const std::string& remote_user_email,
                                           ResultCallback callback) {
  DCHECK(!remote_user_email.empty());
  callback_ = std::move(callback);

  switch (style_) {
    case DialogStyle::kConsumer:
      ShowConsumerDialog(remote_user_email);
      break;
    case DialogStyle::kEnterprise:
      ShowEnterpriseDialog(remote_user_email);
      break;
  }
}

void It2MeConfirmationDialogChromeOS::ShowConsumerDialog(
    const std::string& remote_user_email) {
  message_box_ = std::make_unique<MessageBox>(
      l10n_util::GetStringUTF16(IDS_MODE_IT2ME),
      FormatMessage(remote_user_email, style_),
      l10n_util::GetStringUTF16(IDS_SHARE_CONFIRM_DIALOG_CONFIRM),
      l10n_util::GetStringUTF16(IDS_SHARE_CONFIRM_DIALOG_DECLINE),
      base::BindOnce(&It2MeConfirmationDialogChromeOS::OnMessageBoxResult,
                     base::Unretained(this)));

  message_box_->Show();
}

void It2MeConfirmationDialogChromeOS::ShowEnterpriseDialog(
    const std::string& remote_user_email) {
  message_center::RichNotificationData data;

  data.pinned = true;

  data.buttons.emplace_back(
      l10n_util::GetStringUTF16(IDS_SHARE_CONFIRM_DIALOG_DECLINE));
  data.buttons.emplace_back(
      l10n_util::GetStringUTF16(IDS_SHARE_CONFIRM_DIALOG_CONFIRM));

  std::unique_ptr<message_center::Notification> notification =
      ash::CreateSystemNotification(
          message_center::NOTIFICATION_TYPE_SIMPLE, kEnterpriseNotificationId,
          l10n_util::GetStringUTF16(IDS_MODE_IT2ME),
          FormatMessage(remote_user_email, style_), u"", GURL(),
          message_center::NotifierId(
              message_center::NotifierType::SYSTEM_COMPONENT,
              kEnterpriseNotifierId),
          data,
          base::MakeRefCounted<message_center::HandleNotificationClickDelegate>(
              base::BindRepeating(&It2MeConfirmationDialogChromeOS::
                                      OnEnterpriseNotificationResult,
                                  base::Unretained(this))),
          chromeos::kEnterpriseIcon,
          message_center::SystemNotificationWarningLevel::NORMAL);

  // Set system priority so the notification is always shown (even in
  // do-not-disturb mode) and it will never time out.
  notification->SetSystemPriority();

  message_center::MessageCenter::Get()->AddNotification(
      std::move(notification));
}

void It2MeConfirmationDialogChromeOS::OnMessageBoxResult(
    MessageBox::Result result) {
  std::move(callback_).Run(result == MessageBox::OK ? Result::OK
                                                    : Result::CANCEL);
}

void It2MeConfirmationDialogChromeOS::OnEnterpriseNotificationResult(
    absl::optional<int> button_index) {
  if (!button_index.has_value())
    return;  // This happens when the user clicks the notification itself.

  // Note: |by_user| must be false, otherwise the notification will not actually
  // be removed but instead it will be moved into the message center bubble
  // (because the message was pinned).
  message_center::MessageCenter::Get()->RemoveNotification(
      kEnterpriseNotificationId,
      /*by_user=*/false);

  std::move(callback_).Run(*button_index == 0 ? Result::CANCEL : Result::OK);
}

std::unique_ptr<It2MeConfirmationDialog>
It2MeConfirmationDialogFactory::Create() {
  return std::make_unique<It2MeConfirmationDialogChromeOS>(dialog_style_);
}

}  // namespace remoting
