// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/intent_picker_bubble_view.h"

#include <memory>
#include <vector>

#include "ash/components/arc/session/arc_bridge_service.h"
#include "ash/components/arc/session/arc_service_manager.h"
#include "ash/components/arc/test/arc_util_test_support.h"
#include "ash/components/arc/test/connection_holder_util.h"
#include "ash/components/arc/test/fake_app_instance.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/ash/arc/arc_util.h"
#include "chrome/browser/ash/arc/session/arc_session_manager.h"
#include "chrome/browser/ui/app_list/arc/arc_app_list_prefs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/location_bar/intent_picker_view.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/web_application_info.h"
#include "chrome/common/chrome_features.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/arc/test/fake_intent_helper_instance.h"
#include "components/services/app_service/public/cpp/icon_loader.h"
#include "components/services/app_service/public/cpp/intent_filter_util.h"
#include "components/services/app_service/public/cpp/intent_test_util.h"
#include "components/services/app_service/public/cpp/intent_util.h"
#include "components/services/app_service/public/mojom/types.mojom.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/prerender_test_util.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/base_event_utils.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/widget/any_widget_observer.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace mojo {

template <>
struct TypeConverter<arc::mojom::ArcPackageInfoPtr,
                     arc::mojom::ArcPackageInfo> {
  static arc::mojom::ArcPackageInfoPtr Convert(
      const arc::mojom::ArcPackageInfo& package_info) {
    return package_info.Clone();
  }
};

}  // namespace mojo

namespace {

using content::RenderFrameHost;
using content::test::PrerenderHostObserver;
using content::test::PrerenderHostRegistryObserver;
using content::test::PrerenderTestHelper;

const char kTestAppActivity[] = "abcdefg";

class FakeIconLoader : public apps::IconLoader {
 public:
  FakeIconLoader() = default;
  FakeIconLoader(const FakeIconLoader&) = delete;
  FakeIconLoader& operator=(const FakeIconLoader&) = delete;
  ~FakeIconLoader() override = default;

  std::unique_ptr<apps::IconLoader::Releaser> LoadIconFromIconKey(
      apps::AppType app_type,
      const std::string& app_id,
      const apps::IconKey& icon_key,
      apps::IconType icon_type,
      int32_t size_hint_in_dip,
      bool allow_placeholder_icon,
      apps::LoadIconCallback callback) override {
    auto iv = std::make_unique<apps::IconValue>();
    iv->icon_type = icon_type;
    iv->uncompressed = gfx::ImageSkia(gfx::ImageSkiaRep(gfx::Size(1, 1), 1.0f));
    iv->is_placeholder_icon = false;

    std::move(callback).Run(std::move(iv));
    return nullptr;
  }

  std::unique_ptr<apps::IconLoader::Releaser> LoadIconFromIconKey(
      apps::mojom::AppType app_type,
      const std::string& app_id,
      apps::mojom::IconKeyPtr mojom_icon_key,
      apps::mojom::IconType icon_type,
      int32_t size_hint_in_dip,
      bool allow_placeholder_icon,
      apps::mojom::Publisher::LoadIconCallback callback) override {
    auto icon_key = apps::ConvertMojomIconKeyToIconKey(mojom_icon_key);
    return LoadIconFromIconKey(
        apps::ConvertMojomAppTypToAppType(app_type), app_id, *icon_key,
        apps::ConvertMojomIconTypeToIconType(icon_type), size_hint_in_dip,
        allow_placeholder_icon,
        apps::IconValueToMojomIconValueCallback(std::move(callback)));
  }
};
}  // namespace

class IntentPickerBubbleViewBrowserTestChromeOS : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    arc::SetArcAvailableCommandLineForTesting(command_line);
  }

  void SetUpInProcessBrowserTestFixture() override {
    InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
    arc::ArcSessionManager::SetUiEnabledForTesting(false);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    app_service_proxy_ = apps::AppServiceProxyFactory::GetForProfile(profile());
    ASSERT_TRUE(app_service_proxy_);
    app_service_proxy_->OverrideInnerIconLoaderForTesting(&icon_loader_);

    arc::SetArcPlayStoreEnabledForProfile(profile(), true);

    intent_helper_instance_ = std::make_unique<arc::FakeIntentHelperInstance>();
    arc::ArcServiceManager::Get()
        ->arc_bridge_service()
        ->intent_helper()
        ->SetInstance(intent_helper_instance_.get());
    WaitForInstanceReady(
        arc::ArcServiceManager::Get()->arc_bridge_service()->intent_helper());
    app_instance_ = std::make_unique<arc::FakeAppInstance>(app_host());
    arc::ArcServiceManager::Get()->arc_bridge_service()->app()->SetInstance(
        app_instance_.get());
    WaitForInstanceReady(
        arc::ArcServiceManager::Get()->arc_bridge_service()->app());
  }

  std::string AddArcAppWithIntentFilter(const std::string& app_name,
                                        const GURL& url) {
    std::vector<arc::mojom::AppInfoPtr> app_infos;

    arc::mojom::AppInfoPtr app_info(arc::mojom::AppInfo::New());
    app_info->name = app_name;
    app_info->package_name = app_name;
    app_info->activity = kTestAppActivity;
    app_info->sticky = false;
    app_infos.push_back(std::move(app_info));
    app_host()->OnAppListRefreshed(std::move(app_infos));
    WaitForAppService();
    std::string app_id = ArcAppListPrefs::GetAppId(app_name, kTestAppActivity);
    auto test_app_info = app_prefs()->GetApp(app_id);
    EXPECT_TRUE(test_app_info);

    std::vector<apps::mojom::AppPtr> apps;
    auto app = apps::mojom::App::New();
    app->app_id = app_id;
    app->app_type = apps::mojom::AppType::kArc;
    app->name = app_name;
    auto intent_filter = apps_util::CreateIntentFilterForUrlScope(url);
    app->intent_filters.push_back(std::move(intent_filter));
    apps.push_back(std::move(app));
    app_service_proxy_->AppRegistryCache().OnApps(
        std::move(apps), apps::mojom::AppType::kArc,
        false /* should_notify_initialized */);
    WaitForAppService();

    return app_id;
  }

  std::string InstallWebApp(const std::string& app_name, const GURL& url) {
    auto web_app_info = std::make_unique<WebApplicationInfo>();
    web_app_info->title = base::UTF8ToUTF16(app_name);
    web_app_info->start_url = url;
    web_app_info->scope = url;
    web_app_info->user_display_mode = blink::mojom::DisplayMode::kStandalone;
    auto app_id =
        web_app::test::InstallWebApp(profile(), std::move(web_app_info));
    WaitForAppService();
    return app_id;
  }

  PageActionIconView* GetIntentPickerIcon() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->toolbar_button_provider()
        ->GetPageActionIconView(PageActionIconType::kIntentPicker);
  }

  IntentPickerBubbleView* intent_picker_bubble() {
    return IntentPickerBubbleView::intent_picker_bubble();
  }

  views::Checkbox* remember_selection_checkbox() {
    return intent_picker_bubble()->remember_selection_checkbox_;
  }

  // TODO(crbug.com/1265991): There should be an explicit signal we can wait on
  // rather than assuming the AppService will be started after RunUntilIdle.
  void WaitForAppService() { base::RunLoop().RunUntilIdle(); }

  ArcAppListPrefs* app_prefs() { return ArcAppListPrefs::Get(profile()); }

  // Returns as AppHost interface in order to access to private implementation
  // of the interface.
  arc::mojom::AppHost* app_host() { return app_prefs(); }

  Profile* profile() { return browser()->profile(); }

  // The handled intents list in the intent helper instance represents the arc
  // app that app service tried to launch.
  const std::vector<arc::FakeIntentHelperInstance::HandledIntent>&
  launched_arc_apps() {
    return intent_helper_instance_->handled_intents();
  }

  void clear_launched_arc_apps() {
    intent_helper_instance_->clear_handled_intents();
  }

  void ClickIconToShowBubble() {
    views::NamedWidgetShownWaiter waiter(
        views::test::AnyWidgetTestPasskey{},
        IntentPickerBubbleView::kViewClassName);
    GetIntentPickerIcon()->ExecuteForTesting();
    waiter.WaitIfNeededAndGet();
    ASSERT_TRUE(intent_picker_bubble());
    EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  }

  // Dummy method to be called upon bubble closing.
  void OnBubbleClosed(const std::string& selected_app_package,
                      apps::PickerEntryType entry_type,
                      apps::IntentPickerCloseReason close_reason,
                      bool should_persist) {
    bubble_closed_ = true;
  }

  void ShowBubbleForTesting() {
    std::vector<apps::IntentPickerAppInfo> app_info;
    app_info.emplace_back(apps::PickerEntryType::kArc, ui::ImageModel(),
                          "package_1", "dank app 1");
    app_info.emplace_back(apps::PickerEntryType::kArc, ui::ImageModel(),
                          "package_2", "dank_app_2");

    browser()->window()->ShowIntentPickerBubble(
        std::move(app_info), /*show_stay_in_chrome=*/true,
        /*show_remember_selection=*/true, PageActionIconType::kIntentPicker,
        absl::nullopt,
        base::BindOnce(
            &IntentPickerBubbleViewBrowserTestChromeOS::OnBubbleClosed,
            base::Unretained(this)));
  }

  bool bubble_closed() { return bubble_closed_; }

  void CheckStayInChrome() {
    ASSERT_TRUE(intent_picker_bubble());
    intent_picker_bubble()->CancelDialog();
    EXPECT_EQ(BrowserList::GetInstance()->GetLastActive(), browser());
    EXPECT_EQ(launched_arc_apps().size(), 0U);
  }

  void VerifyArcAppLaunched(const std::string& app_name, const GURL& test_url) {
    WaitForAppService();
    ASSERT_EQ(1U, launched_arc_apps().size());
    EXPECT_EQ(app_name, launched_arc_apps()[0].activity->package_name);
    EXPECT_EQ(test_url.spec(), launched_arc_apps()[0].intent->data);
  }

  bool VerifyPWALaunched(const std::string& app_id) {
    WaitForAppService();
    Browser* app_browser = BrowserList::GetInstance()->GetLastActive();
    return web_app::AppBrowserController::IsForWebApp(app_browser, app_id);
  }

 private:
  apps::AppServiceProxy* app_service_proxy_ = nullptr;
  std::unique_ptr<arc::FakeIntentHelperInstance> intent_helper_instance_;
  std::unique_ptr<arc::FakeAppInstance> app_instance_;
  FakeIconLoader icon_loader_;
  bool bubble_closed_ = false;
};

// Test that the intent picker bubble will pop out for ARC apps.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       BubblePopOut) {
  GURL test_url("https://www.google.com/");
  std::string app_name = "test_name";
  auto app_id = AddArcAppWithIntentFilter(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       IntentPickerBubbleView::kViewClassName);
  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  waiter.WaitIfNeededAndGet();

  EXPECT_TRUE(intent_picker_view->GetVisible());
  ASSERT_TRUE(intent_picker_bubble());
  EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  EXPECT_EQ(1U, intent_picker_bubble()->GetScrollViewSize());
  auto& app_info = intent_picker_bubble()->app_info_for_testing();
  ASSERT_EQ(1U, app_info.size());
  EXPECT_EQ(app_id, app_info[0].launch_name);
  EXPECT_EQ(app_name, app_info[0].display_name);

  // Check the status of the remember selection checkbox.
  ASSERT_TRUE(remember_selection_checkbox());
  EXPECT_TRUE(remember_selection_checkbox()->GetEnabled());
  EXPECT_FALSE(remember_selection_checkbox()->GetChecked());

  // Launch the default selected app.
  EXPECT_EQ(0U, launched_arc_apps().size());

  content::TestNavigationObserver observer(
      browser()->tab_strip_model()->GetActiveWebContents());

  intent_picker_bubble()->AcceptDialog();
  ASSERT_NO_FATAL_FAILURE(VerifyArcAppLaunched(app_name, test_url));

  // The page should go back to blank state after launching the app.
  observer.WaitForNavigationFinished();

  // Make sure that the intent picker icon is no longer visible.
  ASSERT_TRUE(intent_picker_view);
  EXPECT_FALSE(intent_picker_view->GetVisible());
}

// Test that navigate outside url scope will not show the intent picker icon or
// bubble.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       OutOfScopeDoesNotShowBubble) {
  GURL test_url("https://www.google.com/");
  GURL out_of_scope_url("https://www.example.com/");
  std::string app_name = "test_name";
  auto app_id = AddArcAppWithIntentFilter(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), out_of_scope_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);
  WaitForAppService();
  EXPECT_FALSE(intent_picker_view->GetVisible());
  EXPECT_FALSE(intent_picker_bubble());
}

// Test that navigating to service pages (chrome://) will hide the intent
// picker icon.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       DoNotShowIconAndBubbleOnServicePages) {
  GURL test_url("https://www.google.com/");
  GURL chrome_pages_url("chrome://version");
  std::string app_name = "test_name";
  auto app_id = InstallWebApp(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Go to google.com and wait for the intent picker icon to load.
  {
    NavigateParams params(browser(), test_url,
                          ui::PageTransition::PAGE_TRANSITION_TYPED);
    ui_test_utils::NavigateToURL(&params);
  }

  WaitForAppService();

  ASSERT_TRUE(intent_picker_view);
  EXPECT_TRUE(intent_picker_view->GetVisible());

  // Now switch to chrome://version.
  {
    NavigateParams params(browser(), chrome_pages_url,
                          ui::PageTransition::PAGE_TRANSITION_TYPED);
    // Navigates and waits for loading to finish.
    ui_test_utils::NavigateToURL(&params);
  }

  WaitForAppService();

  // Make sure that the intent picker icon is no longer visible.

  ASSERT_TRUE(intent_picker_view);
  EXPECT_FALSE(intent_picker_view->GetVisible());
}

// Test that intent picker bubble pop up status will depends on
// kIntentPickerPWAPersistence flag for if there is only PWA as
// candidates.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       PWAOnlyShowBubble) {
  GURL test_url("https://www.google.com/");
  std::string app_name = "test_name";
  auto app_id = InstallWebApp(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);
  WaitForAppService();
  EXPECT_TRUE(intent_picker_view->GetVisible());
  if (base::FeatureList::IsEnabled(features::kIntentPickerPWAPersistence)) {
    ASSERT_TRUE(intent_picker_bubble());
    EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  } else {
    EXPECT_FALSE(intent_picker_bubble());
    ClickIconToShowBubble();
  }

  EXPECT_EQ(1U, intent_picker_bubble()->GetScrollViewSize());
  auto& app_info = intent_picker_bubble()->app_info_for_testing();
  ASSERT_EQ(1U, app_info.size());
  EXPECT_EQ(app_id, app_info[0].launch_name);
  EXPECT_EQ(app_name, app_info[0].display_name);

  // Check the status of the remember selection checkbox.
  ASSERT_TRUE(remember_selection_checkbox());
  EXPECT_EQ(
      remember_selection_checkbox()->GetEnabled(),
      base::FeatureList::IsEnabled(features::kIntentPickerPWAPersistence));
  EXPECT_FALSE(remember_selection_checkbox()->GetChecked());

  // Launch the app.
  intent_picker_bubble()->AcceptDialog();
  EXPECT_TRUE(VerifyPWALaunched(app_id));
}

// Test that intent picker bubble will not pop up for non-link navigation.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       NotLinkDoesNotShowBubble) {
  GURL test_url("https://www.google.com/");
  std::string app_name = "test_name";
  auto app_id = AddArcAppWithIntentFilter(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_FROM_ADDRESS_BAR);

  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);
  WaitForAppService();
  EXPECT_TRUE(intent_picker_view->GetVisible());
  EXPECT_FALSE(intent_picker_bubble());

  ClickIconToShowBubble();
  EXPECT_EQ(1U, intent_picker_bubble()->GetScrollViewSize());
  auto& app_info = intent_picker_bubble()->app_info_for_testing();
  ASSERT_EQ(1U, app_info.size());
  EXPECT_EQ(app_id, app_info[0].launch_name);
  EXPECT_EQ(app_name, app_info[0].display_name);

  // Launch the default selected app.
  EXPECT_EQ(0U, launched_arc_apps().size());
  intent_picker_bubble()->AcceptDialog();
  ASSERT_NO_FATAL_FAILURE(VerifyArcAppLaunched(app_name, test_url));
}

// Test that dismiss the bubble for 2 times for the same origin will not show
// the bubble again.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       DismissBubble) {
  GURL test_url("https://www.google.com/");
  std::string app_name = "test_name";
  auto app_id = AddArcAppWithIntentFilter(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);
  WaitForAppService();
  EXPECT_TRUE(intent_picker_view->GetVisible());
  ASSERT_TRUE(intent_picker_bubble());
  EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  EXPECT_EQ(1U, intent_picker_bubble()->GetScrollViewSize());
  auto& app_info = intent_picker_bubble()->app_info_for_testing();
  ASSERT_EQ(1U, app_info.size());
  EXPECT_EQ(app_id, app_info[0].launch_name);
  EXPECT_EQ(app_name, app_info[0].display_name);
  EXPECT_TRUE(intent_picker_bubble()->Close());

  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));
  ui_test_utils::NavigateToURL(&params);
  WaitForAppService();
  EXPECT_TRUE(intent_picker_view->GetVisible());
  ASSERT_TRUE(intent_picker_bubble());
  EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  EXPECT_TRUE(intent_picker_bubble()->Close());

  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));
  ui_test_utils::NavigateToURL(&params);
  WaitForAppService();
  EXPECT_TRUE(intent_picker_view->GetVisible());
  EXPECT_FALSE(intent_picker_bubble());

  ClickIconToShowBubble();
  EXPECT_EQ(1U, intent_picker_bubble()->GetScrollViewSize());
  auto& new_app_info = intent_picker_bubble()->app_info_for_testing();
  ASSERT_EQ(1U, new_app_info.size());
  EXPECT_EQ(app_id, new_app_info[0].launch_name);
  EXPECT_EQ(app_name, new_app_info[0].display_name);

  // Launch the default selected app.
  EXPECT_EQ(0U, launched_arc_apps().size());
  intent_picker_bubble()->AcceptDialog();
  ASSERT_NO_FATAL_FAILURE(VerifyArcAppLaunched(app_name, test_url));
}

// Test that show intent picker bubble twice without closing doesn't
// crash the browser.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       ShowBubbleTwice) {
  ShowBubbleForTesting();
  ASSERT_TRUE(intent_picker_bubble());
  EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  EXPECT_EQ(2U, intent_picker_bubble()->GetScrollViewSize());
  ShowBubbleForTesting();
  ASSERT_TRUE(bubble_closed());
  ASSERT_TRUE(intent_picker_bubble());
  EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  EXPECT_EQ(2U, intent_picker_bubble()->GetScrollViewSize());
}

// Test that loading a page with pushState() call that doesn't change URL work
// as normal.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       PushStateLoadingTest) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL test_url =
      embedded_test_server()->GetURL("/intent_picker/push_state_test.html");
  std::string app_name = "test_name";
  auto app_id = AddArcAppWithIntentFilter(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       IntentPickerBubbleView::kViewClassName);
  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  waiter.WaitIfNeededAndGet();
  EXPECT_TRUE(intent_picker_view->GetVisible());
  ASSERT_TRUE(intent_picker_bubble());
  EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  EXPECT_EQ(1U, intent_picker_bubble()->GetScrollViewSize());
  auto& app_info = intent_picker_bubble()->app_info_for_testing();
  ASSERT_EQ(1U, app_info.size());
  EXPECT_EQ(app_id, app_info[0].launch_name);
  EXPECT_EQ(app_name, app_info[0].display_name);

  // Launch the default selected app.
  EXPECT_EQ(0U, launched_arc_apps().size());
  intent_picker_bubble()->AcceptDialog();
  ASSERT_NO_FATAL_FAILURE(VerifyArcAppLaunched(app_name, test_url));
}

// Test that loading a page with pushState() call that changes URL
// updates the intent picker view.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       PushStateURLChangeTest) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL test_url =
      embedded_test_server()->GetURL("/intent_picker/push_state_test.html");
  std::string app_name = "test_name";
  auto app_id = AddArcAppWithIntentFilter(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       IntentPickerBubbleView::kViewClassName);
  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  waiter.WaitIfNeededAndGet();
  EXPECT_TRUE(intent_picker_view->GetVisible());
  ASSERT_TRUE(intent_picker_bubble());
  EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  EXPECT_EQ(1U, intent_picker_bubble()->GetScrollViewSize());
  auto& app_info = intent_picker_bubble()->app_info_for_testing();
  ASSERT_EQ(1U, app_info.size());
  EXPECT_EQ(app_id, app_info[0].launch_name);
  EXPECT_EQ(app_name, app_info[0].display_name);
  EXPECT_TRUE(intent_picker_bubble()->Close());

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::TestNavigationObserver observer(web_contents);
  ASSERT_TRUE(content::ExecuteScript(
      web_contents,
      "document.getElementById('push_to_new_url_button').click();"));
  observer.WaitForNavigationFinished();
  EXPECT_FALSE(intent_picker_view->GetVisible());
}

// Test that reload a page after app installation will show intent picker.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       ReloadAfterInstall) {
  GURL test_url("https://www.google.com/");
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  WaitForAppService();
  EXPECT_FALSE(intent_picker_view->GetVisible());

  std::string app_name = "test_name";
  auto app_id = AddArcAppWithIntentFilter(app_name, test_url);

  // Reload the page and the intent picker should show up.
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::TestNavigationObserver observer(web_contents);
  chrome::Reload(browser(), WindowOpenDisposition::CURRENT_TAB);
  observer.WaitForNavigationFinished();

  EXPECT_TRUE(intent_picker_view->GetVisible());

  ClickIconToShowBubble();
  EXPECT_EQ(1U, intent_picker_bubble()->GetScrollViewSize());
  auto& app_info = intent_picker_bubble()->app_info_for_testing();
  ASSERT_EQ(1U, app_info.size());
  EXPECT_EQ(app_id, app_info[0].launch_name);
  EXPECT_EQ(app_name, app_info[0].display_name);

  // Launch the default selected app.
  EXPECT_EQ(0U, launched_arc_apps().size());
  intent_picker_bubble()->AcceptDialog();
  ASSERT_NO_FATAL_FAILURE(VerifyArcAppLaunched(app_name, test_url));
}

// Test that stay in chrome works when there is only PWA candidates.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       StayInChromePWAOnly) {
  GURL test_url("https://www.google.com/");
  std::string app_name = "test_name";
  auto app_id = InstallWebApp(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);
  WaitForAppService();
  EXPECT_TRUE(intent_picker_view->GetVisible());
  if (base::FeatureList::IsEnabled(features::kIntentPickerPWAPersistence)) {
    ASSERT_TRUE(intent_picker_bubble());
    EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  } else {
    EXPECT_FALSE(intent_picker_bubble());
    ClickIconToShowBubble();
  }

  ASSERT_NO_FATAL_FAILURE(CheckStayInChrome());
}

// Test that stay in chrome works when there is only ARC candidates.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       StayInChromeARCOnly) {
  GURL test_url("https://www.google.com/");
  std::string app_name = "test_name";
  auto app_id = AddArcAppWithIntentFilter(app_name, test_url);

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       IntentPickerBubbleView::kViewClassName);
  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  waiter.WaitIfNeededAndGet();

  ASSERT_NO_FATAL_FAILURE(CheckStayInChrome());
}

// Test that bubble pops out when there is both PWA and ARC candidates, and
// test launch the PWA.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       ARCAndPWACandidateLaunchPWA) {
  GURL test_url("https://www.google.com/");
  std::string app_name_pwa = "pwa_test_name";
  auto app_id_pwa = InstallWebApp(app_name_pwa, test_url);
  std::string app_name_arc = "arc_test_name";
  auto app_id_arc = AddArcAppWithIntentFilter(app_name_arc, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       IntentPickerBubbleView::kViewClassName);
  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  waiter.WaitIfNeededAndGet();

  EXPECT_TRUE(intent_picker_view->GetVisible());
  ASSERT_TRUE(intent_picker_bubble());
  EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  EXPECT_EQ(2U, intent_picker_bubble()->GetScrollViewSize());
  auto& app_info = intent_picker_bubble()->app_info_for_testing();
  ASSERT_EQ(2U, app_info.size());
  const apps::IntentPickerAppInfo* pwa_app_info;
  const apps::IntentPickerAppInfo* arc_app_info;
  if (app_info[0].launch_name == app_id_pwa) {
    pwa_app_info = &app_info[0];
    arc_app_info = &app_info[1];
  } else {
    pwa_app_info = &app_info[1];
    arc_app_info = &app_info[0];

    // Select the PWA when it is not automatically selected.
    intent_picker_bubble()->PressButtonForTesting(
        /* index= */ 1,
        ui::MouseEvent(ui::ET_MOUSE_RELEASED, gfx::Point(), gfx::Point(),
                       ui::EventTimeForNow(), 0, 0));
  }

  EXPECT_EQ(app_id_pwa, pwa_app_info->launch_name);
  EXPECT_EQ(app_name_pwa, pwa_app_info->display_name);
  EXPECT_EQ(app_id_arc, arc_app_info->launch_name);
  EXPECT_EQ(app_name_arc, arc_app_info->display_name);

  // Check the status of the remember selection checkbox.
  ASSERT_TRUE(remember_selection_checkbox());
  EXPECT_EQ(
      remember_selection_checkbox()->GetEnabled(),
      base::FeatureList::IsEnabled(features::kIntentPickerPWAPersistence));
  EXPECT_FALSE(remember_selection_checkbox()->GetChecked());

  // Launch the app.
  intent_picker_bubble()->AcceptDialog();
  EXPECT_TRUE(VerifyPWALaunched(app_id_pwa));
}

// Test that bubble pops out when there is both PWA and ARC candidates, and
// test launch the ARC app.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       ARCAndPWACandidateLaunchARC) {
  GURL test_url("https://www.google.com/");
  std::string app_name_pwa = "pwa_test_name";
  auto app_id_pwa = InstallWebApp(app_name_pwa, test_url);
  std::string app_name_arc = "arc_test_name";
  auto app_id_arc = AddArcAppWithIntentFilter(app_name_arc, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       IntentPickerBubbleView::kViewClassName);
  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  waiter.WaitIfNeededAndGet();

  EXPECT_TRUE(intent_picker_view->GetVisible());
  ASSERT_TRUE(intent_picker_bubble());
  EXPECT_TRUE(intent_picker_bubble()->GetVisible());
  EXPECT_EQ(2U, intent_picker_bubble()->GetScrollViewSize());
  auto& app_info = intent_picker_bubble()->app_info_for_testing();
  ASSERT_EQ(2U, app_info.size());
  const apps::IntentPickerAppInfo* pwa_app_info;
  const apps::IntentPickerAppInfo* arc_app_info;
  if (app_info[0].launch_name == app_id_pwa) {
    pwa_app_info = &app_info[0];
    arc_app_info = &app_info[1];

    // Select the ARC app when it is not automatically selected.
    intent_picker_bubble()->PressButtonForTesting(
        /* index= */ 1,
        ui::MouseEvent(ui::ET_MOUSE_RELEASED, gfx::Point(), gfx::Point(),
                       ui::EventTimeForNow(), 0, 0));
  } else {
    pwa_app_info = &app_info[1];
    arc_app_info = &app_info[0];
  }

  EXPECT_EQ(app_id_pwa, pwa_app_info->launch_name);
  EXPECT_EQ(app_name_pwa, pwa_app_info->display_name);
  EXPECT_EQ(app_id_arc, arc_app_info->launch_name);
  EXPECT_EQ(app_name_arc, arc_app_info->display_name);

  // Check the status of the remember selection checkbox.
  ASSERT_TRUE(remember_selection_checkbox());
  EXPECT_TRUE(remember_selection_checkbox()->GetEnabled());
  EXPECT_FALSE(remember_selection_checkbox()->GetChecked());

  // Launch the app.
  EXPECT_EQ(0U, launched_arc_apps().size());
  intent_picker_bubble()->AcceptDialog();
  ASSERT_NO_FATAL_FAILURE(VerifyArcAppLaunched(app_name_arc, test_url));
}

// Test that stay in chrome works when there is both PWA and ARC candidates.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       StayInChromeARCAndPWA) {
  GURL test_url("https://www.google.com/");
  std::string app_name_pwa = "pwa_test_name";
  auto app_id_pwa = InstallWebApp(app_name_pwa, test_url);
  std::string app_name_arc = "arc_test_name";
  auto app_id_arc = AddArcAppWithIntentFilter(app_name_arc, test_url);

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       IntentPickerBubbleView::kViewClassName);
  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  waiter.WaitIfNeededAndGet();

  ASSERT_NO_FATAL_FAILURE(CheckStayInChrome());
}

// Test that remember by choice checkbox works for stay in chrome option for ARC
// app.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       RememberStayInChromeARC) {
  GURL test_url("https://www.google.com/");
  std::string app_name = "test_name";
  auto app_id = AddArcAppWithIntentFilter(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       IntentPickerBubbleView::kViewClassName);
  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  waiter.WaitIfNeededAndGet();
  EXPECT_TRUE(intent_picker_view->GetVisible());

  // Check "Remember my choice" and choose "Stay in Chrome".
  ASSERT_TRUE(remember_selection_checkbox());
  ASSERT_TRUE(remember_selection_checkbox()->GetEnabled());
  remember_selection_checkbox()->SetChecked(true);
  ASSERT_TRUE(intent_picker_bubble());
  intent_picker_bubble()->CancelDialog();

  // Navigate to the same site again, and see there will be no bubble
  // pop out, and no app will be launched.
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));
  ui_test_utils::NavigateToURL(&params);
  WaitForAppService();
  EXPECT_TRUE(intent_picker_view->GetVisible());
  EXPECT_FALSE(intent_picker_bubble());
  EXPECT_EQ(BrowserList::GetInstance()->GetLastActive(), browser());
  EXPECT_EQ(launched_arc_apps().size(), 0U);
}

// Test that remember by choice checkbox works for open ARC app option.
IN_PROC_BROWSER_TEST_F(IntentPickerBubbleViewBrowserTestChromeOS,
                       RememberOpenARCApp) {
  GURL test_url("https://www.google.com/");
  std::string app_name = "test_name";
  auto app_id = AddArcAppWithIntentFilter(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       IntentPickerBubbleView::kViewClassName);
  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  waiter.WaitIfNeededAndGet();
  EXPECT_TRUE(intent_picker_view->GetVisible());

  // Check "Remember my choice" and choose "Open App".
  ASSERT_TRUE(remember_selection_checkbox());
  ASSERT_TRUE(remember_selection_checkbox()->GetEnabled());
  remember_selection_checkbox()->SetChecked(true);
  ASSERT_TRUE(intent_picker_bubble());
  intent_picker_bubble()->AcceptDialog();
  WaitForAppService();

  // Navigate to the same site again, and verify the app is automatically
  // launched.
  clear_launched_arc_apps();
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));
  ui_test_utils::NavigateToURL(&params);
  ASSERT_NO_FATAL_FAILURE(VerifyArcAppLaunched(app_name, test_url));
}

class IntentPickerBrowserTestPWAPersistence
    : public IntentPickerBubbleViewBrowserTestChromeOS {
 public:
  IntentPickerBrowserTestPWAPersistence() {
    scoped_feature_list_.InitAndEnableFeature(
        features::kIntentPickerPWAPersistence);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

// Test that remember by choice checkbox works for stay in chrome option for
// PWA.
IN_PROC_BROWSER_TEST_F(IntentPickerBrowserTestPWAPersistence,
                       RememberStayInChromePWA) {
  GURL test_url("https://www.google.com/");
  std::string app_name = "test_name";
  auto app_id = InstallWebApp(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       IntentPickerBubbleView::kViewClassName);
  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  waiter.WaitIfNeededAndGet();
  EXPECT_TRUE(intent_picker_view->GetVisible());

  // Check "Remember my choice" and choose "Stay in Chrome".
  ASSERT_TRUE(remember_selection_checkbox());
  ASSERT_TRUE(remember_selection_checkbox()->GetEnabled());
  remember_selection_checkbox()->SetChecked(true);
  ASSERT_TRUE(intent_picker_bubble());
  intent_picker_bubble()->CancelDialog();

  // Navigate to the same site again, and see there will be no bubble
  // pop out, and no app will be launched.
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));
  ui_test_utils::NavigateToURL(&params);
  WaitForAppService();
  EXPECT_TRUE(intent_picker_view->GetVisible());
  EXPECT_FALSE(intent_picker_bubble());
  EXPECT_EQ(BrowserList::GetInstance()->GetLastActive(), browser());
  EXPECT_EQ(launched_arc_apps().size(), 0U);
}

// Test that remember by choice checkbox works for open PWA option.
IN_PROC_BROWSER_TEST_F(IntentPickerBrowserTestPWAPersistence, RememberOpenPWA) {
  GURL test_url("https://www.google.com/");
  std::string app_name = "test_name";
  auto app_id = InstallWebApp(app_name, test_url);
  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  chrome::NewTab(browser());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  // Navigate from a link.
  NavigateParams params(browser(), test_url,
                        ui::PageTransition::PAGE_TRANSITION_LINK);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       IntentPickerBubbleView::kViewClassName);
  // Navigates and waits for loading to finish.
  ui_test_utils::NavigateToURL(&params);

  waiter.WaitIfNeededAndGet();
  EXPECT_TRUE(intent_picker_view->GetVisible());

  // Check "Remember my choice" and choose "Open App".
  ASSERT_TRUE(remember_selection_checkbox());
  ASSERT_TRUE(remember_selection_checkbox()->GetEnabled());
  remember_selection_checkbox()->SetChecked(true);
  ASSERT_TRUE(intent_picker_bubble());
  intent_picker_bubble()->AcceptDialog();
  EXPECT_TRUE(VerifyPWALaunched(app_id));
  Browser* app_browser = BrowserList::GetInstance()->GetLastActive();
  chrome::CloseWindow(app_browser);
  ui_test_utils::WaitForBrowserToClose(app_browser);

  // Navigate to the same site again, and verify the app is automatically
  // launched.
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));

  NavigateParams params_new(browser(), test_url,
                            ui::PageTransition::PAGE_TRANSITION_LINK);
  ui_test_utils::NavigateToURL(&params_new);

  EXPECT_TRUE(VerifyPWALaunched(app_id));
}

class IntentPickerBrowserTestPrerendering
    : public IntentPickerBrowserTestPWAPersistence {
 public:
  IntentPickerBrowserTestPrerendering()
      : prerender_helper_(base::BindRepeating(
            &IntentPickerBrowserTestPrerendering::web_contents,
            base::Unretained(this))) {}
  ~IntentPickerBrowserTestPrerendering() override = default;

 protected:
  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  PrerenderTestHelper prerender_helper_;
};

// Simulates prerendering an app URL that the user has opted into always
// launching an app window for. In this case, the prerender should be canceled
// and the app shouldn't be opened.
IN_PROC_BROWSER_TEST_F(IntentPickerBrowserTestPrerendering,
                       AppLaunchURLCancelsPrerendering) {
  // Prerendering is currently limited to same-origin pages so we need to start
  // it from an arbitrary page on the same origin, rather than about:blank.
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL kInitialUrl = embedded_test_server()->GetURL("/empty.html");
  const GURL kAppUrl = embedded_test_server()->GetURL("/app");
  const std::string kAppName = "test_name";
  const auto kAppId = InstallWebApp(kAppName, kAppUrl);

  chrome::NewTab(browser());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), kInitialUrl));

  // Setup: navigate to the app URL and persist the "Open App" setting. Then
  // close the app.
  {
    // Navigate from a link.
    NavigateParams params(browser(), kAppUrl,
                          ui::PageTransition::PAGE_TRANSITION_LINK);

    views::NamedWidgetShownWaiter waiter(
        views::test::AnyWidgetTestPasskey{},
        IntentPickerBubbleView::kViewClassName);
    // Navigates and waits for loading to finish.
    ui_test_utils::NavigateToURL(&params);

    waiter.WaitIfNeededAndGet();
    EXPECT_TRUE(GetIntentPickerIcon()->GetVisible());

    // Check "Remember my choice" and choose "Open App".
    ASSERT_TRUE(remember_selection_checkbox());
    ASSERT_TRUE(remember_selection_checkbox()->GetEnabled());
    remember_selection_checkbox()->SetChecked(true);
    ASSERT_TRUE(intent_picker_bubble());
    intent_picker_bubble()->AcceptDialog();
    ASSERT_TRUE(VerifyPWALaunched(kAppId));
    Browser* app_browser = BrowserList::GetInstance()->GetLastActive();
    chrome::CloseWindow(app_browser);
    ui_test_utils::WaitForBrowserToClose(app_browser);
    ASSERT_FALSE(VerifyPWALaunched(kAppId));
  }

  chrome::NewTab(browser());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), kInitialUrl));

  // Trigger a prerender of the app URL.
  PrerenderHostObserver host_observer(*web_contents(), kAppUrl);
  prerender_helper_.AddPrerenderAsync(kAppUrl);
  host_observer.WaitForDestroyed();

  // The app must not have been launched.
  EXPECT_FALSE(VerifyPWALaunched(kAppId));

  // However, a standard user navigation should launch the app as usual.
  NavigateParams params_new(browser(), kAppUrl,
                            ui::PageTransition::PAGE_TRANSITION_LINK);
  ui_test_utils::NavigateToURL(&params_new);
  EXPECT_TRUE(VerifyPWALaunched(kAppId));
}
