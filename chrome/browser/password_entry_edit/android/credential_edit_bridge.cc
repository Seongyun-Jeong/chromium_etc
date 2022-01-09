// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/password_entry_edit/android/credential_edit_bridge.h"

#include <jni.h>
#include <memory>

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/password_entry_edit/android/jni_headers/CredentialEditBridge_jni.h"
#include "chrome/grit/generated_resources.h"
#include "components/password_manager/core/browser/android_affiliation/affiliation_utils.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_list_sorter.h"
#include "components/password_manager/core/browser/ui/saved_passwords_presenter.h"
#include "components/url_formatter/url_formatter.h"
#include "ui/base/l10n/l10n_util.h"

std::unique_ptr<CredentialEditBridge> CredentialEditBridge::MaybeCreate(
    const password_manager::PasswordForm credential,
    IsInsecureCredential is_insecure_credential,
    std::vector<std::u16string> existing_usernames,
    password_manager::SavedPasswordsPresenter* saved_passwords_presenter,
    PasswordManagerPresenter* password_manager_presenter,
    base::OnceClosure dismissal_callback,
    const base::android::JavaRef<jobject>& context,
    const base::android::JavaRef<jobject>& settings_launcher) {
  base::android::ScopedJavaGlobalRef<jobject> java_bridge;
  java_bridge.Reset(Java_CredentialEditBridge_maybeCreate(
      base::android::AttachCurrentThread()));
  if (!java_bridge) {
    return nullptr;
  }
  return base::WrapUnique(new CredentialEditBridge(
      std::move(credential), is_insecure_credential,
      std::move(existing_usernames), saved_passwords_presenter,
      password_manager_presenter, std::move(dismissal_callback), context,
      settings_launcher, std::move(java_bridge)));
}

CredentialEditBridge::CredentialEditBridge(
    const password_manager::PasswordForm credential,
    IsInsecureCredential is_insecure_credential,
    std::vector<std::u16string> existing_usernames,
    password_manager::SavedPasswordsPresenter* saved_passwords_presenter,
    PasswordManagerPresenter* password_manager_presenter,
    base::OnceClosure dismissal_callback,
    const base::android::JavaRef<jobject>& context,
    const base::android::JavaRef<jobject>& settings_launcher,
    base::android::ScopedJavaGlobalRef<jobject> java_bridge)
    : credential_(std::move(credential)),
      is_insecure_credential_(is_insecure_credential),
      existing_usernames_(std::move(existing_usernames)),
      saved_passwords_presenter_(saved_passwords_presenter),
      password_manager_presenter_(password_manager_presenter),
      dismissal_callback_(std::move(dismissal_callback)),
      java_bridge_(java_bridge) {
  Java_CredentialEditBridge_initAndLaunchUi(
      base::android::AttachCurrentThread(), java_bridge_,
      reinterpret_cast<intptr_t>(this), context, settings_launcher,
      credential.blocked_by_user, !credential.federation_origin.opaque());
}

CredentialEditBridge::~CredentialEditBridge() {
  Java_CredentialEditBridge_destroy(base::android::AttachCurrentThread(),
                                    java_bridge_);
}

void CredentialEditBridge::GetCredential(JNIEnv* env) {
  Java_CredentialEditBridge_setCredential(
      env, java_bridge_,
      base::android::ConvertUTF16ToJavaString(env, GetDisplayURLOrAppName()),
      base::android::ConvertUTF16ToJavaString(env, credential_.username_value),
      base::android::ConvertUTF16ToJavaString(env, credential_.password_value),
      base::android::ConvertUTF16ToJavaString(env,
                                              GetDisplayFederationOrigin()),
      is_insecure_credential_.value());
}

void CredentialEditBridge::GetExistingUsernames(JNIEnv* env) {
  Java_CredentialEditBridge_setExistingUsernames(
      env, java_bridge_,
      base::android::ToJavaArrayOfStrings(env, existing_usernames_));
}

void CredentialEditBridge::SaveChanges(
    JNIEnv* env,
    const base::android::JavaParamRef<jstring>& username,
    const base::android::JavaParamRef<jstring>& password) {
  saved_passwords_presenter_->EditSavedPasswords(
      credential_, base::android::ConvertJavaStringToUTF16(username),
      base::android::ConvertJavaStringToUTF16(password));
}

void CredentialEditBridge::DeleteCredential(JNIEnv* env) {
  if (credential_.blocked_by_user) {
    std::vector<std::string> sort_keys = {
        password_manager::CreateSortKey(credential_)};
    password_manager_presenter_->RemovePasswordExceptions(sort_keys);
  } else if (!credential_.federation_origin.opaque()) {
    std::vector<std::string> sort_keys = {
        password_manager::CreateSortKey(credential_)};
    password_manager_presenter_->RemoveSavedPasswords(sort_keys);
  } else {
    saved_passwords_presenter_->RemovePassword(credential_);
  }
  std::move(dismissal_callback_).Run();
}

void CredentialEditBridge::OnUIDismissed(JNIEnv* env) {
  std::move(dismissal_callback_).Run();
}

std::u16string CredentialEditBridge::GetDisplayURLOrAppName() {
  auto facet = password_manager::FacetURI::FromPotentiallyInvalidSpec(
      credential_.signon_realm);

  if (facet.IsValidAndroidFacetURI()) {
    if (credential_.app_display_name.empty()) {
      // In case no affiliation information could be obtained show the
      // formatted package name to the user.
      return l10n_util::GetStringFUTF16(
          IDS_SETTINGS_PASSWORDS_ANDROID_APP,
          base::UTF8ToUTF16(facet.android_package_name()));
    }

    return base::UTF8ToUTF16(credential_.app_display_name);
  }

  return url_formatter::FormatUrl(
      credential_.url.DeprecatedGetOriginAsURL(),
      url_formatter::kFormatUrlOmitDefaults |
          url_formatter::kFormatUrlOmitHTTPS |
          url_formatter::kFormatUrlOmitTrivialSubdomains |
          url_formatter::kFormatUrlTrimAfterHost,
      net::UnescapeRule::SPACES, nullptr, nullptr, nullptr);
}

std::u16string CredentialEditBridge::GetDisplayFederationOrigin() {
  return credential_.IsFederatedCredential()
             ? url_formatter::FormatUrl(
                   credential_.federation_origin.GetURL(),
                   url_formatter::kFormatUrlOmitDefaults |
                       url_formatter::kFormatUrlOmitHTTPS |
                       url_formatter::kFormatUrlOmitTrivialSubdomains |
                       url_formatter::kFormatUrlTrimAfterHost,
                   net::UnescapeRule::SPACES, nullptr, nullptr, nullptr)
             : std::u16string();
}
