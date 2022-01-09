// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_PROFILE_KEY_STARTUP_ACCESSOR_H_
#define CHROME_BROWSER_ANDROID_PROFILE_KEY_STARTUP_ACCESSOR_H_

class ProfileKey;

// The ProfileKeyStartupAccessor is a singleton class that exposes the
// pointer of the ProfileKey of the associated Profile in the ServiceManager
// only mode on Android. On Android, there is only one Profile, thus it is
// possible to use this accessor to get the associated ProfileKey in the reduced
// mode.
//
// Note: after the Profile is created, the ProfileKey should be obtained from
// Profile.
class ProfileKeyStartupAccessor {
 public:
  ProfileKeyStartupAccessor();

  static ProfileKeyStartupAccessor* GetInstance();

  // The |key_| should NOT be used after Profile is created.
  ProfileKey* profile_key() { return key_; }
  void SetProfileKey(ProfileKey* key);

  // Resets the |key_| when the Profile is created.
  void Reset();

 private:
  ProfileKey* key_;
};

#endif  // CHROME_BROWSER_ANDROID_PROFILE_KEY_STARTUP_ACCESSOR_H_
