// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/login/test/oobe_screens_utils.h"

#include "ash/constants/ash_features.h"
#include "chrome/browser/ash/login/oobe_screen.h"
#include "chrome/browser/ash/login/screens/sync_consent_screen.h"
#include "chrome/browser/ash/login/screens/update_screen.h"
#include "chrome/browser/ash/login/test/js_checker.h"
#include "chrome/browser/ash/login/test/oobe_screen_exit_waiter.h"
#include "chrome/browser/ash/login/test/oobe_screen_waiter.h"
#include "chrome/browser/ash/login/test/test_condition_waiter.h"
#include "chrome/browser/ash/login/wizard_controller.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/ui/webui/chromeos/login/enrollment_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/eula_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/fingerprint_setup_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/marketing_opt_in_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/network_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/pin_setup_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/update_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/user_creation_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/welcome_screen_handler.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"

namespace ash {
namespace test {
namespace {

void WaitFor(OobeScreenId screen_id) {
  OobeScreenWaiter(screen_id).Wait();
  LOG(INFO) << "Switched to '" << screen_id.name << "' screen.";
}

void WaitForExit(OobeScreenId screen_id) {
  OobeScreenExitWaiter(screen_id).Wait();
  LOG(INFO) << "Screen '" << screen_id.name << "' is done.";
}

}  // namespace

void WaitForWelcomeScreen() {
  WaitFor(WelcomeView::kScreenId);
}

void TapWelcomeNext() {
  OobeJS().TapOnPath({"connect", "welcomeScreen", "getStarted"});
}

void WaitForNetworkSelectionScreen() {
  WaitFor(NetworkScreenView::kScreenId);
}

void TapNetworkSelectionNext() {
  OobeJS()
      .CreateEnabledWaiter(true /* enabled */,
                           {"network-selection", "nextButton"})
      ->Wait();
  OobeJS().TapOnPath({"network-selection", "nextButton"});
}

void WaitForUpdateScreen() {
  WaitFor(UpdateView::kScreenId);
  OobeJS().CreateVisibilityWaiter(true, {"oobe-update"})->Wait();
}

void ExitUpdateScreenNoUpdate() {
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::ERROR);

  UpdateScreen* screen =
      WizardController::default_controller()->GetScreen<UpdateScreen>();
  screen->GetVersionUpdaterForTesting()->UpdateStatusChangedForTesting(status);
}

void WaitForFingerprintScreen() {
  LOG(INFO) << "Waiting for 'fingerprint-setup' screen.";
  OobeScreenWaiter(FingerprintSetupScreenView::kScreenId).Wait();
  LOG(INFO) << "Waiting for fingerprint setup screen "
               "to show.";
  OobeJS().CreateVisibilityWaiter(true, {"fingerprint-setup"})->Wait();
  LOG(INFO) << "Waiting for fingerprint setup screen "
               "to show setupFingerprint.";
  OobeJS()
      .CreateVisibilityWaiter(true, {"fingerprint-setup", "setupFingerprint"})
      ->Wait();
}

void ExitFingerprintPinSetupScreen() {
  OobeJS().ExpectVisiblePath({"fingerprint-setup", "setupFingerprint"});
  // This might be the last step in flow. Synchronous execute gets stuck as
  // WebContents may be destroyed in the process. So it may never return.
  // So we use ExecuteAsync() here.
  OobeJS().ExecuteAsync("$('fingerprint-setup').$.skipStart.click()");
  LOG(INFO) << "OobeInteractiveUITest: Waiting for fingerprint setup screen "
               "to close.";
  WaitForExit(FingerprintSetupScreenView::kScreenId);
}

void WaitForPinSetupScreen() {
  WaitFor(PinSetupScreenView::kScreenId);
}

void ExitPinSetupScreen() {
  // This might be the last step in flow. Synchronous execute gets stuck as
  // WebContents may be destroyed in the process. So it may never return.
  // So we use ExecuteAsync() here.
  OobeJS().ExecuteAsync("$('pin-setup').$.setupSkipButton.click()");
  WaitForExit(PinSetupScreenView::kScreenId);
}

void SkipToEnrollmentOnRecovery() {
  WaitForWelcomeScreen();
  TapWelcomeNext();

  WaitForNetworkSelectionScreen();
  TapNetworkSelectionNext();

  WaitForEulaScreen();
  TapEulaAccept();

  WaitForUpdateScreen();
  ExitUpdateScreenNoUpdate();

  WaitFor(EnrollmentScreenView::kScreenId);
}

void WaitForEnrollmentScreen() {
  WaitFor(EnrollmentScreenView::kScreenId);
}

void WaitForUserCreationScreen() {
  WaitFor(UserCreationView::kScreenId);
}

void TapUserCreationNext() {
  OobeJS().TapOnPath({"user-creation", "nextButton"});
}

void WaitForOobeJSReady() {
  if (!LoginDisplayHost::default_host()->GetOobeUI()) {
    base::RunLoop run_loop;
    LoginDisplayHost::default_host()->AddWizardCreatedObserverForTests(
        run_loop.QuitClosure());
    run_loop.Run();
  }

  base::RunLoop run_loop;
  if (!LoginDisplayHost::default_host()->GetOobeUI()->IsJSReady(
          run_loop.QuitClosure())) {
    run_loop.Run();
  }
}

void WaitForEulaScreen() {
  if (!LoginDisplayHost::default_host()->GetWizardContext()->is_branded_build)
    return;
  WaitFor(EulaView::kScreenId);
}

void TapEulaAccept() {
  if (!LoginDisplayHost::default_host()->GetWizardContext()->is_branded_build)
    return;
  OobeJS().TapOnPath({"oobe-eula-md", "acceptButton"});
}

void WaitForSyncConsentScreen() {
  if (!LoginDisplayHost::default_host()->GetWizardContext()->is_branded_build)
    return;
  WaitFor(SyncConsentScreenView::kScreenId);
}

void ExitScreenSyncConsent() {
  if (!LoginDisplayHost::default_host()->GetWizardContext()->is_branded_build)
    return;
  SyncConsentScreen* screen = static_cast<SyncConsentScreen*>(
      WizardController::default_controller()->GetScreen(
          SyncConsentScreenView::kScreenId));

  screen->SetProfileSyncDisabledByPolicyForTesting(true);
  screen->OnStateChanged(nullptr);
  WaitForExit(SyncConsentScreenView::kScreenId);
}

void ClickSignInFatalScreenActionButton() {
  OobeJS().ClickOnPath({"signin-fatal-error", "actionButton"});
}

bool IsScanningRequestedOnNetworkScreen() {
  return OobeJS().GetAttributeBool(
      "enableWifiScans",
      {"network-selection", "networkSelectLogin", "networkSelect"});
}

bool IsScanningRequestedOnErrorScreen() {
  return OobeJS().GetAttributeBool(
      "enableWifiScans",
      {"error-message", "offline-network-control", "networkSelect"});
}

LanguageReloadObserver::LanguageReloadObserver(WelcomeScreen* welcome_screen)
    : welcome_screen_(welcome_screen) {
  welcome_screen_->AddObserver(this);
}

void LanguageReloadObserver::OnLanguageListReloaded() {
  run_loop_.Quit();
}

void LanguageReloadObserver::Wait() {
  run_loop_.Run();
}

LanguageReloadObserver::~LanguageReloadObserver() {
  welcome_screen_->RemoveObserver(this);
}

}  // namespace test
}  // namespace ash
