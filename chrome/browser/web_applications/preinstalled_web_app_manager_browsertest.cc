// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_refptr.h"
#include "chrome/browser/web_applications/preinstalled_web_app_manager.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/test/bind.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/ui/web_applications/test/ssl_test_utils.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/web_applications/policy/web_app_policy_manager.h"
#include "chrome/browser/web_applications/preinstalled_app_install_features.h"
#include "chrome/browser/web_applications/preinstalled_web_apps/preinstalled_web_apps.h"
#include "chrome/browser/web_applications/test/fake_os_integration_manager.h"
#include "chrome/browser/web_applications/test/test_file_utils.h"
#include "chrome/browser/web_applications/test/web_app_icon_test_utils.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_application_info.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "components/services/app_service/public/cpp/app_registry_cache.h"
#include "components/services/app_service/public/cpp/app_update.h"
#include "components/services/app_service/public/mojom/types.mojom.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_launcher.h"
#include "content/public/test/url_loader_interceptor.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "net/ssl/ssl_info.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/devices/device_data_manager_test_api.h"
#include "ui/events/devices/touchscreen_device.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/public/cpp/test/app_list_test_api.h"
#include "chrome/browser/ui/app_list/app_list_client_impl.h"
#include "chrome/browser/ui/app_list/app_list_syncable_service.h"
#include "chrome/browser/ui/app_list/app_list_syncable_service_factory.h"
#endif

namespace web_app {

namespace {

constexpr char kBaseDataDir[] = "chrome/test/data/banners";

// start_url in manifest.json matches navigation url for the simple
// manifest_test_page.html.
constexpr char kSimpleManifestStartUrl[] =
    "https://example.org/manifest_test_page.html";

constexpr char kNoManifestTestPageStartUrl[] =
    "https://example.org/no_manifest_test_page.html";

// Performs blocking IO operations.
base::FilePath GetDataFilePath(const base::FilePath& relative_path,
                               bool* path_exists) {
  base::ScopedAllowBlockingForTesting allow_io;

  base::FilePath root_path;
  CHECK(base::PathService::Get(base::DIR_SOURCE_ROOT, &root_path));
  base::FilePath path = root_path.Append(relative_path);
  *path_exists = base::PathExists(path);
  return path;
}

#if defined(OS_CHROMEOS)
void ExpectInitialManifestFieldsFromBasicWebApp(
    const WebAppIconManager& icon_manager,
    const WebApp* web_app,
    const GURL& expect_start_url,
    const GURL& expect_scope) {
  // Manifest fields:
  EXPECT_EQ(web_app->name(), "Basic web app");
  EXPECT_EQ(web_app->start_url().spec(), expect_start_url);
  EXPECT_EQ(web_app->scope().spec(), expect_scope);
  EXPECT_EQ(web_app->display_mode(), DisplayMode::kStandalone);
  EXPECT_FALSE(web_app->theme_color().has_value());

  EXPECT_FALSE(web_app->sync_fallback_data().theme_color.has_value());
  EXPECT_EQ("Basic web app", web_app->sync_fallback_data().name);
  EXPECT_EQ(expect_scope.spec(), web_app->sync_fallback_data().scope);

  EXPECT_EQ(2u, web_app->sync_fallback_data().icon_infos.size());

  EXPECT_EQ(expect_start_url.Resolve("basic-48.png"),
            web_app->sync_fallback_data().icon_infos[0].url);
  EXPECT_EQ(48, web_app->sync_fallback_data().icon_infos[0].square_size_px);
  EXPECT_EQ(apps::IconInfo::Purpose::kAny,
            web_app->sync_fallback_data().icon_infos[0].purpose);

  EXPECT_EQ(expect_start_url.Resolve("basic-192.png"),
            web_app->sync_fallback_data().icon_infos[1].url);
  EXPECT_EQ(192, web_app->sync_fallback_data().icon_infos[1].square_size_px);
  EXPECT_EQ(apps::IconInfo::Purpose::kAny,
            web_app->sync_fallback_data().icon_infos[1].purpose);

  // Manifest Resources: This is chrome/test/data/web_apps/basic-192.png
  EXPECT_EQ(IconManagerReadAppIconPixel(icon_manager, web_app->app_id(),
                                        /*size=*/192),
            SK_ColorBLACK);

  // User preferences:
  EXPECT_EQ(web_app->user_display_mode(), DisplayMode::kStandalone);
}
#endif  // defined(OS_CHROMEOS)

}  // namespace

class PreinstalledWebAppManagerBrowserTest
    : virtual public InProcessBrowserTest {
 public:
  PreinstalledWebAppManagerBrowserTest() {
    feature_list_.InitAndEnableFeature(features::kRecordWebAppDebugInfo);
    PreinstalledWebAppManager::SkipStartupForTesting();
  }

  // InProcessBrowserTest:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    web_app::test::WaitUntilReady(
        web_app::WebAppProvider::GetForTest(browser()->profile()));
  }
  void TearDownOnMainThread() override {
    ResetInterceptor();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void InitUrlLoaderInterceptor() {
    // We use a URLLoaderInterceptor, rather than the EmbeddedTestServer, since
    // a stable app_id across tests requires stable origin, whereas
    // EmbeddedTestServer serves content on a random port.
    url_loader_interceptor_ =
        std::make_unique<content::URLLoaderInterceptor>(base::BindRepeating(
            [](content::URLLoaderInterceptor::RequestParams* params) -> bool {
              std::string relative_request = base::StrCat(
                  {kBaseDataDir, params->url_request.url.path_piece()});
              base::FilePath relative_path =
                  base::FilePath().AppendASCII(relative_request);

              bool path_exists = false;
              base::FilePath path =
                  GetDataFilePath(relative_path, &path_exists);
              if (!path_exists)
                return /*intercepted=*/false;

              // Provide fake SSLInfo to avoid NOT_FROM_SECURE_ORIGIN error in
              // InstallableManager::GetData().
              net::SSLInfo ssl_info;
              CreateFakeSslInfoCertificate(&ssl_info);

              content::URLLoaderInterceptor::WriteResponse(
                  path, params->client.get(), /*headers=*/nullptr, ssl_info);

              return /*intercepted=*/true;
            }));
  }

  GURL GetAppUrl() const {
    return embedded_test_server()->GetURL("/web_apps/basic.html");
  }

  const WebAppRegistrar& registrar() {
    return WebAppProvider::GetForTest(browser()->profile())->registrar();
  }

  const WebAppIconManager& icon_manager() {
    return WebAppProvider::GetForTest(browser()->profile())->icon_manager();
  }

  const PreinstalledWebAppManager& manager() {
    return WebAppProvider::GetForTest(profile())
        ->preinstalled_web_app_manager();
  }

  void SyncEmptyConfigs() {
    std::vector<base::Value> app_configs;
    PreinstalledWebAppManager::SetConfigsForTesting(&app_configs);

    base::RunLoop run_loop;
    WebAppProvider::GetForTest(profile())
        ->preinstalled_web_app_manager()
        .LoadAndSynchronizeForTesting(base::BindLambdaForTesting(
            [&](std::map<GURL, ExternallyManagedAppManager::InstallResult>
                    install_results,
                std::map<GURL, bool> uninstall_results) {
              EXPECT_EQ(install_results.size(), 0u);
              EXPECT_EQ(uninstall_results.size(), 0u);
              run_loop.Quit();
            }));
    run_loop.Run();

    PreinstalledWebAppManager::SetConfigsForTesting(nullptr);
  }

  // Mocks "icon.png" as chrome/test/data/web_apps/blue-192.png.
  absl::optional<InstallResultCode> SyncPreinstalledAppConfig(
      const GURL& install_url,
      base::StringPiece app_config_string) {
    base::FilePath test_config_dir(FILE_PATH_LITERAL("test_dir"));
    PreinstalledWebAppManager::SetConfigDirForTesting(&test_config_dir);

    base::FilePath source_root_dir;
    CHECK(base::PathService::Get(base::DIR_SOURCE_ROOT, &source_root_dir));
    base::FilePath test_icon_path =
        source_root_dir.Append(GetChromeTestDataDir())
            .AppendASCII("web_apps/blue-192.png");
    scoped_refptr<TestFileUtils> file_utils = TestFileUtils::Create(
        {{base::FilePath(FILE_PATH_LITERAL("test_dir/icon.png")),
          test_icon_path}});
    PreinstalledWebAppManager::SetFileUtilsForTesting(file_utils.get());

    std::vector<base::Value> app_configs;
    base::JSONReader::ValueWithError json_parse_result =
        base::JSONReader::ReadAndReturnValueWithError(app_config_string);
    EXPECT_TRUE(json_parse_result.value)
        << "JSON parse error: " << json_parse_result.error_message;
    if (!json_parse_result.value)
      return absl::nullopt;
    app_configs.push_back(*std::move(json_parse_result.value));
    PreinstalledWebAppManager::SetConfigsForTesting(&app_configs);

    absl::optional<InstallResultCode> code;
    base::RunLoop sync_run_loop;
    WebAppProvider::GetForTest(profile())
        ->preinstalled_web_app_manager()
        .LoadAndSynchronizeForTesting(base::BindLambdaForTesting(
            [&](std::map<GURL, ExternallyManagedAppManager::InstallResult>
                    install_results,
                std::map<GURL, bool> uninstall_results) {
              auto it = install_results.find(install_url);
              if (it != install_results.end())
                code = it->second.code;
              sync_run_loop.Quit();
            }));
    sync_run_loop.Run();

    PreinstalledWebAppManager::SetConfigDirForTesting(nullptr);
    PreinstalledWebAppManager::SetFileUtilsForTesting(nullptr);
    PreinstalledWebAppManager::SetConfigsForTesting(nullptr);

    return code;
  }

  ~PreinstalledWebAppManagerBrowserTest() override = default;

  Profile* profile() { return browser()->profile(); }

 protected:
  void ResetInterceptor() { url_loader_interceptor_.reset(); }

 private:
  std::unique_ptr<content::URLLoaderInterceptor> url_loader_interceptor_;
  OsIntegrationManager::ScopedSuppressForTesting os_hooks_suppress_;
  base::test::ScopedFeatureList feature_list_;
};

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       LaunchQueryParamsBasic) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL start_url = embedded_test_server()->GetURL("/web_apps/basic.html");
  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt, start_url);
  EXPECT_FALSE(registrar().IsInstalled(app_id));

  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "user_type": ["unmanaged"],
        "launch_query_params": "test_launch_params"
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate, {start_url.spec()}, nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(start_url, app_config),
            InstallResultCode::kSuccessNewInstall);

  EXPECT_TRUE(registrar().IsInstalled(app_id));
  EXPECT_EQ(registrar().GetAppStartUrl(app_id).spec(), start_url);

  GURL launch_url =
      embedded_test_server()->GetURL("/web_apps/basic.html?test_launch_params");
  EXPECT_EQ(registrar().GetAppLaunchUrl(app_id), launch_url);

  Browser* app_browser = LaunchWebAppBrowserAndWait(profile(), app_id);
  EXPECT_EQ(
      app_browser->tab_strip_model()->GetActiveWebContents()->GetVisibleURL(),
      launch_url);
}

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       LaunchQueryParamsDuplicate) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL install_url = embedded_test_server()->GetURL(
      "/web_apps/query_params_in_start_url.html");
  GURL start_url = embedded_test_server()->GetURL(
      "/web_apps/query_params_in_start_url.html?query_params=in&start=url");
  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt, start_url);
  EXPECT_FALSE(registrar().IsInstalled(app_id));

  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "user_type": ["unmanaged"],
        "launch_query_params": "query_params=in"
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate, {install_url.spec()}, nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(install_url, app_config),
            InstallResultCode::kSuccessNewInstall);

  EXPECT_TRUE(registrar().IsInstalled(app_id));
  EXPECT_EQ(registrar().GetAppStartUrl(app_id).spec(), start_url);

  // We should not duplicate the query param if start_url already has it.
  EXPECT_EQ(registrar().GetAppLaunchUrl(app_id), start_url);

  Browser* app_browser = LaunchWebAppBrowserAndWait(profile(), app_id);
  EXPECT_EQ(
      app_browser->tab_strip_model()->GetActiveWebContents()->GetVisibleURL(),
      start_url);
}

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       LaunchQueryParamsMultiple) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL start_url = embedded_test_server()->GetURL("/web_apps/basic.html");
  GURL launch_url = embedded_test_server()->GetURL(
      "/web_apps/basic.html?more=than&one=query&param");
  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt, start_url);
  EXPECT_FALSE(registrar().IsInstalled(app_id));

  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "user_type": ["unmanaged"],
        "launch_query_params": "more=than&one=query&param"
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate, {start_url.spec()}, nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(start_url, app_config),
            InstallResultCode::kSuccessNewInstall);

  EXPECT_TRUE(registrar().IsInstalled(app_id));
  EXPECT_EQ(registrar().GetAppStartUrl(app_id).spec(), start_url);
  EXPECT_EQ(registrar().GetAppLaunchUrl(app_id), launch_url);

  Browser* app_browser = LaunchWebAppBrowserAndWait(profile(), app_id);
  EXPECT_EQ(
      app_browser->tab_strip_model()->GetActiveWebContents()->GetVisibleURL(),
      launch_url);
}

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       LaunchQueryParamsComplex) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL install_url = embedded_test_server()->GetURL(
      "/web_apps/query_params_in_start_url.html");
  GURL start_url = embedded_test_server()->GetURL(
      "/web_apps/query_params_in_start_url.html?query_params=in&start=url");
  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt, start_url);
  EXPECT_FALSE(registrar().IsInstalled(app_id));

  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "user_type": ["unmanaged"],
        "launch_query_params": "!@#$$%^*&)("
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate, {install_url.spec()}, nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(install_url, app_config),
            InstallResultCode::kSuccessNewInstall);

  EXPECT_TRUE(registrar().IsInstalled(app_id));
  EXPECT_EQ(registrar().GetAppStartUrl(app_id).spec(), start_url);

  GURL launch_url = embedded_test_server()->GetURL(
      "/web_apps/"
      "query_params_in_start_url.html?query_params=in&start=url&!@%23$%^*&)(");
  EXPECT_EQ(registrar().GetAppLaunchUrl(app_id), launch_url);

  Browser* app_browser = LaunchWebAppBrowserAndWait(profile(), app_id);
  EXPECT_EQ(
      app_browser->tab_strip_model()->GetActiveWebContents()->GetVisibleURL(),
      launch_url);
}

class PreinstalledWebAppManagerExtensionBrowserTest
    : public extensions::ExtensionBrowserTest,
      public PreinstalledWebAppManagerBrowserTest {
 public:
  PreinstalledWebAppManagerExtensionBrowserTest() = default;
  ~PreinstalledWebAppManagerExtensionBrowserTest() override = default;

  void SetUpOnMainThread() override {
    extensions::ExtensionBrowserTest::SetUpOnMainThread();
    web_app::test::WaitUntilReady(
        web_app::WebAppProvider::GetForTest(browser()->profile()));
  }
  void TearDownOnMainThread() override {
    ResetInterceptor();
    extensions::ExtensionBrowserTest::TearDownOnMainThread();
  }
};

#if !BUILDFLAG(IS_CHROMEOS_LACROS)
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerExtensionBrowserTest,
                       UninstallAndReplace) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  ASSERT_TRUE(embedded_test_server()->Start());

  // Install Chrome app to be replaced.
  const char kChromeAppDirectory[] = "app";
  const char kChromeAppName[] = "App Test";
  const extensions::Extension* app = InstallExtensionWithSourceAndFlags(
      test_data_dir_.AppendASCII(kChromeAppDirectory), 1,
      extensions::mojom::ManifestLocation::kInternal,
      extensions::Extension::NO_FLAGS);
  EXPECT_EQ(app->name(), kChromeAppName);

  // Start listening for Chrome app uninstall.
  extensions::TestExtensionRegistryObserver uninstall_observer(
      extensions::ExtensionRegistry::Get(browser()->profile()));

  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "user_type": ["unmanaged"],
        "uninstall_and_replace": ["$2"]
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate, {GetAppUrl().spec(), app->id()}, nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(GetAppUrl(), app_config),
            InstallResultCode::kSuccessNewInstall);

  // Chrome app should get uninstalled.
  scoped_refptr<const extensions::Extension> uninstalled_app =
      uninstall_observer.WaitForExtensionUninstalled();
  EXPECT_EQ(app, uninstalled_app.get());
}
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       PreinstalledAppsPrefInstall) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  ASSERT_TRUE(embedded_test_server()->Start());
  profile()->GetPrefs()->SetString(prefs::kPreinstalledApps, "install");

  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "user_type": ["unmanaged"]
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate, {GetAppUrl().spec()}, nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(GetAppUrl(), app_config),
            InstallResultCode::kSuccessNewInstall);
}

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       PreinstalledAppsPrefNoinstall) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  ASSERT_TRUE(embedded_test_server()->Start());
  profile()->GetPrefs()->SetString(prefs::kPreinstalledApps, "noinstall");

  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "user_type": ["unmanaged"]
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate, {GetAppUrl().spec()}, nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(GetAppUrl(), app_config), absl::nullopt);
}

const char kOnlyIfPreviouslyPreinstalled_PreviousConfig[] = R"({
  "app_url": "$1",
  "launch_container": "window",
  "user_type": ["unmanaged"]
})";
const char kOnlyIfPreviouslyPreinstalled_NextConfig[] = R"({
  "app_url": "$1",
  "launch_container": "window",
  "user_type": ["unmanaged"],
  "only_if_previously_preinstalled": true
})";

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       PRE_OnlyIfPreviouslyPreinstalled_AppPreserved) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  InitUrlLoaderInterceptor();

  std::string prev_app_config = base::ReplaceStringPlaceholders(
      kOnlyIfPreviouslyPreinstalled_PreviousConfig, {kSimpleManifestStartUrl},
      nullptr);

  // The user had the app installed.
  EXPECT_EQ(
      SyncPreinstalledAppConfig(GURL{kSimpleManifestStartUrl}, prev_app_config),
      InstallResultCode::kSuccessNewInstall);

  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt,
                               GURL{kSimpleManifestStartUrl});
  EXPECT_TRUE(registrar().IsInstalled(app_id));
}

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       OnlyIfPreviouslyPreinstalled_AppPreserved) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  InitUrlLoaderInterceptor();

  std::string next_app_config =
      base::ReplaceStringPlaceholders(kOnlyIfPreviouslyPreinstalled_NextConfig,
                                      {kSimpleManifestStartUrl}, nullptr);

  // The user still has the app.
  EXPECT_EQ(
      SyncPreinstalledAppConfig(GURL{kSimpleManifestStartUrl}, next_app_config),
      InstallResultCode::kSuccessAlreadyInstalled);

  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt,
                               GURL{kSimpleManifestStartUrl});
  EXPECT_TRUE(registrar().IsInstalled(app_id));
}

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       PRE_OnlyIfPreviouslyPreinstalled_NoAppPreinstalled) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  InitUrlLoaderInterceptor();

  std::string prev_app_config = base::ReplaceStringPlaceholders(
      kOnlyIfPreviouslyPreinstalled_PreviousConfig,
      {kNoManifestTestPageStartUrl}, nullptr);

  EXPECT_EQ(SyncPreinstalledAppConfig(GURL{kNoManifestTestPageStartUrl},
                                      prev_app_config),
            InstallResultCode::kNotValidManifestForWebApp);

  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt,
                               GURL{kNoManifestTestPageStartUrl});
  EXPECT_FALSE(registrar().IsInstalled(app_id));
}

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       OnlyIfPreviouslyPreinstalled_NoAppPreinstalled) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  InitUrlLoaderInterceptor();

  std::string next_app_config =
      base::ReplaceStringPlaceholders(kOnlyIfPreviouslyPreinstalled_NextConfig,
                                      {kNoManifestTestPageStartUrl}, nullptr);

  // The user has no the app.
  EXPECT_EQ(SyncPreinstalledAppConfig(GURL{kNoManifestTestPageStartUrl},
                                      next_app_config),
            absl::nullopt);

  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt,
                               GURL{kNoManifestTestPageStartUrl});
  EXPECT_FALSE(registrar().IsInstalled(app_id));
}

// The offline manifest JSON config functionality is only available on Chrome
// OS.
#if defined(OS_CHROMEOS)

// Check that offline fallback installs work offline.
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       OfflineFallbackManifestSiteOffline) {
  constexpr char kAppInstallUrl[] = "https://offline-site.com/install.html";
  constexpr char kAppName[] = "Offline app name";
  constexpr char kAppStartUrl[] = "https://offline-site.com/start.html";
  constexpr char kAppScope[] = "https://offline-site.com/";

  AppId app_id =
      GenerateAppId(/*manifest_id=*/absl::nullopt, GURL(kAppStartUrl));
  EXPECT_FALSE(registrar().IsInstalled(app_id));

  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "user_type": ["unmanaged"],
        "offline_manifest": {
          "name": "$2",
          "start_url": "$3",
          "scope": "$4",
          "display": "minimal-ui",
          "theme_color_argb_hex": "AABBCCDD",
          "icon_any_pngs": ["icon.png"]
        }
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate, {kAppInstallUrl, kAppName, kAppStartUrl, kAppScope},
      nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(GURL(kAppInstallUrl), app_config),
            InstallResultCode::kSuccessOfflineFallbackInstall);

  EXPECT_TRUE(registrar().IsInstalled(app_id));
  EXPECT_EQ(registrar().GetAppShortName(app_id), kAppName);
  EXPECT_EQ(registrar().GetAppStartUrl(app_id).spec(), kAppStartUrl);
  EXPECT_EQ(registrar().GetAppScope(app_id).spec(), kAppScope);
  EXPECT_EQ(registrar().GetAppUserDisplayMode(app_id),
            DisplayMode::kStandalone);
  EXPECT_EQ(registrar().GetAppDisplayMode(app_id), DisplayMode::kMinimalUi);
  // theme_color must be installed opaque.
  EXPECT_EQ(registrar().GetAppThemeColor(app_id),
            SkColorSetARGB(0xFF, 0xBB, 0xCC, 0xDD));
  EXPECT_EQ(IconManagerReadAppIconPixel(icon_manager(), app_id, /*size=*/192),
            SK_ColorBLUE);
}

// Check that offline fallback installs attempt fetching the install_url.
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       OfflineFallbackManifestSiteOnline) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // This install_url serves a manifest with different values to what we specify
  // in the offline_manifest. Check that it gets used instead of the
  // offline_manifest.
  GURL install_url = embedded_test_server()->GetURL("/web_apps/basic.html");
  GURL offline_start_url = embedded_test_server()->GetURL(
      "/web_apps/offline-only-start-url-that-does-not-exist.html");
  GURL scope = embedded_test_server()->GetURL("/web_apps/");

  AppId offline_app_id =
      GenerateAppId(/*manifest_id=*/absl::nullopt, offline_start_url);
  EXPECT_FALSE(registrar().IsInstalled(offline_app_id));

  constexpr char kAppConfigTemplate[] =
      R"({
          "app_url": "$1",
          "launch_container": "window",
          "user_type": ["unmanaged"],
          "offline_manifest": {
            "name": "Offline only app name",
            "start_url": "$2",
            "scope": "$3",
            "display": "minimal-ui",
            "theme_color_argb_hex": "AABBCCDD",
            "icon_any_pngs": ["icon.png"]
          }
        })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate,
      {install_url.spec(), offline_start_url.spec(), scope.spec()}, nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(install_url, app_config),
            InstallResultCode::kSuccessNewInstall);

  EXPECT_FALSE(registrar().IsInstalled(offline_app_id));

  // basic.html's manifest start_url is basic.html.
  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt, install_url);
  EXPECT_TRUE(registrar().IsInstalled(app_id));
  EXPECT_EQ(registrar().GetAppShortName(app_id), "Basic web app");
  EXPECT_EQ(registrar().GetAppStartUrl(app_id).spec(), install_url);
  EXPECT_EQ(registrar().GetAppScope(app_id).spec(), scope);
  EXPECT_EQ(registrar().GetAppUserDisplayMode(app_id),
            DisplayMode::kStandalone);
  EXPECT_EQ(registrar().GetAppDisplayMode(app_id), DisplayMode::kStandalone);
}

// Check that offline only installs work offline.
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       OfflineOnlyManifestSiteOffline) {
  constexpr char kAppInstallUrl[] = "https://offline-site.com/install.html";
  constexpr char kAppName[] = "Offline app name";
  constexpr char kAppStartUrl[] = "https://offline-site.com/start.html";
  constexpr char kAppScope[] = "https://offline-site.com/";

  AppId app_id =
      GenerateAppId(/*manifest_id=*/absl::nullopt, GURL(kAppStartUrl));
  EXPECT_FALSE(registrar().IsInstalled(app_id));

  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "user_type": ["unmanaged"],
        "only_use_offline_manifest": true,
        "offline_manifest": {
          "name": "$2",
          "start_url": "$3",
          "scope": "$4",
          "display": "minimal-ui",
          "theme_color_argb_hex": "AABBCCDD",
          "icon_any_pngs": ["icon.png"]
        }
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate, {kAppInstallUrl, kAppName, kAppStartUrl, kAppScope},
      nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(GURL(kAppInstallUrl), app_config),
            InstallResultCode::kSuccessOfflineOnlyInstall);

  EXPECT_TRUE(registrar().IsInstalled(app_id));
  EXPECT_EQ(registrar().GetAppShortName(app_id), kAppName);
  EXPECT_EQ(registrar().GetAppStartUrl(app_id).spec(), kAppStartUrl);
  EXPECT_EQ(registrar().GetAppScope(app_id).spec(), kAppScope);
  EXPECT_EQ(registrar().GetAppUserDisplayMode(app_id),
            DisplayMode::kStandalone);
  EXPECT_EQ(registrar().GetAppDisplayMode(app_id), DisplayMode::kMinimalUi);
  // theme_color must be installed opaque.
  EXPECT_EQ(registrar().GetAppThemeColor(app_id),
            SkColorSetARGB(0xFF, 0xBB, 0xCC, 0xDD));
  EXPECT_EQ(IconManagerReadAppIconPixel(icon_manager(), app_id, /*size=*/192),
            SK_ColorBLUE);
}

// Check that offline only installs don't fetch from the install_url.
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       OfflineOnlyManifestSiteOnline) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // This install_url serves a manifest with different values to what we specify
  // in the offline_manifest. Check that it doesn't get used.
  GURL install_url = GetAppUrl();
  const char kAppName[] = "Offline only app name";
  GURL start_url = embedded_test_server()->GetURL(
      "/web_apps/offline-only-start-url-that-does-not-exist.html");
  GURL scope = embedded_test_server()->GetURL("/web_apps/");

  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt, start_url);
  EXPECT_FALSE(registrar().IsInstalled(app_id));

  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "user_type": ["unmanaged"],
        "only_use_offline_manifest": true,
        "offline_manifest": {
          "name": "$2",
          "start_url": "$3",
          "scope": "$4",
          "display": "minimal-ui",
          "theme_color_argb_hex": "AABBCCDD",
          "icon_any_pngs": ["icon.png"]
        }
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate,
      {install_url.spec(), kAppName, start_url.spec(), scope.spec()}, nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(install_url, app_config),
            InstallResultCode::kSuccessOfflineOnlyInstall);

  EXPECT_TRUE(registrar().IsInstalled(app_id));
  EXPECT_EQ(registrar().GetAppShortName(app_id), kAppName);
  EXPECT_EQ(registrar().GetAppStartUrl(app_id).spec(), start_url);
  EXPECT_EQ(registrar().GetAppScope(app_id).spec(), scope);
  EXPECT_EQ(registrar().GetAppUserDisplayMode(app_id),
            DisplayMode::kStandalone);
  EXPECT_EQ(registrar().GetAppDisplayMode(app_id), DisplayMode::kMinimalUi);
  // theme_color must be installed opaque.
  EXPECT_EQ(registrar().GetAppThemeColor(app_id),
            SkColorSetARGB(0xFF, 0xBB, 0xCC, 0xDD));
  EXPECT_EQ(IconManagerReadAppIconPixel(icon_manager(), app_id, /*size=*/192),
            SK_ColorBLUE);
}

const char kOnlyForNewUsersInstallUrl[] = "https://example.org/";
const char kOnlyForNewUsersConfig[] = R"({
    "app_url": "https://example.org/",
    "launch_container": "window",
    "user_type": ["unmanaged"],
    "only_for_new_users": true,
    "only_use_offline_manifest": true,
    "offline_manifest": {
      "name": "Test",
      "start_url": "https://example.org/",
      "scope": "https://example.org/",
      "display": "standalone",
      "icon_any_pngs": ["icon.png"]
    }
  })";

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       PRE_OnlyForNewUsersWithNewUser) {
  // Install a policy app first to check that it doesn't interfere.
  {
    base::RunLoop run_loop;
    WebAppPolicyManager& policy_manager =
        WebAppProvider::GetForTest(profile())->policy_manager();
    policy_manager.SetOnAppsSynchronizedCompletedCallbackForTesting(
        run_loop.QuitClosure());
    const char kWebAppPolicy[] = R"([{
      "url": "https://policy-example.org/",
      "default_launch_container": "window"
    }])";
    profile()->GetPrefs()->Set(prefs::kWebAppInstallForceList,
                               base::JSONReader::Read(kWebAppPolicy).value());
    run_loop.Run();
  }

  // New user should have the app installed.
  EXPECT_EQ(SyncPreinstalledAppConfig(GURL(kOnlyForNewUsersInstallUrl),
                                      kOnlyForNewUsersConfig),
            InstallResultCode::kSuccessOfflineOnlyInstall);
}

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       OnlyForNewUsersWithNewUser) {
  // App should persist after user stops being a new user.
  EXPECT_EQ(SyncPreinstalledAppConfig(GURL(kOnlyForNewUsersInstallUrl),
                                      kOnlyForNewUsersConfig),
            InstallResultCode::kSuccessAlreadyInstalled);
}

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       PRE_OnlyForNewUsersWithOldUser) {
  // Simulate running Chrome without the configs present.
  SyncEmptyConfigs();
}
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       OnlyForNewUsersWithOldUser) {
  // This instance of Chrome should be considered not a new user after the
  // previous PRE_ launch and sync.
  EXPECT_EQ(SyncPreinstalledAppConfig(GURL(kOnlyForNewUsersInstallUrl),
                                      kOnlyForNewUsersConfig),
            absl::nullopt);
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest, OemInstalled) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  ASSERT_TRUE(embedded_test_server()->Start());

  EXPECT_EQ(SyncPreinstalledAppConfig(GetAppUrl(),
                                      base::ReplaceStringPlaceholders(
                                          R"({
                "app_url": "$1",
                "launch_container": "window",
                "oem_installed": true,
                "user_type": ["unmanaged"]
              })",
                                          {GetAppUrl().spec()}, nullptr)),
            InstallResultCode::kSuccessNewInstall);

  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt, GetAppUrl());
  EXPECT_TRUE(registrar().WasInstalledByOem(app_id));

  // Wait for app service to see the newly installed app.
  auto* proxy = apps::AppServiceProxyFactory::GetForProfile(profile());
  proxy->FlushMojoCallsForTesting();

  apps::mojom::InstallReason install_reason =
      apps::mojom::InstallReason::kUnknown;
  proxy->AppRegistryCache().ForOneApp(app_id,
                                      [&](const apps::AppUpdate& update) {
                                        install_reason = update.InstallReason();
                                      });

  EXPECT_EQ(install_reason, apps::mojom::InstallReason::kOem);
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

namespace {
ui::TouchscreenDevice CreateTouchDevice(ui::InputDeviceType type,
                                        bool stylus_support) {
  ui::TouchscreenDevice touch_device = ui::TouchscreenDevice();
  touch_device.type = type;
  touch_device.has_stylus = stylus_support;
  return touch_device;
}
}  // namespace

// Note that SetTouchscreenDevices() does not update the device list
// if the number of displays don't change.
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       DisableIfTouchscreenWithStylusNotSupported) {
  PreinstalledWebAppManager::BypassOfflineManifestRequirementForTesting();
  ASSERT_TRUE(embedded_test_server()->Start());

  const auto manifest = base::ReplaceStringPlaceholders(
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "disable_if_touchscreen_with_stylus_not_supported": true,
        "user_type": ["unmanaged"]
      })",
      {GetAppUrl().spec()}, nullptr);
  AppId app_id = GenerateAppId(/*manifest_id=*/absl::nullopt, GetAppUrl());
  const auto& disabled_configs = manager().debug_info()->disabled_configs;
  constexpr char kErrorMessage[] =
      " disabled because the device does not have a built-in touchscreen with "
      "stylus support.";

  // Test Case: No touchscreen installed on device.
  EXPECT_EQ(SyncPreinstalledAppConfig(GetAppUrl(), manifest), absl::nullopt);
  EXPECT_FALSE(registrar().IsInstalled(app_id));
  EXPECT_EQ(disabled_configs.size(), 1u);
  EXPECT_EQ(disabled_configs.back().second, GetAppUrl().spec() + kErrorMessage);

  // Test Case: Built-in touchscreen without stylus support installed on device.
  ui::DeviceDataManagerTestApi().SetTouchscreenDevices({CreateTouchDevice(
      ui::InputDeviceType::INPUT_DEVICE_INTERNAL, /* stylus_support =*/false)});
  EXPECT_EQ(SyncPreinstalledAppConfig(GetAppUrl(), manifest), absl::nullopt);
  EXPECT_FALSE(registrar().IsInstalled(app_id));
  EXPECT_EQ(disabled_configs.size(), 2u);
  EXPECT_EQ(disabled_configs.back().second, GetAppUrl().spec() + kErrorMessage);

  // Test Case: Connected external touchscreen with stylus support connected to
  // device.
  ui::DeviceDataManagerTestApi().SetTouchscreenDevices(
      {CreateTouchDevice(ui::InputDeviceType::INPUT_DEVICE_INTERNAL,
                         /* stylus_support =*/false),
       CreateTouchDevice(ui::InputDeviceType::INPUT_DEVICE_USB,
                         /* stylus_support =*/true)});
  EXPECT_EQ(SyncPreinstalledAppConfig(GetAppUrl(), manifest), absl::nullopt);
  EXPECT_FALSE(registrar().IsInstalled(app_id));
  EXPECT_EQ(disabled_configs.size(), 3u);
  EXPECT_EQ(disabled_configs.back().second, GetAppUrl().spec() + kErrorMessage);

  // Test Case: Create a built-in touchscreen device with stylus support and add
  // it to the device.
  ui::DeviceDataManagerTestApi().SetTouchscreenDevices(
      {CreateTouchDevice(ui::InputDeviceType::INPUT_DEVICE_INTERNAL, true)});
  EXPECT_EQ(SyncPreinstalledAppConfig(GetAppUrl(), manifest),
            InstallResultCode::kSuccessNewInstall);
  EXPECT_TRUE(registrar().IsInstalled(app_id));
  EXPECT_EQ(disabled_configs.size(), 3u);
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
// Disabled due to test flakiness. https://crbug.com/1267164.
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       DISABLED_UninstallFromTwoItemAppListFolder) {
  GURL preinstalled_app_start_url("https://example.org/");
  GURL user_app_start_url("https://test.org/");

  apps::AppServiceProxy* proxy =
      apps::AppServiceProxyFactory::GetForProfile(profile());
  AppListClientImpl::GetInstance()->UpdateProfile();
  ash::AppListTestApi app_list_test_api;
  app_list::AppListSyncableService* app_list_syncable_service =
      app_list::AppListSyncableServiceFactory::GetForProfile(profile());

  // Install default app.
  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "window",
        "user_type": ["unmanaged"],
        "only_use_offline_manifest": true,
        "offline_manifest": {
          "name": "Test default app",
          "display": "standalone",
          "start_url": "$1",
          "scope": "$1",
          "icon_any_pngs": ["icon.png"]
        }
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate, {preinstalled_app_start_url.spec()}, nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(preinstalled_app_start_url, app_config),
            InstallResultCode::kSuccessOfflineOnlyInstall);
  AppId preinstalled_app_id =
      GenerateAppId(/*manifest_id=*/absl::nullopt, preinstalled_app_start_url);

  // Install user app.
  auto web_application_info = std::make_unique<WebApplicationInfo>();
  web_application_info->start_url = user_app_start_url;
  web_application_info->title = u"Test user app";
  AppId user_app_id =
      test::InstallWebApp(profile(), std::move(web_application_info));

  // Ensure the UI receives these apps.
  proxy->FlushMojoCallsForTesting();

  // Put apps in app list folder.
  std::string folder_id = app_list_test_api.CreateFolderWithApps(
      {preinstalled_app_id, user_app_id});
  EXPECT_EQ(
      app_list_syncable_service->GetSyncItem(preinstalled_app_id)->parent_id,
      folder_id);
  EXPECT_EQ(app_list_syncable_service->GetSyncItem(user_app_id)->parent_id,
            folder_id);

  // Uninstall default app.
  proxy->UninstallSilently(preinstalled_app_id,
                           apps::mojom::UninstallSource::kUnknown);

  // Ensure the UI receives the app uninstall.
  proxy->FlushMojoCallsForTesting();

  // Default app should be removed from local app list but remain in sync list.
  EXPECT_FALSE(registrar().IsInstalled(preinstalled_app_id));
  EXPECT_TRUE(registrar().IsInstalled(user_app_id));
  EXPECT_FALSE(app_list_test_api.HasApp(preinstalled_app_id));
  EXPECT_TRUE(app_list_test_api.HasApp(user_app_id));
  EXPECT_EQ(
      app_list_syncable_service->GetSyncItem(preinstalled_app_id)->parent_id,
      "");
  EXPECT_EQ(app_list_syncable_service->GetSyncItem(user_app_id)->parent_id, "");
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

// Check that offline only installs don't overwrite fresh online manifest
// obtained via sync install.
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppManagerBrowserTest,
                       OfflineOnlyManifest_SiteAlreadyInstalledFromSync) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL install_url = GetAppUrl();
  GURL start_url = install_url;
  GURL scope = embedded_test_server()->GetURL("/web_apps/");

  const AppId app_id = InstallWebAppFromPage(browser(), install_url);

  const WebApp* web_app = registrar().GetAppById(app_id);
  ASSERT_TRUE(web_app);

  EXPECT_TRUE(web_app->IsSynced());
  EXPECT_FALSE(web_app->IsPreinstalledApp());

  {
    SCOPED_TRACE("Expect initial manifest fields from basic.html web app.");
    ExpectInitialManifestFieldsFromBasicWebApp(icon_manager(), web_app,
                                               start_url, scope);
  }

  constexpr char kAppConfigTemplate[] =
      R"({
        "app_url": "$1",
        "launch_container": "tab",
        "user_type": ["unmanaged"],
        "only_use_offline_manifest": true,
        "offline_manifest": {
          "name": "$2",
          "start_url": "$3",
          "scope": "$4",
          "display": "minimal-ui",
          "theme_color_argb_hex": "AABBCCDD",
          "icon_any_pngs": ["icon.png"]
        }
      })";
  std::string app_config = base::ReplaceStringPlaceholders(
      kAppConfigTemplate,
      {install_url.spec(), "Overwrite app name", start_url.spec(),
       "https://overwrite.scope/"},
      nullptr);
  EXPECT_EQ(SyncPreinstalledAppConfig(install_url, app_config),
            InstallResultCode::kSuccessOfflineOnlyInstall);

  EXPECT_EQ(web_app, registrar().GetAppById(app_id));

  EXPECT_TRUE(web_app->IsSynced());
  EXPECT_TRUE(web_app->IsPreinstalledApp());

  {
    SCOPED_TRACE(
        "Expect same manifest fields from basic.html web app, no overwrites.");
    ExpectInitialManifestFieldsFromBasicWebApp(icon_manager(), web_app,
                                               start_url, scope);
  }
}

#endif  // defined(OS_CHROMEOS)

}  // namespace web_app
