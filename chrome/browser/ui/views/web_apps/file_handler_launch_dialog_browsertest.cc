// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/web_apps/file_handler_launch_dialog_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/startup/web_app_startup_utils.h"
#include "chrome/browser/ui/test/test_browser_dialog.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_registry_update.h"
#include "chrome/browser/web_applications/web_application_info.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/services/app_service/public/cpp/file_handler.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "third_party/blink/public/common/features.h"
#include "ui/views/widget/any_widget_observer.h"
#include "ui/views/widget/widget.h"

namespace web_app {

namespace {

const char kStartUrl[] = "https://example.org/";
const char kFileLaunchUrl[] = "https://example.org/file_launch/";

}  // namespace

// Tests for the `FileHandlerLaunchDialogView` as well as
// `startup::web_app::MaybeHandleWebAppLaunch()`.
class FileHandlerLaunchDialogTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    InstallTestWebApp();
  }

  void LaunchAppWithFile(const base::FilePath& path) {
    StartupBrowserCreator browser_creator;
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
    command_line.AppendSwitchASCII(switches::kAppId, app_id_);
    command_line.AppendArgPath(path);

    browser_creator.Start(command_line, profile_manager->user_data_dir(),
                          browser()->profile(), {});
  }

  void InstallTestWebApp() {
    const GURL example_url = GURL(kStartUrl);
    auto web_app_info = std::make_unique<WebApplicationInfo>();
    web_app_info->title = u"Test app";
    web_app_info->start_url = example_url;
    web_app_info->scope = example_url;
    web_app_info->display_mode = blink::mojom::DisplayMode::kStandalone;

    // Basic plain text format.
    apps::FileHandler entry1;
    entry1.action = GURL(kFileLaunchUrl);
    entry1.accept.emplace_back();
    entry1.accept[0].mime_type = "text/*";
    entry1.accept[0].file_extensions.insert(".txt");
    web_app_info->file_handlers.push_back(std::move(entry1));

    app_id_ =
        test::InstallWebApp(browser()->profile(), std::move(web_app_info));

    // Setting the user display mode is necessary because
    // `test::InstallWebApp()` forces a kBrowser display mode; see
    // `WebAppInstallFinalizer::FinalizeInstall()`.
    ScopedRegistryUpdate update(
        &WebAppProvider::GetForTest(browser()->profile())->sync_bridge());
    update->UpdateApp(app_id_)->SetUserDisplayMode(DisplayMode::kStandalone);
  }

  const WebApp* GetApp() {
    return WebAppProvider::GetForTest(browser()->profile())
        ->registrar()
        .GetAppById(app_id_);
  }

  // Launches the app and responds to the dialog, verifying expected outcomes.
  void LaunchAppAndRespond(bool remember_checkbox_state,
                           views::Widget::ClosedReason user_response,
                           ApiApprovalState expected_end_state,
                           GURL expected_url = {}) {
    content::TestNavigationObserver navigation_observer(expected_url);
    if (!expected_url.is_empty())
      navigation_observer.StartWatchingNewWebContents();

    base::RunLoop run_loop;
    web_app::startup::SetStartupDoneCallbackForTesting(run_loop.QuitClosure());

    FileHandlerLaunchDialogView::SetDefaultRememberSelectionForTesting(
        remember_checkbox_state);
    views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                         "FileHandlerLaunchDialogView");
    LaunchAppWithFile(base::FilePath::FromASCII("foo.txt"));
    waiter.WaitIfNeededAndGet()->CloseWithReason(user_response);
    run_loop.Run();
    EXPECT_EQ(expected_end_state, GetApp()->file_handler_approval_state());

    if (!expected_url.is_empty())
      navigation_observer.Wait();
  }

  // Launches the app to handle a file, assumes no dialog will be shown, but
  // waits for the app window to be launched to `expected_url`.
  void LaunchAppAndExpectUrlWithoutDialog(const base::FilePath& file,
                                          const GURL& expected_url) {
    content::TestNavigationObserver navigation_observer(expected_url);
    navigation_observer.StartWatchingNewWebContents();
    LaunchAppWithFile(file);
    navigation_observer.Wait();
  }

  // Returns the URL of the first tab in the last opened browser.
  static GURL GetLastOpenedUrl() {
    auto* list = BrowserList::GetInstance();
    return list->get(list->size() - 1)
        ->tab_strip_model()
        ->GetWebContentsAt(0)
        ->GetLastCommittedURL();
  }

 protected:
  AppId app_id_;

  base::test::ScopedFeatureList feature_list_{
      blink::features::kFileHandlingAPI};
};

IN_PROC_BROWSER_TEST_F(FileHandlerLaunchDialogTest,
                       EscapeDoesNotRememberPreference) {
  // One normal browser window exists.
  EXPECT_EQ(1U, BrowserList::GetInstance()->size());
  LaunchAppAndRespond(/*remember_checkbox_state=*/true,
                      views::Widget::ClosedReason::kEscKeyPressed,
                      ApiApprovalState::kRequiresPrompt);
  // One normal browser window exists still as the app wasn't launched.
  EXPECT_EQ(1U, BrowserList::GetInstance()->size());
}

IN_PROC_BROWSER_TEST_F(FileHandlerLaunchDialogTest, DisallowAndRemember) {
  // One normal browser window exists.
  EXPECT_EQ(1U, BrowserList::GetInstance()->size());

  // Try to launch the app to handle files, deny at the prompt and "don't ask
  // again".
  LaunchAppAndRespond(/*remember_checkbox_state=*/true,
                      views::Widget::ClosedReason::kCancelButtonClicked,
                      ApiApprovalState::kDisallowed);
  EXPECT_EQ(1U, BrowserList::GetInstance()->size());

  // Try to launch the app again. It should fail without showing a dialog. The
  // app window will be shown, but the files won't be passed.
  LaunchAppAndExpectUrlWithoutDialog(base::FilePath::FromASCII("foo.txt"),
                                     GURL(kStartUrl));
  ASSERT_EQ(2U, BrowserList::GetInstance()->size());
  EXPECT_TRUE(BrowserList::GetInstance()->get(1)->is_type_app());
  EXPECT_EQ(GURL(kStartUrl), GetLastOpenedUrl());
}

IN_PROC_BROWSER_TEST_F(FileHandlerLaunchDialogTest, AllowAndRemember) {
  // One normal browser window exists.
  EXPECT_EQ(1U, BrowserList::GetInstance()->size());

  // Try to launch the app to handle files, allow at the prompt and "don't ask
  // again".
  LaunchAppAndRespond(/*remember_checkbox_state=*/true,
                      views::Widget::ClosedReason::kAcceptButtonClicked,
                      ApiApprovalState::kAllowed, GURL(kFileLaunchUrl));
  // An app window is created.
  ASSERT_EQ(2U, BrowserList::GetInstance()->size());
  EXPECT_TRUE(BrowserList::GetInstance()->get(1)->is_type_app());

  // Try to launch the app again. It should succeed without showing a dialog.
  LaunchAppAndExpectUrlWithoutDialog(base::FilePath::FromASCII("foo.txt"),
                                     GURL(kFileLaunchUrl));
  EXPECT_EQ(3U, BrowserList::GetInstance()->size());
  EXPECT_TRUE(BrowserList::GetInstance()->get(2)->is_type_app());
  EXPECT_EQ(GURL(kFileLaunchUrl), GetLastOpenedUrl());
}

IN_PROC_BROWSER_TEST_F(FileHandlerLaunchDialogTest, DisallowDoNotRemember) {
  // One normal browser window exists.
  EXPECT_EQ(1U, BrowserList::GetInstance()->size());

  // Try to launch the app to handle files, deny at the prompt and uncheck
  // "don't ask again".
  LaunchAppAndRespond(/*remember_checkbox_state=*/false,
                      views::Widget::ClosedReason::kCancelButtonClicked,
                      ApiApprovalState::kRequiresPrompt);
  EXPECT_EQ(1U, BrowserList::GetInstance()->size());

  // Try to launch the app again. It should show a dialog again. This time,
  // accept.
  LaunchAppAndRespond(/*remember_checkbox_state=*/false,
                      views::Widget::ClosedReason::kAcceptButtonClicked,
                      ApiApprovalState::kRequiresPrompt, GURL(kFileLaunchUrl));
  // An app window is created.
  ASSERT_EQ(2U, BrowserList::GetInstance()->size());
  EXPECT_TRUE(BrowserList::GetInstance()->get(1)->is_type_app());
  EXPECT_EQ(GURL(kFileLaunchUrl), GetLastOpenedUrl());
}

IN_PROC_BROWSER_TEST_F(FileHandlerLaunchDialogTest, AcceptDoNotRemember) {
  // One normal browser window exists.
  EXPECT_EQ(1U, BrowserList::GetInstance()->size());

  // Try to launch the app to handle files, allow at the prompt and uncheck
  // "don't ask again".
  LaunchAppAndRespond(/*remember_checkbox_state=*/false,
                      views::Widget::ClosedReason::kAcceptButtonClicked,
                      ApiApprovalState::kRequiresPrompt, GURL(kFileLaunchUrl));
  // An app window is created.
  ASSERT_EQ(2U, BrowserList::GetInstance()->size());
  EXPECT_TRUE(BrowserList::GetInstance()->get(1)->is_type_app());

  // Try to launch the app again. It should show a dialog again.
  LaunchAppAndRespond(/*remember_checkbox_state=*/false,
                      views::Widget::ClosedReason::kCancelButtonClicked,
                      ApiApprovalState::kRequiresPrompt);
  // An app window is not created.
  ASSERT_EQ(2U, BrowserList::GetInstance()->size());
}

IN_PROC_BROWSER_TEST_F(FileHandlerLaunchDialogTest, UnhandledType) {
  // One normal browser window exists.
  EXPECT_EQ(1U, BrowserList::GetInstance()->size());

  // Try to launch the app with a file type it doesn't handle. It should fail
  // without showing a dialog, but fall back to showing a normal browser window.
  LaunchAppAndExpectUrlWithoutDialog(base::FilePath::FromASCII("foo.png"),
                                     GURL(kStartUrl));
  EXPECT_EQ(2U, BrowserList::GetInstance()->size());
  EXPECT_TRUE(BrowserList::GetInstance()->get(1)->is_type_app());
  EXPECT_EQ(GURL(kStartUrl), GetLastOpenedUrl());
}

}  // namespace web_app
