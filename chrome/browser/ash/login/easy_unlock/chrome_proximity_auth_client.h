// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOGIN_EASY_UNLOCK_CHROME_PROXIMITY_AUTH_CLIENT_H_
#define CHROME_BROWSER_ASH_LOGIN_EASY_UNLOCK_CHROME_PROXIMITY_AUTH_CLIENT_H_

#include "ash/components/proximity_auth/proximity_auth_client.h"

class Profile;

namespace ash {

// A Chrome-specific implementation of the ProximityAuthClient interface.
// There is one `ChromeProximityAuthClient` per `Profile`.
class ChromeProximityAuthClient : public proximity_auth::ProximityAuthClient {
 public:
  explicit ChromeProximityAuthClient(Profile* profile);

  ChromeProximityAuthClient(const ChromeProximityAuthClient&) = delete;
  ChromeProximityAuthClient& operator=(const ChromeProximityAuthClient&) =
      delete;

  ~ChromeProximityAuthClient() override;

  // proximity_auth::ProximityAuthClient:
  void UpdateSmartLockState(SmartLockState state) override;
  void FinalizeUnlock(bool success) override;
  void FinalizeSignin(const std::string& secret) override;
  void GetChallengeForUserAndDevice(
      const std::string& user_email,
      const std::string& remote_public_key,
      const std::string& nonce,
      base::OnceCallback<void(const std::string& challenge)> callback) override;
  proximity_auth::ProximityAuthPrefManager* GetPrefManager() override;

 private:
  Profile* const profile_;
};

}  // namespace ash

#endif  // CHROME_BROWSER_ASH_LOGIN_EASY_UNLOCK_CHROME_PROXIMITY_AUTH_CLIENT_H_
