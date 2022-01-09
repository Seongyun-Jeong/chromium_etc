// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_PUBLIC_PROVIDER_CHROME_BROWSER_APP_DISTRIBUTION_TEST_APP_DISTRIBUTION_H_
#define IOS_PUBLIC_PROVIDER_CHROME_BROWSER_APP_DISTRIBUTION_TEST_APP_DISTRIBUTION_H_

namespace ios {
namespace provider {
namespace test {

// Returns whether ScheduleAppDistributionNotifications() has been called.
bool AreAppDistributionNotificationsScheduled();

// Returns whether CancelAppDistributionNotifications() has been called.
bool AreAppDistributionNotificationsCanceled();

// Reset the state used to tracked if ScheduleAppDistributionNotifications()
// and CancelAppDistributionNotifications() have been called.
void ResetAppDistributionNotificationsState();

}  // namespace test
}  // namespace provider
}  // namespace ios

#endif  // IOS_PUBLIC_PROVIDER_CHROME_BROWSER_APP_DISTRIBUTION_TEST_APP_DISTRIBUTION_H_
