// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SAFE_BROWSING_CONTENT_BROWSER_CLIENT_SIDE_DETECTION_HOST_H_
#define COMPONENTS_SAFE_BROWSING_CONTENT_BROWSER_CLIENT_SIDE_DETECTION_HOST_H_

#include <stddef.h>

#include <memory>
#include <string>

#include "base/containers/flat_map.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "components/safe_browsing/content/browser/base_ui_manager.h"
#include "components/safe_browsing/content/common/safe_browsing.mojom-shared.h"
#include "components/safe_browsing/content/common/safe_browsing.mojom.h"
#include "components/safe_browsing/core/browser/db/database_manager.h"
#include "components/safe_browsing/core/browser/safe_browsing_token_fetcher.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "url/gurl.h"

namespace base {
class TickClock;
}

namespace safe_browsing {
class ClientPhishingRequest;
class ClientSideDetectionService;

// This class is used to receive the IPC from the renderer which
// notifies the browser that a URL was classified as phishing.  This
// class relays this information to the client-side detection service
// class which sends a ping to a server to validate the verdict.
// TODO(noelutz): move all client-side detection IPCs to this class.
class ClientSideDetectionHost : public content::WebContentsObserver {
 public:
  // A callback via which the client of this component indicates whether the
  // primary account is signed in.
  using PrimaryAccountSignedIn = base::RepeatingCallback<bool()>;

  // Delegate which allows to provide embedder specific implementations.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Returns whether there is a SafeBrowsingUserInteractionObserver available.
    virtual bool HasSafeBrowsingUserInteractionObserver() = 0;
    // Returns the prefs service associated with the current embedders profile.
    virtual PrefService* GetPrefs() = 0;
    virtual scoped_refptr<SafeBrowsingDatabaseManager>
    GetSafeBrowsingDBManager() = 0;
    virtual scoped_refptr<BaseUIManager> GetSafeBrowsingUIManager() = 0;
    virtual ClientSideDetectionService* GetClientSideDetectionService() = 0;
    virtual void AddReferrerChain(ClientPhishingRequest* verdict,
                                  GURL current_url) = 0;
  };

  // The caller keeps ownership of the tab object and is responsible for
  // ensuring that it stays valid until WebContentsDestroyed is called.
  // The caller also keeps ownership of pref_service. The
  // ClientSideDetectionHost takes ownership of token_fetcher. is_off_the_record
  // indicates if the profile is incognito, and account_signed_in_callback is
  // checked to find out if primary account is signed in.
  static std::unique_ptr<ClientSideDetectionHost> Create(
      content::WebContents* tab,
      std::unique_ptr<Delegate> delegate,
      PrefService* pref_service,
      std::unique_ptr<SafeBrowsingTokenFetcher> token_fetcher,
      bool is_off_the_record,
      const PrimaryAccountSignedIn& account_signed_in_callback);

  ClientSideDetectionHost(const ClientSideDetectionHost&) = delete;
  ClientSideDetectionHost& operator=(const ClientSideDetectionHost&) = delete;

  // The caller keeps ownership of the tab object and is responsible for
  // ensuring that it stays valid until WebContentsDestroyed is called.
  ~ClientSideDetectionHost() override;

  // From content::WebContentsObserver.  If we navigate away we cancel all
  // pending callbacks that could show an interstitial, and check to see whether
  // we should classify the new URL.
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // Send the model to all the render frame hosts in this WebContents.
  void SendModelToRenderFrame();

 protected:
  explicit ClientSideDetectionHost(
      content::WebContents* tab,
      std::unique_ptr<Delegate> delegate,
      PrefService* pref_service,
      std::unique_ptr<SafeBrowsingTokenFetcher> token_fetcher,
      bool is_off_the_record,
      const PrimaryAccountSignedIn& account_signed_in_callback);

  // From content::WebContentsObserver.
  void WebContentsDestroyed() override;
  void RenderFrameCreated(content::RenderFrameHost* render_frame_host) override;
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;

  // Used for testing.
  void set_ui_manager(BaseUIManager* ui_manager);
  void set_database_manager(SafeBrowsingDatabaseManager* database_manager);

 private:
  friend class ClientSideDetectionHostTestBase;
  class ShouldClassifyUrlRequest;
  friend class ShouldClassifyUrlRequest;
  FRIEND_TEST_ALL_PREFIXES(ClientSideDetectionHostBrowserTest,
                           VerifyVisualFeatureCollection);
  FRIEND_TEST_ALL_PREFIXES(ClientSideDetectionHostPrerenderBrowserTest,
                           PrerenderShouldNotAffectClientSideDetection);
  FRIEND_TEST_ALL_PREFIXES(ClientSideDetectionHostPrerenderBrowserTest,
                           ClassifyPrerenderedPageAfterActivation);

  // Called when pre-classification checks are done for the phishing
  // classifiers.
  void OnPhishingPreClassificationDone(bool should_classify);

  // |verdict| is an encoded ClientPhishingRequest protocol message, |result|
  // is the outcome of the renderer classification.
  void PhishingDetectionDone(mojom::PhishingDetectorResult result,
                             const std::string& verdict);

  // Callback that is called when the server ping back is
  // done. Display an interstitial if |is_phishing| is true.
  // Otherwise, we do nothing. Called in UI thread. |is_from_cache| indicates
  // whether the warning is being shown due to a cached verdict or from an
  // actual server ping.
  void MaybeShowPhishingWarning(bool is_from_cache,
                                GURL phishing_url,
                                bool is_phishing);

  // Used for testing.  This function does not take ownership of the service
  // class.
  void set_client_side_detection_service(ClientSideDetectionService* service);

  // Sets a test tick clock only for testing.
  void set_tick_clock_for_testing(const base::TickClock* tick_clock) {
    tick_clock_ = tick_clock;
  }

  // Sets the token fetcher only for testing.
  void set_token_fetcher_for_testing(
      std::unique_ptr<SafeBrowsingTokenFetcher> token_fetcher) {
    token_fetcher_ = std::move(token_fetcher);
  }

  // Sets the incognito bit only for testing.
  void set_is_off_the_record_for_testing(bool is_off_the_record) {
    is_off_the_record_ = is_off_the_record;
  }

  // Sets the primary account signed in callback for testing.
  void set_account_signed_in_for_testing(
      const PrimaryAccountSignedIn& account_signed_in_callback) {
    account_signed_in_callback_ = account_signed_in_callback;
  }

  // Check if CSD can get an access Token. Should be enabled only for ESB
  // users, who are signed in and not in incognito mode.
  bool CanGetAccessToken();

  // Set phishing model in PhishingDetector in renderers.
  void SetPhishingModel(
      const mojo::Remote<mojom::PhishingDetector>& phishing_detector);

  // Send the client report to CSD server.
  void SendRequest(std::unique_ptr<ClientPhishingRequest> verdict,
                   const std::string& access_token);

  // Called when token_fetcher_ has fetched the token.
  void OnGotAccessToken(std::unique_ptr<ClientPhishingRequest> verdict,
                        const std::string& access_token);

  // Setup a PhishingDetector Mojo connection for the given render frame.
  void InitializePhishingDetector(content::RenderFrameHost* render_frame_host);

  // This pointer may be nullptr if client-side phishing detection is
  // disabled.
  raw_ptr<ClientSideDetectionService> csd_service_;
  // The WebContents that the class is observing.
  raw_ptr<content::WebContents> tab_;
  // These pointers may be nullptr if SafeBrowsing is disabled.
  scoped_refptr<SafeBrowsingDatabaseManager> database_manager_;
  scoped_refptr<BaseUIManager> ui_manager_;
  // Keep a handle to the latest classification request so that we can cancel
  // it if necessary.
  scoped_refptr<ShouldClassifyUrlRequest> classification_request_;
  // The current URL
  GURL current_url_;
  // A map from the live RenderFrameHosts to their PhishingDetector. These
  // correspond to the `phishing_detector_receiver_` in the
  // PhishingClassifierDelegate.
  base::flat_map<content::RenderFrameHost*,
                 mojo::Remote<mojom::PhishingDetector>>
      phishing_detectors_;

  // Records the start time of when phishing detection started.
  base::TimeTicks phishing_detection_start_time_;
  raw_ptr<const base::TickClock> tick_clock_;

  std::unique_ptr<Delegate> delegate_;

  // Unowned object used for getting preference settings.
  raw_ptr<PrefService> pref_service_;

  // The token fetcher used for getting access token.
  std::unique_ptr<SafeBrowsingTokenFetcher> token_fetcher_;

  // A boolean indicates whether the associated profile associated is an
  // incognito profile.
  bool is_off_the_record_;

  // Callback for checking if the user is signed in, before fetching
  // acces_token.
  PrimaryAccountSignedIn account_signed_in_callback_;

  base::WeakPtrFactory<ClientSideDetectionHost> weak_factory_{this};
};

}  // namespace safe_browsing

#endif  // COMPONENTS_SAFE_BROWSING_CONTENT_BROWSER_CLIENT_SIDE_DETECTION_HOST_H_
