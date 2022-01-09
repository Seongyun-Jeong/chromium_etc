// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SIGNIN_ENTERPRISE_PROFILE_WELCOME_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SIGNIN_ENTERPRISE_PROFILE_WELCOME_HANDLER_H_

#include <string>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/webui/signin/enterprise_profile_welcome_ui.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "google_apis/gaia/core_account_id.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/skia/include/core/SkColor.h"

class Browser;
struct AccountInfo;

namespace base {
class FilePath;
}

// WebUI message handler for the welcome screen for enterprise profiles in the
// profile creation flow.
class EnterpriseProfileWelcomeHandler
    : public content::WebUIMessageHandler,
      public ProfileAttributesStorage::Observer,
      public BrowserListObserver,
      public signin::IdentityManager::Observer {
 public:
  EnterpriseProfileWelcomeHandler(
      Browser* browser,
      EnterpriseProfileWelcomeUI::ScreenType type,
      const AccountInfo& account_info,
      absl::optional<SkColor> profile_color,
      base::OnceCallback<void(bool)> proceed_callback);
  ~EnterpriseProfileWelcomeHandler() override;

  EnterpriseProfileWelcomeHandler(const EnterpriseProfileWelcomeHandler&) =
      delete;
  EnterpriseProfileWelcomeHandler& operator=(
      const EnterpriseProfileWelcomeHandler&) = delete;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // ProfileAttributesStorage::Observer:
  void OnProfileAvatarChanged(const base::FilePath& profile_path) override;
  void OnProfileHighResAvatarLoaded(
      const base::FilePath& profile_path) override;
  void OnProfileHostedDomainChanged(
      const base::FilePath& profile_path) override;

  // BrowserListObserver:
  void OnBrowserRemoved(Browser* browser) override;

  // signin::IdentityManager::Observer:
  void OnExtendedAccountInfoUpdated(const AccountInfo& info) override;

  // Access to construction parameters for tests.
  EnterpriseProfileWelcomeUI::ScreenType GetTypeForTesting();
  void CallProceedCallbackForTesting(bool proceed);

 private:
  void HandleInitialized(const base::ListValue* args);
  // Handles the web ui message sent when the html content is done being laid
  // out and it's time to resize the native view hosting it to fit. |args| is
  // a single integer value for the height the native view should resize to.
  void HandleInitializedWithSize(const base::ListValue* args);
  void HandleProceed(const base::ListValue* args);
  void HandleCancel(const base::ListValue* args);

  // Sends an updated profile info (avatar and colors) to the WebUI.
  // `profile_path` is the path of the profile being updated, this function does
  // nothing if the profile path does not match the current profile.
  void UpdateProfileInfo(const base::FilePath& profile_path);

  // Computes the profile info (avatar and colors) to be sent to the WebUI.
  base::Value GetProfileInfoValue();

  // Returns the ProfilesAttributesEntry associated with the current profile.
  ProfileAttributesEntry* GetProfileEntry() const;

  std::string GetPictureUrl();

  base::FilePath profile_path_;
  base::ScopedObservation<ProfileAttributesStorage,
                          ProfileAttributesStorage::Observer>
      observed_profile_{this};

  base::ScopedObservation<signin::IdentityManager,
                          signin::IdentityManager::Observer>
      observed_account_{this};

  raw_ptr<Browser> browser_ = nullptr;
  const EnterpriseProfileWelcomeUI::ScreenType type_;
  const std::u16string email_;
  const std::string domain_name_;
  const CoreAccountId account_id_;
  absl::optional<SkColor> profile_color_;
  base::OnceCallback<void(bool)> proceed_callback_;
};

#endif  // CHROME_BROWSER_UI_WEBUI_SIGNIN_ENTERPRISE_PROFILE_WELCOME_HANDLER_H_
