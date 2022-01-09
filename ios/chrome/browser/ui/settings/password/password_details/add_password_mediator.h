
// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_SETTINGS_PASSWORD_PASSWORD_DETAILS_ADD_PASSWORD_MEDIATOR_H_
#define IOS_CHROME_BROWSER_UI_SETTINGS_PASSWORD_PASSWORD_DETAILS_ADD_PASSWORD_MEDIATOR_H_

#import <Foundation/Foundation.h>

#import "ios/chrome/browser/ui/settings/password/password_details/password_details_table_view_controller_delegate.h"

@protocol AddPasswordDetailsConsumer;
@protocol AddPasswordMediatorDelegate;
class IOSChromePasswordCheckManager;

// This mediator stores logic for adding new password credentials.
@interface AddPasswordMediator
    : NSObject <PasswordDetailsTableViewControllerDelegate>

- (instancetype)initWithDelegate:(id<AddPasswordMediatorDelegate>)delegate
            passwordCheckManager:(IOSChromePasswordCheckManager*)manager
    NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;

// Consumer of this mediator.
@property(nonatomic, weak) id<AddPasswordDetailsConsumer> consumer;

@end

#endif  // IOS_CHROME_BROWSER_UI_SETTINGS_PASSWORD_PASSWORD_DETAILS_ADD_PASSWORD_MEDIATOR_H_
