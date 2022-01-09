// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_LACROS_PREFS_ASH_OBSERVER_H_
#define CHROME_BROWSER_LACROS_PREFS_ASH_OBSERVER_H_

#include <memory>

#include "base/gtest_prod_util.h"
#include "chrome/browser/lacros/crosapi_pref_observer.h"
#include "components/prefs/pref_service.h"

// Observes ash-chrome for changes in the secure DNS preferences.
class PrefsAshObserver {
 public:
  explicit PrefsAshObserver(PrefService* local_state);
  PrefsAshObserver(const PrefsAshObserver&) = delete;
  PrefsAshObserver& operator=(const PrefsAshObserver&) = delete;
  ~PrefsAshObserver();

  void Init();

 private:
  FRIEND_TEST_ALL_PREFIXES(PrefsAshObserver, LocalStateUpdatedOnChange);

  void OnDnsOverHttpsModeChanged(base::Value value);
  void OnDnsOverHttpsTemplatesChanged(base::Value value);

  PrefService* local_state_{nullptr};
  std::unique_ptr<CrosapiPrefObserver> doh_mode_observer_;
  std::unique_ptr<CrosapiPrefObserver> doh_templates_observer_;
};

#endif  // CHROME_BROWSER_LACROS_PREFS_ASH_OBSERVER_H_
