// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_OPTIMIZATION_GUIDE_CORE_HINTS_FETCHER_FACTORY_H_
#define COMPONENTS_OPTIMIZATION_GUIDE_CORE_HINTS_FETCHER_FACTORY_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "url/gurl.h"

class PrefService;

namespace network {
class NetworkConnectionTracker;
class SharedURLLoaderFactory;
}  // namespace network

namespace optimization_guide {

class HintsFetcher;

// A factory for creating hints fetchers. Mostly used so tests can override
// what fetchers get used.
class HintsFetcherFactory {
 public:
  HintsFetcherFactory(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      const GURL& optimization_guide_service_url,
      PrefService* pref_service,
      network::NetworkConnectionTracker* network_connection_tracker);
  HintsFetcherFactory(const HintsFetcherFactory&) = delete;
  HintsFetcherFactory& operator=(const HintsFetcherFactory&) = delete;
  virtual ~HintsFetcherFactory();

  // Creates a new instance of HintsFetcher. Virtualized for testing so that the
  // testing code can override this to provide a mocked instance.
  virtual std::unique_ptr<HintsFetcher> BuildInstance();

  // Override the optimization guide hints server URL. Used for testing.
  void OverrideOptimizationGuideServiceUrlForTesting(
      const GURL& optimization_guide_service_url);

 protected:
  // The URL Loader Factory that will be used by hints fetchers created by this
  // factory.
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  // The URL for the remote Optimization Guide Service.
  GURL optimization_guide_service_url_;

  // A reference to the PrefService for this profile. Not owned.
  raw_ptr<PrefService> pref_service_ = nullptr;

  // A reference to the object that listens for changes in network connection.
  // Not owned. Guaranteed to outlive |this|.
  raw_ptr<network::NetworkConnectionTracker> network_connection_tracker_;
};

}  // namespace optimization_guide

#endif  // COMPONENTS_OPTIMIZATION_GUIDE_CORE_HINTS_FETCHER_FACTORY_H_
