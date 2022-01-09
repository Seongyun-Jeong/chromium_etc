// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/message_center/message_center_impl.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/observer_list.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "ui/message_center/lock_screen/lock_screen_controller.h"
#include "ui/message_center/message_center_types.h"
#include "ui/message_center/notification_blocker.h"
#include "ui/message_center/notification_list.h"
#include "ui/message_center/popup_timers_controller.h"
#include "ui/message_center/public/cpp/message_center_constants.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notification_types.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_features.h"
#endif

namespace message_center {

////////////////////////////////////////////////////////////////////////////////
// MessageCenterImpl

MessageCenterImpl::MessageCenterImpl(
    std::unique_ptr<LockScreenController> lock_screen_controller)
    : lock_screen_controller_(std::move(lock_screen_controller)),
      popup_timers_controller_(std::make_unique<PopupTimersController>(this)),
      stats_collector_(this) {
  notification_list_ = std::make_unique<NotificationList>(this);
#if BUILDFLAG(IS_CHROMEOS_ASH)
  notifications_grouping_enabled_ =
      ash::features::IsNotificationsRefreshEnabled();
#endif
}

MessageCenterImpl::~MessageCenterImpl() = default;

void MessageCenterImpl::AddObserver(MessageCenterObserver* observer) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  observer_list_.AddObserver(observer);
}

void MessageCenterImpl::RemoveObserver(MessageCenterObserver* observer) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  observer_list_.RemoveObserver(observer);
}

void MessageCenterImpl::AddNotificationBlocker(NotificationBlocker* blocker) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (base::Contains(blockers_, blocker))
    return;

  blocker->AddObserver(this);
  blockers_.push_back(blocker);
}

void MessageCenterImpl::RemoveNotificationBlocker(
    NotificationBlocker* blocker) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto iter = std::find(blockers_.begin(), blockers_.end(), blocker);
  if (iter == blockers_.end())
    return;
  blocker->RemoveObserver(this);
  blockers_.erase(iter);
}

void MessageCenterImpl::OnBlockingStateChanged(NotificationBlocker* blocker) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::list<std::string> blocked;
  NotificationList::PopupNotifications popups =
      notification_list_->GetPopupNotifications(blockers_, &blocked);

  visible_notifications_ =
      notification_list_->GetVisibleNotifications(blockers_);

  for (const std::string& notification_id : blocked) {
    for (MessageCenterObserver& observer : observer_list_)
      observer.OnNotificationUpdated(notification_id);
  }
  for (MessageCenterObserver& observer : observer_list_)
    observer.OnBlockingStateChanged(blocker);
}

void MessageCenterImpl::SetVisibility(Visibility visibility) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  visible_ = (visibility == VISIBILITY_MESSAGE_CENTER);

  if (visible_) {
    std::set<std::string> updated_ids;
    notification_list_->SetNotificationsShown(blockers_, &updated_ids);

    for (const auto& id : updated_ids) {
      for (MessageCenterObserver& observer : observer_list_)
        observer.OnNotificationUpdated(id);
    }

    for (Notification* notification : GetPopupNotifications())
      MarkSinglePopupAsShown(notification->id(), false);
  }

  for (MessageCenterObserver& observer : observer_list_)
    observer.OnCenterVisibilityChanged(visibility);
}

bool MessageCenterImpl::IsMessageCenterVisible() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return visible_;
}

void MessageCenterImpl::SetHasMessageCenterView(bool has_message_center_view) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  has_message_center_view_ = has_message_center_view;
}

bool MessageCenterImpl::HasMessageCenterView() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return has_message_center_view_;
}

size_t MessageCenterImpl::NotificationCount() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return visible_notifications_.size();
}

bool MessageCenterImpl::HasPopupNotifications() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return !IsMessageCenterVisible() &&
      notification_list_->HasPopupNotifications(blockers_);
}

bool MessageCenterImpl::IsQuietMode() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return notification_list_->quiet_mode();
}

bool MessageCenterImpl::IsSpokenFeedbackEnabled() const {
  return spoken_feedback_enabled_;
}

Notification* MessageCenterImpl::FindNotificationById(const std::string& id) {
  return notification_list_->GetNotificationById(id);
}

Notification* MessageCenterImpl::FindParentNotificationForOriginUrl(
    const GURL& origin_url) {
  if (origin_url.is_empty())
    return nullptr;

  NotificationList::Notifications notifications =
      notification_list_->GetNotificationsByOriginUrl(origin_url);

  if (notifications.size())
    return *std::prev(notifications.end());
  return nullptr;
}

Notification* MessageCenterImpl::FindPopupNotificationById(
    const std::string& id) {
  auto id_match = [&id](Notification* notification) {
    return id == notification->id();
  };
  auto notifications = GetPopupNotifications();
  auto notification =
      std::find_if(notifications.begin(), notifications.end(), id_match);

  return notification == notifications.end() ? nullptr : *notification;
}

Notification* MessageCenterImpl::FindVisibleNotificationById(
    const std::string& id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  const auto& notifications = GetVisibleNotifications();
  for (Notification* notification : notifications) {
    if (notification->id() == id)
      return notification;
  }

  return nullptr;
}

NotificationList::Notifications MessageCenterImpl::FindNotificationsByAppId(
    const std::string& app_id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return notification_list_->GetNotificationsByAppId(app_id);
}

NotificationList::Notifications MessageCenterImpl::GetNotifications() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return notification_list_->GetNotifications();
}

const NotificationList::Notifications&
MessageCenterImpl::GetVisibleNotifications() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return visible_notifications_;
}

NotificationList::PopupNotifications
MessageCenterImpl::GetPopupNotifications() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return notification_list_->GetPopupNotifications(blockers_, nullptr);
}

//------------------------------------------------------------------------------
// Client code interface.
void MessageCenterImpl::AddNotification(
    std::unique_ptr<Notification> notification) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(notification);

  notification->set_allow_group(notifications_grouping_enabled_);

  const std::string id = notification->id();
  for (NotificationBlocker* blocker : blockers_)
    blocker->CheckState();

  // Sometimes the notification can be added with the same id and the
  // |notification_list| will replace the notification instead of adding new.
  // This is essentially an update rather than addition.
  bool already_exists = notification_list_->GetNotificationById(id) != nullptr;
  if (already_exists) {
    UpdateNotification(id, std::move(notification));
    return;
  }

  auto* parent = FindParentNotificationForOriginUrl(notification->origin_url());
  if (notification->allow_group() && parent) {
    parent->SetGroupParent();
    notification->SetGroupChild();
  }

  notification_list_->AddNotification(std::move(notification));
  visible_notifications_ =
      notification_list_->GetVisibleNotifications(blockers_);
  for (MessageCenterObserver& observer : observer_list_)
    observer.OnNotificationAdded(id);
}

void MessageCenterImpl::UpdateNotification(
    const std::string& old_id,
    std::unique_ptr<Notification> new_notification) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  for (NotificationBlocker* blocker : blockers_)
    blocker->CheckState();

  std::string new_id = new_notification->id();
  notification_list_->UpdateNotificationMessage(old_id,
                                                std::move(new_notification));
  visible_notifications_ =
      notification_list_->GetVisibleNotifications(blockers_);
  for (MessageCenterObserver& observer : observer_list_) {
    if (old_id == new_id) {
      observer.OnNotificationUpdated(new_id);
    } else {
      observer.OnNotificationRemoved(old_id, false);
      observer.OnNotificationAdded(new_id);
    }
  }
}

void MessageCenterImpl::RemoveNotification(const std::string& id,
                                           bool by_user) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  Notification* notification = notification_list_->GetNotificationById(id);
  if (!notification)
    return;

  if (by_user && notification->pinned()) {
    // When pinned, a popup will not be removed completely but moved into the
    // message center bubble.
    MarkSinglePopupAsShown(id, true);
    return;
  }

  // In many cases |id| is a reference to an existing notification instance
  // but the instance can be destructed in this method. Hence copies the id
  // explicitly here.
  std::string copied_id(id);

  scoped_refptr<NotificationDelegate> delegate =
      notification_list_->GetNotificationDelegate(copied_id);

  // Remove notification before calling the Close method in case it calls
  // RemoveNotification reentrantly.
  notification_list_->RemoveNotification(copied_id);

  if (delegate.get())
    delegate->Close(by_user);

  visible_notifications_ =
      notification_list_->GetVisibleNotifications(blockers_);
  for (MessageCenterObserver& observer : observer_list_)
    observer.OnNotificationRemoved(copied_id, by_user);
}

void MessageCenterImpl::RemoveNotificationsForNotifierId(
    const NotifierId& notifier_id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  NotificationList::Notifications notifications =
      notification_list_->GetNotificationsByNotifierId(notifier_id);
  for (Notification* notification : notifications)
    RemoveNotification(notification->id(), false);
  if (!notifications.empty()) {
    visible_notifications_ =
        notification_list_->GetVisibleNotifications(blockers_);
  }
}

void MessageCenterImpl::RemoveAllNotifications(bool by_user, RemoveType type) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  bool remove_pinned = (type == RemoveType::ALL);

  const NotificationBlockers& blockers =
      remove_pinned ? NotificationBlockers() /* empty blockers */
                    : blockers_;             /* use default blockers */

  const NotificationList::Notifications notifications =
      notification_list_->GetVisibleNotifications(blockers);
  std::set<std::string> ids;
  for (Notification* notification : notifications) {
    if (!remove_pinned && notification->pinned())
      continue;

    ids.insert(notification->id());
    scoped_refptr<NotificationDelegate> delegate = notification->delegate();

    // Remove notification before calling the Close method in case it calls
    // RemoveNotification reentrantly.
    notification_list_->RemoveNotification(notification->id());

    if (delegate.get())
      delegate->Close(by_user);
  }

  if (!ids.empty()) {
    visible_notifications_ =
        notification_list_->GetVisibleNotifications(blockers_);
  }
  for (const auto& id : ids) {
    for (MessageCenterObserver& observer : observer_list_)
      observer.OnNotificationRemoved(id, by_user);
  }
}

void MessageCenterImpl::SetNotificationIcon(const std::string& notification_id,
                                            const gfx::Image& image) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (notification_list_->SetNotificationIcon(notification_id, image)) {
    for (MessageCenterObserver& observer : observer_list_)
      observer.OnNotificationUpdated(notification_id);
  }
}

void MessageCenterImpl::SetNotificationImage(const std::string& notification_id,
                                             const gfx::Image& image) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (notification_list_->SetNotificationImage(notification_id, image)) {
    for (MessageCenterObserver& observer : observer_list_)
      observer.OnNotificationUpdated(notification_id);
  }
}

void MessageCenterImpl::ClickOnNotification(const std::string& id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!FindVisibleNotificationById(id))
    return;

  lock_screen_controller_->DismissLockScreenThenExecute(
      base::BindOnce(&MessageCenterImpl::ClickOnNotificationUnlocked,
                     base::Unretained(this), id, absl::nullopt, absl::nullopt),
      base::OnceClosure());
}

void MessageCenterImpl::ClickOnNotificationButton(const std::string& id,
                                                  int button_index) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!FindVisibleNotificationById(id))
    return;

  lock_screen_controller_->DismissLockScreenThenExecute(
      base::BindOnce(&MessageCenterImpl::ClickOnNotificationUnlocked,
                     base::Unretained(this), id, button_index, absl::nullopt),
      base::OnceClosure());
}

void MessageCenterImpl::ClickOnNotificationButtonWithReply(
    const std::string& id,
    int button_index,
    const std::u16string& reply) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!FindVisibleNotificationById(id))
    return;

  lock_screen_controller_->DismissLockScreenThenExecute(
      base::BindOnce(&MessageCenterImpl::ClickOnNotificationUnlocked,
                     base::Unretained(this), id, button_index, reply),
      base::OnceClosure());
}

void MessageCenterImpl::ClickOnNotificationUnlocked(
    const std::string& id,
    const absl::optional<int>& button_index,
    const absl::optional<std::u16string>& reply) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // This method must be called under unlocked screen.
  DCHECK(!lock_screen_controller_->IsScreenLocked());

  // Ensure the notification is still visible.
  if (!FindVisibleNotificationById(id))
    return;

  if (HasMessageCenterView() && HasPopupNotifications())
    MarkSinglePopupAsShown(id, true);
  for (MessageCenterObserver& observer : observer_list_)
    observer.OnNotificationClicked(id, button_index, reply);

  scoped_refptr<NotificationDelegate> delegate =
      notification_list_->GetNotificationDelegate(id);
  if (delegate)
    delegate->Click(button_index, reply);
}

void MessageCenterImpl::ClickOnSettingsButton(const std::string& id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  Notification* notification = notification_list_->GetNotificationById(id);

  bool handled_by_delegate =
      notification &&
      (notification->rich_notification_data().settings_button_handler ==
       SettingsButtonHandler::DELEGATE);
  if (handled_by_delegate)
    notification->delegate()->SettingsClick();

  for (MessageCenterObserver& observer : observer_list_)
    observer.OnNotificationSettingsClicked(handled_by_delegate);
}

void MessageCenterImpl::DisableNotification(const std::string& id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  Notification* notification = notification_list_->GetNotificationById(id);

  if (notification && notification->delegate()) {
    notification->delegate()->DisableNotification();
    RemoveNotificationsForNotifierId(notification->notifier_id());
  }
}

void MessageCenterImpl::MarkSinglePopupAsShown(const std::string& id,
                                               bool mark_notification_as_read) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!FindVisibleNotificationById(id))
    return;

  if (HasMessageCenterView()) {
    notification_list_->MarkSinglePopupAsShown(id, mark_notification_as_read);
    for (MessageCenterObserver& observer : observer_list_) {
      observer.OnNotificationUpdated(id);
      observer.OnNotificationPopupShown(id, mark_notification_as_read);
    }
  } else {
    RemoveNotification(id, false);
  }
}

void MessageCenterImpl::ResetSinglePopup(const std::string& id) {
  notification_list_->ResetSinglePopup(id);
  for (MessageCenterObserver& observer : observer_list_) {
    observer.OnNotificationUpdated(id);
  }
}

void MessageCenterImpl::DisplayedNotification(
    const std::string& id,
    const DisplaySource source) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // This method may be called from the handlers, so we shouldn't manipulate
  // notifications in this method.

  if (!FindVisibleNotificationById(id))
    return;

  if (HasPopupNotifications())
    notification_list_->MarkSinglePopupAsDisplayed(id);
  scoped_refptr<NotificationDelegate> delegate =
      notification_list_->GetNotificationDelegate(id);
  for (MessageCenterObserver& observer : observer_list_)
    observer.OnNotificationDisplayed(id, source);
}

void MessageCenterImpl::SetQuietMode(bool in_quiet_mode) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (in_quiet_mode != notification_list_->quiet_mode()) {
    notification_list_->SetQuietMode(in_quiet_mode);
    for (MessageCenterObserver& observer : observer_list_)
      observer.OnQuietModeChanged(in_quiet_mode);
  }
  quiet_mode_timer_.Stop();
}

void MessageCenterImpl::SetSpokenFeedbackEnabled(bool enabled) {
  spoken_feedback_enabled_ = enabled;
}

void MessageCenterImpl::EnterQuietModeWithExpire(
    const base::TimeDelta& expires_in) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (!quiet_mode_timer_.IsRunning()) {
    notification_list_->SetQuietMode(true);
    for (MessageCenterObserver& observer : observer_list_)
      observer.OnQuietModeChanged(true);
  }

  // This will restart the timer if it is already running.
  quiet_mode_timer_.Start(FROM_HERE, expires_in,
                          base::BindOnce(&MessageCenterImpl::SetQuietMode,
                                         base::Unretained(this), false));
}

void MessageCenterImpl::RestartPopupTimers() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (popup_timers_controller_)
    popup_timers_controller_->StartAll();
}

void MessageCenterImpl::PausePopupTimers() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (popup_timers_controller_)
    popup_timers_controller_->PauseAll();
}

const std::u16string& MessageCenterImpl::GetSystemNotificationAppName() const {
  return system_notification_app_name_;
}

void MessageCenterImpl::SetSystemNotificationAppName(
    const std::u16string& name) {
  system_notification_app_name_ = name;
}

void MessageCenterImpl::DisableTimersForTest() {
  popup_timers_controller_.reset();
}

}  // namespace message_center
