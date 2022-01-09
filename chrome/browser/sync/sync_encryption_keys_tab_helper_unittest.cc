// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/sync_encryption_keys_tab_helper.h"

#include "base/memory/scoped_refptr.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/test_signin_client_builder.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/common/sync_encryption_keys_extension.mojom.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using testing::IsNull;
using testing::NotNull;

class SyncEncryptionKeysTabHelperTest : public ChromeRenderViewHostTestHarness {
 public:
  SyncEncryptionKeysTabHelperTest(const SyncEncryptionKeysTabHelperTest&) =
      delete;
  SyncEncryptionKeysTabHelperTest& operator=(
      const SyncEncryptionKeysTabHelperTest&) = delete;

 protected:
  SyncEncryptionKeysTabHelperTest() = default;

  ~SyncEncryptionKeysTabHelperTest() override = default;

  // content::RenderViewHostTestHarness:
  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    SyncEncryptionKeysTabHelper::CreateForWebContents(web_contents());
  }

  bool IsEncryptionKeysApiBound() {
    auto* tab_helper =
        SyncEncryptionKeysTabHelper::FromWebContents(web_contents());
    return tab_helper->IsEncryptionKeysApiBoundForTesting();
  }

  content::WebContentsTester* web_contents_tester() {
    return content::WebContentsTester::For(web_contents());
  }

  TestingProfile::TestingFactories GetTestingFactories() const override {
    return {{SyncServiceFactory::GetInstance(),
             SyncServiceFactory::GetDefaultFactory()},
            {ChromeSigninClientFactory::GetInstance(),
             base::BindRepeating(&signin::BuildTestSigninClient)}};
  }
};

TEST_F(SyncEncryptionKeysTabHelperTest, ShouldExposeMojoApiToAllowedOrigin) {
  ASSERT_FALSE(IsEncryptionKeysApiBound());
  web_contents_tester()->NavigateAndCommit(GaiaUrls::GetInstance()->gaia_url());
  EXPECT_TRUE(IsEncryptionKeysApiBound());
}

TEST_F(SyncEncryptionKeysTabHelperTest,
       ShouldNotExposeMojoApiToUnallowedOrigin) {
  web_contents_tester()->NavigateAndCommit(GURL("http://page.com"));
  EXPECT_FALSE(IsEncryptionKeysApiBound());
}

TEST_F(SyncEncryptionKeysTabHelperTest, ShouldNotExposeMojoApiIfNavigatedAway) {
  web_contents_tester()->NavigateAndCommit(GaiaUrls::GetInstance()->gaia_url());
  ASSERT_TRUE(IsEncryptionKeysApiBound());
  web_contents_tester()->NavigateAndCommit(GURL("http://page.com"));
  EXPECT_FALSE(IsEncryptionKeysApiBound());
}

TEST_F(SyncEncryptionKeysTabHelperTest,
       ShouldExposeMojoApiEvenIfSubframeNavigatedAway) {
  web_contents_tester()->NavigateAndCommit(GaiaUrls::GetInstance()->gaia_url());
  content::RenderFrameHost* subframe =
      content::RenderFrameHostTester::For(main_rfh())->AppendChild("subframe");
  ASSERT_TRUE(IsEncryptionKeysApiBound());

  content::NavigationSimulator::CreateRendererInitiated(GURL("http://page.com"),
                                                        subframe)
      ->Commit();
  // For the receiver set to be fully updated, a mainframe navigation is needed.
  // Otherwise the test passes regardless of whether the logic is buggy.
  web_contents_tester()->NavigateAndCommit(GaiaUrls::GetInstance()->gaia_url());
  EXPECT_TRUE(IsEncryptionKeysApiBound());
}

TEST_F(SyncEncryptionKeysTabHelperTest,
       ShouldNotExposeMojoApiIfNavigationFailed) {
  web_contents_tester()->NavigateAndFail(GaiaUrls::GetInstance()->gaia_url(),
                                         net::ERR_ABORTED);
  EXPECT_FALSE(IsEncryptionKeysApiBound());
}

TEST_F(SyncEncryptionKeysTabHelperTest,
       ShouldNotExposeMojoApiIfNavigatedAwayToErrorPage) {
  web_contents_tester()->NavigateAndCommit(GaiaUrls::GetInstance()->gaia_url());
  ASSERT_TRUE(IsEncryptionKeysApiBound());
  web_contents_tester()->NavigateAndFail(GURL("http://page.com"),
                                         net::ERR_ABORTED);
  EXPECT_FALSE(IsEncryptionKeysApiBound());
}

}  // namespace
