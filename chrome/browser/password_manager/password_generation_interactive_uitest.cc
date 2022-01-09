// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "build/build_config.h"
#include "chrome/browser/password_manager/chrome_password_manager_client.h"
#include "chrome/browser/password_manager/password_manager_interactive_test_base.h"
#include "chrome/browser/password_manager/password_manager_uitest_util.h"
#include "chrome/browser/password_manager/password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/passwords/password_generation_popup_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/password_manager/core/browser/password_form_manager.h"
#include "components/password_manager/core/browser/password_generation_frame_helper.h"
#include "components/password_manager/core/browser/password_manager_util.h"
#include "components/password_manager/core/browser/test_password_store.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/prerender_test_util.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace {

enum ReturnCodes {  // Possible results of the JavaScript code.
  RETURN_CODE_OK,
  RETURN_CODE_NO_ELEMENT,
  RETURN_CODE_INVALID,
};

}  // namespace

class PasswordGenerationInteractiveTest
    : public PasswordManagerInteractiveTestBase {
 public:
  void SetUpOnMainThread() override {
    PasswordManagerBrowserTestBase::SetUpOnMainThread();
    // Disable Autofill requesting access to AddressBook data. This will cause
    // the tests to hang on Mac.
    autofill::test::DisableSystemServices(browser()->profile()->GetPrefs());

    // Set observer for popup.
    ChromePasswordManagerClient* client =
        ChromePasswordManagerClient::FromWebContents(WebContents());
    client->SetTestObserver(&observer_);

    password_manager::PasswordFormManager::
        set_wait_for_server_predictions_for_filling(false);

    NavigateToFile("/password/signup_form_new_password.html");
  }

  void TearDownOnMainThread() override {
    PasswordManagerBrowserTestBase::TearDownOnMainThread();

    autofill::test::ReenableSystemServices();
  }

  // Waits until the value of the field with id |field_id| becomes non-empty.
  void WaitForNonEmptyFieldValue(const std::string& field_id) {
    const std::string script = base::StringPrintf(
        "element = document.getElementById('%s');"
        "if (!element) {"
        "  setTimeout(window.domAutomationController.send(%d), 0);"
        "}"
        "if (element.value) {"
        "  setTimeout(window.domAutomationController.send(%d), 0); "
        "} else {"
        "  element.onchange = function() {"
        "    if (element.value) {"
        "      window.domAutomationController.send(%d);"
        "    }"
        "  }"
        "}",
        field_id.c_str(), RETURN_CODE_NO_ELEMENT, RETURN_CODE_OK,
        RETURN_CODE_OK);
    EXPECT_EQ(RETURN_CODE_OK,
              content::EvalJs(RenderFrameHost(), script,
                              content::EXECUTE_SCRIPT_NO_USER_GESTURE |
                                  content::EXECUTE_SCRIPT_USE_MANUAL_REPLY));
  }

  std::string GetFocusedElement() {
    std::string focused_element;
    EXPECT_TRUE(content::ExecuteScriptAndExtractString(
        WebContents(),
        "window.domAutomationController.send("
        "    document.activeElement.id)",
        &focused_element));
    return focused_element;
  }

  void FocusPasswordField() {
    ASSERT_TRUE(content::ExecuteScript(
        WebContents(), "document.getElementById('password_field').focus()"));
  }

  void FocusUsernameField() {
    ASSERT_TRUE(content::ExecuteScript(
        WebContents(), "document.getElementById('username_field').focus();"));
  }

  void SendKeyToPopup(ui::KeyboardCode key) {
    content::NativeWebKeyboardEvent event(
        blink::WebKeyboardEvent::Type::kRawKeyDown,
        blink::WebInputEvent::kNoModifiers,
        blink::WebInputEvent::GetStaticTimeStampForTests());
    event.windows_key_code = key;
    WebContents()
        ->GetMainFrame()
        ->GetRenderViewHost()
        ->GetWidget()
        ->ForwardKeyboardEvent(event);
  }

  bool GenerationPopupShowing() {
    return observer_.popup_showing() &&
           observer_.state() ==
               PasswordGenerationPopupController::kOfferGeneration;
  }

  bool EditingPopupShowing() {
    return observer_.popup_showing() &&
           observer_.state() ==
               PasswordGenerationPopupController::kEditGeneratedPassword;
  }

  void WaitForStatus(TestGenerationPopupObserver::GenerationPopup status) {
    observer_.WaitForStatus(status);
  }

  void WaitForGenerationPopupShowing() {
    if (GenerationPopupShowing())
      return;
    observer_.WaitForStatusChange();
    EXPECT_TRUE(GenerationPopupShowing());
  }

 private:
  TestGenerationPopupObserver observer_;
};

IN_PROC_BROWSER_TEST_F(PasswordGenerationInteractiveTest,
                       PopupShownAndPasswordSelected) {
  FocusPasswordField();
  EXPECT_TRUE(GenerationPopupShowing());
  base::HistogramTester histogram_tester;
  SendKeyToPopup(ui::VKEY_DOWN);
  SendKeyToPopup(ui::VKEY_RETURN);

  // Selecting the password should fill the field and move focus to the
  // submit button.
  WaitForNonEmptyFieldValue("password_field");
  EXPECT_FALSE(GenerationPopupShowing());
  EXPECT_FALSE(EditingPopupShowing());
  EXPECT_EQ("input_submit_button", GetFocusedElement());

  // Re-focusing the password field should show the editing popup.
  FocusPasswordField();
  EXPECT_TRUE(EditingPopupShowing());

  // The metrics are recorded when the form manager is destroyed. Closing the
  // tab enforces it.
  CloseAllBrowsers();
  histogram_tester.ExpectUniqueSample(
      "PasswordGeneration.UserDecision",
      password_manager::PasswordFormMetricsRecorder::GeneratedPasswordStatus::
          kPasswordAccepted,
      1);
}

IN_PROC_BROWSER_TEST_F(PasswordGenerationInteractiveTest,
                       PopupShownAutomaticallyAndPasswordErased) {
  FocusPasswordField();
  EXPECT_TRUE(GenerationPopupShowing());
  SendKeyToPopup(ui::VKEY_DOWN);
  SendKeyToPopup(ui::VKEY_RETURN);

  // Wait until the password is filled.
  WaitForNonEmptyFieldValue("password_field");

  // Re-focusing the password field should show the editing popup.
  FocusPasswordField();
  EXPECT_TRUE(EditingPopupShowing());

  // Delete the password. The generation prompt should be visible.
  base::HistogramTester histogram_tester;
  SimulateUserDeletingFieldContent("password_field");
  WaitForGenerationPopupShowing();

  // The metrics are recorded on navigation when the frame is destroyed.
  NavigateToFile("/password/done.html");
  histogram_tester.ExpectUniqueSample(
      "PasswordGeneration.UserDecision",
      password_manager::PasswordFormMetricsRecorder::GeneratedPasswordStatus::
          kPasswordDeleted,
      1);
}

IN_PROC_BROWSER_TEST_F(PasswordGenerationInteractiveTest,
                       PopupShownManuallyAndPasswordErased) {
  NavigateToFile("/password/password_form.html");
  FocusPasswordField();
  EXPECT_FALSE(GenerationPopupShowing());
  // The same flow happens when user generates a password from the context menu.
  password_manager_util::UserTriggeredManualGenerationFromContextMenu(
      ChromePasswordManagerClient::FromWebContents(WebContents()));
  WaitForStatus(TestGenerationPopupObserver::GenerationPopup::kShown);
  EXPECT_TRUE(GenerationPopupShowing());
  SendKeyToPopup(ui::VKEY_DOWN);
  SendKeyToPopup(ui::VKEY_RETURN);

  // Wait until the password is filled.
  WaitForNonEmptyFieldValue("password_field");

  // Re-focusing the password field should show the editing popup.
  FocusPasswordField();
  EXPECT_TRUE(EditingPopupShowing());

  // Delete the password. The generation prompt should not be visible.
  SimulateUserDeletingFieldContent("password_field");
  WaitForStatus(TestGenerationPopupObserver::GenerationPopup::kHidden);
  EXPECT_FALSE(EditingPopupShowing());
  EXPECT_FALSE(GenerationPopupShowing());
}

IN_PROC_BROWSER_TEST_F(PasswordGenerationInteractiveTest,
                       PopupShownAndDismissed) {
  FocusPasswordField();
  EXPECT_TRUE(GenerationPopupShowing());

  FocusUsernameField();

  // Popup is dismissed.
  WaitForStatus(TestGenerationPopupObserver::GenerationPopup::kHidden);
}

IN_PROC_BROWSER_TEST_F(PasswordGenerationInteractiveTest,
                       PopupShownAndDismissedByKeyPress) {
  FocusPasswordField();
  EXPECT_TRUE(GenerationPopupShowing());

  SendKeyToPopup(ui::VKEY_ESCAPE);

  // Popup is dismissed.
  EXPECT_FALSE(GenerationPopupShowing());
}

IN_PROC_BROWSER_TEST_F(PasswordGenerationInteractiveTest,
                       PopupShownAndDismissedByScrolling) {
  FocusPasswordField();
  EXPECT_TRUE(GenerationPopupShowing());

  ASSERT_TRUE(
      content::ExecuteScript(WebContents(), "window.scrollTo(100, 0);"));

  EXPECT_FALSE(GenerationPopupShowing());
}

IN_PROC_BROWSER_TEST_F(PasswordGenerationInteractiveTest,
                       GenerationTriggeredInIFrame) {
  NavigateToFile("/password/framed_signup_form.html");

  // Execute the script in the context of the iframe so that it kinda receives a
  // user gesture.
  content::RenderFrameHost* child_frame = ChildFrameAt(WebContents(), 0);

  std::string focus_script =
      "document.getElementById('password_field').focus();";

  ASSERT_TRUE(content::ExecuteScript(child_frame, focus_script));
  EXPECT_TRUE(GenerationPopupShowing());
}

IN_PROC_BROWSER_TEST_F(PasswordGenerationInteractiveTest,
                       GenerationTriggeredOnTap) {
  ASSERT_TRUE(content::ExecJs(
      RenderFrameHost(),
      "var submitRect = document.getElementById('password_field')"
      ".getBoundingClientRect();",
      content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  double y = content::EvalJs(RenderFrameHost(),
                             "(submitRect.top + submitRect.bottom) / 2;",
                             content::EXECUTE_SCRIPT_NO_USER_GESTURE)
                 .ExtractDouble();
  double x = content::EvalJs(RenderFrameHost(),
                             "(submitRect.left + submitRect.right) / 2;",
                             content::EXECUTE_SCRIPT_NO_USER_GESTURE)
                 .ExtractDouble();

  // Tap in the middle of the field.
  content::SimulateTapAt(WebContents(),
                         gfx::Point(static_cast<int>(x), static_cast<int>(y)));
  WaitForStatus(TestGenerationPopupObserver::GenerationPopup::kShown);
}

IN_PROC_BROWSER_TEST_F(PasswordGenerationInteractiveTest,
                       GenerationTriggeredOnClick) {
  ASSERT_TRUE(content::ExecJs(
      RenderFrameHost(),
      "var submitRect = document.getElementById('password_field')"
      ".getBoundingClientRect();",
      content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  double y = content::EvalJs(RenderFrameHost(),
                             "(submitRect.top + submitRect.bottom) / 2;",
                             content::EXECUTE_SCRIPT_NO_USER_GESTURE)
                 .ExtractDouble();
  double x = content::EvalJs(RenderFrameHost(),
                             "(submitRect.left + submitRect.right) / 2;",
                             content::EXECUTE_SCRIPT_NO_USER_GESTURE)
                 .ExtractDouble();

  // Click in the middle of the field.
  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft,
      gfx::Point(static_cast<int>(x), static_cast<int>(y)));
  WaitForStatus(TestGenerationPopupObserver::GenerationPopup::kShown);
}

// https://crbug.com/791389
IN_PROC_BROWSER_TEST_F(PasswordGenerationInteractiveTest,
                       DISABLED_AutoSavingGeneratedPassword) {
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS).get());

  FocusPasswordField();
  EXPECT_TRUE(GenerationPopupShowing());
  SendKeyToPopup(ui::VKEY_DOWN);
  SendKeyToPopup(ui::VKEY_RETURN);

  // Change username.
  FocusUsernameField();
  content::SimulateKeyPress(WebContents(), ui::DomKey::FromCharacter('U'),
                            ui::DomCode::US_U, ui::VKEY_U, false, false, false,
                            false);
  content::SimulateKeyPress(WebContents(), ui::DomKey::FromCharacter('N'),
                            ui::DomCode::US_N, ui::VKEY_N, false, false, false,
                            false);

  // Submit form.
  NavigationObserver observer(WebContents());
  std::string submit_script =
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(WebContents(), submit_script));
  observer.Wait();

  WaitForPasswordStore();
  EXPECT_FALSE(password_store->IsEmpty());

  // Make sure the username is correct.
  password_manager::TestPasswordStore::PasswordMap stored_passwords =
      password_store->stored_passwords();
  EXPECT_EQ(1u, stored_passwords.size());
  EXPECT_EQ(1u, stored_passwords.begin()->second.size());
  EXPECT_EQ(u"UN", (stored_passwords.begin()->second)[0].username_value);
}

// Verify that navigating away closes the popup.
IN_PROC_BROWSER_TEST_F(PasswordGenerationInteractiveTest,
                       NavigatingAwayClosesPopup) {
  // Open popup.
  FocusPasswordField();
  EXPECT_TRUE(GenerationPopupShowing());

  // Simulate navigating to a different page.
  NavigateToFile("/password/signup_form.html");

  // Check that popup is dismissed.
  EXPECT_FALSE(GenerationPopupShowing());
}

class PasswordGenerationPopupViewPrerenderingTest
    : public PasswordGenerationInteractiveTest {
 public:
  PasswordGenerationPopupViewPrerenderingTest()
      : prerender_helper_(base::BindRepeating(
            &PasswordGenerationPopupViewPrerenderingTest::WebContents,
            base::Unretained(this))) {}
  ~PasswordGenerationPopupViewPrerenderingTest() override = default;

  void SetUp() override {
    prerender_helper_.SetUp(embedded_test_server());
    PasswordGenerationInteractiveTest::SetUp();
  }

  content::test::PrerenderTestHelper* prerender_helper() {
    return &prerender_helper_;
  }

 protected:
  content::test::PrerenderTestHelper prerender_helper_;
};

IN_PROC_BROWSER_TEST_F(PasswordGenerationPopupViewPrerenderingTest,
                       PasswordGenerationPopupControllerInPrerendering) {
  // Open popup.
  FocusPasswordField();
  EXPECT_TRUE(GenerationPopupShowing());

  auto prerender_url = embedded_test_server()->GetURL("/empty.html");
  // Loads a page in the prerender.
  int host_id = prerender_helper()->AddPrerender(prerender_url);
  content::test::PrerenderHostObserver host_observer(*WebContents(), host_id);
  // It should keep the current popup controller since the prerenedering should
  // not affect the current page.
  EXPECT_TRUE(GenerationPopupShowing());

  // Navigates the primary page to the URL.
  prerender_helper()->NavigatePrimaryPage(prerender_url);
  // Makes sure that the page is activated from the prerendering.
  EXPECT_TRUE(host_observer.was_activated());
  // It should clear the current popup controller since the page loading deletes
  // the popup controller from the previous page.
  EXPECT_FALSE(GenerationPopupShowing());
}
