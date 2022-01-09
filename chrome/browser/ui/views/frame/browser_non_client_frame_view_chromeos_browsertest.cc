// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/browser_non_client_frame_view_chromeos.h"

#include <string>

#include "base/gtest_prod_util.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/sessions/session_restore_test_helper.h"
#include "chrome/browser/sessions/session_service_test_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#include "chrome/browser/ui/views/frame/browser_non_client_frame_view_chromeos_test_utils.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/immersive_mode_controller.h"
#include "chrome/browser/ui/views/frame/immersive_mode_tester.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chromeos/ui/base/window_properties.h"
#include "chromeos/ui/frame/caption_buttons/frame_caption_button_container_view.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "content/public/test/browser_test.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_switches.h"
#include "ash/public/cpp/shelf_test_api.h"
#include "ash/public/cpp/split_view_test_api.h"
#include "ash/public/cpp/test/shell_test_api.h"
#include "ash/shell.h"
#include "ash/wm/overview/overview_controller.h"
#include "ash/wm/tablet_mode/tablet_mode_controller.h"
#include "base/callback_helpers.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/test/metrics/histogram_tester.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/command_updater.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profile_io_data.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager_helper.h"
#include "chrome/browser/ui/ash/multi_user/test_multi_user_window_manager.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_test.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/passwords/passwords_client_ui_delegate.h"
#include "chrome/browser/ui/settings_window_manager_chromeos.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/immersive_mode_controller_chromeos.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/webui_tab_strip_container_view.h"
#include "chrome/browser/ui/views/fullscreen_control/fullscreen_control_host.h"
#include "chrome/browser/ui/views/location_bar/content_setting_image_view.h"
#include "chrome/browser/ui/views/location_bar/custom_tab_bar_view.h"
#include "chrome/browser/ui/views/location_bar/zoom_bubble_view.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view_base.h"
#include "chrome/browser/ui/views/tab_search_bubble_host.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/toolbar/app_menu.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/web_apps/frame_toolbar/web_app_frame_toolbar_view.h"
#include "chrome/browser/ui/views/web_apps/frame_toolbar/web_app_menu_button.h"
#include "chrome/browser/ui/views/web_apps/frame_toolbar/web_app_toolbar_button_container.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/ui/web_applications/system_web_app_ui_utils.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/web_applications/system_web_apps/system_web_app_manager.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_application_info.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chromeos/ui/base/chromeos_ui_constants.h"
#include "chromeos/ui/base/window_pin_type.h"
#include "chromeos/ui/frame/default_frame_header.h"
#include "chromeos/ui/frame/frame_header.h"
#include "components/account_id/account_id.h"
#include "components/password_manager/core/browser/password_form.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/test/background_color_change_waiter.h"
#include "content/public/test/content_mock_cert_verifier.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/blink/public/mojom/frame/fullscreen.mojom.h"
#include "ui/aura/test/env_test_helper.h"
#include "ui/base/class_property.h"
#include "ui/base/hit_test.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/pointer/touch_ui_controller.h"
#include "ui/base/window_open_disposition.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/window/caption_button_layout_constants.h"
#include "ui/views/window/frame_caption_button.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

using views::Widget;

// TODO(crbug.com/1235203): Identify tests that should also run under Lacros.

using BrowserNonClientFrameViewChromeOSTest =
    TopChromeMdParamTest<InProcessBrowserTest>;
using BrowserNonClientFrameViewChromeOSTestNoWebUiTabStrip =
    WebUiTabStripOverrideTest<false, BrowserNonClientFrameViewChromeOSTest>;

#if BUILDFLAG(IS_CHROMEOS_ASH)
using BrowserNonClientFrameViewChromeOSTouchTest =
    TopChromeTouchTest<InProcessBrowserTest>;
using BrowserNonClientFrameViewChromeOSTestWithWebUiTabStrip =
    WebUiTabStripOverrideTest<true, BrowserNonClientFrameViewChromeOSTest>;
using BrowserNonClientFrameViewChromeOSTouchTestWithWebUiTabStrip =
    WebUiTabStripOverrideTest<true, BrowserNonClientFrameViewChromeOSTouchTest>;

// Base class for background color change browser tests parameterized by whether
// to use a SWA or a non-SWA.
class BrowserNonClientFrameViewChromeOSTestBackgroundColorChange
    : public InProcessBrowserTest,
      public testing::WithParamInterface</*use_swa=*/bool> {
 public:
  // Returns whether to use a SWA given test parameterization.
  bool UseSwa() const { return GetParam(); }

  // Installs an SWA or a non-SWA depending on test parameterization, returning
  // the `AppId` of the installed app. Note that this method may only be invoked
  // once per test.
  web_app::AppId InstallWebApp() {
    DCHECK(!app_id_.has_value());
    app_id_ = UseSwa() ? InstallSWA() : InstallNonSWA();
    return app_id_.value();
  }

  // Toggles the color mode, triggering propagation of theme change events.
  void ToggleColorMode() {
    auto* native_theme = ui::NativeTheme::GetInstanceForNativeUi();
    auto* native_theme_web = ui::NativeTheme::GetInstanceForWeb();

    const bool is_dark_mode_enabled = native_theme->ShouldUseDarkColors();

    native_theme->set_use_dark_colors(!is_dark_mode_enabled);
    native_theme_web->set_preferred_color_scheme(
        is_dark_mode_enabled ? ui::NativeTheme::PreferredColorScheme::kLight
                             : ui::NativeTheme::PreferredColorScheme::kDark);

    native_theme->NotifyOnNativeThemeUpdated();
    native_theme_web->NotifyOnNativeThemeUpdated();
  }

  // Returns the profile associated with the test.
  Profile* profile() { return browser()->profile(); }

 private:
  web_app::AppId InstallSWA() {
    web_app::WebAppProvider::GetForSystemWebApps(profile())
        ->system_web_app_manager()
        .InstallSystemAppsForTesting();
    return *web_app::GetAppIdForSystemWebApp(profile(),
                                             web_app::SystemAppType::SETTINGS);
  }

  web_app::AppId InstallNonSWA() {
    if (!test_server_) {
      test_server_ = std::make_unique<net::EmbeddedTestServer>(
          net::EmbeddedTestServer::TYPE_HTTPS);
      test_server_->AddDefaultHandlers(GetChromeTestDataDir());
      CHECK(test_server_->Start());
    }
    const GURL app_url = test_server_->GetURL("app.com", "/ssl/google.html");
    auto web_app_info = std::make_unique<WebApplicationInfo>();
    web_app_info->start_url = app_url;
    web_app_info->scope = app_url.GetWithoutFilename();
    web_app_info->theme_color = SK_ColorBLUE;
    web_app_info->background_color = SK_ColorBLUE;
    web_app_info->dark_mode_theme_color = SK_ColorRED;
    web_app_info->dark_mode_background_color = SK_ColorRED;
    return web_app::test::InstallWebApp(profile(), std::move(web_app_info));
  }

  absl::optional<web_app::AppId> app_id_;
  std::unique_ptr<net::EmbeddedTestServer> test_server_;
};

IN_PROC_BROWSER_TEST_P(
    BrowserNonClientFrameViewChromeOSTestBackgroundColorChange,
    BackgroundColorChange) {
  const web_app::AppId app_id = InstallWebApp();
  Browser* const app_browser = web_app::LaunchWebAppBrowser(profile(), app_id);
  ContentsWebView* const contents_web_view =
      BrowserView::GetBrowserViewForBrowser(app_browser)->contents_web_view();
  content::WebContents* const web_contents =
      app_browser->tab_strip_model()->GetActiveWebContents();

  // Verify background color is immediately resolved from the app controller
  // despite the fact that the web contents background color hasn't loaded yet.
  EXPECT_EQ(contents_web_view->GetBackground()->get_color(),
            app_browser->app_controller()->GetBackgroundColor().value());
  EXPECT_FALSE(web_contents->GetBackgroundColor().has_value());

  // Wait for the web contents background color to load and verify that the
  // background color still matches that resolved from the app controller.
  {
    content::BackgroundColorChangeWaiter waiter(web_contents);
    waiter.Wait();
    EXPECT_EQ(contents_web_view->GetBackground()->get_color(),
              app_browser->app_controller()->GetBackgroundColor().value());
    EXPECT_EQ(contents_web_view->GetBackground()->get_color(),
              web_contents->GetBackgroundColor().value());
  }

  content::AwaitDocumentOnLoadCompleted(web_contents);

  // Toggle color mode and verify background color is immediately resolved from
  // the app controller. In the case of SWAs, there may be a temporary mismatch
  // between the contents background color and the web contents background color
  // due to the fact that the web contents background color update is async.
  ToggleColorMode();
  EXPECT_EQ(contents_web_view->GetBackground()->get_color(),
            app_browser->app_controller()->GetBackgroundColor().value());
  if (!UseSwa()) {
    EXPECT_EQ(contents_web_view->GetBackground()->get_color(),
              web_contents->GetBackgroundColor().value());
  }

  // Wait for the web contents background color to update and verify that the
  // background color still matches that resolved from the app controller.
  {
    content::BackgroundColorChangeWaiter waiter(web_contents);
    waiter.Wait();
    EXPECT_EQ(contents_web_view->GetBackground()->get_color(),
              app_browser->app_controller()->GetBackgroundColor().value());
    EXPECT_EQ(contents_web_view->GetBackground()->get_color(),
              web_contents->GetBackgroundColor().value());
  }
}

// This test does not make sense for the webUI tabstrip, since the window layout
// is different in that case.
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTestNoWebUiTabStrip,
                       NonClientHitTest) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  Widget* widget = browser_view->GetWidget();
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);

  // Click on the top edge of a restored window hits the top edge resize handle.
  const int kWindowWidth = 300;
  const int kWindowHeight = 290;
  widget->SetBounds(gfx::Rect(10, 10, kWindowWidth, kWindowHeight));
  gfx::Point top_edge(kWindowWidth / 2, 0);
  EXPECT_EQ(HTTOP, frame_view->NonClientHitTest(top_edge));

  // Click just below the resize handle hits the caption.
  gfx::Point below_resize(kWindowWidth / 2, chromeos::kResizeInsideBoundsSize);
  EXPECT_EQ(HTCAPTION, frame_view->NonClientHitTest(below_resize));

  // Click in the top edge of a maximized window now hits the client area,
  // because we want it to fall through to the tab strip and select a tab.
  widget->Maximize();
  int expected_value = HTCLIENT;
  EXPECT_EQ(expected_value, frame_view->NonClientHitTest(top_edge));
}

IN_PROC_BROWSER_TEST_F(
    BrowserNonClientFrameViewChromeOSTouchTestWithWebUiTabStrip,
    TabletSplitViewNonClientHitTest) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);
  EXPECT_EQ(0, frame_view->GetBoundsForClientView().y());

  Widget* widget = browser_view->GetWidget();
  ASSERT_NO_FATAL_FAILURE(
      ash::ShellTestApi().SetTabletModeEnabledForTest(true));
  ash::SplitViewTestApi().SnapWindow(widget->GetNativeWindow(),
                                     ash::SplitViewTestApi::SnapPosition::LEFT);

  // Touch on the top of the window is interpreted as client hit.
  gfx::Point top_point(widget->GetWindowBoundsInScreen().width() / 2, 0);
  EXPECT_EQ(HTCLIENT, frame_view->NonClientHitTest(top_point));
}

IN_PROC_BROWSER_TEST_F(
    BrowserNonClientFrameViewChromeOSTouchTestWithWebUiTabStrip,
    TabletSplitViewSwipeDownFromEdgeOpensWebUiTabStrip) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);
  EXPECT_EQ(0, frame_view->GetBoundsForClientView().y());

  Widget* widget = browser_view->GetWidget();
  ASSERT_NO_FATAL_FAILURE(
      ash::ShellTestApi().SetTabletModeEnabledForTest(true));
  ash::SplitViewTestApi().SnapWindow(widget->GetNativeWindow(),
                                     ash::SplitViewTestApi::SnapPosition::LEFT);

  // A point above the window.
  gfx::Point edge_point(widget->GetWindowBoundsInScreen().width() / 2, -1);

  ASSERT_FALSE(browser_view->webui_tab_strip()->GetVisible());
  aura::Window* window = widget->GetNativeWindow();
  ui::test::EventGenerator event_generator(window->GetRootWindow());
  event_generator.SetTouchRadius(10, 5);
  event_generator.PressTouch(edge_point);
  event_generator.MoveTouchBy(0, 100);
  event_generator.ReleaseTouch();
  ASSERT_TRUE(browser_view->webui_tab_strip()->GetVisible());
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

// Test that the frame view does not do any painting in non-immersive
// fullscreen.
// This test does not make sense for the webUI tabstrip, since the frame is not
// painted in that case.
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTestNoWebUiTabStrip,
                       NonImmersiveFullscreen) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  content::WebContents* web_contents = browser_view->GetActiveWebContents();
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);

  // Frame paints by default.
  EXPECT_TRUE(frame_view->GetShouldPaint());

  // No painting should occur in non-immersive fullscreen. (We enter into tab
  // fullscreen here because tab fullscreen is non-immersive even on ChromeOS).
  EnterFullscreenModeForTabAndWait(browser(), web_contents);
  EXPECT_FALSE(browser_view->immersive_mode_controller()->IsEnabled());
  EXPECT_FALSE(frame_view->GetShouldPaint());

  // The client view abuts top of the window.
  EXPECT_EQ(0, frame_view->GetBoundsForClientView().y());

  // The frame should be painted again when fullscreen is exited and the caption
  // buttons should be visible.
  ToggleFullscreenModeAndWait(browser());
  EXPECT_TRUE(frame_view->GetShouldPaint());
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
// Tests that caption buttons are hidden when entering tab fullscreen.
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTestNoWebUiTabStrip,
                       CaptionButtonsHiddenNonImmersiveFullscreen) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  content::WebContents* web_contents = browser_view->GetActiveWebContents();
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);

  EXPECT_TRUE(frame_view->caption_button_container_->GetVisible());

  EnterFullscreenModeForTabAndWait(browser(), web_contents);
  EXPECT_FALSE(browser_view->immersive_mode_controller()->IsEnabled());
  // Caption buttons are hidden.
  EXPECT_FALSE(frame_view->caption_button_container_->GetVisible());

  // The frame should be painted again when fullscreen is exited and the caption
  // buttons should be visible.
  ToggleFullscreenModeAndWait(browser());
  // Caption button container visible again.
  EXPECT_TRUE(frame_view->caption_button_container_->GetVisible());
}

// Tests that Avatar icon should show on the top left corner of the teleported
// browser window on ChromeOS.
// TODO(http://crbug.com/1059514): This test should be made to work with the
// webUI tabstrip.
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTestNoWebUiTabStrip,
                       AvatarDisplayOnTeleportedWindow) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);
  aura::Window* window = browser()->window()->GetNativeWindow();

  EXPECT_FALSE(MultiUserWindowManagerHelper::ShouldShowAvatar(window));
  EXPECT_FALSE(frame_view->profile_indicator_icon_);

  const AccountId account_id1 =
      multi_user_util::GetAccountIdFromProfile(browser()->profile());
  TestMultiUserWindowManager* window_manager =
      TestMultiUserWindowManager::Create(browser(), account_id1);

  // Teleport the window to another desktop.
  const AccountId account_id2(AccountId::FromUserEmail("user2"));
  window_manager->ShowWindowForUser(window, account_id2);
  EXPECT_TRUE(MultiUserWindowManagerHelper::ShouldShowAvatar(window));
  EXPECT_TRUE(frame_view->profile_indicator_icon_);

  // Teleport the window back to owner desktop.
  window_manager->ShowWindowForUser(window, account_id1);
  EXPECT_FALSE(MultiUserWindowManagerHelper::ShouldShowAvatar(window));
  EXPECT_FALSE(frame_view->profile_indicator_icon_);
}

// There should be no top inset when using the WebUI tab strip since the frame
// is invisible. Regression test for crbug.com/1076675
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTestWithWebUiTabStrip,
                       TopInset) {
  // This test doesn't make sense in non-touch mode since it expects the WebUI
  // tab strip to be active. This test is instantiated with and without touch
  // mode.
  if (!ui::TouchUiController::Get()->touch_ui())
    return;

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());

  StartOverview();
  EXPECT_EQ(0, GetFrameViewChromeOS(browser_view)->GetTopInset(false));

  EndOverview();
  EXPECT_EQ(0, GetFrameViewChromeOS(browser_view)->GetTopInset(false));
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTest,
                       IncognitoMarkedAsAssistantBlocked) {
  Browser* incognito_browser = CreateIncognitoBrowser();
  EXPECT_TRUE(incognito_browser->window()->GetNativeWindow()->GetProperty(
      chromeos::kBlockedForAssistantSnapshotKey));
}

// Tests that browser frame minimum size constraint is updated in response to
// browser view layout.
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTest,
                       FrameMinSizeIsUpdated) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);

  BookmarkBarView* bookmark_bar = browser_view->GetBookmarkBarView();
  EXPECT_FALSE(bookmark_bar->GetVisible());
  const int min_height_no_bookmarks = frame_view->GetMinimumSize().height();

  // Setting non-zero bookmark bar preferred size forces it to be visible and
  // triggers BrowserView layout update.
  bookmark_bar->SetPreferredSize(gfx::Size(50, 5));
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(bookmark_bar->GetVisible());

  // Minimum window size should grow with the bookmark bar shown.
  gfx::Size min_window_size = frame_view->GetMinimumSize();
  EXPECT_GT(min_window_size.height(), min_height_no_bookmarks);
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTest,
                       SettingsSystemWebAppHasMinimumWindowSize) {
  // Install the Settings System Web App.
  web_app::WebAppProvider::GetForTest(browser()->profile())
      ->system_web_app_manager()
      .InstallSystemAppsForTesting();

  // Open a settings window.
  auto* settings_manager = chrome::SettingsWindowManager::GetInstance();
  settings_manager->ShowOSSettings(browser()->profile());

  // The above ShowOSSettings() should trigger an asynchronous call to launch
  // OS Settings SWA. Flush Mojo calls so the browser window is created.
  web_app::FlushSystemWebAppLaunchesForTesting(browser()->profile());

  Browser* settings_browser =
      settings_manager->FindBrowserForProfile(browser()->profile());

  // Try to set the bounds to a tiny value.
  settings_browser->window()->SetBounds(gfx::Rect(1, 1));

  // The window has a reasonable size.
  gfx::Rect actual_bounds = settings_browser->window()->GetBounds();
  EXPECT_LE(300, actual_bounds.width());
  EXPECT_LE(100, actual_bounds.height());
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

// This is a regression test that session restore minimized browser should
// re-layout the header (https://crbug.com/827444).
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTest,
                       RestoreMinimizedBrowserUpdatesCaption) {
  // Enable session service.
  SessionStartupPref pref(SessionStartupPref::LAST);
  Profile* profile = browser()->profile();
  SessionStartupPref::SetStartupPref(profile, pref);

  SessionServiceTestHelper helper(profile);
  helper.SetForceBrowserNotAliveWithNoWindows(true);

  // Do not exit from test when last browser is closed.
  ScopedKeepAlive keep_alive(KeepAliveOrigin::SESSION_RESTORE,
                             KeepAliveRestartOption::DISABLED);

  // Quit and restore.
  browser()->window()->Minimize();
  CloseBrowserSynchronously(browser());

  chrome::NewEmptyWindow(profile);
  SessionRestoreTestHelper().Wait();

  Browser* new_browser = BrowserList::GetInstance()->GetLastActive();

  // Check that a layout occurs.
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(new_browser);
  Widget* widget = browser_view->GetWidget();

  BrowserNonClientFrameViewChromeOS* frame_view =
      static_cast<BrowserNonClientFrameViewChromeOS*>(
          widget->non_client_view()->frame_view());

  chromeos::FrameCaptionButtonContainerView::TestApi test(
      frame_view->caption_button_container_);
  EXPECT_TRUE(test.size_button()->icon_definition_for_test());
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
namespace {

class WebAppNonClientFrameViewAshTest
    : public TopChromeMdParamTest<InProcessBrowserTest> {
 public:
  WebAppNonClientFrameViewAshTest() = default;

  WebAppNonClientFrameViewAshTest(const WebAppNonClientFrameViewAshTest&) =
      delete;
  WebAppNonClientFrameViewAshTest& operator=(
      const WebAppNonClientFrameViewAshTest&) = delete;

  ~WebAppNonClientFrameViewAshTest() override = default;

  GURL GetAppURL() const {
    return https_server_.GetURL("app.com", "/ssl/google.html");
  }

  static SkColor GetThemeColor() { return SK_ColorBLUE; }

  Browser* app_browser_ = nullptr;
  BrowserView* browser_view_ = nullptr;
  chromeos::DefaultFrameHeader* frame_header_ = nullptr;
  WebAppFrameToolbarView* web_app_frame_toolbar_ = nullptr;
  const std::vector<ContentSettingImageView*>* content_setting_views_ = nullptr;
  AppMenuButton* web_app_menu_button_ = nullptr;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    TopChromeMdParamTest<InProcessBrowserTest>::SetUpCommandLine(command_line);
    cert_verifier_.SetUpCommandLine(command_line);
  }

  void SetUpInProcessBrowserTestFixture() override {
    TopChromeMdParamTest<
        InProcessBrowserTest>::SetUpInProcessBrowserTestFixture();
    cert_verifier_.SetUpInProcessBrowserTestFixture();
  }

  void TearDownInProcessBrowserTestFixture() override {
    cert_verifier_.TearDownInProcessBrowserTestFixture();
    TopChromeMdParamTest<
        InProcessBrowserTest>::TearDownInProcessBrowserTestFixture();
  }

  void SetUpOnMainThread() override {
    TopChromeMdParamTest<InProcessBrowserTest>::SetUpOnMainThread();

    WebAppToolbarButtonContainer::DisableAnimationForTesting();

    // Start secure local server.
    host_resolver()->AddRule("*", "127.0.0.1");
    cert_verifier_.mock_cert_verifier()->set_default_result(net::OK);
    https_server_.AddDefaultHandlers(GetChromeTestDataDir());
    ASSERT_TRUE(https_server_.Start());
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  // |SetUpWebApp()| must be called after |SetUpOnMainThread()| to make sure
  // the Network Service process has been setup properly.
  void SetUpWebApp() {
    auto web_app_info = std::make_unique<WebApplicationInfo>();
    web_app_info->start_url = GetAppURL();
    web_app_info->scope = GetAppURL().GetWithoutFilename();
    web_app_info->display_mode = blink::mojom::DisplayMode::kStandalone;
    web_app_info->theme_color = GetThemeColor();

    web_app::AppId app_id = web_app::test::InstallWebApp(
        browser()->profile(), std::move(web_app_info));
    content::TestNavigationObserver navigation_observer(GetAppURL());
    navigation_observer.StartWatchingNewWebContents();
    app_browser_ = web_app::LaunchWebAppBrowser(browser()->profile(), app_id);
    navigation_observer.WaitForNavigationFinished();

    browser_view_ = BrowserView::GetBrowserViewForBrowser(app_browser_);
    BrowserNonClientFrameViewChromeOS* frame_view =
        GetFrameViewChromeOS(browser_view_);
    frame_header_ = static_cast<chromeos::DefaultFrameHeader*>(
        frame_view->frame_header_.get());

    web_app_frame_toolbar_ = frame_view->web_app_frame_toolbar_for_testing();
    DCHECK(web_app_frame_toolbar_);
    DCHECK(web_app_frame_toolbar_->GetVisible());

    content_setting_views_ =
        &web_app_frame_toolbar_->GetContentSettingViewsForTesting();
    web_app_menu_button_ = web_app_frame_toolbar_->GetAppMenuButton();
  }

  AppMenu* GetAppMenu() { return web_app_menu_button_->app_menu(); }

  SkColor GetActiveColor() const {
    return *web_app_frame_toolbar_->active_foreground_color_;
  }

  bool GetPaintingAsActive() const {
    return web_app_frame_toolbar_->paint_as_active_;
  }

  PageActionIconView* GetPageActionIcon(PageActionIconType type) {
    return browser_view_->toolbar_button_provider()->GetPageActionIconView(
        type);
  }

  ContentSettingImageView* GrantGeolocationPermission() {
    content::RenderFrameHost* frame =
        app_browser_->tab_strip_model()->GetActiveWebContents()->GetMainFrame();
    content_settings::PageSpecificContentSettings* content_settings =
        content_settings::PageSpecificContentSettings::GetForFrame(
            frame->GetProcess()->GetID(), frame->GetRoutingID());
    content_settings->OnContentAllowed(ContentSettingsType::GEOLOCATION);

    return *std::find_if(
        content_setting_views_->begin(), content_setting_views_->end(),
        [](const auto* view) {
          return view->GetTypeForTesting() ==
                 ContentSettingImageModel::ImageType::GEOLOCATION;
        });
  }

  void SimulateClickOnView(views::View* view) {
    const gfx::Point point;
    ui::MouseEvent event(ui::ET_MOUSE_PRESSED, point, point,
                         ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
    view->OnMouseEvent(&event);
    ui::MouseEvent event_rel(ui::ET_MOUSE_RELEASED, point, point,
                             ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                             ui::EF_LEFT_MOUSE_BUTTON);
    view->OnMouseEvent(&event_rel);
  }

 private:
  // For mocking a secure site.
  net::EmbeddedTestServer https_server_{net::EmbeddedTestServer::TYPE_HTTPS};
  content::ContentMockCertVerifier cert_verifier_;
};

}  // namespace

// Tests that the page info dialog doesn't anchor in a way that puts it outside
// of web-app windows. This is important as some platforms don't support bubble
// anchor adjustment (see |BubbleDialogDelegateView::CreateBubble()|).
IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest,
                       PageInfoBubblePosition) {
  SetUpWebApp();
  // Resize app window to only take up the left half of the screen.
  views::Widget* widget = browser_view_->GetWidget();
  gfx::Size screen_size =
      display::Screen::GetScreen()
          ->GetDisplayNearestWindow(widget->GetNativeWindow())
          .work_area_size();
  widget->SetBounds(
      gfx::Rect(0, 0, screen_size.width() / 2, screen_size.height()));

  // Show page info dialog (currently PWAs use page info in place of an actual
  // app info dialog).
  chrome::ExecuteCommand(app_browser_, IDC_WEB_APP_MENU_APP_INFO);

  // Check the bubble anchors inside the main app window even if there's space
  // available outside the main app window.
  gfx::Rect page_info_bounds =
      PageInfoBubbleViewBase::GetPageInfoBubbleForTesting()
          ->GetWidget()
          ->GetWindowBoundsInScreen();
  EXPECT_TRUE(widget->GetWindowBoundsInScreen().Contains(page_info_bounds));
}

IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest, FocusableViews) {
  SetUpWebApp();
  EXPECT_TRUE(browser_view_->contents_web_view()->HasFocus());
  browser_view_->GetFocusManager()->AdvanceFocus(false);
  EXPECT_TRUE(web_app_menu_button_->HasFocus());
  browser_view_->GetFocusManager()->AdvanceFocus(false);
  EXPECT_TRUE(browser_view_->contents_web_view()->HasFocus());
}

IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest,
                       ButtonVisibilityInOverviewMode) {
  SetUpWebApp();
  EXPECT_TRUE(web_app_frame_toolbar_->GetVisible());

  StartOverview();
  EXPECT_FALSE(web_app_frame_toolbar_->GetVisible());
  EndOverview();
  EXPECT_TRUE(web_app_frame_toolbar_->GetVisible());
}

IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest, FrameThemeColorIsSet) {
  SetUpWebApp();
  aura::Window* window = browser_view_->GetWidget()->GetNativeWindow();
  EXPECT_EQ(GetThemeColor(),
            window->GetProperty(chromeos::kFrameActiveColorKey));
  EXPECT_EQ(GetThemeColor(),
            window->GetProperty(chromeos::kFrameInactiveColorKey));
  EXPECT_EQ(gfx::kGoogleGrey200, GetActiveColor());
}

// Make sure that for web apps, the height of the frame doesn't exceed the
// height of the caption buttons.
IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest, FrameSize) {
  SetUpWebApp();
  const int inset = GetFrameViewChromeOS(browser_view_)->GetTopInset(false);
  EXPECT_EQ(inset, views::GetCaptionButtonLayoutSize(
                       views::CaptionButtonLayoutSize::kNonBrowserCaption)
                       .height());
  EXPECT_GE(inset, web_app_menu_button_->size().height());
  EXPECT_GE(inset, web_app_frame_toolbar_->size().height());
}

IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest,
                       IsToolbarButtonProvider) {
  SetUpWebApp();
  EXPECT_EQ(browser_view_->toolbar_button_provider(), web_app_frame_toolbar_);
}

IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest,
                       ShowManagePasswordsIcon) {
  SetUpWebApp();
  content::WebContents* web_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  PageActionIconView* manage_passwords_icon =
      GetPageActionIcon(PageActionIconType::kManagePasswords);

  EXPECT_TRUE(manage_passwords_icon);
  EXPECT_FALSE(manage_passwords_icon->GetVisible());

  password_manager::PasswordForm password_form;
  password_form.username_value = u"test";
  password_form.url = GetAppURL().DeprecatedGetOriginAsURL();
  PasswordsClientUIDelegateFromWebContents(web_contents)
      ->OnPasswordAutofilled({&password_form},
                             url::Origin::Create(password_form.url), nullptr);
  chrome::ManagePasswordsForPage(app_browser_);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(manage_passwords_icon->GetVisible());
}

IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest, ShowZoomIcon) {
  SetUpWebApp();
  content::WebContents* web_contents =
      app_browser_->tab_strip_model()->GetActiveWebContents();
  zoom::ZoomController* zoom_controller =
      zoom::ZoomController::FromWebContents(web_contents);
  PageActionIconView* zoom_icon = GetPageActionIcon(PageActionIconType::kZoom);

  EXPECT_TRUE(zoom_icon);
  EXPECT_FALSE(zoom_icon->GetVisible());
  EXPECT_FALSE(ZoomBubbleView::GetZoomBubble());

  zoom_controller->SetZoomLevel(blink::PageZoomFactorToZoomLevel(1.5));
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(zoom_icon->GetVisible());
  EXPECT_TRUE(ZoomBubbleView::GetZoomBubble());
}

IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest, ShowFindIcon) {
  SetUpWebApp();
  PageActionIconView* find_icon = GetPageActionIcon(PageActionIconType::kFind);

  EXPECT_TRUE(find_icon);
  EXPECT_FALSE(find_icon->GetVisible());

  chrome::Find(app_browser_);

  EXPECT_TRUE(find_icon->GetVisible());
}

IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest, ShowTranslateIcon) {
  SetUpWebApp();
  PageActionIconView* translate_icon =
      GetPageActionIcon(PageActionIconType::kTranslate);

  ASSERT_TRUE(translate_icon);
  EXPECT_FALSE(translate_icon->GetVisible());

  chrome::Find(app_browser_);
  browser_view_->ShowTranslateBubble(browser_view_->GetActiveWebContents(),
                                     translate::TRANSLATE_STEP_AFTER_TRANSLATE,
                                     "en", "fr",
                                     translate::TranslateErrors::NONE, true);

  EXPECT_TRUE(translate_icon->GetVisible());
}

// Tests that the focus toolbar command focuses the app menu button in web-app
// windows.
IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest,
                       BrowserCommandFocusToolbarAppMenu) {
  SetUpWebApp();
  EXPECT_FALSE(web_app_menu_button_->HasFocus());
  chrome::ExecuteCommand(app_browser_, IDC_FOCUS_TOOLBAR);
  EXPECT_TRUE(web_app_menu_button_->HasFocus());
}

// TODO(): Flaky crash on Chrome OS debug.
#if BUILDFLAG(IS_CHROMEOS_ASH)
#define MAYBE_BrowserCommandFocusToolbarGeolocation \
  DISABLED_BrowserCommandFocusToolbarGeolocation
#else
#define MAYBE_BrowserCommandFocusToolbarGeolocation \
  BrowserCommandFocusToolbarGeolocation
#endif
// Tests that the focus toolbar command focuses content settings icons before
// the app menu button when present in web-app windows.
IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest,
                       MAYBE_BrowserCommandFocusToolbarGeolocation) {
  SetUpWebApp();
  ContentSettingImageView* geolocation_icon = GrantGeolocationPermission();

  // In order to receive focus, the geo icon must be laid out (and be both
  // visible and nonzero size).
  web_app_frame_toolbar_->Layout();

  EXPECT_FALSE(web_app_menu_button_->HasFocus());
  EXPECT_FALSE(geolocation_icon->HasFocus());

  chrome::ExecuteCommand(app_browser_, IDC_FOCUS_TOOLBAR);

  EXPECT_FALSE(web_app_menu_button_->HasFocus());
  EXPECT_TRUE(geolocation_icon->HasFocus());
}

// Tests that the show app menu command opens the app menu for web-app windows.
IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest,
                       BrowserCommandShowAppMenu) {
  SetUpWebApp();
  EXPECT_EQ(nullptr, GetAppMenu());
  chrome::ExecuteCommand(app_browser_, IDC_SHOW_APP_MENU);
  EXPECT_NE(nullptr, GetAppMenu());
}

// Tests that the focus next pane command focuses the app menu for web-app
// windows.
IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest,
                       BrowserCommandFocusNextPane) {
  SetUpWebApp();
  EXPECT_FALSE(web_app_menu_button_->HasFocus());
  chrome::ExecuteCommand(app_browser_, IDC_FOCUS_NEXT_PANE);
  EXPECT_TRUE(web_app_menu_button_->HasFocus());
}

// Tests the app icon and title are not shown.
IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest, IconAndTitleNotShown) {
  SetUpWebApp();
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(app_browser_);
  EXPECT_FALSE(browser_view->ShouldShowWindowIcon());
  EXPECT_FALSE(browser_view->ShouldShowWindowTitle());
}

// Tests that the custom tab bar is focusable from the keyboard.
IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest,
                       CustomTabBarIsFocusable) {
  SetUpWebApp();

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(app_browser_);

  const GURL kOutOfScopeURL("http://example.org/");
  NavigateParams nav_params(app_browser_, kOutOfScopeURL,
                            ui::PAGE_TRANSITION_LINK);
  ui_test_utils::NavigateToURL(&nav_params);
  auto* custom_tab_bar = browser_view->toolbar()->custom_tab_bar();

  chrome::ExecuteCommand(app_browser_, IDC_FOCUS_NEXT_PANE);
  ASSERT_TRUE(web_app_menu_button_->HasFocus());

  EXPECT_FALSE(custom_tab_bar->close_button_for_testing()->HasFocus());
  chrome::ExecuteCommand(app_browser_, IDC_FOCUS_NEXT_PANE);
  EXPECT_TRUE(custom_tab_bar->close_button_for_testing()->HasFocus());
}

// Tests that the focus previous pane command focuses the app menu for web-app
// windows.
IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest,
                       BrowserCommandFocusPreviousPane) {
  SetUpWebApp();
  EXPECT_FALSE(web_app_menu_button_->HasFocus());
  chrome::ExecuteCommand(app_browser_, IDC_FOCUS_PREVIOUS_PANE);
  EXPECT_TRUE(web_app_menu_button_->HasFocus());
}

// Tests that a web app's content settings icons can be interacted with.
IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest, ContentSettingIcons) {
  SetUpWebApp();
  for (auto* view : *content_setting_views_)
    EXPECT_FALSE(view->GetVisible());

  ContentSettingImageView* geolocation_icon = GrantGeolocationPermission();

  for (auto* view : *content_setting_views_) {
    bool is_geolocation_icon = view == geolocation_icon;
    EXPECT_EQ(is_geolocation_icon, view->GetVisible());
  }

  // Press the geolocation button.
  base::HistogramTester histograms;
  geolocation_icon->OnKeyPressed(
      ui::KeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_SPACE, ui::EF_NONE));
  geolocation_icon->OnKeyReleased(
      ui::KeyEvent(ui::ET_KEY_RELEASED, ui::VKEY_SPACE, ui::EF_NONE));

  histograms.ExpectBucketCount(
      "HostedAppFrame.ContentSettings.ImagePressed",
      static_cast<int>(ContentSettingImageModel::ImageType::GEOLOCATION), 1);
  histograms.ExpectBucketCount(
      "ContentSettings.ImagePressed",
      static_cast<int>(ContentSettingImageModel::ImageType::GEOLOCATION), 1);
}

// Regression test for https://crbug.com/839955
IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest,
                       ActiveStateOfButtonMatchesWidget) {
  SetUpWebApp();
  chromeos::FrameCaptionButtonContainerView::TestApi test(
      GetFrameViewChromeOS(browser_view_)->caption_button_container_);
  EXPECT_TRUE(test.size_button()->GetPaintAsActive());
  EXPECT_TRUE(GetPaintingAsActive());

  browser_view_->GetWidget()->Deactivate();
  EXPECT_FALSE(test.size_button()->GetPaintAsActive());
  EXPECT_FALSE(GetPaintingAsActive());
}

IN_PROC_BROWSER_TEST_P(WebAppNonClientFrameViewAshTest, PopupHasNoToolbar) {
  SetUpWebApp();
  {
    NavigateParams navigate_params(app_browser_, GetAppURL(),
                                   ui::PAGE_TRANSITION_LINK);
    navigate_params.disposition = WindowOpenDisposition::NEW_POPUP;

    content::TestNavigationObserver navigation_observer(GetAppURL());
    navigation_observer.StartWatchingNewWebContents();
    Navigate(&navigate_params);
    navigation_observer.WaitForNavigationFinished();
  }

  Browser* popup_browser = BrowserList::GetInstance()->GetLastActive();
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(popup_browser);
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);
  EXPECT_FALSE(frame_view->web_app_frame_toolbar_for_testing());
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

// Test the normal type browser's kTopViewInset is always 0.
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTest, TopViewInset) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ImmersiveModeController* immersive_mode_controller =
      browser_view->immersive_mode_controller();
  aura::Window* window = browser()->window()->GetNativeWindow();
  EXPECT_FALSE(immersive_mode_controller->IsEnabled());
  EXPECT_EQ(0, window->GetProperty(aura::client::kTopViewInset));

  // The kTopViewInset should be 0 when in immersive mode.
  ToggleFullscreenModeAndWait(browser());
  EXPECT_TRUE(immersive_mode_controller->IsEnabled());
  EXPECT_EQ(0, window->GetProperty(aura::client::kTopViewInset));

  // An immersive reveal shows the top of the frame.
  std::unique_ptr<ImmersiveRevealedLock> revealed_lock(
      immersive_mode_controller->GetRevealedLock(
          ImmersiveModeController::ANIMATE_REVEAL_NO));
  EXPECT_TRUE(immersive_mode_controller->IsRevealed());
  EXPECT_EQ(0, window->GetProperty(aura::client::kTopViewInset));

  // End the reveal and exit immersive mode.
  // The kTopViewInset should be 0 when immersive mode is exited.
  revealed_lock.reset();
  ToggleFullscreenModeAndWait(browser());
  EXPECT_FALSE(immersive_mode_controller->IsEnabled());
  EXPECT_EQ(0, window->GetProperty(aura::client::kTopViewInset));
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
// Test that for a browser window, its caption buttons are always hidden in
// tablet mode.
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTest,
                       BrowserHeaderVisibilityInTabletModeTest) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  Widget* widget = browser_view->GetWidget();
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);

  widget->GetNativeWindow()->SetProperty(
      aura::client::kResizeBehaviorKey,
      aura::client::kResizeBehaviorCanMaximize |
          aura::client::kResizeBehaviorCanResize);
  EXPECT_TRUE(frame_view->caption_button_container_->GetVisible());

  StartOverview();
  EXPECT_FALSE(frame_view->caption_button_container_->GetVisible());
  EndOverview();
  EXPECT_TRUE(frame_view->caption_button_container_->GetVisible());

  ASSERT_NO_FATAL_FAILURE(
      ash::ShellTestApi().SetTabletModeEnabledForTest(true));
  EXPECT_FALSE(frame_view->caption_button_container_->GetVisible());
  StartOverview();
  EXPECT_FALSE(frame_view->caption_button_container_->GetVisible());
  EndOverview();
  EXPECT_FALSE(frame_view->caption_button_container_->GetVisible());
  ash::SplitViewTestApi().SnapWindow(widget->GetNativeWindow(),
                                     ash::SplitViewTestApi::SnapPosition::LEFT);
  EXPECT_FALSE(frame_view->caption_button_container_->GetVisible());
}

// Test that for a browser app window, its caption buttons may or may not hide
// in tablet mode.
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTest,
                       AppHeaderVisibilityInTabletModeTest) {
  // Create a browser app window.
  Browser::CreateParams params = Browser::CreateParams::CreateForApp(
      "test_browser_app", true /* trusted_source */, gfx::Rect(),
      browser()->profile(), true);
  params.initial_show_state = ui::SHOW_STATE_DEFAULT;
  Browser* browser2 = Browser::Create(params);
  AddBlankTabAndShow(browser2);
  BrowserView* browser_view2 = BrowserView::GetBrowserViewForBrowser(browser2);
  Widget* widget2 = browser_view2->GetWidget();
  BrowserNonClientFrameViewChromeOS* frame_view2 =
      GetFrameViewChromeOS(browser_view2);
  widget2->GetNativeWindow()->SetProperty(
      aura::client::kResizeBehaviorKey,
      aura::client::kResizeBehaviorCanMaximize |
          aura::client::kResizeBehaviorCanResize);
  StartOverview();
  EXPECT_FALSE(frame_view2->caption_button_container_->GetVisible());
  EndOverview();
  EXPECT_TRUE(frame_view2->caption_button_container_->GetVisible());

  ASSERT_NO_FATAL_FAILURE(
      ash::ShellTestApi().SetTabletModeEnabledForTest(true));
  StartOverview();
  EXPECT_FALSE(frame_view2->caption_button_container_->GetVisible());

  EndOverview();
  EXPECT_TRUE(frame_view2->caption_button_container_->GetVisible());

  ash::SplitViewTestApi().SnapWindow(
      widget2->GetNativeWindow(), ash::SplitViewTestApi::SnapPosition::RIGHT);
  EXPECT_TRUE(frame_view2->caption_button_container_->GetVisible());
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

// Regression test for https://crbug.com/879851.
// Tests that we don't accidentally change the color of app frame title bars.
// Update expectation if change is intentional.
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTest, AppFrameColor) {
  browser()->window()->Close();

  // Open a new app window.
  Browser* app_browser = Browser::Create(Browser::CreateParams::CreateForApp(
      "test_browser_app", true /* trusted_source */, gfx::Rect(),
      browser()->profile(), true /* user_gesture */));
  aura::Window* window = app_browser->window()->GetNativeWindow();
  window->Show();

  SkColor active_frame_color =
      window->GetProperty(chromeos::kFrameActiveColorKey);
  EXPECT_EQ(active_frame_color, SkColorSetRGB(253, 254, 255))
      << "RGB: " << SkColorGetR(active_frame_color) << ", "
      << SkColorGetG(active_frame_color) << ", "
      << SkColorGetB(active_frame_color);
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
IN_PROC_BROWSER_TEST_P(BrowserNonClientFrameViewChromeOSTest,
                       ImmersiveModeTopViewInset) {
  browser()->window()->Close();

  // Open a new app window.
  Browser::CreateParams params = Browser::CreateParams::CreateForApp(
      "test_browser_app", true /* trusted_source */, gfx::Rect(),
      browser()->profile(), true);
  params.initial_show_state = ui::SHOW_STATE_DEFAULT;
  Browser* browser = Browser::Create(params);
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  ImmersiveModeController* immersive_mode_controller =
      browser_view->immersive_mode_controller();
  aura::Window* window = browser->window()->GetNativeWindow();
  window->Show();
  EXPECT_FALSE(immersive_mode_controller->IsEnabled());
  EXPECT_LT(0, window->GetProperty(aura::client::kTopViewInset));

  // The kTopViewInset should be 0 when in immersive mode.
  ToggleFullscreenModeAndWait(browser);
  EXPECT_TRUE(immersive_mode_controller->IsEnabled());
  EXPECT_EQ(0, window->GetProperty(aura::client::kTopViewInset));

  // An immersive reveal shows the top of the frame.
  std::unique_ptr<ImmersiveRevealedLock> revealed_lock(
      immersive_mode_controller->GetRevealedLock(
          ImmersiveModeController::ANIMATE_REVEAL_NO));
  EXPECT_TRUE(immersive_mode_controller->IsRevealed());
  EXPECT_EQ(0, window->GetProperty(aura::client::kTopViewInset));

  // End the reveal and exit immersive mode.
  // The kTopViewInset should be larger than 0 again when immersive mode is
  // exited.
  revealed_lock.reset();
  ToggleFullscreenModeAndWait(browser);
  EXPECT_FALSE(immersive_mode_controller->IsEnabled());
  EXPECT_LT(0, window->GetProperty(aura::client::kTopViewInset));

  // The kTopViewInset is the same as in overview mode.
  const int inset_normal = window->GetProperty(aura::client::kTopViewInset);
  StartOverview();
  const int inset_in_overview_mode =
      window->GetProperty(aura::client::kTopViewInset);
  EXPECT_EQ(inset_normal, inset_in_overview_mode);
}

namespace {

class HomeLauncherBrowserNonClientFrameViewChromeOSTest
    : public TopChromeMdParamTest<InProcessBrowserTest> {
 public:
  HomeLauncherBrowserNonClientFrameViewChromeOSTest() = default;

  HomeLauncherBrowserNonClientFrameViewChromeOSTest(
      const HomeLauncherBrowserNonClientFrameViewChromeOSTest&) = delete;
  HomeLauncherBrowserNonClientFrameViewChromeOSTest& operator=(
      const HomeLauncherBrowserNonClientFrameViewChromeOSTest&) = delete;

  ~HomeLauncherBrowserNonClientFrameViewChromeOSTest() override = default;

  void SetUpDefaultCommandLine(base::CommandLine* command_line) override {
    TopChromeMdParamTest<InProcessBrowserTest>::SetUpDefaultCommandLine(
        command_line);

    command_line->AppendSwitch(ash::switches::kAshEnableTabletMode);
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_P(HomeLauncherBrowserNonClientFrameViewChromeOSTest,
                       TabletModeBrowserCaptionButtonVisibility) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);

  EXPECT_TRUE(frame_view->caption_button_container_->GetVisible());
  ASSERT_NO_FATAL_FAILURE(
      ash::ShellTestApi().SetTabletModeEnabledForTest(true));
  EXPECT_FALSE(frame_view->caption_button_container_->GetVisible());

  StartOverview();
  EXPECT_FALSE(frame_view->caption_button_container_->GetVisible());
  EndOverview();
  EXPECT_FALSE(frame_view->caption_button_container_->GetVisible());

  ASSERT_NO_FATAL_FAILURE(
      ash::ShellTestApi().SetTabletModeEnabledForTest(false));
  EXPECT_TRUE(frame_view->caption_button_container_->GetVisible());
}

// TODO(crbug.com/993974): When the test flake has been addressed, improve
// performance by consolidating this unit test with
// |TabletModeBrowserCaptionButtonVisibility|. Do not forget to remove the
// corresponding |FRIEND_TEST_ALL_PREFIXES| usage from
// |BrowserNonClientFrameViewChromeOS|.
IN_PROC_BROWSER_TEST_P(HomeLauncherBrowserNonClientFrameViewChromeOSTest,
                       CaptionButtonVisibilityForBrowserLaunchedInTabletMode) {
  ASSERT_NO_FATAL_FAILURE(
      ash::ShellTestApi().SetTabletModeEnabledForTest(true));
  EXPECT_FALSE(GetFrameViewChromeOS(BrowserView::GetBrowserViewForBrowser(
                                        CreateBrowser(browser()->profile())))
                   ->caption_button_container_->GetVisible());
}

IN_PROC_BROWSER_TEST_P(HomeLauncherBrowserNonClientFrameViewChromeOSTest,
                       TabletModeAppCaptionButtonVisibility) {
  browser()->window()->Close();

  // Open a new app window.
  Browser::CreateParams params = Browser::CreateParams::CreateForApp(
      "test_browser_app", true /* trusted_source */, gfx::Rect(),
      browser()->profile(), true);
  params.initial_show_state = ui::SHOW_STATE_DEFAULT;
  Browser* browser = Browser::Create(params);
  ASSERT_TRUE(browser->is_type_app());
  browser->window()->Show();

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);
  EXPECT_TRUE(frame_view->caption_button_container_->GetVisible());

  // Tablet mode doesn't affect app's caption button's visibility.
  ASSERT_NO_FATAL_FAILURE(
      ash::ShellTestApi().SetTabletModeEnabledForTest(true));
  EXPECT_TRUE(frame_view->caption_button_container_->GetVisible());

  // However, overview mode does.
  StartOverview();
  EXPECT_FALSE(frame_view->caption_button_container_->GetVisible());
  EndOverview();
  EXPECT_TRUE(frame_view->caption_button_container_->GetVisible());

  ASSERT_NO_FATAL_FAILURE(
      ash::ShellTestApi().SetTabletModeEnabledForTest(false));
  EXPECT_TRUE(frame_view->caption_button_container_->GetVisible());
}

namespace {

class TabSearchFrameCaptionButtonTest
    : public TopChromeMdParamTest<InProcessBrowserTest> {
 public:
  TabSearchFrameCaptionButtonTest() = default;
  TabSearchFrameCaptionButtonTest(const TabSearchFrameCaptionButtonTest&) =
      delete;
  TabSearchFrameCaptionButtonTest& operator=(
      const TabSearchFrameCaptionButtonTest&) = delete;
  ~TabSearchFrameCaptionButtonTest() override = default;

  void SetUp() override {
    scoped_feature_list_.InitAndEnableFeature(
        features::kChromeOSTabSearchCaptionButton);
    TopChromeMdParamTest<InProcessBrowserTest>::SetUp();
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

}  // namespace

IN_PROC_BROWSER_TEST_P(TabSearchFrameCaptionButtonTest,
                       TabSearchBubbleHostTest) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  BrowserNonClientFrameViewChromeOS* frame_view =
      GetFrameViewChromeOS(browser_view);
  ASSERT_TRUE(browser()->is_type_normal());

  chromeos::FrameCaptionButtonContainerView::TestApi test(
      frame_view->caption_button_container_);
  EXPECT_TRUE(test.custom_button());
  EXPECT_EQ(browser_view->GetTabSearchBubbleHost()->button(),
            test.custom_button());
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#define INSTANTIATE_TEST_SUITE(name) \
  INSTANTIATE_TEST_SUITE_P(All, name, ::testing::Values(false, true))

INSTANTIATE_TEST_SUITE(BrowserNonClientFrameViewChromeOSTest);
INSTANTIATE_TEST_SUITE(BrowserNonClientFrameViewChromeOSTestNoWebUiTabStrip);
#if BUILDFLAG(IS_CHROMEOS_ASH)
INSTANTIATE_TEST_SUITE(
    BrowserNonClientFrameViewChromeOSTestBackgroundColorChange);
INSTANTIATE_TEST_SUITE(BrowserNonClientFrameViewChromeOSTestWithWebUiTabStrip);
INSTANTIATE_TEST_SUITE(WebAppNonClientFrameViewAshTest);
INSTANTIATE_TEST_SUITE(HomeLauncherBrowserNonClientFrameViewChromeOSTest);
INSTANTIATE_TEST_SUITE(TabSearchFrameCaptionButtonTest);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
