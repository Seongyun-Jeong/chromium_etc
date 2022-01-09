// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_ABOUT_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_ABOUT_HANDLER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/ui/webui/help/version_updater.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"
#include "chrome/browser/upgrade_detector/upgrade_observer.h"
#include "components/policy/core/common/policy_service.h"
#include "content/public/browser/web_ui_message_handler.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/ash/tpm_firmware_update.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

namespace base {
class DictionaryValue;
class FilePath;
class ListValue;
class Clock;
}  // namespace base

class Profile;

namespace settings {

// WebUI message handler for the help page.
class AboutHandler : public settings::SettingsPageUIHandler,
                     public UpgradeObserver {
 public:
  explicit AboutHandler(Profile* profile);

  AboutHandler(const AboutHandler&) = delete;
  AboutHandler& operator=(const AboutHandler&) = delete;

  ~AboutHandler() override;

  // WebUIMessageHandler implementation.
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // UpgradeObserver implementation.
  void OnUpgradeRecommended() override;

  // Returns the browser version as a string.
  static std::u16string BuildBrowserVersionString();

 protected:
  // Used to test the EOL string displayed in the About details page.
  void set_clock(base::Clock* clock) { clock_ = clock; }

 private:
  void OnDeviceAutoUpdatePolicyChanged(const base::Value* previous_policy,
                                       const base::Value* current_policy);

  // Called once the JS page is ready to be called, serves as a signal to the
  // handler to register C++ observers.
  void HandlePageReady(const base::ListValue* args);

  // Called once when the page has loaded. On ChromeOS, this gets the current
  // update status. On other platforms, it will request and perform an update
  // (if one is available).
  void HandleRefreshUpdateStatus(const base::ListValue* args);
  void RefreshUpdateStatus();

#if defined(OS_MAC)
  // Promotes the updater for all users.
  void PromoteUpdater(const base::ListValue* args);
#endif

  // Opens the feedback dialog. |args| must be empty.
  void HandleOpenFeedbackDialog(const base::ListValue* args);

  // Opens the help page. |args| must be empty.
  void HandleOpenHelpPage(const base::ListValue* args);

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // Checks if ReleaseNotes is enabled.
  void HandleGetEnabledReleaseNotes(const base::ListValue* args);

  // Checks if system is connected to internet.
  void HandleCheckInternetConnection(const base::ListValue* args);

  // Opens the release notes app. |args| must be empty.
  void HandleLaunchReleaseNotes(const base::ListValue* args);

  // Opens the help page. |args| must be empty.
  void HandleOpenOsHelpPage(const base::ListValue* args);

  // Sets the release track version.
  void HandleSetChannel(const base::ListValue* args);

  // Retrieves OS, ARC and firmware versions.
  void HandleGetVersionInfo(const base::ListValue* args);
  void OnGetVersionInfoReady(
      std::string callback_id,
      std::unique_ptr<base::DictionaryValue> version_info);

  // Retrieves channel info.
  void HandleGetChannelInfo(const base::ListValue* args);

  // Checks whether we can change the current channel.
  void HandleCanChangeChannel(const base::ListValue* args);

  // Callbacks for version_updater_->GetChannel calls.
  void OnGetCurrentChannel(std::string callback_id,
                           const std::string& current_channel);
  void OnGetTargetChannel(std::string callback_id,
                          const std::string& current_channel,
                          const std::string& target_channel);

  // Checks for and applies update, triggered by JS.
  void HandleRequestUpdate(const base::ListValue* args);

  // Checks for and applies update over cellular connection, triggered by JS.
  // Update version and size should be included in the list of arguments.
  void HandleRequestUpdateOverCellular(const base::ListValue* args);

  // Checks for and applies update over cellular connection.
  void RequestUpdateOverCellular(const std::string& update_version,
                                 int64_t update_size);

  // Called once when the page has loaded to retrieve the TPM firmware update
  // status.
  void HandleRefreshTPMFirmwareUpdateStatus(const base::ListValue* args);
  void RefreshTPMFirmwareUpdateStatus(
      const std::set<ash::tpm_firmware_update::Mode>& modes);
#endif

  // Checks for and applies update.
  void RequestUpdate();

  // Callback method which forwards status updates to the page.
  void SetUpdateStatus(VersionUpdater::Status status,
                       int progress,
                       bool rollback,
                       bool powerwash,
                       const std::string& version,
                       int64_t size,
                       const std::u16string& fail_message);

#if defined(OS_MAC)
  // Callback method which forwards promotion state to the page.
  void SetPromotionState(VersionUpdater::PromotionState state);
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
  void HandleOpenDiagnostics(const base::ListValue* args);

  void HandleOpenFirmwareUpdates(const base::ListValue* args);

  void HandleGetRegulatoryInfo(const base::ListValue* args);

  // Callback for when the directory with the regulatory label image and alt
  // text has been found.
  void OnRegulatoryLabelDirFound(std::string callback_id,
                                 const base::FilePath& label_dir_path);

  // Callback for when the regulatory text has been read.
  void OnRegulatoryLabelTextRead(std::string callback_id,
                                 const base::FilePath& label_dir_path,
                                 const std::string& text);

  // Retrieves device End of Life information which contains the End of Life
  // date. Will asynchronously resolve the provided callback with an object
  // containing a boolean indicating whether the device has reached/passed End
  // of Life, and an End Of Life description formatted with the month and year.
  void HandleGetEndOfLifeInfo(const base::ListValue* args);

  // Callbacks for version_updater_->GetEolInfo calls.
  void OnGetEndOfLifeInfo(std::string callback_id,
                          chromeos::UpdateEngineClient::EolInfo eol_info);
#endif

  raw_ptr<Profile> profile_;

  // Specialized instance of the VersionUpdater used to update the browser.
  std::unique_ptr<VersionUpdater> version_updater_;

  // Used to observe changes in the |kDeviceAutoUpdateDisabled| policy.
  std::unique_ptr<policy::PolicyChangeRegistrar> policy_registrar_;

  // If true changes to UpgradeObserver are applied, if false they are ignored.
  bool apply_changes_from_upgrade_observer_;

  // Override to test the EOL string displayed in the About details page.
  raw_ptr<base::Clock> clock_;

  // Used for callbacks.
  base::WeakPtrFactory<AboutHandler> weak_factory_{this};
};

}  // namespace settings

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_ABOUT_HANDLER_H_
