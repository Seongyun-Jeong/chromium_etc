// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOGIN_ENROLLMENT_MOCK_ENROLLMENT_SCREEN_H_
#define CHROME_BROWSER_ASH_LOGIN_ENROLLMENT_MOCK_ENROLLMENT_SCREEN_H_

#include "chrome/browser/ash/login/enrollment/enrollment_screen.h"
#include "chrome/browser/ash/login/enrollment/enrollment_screen_view.h"
#include "chrome/browser/ash/policy/enrollment/enrollment_config.h"
#include "chrome/browser/policy/enrollment_status.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace ash {

class MockEnrollmentScreen : public EnrollmentScreen {
 public:
  MockEnrollmentScreen(EnrollmentScreenView* view,
                       const ScreenExitCallback& exit_callback);
  ~MockEnrollmentScreen() override;

  MOCK_METHOD(void, ShowImpl, ());
  MOCK_METHOD(void, HideImpl, ());

  void ExitScreen(Result result);
};

class MockEnrollmentScreenView : public EnrollmentScreenView {
 public:
  MockEnrollmentScreenView();
  virtual ~MockEnrollmentScreenView();

  void Bind(EnrollmentScreen* screen) override;
  void Unbind() override;

  MOCK_METHOD(void,
              SetEnrollmentConfig,
              (const policy::EnrollmentConfig& config));
  MOCK_METHOD(void, SetEnrollmentController, (Controller*));
  MOCK_METHOD(void,
              SetEnterpriseDomainInfo,
              (const std::string& manager, const std::u16string& device_type));
  MOCK_METHOD(void, SetFlowType, (FlowType flow_type));
  MOCK_METHOD(void, Show, ());
  MOCK_METHOD(void, Hide, ());
  MOCK_METHOD(void, MockBind, (EnrollmentScreen * screen));
  MOCK_METHOD(void, MockUnbind, ());
  MOCK_METHOD(void, ShowSigninScreen, ());
  MOCK_METHOD(void,
              ShowUserError,
              (UserErrorType error_type, const std::string& email));
  MOCK_METHOD(void, ShowEnrollmentCloudReadyNotAllowedError, ());
  MOCK_METHOD(void,
              ShowLicenseTypeSelectionScreen,
              (const base::DictionaryValue&));
  MOCK_METHOD(void,
              ShowActiveDirectoryScreen,
              (const std::string& domain_join_config,
               const std::string& machine_name,
               const std::string& username,
               authpolicy::ErrorType error));
  MOCK_METHOD(void,
              ShowAttributePromptScreen,
              (const std::string& asset_id, const std::string& location));
  MOCK_METHOD(void, ShowEnrollmentSuccessScreen, ());
  MOCK_METHOD(void, ShowEnrollmentTPMCheckingScreen, ());
  MOCK_METHOD(void, ShowEnrollmentWorkingScreen, ());
  MOCK_METHOD(void, ShowAuthError, (const GoogleServiceAuthError&));
  MOCK_METHOD(void, ShowOtherError, (EnterpriseEnrollmentHelper::OtherError));
  MOCK_METHOD(void, ShowEnrollmentStatus, (policy::EnrollmentStatus status));
  MOCK_METHOD(void, Shutdown, ());
  MOCK_METHOD(void, SetIsBrandedBuild, (bool is_branded));

 private:
  EnrollmentScreen* screen_ = nullptr;
};

}  // namespace ash

// TODO(https://crbug.com/1164001): remove after the //chrome/browser/chromeos
// source migration is finished.
namespace chromeos {
using ::ash::MockEnrollmentScreen;
using ::ash::MockEnrollmentScreenView;
}  // namespace chromeos

#endif  // CHROME_BROWSER_ASH_LOGIN_ENROLLMENT_MOCK_ENROLLMENT_SCREEN_H_
