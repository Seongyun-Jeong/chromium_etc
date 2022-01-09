// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/autofill_save_update_address_profile_delegate_ios.h"

#include <utility>

#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/autofill_address_util.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/grit/components_scaled_resources.h"
#include "components/infobars/core/infobar.h"
#include "components/infobars/core/infobar_manager.h"
#include "components/strings/grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"

namespace autofill {

AutofillSaveUpdateAddressProfileDelegateIOS::
    AutofillSaveUpdateAddressProfileDelegateIOS(
        const AutofillProfile& profile,
        const AutofillProfile* original_profile,
        const std::string& locale,
        AutofillClient::AddressProfileSavePromptCallback callback)
    : locale_(locale),
      profile_(profile),
      original_profile_(base::OptionalFromPtr(original_profile)),
      address_profile_save_prompt_callback_(std::move(callback)) {}

AutofillSaveUpdateAddressProfileDelegateIOS::
    ~AutofillSaveUpdateAddressProfileDelegateIOS() {
  // If the user has navigated away without saving the modal, then the
  // |address_profile_save_prompt_callback_| is run here.
  if (!address_profile_save_prompt_callback_.is_null()) {
    DCHECK(
        user_decision_ !=
            AutofillClient::SaveAddressProfileOfferUserDecision::kAccepted &&
        user_decision_ !=
            AutofillClient::SaveAddressProfileOfferUserDecision::kEditAccepted);
    RunSaveAddressProfilePromptCallback();
  }
}

// static
AutofillSaveUpdateAddressProfileDelegateIOS*
AutofillSaveUpdateAddressProfileDelegateIOS::FromInfobarDelegate(
    infobars::InfoBarDelegate* delegate) {
  return delegate->GetIdentifier() ==
                 AUTOFILL_ADDRESS_PROFILE_INFOBAR_DELEGATE_IOS
             ? static_cast<AutofillSaveUpdateAddressProfileDelegateIOS*>(
                   delegate)
             : nullptr;
}

std::u16string
AutofillSaveUpdateAddressProfileDelegateIOS::GetEnvelopeStyleAddress() const {
  return ::autofill::GetEnvelopeStyleAddress(profile_, locale_,
                                             /*include_recipient=*/true,
                                             /*include_country=*/true);
}

std::u16string AutofillSaveUpdateAddressProfileDelegateIOS::GetPhoneNumber()
    const {
  return GetProfileInfo(PHONE_HOME_WHOLE_NUMBER);
}

std::u16string AutofillSaveUpdateAddressProfileDelegateIOS::GetEmailAddress()
    const {
  return GetProfileInfo(EMAIL_ADDRESS);
}

std::u16string AutofillSaveUpdateAddressProfileDelegateIOS::GetDescription()
    const {
  return GetProfileDescription(
      original_profile_ ? *original_profile_ : profile_, locale_,
      /*include_address_and_contacts=*/true);
}

std::u16string AutofillSaveUpdateAddressProfileDelegateIOS::GetSubtitle() {
  DCHECK(original_profile_);
  std::vector<ProfileValueDifference> differences =
      GetProfileDifferenceForUi(original_profile_.value(), profile_, locale_);
  bool address_updated =
      std::find_if(differences.begin(), differences.end(),
                   [](const ProfileValueDifference& diff) {
                     return diff.type == ADDRESS_HOME_ADDRESS;
                   }) != differences.end();
  return GetProfileDescription(
      original_profile_.value(), locale_,
      /*include_address_and_contacts=*/!address_updated);
}

std::u16string
AutofillSaveUpdateAddressProfileDelegateIOS::GetMessageActionText() const {
  return l10n_util::GetStringUTF16(
      original_profile_ ? IDS_IOS_AUTOFILL_UPDATE_ADDRESS_MESSAGE_PRIMARY_ACTION
                        : IDS_IOS_AUTOFILL_SAVE_ADDRESS_MESSAGE_PRIMARY_ACTION);
}

const autofill::AutofillProfile*
AutofillSaveUpdateAddressProfileDelegateIOS::GetProfile() const {
  return &profile_;
}

const autofill::AutofillProfile*
AutofillSaveUpdateAddressProfileDelegateIOS::GetOriginalProfile() const {
  return base::OptionalOrNullptr(original_profile_);
}

std::u16string AutofillSaveUpdateAddressProfileDelegateIOS::GetProfileInfo(
    ServerFieldType type) const {
  return profile_.GetInfo(type, locale_);
}

std::vector<ProfileValueDifference>
AutofillSaveUpdateAddressProfileDelegateIOS::GetProfileDiff() const {
  return GetProfileDifferenceForUi(*GetProfile(), *GetOriginalProfile(),
                                   locale_);
}

void AutofillSaveUpdateAddressProfileDelegateIOS::EditAccepted() {
  user_decision_ =
      AutofillClient::SaveAddressProfileOfferUserDecision::kEditAccepted;
  RunSaveAddressProfilePromptCallback();
}

void AutofillSaveUpdateAddressProfileDelegateIOS::EditDeclined() {
  SetUserDecision(
      AutofillClient::SaveAddressProfileOfferUserDecision::kEditDeclined);
}

void AutofillSaveUpdateAddressProfileDelegateIOS::MessageTimeout() {
  SetUserDecision(
      AutofillClient::SaveAddressProfileOfferUserDecision::kMessageTimeout);
}

void AutofillSaveUpdateAddressProfileDelegateIOS::MessageDeclined() {
  SetUserDecision(
      AutofillClient::SaveAddressProfileOfferUserDecision::kMessageDeclined);
}

void AutofillSaveUpdateAddressProfileDelegateIOS::SetProfileInfo(
    const ServerFieldType& type,
    const std::u16string& value) {
  // Since the country field is a text field, we should use SetInfo() to make
  // sure they get converted to country codes.
  if (type == autofill::ADDRESS_HOME_COUNTRY) {
    profile_.SetInfoWithVerificationStatus(
        type, value, locale_,
        autofill::structured_address::VerificationStatus::kUserVerified);
    return;
  }

  profile_.SetRawInfoWithVerificationStatus(
      type, value,
      autofill::structured_address::VerificationStatus::kUserVerified);
}

bool AutofillSaveUpdateAddressProfileDelegateIOS::Accept() {
  user_decision_ =
      AutofillClient::SaveAddressProfileOfferUserDecision::kAccepted;
  RunSaveAddressProfilePromptCallback();
  return true;
}

bool AutofillSaveUpdateAddressProfileDelegateIOS::Cancel() {
  SetUserDecision(
      AutofillClient::SaveAddressProfileOfferUserDecision::kDeclined);
  return true;
}

bool AutofillSaveUpdateAddressProfileDelegateIOS::EqualsDelegate(
    infobars::InfoBarDelegate* delegate) const {
  return delegate->GetIdentifier() == GetIdentifier();
}

int AutofillSaveUpdateAddressProfileDelegateIOS::GetIconId() const {
  NOTREACHED();
  return IDR_INFOBAR_AUTOFILL_CC;
}

std::u16string AutofillSaveUpdateAddressProfileDelegateIOS::GetMessageText()
    const {
  return l10n_util::GetStringUTF16(
      original_profile_ ? IDS_IOS_AUTOFILL_UPDATE_ADDRESS_MESSAGE_TITLE
                        : IDS_IOS_AUTOFILL_SAVE_ADDRESS_MESSAGE_TITLE);
}

infobars::InfoBarDelegate::InfoBarIdentifier
AutofillSaveUpdateAddressProfileDelegateIOS::GetIdentifier() const {
  return AUTOFILL_ADDRESS_PROFILE_INFOBAR_DELEGATE_IOS;
}

bool AutofillSaveUpdateAddressProfileDelegateIOS::ShouldExpire(
    const NavigationDetails& details) const {
  // Expire the Infobar unless the navigation was triggered by the form that
  // presented the Infobar, or the navigation is a redirect.
  // Also, expire the infobar if the navigation is to a different page.
  return !details.is_form_submission && !details.is_redirect &&
         ConfirmInfoBarDelegate::ShouldExpire(details);
}

void AutofillSaveUpdateAddressProfileDelegateIOS::
    RunSaveAddressProfilePromptCallback() {
  std::move(address_profile_save_prompt_callback_)
      .Run(user_decision_, profile_);
}

void AutofillSaveUpdateAddressProfileDelegateIOS::SetUserDecision(
    AutofillClient::SaveAddressProfileOfferUserDecision user_decision) {
  if (user_decision == AutofillClient::SaveAddressProfileOfferUserDecision::
                           kMessageTimeout &&
      user_decision_ == AutofillClient::SaveAddressProfileOfferUserDecision::
                            kMessageDeclined) {
    // |SaveAddressProfileInfobarBannerInteractionHandler::InfobarVisibilityChanged|
    // would be called even when the banner is explicitly dismissed by the
    // user. In that case, do not change the |user_decision_|.
    return;
  }
  if (user_decision_ ==
          AutofillClient::SaveAddressProfileOfferUserDecision::kEditAccepted ||
      user_decision_ ==
          AutofillClient::SaveAddressProfileOfferUserDecision::kAccepted) {
    // The infobar has already been saved. So, cancel should not change the
    // |user_decision_| now.
    return;
  }
  user_decision_ = user_decision;
}

}  // namespace autofill
