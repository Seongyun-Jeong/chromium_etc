// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/login/wizard_controller.h"

#include "ash/components/audio/cras_audio_handler.h"
#include "ash/components/geolocation/simple_geolocation_provider.h"
#include "ash/components/settings/timezone_settings.h"
#include "ash/components/timezone/timezone_request.h"
#include "ash/constants/ash_switches.h"
#include "ash/public/cpp/login_screen_test_api.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/ash/accessibility/accessibility_manager.h"
#include "chrome/browser/ash/base/locale_util.h"
#include "chrome/browser/ash/login/demo_mode/demo_setup_controller.h"
#include "chrome/browser/ash/login/enrollment/auto_enrollment_controller.h"
#include "chrome/browser/ash/login/enrollment/enrollment_screen.h"
#include "chrome/browser/ash/login/enrollment/enterprise_enrollment_helper.h"
#include "chrome/browser/ash/login/enrollment/mock_auto_enrollment_check_screen.h"
#include "chrome/browser/ash/login/enrollment/mock_enrollment_screen.h"
#include "chrome/browser/ash/login/existing_user_controller.h"
#include "chrome/browser/ash/login/login_manager_test.h"
#include "chrome/browser/ash/login/login_wizard.h"
#include "chrome/browser/ash/login/oobe_screen.h"
#include "chrome/browser/ash/login/screens/device_disabled_screen.h"
#include "chrome/browser/ash/login/screens/error_screen.h"
#include "chrome/browser/ash/login/screens/hid_detection_screen.h"
#include "chrome/browser/ash/login/screens/mock_arc_terms_of_service_screen.h"
#include "chrome/browser/ash/login/screens/mock_demo_preferences_screen.h"
#include "chrome/browser/ash/login/screens/mock_demo_setup_screen.h"
#include "chrome/browser/ash/login/screens/mock_device_disabled_screen_view.h"
#include "chrome/browser/ash/login/screens/mock_enable_adb_sideloading_screen.h"
#include "chrome/browser/ash/login/screens/mock_enable_debugging_screen.h"
#include "chrome/browser/ash/login/screens/mock_eula_screen.h"
#include "chrome/browser/ash/login/screens/mock_network_screen.h"
#include "chrome/browser/ash/login/screens/mock_update_screen.h"
#include "chrome/browser/ash/login/screens/mock_welcome_screen.h"
#include "chrome/browser/ash/login/screens/mock_wrong_hwid_screen.h"
#include "chrome/browser/ash/login/screens/reset_screen.h"
#include "chrome/browser/ash/login/screens/welcome_screen.h"
#include "chrome/browser/ash/login/screens/wrong_hwid_screen.h"
#include "chrome/browser/ash/login/startup_utils.h"
#include "chrome/browser/ash/login/test/device_state_mixin.h"
#include "chrome/browser/ash/login/test/js_checker.h"
#include "chrome/browser/ash/login/test/login_manager_mixin.h"
#include "chrome/browser/ash/login/test/oobe_base_test.h"
#include "chrome/browser/ash/login/test/oobe_configuration_waiter.h"
#include "chrome/browser/ash/login/test/oobe_screen_exit_waiter.h"
#include "chrome/browser/ash/login/test/oobe_screen_waiter.h"
#include "chrome/browser/ash/login/ui/login_display_host.h"
#include "chrome/browser/ash/login/ui/webui_login_view.h"
#include "chrome/browser/ash/net/network_portal_detector_test_impl.h"
#include "chrome/browser/ash/net/rollback_network_config/fake_rollback_network_config.h"
#include "chrome/browser/ash/net/rollback_network_config/rollback_network_config_service.h"
#include "chrome/browser/ash/policy/enrollment/auto_enrollment_client.h"
#include "chrome/browser/ash/policy/enrollment/enrollment_config.h"
#include "chrome/browser/ash/policy/enrollment/fake_auto_enrollment_client.h"
#include "chrome/browser/ash/policy/server_backed_state/server_backed_device_state.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/webui/chromeos/login/error_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/gaia_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/oobe_ui.h"
#include "chrome/browser/ui/webui/chromeos/login/reset_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/signin_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/update_required_screen_handler.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/dbus/constants/dbus_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/session_manager/fake_session_manager_client.h"
#include "chromeos/dbus/shill/fake_shill_manager_client.h"
#include "chromeos/dbus/system_clock/system_clock_client.h"
#include "chromeos/dbus/userdataauth/fake_install_attributes_client.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/system/fake_statistics_provider.h"
#include "chromeos/system/statistics_provider.h"
#include "chromeos/test/chromeos_test_utils.h"
#include "chromeos/tpm/stub_install_attributes.h"
#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "components/prefs/testing_pref_store.h"
#include "components/session_manager/core/session_manager.h"
#include "components/session_manager/session_manager_types.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/mock_notification_observer.h"
#include "content/public/test/test_utils.h"
#include "net/test/spawned_test_server/spawned_test_server.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/icu/source/common/unicode/locid.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"

namespace ash {
namespace {

using ::testing::_;
using ::testing::IsNull;
using ::testing::Mock;
using ::testing::NotNull;

const char kGeolocationResponseBody[] =
    "{\n"
    "  \"location\": {\n"
    "    \"lat\": 51.0,\n"
    "    \"lng\": -0.1\n"
    "  },\n"
    "  \"accuracy\": 1200.4\n"
    "}";

// Timezone should not match kGeolocationResponseBody to check that exactly
// this value will be used.
const char kTimezoneResponseBody[] =
    "{\n"
    "    \"dstOffset\" : 0.0,\n"
    "    \"rawOffset\" : -32400.0,\n"
    "    \"status\" : \"OK\",\n"
    "    \"timeZoneId\" : \"America/Anchorage\",\n"
    "    \"timeZoneName\" : \"Pacific Standard Time\"\n"
    "}";

const char kDisabledMessage[] = "This device has been disabled.";

const test::UIPath kGuestSessionLink = {"error-message",
                                        "error-guest-signin-fix-network"};

// Matches on the mode parameter of an EnrollmentConfig object.
MATCHER_P(EnrollmentModeMatches, mode, "") {
  return arg.mode == mode;
}

class PrefStoreStub : public TestingPrefStore {
 public:
  // TestingPrefStore overrides:
  PrefReadError GetReadError() const override {
    return PersistentPrefStore::PREF_READ_ERROR_JSON_PARSE;
  }

  bool IsInitializationComplete() const override { return true; }

 private:
  ~PrefStoreStub() override {}
};

// Used to set up a `FakeAutoEnrollmentClientFactory` for the duration of a
// test.
class ScopedFakeAutoEnrollmentClientFactory {
 public:
  explicit ScopedFakeAutoEnrollmentClientFactory(
      AutoEnrollmentController* controller)
      : controller_(controller),
        fake_auto_enrollment_client_factory_(
            base::BindRepeating(&ScopedFakeAutoEnrollmentClientFactory::
                                    OnFakeAutoEnrollmentClientCreated,
                                base::Unretained(this))) {
    controller_->SetAutoEnrollmentClientFactoryForTesting(
        &fake_auto_enrollment_client_factory_);
  }

  ScopedFakeAutoEnrollmentClientFactory(
      const ScopedFakeAutoEnrollmentClientFactory&) = delete;
  ScopedFakeAutoEnrollmentClientFactory& operator=(
      const ScopedFakeAutoEnrollmentClientFactory&) = delete;

  ~ScopedFakeAutoEnrollmentClientFactory() {
    controller_->SetAutoEnrollmentClientFactoryForTesting(nullptr);
  }

  // Waits until the `AutoEnrollmentController` has requested the creation of an
  // `AutoEnrollmentClient`. Returns the created `AutoEnrollmentClient`. If an
  // `AutoEnrollmentClient` has already been created, returns immediately.
  // Note: The returned instance is owned by `AutoEnrollmentController`.
  policy::FakeAutoEnrollmentClient* WaitAutoEnrollmentClientCreated() {
    if (created_auto_enrollment_client_)
      return created_auto_enrollment_client_;

    base::RunLoop run_loop;
    run_on_auto_enrollment_client_created_ = run_loop.QuitClosure();
    run_loop.Run();

    return created_auto_enrollment_client_;
  }

  // Resets the cached `AutoEnrollmentClient`, so another `AutoEnrollmentClient`
  // may be created through this factory.
  void Reset() { created_auto_enrollment_client_ = nullptr; }

 private:
  // Called when `fake_auto_enrollment_client_factory_` was asked to create an
  // `AutoEnrollmentClient`.
  void OnFakeAutoEnrollmentClientCreated(
      policy::FakeAutoEnrollmentClient* auto_enrollment_client) {
    // Only allow an AutoEnrollmentClient to be created when the test expects
    // it. The test should call `Reset` to expect a new `AutoEnrollmentClient`
    // to be created.
    EXPECT_FALSE(created_auto_enrollment_client_);
    created_auto_enrollment_client_ = auto_enrollment_client;

    if (run_on_auto_enrollment_client_created_)
      std::move(run_on_auto_enrollment_client_created_).Run();
  }

  // The `AutoEnrollmentController` which is using
  // `fake_auto_enrollment_client_factory_`.
  AutoEnrollmentController* controller_;
  policy::FakeAutoEnrollmentClient::FactoryImpl
      fake_auto_enrollment_client_factory_;

  policy::FakeAutoEnrollmentClient* created_auto_enrollment_client_ = nullptr;
  base::OnceClosure run_on_auto_enrollment_client_created_;
};

struct SwitchLanguageTestData {
  SwitchLanguageTestData() : result("", "", false), done(false) {}

  locale_util::LanguageSwitchResult result;
  bool done;
};

void OnLocaleSwitched(SwitchLanguageTestData* self,
                      const locale_util::LanguageSwitchResult& result) {
  self->result = result;
  self->done = true;
}

void RunSwitchLanguageTest(const std::string& locale,
                           const std::string& expected_locale,
                           const bool expect_success) {
  SwitchLanguageTestData data;
  locale_util::SwitchLanguageCallback callback(
      base::BindOnce(&OnLocaleSwitched, base::Unretained(&data)));
  locale_util::SwitchLanguage(locale, true, false, std::move(callback),
                              ProfileManager::GetActiveUserProfile());

  // Token writing moves control to BlockingPool and back.
  content::RunAllTasksUntilIdle();

  EXPECT_EQ(data.done, true);
  EXPECT_EQ(data.result.requested_locale, locale);
  EXPECT_EQ(data.result.loaded_locale, expected_locale);
  EXPECT_EQ(data.result.success, expect_success);
}

void SetUpCrasAndEnableChromeVox(int volume_percent, bool mute_on) {
  AccessibilityManager* a11y = AccessibilityManager::Get();
  CrasAudioHandler* cras = CrasAudioHandler::Get();

  // Audio output is at `volume_percent` and `mute_on`. Spoken feedback
  // is disabled.
  cras->SetOutputVolumePercent(volume_percent);
  cras->SetOutputMute(mute_on);
  a11y->EnableSpokenFeedback(false);

  // Spoken feedback is enabled.
  a11y->EnableSpokenFeedback(true);
  base::RunLoop().RunUntilIdle();
}

void QuitLoopOnAutoEnrollmentProgress(
    policy::AutoEnrollmentState expected_state,
    base::RunLoop* loop,
    policy::AutoEnrollmentState actual_state) {
  if (expected_state == actual_state)
    loop->Quit();
}

// Returns a string which can be put into the VPD variable
// `kEnterpriseManagementEmbargoEndDateKey`. If `days_offset` is 0, the return
// value represents the current day. If `days_offset` is positive, the return
// value represents `days_offset` days in the future. If `days_offset` is
// negative, the return value represents `days_offset` days in the past.
std::string GenerateEmbargoEndDate(int days_offset) {
  base::Time::Exploded exploded;
  base::Time target_time = base::Time::Now() + base::Days(days_offset);
  target_time.UTCExplode(&exploded);

  std::string embargo_end_date_string = base::StringPrintf(
      "%04d-%02d-%02d", exploded.year, exploded.month, exploded.day_of_month);

  // Sanity check that base::Time::FromUTCString can read back the format used
  // here.
  base::Time reparsed_time;
  EXPECT_TRUE(base::Time::FromUTCString(embargo_end_date_string.c_str(),
                                        &reparsed_time));
  EXPECT_EQ(target_time.ToDeltaSinceWindowsEpoch().InMicroseconds() /
                base::Time::kMicrosecondsPerDay,
            reparsed_time.ToDeltaSinceWindowsEpoch().InMicroseconds() /
                base::Time::kMicrosecondsPerDay);

  return embargo_end_date_string;
}

template <typename View>
void ExpectBind(View* view) {
  // TODO(jdufault): The view* api should follow the bind/unbind pattern instead
  // of bind(ptr), bind(nullptr).
  EXPECT_CALL(*view, MockBind(NotNull())).Times(1);
  EXPECT_CALL(*view, MockBind(IsNull())).Times(1);
}

template <typename View>
void ExpectBindUnbind(View* view) {
  EXPECT_CALL(*view, MockBind(NotNull())).Times(1);
  EXPECT_CALL(*view, MockUnbind()).Times(1);
}

template <typename View>
void ExpectSetDelegate(View* view) {
  EXPECT_CALL(*view, MockSetDelegate(NotNull())).Times(1);
  EXPECT_CALL(*view, MockSetDelegate(IsNull())).Times(1);
}

template <typename Mock>
Mock* MockScreen(std::unique_ptr<Mock> mock) {
  auto mock0 = mock.get();
  WizardController::default_controller()->screen_manager()->SetScreenForTesting(
      std::move(mock));
  return mock0;
}

template <typename Mock>
Mock* MockScreenExpectLifecycle(std::unique_ptr<Mock> mock) {
  auto mock0 = MockScreen(std::move(mock));
  EXPECT_CALL(*mock0, ShowImpl()).Times(0);
  return mock0;
}

}  // namespace

class WizardControllerTest : public OobeBaseTest {
 public:
  WizardControllerTest(const WizardControllerTest&) = delete;
  WizardControllerTest& operator=(const WizardControllerTest&) = delete;

 protected:
  WizardControllerTest() = default;
  ~WizardControllerTest() override = default;

  ErrorScreen* GetErrorScreen() { return GetOobeUI()->GetErrorScreen(); }

  OobeUI* GetOobeUI() { return LoginDisplayHost::default_host()->GetOobeUI(); }

  content::WebContents* GetWebContents() {
    LoginDisplayHost* host = LoginDisplayHost::default_host();
    if (!host)
      return nullptr;
    return host->GetOobeWebContents();
  }

  bool JSExecute(const std::string& script) {
    return content::ExecuteScript(GetWebContents(), script);
  }

  bool JSExecuteBooleanExpression(const std::string& expression) {
    bool result;
    EXPECT_TRUE(content::ExecuteScriptAndExtractBool(
        GetWebContents(),
        "window.domAutomationController.send(!!(" + expression + "));",
        &result));
    return result;
  }

  std::string JSExecuteStringExpression(const std::string& expression) {
    std::string result;
    EXPECT_TRUE(content::ExecuteScriptAndExtractString(
        GetWebContents(),
        "window.domAutomationController.send(" + expression + ");", &result));
    return result;
  }

  void CheckCurrentScreen(OobeScreenId screen) {
    BaseScreen* current_screen =
        WizardController::default_controller()->current_screen();
    const std::string actual_screen =
        current_screen ? current_screen->screen_id().name : "nullptr";
    const std::string expected_screen = screen.name;
    EXPECT_EQ(actual_screen, expected_screen);
  }

  WrongHWIDScreen* GetWrongHWIDScreen() {
    return static_cast<WrongHWIDScreen*>(
        WizardController::default_controller()->GetScreen(
            WrongHWIDScreenView::kScreenId));
  }
};

IN_PROC_BROWSER_TEST_F(WizardControllerTest, SwitchLanguage) {
  ASSERT_TRUE(WizardController::default_controller() != NULL);
  WizardController::default_controller()->AdvanceToScreen(
      WelcomeView::kScreenId);

  // Checking the default locale. Provided that the profile is cleared in SetUp.
  EXPECT_EQ("en-US", g_browser_process->GetApplicationLocale());
  EXPECT_STREQ("en", icu::Locale::getDefault().getLanguage());
  EXPECT_FALSE(base::i18n::IsRTL());
  const std::u16string en_str =
      l10n_util::GetStringUTF16(IDS_NETWORK_SELECTION_TITLE);

  RunSwitchLanguageTest("fr", "fr", true);
  EXPECT_EQ("fr", g_browser_process->GetApplicationLocale());
  EXPECT_STREQ("fr", icu::Locale::getDefault().getLanguage());
  EXPECT_FALSE(base::i18n::IsRTL());
  const std::u16string fr_str =
      l10n_util::GetStringUTF16(IDS_NETWORK_SELECTION_TITLE);

  EXPECT_NE(en_str, fr_str);

  RunSwitchLanguageTest("ar", "ar", true);
  EXPECT_EQ("ar", g_browser_process->GetApplicationLocale());
  EXPECT_STREQ("ar", icu::Locale::getDefault().getLanguage());
  EXPECT_TRUE(base::i18n::IsRTL());
  const std::u16string ar_str =
      l10n_util::GetStringUTF16(IDS_NETWORK_SELECTION_TITLE);

  EXPECT_NE(fr_str, ar_str);
}

IN_PROC_BROWSER_TEST_F(WizardControllerTest, VolumeIsChangedForChromeVox) {
  SetUpCrasAndEnableChromeVox(75 /* volume_percent */, true /* mute_on */);

  // Check that output is unmuted now and at some level.
  CrasAudioHandler* cras = CrasAudioHandler::Get();
  ASSERT_FALSE(cras->IsOutputMuted());
  ASSERT_EQ(WizardController::kMinAudibleOutputVolumePercent,
            cras->GetOutputVolumePercent());
}

IN_PROC_BROWSER_TEST_F(WizardControllerTest, VolumeIsUnchangedForChromeVox) {
  SetUpCrasAndEnableChromeVox(75 /* volume_percent */, false /* mute_on */);

  // Check that output is unmuted now and at some level.
  CrasAudioHandler* cras = CrasAudioHandler::Get();
  ASSERT_FALSE(cras->IsOutputMuted());
  ASSERT_EQ(75, cras->GetOutputVolumePercent());
}

IN_PROC_BROWSER_TEST_F(WizardControllerTest, VolumeIsAdjustedForChromeVox) {
  SetUpCrasAndEnableChromeVox(5 /* volume_percent */, false /* mute_on */);

  // Check that output is unmuted now and at some level.
  CrasAudioHandler* cras = CrasAudioHandler::Get();
  ASSERT_FALSE(cras->IsOutputMuted());
  ASSERT_EQ(WizardController::kMinAudibleOutputVolumePercent,
            cras->GetOutputVolumePercent());
}

class WizardControllerFlowTest : public WizardControllerTest {
 public:
  WizardControllerFlowTest(const WizardControllerFlowTest&) = delete;
  WizardControllerFlowTest& operator=(const WizardControllerFlowTest&) = delete;

 protected:
  WizardControllerFlowTest() {}
  // WizardControllerTest:
  void SetUpOnMainThread() override {
    WizardControllerTest::SetUpOnMainThread();

    // Make sure that OOBE is run as an "official" build.
    LoginDisplayHost::default_host()->GetWizardContext()->is_branded_build =
        true;

    WizardController* wizard_controller =
        WizardController::default_controller();
    wizard_controller->SetCurrentScreen(nullptr);
    wizard_controller->SetSharedURLLoaderFactoryForTesting(
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_url_loader_factory_));

    // Clear portal list (as it is by default in OOBE).
    NetworkHandler::Get()->network_state_handler()->SetCheckPortalList("");

    // Set up the mocks for all screens.
    mock_welcome_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockWelcomeScreen>(
            GetOobeUI()->GetView<WelcomeScreenHandler>(),
            base::BindRepeating(&WizardController::OnWelcomeScreenExit,
                                base::Unretained(wizard_controller))));

    mock_demo_preferences_screen_view_ =
        std::make_unique<MockDemoPreferencesScreenView>();
    mock_demo_preferences_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockDemoPreferencesScreen>(
            mock_demo_preferences_screen_view_.get(),
            base::BindRepeating(&WizardController::OnDemoPreferencesScreenExit,
                                base::Unretained(wizard_controller))));

    mock_arc_terms_of_service_screen_view_ =
        std::make_unique<MockArcTermsOfServiceScreenView>();
    mock_arc_terms_of_service_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockArcTermsOfServiceScreen>(
            mock_arc_terms_of_service_screen_view_.get(),
            base::BindRepeating(
                &WizardController::OnArcTermsOfServiceScreenExit,
                base::Unretained(wizard_controller))));

    device_disabled_screen_view_ =
        std::make_unique<MockDeviceDisabledScreenView>();
    MockScreen(std::make_unique<DeviceDisabledScreen>(
        device_disabled_screen_view_.get()));
    EXPECT_CALL(*device_disabled_screen_view_, Show(_, _, _)).Times(0);

    mock_network_screen_view_ = std::make_unique<MockNetworkScreenView>();
    mock_network_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockNetworkScreen>(
            mock_network_screen_view_.get(),
            base::BindRepeating(&WizardController::OnNetworkScreenExit,
                                base::Unretained(wizard_controller))));

    mock_update_view_ = std::make_unique<MockUpdateView>();
    mock_update_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockUpdateScreen>(
            mock_update_view_.get(), GetErrorScreen(),
            base::BindRepeating(&WizardController::OnUpdateScreenExit,
                                base::Unretained(wizard_controller))));

    mock_eula_view_ = std::make_unique<MockEulaView>();
    mock_eula_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockEulaScreen>(
            mock_eula_view_.get(),
            base::BindRepeating(&WizardController::OnEulaScreenExit,
                                base::Unretained(wizard_controller))));

    mock_enrollment_screen_view_ = std::make_unique<MockEnrollmentScreenView>();
    mock_enrollment_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockEnrollmentScreen>(
            mock_enrollment_screen_view_.get(),
            base::BindRepeating(&WizardController::OnEnrollmentScreenExit,
                                base::Unretained(wizard_controller))));

    mock_auto_enrollment_check_screen_view_ =
        std::make_unique<MockAutoEnrollmentCheckScreenView>();
    ExpectSetDelegate(mock_auto_enrollment_check_screen_view_.get());
    mock_auto_enrollment_check_screen_ = MockScreenExpectLifecycle(
        std::make_unique<MockAutoEnrollmentCheckScreen>(
            mock_auto_enrollment_check_screen_view_.get(), GetErrorScreen(),
            base::BindRepeating(
                &WizardController::OnAutoEnrollmentCheckScreenExit,
                base::Unretained(wizard_controller))));

    mock_wrong_hwid_screen_view_ = std::make_unique<MockWrongHWIDScreenView>();
    ExpectBindUnbind(mock_wrong_hwid_screen_view_.get());
    mock_wrong_hwid_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockWrongHWIDScreen>(
            mock_wrong_hwid_screen_view_.get(),
            base::BindRepeating(&WizardController::OnWrongHWIDScreenExit,
                                base::Unretained(wizard_controller))));

    mock_enable_adb_sideloading_screen_view_ =
        std::make_unique<MockEnableAdbSideloadingScreenView>();
    ExpectBindUnbind(mock_enable_adb_sideloading_screen_view_.get());
    mock_enable_adb_sideloading_screen_ = MockScreenExpectLifecycle(
        std::make_unique<MockEnableAdbSideloadingScreen>(
            mock_enable_adb_sideloading_screen_view_.get(),
            base::BindRepeating(
                &WizardController::OnEnableAdbSideloadingScreenExit,
                base::Unretained(wizard_controller))));

    mock_enable_debugging_screen_view_ =
        std::make_unique<MockEnableDebuggingScreenView>();
    ExpectSetDelegate(mock_enable_debugging_screen_view_.get());
    mock_enable_debugging_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockEnableDebuggingScreen>(
            mock_enable_debugging_screen_view_.get(),
            base::BindRepeating(&WizardController::OnEnableDebuggingScreenExit,
                                base::Unretained(wizard_controller))));

    mock_demo_setup_screen_view_ = std::make_unique<MockDemoSetupScreenView>();
    ExpectBind(mock_demo_setup_screen_view_.get());
    mock_demo_setup_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockDemoSetupScreen>(
            mock_demo_setup_screen_view_.get(),
            base::BindRepeating(&WizardController::OnDemoSetupScreenExit,
                                base::Unretained(wizard_controller))));

    mock_demo_preferences_screen_view_ =
        std::make_unique<MockDemoPreferencesScreenView>();
    ExpectBind(mock_demo_preferences_screen_view_.get());
    mock_demo_preferences_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockDemoPreferencesScreen>(
            mock_demo_preferences_screen_view_.get(),
            base::BindRepeating(&WizardController::OnDemoPreferencesScreenExit,
                                base::Unretained(wizard_controller))));

    mock_arc_terms_of_service_screen_view_ =
        std::make_unique<MockArcTermsOfServiceScreenView>();
    mock_arc_terms_of_service_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockArcTermsOfServiceScreen>(
            mock_arc_terms_of_service_screen_view_.get(),
            base::BindRepeating(
                &WizardController::OnArcTermsOfServiceScreenExit,
                base::Unretained(wizard_controller))));

    // Switch to the initial screen.
    EXPECT_EQ(NULL, wizard_controller->current_screen());
    EXPECT_CALL(*mock_welcome_screen_, ShowImpl()).Times(1);
    wizard_controller->AdvanceToScreen(WelcomeView::kScreenId);
  }

  void TearDownOnMainThread() override {
    mock_welcome_screen_ = nullptr;
    device_disabled_screen_view_.reset();
    test_url_loader_factory_.ClearResponses();
    WizardControllerTest::TearDownOnMainThread();
  }

  void InitTimezoneResolver() {
    network_portal_detector_ = new NetworkPortalDetectorTestImpl();
    network_portal_detector::InitializeForTesting(network_portal_detector_);

    // Default detworks happens to be usually "eth1" in tests.
    const NetworkState* default_network =
        NetworkHandler::Get()->network_state_handler()->DefaultNetwork();

    network_portal_detector_->SetDefaultNetworkForTesting(
        default_network->guid());
    network_portal_detector_->SetDetectionResultsForTesting(
        default_network->guid(),
        NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_ONLINE, 204);
  }

  SimpleGeolocationProvider* GetGeolocationProvider() {
    return WizardController::default_controller()->geolocation_provider_.get();
  }

  void WaitUntilTimezoneResolved() {
    base::RunLoop loop;
    if (!WizardController::default_controller()
             ->SetOnTimeZoneResolvedForTesting(loop.QuitClosure())) {
      return;
    }

    loop.Run();
  }

  void ResetAutoEnrollmentCheckScreen() {
    WizardController::default_controller()
        ->screen_manager()
        ->DeleteScreenForTesting(AutoEnrollmentCheckScreenView::kScreenId);
  }

  void TestControlFlowMain() {
    CheckCurrentScreen(WelcomeView::kScreenId);

    test_url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) {
          if (base::StartsWith(
                  request.url.spec(),
                  SimpleGeolocationProvider::DefaultGeolocationProviderURL()
                      .spec(),
                  base::CompareCase::SENSITIVE)) {
            test_url_loader_factory_.AddResponse(request.url.spec(),
                                                 kGeolocationResponseBody);
          } else if (base::StartsWith(
                         request.url.spec(),
                         DefaultTimezoneProviderURL().spec(),
                         base::CompareCase::SENSITIVE)) {
            test_url_loader_factory_.AddResponse(request.url.spec(),
                                                 kTimezoneResponseBody);
          }
        }));

    ASSERT_TRUE(LoginScreenTestApi::IsLoginShelfShown());

    EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
    EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
    EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
    mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

    CheckCurrentScreen(NetworkScreenView::kScreenId);
    EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
    mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

    CheckCurrentScreen(EulaView::kScreenId);
    // Login shelf should still be visible.
    EXPECT_TRUE(LoginScreenTestApi::IsLoginShelfShown());

    EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
    EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
    // Enable TimeZone resolve
    InitTimezoneResolver();
    mock_eula_screen_->ExitScreen(
        EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);
    EXPECT_TRUE(GetGeolocationProvider());

    // Let update screen smooth time process (time = 0ms).
    content::RunAllPendingInMessageLoop();

    CheckCurrentScreen(UpdateView::kScreenId);
    EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
    EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
    mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

    CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
    EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
    EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(0);
    mock_auto_enrollment_check_screen_->ExitScreen();

    EXPECT_FALSE(ExistingUserController::current_controller() == NULL);
    EXPECT_EQ("ethernet,wifi,cellular", NetworkHandler::Get()
                                            ->network_state_handler()
                                            ->GetCheckPortalListForTest());

    WaitUntilTimezoneResolved();
    EXPECT_EQ(
        "America/Anchorage",
        base::UTF16ToUTF8(
            system::TimezoneSettings::GetInstance()->GetCurrentTimezoneID()));
  }

  // All of the *Screen types are owned by WizardController. The views are owned
  // by this test class.
  MockWelcomeScreen* mock_welcome_screen_ = nullptr;

  MockNetworkScreen* mock_network_screen_ = nullptr;
  std::unique_ptr<MockNetworkScreenView> mock_network_screen_view_;

  MockUpdateScreen* mock_update_screen_ = nullptr;
  std::unique_ptr<MockUpdateView> mock_update_view_;

  MockEulaScreen* mock_eula_screen_ = nullptr;
  std::unique_ptr<MockEulaView> mock_eula_view_;

  MockEnrollmentScreen* mock_enrollment_screen_ = nullptr;
  std::unique_ptr<MockEnrollmentScreenView> mock_enrollment_screen_view_;

  MockAutoEnrollmentCheckScreen* mock_auto_enrollment_check_screen_ = nullptr;
  std::unique_ptr<MockAutoEnrollmentCheckScreenView>
      mock_auto_enrollment_check_screen_view_;

  MockWrongHWIDScreen* mock_wrong_hwid_screen_ = nullptr;
  std::unique_ptr<MockWrongHWIDScreenView> mock_wrong_hwid_screen_view_;

  MockEnableAdbSideloadingScreen* mock_enable_adb_sideloading_screen_ = nullptr;
  std::unique_ptr<MockEnableAdbSideloadingScreenView>
      mock_enable_adb_sideloading_screen_view_;

  MockEnableDebuggingScreen* mock_enable_debugging_screen_ = nullptr;
  std::unique_ptr<MockEnableDebuggingScreenView>
      mock_enable_debugging_screen_view_;

  MockDemoSetupScreen* mock_demo_setup_screen_ = nullptr;
  std::unique_ptr<MockDemoSetupScreenView> mock_demo_setup_screen_view_;

  MockDemoPreferencesScreen* mock_demo_preferences_screen_ = nullptr;
  std::unique_ptr<MockDemoPreferencesScreenView>
      mock_demo_preferences_screen_view_;

  MockArcTermsOfServiceScreen* mock_arc_terms_of_service_screen_ = nullptr;
  std::unique_ptr<MockArcTermsOfServiceScreenView>
      mock_arc_terms_of_service_screen_view_;

  std::unique_ptr<MockDeviceDisabledScreenView> device_disabled_screen_view_;

 private:
  NetworkPortalDetectorTestImpl* network_portal_detector_ = nullptr;
  network::TestURLLoaderFactory test_url_loader_factory_;
  std::unique_ptr<base::AutoReset<bool>> branded_build_override_;
};

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest, ControlFlowMain) {
  TestControlFlowMain();
}

// This test verifies that if WizardController fails to apply a non-critical
// update before the OOBE is marked complete, it allows the user to proceed to
// log in.
IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest,
                       ControlFlowErrorUpdateNonCriticalUpdate) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(0);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  content::RunAllPendingInMessageLoop();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(0);
  mock_auto_enrollment_check_screen_->ExitScreen();

  EXPECT_FALSE(ExistingUserController::current_controller() == NULL);
}

// This test verifies that if WizardController fails to apply a critical update
// before the OOBE is marked complete, it goes back the network selection
// screen and thus prevents the user from proceeding to log in.
IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest,
                       ControlFlowErrorUpdateCriticalUpdate) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(0);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  content::RunAllPendingInMessageLoop();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(0);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(0);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(0);  // last transition
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_ERROR);
  CheckCurrentScreen(NetworkScreenView::kScreenId);
}

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest, ControlFlowSkipUpdateEnroll) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(0);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(0);
  WizardController::default_controller()
      ->wizard_context_->enrollment_triggered_early = true;
  EXPECT_CALL(*mock_enrollment_screen_view_,
              SetEnrollmentConfig(
                  EnrollmentModeMatches(policy::EnrollmentConfig::MODE_MANUAL)))
      .Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);
  content::RunAllPendingInMessageLoop();

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_enrollment_screen_, HideImpl()).Times(0);
  mock_auto_enrollment_check_screen_->ExitScreen();
  content::RunAllPendingInMessageLoop();

  CheckCurrentScreen(EnrollmentScreenView::kScreenId);
  EXPECT_EQ("ethernet,wifi,cellular", NetworkHandler::Get()
                                          ->network_state_handler()
                                          ->GetCheckPortalListForTest());
}

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest, ControlFlowEulaDeclined) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(0);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  mock_eula_screen_->ExitScreen(EulaScreen::Result::BACK);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
}

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest,
                       ControlFlowEnrollmentCompleted) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(0);
  EXPECT_CALL(*mock_enrollment_screen_view_,
              SetEnrollmentConfig(
                  EnrollmentModeMatches(policy::EnrollmentConfig::MODE_MANUAL)))
      .Times(1);
  EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);

  WizardController::default_controller()->AdvanceToScreen(
      EnrollmentScreenView::kScreenId);
  CheckCurrentScreen(EnrollmentScreenView::kScreenId);
  mock_enrollment_screen_->ExitScreen(EnrollmentScreen::Result::COMPLETED);

  EXPECT_FALSE(ExistingUserController::current_controller() == NULL);
}

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest,
                       ControlFlowWrongHWIDScreenFromLogin) {
  CheckCurrentScreen(WelcomeView::kScreenId);

  // Verify and clear all expectations on the mock welcome screen before setting
  // new ones.
  testing::Mock::VerifyAndClearExpectations(mock_welcome_screen_);

  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  LoginDisplayHost::default_host()->StartSignInScreen();
  EXPECT_FALSE(ExistingUserController::current_controller() == NULL);

  EXPECT_CALL(*mock_wrong_hwid_screen_, ShowImpl()).Times(1);
  WizardController::default_controller()->AdvanceToScreen(
      WrongHWIDScreenView::kScreenId);

  CheckCurrentScreen(WrongHWIDScreenView::kScreenId);

  // Verify and clear all expectations on the mock wrong hwid screen before
  // setting new ones.
  testing::Mock::VerifyAndClearExpectations(mock_wrong_hwid_screen_);

  // After warning is skipped, user returns to sign-in screen.
  // And this destroys WizardController.
  EXPECT_CALL(*mock_wrong_hwid_screen_, HideImpl()).Times(1);
  GetWrongHWIDScreen()->OnExit();
  EXPECT_FALSE(ExistingUserController::current_controller() == NULL);
}

// This parameterized test class extends WizardControllerFlowTest to verify how
// WizardController behaves if it does not find or fails to apply an update
// after the OOBE is marked complete.
class WizardControllerUpdateAfterCompletedOobeTest
    : public WizardControllerFlowTest,
      public testing::WithParamInterface<UpdateScreen::Result>,
      public LocalStateMixin::Delegate {
 public:
  WizardControllerUpdateAfterCompletedOobeTest(
      const WizardControllerUpdateAfterCompletedOobeTest&) = delete;
  WizardControllerUpdateAfterCompletedOobeTest& operator=(
      const WizardControllerUpdateAfterCompletedOobeTest&) = delete;

 protected:
  WizardControllerUpdateAfterCompletedOobeTest() = default;

  // LocalStateMixin::Delegate:
  void SetUpLocalState() override {
    StartupUtils::MarkOobeCompleted();  // Pretend OOBE was complete.
  }

 private:
  LocalStateMixin local_state_mixin_{&mixin_host_, this};
};

// This test verifies that if WizardController reports any result after the
// OOBE is marked complete, it allows the user to proceed to log in.
IN_PROC_BROWSER_TEST_P(WizardControllerUpdateAfterCompletedOobeTest,
                       ControlFlowErrorUpdate) {
  CheckCurrentScreen(WelcomeView::kScreenId);

  // Verify and clear all expectations on the mock welcome screen before setting
  // new ones.
  testing::Mock::VerifyAndClearExpectations(mock_welcome_screen_);

  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(0);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);

  // Verify and clear all expectations on the mock network screen before setting
  // new ones.
  testing::Mock::VerifyAndClearExpectations(mock_network_screen_);

  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);

  testing::Mock::VerifyAndClearExpectations(mock_eula_screen_);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  content::RunAllPendingInMessageLoop();

  CheckCurrentScreen(UpdateView::kScreenId);

  testing::Mock::VerifyAndClearExpectations(mock_update_screen_);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(GetParam());

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);

  testing::Mock::VerifyAndClearExpectations(mock_auto_enrollment_check_screen_);
  testing::Mock::VerifyAndClearExpectations(mock_eula_screen_);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(0);
  mock_auto_enrollment_check_screen_->ExitScreen();

  EXPECT_NE(nullptr, ExistingUserController::current_controller());
}

INSTANTIATE_TEST_SUITE_P(
    All,
    WizardControllerUpdateAfterCompletedOobeTest,
    testing::Values(UpdateScreen::Result::UPDATE_NOT_REQUIRED,
                    UpdateScreen::Result::UPDATE_ERROR));

class WizardControllerDeviceStateTest : public WizardControllerFlowTest {
 public:
  WizardControllerDeviceStateTest(const WizardControllerDeviceStateTest&) =
      delete;
  WizardControllerDeviceStateTest& operator=(
      const WizardControllerDeviceStateTest&) = delete;

 protected:
  WizardControllerDeviceStateTest() {
    fake_statistics_provider_.SetMachineStatistic(
        system::kSerialNumberKeyForTest, "test");
    fake_statistics_provider_.SetMachineStatistic(system::kActivateDateKey,
                                                  "2000-01");
  }

  static AutoEnrollmentController* auto_enrollment_controller() {
    return WizardController::default_controller()
        ->GetAutoEnrollmentController();
  }

  static void WaitForAutoEnrollmentState(policy::AutoEnrollmentState state) {
    base::RunLoop loop;
    base::CallbackListSubscription progress_subscription =
        auto_enrollment_controller()->RegisterProgressCallback(
            base::BindRepeating(&QuitLoopOnAutoEnrollmentProgress, state,
                                &loop));
    loop.Run();
  }

  // WizardControllerFlowTest:
  void SetUpOnMainThread() override {
    WizardControllerFlowTest::SetUpOnMainThread();

    histogram_tester_ = std::make_unique<base::HistogramTester>();

    // Initialize the FakeShillManagerClient. This does not happen
    // automatically because of the `DBusThreadManager::Initialize`
    // call in `SetUpInProcessBrowserTestFixture`. See https://crbug.com/847422.
    // TODO(pmarko): Find a way for FakeShillManagerClient to be initialized
    // automatically (https://crbug.com/847422).
    DBusThreadManager::Get()
        ->GetShillManagerClient()
        ->GetTestInterface()
        ->SetupDefaultEnvironment();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WizardControllerFlowTest::SetUpCommandLine(command_line);

    command_line->AppendSwitchASCII(
        switches::kEnterpriseEnableForcedReEnrollment,
        AutoEnrollmentController::kForcedReEnrollmentAlways);
    command_line->AppendSwitchASCII(
        switches::kEnterpriseEnrollmentInitialModulus, "1");
    command_line->AppendSwitchASCII(switches::kEnterpriseEnrollmentModulusLimit,
                                    "2");
  }

  system::ScopedFakeStatisticsProvider fake_statistics_provider_;

  base::HistogramTester* histogram_tester() { return histogram_tester_.get(); }

 private:
  DeviceStateMixin device_state_{&mixin_host_,
                                 DeviceStateMixin::State::BEFORE_OOBE};

  std::unique_ptr<base::HistogramTester> histogram_tester_;
};

IN_PROC_BROWSER_TEST_F(WizardControllerDeviceStateTest,
                       ControlFlowNoForcedReEnrollmentOnFirstBoot) {
  fake_statistics_provider_.ClearMachineStatistic(system::kActivateDateKey);
  EXPECT_NE(policy::AUTO_ENROLLMENT_STATE_NO_ENROLLMENT,
            auto_enrollment_controller()->state());

  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  content::RunAllPendingInMessageLoop();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  mock_auto_enrollment_check_screen_->RealShow();
  EXPECT_EQ(policy::AUTO_ENROLLMENT_STATE_NO_ENROLLMENT,
            auto_enrollment_controller()->state());
  EXPECT_EQ(1,
            FakeInstallAttributesClient::Get()
                ->remove_firmware_management_parameters_from_tpm_call_count());
  EXPECT_EQ(1, FakeSessionManagerClient::Get()
                   ->clear_forced_re_enrollment_vpd_call_count());
}

// TODO(https://crbug.com/911661) Flaky time outs on Linux Chromium OS ASan
// LSan bot.
#if defined(ADDRESS_SANITIZER)
#define MAYBE_ControlFlowDeviceDisabled DISABLED_ControlFlowDeviceDisabled
#else
#define MAYBE_ControlFlowDeviceDisabled ControlFlowDeviceDisabled
#endif
IN_PROC_BROWSER_TEST_F(WizardControllerDeviceStateTest,
                       MAYBE_ControlFlowDeviceDisabled) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  content::RunAllPendingInMessageLoop();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
  mock_auto_enrollment_check_screen_->RealShow();

  // Wait for auto-enrollment controller to encounter the connection error.
  WaitForAutoEnrollmentState(policy::AUTO_ENROLLMENT_STATE_CONNECTION_ERROR);

  // The error screen shows up if device state could not be retrieved.
  EXPECT_FALSE(StartupUtils::IsOobeCompleted());
  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_EQ(AutoEnrollmentCheckScreenView::kScreenId.AsId(),
            GetErrorScreen()->GetParentScreen());
  base::DictionaryValue device_state;
  device_state.SetString(policy::kDeviceStateMode,
                         policy::kDeviceStateModeDisabled);
  device_state.SetString(policy::kDeviceStateDisabledMessage, kDisabledMessage);
  g_browser_process->local_state()->Set(prefs::kServerBackedDeviceState,
                                        device_state);
  EXPECT_CALL(*device_disabled_screen_view_, Show(_, _, kDisabledMessage))
      .Times(1);
  mock_auto_enrollment_check_screen_->ExitScreen();

  base::RunLoop().RunUntilIdle();
  ResetAutoEnrollmentCheckScreen();

  // Make sure the device disabled screen is shown.
  CheckCurrentScreen(DeviceDisabledScreenView::kScreenId);

  EXPECT_EQ(0,
            FakeInstallAttributesClient::Get()
                ->remove_firmware_management_parameters_from_tpm_call_count());
  EXPECT_EQ(0, FakeSessionManagerClient::Get()
                   ->clear_forced_re_enrollment_vpd_call_count());

  EXPECT_FALSE(StartupUtils::IsOobeCompleted());
}

// Allows testing different behavior if forced re-enrollment is performed but
// not explicitly required (instantiated with `false`) vs. if forced
// re-enrollment is explicitly required (instantiated with `true`).
class WizardControllerDeviceStateExplicitRequirementTest
    : public WizardControllerDeviceStateTest,
      public testing::WithParamInterface<bool /* fre_explicitly_required */> {
 public:
  WizardControllerDeviceStateExplicitRequirementTest(
      const WizardControllerDeviceStateExplicitRequirementTest&) = delete;
  WizardControllerDeviceStateExplicitRequirementTest& operator=(
      const WizardControllerDeviceStateExplicitRequirementTest&) = delete;

 protected:
  WizardControllerDeviceStateExplicitRequirementTest() {
    if (IsFREExplicitlyRequired()) {
      fake_statistics_provider_.SetMachineStatistic(
          chromeos::system::kCheckEnrollmentKey, "1");
    }
  }

  // Returns true if forced re-enrollment was explicitly required (which
  // corresponds to the check_enrollment VPD value being set to "1").
  bool IsFREExplicitlyRequired() { return GetParam(); }
};

// Test the control flow for Forced Re-Enrollment. First, a connection error
// occurs, leading to a network error screen. On the network error screen, the
// test verifies that the user may enter a guest session if FRE was not
// explicitly required, and that the user may not enter a guest session if FRE
// was explicitly required. Then, a retry is performed and FRE indicates that
// the device should be enrolled.
IN_PROC_BROWSER_TEST_P(WizardControllerDeviceStateExplicitRequirementTest,
                       ControlFlowForcedReEnrollment) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
  mock_auto_enrollment_check_screen_->RealShow();

  // Wait for auto-enrollment controller to encounter the connection error.
  WaitForAutoEnrollmentState(policy::AUTO_ENROLLMENT_STATE_CONNECTION_ERROR);

  // The error screen shows up if there's no auto-enrollment decision.
  EXPECT_FALSE(StartupUtils::IsOobeCompleted());
  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_EQ(AutoEnrollmentCheckScreenView::kScreenId.AsId(),
            GetErrorScreen()->GetParentScreen());

  if (IsFREExplicitlyRequired()) {
    // Check that guest sign-in is not allowed on the network error screen
    // (because the check_enrollment VPD key was set to "1", making FRE
    // explicitly required).
    test::OobeJS().ExpectHiddenPath(kGuestSessionLink);
  } else {
    // Check that guest sign-in is allowed if FRE was not explicitly required.
    test::OobeJS().ExpectVisiblePath(kGuestSessionLink);
  }
  EXPECT_EQ(0,
            FakeInstallAttributesClient::Get()
                ->remove_firmware_management_parameters_from_tpm_call_count());
  EXPECT_EQ(0, FakeSessionManagerClient::Get()
                   ->clear_forced_re_enrollment_vpd_call_count());

  base::DictionaryValue device_state;
  device_state.SetString(policy::kDeviceStateMode,
                         policy::kDeviceStateRestoreModeReEnrollmentEnforced);
  g_browser_process->local_state()->Set(prefs::kServerBackedDeviceState,
                                        device_state);
  EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(
      *mock_enrollment_screen_view_,
      SetEnrollmentConfig(
          EnrollmentModeMatches(policy::EnrollmentConfig::MODE_SERVER_FORCED)))
      .Times(1);
  mock_auto_enrollment_check_screen_->ExitScreen();

  ResetAutoEnrollmentCheckScreen();

  // Make sure enterprise enrollment page shows up.
  CheckCurrentScreen(EnrollmentScreenView::kScreenId);
  mock_enrollment_screen_->ExitScreen(EnrollmentScreen::Result::COMPLETED);

  EXPECT_TRUE(StartupUtils::IsOobeCompleted());
}

// Tests that a server error occurs during a check for Forced Re-Enrollment.
// When Forced Re-Enrollment is not explicitly required (there is no
// "check_enrollment" VPD key), the expectation is that the server error is
// treated as "don't force enrollment".
// When Forced Re-Enrollment is explicitly required (the "check_enrollment" VPD
// key is set to "1"), the expectation is that a network error screen shows up
// (from which it's not possible to enter a Guest session).
IN_PROC_BROWSER_TEST_P(WizardControllerDeviceStateExplicitRequirementTest,
                       ControlFlowForcedReEnrollmentServerError) {
  ScopedFakeAutoEnrollmentClientFactory fake_auto_enrollment_client_factory(
      auto_enrollment_controller());

  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  mock_auto_enrollment_check_screen_->RealShow();

  policy::FakeAutoEnrollmentClient* fake_auto_enrollment_client =
      fake_auto_enrollment_client_factory.WaitAutoEnrollmentClientCreated();
  if (IsFREExplicitlyRequired()) {
    // Expect that the auto enrollment screen will be hidden, because OOBE is
    // switching to the error screen.
    EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);

    // Make AutoEnrollmentClient notify the controller that a server error
    // occurred.
    fake_auto_enrollment_client->SetState(
        policy::AUTO_ENROLLMENT_STATE_SERVER_ERROR);
    base::RunLoop().RunUntilIdle();

    // The error screen shows up.
    EXPECT_FALSE(StartupUtils::IsOobeCompleted());
    CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
    EXPECT_EQ(AutoEnrollmentCheckScreenView::kScreenId.AsId(),
              GetErrorScreen()->GetParentScreen());

    // Check that guest sign-in is not allowed on the network error screen
    // (because the check_enrollment VPD key was set to "1", making FRE
    // explicitly required).
    test::OobeJS().ExpectHiddenPath(kGuestSessionLink);

    base::DictionaryValue device_state;
    device_state.SetString(policy::kDeviceStateMode,
                           policy::kDeviceStateRestoreModeReEnrollmentEnforced);
    g_browser_process->local_state()->Set(prefs::kServerBackedDeviceState,
                                          device_state);
    EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);
    EXPECT_CALL(*mock_enrollment_screen_view_,
                SetEnrollmentConfig(EnrollmentModeMatches(
                    policy::EnrollmentConfig::MODE_SERVER_FORCED)))
        .Times(1);
    fake_auto_enrollment_client->SetState(
        policy::AUTO_ENROLLMENT_STATE_TRIGGER_ENROLLMENT);
    mock_auto_enrollment_check_screen_->ExitScreen();

    ResetAutoEnrollmentCheckScreen();

    // Make sure enterprise enrollment page shows up.
    CheckCurrentScreen(EnrollmentScreenView::kScreenId);
    mock_enrollment_screen_->ExitScreen(EnrollmentScreen::Result::COMPLETED);

    EXPECT_TRUE(StartupUtils::IsOobeCompleted());
    EXPECT_EQ(
        0, FakeInstallAttributesClient::Get()
               ->remove_firmware_management_parameters_from_tpm_call_count());
    EXPECT_EQ(0, FakeSessionManagerClient::Get()
                     ->clear_forced_re_enrollment_vpd_call_count());
  } else {
    // Make AutoEnrollmentClient notify the controller that a server error
    // occurred.
    fake_auto_enrollment_client->SetState(
        policy::AUTO_ENROLLMENT_STATE_SERVER_ERROR);
    base::RunLoop().RunUntilIdle();

    EXPECT_TRUE(StartupUtils::IsOobeCompleted());
    // Don't expect that the auto enrollment screen will be hidden, because
    // OOBE is exited from the auto enrollment screen. Instead only expect
    // that the sign-in screen is reached.
    OobeScreenWaiter(GetFirstSigninScreen()).Wait();
    EXPECT_EQ(
        0, FakeInstallAttributesClient::Get()
               ->remove_firmware_management_parameters_from_tpm_call_count());
    EXPECT_EQ(0, FakeSessionManagerClient::Get()
                     ->clear_forced_re_enrollment_vpd_call_count());
  }
}

INSTANTIATE_TEST_SUITE_P(All,
                         WizardControllerDeviceStateExplicitRequirementTest,
                         testing::Values(false, true));

class WizardControllerDeviceStateWithInitialEnrollmentTest
    : public WizardControllerDeviceStateTest {
 public:
  WizardControllerDeviceStateWithInitialEnrollmentTest(
      const WizardControllerDeviceStateWithInitialEnrollmentTest&) = delete;
  WizardControllerDeviceStateWithInitialEnrollmentTest& operator=(
      const WizardControllerDeviceStateWithInitialEnrollmentTest&) = delete;

 protected:
  WizardControllerDeviceStateWithInitialEnrollmentTest() {
    fake_statistics_provider_.SetMachineStatistic(
        system::kSerialNumberKeyForTest, "test");
    fake_statistics_provider_.SetMachineStatistic(system::kRlzBrandCodeKey,
                                                  "AABC");
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WizardControllerDeviceStateTest::SetUpCommandLine(command_line);

    // Enable usage of fake PSM (private set membership) RLWE client (for tests
    // checking initial enrollment).
    command_line->AppendSwitch(
        switches::kEnterpriseUseFakePsmRlweClientForTesting);

    command_line->AppendSwitchASCII(
        switches::kEnterpriseEnableInitialEnrollment,
        AutoEnrollmentController::kInitialEnrollmentAlways);
  }

  // Test initial enrollment. This method is shared by the tests for initial
  // enrollment for a device that is new or in consumer mode.
  void DoInitialEnrollment() {
    fake_statistics_provider_.SetMachineStatistic(
        system::kEnterpriseManagementEmbargoEndDateKey,
        GenerateEmbargoEndDate(-15 /* days_offset */));
    CheckCurrentScreen(WelcomeView::kScreenId);
    EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
    EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
    mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

    CheckCurrentScreen(NetworkScreenView::kScreenId);
    EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
    EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
    mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

    CheckCurrentScreen(EulaView::kScreenId);
    EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
    EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
    mock_eula_screen_->ExitScreen(
        EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

    // Wait for auto-enrollment controller to encounter the connection error.
    WaitForAutoEnrollmentState(policy::AUTO_ENROLLMENT_STATE_CONNECTION_ERROR);

    // Let update screen smooth time process (time = 0ms).
    base::RunLoop().RunUntilIdle();

    CheckCurrentScreen(UpdateView::kScreenId);
    EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
    EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
    mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

    CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
    EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
    mock_auto_enrollment_check_screen_->RealShow();

    // The error screen shows up if there's no auto-enrollment decision.
    EXPECT_FALSE(StartupUtils::IsOobeCompleted());
    EXPECT_EQ(AutoEnrollmentCheckScreenView::kScreenId.AsId(),
              GetErrorScreen()->GetParentScreen());
    base::DictionaryValue device_state;
    device_state.SetString(policy::kDeviceStateMode,
                           policy::kDeviceStateRestoreModeReEnrollmentEnforced);
    g_browser_process->local_state()->Set(prefs::kServerBackedDeviceState,
                                          device_state);
    EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);
    EXPECT_CALL(*mock_enrollment_screen_view_,
                SetEnrollmentConfig(EnrollmentModeMatches(
                    policy::EnrollmentConfig::MODE_SERVER_FORCED)))
        .Times(1);
    mock_auto_enrollment_check_screen_->ExitScreen();

    ResetAutoEnrollmentCheckScreen();

    // Make sure enterprise enrollment page shows up.
    CheckCurrentScreen(EnrollmentScreenView::kScreenId);
    mock_enrollment_screen_->ExitScreen(EnrollmentScreen::Result::COMPLETED);

    EXPECT_TRUE(StartupUtils::IsOobeCompleted());
  }

  SystemClockClient::TestInterface* system_clock_client() {
    return SystemClockClient::Get()->GetTestInterface();
  }
};

// Tests that a device that is brand new properly does initial enrollment.
IN_PROC_BROWSER_TEST_F(WizardControllerDeviceStateWithInitialEnrollmentTest,
                       ControlFlowInitialEnrollment) {
  fake_statistics_provider_.ClearMachineStatistic(system::kActivateDateKey);

  DoInitialEnrollment();
}

// Tests that a device that is in consumer mode can do another initial
// enrollment.
IN_PROC_BROWSER_TEST_F(WizardControllerDeviceStateWithInitialEnrollmentTest,
                       ControlFlowSecondaryInitialEnrollment) {
  // Mark the device has being in consumer mode.
  fake_statistics_provider_.SetMachineStatistic(system::kCheckEnrollmentKey,
                                                "0");

  DoInitialEnrollment();
}

// Tests that a server error occurs during the Initial Enrollment check.  The
// expectation is that a network error screen shows up (from which it's possible
// to enter a Guest session).
IN_PROC_BROWSER_TEST_F(WizardControllerDeviceStateWithInitialEnrollmentTest,
                       ControlFlowInitialEnrollmentServerError) {
  ScopedFakeAutoEnrollmentClientFactory fake_auto_enrollment_client_factory(
      auto_enrollment_controller());

  fake_statistics_provider_.ClearMachineStatistic(system::kActivateDateKey);
  fake_statistics_provider_.SetMachineStatistic(
      system::kEnterpriseManagementEmbargoEndDateKey,
      GenerateEmbargoEndDate(-15 /* days_offset */));
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  mock_auto_enrollment_check_screen_->RealShow();

  policy::FakeAutoEnrollmentClient* fake_auto_enrollment_client =
      fake_auto_enrollment_client_factory.WaitAutoEnrollmentClientCreated();

  // Expect that the auto enrollment screen will be hidden, because OOBE is
  // switching to the error screen.
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);

  // Make AutoEnrollmentClient notify the controller that a server error
  // occurred.
  fake_auto_enrollment_client->SetState(
      policy::AUTO_ENROLLMENT_STATE_SERVER_ERROR);
  base::RunLoop().RunUntilIdle();

  // The error screen shows up if there's no auto-enrollment decision.
  EXPECT_FALSE(StartupUtils::IsOobeCompleted());
  EXPECT_EQ(AutoEnrollmentCheckScreenView::kScreenId.AsId(),
            GetErrorScreen()->GetParentScreen());

  // Check that guest sign-in is allowed on the network error screen for initial
  // enrollment.
  test::OobeJS().ExpectVisiblePath(kGuestSessionLink);

  base::DictionaryValue device_state;
  device_state.SetString(policy::kDeviceStateMode,
                         policy::kDeviceStateRestoreModeReEnrollmentEnforced);
  g_browser_process->local_state()->Set(prefs::kServerBackedDeviceState,
                                        device_state);
  EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(
      *mock_enrollment_screen_view_,
      SetEnrollmentConfig(
          EnrollmentModeMatches(policy::EnrollmentConfig::MODE_SERVER_FORCED)))
      .Times(1);
  fake_auto_enrollment_client->SetState(
      policy::AUTO_ENROLLMENT_STATE_TRIGGER_ENROLLMENT);
  mock_auto_enrollment_check_screen_->ExitScreen();

  ResetAutoEnrollmentCheckScreen();

  // Make sure enterprise enrollment page shows up.
  CheckCurrentScreen(EnrollmentScreenView::kScreenId);
  mock_enrollment_screen_->ExitScreen(EnrollmentScreen::Result::COMPLETED);

  EXPECT_TRUE(StartupUtils::IsOobeCompleted());
}

IN_PROC_BROWSER_TEST_F(WizardControllerDeviceStateWithInitialEnrollmentTest,
                       ControlFlowNoInitialEnrollmentDuringEmbargoPeriod) {
  system_clock_client()->SetNetworkSynchronized(true);
  system_clock_client()->NotifyObserversSystemClockUpdated();

  fake_statistics_provider_.ClearMachineStatistic(system::kActivateDateKey);
  fake_statistics_provider_.SetMachineStatistic(
      system::kEnterpriseManagementEmbargoEndDateKey,
      GenerateEmbargoEndDate(1 /* days_offset */));
  EXPECT_NE(policy::AUTO_ENROLLMENT_STATE_NO_ENROLLMENT,
            auto_enrollment_controller()->state());

  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  mock_auto_enrollment_check_screen_->RealShow();
  EXPECT_EQ(policy::AUTO_ENROLLMENT_STATE_NO_ENROLLMENT,
            auto_enrollment_controller()->state());
}

IN_PROC_BROWSER_TEST_F(WizardControllerDeviceStateWithInitialEnrollmentTest,
                       ControlFlowWaitSystemClockSyncThenEmbargoPeriod) {
  fake_statistics_provider_.ClearMachineStatistic(system::kActivateDateKey);
  fake_statistics_provider_.SetMachineStatistic(
      system::kEnterpriseManagementEmbargoEndDateKey,
      GenerateEmbargoEndDate(1 /* days_offset */));
  EXPECT_NE(policy::AUTO_ENROLLMENT_STATE_NO_ENROLLMENT,
            auto_enrollment_controller()->state());

  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  mock_auto_enrollment_check_screen_->RealShow();
  EXPECT_EQ(AutoEnrollmentController::AutoEnrollmentCheckType::
                kUnknownDueToMissingSystemClockSync,
            auto_enrollment_controller()->auto_enrollment_check_type());

  system_clock_client()->SetNetworkSynchronized(true);
  system_clock_client()->NotifyObserversSystemClockUpdated();

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(AutoEnrollmentController::AutoEnrollmentCheckType::kNone,
            auto_enrollment_controller()->auto_enrollment_check_type());
  EXPECT_EQ(policy::AUTO_ENROLLMENT_STATE_NO_ENROLLMENT,
            auto_enrollment_controller()->state());
}

IN_PROC_BROWSER_TEST_F(WizardControllerDeviceStateWithInitialEnrollmentTest,
                       ControlFlowWaitSystemClockSyncTimeout) {
  auto task_runner = base::MakeRefCounted<base::TestMockTimeTaskRunner>();

  base::TestMockTimeTaskRunner::ScopedContext scoped_context(task_runner);
  fake_statistics_provider_.ClearMachineStatistic(system::kActivateDateKey);
  fake_statistics_provider_.SetMachineStatistic(
      system::kEnterpriseManagementEmbargoEndDateKey,
      GenerateEmbargoEndDate(1 /* days_offset */));
  EXPECT_NE(policy::AUTO_ENROLLMENT_STATE_NO_ENROLLMENT,
            auto_enrollment_controller()->state());

  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  task_runner->RunUntilIdle();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  mock_auto_enrollment_check_screen_->RealShow();
  EXPECT_EQ(AutoEnrollmentController::AutoEnrollmentCheckType::
                kUnknownDueToMissingSystemClockSync,
            auto_enrollment_controller()->auto_enrollment_check_type());

  // The timeout is 45 seconds, see `auto_enrollment_controller.cc`.
  // Fast-forward by a bit more than that.
  task_runner->FastForwardBy(base::Seconds(45 + 1));

  EXPECT_EQ(AutoEnrollmentController::AutoEnrollmentCheckType::kNone,
            auto_enrollment_controller()->auto_enrollment_check_type());
  EXPECT_EQ(policy::AUTO_ENROLLMENT_STATE_NO_ENROLLMENT,
            auto_enrollment_controller()->state());
}

IN_PROC_BROWSER_TEST_F(WizardControllerDeviceStateWithInitialEnrollmentTest,
                       ControlFlowWaitSystemClockSyncThenInitialEnrollment) {
  ScopedFakeAutoEnrollmentClientFactory fake_auto_enrollment_client_factory(
      auto_enrollment_controller());

  fake_statistics_provider_.ClearMachineStatistic(system::kActivateDateKey);
  fake_statistics_provider_.SetMachineStatistic(
      system::kEnterpriseManagementEmbargoEndDateKey,
      GenerateEmbargoEndDate(1 /* days_offset */));
  EXPECT_NE(policy::AUTO_ENROLLMENT_STATE_NO_ENROLLMENT,
            auto_enrollment_controller()->state());

  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  mock_auto_enrollment_check_screen_->RealShow();
  EXPECT_EQ(AutoEnrollmentController::AutoEnrollmentCheckType::
                kUnknownDueToMissingSystemClockSync,
            auto_enrollment_controller()->auto_enrollment_check_type());

  // Simulate that the clock moved forward, passing the embargo period, by
  // moving the embargo period back in time.
  fake_statistics_provider_.SetMachineStatistic(
      system::kEnterpriseManagementEmbargoEndDateKey,
      GenerateEmbargoEndDate(-1 /* days_offset */));
  base::DictionaryValue device_state;
  device_state.SetString(policy::kDeviceStateMode,
                         policy::kDeviceStateRestoreModeReEnrollmentEnforced);
  g_browser_process->local_state()->Set(prefs::kServerBackedDeviceState,
                                        device_state);

  system_clock_client()->SetNetworkSynchronized(true);
  system_clock_client()->NotifyObserversSystemClockUpdated();

  policy::FakeAutoEnrollmentClient* fake_auto_enrollment_client =
      fake_auto_enrollment_client_factory.WaitAutoEnrollmentClientCreated();

  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);

  EXPECT_CALL(
      *mock_enrollment_screen_view_,
      SetEnrollmentConfig(
          EnrollmentModeMatches(policy::EnrollmentConfig::MODE_SERVER_FORCED)))
      .Times(1);
  mock_auto_enrollment_check_screen_->ExitScreen();
  ResetAutoEnrollmentCheckScreen();

  fake_auto_enrollment_client->SetState(
      policy::AUTO_ENROLLMENT_STATE_TRIGGER_ENROLLMENT);

  // Make sure enterprise enrollment page shows up.
  CheckCurrentScreen(EnrollmentScreenView::kScreenId);
  mock_enrollment_screen_->ExitScreen(EnrollmentScreen::Result::COMPLETED);
  EXPECT_TRUE(StartupUtils::IsOobeCompleted());
}

class WizardControllerScreenPriorityOOBETest : public OobeBaseTest {
 protected:
  WizardControllerScreenPriorityOOBETest() = default;
  ~WizardControllerScreenPriorityOOBETest() override = default;

  void CheckCurrentScreen(OobeScreenId screen) {
    EXPECT_EQ(WizardController::default_controller()->GetScreen(screen),
              WizardController::default_controller()->current_screen());
  }
};

IN_PROC_BROWSER_TEST_F(WizardControllerScreenPriorityOOBETest,
                       DefaultPriorityTest) {
  ASSERT_TRUE(WizardController::default_controller() != nullptr);
  LoginDisplayHost::default_host()->GetWizardContext()->is_branded_build = true;

  CheckCurrentScreen(WelcomeView::kScreenId);
  // Showing network screen should pass it has default priority which is same as
  // welcome screen.
  WizardController::default_controller()->AdvanceToScreen(
      NetworkScreenView::kScreenId);
  CheckCurrentScreen(NetworkScreenView::kScreenId);

  // Showing eula screen should pass it has default priority which is same as
  // network screen.
  WizardController::default_controller()->AdvanceToScreen(EulaView::kScreenId);
  CheckCurrentScreen(EulaView::kScreenId);

  // Showing update screen should pass it has default priority which is same as
  // eula screen.
  WizardController::default_controller()->AdvanceToScreen(
      UpdateView::kScreenId);
  CheckCurrentScreen(UpdateView::kScreenId);
}

class WizardControllerScreenPriorityTest : public LoginManagerTest,
                                           public LocalStateMixin::Delegate {
 protected:
  WizardControllerScreenPriorityTest() {
    login_manager_mixin_.AppendRegularUsers(1);
  }
  ~WizardControllerScreenPriorityTest() override = default;

  void CheckCurrentScreen(OobeScreenId screen) {
    EXPECT_EQ(WizardController::default_controller()->GetScreen(screen),
              WizardController::default_controller()->current_screen());
  }

  // LocalStateMixin::Delegate:
  void SetUpLocalState() override {
    // Set pref to show reset screen on startup.
    g_browser_process->local_state()->SetBoolean(prefs::kFactoryResetRequested,
                                                 true);
  }

 private:
  LoginManagerMixin login_manager_mixin_{&mixin_host_};
  LocalStateMixin local_state_mixin_{&mixin_host_, this};
};

IN_PROC_BROWSER_TEST_F(WizardControllerScreenPriorityTest, CanNavigateToTest) {
  WizardController* const wizard_controller =
      WizardController::default_controller();
  ASSERT_TRUE(wizard_controller != nullptr);
  EXPECT_EQ(1, LoginScreenTestApi::GetUsersCount());

  // Check reset screen is visible on startup.
  OobeScreenWaiter(ResetView::kScreenId).Wait();
  EXPECT_TRUE(LoginScreenTestApi::IsOobeDialogVisible());

  // Showing update required screen should fail due to lower priority than reset
  // screen.
  LoginDisplayHost::default_host()->StartWizard(UpdateRequiredView::kScreenId);
  CheckCurrentScreen(ResetView::kScreenId);
  // Wizard controller should not be recreated.
  EXPECT_EQ(wizard_controller, WizardController::default_controller());

  // Showing device disabled screen is allowed due to higher priority than reset
  // screen.
  LoginDisplayHost::default_host()->StartWizard(
      DeviceDisabledScreenView::kScreenId);
  CheckCurrentScreen(DeviceDisabledScreenView::kScreenId);
  // Wizard controller should not be recreated.
  EXPECT_EQ(wizard_controller, WizardController::default_controller());

  // Showing update required screen should fail due to lower priority than
  // device disabled screen.
  LoginDisplayHost::default_host()->StartWizard(UpdateRequiredView::kScreenId);
  CheckCurrentScreen(DeviceDisabledScreenView::kScreenId);
  EXPECT_EQ(wizard_controller, WizardController::default_controller());
}

class WizardControllerBrokenLocalStateTest : public WizardControllerTest {
 public:
  WizardControllerBrokenLocalStateTest(
      const WizardControllerBrokenLocalStateTest&) = delete;
  WizardControllerBrokenLocalStateTest& operator=(
      const WizardControllerBrokenLocalStateTest&) = delete;

 protected:
  WizardControllerBrokenLocalStateTest() = default;
  ~WizardControllerBrokenLocalStateTest() override = default;

  // WizardControllerTest:
  void SetUpInProcessBrowserTestFixture() override {
    WizardControllerTest::SetUpInProcessBrowserTestFixture();
    PrefServiceFactory factory;
    factory.set_user_prefs(base::MakeRefCounted<PrefStoreStub>());
    local_state_ = factory.Create(new PrefRegistrySimple());
    WizardController::set_local_state_for_testing(local_state_.get());
  }

 private:
  std::unique_ptr<PrefService> local_state_;
};

IN_PROC_BROWSER_TEST_F(WizardControllerBrokenLocalStateTest,
                       LocalStateCorrupted) {
  // Checks that after wizard controller initialization error screen
  // in the proper state is displayed.
  ASSERT_EQ(GetErrorScreen(),
            WizardController::default_controller()->current_screen());
  ASSERT_EQ(NetworkError::UI_STATE_LOCAL_STATE_ERROR,
            GetErrorScreen()->GetUIState());

  OobeScreenWaiter(ErrorScreenView::kScreenId).Wait();

  // Checks visibility of the error message and powerwash button.
  test::OobeJS().ExpectVisible("error-message");
  test::OobeJS().ExpectVisiblePath({"error-message", "powerwashButton"});
  test::OobeJS().ExpectVisiblePath({"error-message", "localStateErrorText"});
  test::OobeJS().ExpectVisiblePath({"error-message", "guestSessionText"});

  // Emulates user click on the "Restart and Powerwash" button.
  ASSERT_EQ(0, FakeSessionManagerClient::Get()->start_device_wipe_call_count());
  test::OobeJS().TapOnPath({"error-message", "powerwashButton"});
  ASSERT_EQ(1, FakeSessionManagerClient::Get()->start_device_wipe_call_count());
}

class WizardControllerProxyAuthOnSigninTest : public WizardControllerTest {
 public:
  WizardControllerProxyAuthOnSigninTest(
      const WizardControllerProxyAuthOnSigninTest&) = delete;
  WizardControllerProxyAuthOnSigninTest& operator=(
      const WizardControllerProxyAuthOnSigninTest&) = delete;

 protected:
  WizardControllerProxyAuthOnSigninTest()
      : proxy_server_(net::SpawnedTestServer::TYPE_BASIC_AUTH_PROXY,
                      base::FilePath()) {}
  ~WizardControllerProxyAuthOnSigninTest() override {}

  // WizardControllerTest:
  void SetUp() override {
    ASSERT_TRUE(proxy_server_.Start());
    WizardControllerTest::SetUp();
  }

  void SetUpOnMainThread() override {
    WizardControllerTest::SetUpOnMainThread();
    WizardController::default_controller()->AdvanceToScreen(
        WelcomeView::kScreenId);
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WizardControllerTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(::switches::kProxyServer,
                                    proxy_server_.host_port_pair().ToString());
  }

  net::SpawnedTestServer& proxy_server() { return proxy_server_; }

 private:
  net::SpawnedTestServer proxy_server_;
};

IN_PROC_BROWSER_TEST_F(WizardControllerProxyAuthOnSigninTest,
                       ProxyAuthDialogOnSigninScreen) {
  content::WindowedNotificationObserver auth_needed_waiter(
      chrome::NOTIFICATION_AUTH_NEEDED,
      content::NotificationService::AllSources());

  CheckCurrentScreen(WelcomeView::kScreenId);

  LoginDisplayHost::default_host()->StartSignInScreen();
  auth_needed_waiter.Wait();
}

class WizardControllerKioskFlowTest : public WizardControllerFlowTest {
 public:
  WizardControllerKioskFlowTest(const WizardControllerKioskFlowTest&) = delete;
  WizardControllerKioskFlowTest& operator=(
      const WizardControllerKioskFlowTest&) = delete;

 protected:
  WizardControllerKioskFlowTest() {}

  // WizardControllerFlowTest:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    WizardControllerFlowTest::SetUpCommandLine(command_line);
    base::FilePath test_data_dir;
    ASSERT_TRUE(chromeos::test_utils::GetTestDataPath(
        "app_mode", "kiosk_manifest", &test_data_dir));
    command_line->AppendSwitchPath(
        switches::kAppOemManifestFile,
        test_data_dir.AppendASCII("kiosk_manifest.json"));
  }
};

IN_PROC_BROWSER_TEST_F(WizardControllerKioskFlowTest,
                       ControlFlowKioskForcedEnrollment) {
  EXPECT_CALL(
      *mock_enrollment_screen_view_,
      SetEnrollmentConfig(
          EnrollmentModeMatches(policy::EnrollmentConfig::MODE_LOCAL_FORCED)))
      .Times(1);
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  content::RunAllPendingInMessageLoop();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);
  mock_auto_enrollment_check_screen_->ExitScreen();

  EXPECT_FALSE(StartupUtils::IsOobeCompleted());

  // Make sure enterprise enrollment page shows up right after update screen.
  CheckCurrentScreen(EnrollmentScreenView::kScreenId);
  mock_enrollment_screen_->ExitScreen(EnrollmentScreen::Result::COMPLETED);

  EXPECT_TRUE(StartupUtils::IsOobeCompleted());
}

IN_PROC_BROWSER_TEST_F(WizardControllerKioskFlowTest,
                       ControlFlowEnrollmentBack) {
  EXPECT_CALL(
      *mock_enrollment_screen_view_,
      SetEnrollmentConfig(
          EnrollmentModeMatches(policy::EnrollmentConfig::MODE_LOCAL_FORCED)))
      .Times(1);

  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);
  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::NEXT);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_REGULAR);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);
  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  // Let update screen smooth time process (time = 0ms).
  content::RunAllPendingInMessageLoop();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_enrollment_screen_, HideImpl()).Times(1);
  mock_auto_enrollment_check_screen_->ExitScreen();

  EXPECT_FALSE(StartupUtils::IsOobeCompleted());

  // Make sure enterprise enrollment page shows up right after update screen.
  CheckCurrentScreen(EnrollmentScreenView::kScreenId);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);
  mock_enrollment_screen_->ExitScreen(EnrollmentScreen::Result::BACK);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_FALSE(StartupUtils::IsOobeCompleted());
}

class WizardControllerEnableAdbSideloadingTest
    : public WizardControllerFlowTest {
 public:
  WizardControllerEnableAdbSideloadingTest(
      const WizardControllerEnableAdbSideloadingTest&) = delete;
  WizardControllerEnableAdbSideloadingTest& operator=(
      const WizardControllerEnableAdbSideloadingTest&) = delete;

 protected:
  WizardControllerEnableAdbSideloadingTest() = default;

  template <class T>
  void SkipToScreen(OobeScreenId screen, T* screen_mock) {
    EXPECT_CALL(*screen_mock, ShowImpl()).Times(1);
    auto* const wizard_controller = WizardController::default_controller();
    wizard_controller->AdvanceToScreen(screen);
  }
};

IN_PROC_BROWSER_TEST_F(WizardControllerEnableAdbSideloadingTest,
                       ShowAndEnableSideloading) {
  CheckCurrentScreen(WelcomeView::kScreenId);

  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  SkipToScreen(EnableAdbSideloadingScreenView::kScreenId,
               mock_enable_adb_sideloading_screen_);
  CheckCurrentScreen(EnableAdbSideloadingScreenView::kScreenId);

  test::OobeJS().ClickOnPath(
      {"adb-sideloading", "enable-adb-sideloading-ok-button"});

  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(EnableAdbSideloadingScreenView::kScreenId);
  EXPECT_CALL(*mock_enable_adb_sideloading_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, ShowImpl()).Times(1);

  mock_enable_adb_sideloading_screen_->ExitScreen();

  // Let update screen smooth time process (time = 0ms).
  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(WelcomeView::kScreenId);
}

IN_PROC_BROWSER_TEST_F(WizardControllerEnableAdbSideloadingTest,
                       ShowAndDoNotEnableSideloading) {
  CheckCurrentScreen(WelcomeView::kScreenId);

  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  SkipToScreen(EnableAdbSideloadingScreenView::kScreenId,
               mock_enable_adb_sideloading_screen_);
  CheckCurrentScreen(EnableAdbSideloadingScreenView::kScreenId);

  test::OobeJS().ClickOnPath(
      {"adb-sideloading", "enable-adb-sideloading-cancel-button"});

  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(EnableAdbSideloadingScreenView::kScreenId);
  EXPECT_CALL(*mock_enable_adb_sideloading_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, ShowImpl()).Times(1);

  mock_enable_adb_sideloading_screen_->ExitScreen();

  // Let update screen smooth time process (time = 0ms).
  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(WelcomeView::kScreenId);
}

class WizardControllerEnableDebuggingTest : public WizardControllerFlowTest {
 public:
  WizardControllerEnableDebuggingTest(
      const WizardControllerEnableDebuggingTest&) = delete;
  WizardControllerEnableDebuggingTest& operator=(
      const WizardControllerEnableDebuggingTest&) = delete;

 protected:
  WizardControllerEnableDebuggingTest() {}

  // MixinBasedInProcessBrowserTest:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    WizardControllerFlowTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(chromeos::switches::kSystemDevMode);
  }
};

IN_PROC_BROWSER_TEST_F(WizardControllerEnableDebuggingTest,
                       ShowAndCancelEnableDebugging) {
  CheckCurrentScreen(WelcomeView::kScreenId);

  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_enable_debugging_screen_, ShowImpl()).Times(1);

  mock_welcome_screen_->ExitScreen(WelcomeScreen::Result::ENABLE_DEBUGGING);

  content::RunAllPendingInMessageLoop();

  CheckCurrentScreen(EnableDebuggingScreenView::kScreenId);
  EXPECT_CALL(*mock_enable_debugging_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, ShowImpl()).Times(1);

  mock_enable_debugging_screen_->ExitScreen();

  // Let update screen smooth time process (time = 0ms).
  content::RunAllPendingInMessageLoop();

  CheckCurrentScreen(WelcomeView::kScreenId);
}

class WizardControllerDemoSetupTest : public WizardControllerFlowTest {
 public:
  WizardControllerDemoSetupTest(const WizardControllerDemoSetupTest&) = delete;
  WizardControllerDemoSetupTest& operator=(
      const WizardControllerDemoSetupTest&) = delete;

 protected:
  WizardControllerDemoSetupTest() = default;
  ~WizardControllerDemoSetupTest() override = default;

  // MixinBasedInProcessBrowserTest:
  void SetUpOnMainThread() override {
    WizardControllerFlowTest::SetUpOnMainThread();
    testing::Mock::VerifyAndClearExpectations(mock_welcome_screen_);
  }

  template <class T>
  void SkipToScreen(OobeScreenId screen, T* screen_mock) {
    EXPECT_CALL(*screen_mock, ShowImpl()).Times(1);
    auto* const wizard_controller = WizardController::default_controller();
    wizard_controller->SimulateDemoModeSetupForTesting();
    wizard_controller->AdvanceToScreen(screen);
  }
};

IN_PROC_BROWSER_TEST_F(WizardControllerDemoSetupTest,
                       OnlineDemoSetupFlowFinished) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_demo_preferences_screen_, ShowImpl()).Times(1);

  WizardController::default_controller()->StartDemoModeSetup();

  CheckCurrentScreen(DemoPreferencesScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_demo_preferences_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);

  mock_demo_preferences_screen_->ExitScreen(
      DemoPreferencesScreen::Result::COMPLETED);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);

  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_DEMO);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_arc_terms_of_service_screen_, ShowImpl()).Times(1);

  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  CheckCurrentScreen(ArcTermsOfServiceScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_arc_terms_of_service_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);

  mock_arc_terms_of_service_screen_->ExitScreen(
      ArcTermsOfServiceScreen::Result::ACCEPTED_DEMO_ONLINE);

  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);

  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_demo_setup_screen_, ShowImpl()).Times(1);

  mock_auto_enrollment_check_screen_->ExitScreen();

  CheckCurrentScreen(DemoSetupScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  mock_demo_setup_screen_->ExitScreen(DemoSetupScreen::Result::COMPLETED);

  EXPECT_TRUE(StartupUtils::IsOobeCompleted());
  EXPECT_TRUE(ExistingUserController::current_controller());
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());
}

IN_PROC_BROWSER_TEST_F(WizardControllerDemoSetupTest,
                       OfflineDemoSetupFlowFinished) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_demo_preferences_screen_, ShowImpl()).Times(1);

  WizardController::default_controller()->StartDemoModeSetup();

  CheckCurrentScreen(DemoPreferencesScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_demo_preferences_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);

  mock_demo_preferences_screen_->ExitScreen(
      DemoPreferencesScreen::Result::COMPLETED);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);

  mock_network_screen_->ExitScreen(NetworkScreen::Result::OFFLINE_DEMO_SETUP);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_arc_terms_of_service_screen_, ShowImpl()).Times(1);

  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  CheckCurrentScreen(ArcTermsOfServiceScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_arc_terms_of_service_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_demo_setup_screen_, ShowImpl()).Times(1);

  mock_arc_terms_of_service_screen_->ExitScreen(
      ArcTermsOfServiceScreen::Result::ACCEPTED_DEMO_OFFLINE);

  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(DemoSetupScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  mock_demo_setup_screen_->ExitScreen(DemoSetupScreen::Result::COMPLETED);

  EXPECT_TRUE(StartupUtils::IsOobeCompleted());
  EXPECT_TRUE(ExistingUserController::current_controller());
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());
}

IN_PROC_BROWSER_TEST_F(WizardControllerDemoSetupTest, DemoSetupCanceled) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_demo_preferences_screen_, ShowImpl()).Times(1);

  WizardController::default_controller()->StartDemoModeSetup();

  CheckCurrentScreen(DemoPreferencesScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_demo_preferences_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);

  mock_demo_preferences_screen_->ExitScreen(
      DemoPreferencesScreen::Result::COMPLETED);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);

  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_DEMO);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_arc_terms_of_service_screen_, ShowImpl()).Times(1);

  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  CheckCurrentScreen(ArcTermsOfServiceScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_arc_terms_of_service_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);

  mock_arc_terms_of_service_screen_->ExitScreen(
      ArcTermsOfServiceScreen::Result::ACCEPTED_DEMO_ONLINE);

  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);

  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_demo_setup_screen_, ShowImpl()).Times(1);

  mock_auto_enrollment_check_screen_->ExitScreen();

  CheckCurrentScreen(DemoSetupScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_demo_setup_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, ShowImpl()).Times(1);

  mock_demo_setup_screen_->ExitScreen(DemoSetupScreen::Result::CANCELED);

  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());
  EXPECT_FALSE(StartupUtils::IsOobeCompleted());
}

IN_PROC_BROWSER_TEST_F(WizardControllerDemoSetupTest, DemoPreferencesCanceled) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());
  SkipToScreen(DemoPreferencesScreenView::kScreenId,
               mock_demo_preferences_screen_);

  CheckCurrentScreen(DemoPreferencesScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_demo_preferences_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, ShowImpl()).Times(1);

  mock_demo_preferences_screen_->ExitScreen(
      DemoPreferencesScreen::Result::CANCELED);

  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());
}

IN_PROC_BROWSER_TEST_F(WizardControllerDemoSetupTest, NetworkBackPressed) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());
  SkipToScreen(NetworkScreenView::kScreenId, mock_network_screen_);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_demo_preferences_screen_, ShowImpl()).Times(1);

  mock_network_screen_->ExitScreen(NetworkScreen::Result::BACK_DEMO);

  CheckCurrentScreen(DemoPreferencesScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());
}

IN_PROC_BROWSER_TEST_F(WizardControllerDemoSetupTest, EulaBackPressed) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());
  SkipToScreen(EulaView::kScreenId, mock_eula_screen_);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);

  mock_eula_screen_->ExitScreen(EulaScreen::Result::BACK);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());
}

IN_PROC_BROWSER_TEST_F(WizardControllerDemoSetupTest, ArcTosBackPressed) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  // User cannot go to ARC ToS screen without accepting eula - simulate that.
  StartupUtils::MarkEulaAccepted();
  SkipToScreen(ArcTermsOfServiceScreenView::kScreenId,
               mock_arc_terms_of_service_screen_);

  CheckCurrentScreen(ArcTermsOfServiceScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_arc_terms_of_service_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);

  mock_arc_terms_of_service_screen_->ExitScreen(
      ArcTermsOfServiceScreen::Result::BACK);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());
}

class WizardControllerDemoSetupDeviceDisabledTest
    : public WizardControllerDeviceStateTest {
 public:
  WizardControllerDemoSetupDeviceDisabledTest(
      const WizardControllerDemoSetupDeviceDisabledTest&) = delete;
  WizardControllerDemoSetupDeviceDisabledTest& operator=(
      const WizardControllerDemoSetupDeviceDisabledTest&) = delete;

 protected:
  WizardControllerDemoSetupDeviceDisabledTest() = default;
  ~WizardControllerDemoSetupDeviceDisabledTest() override = default;

  // MixinBasedInProcessBrowserTest:
  void SetUpOnMainThread() override {
    WizardControllerDeviceStateTest::SetUpOnMainThread();
    testing::Mock::VerifyAndClearExpectations(mock_welcome_screen_);
  }
};

IN_PROC_BROWSER_TEST_F(WizardControllerDemoSetupDeviceDisabledTest,
                       OnlineDemoSetup) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_demo_preferences_screen_, ShowImpl()).Times(1);

  WizardController::default_controller()->StartDemoModeSetup();

  CheckCurrentScreen(DemoPreferencesScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_demo_preferences_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_network_screen_, ShowImpl()).Times(1);

  mock_demo_preferences_screen_->ExitScreen(
      DemoPreferencesScreen::Result::COMPLETED);

  CheckCurrentScreen(NetworkScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_network_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_eula_screen_, ShowImpl()).Times(1);

  mock_network_screen_->ExitScreen(NetworkScreen::Result::CONNECTED_DEMO);

  CheckCurrentScreen(EulaView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_eula_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_arc_terms_of_service_screen_, ShowImpl()).Times(1);

  mock_eula_screen_->ExitScreen(
      EulaScreen::Result::ACCEPTED_WITHOUT_USAGE_STATS_REPORTING);

  CheckCurrentScreen(ArcTermsOfServiceScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_arc_terms_of_service_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_update_screen_, ShowImpl()).Times(1);

  mock_arc_terms_of_service_screen_->ExitScreen(
      ArcTermsOfServiceScreen::Result::ACCEPTED_DEMO_ONLINE);

  base::RunLoop().RunUntilIdle();

  CheckCurrentScreen(UpdateView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_update_screen_, HideImpl()).Times(1);
  EXPECT_CALL(*mock_auto_enrollment_check_screen_, ShowImpl()).Times(1);

  mock_update_screen_->RunExit(UpdateScreen::Result::UPDATE_NOT_REQUIRED);

  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_TRUE(DemoSetupController::IsOobeDemoSetupFlowInProgress());

  EXPECT_CALL(*mock_auto_enrollment_check_screen_, HideImpl()).Times(1);

  mock_auto_enrollment_check_screen_->RealShow();

  // Wait for auto-enrollment controller to encounter the connection error.
  WaitForAutoEnrollmentState(policy::AUTO_ENROLLMENT_STATE_CONNECTION_ERROR);

  // The error screen shows up if device state could not be retrieved.
  CheckCurrentScreen(AutoEnrollmentCheckScreenView::kScreenId);
  EXPECT_EQ(AutoEnrollmentCheckScreenView::kScreenId.AsId(),
            GetErrorScreen()->GetParentScreen());
  base::DictionaryValue device_state;
  device_state.SetString(policy::kDeviceStateMode,
                         policy::kDeviceStateModeDisabled);
  device_state.SetString(policy::kDeviceStateDisabledMessage, kDisabledMessage);
  g_browser_process->local_state()->Set(prefs::kServerBackedDeviceState,
                                        device_state);

  EXPECT_CALL(*device_disabled_screen_view_, Show(_, _, _)).Times(1);
  mock_auto_enrollment_check_screen_->ExitScreen();

  base::RunLoop().RunUntilIdle();

  ResetAutoEnrollmentCheckScreen();
  CheckCurrentScreen(DeviceDisabledScreenView::kScreenId);

  EXPECT_FALSE(StartupUtils::IsOobeCompleted());
  EXPECT_FALSE(DemoSetupController::IsOobeDemoSetupFlowInProgress());
}

class WizardControllerOobeResumeTest : public WizardControllerTest {
 public:
  WizardControllerOobeResumeTest(const WizardControllerOobeResumeTest&) =
      delete;
  WizardControllerOobeResumeTest& operator=(
      const WizardControllerOobeResumeTest&) = delete;

 protected:
  WizardControllerOobeResumeTest() {}
  // WizardControllerTest:
  void SetUpOnMainThread() override {
    WizardControllerTest::SetUpOnMainThread();

    // Make sure that OOBE is run as an "official" build.
    LoginDisplayHost::default_host()->GetWizardContext()->is_branded_build =
        true;

    WizardController* wizard_controller =
        WizardController::default_controller();
    wizard_controller->SetCurrentScreen(nullptr);

    // Clear portal list (as it is by default in OOBE).
    NetworkHandler::Get()->network_state_handler()->SetCheckPortalList("");

    // Set up the mocks for all screens.
    mock_welcome_view_ = std::make_unique<MockWelcomeView>();
    ExpectBindUnbind(mock_welcome_view_.get());
    mock_welcome_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockWelcomeScreen>(
            mock_welcome_view_.get(),
            base::BindRepeating(&WizardController::OnWelcomeScreenExit,
                                base::Unretained(wizard_controller))));

    mock_enrollment_screen_view_ = std::make_unique<MockEnrollmentScreenView>();
    mock_enrollment_screen_ =
        MockScreenExpectLifecycle(std::make_unique<MockEnrollmentScreen>(
            mock_enrollment_screen_view_.get(),
            base::BindRepeating(&WizardController::OnEnrollmentScreenExit,
                                base::Unretained(wizard_controller))));
  }

  std::unique_ptr<MockWelcomeView> mock_welcome_view_;
  MockWelcomeScreen* mock_welcome_screen_;

  std::unique_ptr<MockEnrollmentScreenView> mock_enrollment_screen_view_;
  MockEnrollmentScreen* mock_enrollment_screen_;

  std::unique_ptr<base::AutoReset<bool>> branded_build_override_;
};

IN_PROC_BROWSER_TEST_F(WizardControllerOobeResumeTest,
                       PRE_ControlFlowResumeInterruptedOobe) {
  // Switch to the initial screen.
  EXPECT_CALL(*mock_welcome_screen_, ShowImpl()).Times(1);
  WizardController::default_controller()->AdvanceToScreen(
      WelcomeView::kScreenId);
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_enrollment_screen_view_,
              SetEnrollmentConfig(
                  EnrollmentModeMatches(policy::EnrollmentConfig::MODE_MANUAL)))
      .Times(1);
  EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);

  WizardController::default_controller()->AdvanceToScreen(
      EnrollmentScreenView::kScreenId);
  CheckCurrentScreen(EnrollmentScreenView::kScreenId);
}

IN_PROC_BROWSER_TEST_F(WizardControllerOobeResumeTest,
                       ControlFlowResumeInterruptedOobe) {
  EXPECT_EQ(EnrollmentScreenView::kScreenId.AsId(),
            WizardController::default_controller()->first_screen_for_testing());
}

class WizardControllerOnboardingResumeTest : public WizardControllerTest {
 protected:
  DeviceStateMixin device_state_{
      &mixin_host_, DeviceStateMixin::State::OOBE_COMPLETED_UNOWNED};
  FakeGaiaMixin gaia_mixin_{&mixin_host_};
  LoginManagerMixin login_mixin_{&mixin_host_, LoginManagerMixin::UserList(),
                                 &gaia_mixin_};
  AccountId user_{
      AccountId::FromUserEmailGaiaId(test::kTestEmail, test::kTestGaiaId)};
};

IN_PROC_BROWSER_TEST_F(WizardControllerOnboardingResumeTest,
                       PRE_ControlFlowResumeInterruptedOnboarding) {
  OobeScreenWaiter(UserCreationView::kScreenId).Wait();
  LoginManagerMixin::TestUserInfo test_user(user_);
  login_mixin_.LoginWithDefaultContext(test_user);
  OobeScreenExitWaiter(UserCreationView::kScreenId).Wait();
  WizardController::default_controller()->AdvanceToScreen(
      MarketingOptInScreenView::kScreenId);
  OobeScreenWaiter(MarketingOptInScreenView::kScreenId).Wait();
}

IN_PROC_BROWSER_TEST_F(WizardControllerOnboardingResumeTest,
                       ControlFlowResumeInterruptedOnboarding) {
  login_mixin_.LoginAsNewRegularUser();
  OobeScreenWaiter(MarketingOptInScreenView::kScreenId).Wait();
}

class WizardControllerCellularFirstTest : public WizardControllerFlowTest {
 public:
  WizardControllerCellularFirstTest(const WizardControllerCellularFirstTest&) =
      delete;
  WizardControllerCellularFirstTest& operator=(
      const WizardControllerCellularFirstTest&) = delete;

 protected:
  WizardControllerCellularFirstTest() {}

  // WizardControllerFlowTest:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    WizardControllerFlowTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kCellularFirst);
  }
};

IN_PROC_BROWSER_TEST_F(WizardControllerCellularFirstTest, CellularFirstFlow) {
  TestControlFlowMain();
}

class WizardControllerOobeConfigurationTest : public WizardControllerTest {
 public:
  WizardControllerOobeConfigurationTest(
      const WizardControllerOobeConfigurationTest&) = delete;
  WizardControllerOobeConfigurationTest& operator=(
      const WizardControllerOobeConfigurationTest&) = delete;

 protected:
  WizardControllerOobeConfigurationTest() {}

  // WizardControllerTest:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    WizardControllerTest::SetUpCommandLine(command_line);

    base::FilePath configuration_file;
    ASSERT_TRUE(chromeos::test_utils::GetTestDataPath(
        "oobe_configuration", "non_empty_configuration.json",
        &configuration_file));
    command_line->AppendSwitchPath(switches::kFakeOobeConfiguration,
                                   configuration_file);
  }

  // WizardControllerTest:
  void SetUpOnMainThread() override {
    WizardControllerTest::SetUpOnMainThread();
    // Clear portal list (as it is by default in OOBE).
    NetworkHandler::Get()->network_state_handler()->SetCheckPortalList("");
  }
};

IN_PROC_BROWSER_TEST_F(WizardControllerOobeConfigurationTest,
                       ConfigurationIsLoaded) {
  OobeScreenWaiter(WelcomeView::kScreenId).Wait();
  WelcomeScreen* screen =
      WizardController::default_controller()->GetScreen<WelcomeScreen>();
  base::Value* configuration = screen->GetConfigurationForTesting();
  ASSERT_NE(configuration, nullptr);
  EXPECT_FALSE(configuration->DictEmpty());
}

class WizardControllerRollbackFlowTest : public WizardControllerFlowTest {
 public:
  WizardControllerRollbackFlowTest(const WizardControllerRollbackFlowTest&) =
      delete;
  WizardControllerRollbackFlowTest& operator=(
      const WizardControllerRollbackFlowTest&) = delete;

 protected:
  WizardControllerRollbackFlowTest() {}

  void SetUp() override {
    std::unique_ptr<FakeRollbackNetworkConfig> network_config =
        std::make_unique<FakeRollbackNetworkConfig>();
    network_config_ = network_config.get();
    // Release ownership of network config. It is to be deleted via `Shutdown`.
    rollback_network_config::OverrideInProcessInstanceForTesting(
        std::move(network_config));
    WizardControllerFlowTest::SetUp();
  }

  void TearDown() override {
    rollback_network_config::Shutdown();
    WizardControllerFlowTest::TearDown();
  }

  // WizardControllerTest:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    WizardControllerFlowTest::SetUpCommandLine(command_line);

    base::FilePath configuration_file;
    ASSERT_TRUE(chromeos::test_utils::GetTestDataPath(
        "oobe_configuration", "TestEnterpriseRollbackRecover.json",
        &configuration_file));
    command_line->AppendSwitchPath(switches::kFakeOobeConfiguration,
                                   configuration_file);
  }

  content::MockNotificationObserver observer_;
  content::NotificationRegistrar registrar_;

  FakeRollbackNetworkConfig* network_config_;
};

IN_PROC_BROWSER_TEST_F(WizardControllerRollbackFlowTest,
                       RestartChromeAfterRollback) {
  registrar_.Add(&observer_, chrome::NOTIFICATION_APP_TERMINATING,
                 content::NotificationService::AllSources());
  EXPECT_CALL(observer_, Observe(chrome::NOTIFICATION_APP_TERMINATING, _, _));

  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);
  WizardController::default_controller()->AdvanceToScreen(
      EnrollmentScreenView::kScreenId);
  CheckCurrentScreen(EnrollmentScreenView::kScreenId);
  mock_enrollment_screen_->ExitScreen(EnrollmentScreen::Result::COMPLETED);
}

IN_PROC_BROWSER_TEST_F(WizardControllerRollbackFlowTest,
                       ImportNetworkConfigAfterRollback) {
  CheckCurrentScreen(WelcomeView::kScreenId);
  EXPECT_CALL(*mock_enrollment_screen_, ShowImpl()).Times(1);
  EXPECT_CALL(*mock_welcome_screen_, HideImpl()).Times(1);

  WizardController::default_controller()->AdvanceToScreen(
      EnrollmentScreenView::kScreenId);
  CheckCurrentScreen(EnrollmentScreenView::kScreenId);
  ASSERT_TRUE(network_config_->imported_config() != nullptr);

  const base::Value* network_list =
      network_config_->imported_config()->FindListKey("NetworkConfigurations");
  ASSERT_TRUE(network_list);
  ASSERT_TRUE(network_list->is_list());

  const base::Value& network = network_list->GetList()[0];
  ASSERT_TRUE(network.is_dict());

  const std::string* guid = network.FindStringKey("GUID");
  ASSERT_TRUE(guid);
  EXPECT_EQ(*guid, "wpa-psk-network-guid");
}

// TODO(nkostylev): Add test for WebUI accelerators http://crosbug.com/22571

// TODO(merkulova): Add tests for bluetooth HID detection screen variations when
// UI and logic is ready. http://crbug.com/127016

// TODO(khmel): Add tests for ARC OptIn flow.
// http://crbug.com/651144

// TODO(fukino): Add tests for encryption migration UI.
// http://crbug.com/706017

// TODO(alemate): Add tests for Sync Consent UI.

// TODO(rsgingerrs): Add tests for Recommend Apps UI.

// TODO(alemate): Add tests for Marketing Opt-In.

// TODO(khorimoto): Add tests for MultiDevice Setup UI.

}  // namespace ash
