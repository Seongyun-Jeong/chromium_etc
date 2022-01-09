// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/gtest_util.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu_test_util.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/settings_window_manager_chromeos.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/ui/web_applications/system_web_app_ui_utils.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/web_applications/system_web_apps/test/system_web_app_browsertest_base.h"
#include "chrome/browser/web_applications/system_web_apps/test/test_system_web_app_installation.h"
#include "chrome/browser/web_applications/web_app_tab_helper.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/omnibox/browser/omnibox_edit_model.h"
#include "components/omnibox/browser/omnibox_view.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_navigation_observer.h"
#include "ui/base/models/menu_model.h"
#include "ui/display/screen.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/public/cpp/app_menu_constants.h"
#include "ash/public/cpp/shelf_item_delegate.h"
#include "ash/public/cpp/shelf_model.h"
#include "ash/shell.h"
#include "ash/wm/window_util.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/ash/crosapi/url_handler_ash.h"
#include "chrome/browser/ash/login/login_manager_test.h"
#include "chrome/browser/ash/login/test/login_manager_mixin.h"
#include "chrome/browser/ash/login/ui/user_adding_screen.h"
#include "chrome/browser/ash/web_applications/os_url_handler_system_web_app_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager_helper.h"
#include "chrome/browser/ui/webui/chrome_web_ui_controller_factory.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/test/test_utils.h"
#include "ui/wm/public/activation_change_observer.h"  // nogncheck
#include "ui/wm/public/activation_client.h"           // nogncheck
#endif

namespace web_app {

class SystemWebAppLinkCaptureBrowserTest
    : public SystemWebAppManagerBrowserTest {
 public:
  SystemWebAppLinkCaptureBrowserTest()
      : SystemWebAppManagerBrowserTest(/*install_mock*/ false) {
#if BUILDFLAG(IS_CHROMEOS_LACROS)
    EnableSystemWebAppsInLacrosForTesting();
#endif
    maybe_installation_ =
        TestSystemWebAppInstallation::SetUpAppThatCapturesNavigation();
  }
  ~SystemWebAppLinkCaptureBrowserTest() override = default;

 protected:
  Browser* CreateIncognitoBrowser() {
    Browser* incognito = Browser::Create(Browser::CreateParams(
        browser()->profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true),
        true));

    content::WindowedNotificationObserver observer(
        content::NOTIFICATION_LOAD_STOP,
        content::NotificationService::AllSources());
    chrome::AddSelectedTabWithURL(incognito, GURL(url::kAboutBlankURL),
                                  ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
    observer.Wait();

    incognito->window()->Show();
    return incognito;
  }
  const GURL kInitiatingAppUrl = GURL("chrome://initiating-app/pwa.html");
  const SystemAppType kInitiatingAppType = SystemAppType::SETTINGS;
};

#if !BUILDFLAG(IS_CHROMEOS_LACROS)
IN_PROC_BROWSER_TEST_P(SystemWebAppLinkCaptureBrowserTest,
                       OmniboxTypeURLAndNavigate) {
  WaitForTestSystemAppInstall();

  content::TestNavigationObserver observer(maybe_installation_->GetAppUrl());
  observer.StartWatchingNewWebContents();
  ui_test_utils::SendToOmniboxAndSubmit(
      browser(), maybe_installation_->GetAppUrl().spec());
  observer.Wait();

  Browser* app_browser = FindSystemWebAppBrowser(
      browser()->profile(), maybe_installation_->GetType());
  EXPECT_TRUE(app_browser);
  ui_test_utils::BrowserActivationWaiter(app_browser).WaitForActivation();
  EXPECT_EQ(2U, chrome::GetTotalBrowserCount());
  EXPECT_EQ(Browser::TYPE_APP, app_browser->type());
  EXPECT_FALSE(app_browser->app_controller()->ShouldShowCustomTabBar());
}

IN_PROC_BROWSER_TEST_P(SystemWebAppLinkCaptureBrowserTest, OmniboxPasteAndGo) {
  WaitForTestSystemAppInstall();
  OmniboxEditModel* model =
      browser()->window()->GetLocationBar()->GetOmniboxView()->model();

  content::TestNavigationObserver observer(maybe_installation_->GetAppUrl());
  observer.StartWatchingNewWebContents();
  model->PasteAndGo(base::UTF8ToUTF16(maybe_installation_->GetAppUrl().spec()));
  observer.Wait();

  Browser* app_browser = FindSystemWebAppBrowser(
      browser()->profile(), maybe_installation_->GetType());
  EXPECT_TRUE(app_browser);
  ui_test_utils::BrowserActivationWaiter(app_browser).WaitForActivation();
  EXPECT_EQ(2U, chrome::GetTotalBrowserCount());
  EXPECT_EQ(Browser::TYPE_APP, app_browser->type());
  EXPECT_FALSE(app_browser->app_controller()->ShouldShowCustomTabBar());
}

// This test is flaky on MacOS with ASAN or DBG. https://crbug.com/1173317
#if defined(OS_MAC) && (defined(ADDRESS_SANITIZER) || !defined(NDEBUG))
#define MAYBE_AnchorLinkClick DISABLED_AnchorLinkClick
#else
#define MAYBE_AnchorLinkClick AnchorLinkClick
#endif  // OS_MAC && (ADDRESS_SANITIZER || !NDEBUG)
IN_PROC_BROWSER_TEST_P(SystemWebAppLinkCaptureBrowserTest,
                       MAYBE_AnchorLinkClick) {
  WaitForTestSystemAppInstall();

  GURL kInitiatingChromeUrl = GURL(chrome::kChromeUIAboutURL);
  NavigateToURLAndWait(browser(), kInitiatingChromeUrl);
  EXPECT_EQ(kInitiatingChromeUrl, browser()
                                      ->tab_strip_model()
                                      ->GetActiveWebContents()
                                      ->GetLastCommittedURL());

  const std::string kAnchorTargets[] = {"", "_blank", "_self"};
  const std::string kAnchorRelValues[] = {"", "noreferrer", "noopener",
                                          "noreferrer noopener"};

  for (const auto& target : kAnchorTargets) {
    for (const auto& rel : kAnchorRelValues) {
      SCOPED_TRACE(testing::Message() << "anchor link: target='" << target
                                      << "', rel='" << rel << "'");
      content::TestNavigationObserver observer(
          maybe_installation_->GetAppUrl());
      observer.StartWatchingNewWebContents();
      EXPECT_TRUE(content::ExecuteScript(
          browser()->tab_strip_model()->GetActiveWebContents(),
          content::JsReplace("{"
                             "  let el = document.createElement('a');"
                             "  el.href = $1;"
                             "  el.target = $2;"
                             "  el.rel = $3;"
                             "  el.textContent = 'target = ' + $2;"
                             "  document.body.appendChild(el);"
                             "  el.click();"
                             "}",
                             maybe_installation_->GetAppUrl(), target, rel)));
      observer.Wait();

      Browser* app_browser = FindSystemWebAppBrowser(
          browser()->profile(), maybe_installation_->GetType());
      EXPECT_TRUE(app_browser);
      ui_test_utils::BrowserActivationWaiter(app_browser).WaitForActivation();
      EXPECT_EQ(2U, chrome::GetTotalBrowserCount());
      EXPECT_EQ(Browser::TYPE_APP, app_browser->type());
      EXPECT_FALSE(app_browser->app_controller()->ShouldShowCustomTabBar());
      app_browser->window()->Close();
      ui_test_utils::WaitForBrowserToClose(app_browser);

      // Check the initiating browser window is intact.
      EXPECT_EQ(kInitiatingChromeUrl, browser()
                                          ->tab_strip_model()
                                          ->GetActiveWebContents()
                                          ->GetLastCommittedURL());
    }
  }
}

IN_PROC_BROWSER_TEST_P(SystemWebAppLinkCaptureBrowserTest,
                       AnchorLinkContextMenuNewTab) {
  WaitForTestSystemAppInstall();

  GURL kInitiatingChromeUrl = GURL(chrome::kChromeUIAboutURL);
  NavigateToURLAndWait(browser(), kInitiatingChromeUrl);
  EXPECT_EQ(kInitiatingChromeUrl, browser()
                                      ->tab_strip_model()
                                      ->GetActiveWebContents()
                                      ->GetLastCommittedURL());

  content::ContextMenuParams context_menu_params;
  context_menu_params.page_url = kInitiatingChromeUrl;
  context_menu_params.link_url = maybe_installation_->GetAppUrl();

  content::TestNavigationObserver observer(maybe_installation_->GetAppUrl());
  observer.StartWatchingNewWebContents();

  TestRenderViewContextMenu menu(
      *browser()->tab_strip_model()->GetActiveWebContents()->GetMainFrame(),
      context_menu_params);
  menu.Init();
  menu.ExecuteCommand(IDC_CONTENT_CONTEXT_OPENLINKNEWTAB, 0);

  observer.Wait();

  Browser* app_browser = FindSystemWebAppBrowser(
      browser()->profile(), maybe_installation_->GetType());
  EXPECT_TRUE(app_browser);
  ui_test_utils::BrowserActivationWaiter(app_browser).WaitForActivation();
  EXPECT_EQ(2U, chrome::GetTotalBrowserCount());
  EXPECT_EQ(Browser::TYPE_APP, app_browser->type());
  EXPECT_FALSE(app_browser->app_controller()->ShouldShowCustomTabBar());
  app_browser->window()->Close();
  ui_test_utils::WaitForBrowserToClose(app_browser);

  // Check the initiating browser window is intact.
  EXPECT_EQ(kInitiatingChromeUrl, browser()
                                      ->tab_strip_model()
                                      ->GetActiveWebContents()
                                      ->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_P(SystemWebAppLinkCaptureBrowserTest,
                       AnchorLinkContextMenuNewWindow) {
  WaitForTestSystemAppInstall();

  GURL kInitiatingChromeUrl = GURL(chrome::kChromeUIAboutURL);
  NavigateToURLAndWait(browser(), kInitiatingChromeUrl);
  EXPECT_EQ(kInitiatingChromeUrl, browser()
                                      ->tab_strip_model()
                                      ->GetActiveWebContents()
                                      ->GetLastCommittedURL());

  content::ContextMenuParams context_menu_params;
  context_menu_params.page_url = kInitiatingChromeUrl;
  context_menu_params.link_url = maybe_installation_->GetAppUrl();

  content::TestNavigationObserver observer(maybe_installation_->GetAppUrl());
  observer.StartWatchingNewWebContents();

  TestRenderViewContextMenu menu(
      *browser()->tab_strip_model()->GetActiveWebContents()->GetMainFrame(),
      context_menu_params);
  menu.Init();
  menu.ExecuteCommand(IDC_CONTENT_CONTEXT_OPENLINKNEWWINDOW, 0);

  observer.Wait();

  Browser* app_browser = FindSystemWebAppBrowser(
      browser()->profile(), maybe_installation_->GetType());
  EXPECT_TRUE(app_browser);
  ui_test_utils::BrowserActivationWaiter(app_browser).WaitForActivation();
  EXPECT_EQ(2U, chrome::GetTotalBrowserCount());
  EXPECT_EQ(Browser::TYPE_APP, app_browser->type());
  EXPECT_FALSE(app_browser->app_controller()->ShouldShowCustomTabBar());
  app_browser->window()->Close();
  ui_test_utils::WaitForBrowserToClose(app_browser);

  // Check the initiating browser window is intact.
  EXPECT_EQ(kInitiatingChromeUrl, browser()
                                      ->tab_strip_model()
                                      ->GetActiveWebContents()
                                      ->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_P(SystemWebAppLinkCaptureBrowserTest, ChangeLocationHref) {
  WaitForTestSystemAppInstall();

  GURL kInitiatingChromeUrl = GURL(chrome::kChromeUIAboutURL);
  NavigateToURLAndWait(browser(), kInitiatingChromeUrl);
  EXPECT_EQ(kInitiatingChromeUrl, browser()
                                      ->tab_strip_model()
                                      ->GetActiveWebContents()
                                      ->GetLastCommittedURL());

  content::TestNavigationObserver observer(maybe_installation_->GetAppUrl());
  observer.StartWatchingNewWebContents();
  EXPECT_TRUE(content::ExecuteScript(
      browser()->tab_strip_model()->GetActiveWebContents(),
      content::JsReplace("location.href=$1;",
                         maybe_installation_->GetAppUrl())));
  observer.Wait();

  Browser* app_browser = FindSystemWebAppBrowser(
      browser()->profile(), maybe_installation_->GetType());
  EXPECT_TRUE(app_browser);
  ui_test_utils::BrowserActivationWaiter(app_browser).WaitForActivation();
  EXPECT_EQ(2U, chrome::GetTotalBrowserCount());
  EXPECT_EQ(Browser::TYPE_APP, app_browser->type());
  EXPECT_FALSE(app_browser->app_controller()->ShouldShowCustomTabBar());

  // Check the initiating browser window is intact.
  EXPECT_EQ(kInitiatingChromeUrl, browser()
                                      ->tab_strip_model()
                                      ->GetActiveWebContents()
                                      ->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_P(SystemWebAppLinkCaptureBrowserTest, WindowOpen) {
  WaitForTestSystemAppInstall();

  GURL kInitiatingChromeUrl = GURL(chrome::kChromeUIAboutURL);
  NavigateToURLAndWait(browser(), kInitiatingChromeUrl);
  EXPECT_EQ(kInitiatingChromeUrl, browser()
                                      ->tab_strip_model()
                                      ->GetActiveWebContents()
                                      ->GetLastCommittedURL());

  const std::string kWindowOpenTargets[] = {"", "_blank"};
  const std::string kWindowOpenFeatures[] = {"", "noreferrer", "noopener",
                                             "noreferrer noopener"};

  for (const auto& target : kWindowOpenTargets) {
    for (const auto& features : kWindowOpenFeatures) {
      SCOPED_TRACE(testing::Message() << "window.open: target='" << target
                                      << "', features='" << features << "'");
      content::TestNavigationObserver observer(
          maybe_installation_->GetAppUrl());
      observer.StartWatchingNewWebContents();
      EXPECT_TRUE(content::ExecuteScript(
          browser()->tab_strip_model()->GetActiveWebContents(),
          content::JsReplace("window.open($1, $2, $3);",
                             maybe_installation_->GetAppUrl(), target,
                             features)));
      observer.Wait();

      Browser* app_browser = FindSystemWebAppBrowser(
          browser()->profile(), maybe_installation_->GetType());
      EXPECT_TRUE(app_browser);
      ui_test_utils::BrowserActivationWaiter(app_browser).WaitForActivation();
      EXPECT_EQ(2U, chrome::GetTotalBrowserCount());
      EXPECT_EQ(Browser::TYPE_APP, app_browser->type());
      EXPECT_FALSE(app_browser->app_controller()->ShouldShowCustomTabBar());
      app_browser->window()->Close();
      ui_test_utils::WaitForBrowserToClose(app_browser);

      // Check the initiating browser window is intact.
      EXPECT_EQ(kInitiatingChromeUrl, browser()
                                          ->tab_strip_model()
                                          ->GetActiveWebContents()
                                          ->GetLastCommittedURL());
    }
  }
}

IN_PROC_BROWSER_TEST_P(SystemWebAppLinkCaptureBrowserTest,
                       WindowOpenFromOtherSWA) {
  WaitForTestSystemAppInstall();

  content::WebContents* initiating_web_contents = LaunchApp(kInitiatingAppType);

  const std::string kWindowOpenTargets[] = {"", "_blank"};
  const std::string kWindowOpenFeatures[] = {"", "noreferrer", "noopener",
                                             "noreferrer noopener"};

  for (const auto& target : kWindowOpenTargets) {
    for (const auto& features : kWindowOpenFeatures) {
      SCOPED_TRACE(testing::Message() << "window.open: target='" << target
                                      << "', features='" << features << "'");
      content::TestNavigationObserver observer(
          maybe_installation_->GetAppUrl());
      observer.StartWatchingNewWebContents();
      EXPECT_TRUE(content::ExecuteScript(
          initiating_web_contents,
          content::JsReplace("window.open($1, $2, $3);",
                             maybe_installation_->GetAppUrl(), target,
                             features)));
      observer.Wait();

      Browser* app_browser = FindSystemWebAppBrowser(
          browser()->profile(), maybe_installation_->GetType());
      EXPECT_TRUE(app_browser);
      ui_test_utils::BrowserActivationWaiter(app_browser).WaitForActivation();

      // There should be three browsers: the default one (new tab page), the
      // initiating system app, the link capturing system app.
      EXPECT_EQ(3U, chrome::GetTotalBrowserCount());
      EXPECT_EQ(Browser::TYPE_APP, app_browser->type());
      EXPECT_FALSE(app_browser->app_controller()->ShouldShowCustomTabBar());
      app_browser->window()->Close();
      ui_test_utils::WaitForBrowserToClose(app_browser);

      // Check the initiating browser window is intact.
      EXPECT_EQ(kInitiatingAppUrl,
                initiating_web_contents->GetLastCommittedURL());
    }
  }
}

IN_PROC_BROWSER_TEST_P(SystemWebAppLinkCaptureBrowserTest,
                       CaptureToOpenedWindowAndNavigateURL) {
  WaitForTestSystemAppInstall();

  Browser* app_browser;
  content::WebContents* web_contents =
      LaunchApp(maybe_installation_->GetType(), &app_browser);

  GURL kInitiatingChromeUrl = GURL(chrome::kChromeUIAboutURL);
  NavigateToURLAndWait(browser(), kInitiatingChromeUrl);
  EXPECT_EQ(kInitiatingChromeUrl, browser()
                                      ->tab_strip_model()
                                      ->GetActiveWebContents()
                                      ->GetLastCommittedURL());

  const GURL kPageURL = maybe_installation_->GetAppUrl().Resolve("/page2.html");
  content::TestNavigationObserver observer(web_contents);
  EXPECT_TRUE(content::ExecuteScript(
      browser()->tab_strip_model()->GetActiveWebContents(),
      content::JsReplace("let el = document.createElement('a');"
                         "el.href = $1;"
                         "el.textContent = 'Link to SWA Page 2';"
                         "document.body.appendChild(el);"
                         "el.click();",
                         kPageURL)));
  observer.Wait();

  EXPECT_EQ(kPageURL, app_browser->tab_strip_model()
                          ->GetActiveWebContents()
                          ->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_P(SystemWebAppLinkCaptureBrowserTest,
                       IncognitoBrowserOmniboxLinkCapture) {
  WaitForTestSystemAppInstall();

  Browser* incognito_browser = CreateIncognitoBrowser();
  browser()->window()->Close();
  ui_test_utils::WaitForBrowserToClose(browser());

  content::TestNavigationObserver observer(maybe_installation_->GetAppUrl());
  observer.StartWatchingNewWebContents();
  incognito_browser->window()->GetLocationBar()->FocusLocation(true);
  ui_test_utils::SendToOmniboxAndSubmit(
      incognito_browser, maybe_installation_->GetAppUrl().spec());
  observer.Wait();

  // We launch SWAs into the incognito profile's original profile.
  Browser* app_browser = FindSystemWebAppBrowser(
      incognito_browser->profile()->GetOriginalProfile(),
      maybe_installation_->GetType());
  EXPECT_TRUE(app_browser);
  ui_test_utils::BrowserActivationWaiter(app_browser).WaitForActivation();
  EXPECT_EQ(2U, chrome::GetTotalBrowserCount());
  EXPECT_EQ(Browser::TYPE_APP, app_browser->type());
  EXPECT_FALSE(app_browser->app_controller()->ShouldShowCustomTabBar());
}
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)

class SystemWebAppManagerWindowSizeControlsTest
    : public SystemWebAppManagerBrowserTest {
 public:
  SystemWebAppManagerWindowSizeControlsTest()
      : SystemWebAppManagerBrowserTest(/*install_mock=*/false) {
    maybe_installation_ =
        TestSystemWebAppInstallation::SetUpNonResizeableAndNonMaximizableApp();
  }
  ~SystemWebAppManagerWindowSizeControlsTest() override = default;
};

IN_PROC_BROWSER_TEST_P(SystemWebAppManagerWindowSizeControlsTest,
                       NonResizeableWindow) {
  WaitForTestSystemAppInstall();

  content::TestNavigationObserver observer(maybe_installation_->GetAppUrl());
  observer.StartWatchingNewWebContents();
  Browser* app_browser;
  LaunchApp(maybe_installation_->GetType(), &app_browser);

  EXPECT_FALSE(app_browser->create_params().can_resize);
}

IN_PROC_BROWSER_TEST_P(SystemWebAppManagerWindowSizeControlsTest,
                       NonMaximizableWindow) {
  WaitForTestSystemAppInstall();

  content::TestNavigationObserver observer(maybe_installation_->GetAppUrl());
  observer.StartWatchingNewWebContents();
  Browser* app_browser;
  LaunchApp(maybe_installation_->GetType(), &app_browser);

  EXPECT_FALSE(app_browser->create_params().can_maximize);
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
// Use LoginManagerTest here instead of SystemWebAppManagerBrowserTest, because
// it's less complicated to add SWA to LoginManagerTest than adding multi-logins
// to SWA browsertest.
class SystemWebAppManagerMultiDesktopLaunchBrowserTest
    : public ash::LoginManagerTest {
 public:
  SystemWebAppManagerMultiDesktopLaunchBrowserTest() : ash::LoginManagerTest() {
    login_mixin_.AppendRegularUsers(2);
    account_id1_ = login_mixin_.users()[0].account_id;
    account_id2_ = login_mixin_.users()[1].account_id;
    installation_ =
        TestSystemWebAppInstallation::SetUpAppThatCapturesNavigation();
  }

  ~SystemWebAppManagerMultiDesktopLaunchBrowserTest() override = default;

  void WaitForSystemWebAppInstall(Profile* profile) {
    base::RunLoop run_loop;

    web_app::WebAppProvider::GetForSystemWebApps(profile)
        ->system_web_app_manager()
        .on_apps_synchronized()
        .Post(FROM_HERE, base::BindLambdaForTesting([&]() {
                // Wait one execution loop for
                // on_apps_synchronized() to be called on all
                // listeners.
                base::ThreadTaskRunnerHandle::Get()->PostTask(
                    FROM_HERE, run_loop.QuitClosure());
              }));
    run_loop.Run();
  }

  AppId GetAppId(Profile* profile) {
    SystemWebAppManager& manager =
        web_app::WebAppProvider::GetForSystemWebApps(profile)
            ->system_web_app_manager();
    absl::optional<AppId> app_id =
        manager.GetAppIdForSystemApp(installation_->GetType());
    CHECK(app_id.has_value());
    return *app_id;
  }

  Browser* LaunchAppOnProfile(Profile* profile) {
    AppId app_id = GetAppId(profile);

    auto launch_params = apps::AppLaunchParams(
        app_id, apps::mojom::LaunchContainer::kLaunchContainerWindow,
        WindowOpenDisposition::CURRENT_TAB,
        apps::mojom::LaunchSource::kFromAppListGrid);

    content::TestNavigationObserver navigation_observer(
        installation_->GetAppUrl());

    // Watch new WebContents to wait for launches that open an app for the first
    // time.
    navigation_observer.StartWatchingNewWebContents();

    // Watch existing WebContents to wait for launches that re-use the
    // WebContents e.g. launching an already opened SWA.
    navigation_observer.WatchExistingWebContents();

    LaunchSystemWebAppAsync(profile, installation_->GetType());

    navigation_observer.Wait();

    Browser* swa_browser =
        FindSystemWebAppBrowser(profile, installation_->GetType());
    EXPECT_TRUE(swa_browser);
    ui_test_utils::BrowserActivationWaiter(swa_browser).WaitForActivation();

    return swa_browser;
  }

 protected:
  std::unique_ptr<TestSystemWebAppInstallation> installation_;
  ash::LoginManagerMixin login_mixin_{&mixin_host_};
  AccountId account_id1_;
  AccountId account_id2_;
};

IN_PROC_BROWSER_TEST_F(SystemWebAppManagerMultiDesktopLaunchBrowserTest,
                       LaunchToActiveDesktop) {
  // Login two users.
  LoginUser(account_id1_);
  base::RunLoop().RunUntilIdle();

  // Wait for System Apps to be installed on both user profiles.
  auto* user_manager = user_manager::UserManager::Get();
  Profile* profile1 = chromeos::ProfileHelper::Get()->GetProfileByUser(
      user_manager->FindUser(account_id1_));
  WaitForSystemWebAppInstall(profile1);

  installation_ =
      TestSystemWebAppInstallation::SetUpAppThatCapturesNavigation();
  ash::UserAddingScreen::Get()->Start();
  AddUser(account_id2_);
  base::RunLoop().RunUntilIdle();
  Profile* profile2 = chromeos::ProfileHelper::Get()->GetProfileByUser(
      user_manager->FindUser(account_id2_));
  WaitForSystemWebAppInstall(profile2);
  // Set user 1 to be active.
  user_manager->SwitchActiveUser(account_id1_);
  EXPECT_TRUE(multi_user_util::IsProfileFromActiveUser(profile1));
  EXPECT_FALSE(multi_user_util::IsProfileFromActiveUser(profile2));

  // Launch the app from user 2 profile. The window should be on user 1
  // (the active) desktop.
  Browser* browser2 = LaunchAppOnProfile(profile2);
  EXPECT_TRUE(
      MultiUserWindowManagerHelper::GetInstance()->IsWindowOnDesktopOfUser(
          browser2->window()->GetNativeWindow(), account_id1_));

  // Launch the app from user 1 profile. The window should be on user 1 (the
  // active) desktop. And there should be two different browser windows
  // (for each profile).
  Browser* browser1 = LaunchAppOnProfile(profile1);
  EXPECT_TRUE(
      MultiUserWindowManagerHelper::GetInstance()->IsWindowOnDesktopOfUser(
          browser1->window()->GetNativeWindow(), account_id1_));

  EXPECT_NE(browser1, browser2);
  EXPECT_EQ(2U, chrome::GetTotalBrowserCount());

  // Switch to user 2, then launch the app. SWAs reuse their window, so it
  // should bring `browser2` to user 2 (the active) desktop.
  user_manager->SwitchActiveUser(account_id2_);
  Browser* browser2_relaunch = LaunchAppOnProfile(profile2);

  EXPECT_EQ(browser2, browser2_relaunch);
  EXPECT_TRUE(
      MultiUserWindowManagerHelper::GetInstance()->IsWindowOnDesktopOfUser(
          browser2->window()->GetNativeWindow(), account_id2_));
}

IN_PROC_BROWSER_TEST_F(SystemWebAppManagerMultiDesktopLaunchBrowserTest,
                       ProfileScheduledForDeletion) {
  // Login two users.
  LoginUser(account_id1_);
  base::RunLoop().RunUntilIdle();

  // Wait for System Apps to be installed on both user profiles.
  auto* user_manager = user_manager::UserManager::Get();
  Profile* profile1 = chromeos::ProfileHelper::Get()->GetProfileByUser(
      user_manager->FindUser(account_id1_));
  WaitForSystemWebAppInstall(profile1);

  installation_ =
      TestSystemWebAppInstallation::SetUpAppThatCapturesNavigation();
  ash::UserAddingScreen::Get()->Start();
  AddUser(account_id2_);
  base::RunLoop().RunUntilIdle();
  Profile* profile2 = chromeos::ProfileHelper::Get()->GetProfileByUser(
      user_manager->FindUser(account_id2_));
  WaitForSystemWebAppInstall(profile2);

  g_browser_process->profile_manager()->ScheduleProfileForDeletion(
      profile2->GetPath(), base::DoNothing());

  {
    auto launch_params = apps::AppLaunchParams(
        GetAppId(profile2),
        apps::mojom::LaunchContainer::kLaunchContainerWindow,
        WindowOpenDisposition::CURRENT_TAB,
        apps::mojom::LaunchSource::kFromAppListGrid);
    content::WebContents* web_contents =
        apps::AppServiceProxyFactory::GetForProfile(profile2)
            ->BrowserAppLauncher()
            ->LaunchAppWithParams(std::move(launch_params));
    EXPECT_EQ(web_contents, nullptr);
  }

  {
    auto launch_params = apps::AppLaunchParams(
        GetAppId(profile1),
        apps::mojom::LaunchContainer::kLaunchContainerWindow,
        WindowOpenDisposition::CURRENT_TAB,
        apps::mojom::LaunchSource::kFromAppListGrid);
    content::WebContents* web_contents =
        apps::AppServiceProxyFactory::GetForProfile(profile1)
            ->BrowserAppLauncher()
            ->LaunchAppWithParams(std::move(launch_params));
    EXPECT_NE(web_contents, nullptr);
  }
}

#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(IS_CHROMEOS_ASH)
using SystemWebAppLaunchProfileBrowserTest = SystemWebAppManagerBrowserTest;

IN_PROC_BROWSER_TEST_P(SystemWebAppLaunchProfileBrowserTest,
                       LaunchFromNormalSessionIncognitoProfile) {
  Profile* startup_profile = browser()->profile();
  ASSERT_TRUE(!startup_profile->IsOffTheRecord());

  WaitForTestSystemAppInstall();
  Profile* incognito_profile =
      startup_profile->GetPrimaryOTRProfile(/*create_if_needed=*/true);

  content::TestNavigationObserver observer(GetStartUrl());
  observer.StartWatchingNewWebContents();
  LaunchSystemWebAppAsync(incognito_profile, GetMockAppType());
  observer.Wait();

  EXPECT_FALSE(FindSystemWebAppBrowser(incognito_profile, GetMockAppType()));
  EXPECT_TRUE(FindSystemWebAppBrowser(startup_profile, GetMockAppType()));
}

#if !DCHECK_IS_ON()
// The following tests are disabled in DCHECK builds. LaunchSystemWebAppAsync
// DCHECKs if it can't find a suitable profile. EXPECT_DCHECK_DEATH (or its
// variants) aren't reliable in browsertests, so we don't test this. Here we
// to verify LaunchSystemWebAppAsync doesn't crash in release builds
IN_PROC_BROWSER_TEST_P(SystemWebAppLaunchProfileBrowserTest,
                       LaunchFromSignInProfile) {
  WaitForTestSystemAppInstall();

  Profile* signin_profile = chromeos::ProfileHelper::GetSigninProfile();

  EXPECT_EQ(1U, chrome::GetTotalBrowserCount());

  LaunchSystemWebAppAsync(signin_profile, GetMockAppType());

  // Use RunUntilIdle() here, because this catches the scenario where
  // LaunchSystemWebAppAsync mistakenly picks a profile to launch the app.
  //
  // RunUntilIdle() serves a catch-all solution, so we don't have to flush mojo
  // calls on all existing profiles (and those potentially created during
  // launch).
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1U, chrome::GetTotalBrowserCount());
}
#endif  // !DCHECK_IS_ON()

using SystemWebAppLaunchProfileGuestSessionBrowserTest =
    SystemWebAppLaunchProfileBrowserTest;

IN_PROC_BROWSER_TEST_P(SystemWebAppLaunchProfileGuestSessionBrowserTest,
                       LaunchFromGuestSessionOriginalProfile) {
  // We should start into the guest session browsing profile.
  Profile* startup_profile = browser()->profile();
  ASSERT_TRUE(startup_profile->IsGuestSession());
  ASSERT_TRUE(startup_profile->IsPrimaryOTRProfile());

  WaitForTestSystemAppInstall();

  // We typically don't get the original profile as an argument, but it is a
  // valid input to LaunchSystemWebAppAsync.
  Profile* original_profile = browser()->profile()->GetOriginalProfile();

  content::TestNavigationObserver observer(GetStartUrl());
  observer.StartWatchingNewWebContents();
  LaunchSystemWebAppAsync(original_profile, GetMockAppType());
  observer.Wait();

  EXPECT_FALSE(FindSystemWebAppBrowser(original_profile, GetMockAppType()));
  EXPECT_TRUE(FindSystemWebAppBrowser(startup_profile, GetMockAppType()));
}

IN_PROC_BROWSER_TEST_P(SystemWebAppLaunchProfileGuestSessionBrowserTest,
                       LaunchFromGuestSessionPrimaryOTRProfile) {
  // We should start into the guest session browsing profile.
  Profile* startup_profile = browser()->profile();
  ASSERT_TRUE(startup_profile->IsGuestSession());
  ASSERT_TRUE(startup_profile->IsPrimaryOTRProfile());

  WaitForTestSystemAppInstall();

  content::TestNavigationObserver observer(GetStartUrl());
  observer.StartWatchingNewWebContents();
  LaunchSystemWebAppAsync(startup_profile, GetMockAppType());
  observer.Wait();

  EXPECT_TRUE(FindSystemWebAppBrowser(startup_profile, GetMockAppType()));
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(IS_CHROMEOS_ASH)
using SystemWebAppLaunchOmniboxNavigateBrowsertest =
    SystemWebAppManagerBrowserTest;

IN_PROC_BROWSER_TEST_P(SystemWebAppLaunchOmniboxNavigateBrowsertest,
                       OpenInTab) {
  WaitForTestSystemAppInstall();

  content::TestNavigationObserver observer(GetStartUrl());
  // The app should load in the blank WebContents created when browser starts.
  observer.WatchExistingWebContents();
  ui_test_utils::SendToOmniboxAndSubmit(browser(), GetStartUrl().spec());
  observer.Wait();

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(web_contents->GetLastCommittedURL(), GetStartUrl());
  EXPECT_EQ(1, browser()->tab_strip_model()->count());

  // Verifies the tab has an associated tab helper for System App's AppId.
  auto* tab_helper = web_app::WebAppTabHelper::FromWebContents(web_contents);
  EXPECT_TRUE(tab_helper);
  EXPECT_EQ(tab_helper->GetAppId(),
            *web_app::GetAppIdForSystemWebApp(browser()->profile(),
                                              GetMockAppType()));
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(IS_CHROMEOS_ASH)

// A one shot observer which waits for an activation of any window.
class TestActivationObserver : public wm::ActivationChangeObserver {
 public:
  TestActivationObserver(const TestActivationObserver&) = delete;
  TestActivationObserver& operator=(const TestActivationObserver&) = delete;

  TestActivationObserver() {
    activation_observer_.Observe(ash::Shell::Get()->activation_client());
  }

  ~TestActivationObserver() override = default;

  void Wait() { run_loop_.Run(); }

  // wm::ActivationChangeObserver:
  void OnWindowActivated(ActivationReason reason,
                         aura::Window* gained_active,
                         aura::Window* lost_active) override {
    Browser* browser = chrome::FindBrowserWithWindow(gained_active);
    // Check that the activated window is actually a browser.
    EXPECT_TRUE(browser);
    // Check also that the browser is actually an app.
    EXPECT_TRUE(browser->is_type_app());
    run_loop_.Quit();
  }

 private:
  // The MessageLoopRunner used to spin the message loop.
  base::RunLoop run_loop_;
  base::ScopedObservation<wm::ActivationClient, wm::ActivationChangeObserver>
      activation_observer_{this};
};

// Tests which are exercising OpenUrl called by Lacros in Ash.
class SystemWebAppOpenInAshFromLacrosTests
    : public SystemWebAppManagerBrowserTest {
 public:
  SystemWebAppOpenInAshFromLacrosTests()
      : SystemWebAppManagerBrowserTest(/*install_mock=*/false) {
    OsUrlHandlerSystemWebAppDelegate::EnableDelegateForTesting(true);
    url_handler_ = std::make_unique<crosapi::UrlHandlerAsh>();
  }

  ~SystemWebAppOpenInAshFromLacrosTests() {
    OsUrlHandlerSystemWebAppDelegate::EnableDelegateForTesting(false);
  }

  // A function to wait until a window activation change was observed.
  void LaunchAndWaitForActivationChange(const GURL& url) {
    TestActivationObserver observer;
    url_handler_->OpenUrl(url);
    observer.Wait();
  }

 protected:
  std::unique_ptr<crosapi::UrlHandlerAsh> url_handler_;
};

// This test will make sure that only accepted URLs will be allowed to create
// applications.
IN_PROC_BROWSER_TEST_P(SystemWebAppOpenInAshFromLacrosTests,
                       LaunchOnlyAllowedUrls) {
  WaitForTestSystemAppInstall();

  // There might be an initial browser from the testing framework.
  int initial_browser_count = BrowserList::GetInstance()->size();

  // Test that a non descript URL gets rejected.
  GURL url1 = GURL("http://www.foo.bar");
  EXPECT_FALSE(ChromeWebUIControllerFactory::GetInstance()->CanHandleUrl(url1));
  EXPECT_FALSE(url_handler_->OpenUrlInternal(url1));

  // Test that an unknown internal os url gets rejected.
  GURL url2 = GURL("os://foo-bar");
  EXPECT_FALSE(ChromeWebUIControllerFactory::GetInstance()->CanHandleUrl(url2));
  EXPECT_FALSE(url_handler_->OpenUrlInternal(url2));

  // Test that an unknown internal chrome url gets rejected.
  GURL url3 = GURL("chrome://foo-bar");
  EXPECT_FALSE(ChromeWebUIControllerFactory::GetInstance()->CanHandleUrl(url3));
  EXPECT_FALSE(url_handler_->OpenUrlInternal(url3));

  // Test that a known internal url gets accepted.
  GURL url4 = GURL("os://version");
  EXPECT_TRUE(ChromeWebUIControllerFactory::GetInstance()->CanHandleUrl(url4));
  LaunchAndWaitForActivationChange(url4);
  EXPECT_EQ(initial_browser_count + 1, BrowserList::GetInstance()->size());
  EXPECT_EQ(u"ChromeOS-URLs", ash::window_util::GetActiveWindow()->GetTitle());
}

// This test will make sure that opening the same system URL multiple times will
// re-use the existing app.
IN_PROC_BROWSER_TEST_P(SystemWebAppOpenInAshFromLacrosTests,
                       LaunchLacrosDeDuplicationtest) {
  WaitForTestSystemAppInstall();

  // There might be an initial browser from the testing framework.
  int initial_browser_count = BrowserList::GetInstance()->size();

  // Start an application which uses the OS url handler.
  LaunchAndWaitForActivationChange(GURL(chrome::kOsUICreditsURL));
  EXPECT_EQ(initial_browser_count + 1, BrowserList::GetInstance()->size());
  EXPECT_EQ(u"ChromeOS-URLs", ash::window_util::GetActiveWindow()->GetTitle());

  // Start another application.
  LaunchAndWaitForActivationChange(GURL(chrome::kOsUIFlagsURL));
  EXPECT_EQ(initial_browser_count + 2, BrowserList::GetInstance()->size());
  EXPECT_EQ(u"Flags", ash::window_util::GetActiveWindow()->GetTitle());

  // Start an application of the first type and see that no new app got created.
  LaunchAndWaitForActivationChange(GURL(chrome::kOsUICreditsURL));
  EXPECT_EQ(initial_browser_count + 2, BrowserList::GetInstance()->size());
  EXPECT_EQ(u"ChromeOS-URLs", ash::window_util::GetActiveWindow()->GetTitle());
}

// This test will make sure that opening a different system URL (other than
// flags) will open different windows.
IN_PROC_BROWSER_TEST_P(SystemWebAppOpenInAshFromLacrosTests,
                       LaunchLacrosCreateNewAppForNewSystemUrl) {
  WaitForTestSystemAppInstall();

  // There might be an initial browser from the testing framework.
  int initial_browser_count = BrowserList::GetInstance()->size();

  // Start an application using the OS Url handler.
  LaunchAndWaitForActivationChange(GURL(chrome::kOsUICreditsURL));
  EXPECT_EQ(initial_browser_count + 1, BrowserList::GetInstance()->size());
  EXPECT_EQ(u"ChromeOS-URLs", ash::window_util::GetActiveWindow()->GetTitle());

  // Start another application using the OS Url handler.
  LaunchAndWaitForActivationChange(GURL(chrome::kOsUIComponentsUrl));
  EXPECT_EQ(initial_browser_count + 2, BrowserList::GetInstance()->size());
  EXPECT_EQ(u"ChromeOS-URLs", ash::window_util::GetActiveWindow()->GetTitle());
}

#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

class SystemWebAppManagerCloseFromScriptsTest
    : public SystemWebAppManagerBrowserTest {
 public:
  SystemWebAppManagerCloseFromScriptsTest()
      : SystemWebAppManagerBrowserTest(/*install_mock=*/false) {
    maybe_installation_ =
        TestSystemWebAppInstallation::SetupAppWithAllowScriptsToCloseWindows(
            true);
  }
  ~SystemWebAppManagerCloseFromScriptsTest() override = default;
};

IN_PROC_BROWSER_TEST_P(SystemWebAppManagerCloseFromScriptsTest, WindowClose) {
  WaitForTestSystemAppInstall();

  Browser* app_browser;
  LaunchApp(maybe_installation_->GetType(), &app_browser);

  const GURL kPageURL = maybe_installation_->GetAppUrl().Resolve("/page2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(app_browser, kPageURL));
  EXPECT_EQ(kPageURL, app_browser->tab_strip_model()
                          ->GetActiveWebContents()
                          ->GetLastCommittedURL());

  EXPECT_TRUE(content::ExecuteScript(
      app_browser->tab_strip_model()->GetActiveWebContents(),
      "window.close();"));

  ui_test_utils::WaitForBrowserToClose(app_browser);
  EXPECT_EQ(1U, chrome::GetTotalBrowserCount());
}

class SystemWebAppManagerShouldNotCloseFromScriptsTest
    : public SystemWebAppManagerBrowserTest {
 public:
  SystemWebAppManagerShouldNotCloseFromScriptsTest()
      : SystemWebAppManagerBrowserTest(/*install_mock=*/false) {
    maybe_installation_ =
        TestSystemWebAppInstallation::SetupAppWithAllowScriptsToCloseWindows(
            false);
  }
  ~SystemWebAppManagerShouldNotCloseFromScriptsTest() override = default;
};

IN_PROC_BROWSER_TEST_P(SystemWebAppManagerShouldNotCloseFromScriptsTest,
                       ShouldNotCloseWindow) {
  WaitForTestSystemAppInstall();

  Browser* app_browser;
  LaunchApp(maybe_installation_->GetType(), &app_browser);

  const GURL kPageURL = maybe_installation_->GetAppUrl().Resolve("/page2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(app_browser, kPageURL));
  EXPECT_EQ(kPageURL, app_browser->tab_strip_model()
                          ->GetActiveWebContents()
                          ->GetLastCommittedURL());

  content::WebContentsConsoleObserver console_observer(
      app_browser->tab_strip_model()->GetActiveWebContents());
  console_observer.SetPattern(
      "Scripts may close only the windows that were opened by them.");

  EXPECT_TRUE(content::ExecuteScript(
      app_browser->tab_strip_model()->GetActiveWebContents(),
      "window.close();"));

  console_observer.Wait();
  EXPECT_EQ(2U, chrome::GetTotalBrowserCount());
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
class SystemWebAppNewWindowMenuItemTest
    : public SystemWebAppManagerBrowserTest {
 public:
  SystemWebAppNewWindowMenuItemTest()
      : SystemWebAppManagerBrowserTest(/*install_mock=*/false) {
    maybe_installation_ =
        TestSystemWebAppInstallation::SetUpAppWithNewWindowMenuItem();
  }
  ~SystemWebAppNewWindowMenuItemTest() override = default;

  ash::ShelfItemDelegate* GetAppShelfItemDelegate() {
    return ash::ShelfModel::Get()->GetShelfItemDelegate(
        ash::ShelfID(maybe_installation_->GetAppId()));
  }

  std::unique_ptr<ui::MenuModel> GetContextMenu(
      ash::ShelfItemDelegate* item_delegate,
      int64_t display_id) {
    base::RunLoop run_loop;
    std::unique_ptr<ui::MenuModel> menu;
    item_delegate->GetContextMenu(
        display_id, base::BindLambdaForTesting(
                        [&](std::unique_ptr<ui::SimpleMenuModel> created_menu) {
                          menu = std::move(created_menu);
                          run_loop.Quit();
                        }));
    run_loop.Run();
    return menu;
  }

  int64_t GetDisplayId() {
    return display::Screen::GetScreen()->GetPrimaryDisplay().id();
  }
};

IN_PROC_BROWSER_TEST_P(SystemWebAppNewWindowMenuItemTest, OpensNewWindow) {
  WaitForTestSystemAppInstall();

  // Launch the app so it shows up in shelf.
  LaunchApp(maybe_installation_->GetType());

  // Verify the menu item shows up.
  auto* shelf_item_delegate = GetAppShelfItemDelegate();
  ASSERT_TRUE(shelf_item_delegate);

  // Check the context menu option shows up.
  auto display_id = GetDisplayId();
  std::unique_ptr<ui::MenuModel> menu =
      GetContextMenu(shelf_item_delegate, display_id);
  ASSERT_TRUE(menu);
  ui::MenuModel* model = menu.get();
  int command_index;
  ui::MenuModel::GetModelAndIndexForCommandId(ash::MENU_OPEN_NEW, &model,
                                              &command_index);
  EXPECT_TRUE(menu->IsEnabledAt(command_index));

  // Try to launch the app into a new window.
  content::TestNavigationObserver observer(maybe_installation_->GetAppUrl());
  observer.StartWatchingNewWebContents();
  menu->ActivatedAt(command_index);
  observer.Wait();

  // After launch, we should have two SWA windows.
  auto* browser_list = BrowserList::GetInstance();
  size_t system_app_browser_count = std::count_if(
      browser_list->begin(), browser_list->end(), [&](Browser* browser) {
        return web_app::IsBrowserForSystemWebApp(
            browser, maybe_installation_->GetType());
      });

  EXPECT_EQ(system_app_browser_count, 2U);
}
#endif

#if !BUILDFLAG(IS_CHROMEOS_LACROS)
INSTANTIATE_SYSTEM_WEB_APP_MANAGER_TEST_SUITE_REGULAR_PROFILE_P(
    SystemWebAppLinkCaptureBrowserTest);
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)

#if BUILDFLAG(IS_CHROMEOS_ASH)
INSTANTIATE_SYSTEM_WEB_APP_MANAGER_TEST_SUITE_REGULAR_PROFILE_P(
    SystemWebAppLaunchProfileBrowserTest);

INSTANTIATE_SYSTEM_WEB_APP_MANAGER_TEST_SUITE_GUEST_SESSION_P(
    SystemWebAppLaunchProfileGuestSessionBrowserTest);
#endif

INSTANTIATE_SYSTEM_WEB_APP_MANAGER_TEST_SUITE_REGULAR_PROFILE_P(
    SystemWebAppManagerWindowSizeControlsTest);

#if BUILDFLAG(IS_CHROMEOS_ASH)
INSTANTIATE_SYSTEM_WEB_APP_MANAGER_TEST_SUITE_ALL_PROFILE_TYPES_P(
    SystemWebAppLaunchOmniboxNavigateBrowsertest);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

INSTANTIATE_SYSTEM_WEB_APP_MANAGER_TEST_SUITE_REGULAR_PROFILE_P(
    SystemWebAppManagerCloseFromScriptsTest);

INSTANTIATE_SYSTEM_WEB_APP_MANAGER_TEST_SUITE_REGULAR_PROFILE_P(
    SystemWebAppManagerShouldNotCloseFromScriptsTest);

#if BUILDFLAG(IS_CHROMEOS_ASH)
INSTANTIATE_SYSTEM_WEB_APP_MANAGER_TEST_SUITE_REGULAR_PROFILE_P(
    SystemWebAppNewWindowMenuItemTest);
#endif
#if BUILDFLAG(IS_CHROMEOS_ASH)
INSTANTIATE_SYSTEM_WEB_APP_MANAGER_TEST_SUITE_REGULAR_PROFILE_P(
    SystemWebAppOpenInAshFromLacrosTests);
#endif
}  // namespace web_app
