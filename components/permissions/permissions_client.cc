// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/permissions/permissions_client.h"

#include "base/callback.h"
#include "build/chromeos_buildflags.h"
#include "components/permissions/permission_uma_util.h"

#if !defined(OS_ANDROID)
#include "ui/gfx/paint_vector_icon.h"
#endif

namespace permissions {
namespace {
PermissionsClient* g_client = nullptr;
}

PermissionsClient::PermissionsClient() {
  DCHECK(!g_client);
  g_client = this;
}

PermissionsClient::~PermissionsClient() {
  g_client = nullptr;
}

// static
PermissionsClient* PermissionsClient::Get() {
  DCHECK(g_client);
  return g_client;
}

double PermissionsClient::GetSiteEngagementScore(
    content::BrowserContext* browser_context,
    const GURL& origin) {
  return 0.0;
}

void PermissionsClient::AreSitesImportant(
    content::BrowserContext* browser_context,
    std::vector<std::pair<url::Origin, bool>>* origins) {
  for (auto& entry : *origins)
    entry.second = false;
}

#if defined(OS_ANDROID) || BUILDFLAG(IS_CHROMEOS_ASH)
bool PermissionsClient::IsCookieDeletionDisabled(
    content::BrowserContext* browser_context,
    const GURL& origin) {
  return false;
}
#endif

void PermissionsClient::GetUkmSourceId(content::BrowserContext* browser_context,
                                       const content::WebContents* web_contents,
                                       const GURL& requesting_origin,
                                       GetUkmSourceIdCallback callback) {
  std::move(callback).Run(absl::nullopt);
}

IconId PermissionsClient::GetOverrideIconId(RequestType request_type) {
#if defined(OS_ANDROID)
  return 0;
#else
  return gfx::kNoneIcon;
#endif
}

std::vector<std::unique_ptr<PermissionUiSelector>>
PermissionsClient::CreatePermissionUiSelectors(
    content::BrowserContext* browser_context) {
  return std::vector<std::unique_ptr<PermissionUiSelector>>();
}

void PermissionsClient::OnPromptResolved(
    content::BrowserContext* browser_context,
    RequestType request_type,
    PermissionAction action,
    const GURL& origin,
    PermissionPromptDisposition prompt_disposition,
    absl::optional<QuietUiReason> quiet_ui_reason) {}

absl::optional<bool>
PermissionsClient::HadThreeConsecutiveNotificationPermissionDenies(
    content::BrowserContext* browser_context) {
  return absl::nullopt;
}

absl::optional<url::Origin> PermissionsClient::GetAutoApprovalOrigin() {
  return absl::nullopt;
}

absl::optional<bool> PermissionsClient::HasPreviouslyAutoRevokedPermission(
    content::BrowserContext* browser_context,
    const GURL& origin,
    ContentSettingsType permission) {
  return absl::nullopt;
}

bool PermissionsClient::CanBypassEmbeddingOriginCheck(
    const GURL& requesting_origin,
    const GURL& embedding_origin) {
  return false;
}

absl::optional<GURL> PermissionsClient::OverrideCanonicalOrigin(
    const GURL& requesting_origin,
    const GURL& embedding_origin) {
  return absl::nullopt;
}

bool PermissionsClient::DoOriginsMatchNewTabPage(const GURL& requesting_origin,
                                                 const GURL& embedding_origin) {
  return false;
}

#if defined(OS_ANDROID)
bool PermissionsClient::IsDseOrigin(content::BrowserContext* browser_context,
                                    const url::Origin& origin) {
  return false;
}

infobars::InfoBarManager* PermissionsClient::GetInfoBarManager(
    content::WebContents* web_contents) {
  return nullptr;
}

infobars::InfoBar* PermissionsClient::MaybeCreateInfoBar(
    content::WebContents* web_contents,
    ContentSettingsType type,
    base::WeakPtr<PermissionPromptAndroid> prompt) {
  return nullptr;
}

#if defined(OS_ANDROID)
std::unique_ptr<PermissionsClient::PermissionMessageDelegate>
PermissionsClient::MaybeCreateMessageUI(
    content::WebContents* web_contents,
    ContentSettingsType type,
    base::WeakPtr<PermissionPromptAndroid> prompt) {
  return nullptr;
}
#endif

void PermissionsClient::RepromptForAndroidPermissions(
    content::WebContents* web_contents,
    const std::vector<ContentSettingsType>& content_settings_types,
    PermissionsUpdatedCallback callback) {
  std::move(callback).Run(false);
}

int PermissionsClient::MapToJavaDrawableId(int resource_id) {
  return 0;
}
#else
std::unique_ptr<PermissionPrompt> PermissionsClient::CreatePrompt(
    content::WebContents* web_contents,
    PermissionPrompt::Delegate* delegate) {
  return nullptr;
}
#endif

}  // namespace permissions
