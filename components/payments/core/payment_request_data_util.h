// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CORE_PAYMENT_REQUEST_DATA_UTIL_H_
#define COMPONENTS_PAYMENTS_CORE_PAYMENT_REQUEST_DATA_UTIL_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "components/autofill/core/browser/data_model/credit_card.h"
#include "components/payments/mojom/payment_request_data.mojom.h"
#include "url/gurl.h"

namespace autofill {
class AutofillProfile;
}  // namespace autofill

namespace payments {

struct BasicCardResponse;
class PaymentMethodData;

namespace data_util {

// Helper function to get an instance of PaymentAddressPtr from an autofill
// profile.
mojom::PaymentAddressPtr GetPaymentAddressFromAutofillProfile(
    const autofill::AutofillProfile& profile,
    const std::string& app_locale);

// Helper function to get an instance of web::BasicCardResponse from an autofill
// credit card.
std::unique_ptr<BasicCardResponse> GetBasicCardResponseFromAutofillCreditCard(
    const autofill::CreditCard& card,
    const std::u16string& cvc,
    const autofill::AutofillProfile& billing_profile,
    const std::string& app_locale);

// Parse all the supported payment methods from the merchant including 1) the
// supported card networks from supportedMethods and  "basic-card"'s
// supportedNetworks and 2) the url-based payment method identifiers.
// |out_supported_networks| is filled with a list of networks
// in the order that they were specified by the merchant.
// |out_basic_card_supported_networks| is a subset of |out_supported_networks|
// that includes all networks that were specified as part of "basic-card". This
// is used to know whether to return the card network name (e.g., "visa") or
// "basic-card" in the PaymentResponse. |method_data.supported_networks| is
// expected to only contain basic-card card network names (the list is at
// https://www.w3.org/Payments/card-network-ids).
// |out_url_payment_method_identifiers| is filled with a list of all the
// payment method identifiers specified by the merchant that are URL-based.
void ParseSupportedMethods(
    const std::vector<PaymentMethodData>& method_data,
    std::vector<std::string>* out_supported_networks,
    std::set<std::string>* out_basic_card_supported_networks,
    std::vector<GURL>* out_url_payment_method_identifiers,
    std::set<std::string>* out_payment_method_identifiers);

// Formats |card_number| for display. For example, "4111111111111111" is
// formatted into "4111 1111 1111 1111". This method does not format masked card
// numbers, which start with a letter.
std::u16string FormatCardNumberForDisplay(const std::u16string& card_number);

// Returns the subset of |stringified_method_data| map where the keys are in the
// |supported_payment_method_names| set. Used for ensuring that a payment app
// will not be queried about payment method names that it does not support.
//
// FilterStringifiedMethodData({"a": {"b"}: "c": {"d"}}, {"a"}) -> {"a": {"b"}}
//
// Both the return value and the first parameter to the function have the
// following format:
// Key: Payment method identifier, such as "example-test" or
//      "https://example.test".
// Value: The set of all payment method specific parameters for the given
//        payment method identifier, each one serialized into a JSON string,
//        e.g., '{"key": "value"}'.
std::unique_ptr<std::map<std::string, std::set<std::string>>>
FilterStringifiedMethodData(
    const std::map<std::string, std::set<std::string>>& stringified_method_data,
    const std::set<std::string>& supported_payment_method_names);

}  // namespace data_util
}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CORE_PAYMENT_REQUEST_DATA_UTIL_H_
