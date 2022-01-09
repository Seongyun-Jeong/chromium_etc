// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_UNIFIED_CONSENT_URL_KEYED_DATA_COLLECTION_CONSENT_HELPER_H_
#define COMPONENTS_UNIFIED_CONSENT_URL_KEYED_DATA_COLLECTION_CONSENT_HELPER_H_

#include <memory>

#include "base/observer_list.h"

class PrefService;
namespace syncer {
class SyncService;
}

namespace unified_consent {

// Helper class that allows clients to check whether the user has consented
// for URL-keyed data collection.
class UrlKeyedDataCollectionConsentHelper {
 public:
  class Observer {
   public:
    // Called when the state of the URL-keyed data collection changes.
    virtual void OnUrlKeyedDataCollectionConsentStateChanged(
        UrlKeyedDataCollectionConsentHelper* consent_helper) = 0;
  };

  // Creates a new |UrlKeyedDataCollectionConsentHelper| instance that checks
  // whether *anonymized* data collection is enabled. This should be used when
  // the client needs to check whether the user has granted consent for
  // *anonymized* URL-keyed data collection. It is enabled if the preference
  // |prefs::kUrlKeyedAnonymizedDataCollectionEnabled| from |pref_service| is
  // set to true.
  //
  // Note: |pref_service| must outlive the returned instance.
  static std::unique_ptr<UrlKeyedDataCollectionConsentHelper>
  NewAnonymizedDataCollectionConsentHelper(PrefService* pref_service);

  // Creates a new |UrlKeyedDataCollectionConsentHelper| instance that checks
  // whether *personalized* data collection is enabled. This should be used when
  // the client needs to check whether the user has granted consent for
  // URL-keyed data collection keyed by their Google account.
  //
  // Implementation-wise URL-keyed data collection is enabled if history sync
  // has an active upload state.
  static std::unique_ptr<UrlKeyedDataCollectionConsentHelper>
  NewPersonalizedDataCollectionConsentHelper(syncer::SyncService* sync_service);

  UrlKeyedDataCollectionConsentHelper(
      const UrlKeyedDataCollectionConsentHelper&) = delete;
  UrlKeyedDataCollectionConsentHelper& operator=(
      const UrlKeyedDataCollectionConsentHelper&) = delete;

  virtual ~UrlKeyedDataCollectionConsentHelper();

  // Returns true if the user has consented for URL keyed anonymized data
  // collection.
  virtual bool IsEnabled() = 0;

  // Methods to register or remove observers.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 protected:
  UrlKeyedDataCollectionConsentHelper();

  // Fires |OnUrlKeyedDataCollectionConsentStateChanged| on all the observers.
  void FireOnStateChanged();

 private:
  base::ObserverList<Observer, true>::Unchecked observer_list_;
};

}  // namespace unified_consent

#endif  // COMPONENTS_UNIFIED_CONSENT_URL_KEYED_DATA_COLLECTION_CONSENT_HELPER_H_
