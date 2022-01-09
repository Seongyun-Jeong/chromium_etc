// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_shim/web_app_shim_manager_delegate_mac.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/apps/app_service/app_launch_params.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/test/fake_web_app_provider.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/test/web_app_test.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/common/chrome_features.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"

namespace web_app {

namespace {

class MockDelegate : public apps::AppShimManager::Delegate {
 public:
  MockDelegate() {}
  MockDelegate(const MockDelegate&) = delete;
  MockDelegate& operator=(const MockDelegate&) = delete;
  ~MockDelegate() override = default;

  MOCK_METHOD(bool, ShowAppWindows, (Profile*, const std::string&), (override));
  MOCK_METHOD(void,
              CloseAppWindows,
              (Profile*, const std::string&),
              (override));
  MOCK_METHOD(bool, AppIsInstalled, (Profile*, const std::string&), (override));
  MOCK_METHOD(bool,
              AppCanCreateHost,
              (Profile*, const std::string&),
              (override));
  MOCK_METHOD(bool,
              AppUsesRemoteCocoa,
              (Profile*, const std::string&),
              (override));
  MOCK_METHOD(bool,
              AppIsMultiProfile,
              (Profile*, const std::string&),
              (override));
  MOCK_METHOD(void,
              EnableExtension,
              (Profile*, const std::string&, base::OnceCallback<void()>),
              (override));
  MOCK_METHOD(void,
              LaunchApp,
              (Profile*,
               const std::string&,
               const std::vector<base::FilePath>&,
               const std::vector<GURL>&,
               const GURL&,
               chrome::mojom::AppShimLoginItemRestoreState),
              (override));
  MOCK_METHOD(void,
              LaunchShim,
              (Profile*,
               const std::string&,
               bool,
               apps::ShimLaunchedCallback,
               apps::ShimTerminatedCallback),
              (override));
  MOCK_METHOD(bool, HasNonBookmarkAppWindowsOpen, (), (override));
  MOCK_METHOD(std::vector<chrome::mojom::ApplicationDockMenuItemPtr>,
              GetAppShortcutsMenuItemInfos,
              (Profile*, const std::string&),
              (override));
};

class WebAppShimManagerDelegateTest : public WebAppTest {
 public:
  WebAppShimManagerDelegateTest() {}
  WebAppShimManagerDelegateTest(const WebAppShimManagerDelegateTest&) = delete;
  WebAppShimManagerDelegateTest& operator=(
      const WebAppShimManagerDelegateTest&) = delete;
  ~WebAppShimManagerDelegateTest() override = default;

  void SetUp() override {
    WebAppTest::SetUp();

    auto* provider = web_app::FakeWebAppProvider::Get(profile());

    // FakeWebAppProvider should not wait for a test extension system, that is
    // never started, to be ready.
    provider->SkipAwaitingExtensionSystem();
    web_app::test::AwaitStartWebAppProviderAndSubsystems(profile());

    // Install a dummy app
    app_id_ = web_app::test::InstallDummyWebApp(profile(), "WebAppTest",
                                                GURL("https://testpwa.com/"));
  }

 protected:
  apps::AppLaunchParams CreateLaunchParams(
      const std::vector<base::FilePath>& launch_files,
      const absl::optional<GURL>& url_handler_launch_url,
      const absl::optional<GURL>& protocol_handler_launch_url,
      const GURL& override_url) {
    apps::AppLaunchParams params(
        app_id_, apps::mojom::LaunchContainer::kLaunchContainerWindow,
        WindowOpenDisposition::NEW_WINDOW,
        apps::mojom::LaunchSource::kFromCommandLine);

    params.launch_files = launch_files;
    params.url_handler_launch_url = url_handler_launch_url;
    params.protocol_handler_launch_url = protocol_handler_launch_url;
    params.override_url = override_url;
    return params;
  }

  void ValidateOptionalGURL(const absl::optional<GURL>& actual,
                            const absl::optional<GURL>& expected) {
    ASSERT_EQ(actual.has_value(), expected.has_value());
    if (actual.has_value()) {
      EXPECT_EQ(actual.value(), expected.value());
    }
  }

  void ValidateLaunchParams(const apps::AppLaunchParams& actual_results,
                            const apps::AppLaunchParams& expected_results) {
    EXPECT_EQ(actual_results.app_id, expected_results.app_id);
    EXPECT_EQ(actual_results.command_line.GetArgs(),
              expected_results.command_line.GetArgs());
    EXPECT_EQ(actual_results.current_directory,
              expected_results.current_directory);
    EXPECT_EQ(actual_results.launch_source, expected_results.launch_source);
    EXPECT_EQ(actual_results.launch_files, expected_results.launch_files);
    EXPECT_EQ(actual_results.url_handler_launch_url,
              expected_results.url_handler_launch_url);
    EXPECT_EQ(actual_results.override_url, expected_results.override_url);
    ValidateOptionalGURL(actual_results.url_handler_launch_url,
                         expected_results.url_handler_launch_url);
    ValidateOptionalGURL(actual_results.protocol_handler_launch_url,
                         expected_results.protocol_handler_launch_url);
    EXPECT_EQ(actual_results.protocol_handler_launch_url,
              expected_results.protocol_handler_launch_url);
  }

  const AppId& AppId() const { return app_id_; }

 private:
  web_app::AppId app_id_;
};

TEST_F(WebAppShimManagerDelegateTest, LaunchApp) {
  apps::AppLaunchParams expected_results = CreateLaunchParams(
      std::vector<base::FilePath>(), absl::nullopt, absl::nullopt, GURL());

  std::unique_ptr<MockDelegate> delegate = std::make_unique<MockDelegate>();
  WebAppShimManagerDelegate shim_manager(std::move(delegate));

  SetBrowserAppLauncherForTesting(base::BindLambdaForTesting(
      [&](const apps::AppLaunchParams& results) -> content::WebContents* {
        ValidateLaunchParams(results, expected_results);
        return nullptr;
      }));

  shim_manager.LaunchApp(profile(), AppId(), std::vector<base::FilePath>(),
                         std::vector<GURL>(), GURL(),
                         chrome::mojom::AppShimLoginItemRestoreState::kNone);
}

TEST_F(WebAppShimManagerDelegateTest, LaunchApp_ProtocolWebPrefix) {
  GURL protocol_handler_launch_url("web+test://test");

  apps::AppLaunchParams expected_results =
      CreateLaunchParams(std::vector<base::FilePath>(), absl::nullopt,
                         protocol_handler_launch_url, GURL());
  expected_results.launch_source =
      apps::mojom::LaunchSource::kFromProtocolHandler;

  std::unique_ptr<MockDelegate> delegate = std::make_unique<MockDelegate>();
  WebAppShimManagerDelegate shim_manager(std::move(delegate));

  SetBrowserAppLauncherForTesting(base::BindLambdaForTesting(
      [&](const apps::AppLaunchParams& results) -> content::WebContents* {
        ValidateLaunchParams(results, expected_results);
        return nullptr;
      }));

  shim_manager.LaunchApp(profile(), AppId(), std::vector<base::FilePath>(),
                         {protocol_handler_launch_url}, GURL(),
                         chrome::mojom::AppShimLoginItemRestoreState::kNone);
}

TEST_F(WebAppShimManagerDelegateTest, LaunchApp_ProtocolMailTo) {
  GURL protocol_handler_launch_url("mailto://test@test.com");

  apps::AppLaunchParams expected_results =
      CreateLaunchParams(std::vector<base::FilePath>(), absl::nullopt,
                         protocol_handler_launch_url, GURL());
  expected_results.launch_source =
      apps::mojom::LaunchSource::kFromProtocolHandler;

  std::unique_ptr<MockDelegate> delegate = std::make_unique<MockDelegate>();
  WebAppShimManagerDelegate shim_manager(std::move(delegate));

  SetBrowserAppLauncherForTesting(base::BindLambdaForTesting(
      [&](const apps::AppLaunchParams& results) -> content::WebContents* {
        ValidateLaunchParams(results, expected_results);
        return nullptr;
      }));

  shim_manager.LaunchApp(profile(), AppId(), std::vector<base::FilePath>(),
                         {protocol_handler_launch_url}, GURL(),
                         chrome::mojom::AppShimLoginItemRestoreState::kNone);
}

TEST_F(WebAppShimManagerDelegateTest, LaunchApp_ProtocolFile) {
  GURL protocol_handler_launch_url("file:///test_app_path/test_app_file.txt");

  apps::AppLaunchParams expected_results =
      CreateLaunchParams({base::FilePath("/test_app_path/test_app_file.txt")},
                         absl::nullopt, absl::nullopt, GURL());

  std::unique_ptr<MockDelegate> delegate = std::make_unique<MockDelegate>();
  WebAppShimManagerDelegate shim_manager(std::move(delegate));

  SetBrowserAppLauncherForTesting(base::BindLambdaForTesting(
      [&](const apps::AppLaunchParams& results) -> content::WebContents* {
        ValidateLaunchParams(results, expected_results);
        return nullptr;
      }));

  shim_manager.LaunchApp(profile(), AppId(), std::vector<base::FilePath>(),
                         {protocol_handler_launch_url}, GURL(),
                         chrome::mojom::AppShimLoginItemRestoreState::kNone);
}

TEST_F(WebAppShimManagerDelegateTest, LaunchApp_ProtocolDisallowed) {
  GURL protocol_handler_launch_url("https://www.test.com/");

  apps::AppLaunchParams expected_results = CreateLaunchParams(
      std::vector<base::FilePath>(), absl::nullopt, absl::nullopt, GURL());

  std::unique_ptr<MockDelegate> delegate = std::make_unique<MockDelegate>();
  WebAppShimManagerDelegate shim_manager(std::move(delegate));

  SetBrowserAppLauncherForTesting(base::BindLambdaForTesting(
      [&](const apps::AppLaunchParams& results) -> content::WebContents* {
        ValidateLaunchParams(results, expected_results);
        return nullptr;
      }));

  shim_manager.LaunchApp(profile(), AppId(), std::vector<base::FilePath>(),
                         {protocol_handler_launch_url}, GURL(),
                         chrome::mojom::AppShimLoginItemRestoreState::kNone);
}

TEST_F(WebAppShimManagerDelegateTest, LaunchApp_FileFullPath) {
  const base::FilePath::CharType kTestPath[] =
      FILE_PATH_LITERAL("/test_app_path/test_app_file.txt");
  base::FilePath test_path(kTestPath);

  apps::AppLaunchParams expected_results =
      CreateLaunchParams({test_path}, absl::nullopt, absl::nullopt, GURL());

  std::unique_ptr<MockDelegate> delegate = std::make_unique<MockDelegate>();
  WebAppShimManagerDelegate shim_manager(std::move(delegate));

  SetBrowserAppLauncherForTesting(base::BindLambdaForTesting(
      [&](const apps::AppLaunchParams& results) -> content::WebContents* {
        ValidateLaunchParams(results, expected_results);
        return nullptr;
      }));

  shim_manager.LaunchApp(profile(), AppId(), {test_path}, std::vector<GURL>(),
                         GURL(),
                         chrome::mojom::AppShimLoginItemRestoreState::kNone);
}

TEST_F(WebAppShimManagerDelegateTest, LaunchApp_FileRelativePath) {
  const base::FilePath::CharType kTestPath[] =
      FILE_PATH_LITERAL("test_app_path/test_app_file.txt");
  base::FilePath test_path(kTestPath);

  apps::AppLaunchParams expected_results =
      CreateLaunchParams({test_path}, absl::nullopt, absl::nullopt, GURL());

  std::unique_ptr<MockDelegate> delegate = std::make_unique<MockDelegate>();
  WebAppShimManagerDelegate shim_manager(std::move(delegate));

  SetBrowserAppLauncherForTesting(base::BindLambdaForTesting(
      [&](const apps::AppLaunchParams& results) -> content::WebContents* {
        ValidateLaunchParams(results, expected_results);
        return nullptr;
      }));

  shim_manager.LaunchApp(profile(), AppId(), {test_path}, std::vector<GURL>(),
                         GURL(),
                         chrome::mojom::AppShimLoginItemRestoreState::kNone);
}

TEST_F(WebAppShimManagerDelegateTest, LaunchApp_ProtocolAndFileHandlerMixed) {
  GURL protocol_handler_launch_url("web+test://test");
  const base::FilePath::CharType kTestPath[] =
      FILE_PATH_LITERAL("test_app_path/test_app_file.txt");
  base::FilePath test_path(kTestPath);

  apps::AppLaunchParams expected_results = CreateLaunchParams(
      {test_path}, absl::nullopt, protocol_handler_launch_url, GURL());
  expected_results.launch_source =
      apps::mojom::LaunchSource::kFromProtocolHandler;

  std::unique_ptr<MockDelegate> delegate = std::make_unique<MockDelegate>();
  WebAppShimManagerDelegate shim_manager(std::move(delegate));

  SetBrowserAppLauncherForTesting(base::BindLambdaForTesting(
      [&](const apps::AppLaunchParams& results) -> content::WebContents* {
        ValidateLaunchParams(results, expected_results);
        return nullptr;
      }));

  shim_manager.LaunchApp(profile(), AppId(), {test_path},
                         {protocol_handler_launch_url}, GURL(),
                         chrome::mojom::AppShimLoginItemRestoreState::kNone);
}

TEST_F(WebAppShimManagerDelegateTest,
       LaunchApp_ProtocolWithFileAndFileHandlerMixed) {
  GURL protocol_handler_launch_url("web+test://test");
  GURL protocol_handler_file_url("file:///test_app_path/test_app_file.txt");
  const base::FilePath::CharType kTestPath[] =
      FILE_PATH_LITERAL("test_app_path/test_app_file.txt");
  base::FilePath test_path(kTestPath);

  apps::AppLaunchParams expected_results = CreateLaunchParams(
      {test_path, base::FilePath("/test_app_path/test_app_file.txt")},
      absl::nullopt, protocol_handler_launch_url, GURL());
  expected_results.launch_source =
      apps::mojom::LaunchSource::kFromProtocolHandler;

  std::unique_ptr<MockDelegate> delegate = std::make_unique<MockDelegate>();
  WebAppShimManagerDelegate shim_manager(std::move(delegate));

  SetBrowserAppLauncherForTesting(base::BindLambdaForTesting(
      [&](const apps::AppLaunchParams& results) -> content::WebContents* {
        ValidateLaunchParams(results, expected_results);
        return nullptr;
      }));

  shim_manager.LaunchApp(
      profile(), AppId(), {test_path},
      {protocol_handler_launch_url, protocol_handler_file_url}, GURL(),
      chrome::mojom::AppShimLoginItemRestoreState::kNone);
}

TEST_F(WebAppShimManagerDelegateTest, LaunchApp_OverrideUrl) {
  GURL override_url("index.html");
  apps::AppLaunchParams expected_results =
      CreateLaunchParams(std::vector<base::FilePath>(), absl::nullopt,
                         absl::nullopt, override_url);

  std::unique_ptr<MockDelegate> delegate = std::make_unique<MockDelegate>();
  WebAppShimManagerDelegate shim_manager(std::move(delegate));

  SetBrowserAppLauncherForTesting(base::BindLambdaForTesting(
      [&](const apps::AppLaunchParams& results) -> content::WebContents* {
        ValidateLaunchParams(results, expected_results);
        return nullptr;
      }));

  shim_manager.LaunchApp(profile(), AppId(), std::vector<base::FilePath>(),
                         std::vector<GURL>(), override_url,
                         chrome::mojom::AppShimLoginItemRestoreState::kNone);
}

TEST_F(WebAppShimManagerDelegateTest, GetAppShortcutsMenuItemInfos) {
  std::unique_ptr<MockDelegate> delegate = std::make_unique<MockDelegate>();
  WebAppShimManagerDelegate shim_manager(std::move(delegate));

  // Validate empty array when feature flag is off.
  {
    base::test::ScopedFeatureList feature_list;
    feature_list.InitAndDisableFeature(
        features::kDesktopPWAsAppIconShortcutsMenuUI);
    auto shortcut_menu_items =
        shim_manager.GetAppShortcutsMenuItemInfos(profile(), AppId());
    EXPECT_EQ(0U, shortcut_menu_items.size());
  }

  // Validate empty array when feature flag is on, and app does not have
  // shortcut menus declared in the manifest.
  {
    base::test::ScopedFeatureList feature_list;
    feature_list.InitAndEnableFeature(
        features::kDesktopPWAsAppIconShortcutsMenuUI);
    auto shortcut_menu_items =
        shim_manager.GetAppShortcutsMenuItemInfos(profile(), AppId());
    EXPECT_EQ(0U, shortcut_menu_items.size());
  }

  // Validate array when feature flag is on, and app does declare shortcut menus
  // in the manifest.
  {
    base::test::ScopedFeatureList feature_list;
    feature_list.InitAndEnableFeature(
        features::kDesktopPWAsAppIconShortcutsMenuUI);

    // Install a dummy app with shortcut menu items
    auto web_app_info = std::make_unique<WebApplicationInfo>();
    WebAppShortcutsMenuItemInfo shortcut_info1;
    WebAppShortcutsMenuItemInfo shortcut_info2;
    WebAppShortcutsMenuItemInfo shortcut_info3;

    web_app_info->start_url = GURL("https://mytestpwa.com/");
    web_app_info->title = u"WebAppTestWithShortcutMenuItems";
    web_app_info->scope = web_app_info->start_url;
    web_app_info->description = web_app_info->title;
    web_app_info->user_display_mode = blink::mojom::DisplayMode::kStandalone;

    shortcut_info1.name = u"shortcut_info1";
    shortcut_info1.url = GURL(".");
    web_app_info->shortcuts_menu_item_infos.push_back(shortcut_info1);

    shortcut_info2.name = u"shortcut_info2";
    shortcut_info2.url = GURL("/settings");
    web_app_info->shortcuts_menu_item_infos.push_back(shortcut_info2);

    shortcut_info3.name = u"shortcut_info3";
    shortcut_info3.url = GURL("https://anothersite.com");
    web_app_info->shortcuts_menu_item_infos.push_back(shortcut_info3);

    web_app::AppId shortcut_app_id =
        web_app::test::InstallWebApp(profile(), std::move(web_app_info));
    auto shortcut_menu_items =
        shim_manager.GetAppShortcutsMenuItemInfos(profile(), shortcut_app_id);

    ASSERT_EQ(3U, shortcut_menu_items.size());
    EXPECT_EQ(shortcut_info1.name, shortcut_menu_items[0]->name);
    EXPECT_EQ(shortcut_info1.url, shortcut_menu_items[0]->url);
    EXPECT_EQ(shortcut_info2.name, shortcut_menu_items[1]->name);
    EXPECT_EQ(shortcut_info2.url, shortcut_menu_items[1]->url);
    EXPECT_EQ(shortcut_info3.name, shortcut_menu_items[2]->name);
    EXPECT_EQ(shortcut_info3.url, shortcut_menu_items[2]->url);
  }
}

}  // namespace

}  // namespace web_app
