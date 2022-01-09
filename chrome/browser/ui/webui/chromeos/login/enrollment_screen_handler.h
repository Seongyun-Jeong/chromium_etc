// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_ENROLLMENT_SCREEN_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_ENROLLMENT_SCREEN_HANDLER_H_

#include <memory>
#include <string>
#include <vector>

#include "chrome/browser/ash/login/enrollment/enrollment_screen_view.h"
#include "chrome/browser/ash/login/enrollment/enterprise_enrollment_helper.h"
// TODO(https://crbug.com/1164001): move to forward declaration.
#include "chrome/browser/ash/login/error_screens_histogram_helper.h"
// TODO(https://crbug.com/1164001): move to forward declaration.
#include "chrome/browser/ash/login/help_app_launcher.h"
#include "chrome/browser/ash/login/screens/error_screen.h"
#include "chrome/browser/ash/policy/enrollment/enrollment_config.h"
#include "chrome/browser/ui/webui/chromeos/login/base_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/network_state_informer.h"
#include "net/base/net_errors.h"
#include "net/cookies/canonical_cookie.h"

namespace chromeos {
class CookieWaiter;

// Possible error states of the Active Directory screen. Must be in the same
// order as ActiveDirectoryErrorState ( in enterprise_enrollment.js ) values.
enum class ActiveDirectoryErrorState {
  NONE = 0,
  MACHINE_NAME_INVALID = 1,
  MACHINE_NAME_TOO_LONG = 2,
  BAD_USERNAME = 3,
  BAD_AUTH_PASSWORD = 4,
  BAD_UNLOCK_PASSWORD = 5,
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class ActiveDirectoryDomainJoinType {
  // Configuration is not set on the domain.
  WITHOUT_CONFIGURATION = 0,
  // Configuration is set but was not unlocked during domain join.
  NOT_USING_CONFIGURATION = 1,
  // Configuration is set and was unlocked during domain join.
  USING_CONFIGURATION = 2,
  // Number of elements in the enum. Should be last.
  COUNT,
};

// WebUIMessageHandler implementation which handles events occurring on the
// page, such as the user pressing the signin button.
class EnrollmentScreenHandler
    : public BaseScreenHandler,
      public EnrollmentScreenView,
      public NetworkStateInformer::NetworkStateInformerObserver {
 public:
  using TView = EnrollmentScreenView;

  EnrollmentScreenHandler(
      JSCallsContainer* js_calls_container,
      const scoped_refptr<NetworkStateInformer>& network_state_informer,
      ErrorScreen* error_screen);

  EnrollmentScreenHandler(const EnrollmentScreenHandler&) = delete;
  EnrollmentScreenHandler& operator=(const EnrollmentScreenHandler&) = delete;

  ~EnrollmentScreenHandler() override;

  // Implements WebUIMessageHandler:
  void RegisterMessages() override;

  // Implements EnrollmentScreenView:
  void SetEnrollmentConfig(const policy::EnrollmentConfig& config) override;
  void SetEnrollmentController(Controller* controller) override;

  void SetEnterpriseDomainInfo(const std::string& manager,
                               const std::u16string& device_type) override;
  void SetFlowType(FlowType flow_type) override;
  void Show() override;
  void Hide() override;
  void Bind(ash::EnrollmentScreen* screen) override;
  void Unbind() override;
  void ShowSigninScreen() override;
  void ShowUserError(UserErrorType error_type,
                     const std::string& email) override;
  void ShowEnrollmentCloudReadyNotAllowedError() override;
  void ShowActiveDirectoryScreen(const std::string& domain_join_config,
                                 const std::string& machine_name,
                                 const std::string& username,
                                 authpolicy::ErrorType error) override;
  void ShowAttributePromptScreen(const std::string& asset_id,
                                 const std::string& location) override;
  void ShowEnrollmentSuccessScreen() override;
  void ShowEnrollmentWorkingScreen() override;
  void ShowEnrollmentTPMCheckingScreen() override;
  void ShowAuthError(const GoogleServiceAuthError& error) override;
  void ShowEnrollmentStatus(policy::EnrollmentStatus status) override;
  void ShowOtherError(
      EnterpriseEnrollmentHelper::OtherError error_code) override;
  void Shutdown() override;
  void SetIsBrandedBuild(bool is_branded) override;

  // Implements BaseScreenHandler:
  void Initialize() override;
  void DeclareLocalizedValues(
      ::login::LocalizedValuesBuilder* builder) override;
  void GetAdditionalParameters(base::DictionaryValue* parameters) override;

  // Implements NetworkStateInformer::NetworkStateInformerObserver
  void UpdateState(NetworkError::ErrorReason reason) override;

  void ContinueAuthenticationWhenCookiesAvailable(const std::string& user);
  void OnCookieWaitTimeout();

 private:
  // Handlers for WebUI messages.
  void HandleToggleFakeEnrollment();
  void HandleClose(const std::string& reason);
  void HandleCompleteLogin(const std::string& user);
  void OnGetCookiesForCompleteLogin(
      const std::string& user,
      const net::CookieAccessResultList& cookies,
      const net::CookieAccessResultList& excluded_cookies);
  void HandleAdCompleteLogin(const std::string& machine_name,
                             const std::string& distinguished_name,
                             const std::string& encryption_types,
                             const std::string& user_name,
                             const std::string& password);
  void HandleAdUnlockConfiguration(const std::string& password);
  void HandleIdentifierEntered(const std::string& email);
  void HandleRetry();
  void HandleFrameLoadingCompleted();
  void HandleDeviceAttributesProvided(const std::string& asset_id,
                                      const std::string& location);
  void HandleOnLearnMore();
  void UpdateStateInternal(NetworkError::ErrorReason reason, bool force_update);
  void SetupAndShowOfflineMessage(NetworkStateInformer::State state,
                                  NetworkError::ErrorReason reason);
  void HideOfflineMessage(NetworkStateInformer::State state,
                          NetworkError::ErrorReason reason);

  // Shows a given enrollment step.
  void ShowStep(const char* step);

  // Display the given i18n resource as error message.
  void ShowError(int message_id, bool retry);

  // Display the given i18n resource as an error message, with the $1
  // substitution parameter replaced with the device's product name.
  void ShowErrorForDevice(int message_id, bool retry);

  // Display the given string as error message.
  void ShowErrorMessage(const std::string& message, bool retry);

  // Display the given i18n string as a progress message.
  void ShowWorking(int message_id);

  // Shows the screen.
  void DoShow();

  // Shows the screen.
  void DoShowWithPartition(const std::string& partition_name);

  // Returns true if current visible screen is the enrollment sign-in page.
  bool IsOnEnrollmentScreen();

  // Returns true if current visible screen is the error screen over
  // enrollment sign-in page.
  bool IsEnrollmentScreenHiddenByError();

  // Called after configuration seed was unlocked.
  void OnAdConfigurationUnlocked(std::string unlocked_data);

  // Keeps the controller for this view.
  Controller* controller_ = nullptr;

  bool show_on_init_ = false;

  // The enrollment configuration.
  policy::EnrollmentConfig config_;

  // GAIA flow type parameter that is set to authenticator.
  FlowType flow_type_;

  // Active Directory configuration in the form of encrypted binary data.
  std::string active_directory_domain_join_config_;

  ActiveDirectoryDomainJoinType active_directory_join_type_ =
      ActiveDirectoryDomainJoinType::COUNT;

  // True if screen was not shown yet.
  bool first_show_ = true;

  // Whether we should handle network errors on enrollment screen.
  // True when signin screen step is shown.
  bool observe_network_failure_ = false;

  // Set true when chrome is being restarted to pick up enrollment changes. The
  // renderer processes will be destroyed and can no longer be talked to.
  bool shutdown_ = false;

  // Network state informer used to keep signin screen up.
  scoped_refptr<NetworkStateInformer> network_state_informer_;

  ErrorScreen* error_screen_ = nullptr;
  ash::EnrollmentScreen* screen_ = nullptr;

  std::string signin_partition_name_;

  std::unique_ptr<ErrorScreensHistogramHelper> histogram_helper_;

  // Help application used for help dialogs.
  scoped_refptr<HelpAppLauncher> help_app_;

  std::unique_ptr<CookieWaiter> oauth_code_waiter_;

  bool use_fake_login_for_testing_ = false;

  base::WeakPtrFactory<EnrollmentScreenHandler> weak_ptr_factory_{this};
};

}  // namespace chromeos

// TODO(https://crbug.com/1164001): remove after the //chrome/browser/chromeos
// source migration is finished.
namespace ash {
using ::chromeos::ActiveDirectoryErrorState;
using ::chromeos::EnrollmentScreenHandler;
}

#endif  // CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_ENROLLMENT_SCREEN_HANDLER_H_
