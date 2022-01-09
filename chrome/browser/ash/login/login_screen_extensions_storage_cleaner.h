// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOGIN_LOGIN_SCREEN_EXTENSIONS_STORAGE_CLEANER_H_
#define CHROME_BROWSER_ASH_LOGIN_LOGIN_SCREEN_EXTENSIONS_STORAGE_CLEANER_H_

#include <memory>
#include <string>
#include <vector>

#include "components/prefs/pref_change_registrar.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

class PrefService;

namespace ash {

// Tracks changes to 'DeviceLoginScreenExtensions' policy and clears its data
// stored in the login screen storage whenever a login screen extension is
// removed.
class LoginScreenExtensionsStorageCleaner {
 public:
  LoginScreenExtensionsStorageCleaner();
  ~LoginScreenExtensionsStorageCleaner();

 private:
  // Called whenever the value of 'DeviceLoginScreenExtensions' policy is
  // updated.
  void OnPolicyUpdated();

  // Makes sure that persistent data in the login screen storage is only stored
  // for currently installed login screen extensions (so when an extension is
  // uninstalled its data would automatically be deleted).
  void ClearPersistentDataForUninstalledExtensions();

  void ClearPersistentDataForUninstalledExtensionsImpl(
      const std::vector<std::string>& installed_extension_ids,
      std::vector<std::string> keys,
      absl::optional<std::string> error);

  PrefService* prefs_;
  PrefChangeRegistrar pref_change_registrar_;
};

}  // namespace ash

#endif  // CHROME_BROWSER_ASH_LOGIN_LOGIN_SCREEN_EXTENSIONS_STORAGE_CLEANER_H_
