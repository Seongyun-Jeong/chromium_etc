// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PROFILES_PROFILE_PICKER_DICE_SIGN_IN_PROVIDER_H_
#define CHROME_BROWSER_UI_VIEWS_PROFILES_PROFILE_PICKER_DICE_SIGN_IN_PROVIDER_H_

#include "base/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/chrome_web_modal_dialog_manager_delegate.h"
#include "chrome/browser/ui/views/profiles/profile_picker_web_contents_host.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/color/color_provider_manager.h"

struct CoreAccountInfo;
class ProfilePickerDiceSignInToolbar;

namespace content {
struct ContextMenuParams;
class RenderFrameHost;
class WebContents;
}  // namespace content

namespace ui {
class ThemeProvider;
}  // namespace ui

// Class responsible for the GAIA sign-in within profile creation flow.
class ProfilePickerDiceSignInProvider
    : public content::WebContentsDelegate,
      public ChromeWebModalDialogManagerDelegate,
      public signin::IdentityManager::Observer {
 public:
  // The callback returns the newly created profile and a valid WebContents
  // instance within this profile. If `is_saml` is true, sign-in is not
  // completed there yet. Otherwise, the newly created profile is properly
  // signed-in, i.e. its IdentityManager has a (unconsented) primary account.
  // If the flow gets canceled by closing the window, the callback never gets
  // called.
  // TODO(crbug.com/1240650): Properly support saml sign in so that the special
  // casing is not needed here.
  using SignedInCallback =
      base::OnceCallback<void(Profile* profile,
                              std::unique_ptr<content::WebContents>,
                              bool is_saml)>;

  ProfilePickerDiceSignInProvider(ProfilePickerWebContentsHost* host,
                                  ProfilePickerDiceSignInToolbar* toolbar);
  ~ProfilePickerDiceSignInProvider() override;
  ProfilePickerDiceSignInProvider(const ProfilePickerDiceSignInProvider&) =
      delete;
  ProfilePickerDiceSignInProvider& operator=(
      const ProfilePickerDiceSignInProvider&) = delete;

  // Initiates switching the flow to sign-in (which is normally asynchronous).
  // If a sign-in was in progress before in the lifetime of this class, it only
  // (synchronously) switches the view to show the ongoing sign-in again. When
  // the sign-in screen is displayed, `switch_finished_callback` gets called.
  // When the sign-in finishes (if it ever happens), `signin_finished_callback`
  // gets called.
  void SwitchToSignIn(base::OnceCallback<void(bool)> switch_finished_callback,
                      SignedInCallback signin_finished_callback);

  // Reloads the sign-in page if applicable.
  void ReloadSignInPage();

  // Navigates back in the sign-in flow if applicable.
  void NavigateBack();

  // Returns theme provider based on the sign-in profile or nullptr if the flow
  // is not yet initialized.
  const ui::ThemeProvider* GetThemeProvider() const;
  ui::ColorProviderManager::InitializerSupplier* GetCustomTheme() const;

 private:
  // content::WebContentsDelegate:
  bool HandleContextMenu(content::RenderFrameHost& render_frame_host,
                         const content::ContextMenuParams& params) override;
  void AddNewContents(content::WebContents* source,
                      std::unique_ptr<content::WebContents> new_contents,
                      const GURL& target_url,
                      WindowOpenDisposition disposition,
                      const gfx::Rect& initial_rect,
                      bool user_gesture,
                      bool* was_blocked) override;
  bool HandleKeyboardEvent(
      content::WebContents* source,
      const content::NativeWebKeyboardEvent& event) override;
  void NavigationStateChanged(content::WebContents* source,
                              content::InvalidateTypes changed_flags) override;

  // ChromeWebModalDialogManagerDelegate:
  web_modal::WebContentsModalDialogHost* GetWebContentsModalDialogHost()
      override;

  // IdentityManager::Observer:
  void OnRefreshTokenUpdatedForAccount(
      const CoreAccountInfo& account_info) override;
  void OnPrimaryAccountChanged(
      const signin::PrimaryAccountChangeEvent& event_details) override;

  // Initializes the flow with the newly created profile.
  void OnProfileCreated(
      base::OnceCallback<void(bool)>& switch_finished_callback,
      Profile* new_profile,
      Profile::CreateStatus status);

  // Finishes the sign-in (if there is a primary account with refresh tokens).
  void FinishFlowIfSignedIn();

  // Finishes the sign-in (if `is_saml` is true, it's due to SAML signin getting
  // detected).
  void FinishFlow(bool is_saml);

  // Returns whether the flow is initialized (i.e. whether `profile_` has been
  // created).
  bool IsInitialized() const;

  void OnSignInContentsFreedUp();

  content::WebContents* contents() const { return contents_.get(); }

  // The host and toolbar objects, must outlive this object.
  const raw_ptr<ProfilePickerWebContentsHost> host_;
  const raw_ptr<ProfilePickerDiceSignInToolbar> toolbar_;
  // Sign-in callback, valid until it's called.
  SignedInCallback callback_;

  raw_ptr<Profile> profile_ = nullptr;

  // Prevent |profile_| from being destroyed first.
  std::unique_ptr<ScopedProfileKeepAlive> profile_keep_alive_;

  // The web contents backed by `profile`. This is used for displaying the
  // sign-in flow.
  std::unique_ptr<content::WebContents> contents_;

  // Because of ProfileOAuth2TokenService intricacies, the sign in should not
  // finish before both the notification gets called.
  // TODO(crbug.com/1249488): Remove this if the bug gets resolved.
  bool refresh_token_updated_ = false;

  base::ScopedObservation<signin::IdentityManager,
                          signin::IdentityManager::Observer>
      identity_manager_observation_{this};

  base::WeakPtrFactory<ProfilePickerDiceSignInProvider> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_PROFILES_PROFILE_PICKER_DICE_SIGN_IN_PROVIDER_H_
