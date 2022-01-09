// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CORE_SECURE_PAYMENT_CONFIRMATION_METRICS_H_
#define COMPONENTS_PAYMENTS_CORE_SECURE_PAYMENT_CONFIRMATION_METRICS_H_

namespace payments {

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused. Keep in sync with
// tools/metrics/histograms/enums.xml.
enum class SecurePaymentConfirmationEnrollDialogShown {
  kCouldNotShow = 0,
  kShown = 1,
  kMaxValue = kShown,
};

void RecordEnrollDialogShown(SecurePaymentConfirmationEnrollDialogShown shown);

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused. Keep in sync with
// tools/metrics/histograms/enums.xml.
enum class SecurePaymentConfirmationEnrollDialogResult {
  kClosed = 0,
  kCanceled = 1,
  kAccepted = 2,
  kMaxValue = kAccepted,
};

void RecordEnrollDialogResult(
    SecurePaymentConfirmationEnrollDialogResult result);

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused. Keep in sync with
// tools/metrics/histograms/enums.xml.
enum class SecurePaymentConfirmationEnrollSystemPromptResult {
  kCanceled = 0,
  kAccepted = 1,
  kMaxValue = kAccepted,
};

void RecordEnrollSystemPromptResult(
    SecurePaymentConfirmationEnrollSystemPromptResult result);

// TODO(crbug.com/1183921): Move other SPC metrics into this common file.

}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CORE_SECURE_PAYMENT_CONFIRMATION_METRICS_H_