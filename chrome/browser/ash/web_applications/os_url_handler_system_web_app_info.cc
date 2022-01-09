// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/web_applications/os_url_handler_system_web_app_info.h"

#include "ash/constants/ash_features.h"
#include "ash/public/cpp/resources/grit/ash_public_unscaled_resources.h"
#include "base/feature_list.h"
#include "chrome/browser/ash/crosapi/browser_util.h"
#include "chrome/browser/ash/web_applications/system_web_app_install_utils.h"
#include "chrome/browser/ui/webui/chrome_web_ui_controller_factory.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/crosapi/cpp/gurl_os_handler_utils.h"
#include "content/public/common/url_constants.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/chromeos/styles/cros_styles.h"

namespace {

bool g_enable_delegate_for_testing = false;

SkColor GetBgColor(bool use_dark_mode) {
  return cros_styles::ResolveColor(
      cros_styles::ColorName::kBgColor, use_dark_mode,
      base::FeatureList::IsEnabled(
          ash::features::kSemanticColorsDebugOverride));
}

}  // namespace

OsUrlHandlerSystemWebAppDelegate::OsUrlHandlerSystemWebAppDelegate(
    Profile* profile)
    : web_app::SystemWebAppDelegate(web_app::SystemAppType::OS_URL_HANDLER,
                                    "OsUrlHandler",
                                    GURL(chrome::kChromeUIOsUrlAppURL),
                                    profile) {}

OsUrlHandlerSystemWebAppDelegate::~OsUrlHandlerSystemWebAppDelegate() = default;

std::unique_ptr<WebApplicationInfo>
OsUrlHandlerSystemWebAppDelegate::GetWebAppInfo() const {
  auto info = std::make_unique<WebApplicationInfo>();
  info->start_url = GURL(chrome::kChromeUIOsUrlAppURL);
  info->scope = GURL(chrome::kChromeUIOsUrlAppURL);
  info->title = l10n_util::GetStringUTF16(IDS_OS_URL_HANDLER_APP_NAME);

  web_app::CreateIconInfoForSystemWebApp(
      info->start_url,
      {
          {"os_url_handler_app_icon_48.png", 48,
           IDR_OS_URL_HANDLER_APP_ICONS_48_PNG},
          {"os_url_handler_app_icon_128.png", 128,
           IDR_OS_URL_HANDLER_APP_ICONS_128_PNG},
          {"os_url_handler_app_icon_192.png", 192,
           IDR_OS_URL_HANDLER_APP_ICONS_192_PNG},
      },
      *info);

  info->theme_color = GetBgColor(/*use_dark_mode=*/false);
  info->dark_mode_theme_color = GetBgColor(/*use_dark_mode=*/true);
  info->display_mode = blink::mojom::DisplayMode::kStandalone;
  info->user_display_mode = blink::mojom::DisplayMode::kStandalone;

  return info;
}

bool OsUrlHandlerSystemWebAppDelegate::ShouldCaptureNavigations() const {
  return true;
}

bool OsUrlHandlerSystemWebAppDelegate::IsAppEnabled() const {
  return g_enable_delegate_for_testing ||
         crosapi::browser_util::IsLacrosEnabled();
}

bool OsUrlHandlerSystemWebAppDelegate::ShouldShowInLauncher() const {
  return false;
}

bool OsUrlHandlerSystemWebAppDelegate::ShouldShowInSearch() const {
  return false;
}

bool OsUrlHandlerSystemWebAppDelegate::ShouldReuseExistingWindow() const {
  return false;
}

bool OsUrlHandlerSystemWebAppDelegate::IsUrlInSystemAppScope(
    const GURL& url) const {
  if (!IsAppEnabled())
    return false;

  GURL target_url = crosapi::gurl_os_handler_utils::SanitizeAshURL(url);
  if (!target_url.has_scheme() || !target_url.has_host())
    return false;

  if (ChromeWebUIControllerFactory::GetInstance()->CanHandleUrl(target_url))
    return true;

  if (target_url.scheme() != content::kChromeUIScheme)
    return false;

  // By the time the web app system gets the link, the os:// scheme will have
  // been replaced by the chrome:// scheme. As the user cannot enter in ash
  // chrome:// scheme urls anymore, we should be safely able to assume that they
  // might have been os:// schemed URLs when being called from Lacros.
  target_url =
      crosapi::gurl_os_handler_utils::GetSystemUrlFromChromeUrl(target_url);
  return ChromeWebUIControllerFactory::GetInstance()->CanHandleUrl(target_url);
}

void OsUrlHandlerSystemWebAppDelegate::EnableDelegateForTesting(bool enable) {
  g_enable_delegate_for_testing = enable;
}
