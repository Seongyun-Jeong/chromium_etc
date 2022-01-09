// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOGIN_QUICK_UNLOCK_QUICK_UNLOCK_STORAGE_H_
#define CHROME_BROWSER_ASH_LOGIN_QUICK_UNLOCK_QUICK_UNLOCK_STORAGE_H_

#include "ash/components/login/auth/user_context.h"
#include "base/time/default_clock.h"
#include "base/time/time.h"
#include "components/keyed_service/core/keyed_service.h"

class Profile;

namespace ash {
enum class FingerprintState;

namespace quick_unlock {
class AuthToken;
class FingerprintStorage;
class PinStoragePrefs;

// Helper class for managing state for quick unlock services (pin and
// fingerprint), and general lock screen management (tokens for extension API
// authentication used by Settings).
class QuickUnlockStorage : public KeyedService {
 public:
  explicit QuickUnlockStorage(Profile* profile);

  QuickUnlockStorage(const QuickUnlockStorage&) = delete;
  QuickUnlockStorage& operator=(const QuickUnlockStorage&) = delete;

  ~QuickUnlockStorage() override;

  // Replaces default clock with a test clock for testing.
  void SetClockForTesting(base::Clock* clock);

  // Mark that the user has had a strong authentication. This means
  // that they authenticated with their password, for example. Quick
  // unlock will timeout after a delay.
  void MarkStrongAuth();

  // Returns true if the user has been strongly authenticated.
  bool HasStrongAuth() const;

  // Returns the time when next strong authentication is required. This should
  // not be called if HasStrongAuth returns false.
  base::Time TimeOfNextStrongAuth() const;

  // Returns true if fingerprint unlock is currently available.
  // This checks whether there's fingerprint setup, as well as HasStrongAuth.
  bool IsFingerprintAuthenticationAvailable() const;

  // Returns true if PIN unlock is currently available.
  bool IsPinAuthenticationAvailable() const;

  // Tries to authenticate the given pin. This will consume a pin unlock
  // attempt. This always returns false if HasStrongAuth returns false.
  bool TryAuthenticatePin(const Key& key);

  // Creates a new authentication token to be used by the quickSettingsPrivate
  // API for authenticating requests. Resets the expiration timer and
  // invalidates any previously issued tokens.
  std::string CreateAuthToken(const UserContext& user_context);

  // Returns true if the current authentication token has expired.
  bool GetAuthTokenExpired();

  // Returns the auth token if it is valid or nullptr if it is expired or has
  // not been created. May return nullptr.
  AuthToken* GetAuthToken();

  // Fetch the user context if `auth_token` is valid. May return null.
  const UserContext* GetUserContext(const std::string& auth_token);

  // Determines the fingerprint state. This is called at lock screen
  // initialization or after the fingerprint sensor has restarted.
  FingerprintState GetFingerprintState();

  FingerprintStorage* fingerprint_storage() {
    return fingerprint_storage_.get();
  }

  // Fetch the underlying pref pin storage. If iteracting with pin generally,
  // use the PinBackend APIs.
  PinStoragePrefs* pin_storage_prefs() { return pin_storage_prefs_.get(); }

 private:
  friend class QuickUnlockStorageTestApi;
  friend class QuickUnlockStorageUnitTest;

  // KeyedService:
  void Shutdown() override;

  Profile* const profile_;
  base::Time last_strong_auth_;
  std::unique_ptr<AuthToken> auth_token_;
  base::Clock* clock_;
  std::unique_ptr<FingerprintStorage> fingerprint_storage_;
  std::unique_ptr<PinStoragePrefs> pin_storage_prefs_;
};

}  // namespace quick_unlock
}  // namespace ash

// TODO(https://crbug.com/1164001): remove after the //chrome/browser/chromeos
// source migration is finished.
namespace chromeos {
namespace quick_unlock {
using ::ash::quick_unlock::QuickUnlockStorage;
}
}  // namespace chromeos

#endif  // CHROME_BROWSER_ASH_LOGIN_QUICK_UNLOCK_QUICK_UNLOCK_STORAGE_H_
