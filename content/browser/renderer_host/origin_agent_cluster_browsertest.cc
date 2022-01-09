// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/render_frame_host_impl.h"

#include "base/test/scoped_feature_list.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/content_client.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_base.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/content_mock_cert_verifier.h"
#include "content/shell/browser/shell.h"
#include "content/test/content_browser_test_utils_internal.h"
#include "content/test/test_content_browser_client.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace content {

namespace {

class MockContentBrowserClient : public ContentBrowserClient {
 public:
  bool ShouldDisableOriginAgentClusterDefault(BrowserContext*) override {
    return should_disable_origin_agent_cluster_default_;
  }

  bool should_disable_origin_agent_cluster_default_ = false;
};

}  // anonymous namespace

// Test the effect of the OriginAgentCluster: header on document.domain
// settability and how it (doesn't) affect process assignment.
class OriginAgentClusterBrowserTest : public ContentBrowserTest {
 public:
  OriginAgentClusterBrowserTest()
      : OriginAgentClusterBrowserTest(false, false) {}
  ~OriginAgentClusterBrowserTest() override = default;

  void SetUp() override {
    mock_cert_verifier_.SetUpCommandLine(
        base::CommandLine::ForCurrentProcess());

    // SetUp gets called before the test body, which is why we have to
    // enable/disable the feature awkwardly through the constructor, instead
    // of having a more straightforward setup call in the test body.
    std::vector<base::Feature> enabled, disabled;
    (origin_cluster_default_enabled_ ? enabled : disabled)
        .push_back(blink::features::kOriginAgentClusterDefaultEnabled);
    (origin_cluster_absent_warning_ ? enabled : disabled)
        .push_back(blink::features::kOriginAgentClusterDefaultWarning);
    features_.InitWithFeatures(enabled, disabled);

    ContentBrowserTest::SetUp();
  }

 protected:
  enum OriginAgentClusterState { kUnset, kSetTrue, kSetFalse, kMalformed };

  OriginAgentClusterBrowserTest(bool origin_cluster_default_enabled,
                                bool origin_cluster_absent_warning)
      : server_(net::EmbeddedTestServer::TYPE_HTTPS),
        origin_cluster_default_enabled_(origin_cluster_default_enabled),
        origin_cluster_absent_warning_(origin_cluster_absent_warning) {
    server_.AddDefaultHandlers(GetTestDataFilePath());
  }

  void SetUpOnMainThread() override {
    mock_cert_verifier_.mock_cert_verifier()->set_default_result(net::OK);
    host_resolver()->AddRule("*", "127.0.0.1");
    SetupCrossSiteRedirector(server());
    ASSERT_TRUE(server()->Start());
    original_browser_client_ = SetBrowserClientForTesting(&browser_client_);
  }

  void TearDownOnMainThread() override {
    SetBrowserClientForTesting(original_browser_client_);
    original_browser_client_ = nullptr;
  }

  void SetUpInProcessBrowserTestFixture() override {
    ContentBrowserTest::SetUpInProcessBrowserTestFixture();
    mock_cert_verifier_.SetUpInProcessBrowserTestFixture();
  }

  void TearDownInProcessBrowserTestFixture() override {
    ContentBrowserTest::TearDownInProcessBrowserTestFixture();
    mock_cert_verifier_.TearDownInProcessBrowserTestFixture();
  }

  net::EmbeddedTestServer* server() { return &server_; }

  RenderProcessHost* NavigateAndGetProcess(const char* domain,
                                           OriginAgentClusterState oac_state) {
    DCHECK(domain);
    WebContents* contents = Navigate(domain, oac_state);
    DCHECK(contents);
    return static_cast<WebContentsImpl*>(contents)
        ->GetMainFrame()
        ->GetProcess();
  }

  bool CanDocumentDomain(std::string from,
                         std::string to,
                         OriginAgentClusterState oac_state) {
    WebContents* contents = Navigate(from, oac_state);
    DCHECK(contents);
    return EvalJs(contents, SetDocumentDomainTo(to)).ExtractBool();
  }

  // This tries to set document.domain. But instead of checking whether setting
  // succeeded (which is what CanDocumentDomain above does), it checks whether
  // a warning message is posted to the console.
  bool CanDocumentDomainMessage(std::string from,
                                std::string to,
                                OriginAgentClusterState oac_state) {
    WebContents* contents = Navigate(from, oac_state);
    DCHECK(contents);

    WebContentsConsoleObserver console(contents);
    console.SetPattern("document.domain mutation is ignored*");
    CHECK(ExecJs(contents, SetDocumentDomainTo(to)));
    return !console.messages().size();
  }

  // Simulate setting the OriginAgentClusterDefaultEnabled enterprise policy.
  void SetEnterprisePolicy(bool value) {
    // Note that the enterprise policy has different 'polarity', and true
    // means Chromium picks the default and false is legacy behaviour, while
    // ContentBrowserClientShould::DisableOriginAgentClusterDefault is a
    // disable switch, meaning that false means Chromium picks the default
    // and true is legacy behaviour.
    browser_client_.should_disable_origin_agent_cluster_default_ = !value;
  }

 private:
  std::string SetDocumentDomainTo(std::string to) const {
    // Assign document.domain and check whether it changed.
    // Wrap the statement in a try-catch, since since document.domain setting
    // may throw.
    return JsReplace(
        "try { "
        "document.domain = $1; "
        "document.domain == $1; "
        "} catch (e) { false; }",
        to);
  }

  WebContents* Navigate(std::string domain, std::string path) {
    GURL url(server()->GetURL(domain, path));
    EXPECT_TRUE(NavigateToURL(shell(), url));
    return shell()->web_contents();
  }

  // For the purpose of this test, we only care about the Origin-Agent-Cluster:
  // header. The test setup has four pages corresponding to the three valid
  // states (absent, true ("?1"), and false ("?0"), and one malformed one
  // ("?2"). For ease of use, we have an enum to designate the appropriate
  // test page.
  WebContents* Navigate(std::string domain,
                        const OriginAgentClusterState state) {
    switch (state) {
      case kUnset:
        return Navigate(domain, "/empty.html");
      case kSetTrue:
        return Navigate(domain, "/set-header?Origin-Agent-Cluster: ?1");
      case kSetFalse:
        return Navigate(domain, "/set-header?Origin-Agent-Cluster: ?0");
      case kMalformed:
        return Navigate(domain, "/set-header?Origin-Agent-Cluster: potato");
    }
  }

 protected:
  // https:-embedded test server.
  // The BrowserTestBase::embedded_test_server_ is a private member and is
  // constructed as http:-only, and so we cannot change or replace it.
  // The setup of server_ emulates that of embedded_test_server_.
  net::EmbeddedTestServer server_;
  content::ContentMockCertVerifier mock_cert_verifier_;

  MockContentBrowserClient browser_client_;
  ContentBrowserClient* original_browser_client_ = nullptr;

  const bool origin_cluster_default_enabled_;
  const bool origin_cluster_absent_warning_;
  base::test::ScopedFeatureList features_;
};

// Test fixture wih the default behaviour change enabled.
// (blink::features::kOriginAgentClusterDefaultEnabled)
class OriginAgentClusterEnabledBrowserTest
    : public OriginAgentClusterBrowserTest {
 public:
  OriginAgentClusterEnabledBrowserTest()
      : OriginAgentClusterBrowserTest(true, false) {}
  ~OriginAgentClusterEnabledBrowserTest() override = default;
};

// Test fixture wih the deprecation warning enabled.
// (blink::features::kOriginAgentClusterDefaultWarning)
class OriginAgentClusterWarningBrowserTest
    : public OriginAgentClusterBrowserTest {
 public:
  OriginAgentClusterWarningBrowserTest()
      : OriginAgentClusterBrowserTest(false, true) {}
  ~OriginAgentClusterWarningBrowserTest() override = default;
};

// DocumentDomain: Can we set document.domain?
//
// Tests are for each Origin-Agent-Cluster: header state
// (enabled/disabled/default/malformed), and flag being enabled/disabled.
//
// These tests ensure that the flag will change the default behaviour only.

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest, DocumentDomain_Default) {
  EXPECT_TRUE(CanDocumentDomain("a.domain.test", "domain.test", kUnset));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest, DocumentDomain_Enabled) {
  EXPECT_FALSE(CanDocumentDomain("a.domain.test", "domain.test", kSetTrue));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest, DocumentDomain_Disabled) {
  EXPECT_TRUE(CanDocumentDomain("a.domain.test", "domain.test", kSetFalse));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest,
                       DocumentDomain_Malformed) {
  EXPECT_TRUE(CanDocumentDomain("a.domain.test", "domain.test", kMalformed));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       DocumentDomain_Default) {
  EXPECT_FALSE(CanDocumentDomain("a.domain.test", "domain.test", kUnset));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       DocumentDomain_Enabled) {
  EXPECT_FALSE(CanDocumentDomain("a.domain.test", "domain.test", kSetTrue));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       DocumentDomain_Disabled) {
  EXPECT_TRUE(CanDocumentDomain("a.domain.test", "domain.test", kSetFalse));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       DocumentDomain_Malformed) {
  EXPECT_FALSE(CanDocumentDomain("a.domain.test", "domain.test", kMalformed));
}

// Process: Will two pages (same site, different origin) be assigned to the
// same process?
//
// Tests are for each Origin-Agent-Cluster: header state
// (enabled/disabled/default/malformed), and the flag being enabled/disabled.
//
// These tests mainly ensure that the enabled-flag will not actually change
// this behaviour, since we use same-process clustering. (Unlike some earlier
// plans, where we were trying to change the process model as well.)

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest, SameProcess_Default) {
  EXPECT_EQ(NavigateAndGetProcess("a.domain.test", kUnset),
            NavigateAndGetProcess("b.domain.test", kUnset));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest, SameProcess_Enabled) {
  EXPECT_NE(NavigateAndGetProcess("a.domain.test", kSetTrue),
            NavigateAndGetProcess("b.domain.test", kSetTrue));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest, SameProcess_Disabled) {
  EXPECT_EQ(NavigateAndGetProcess("a.domain.test", kSetFalse),
            NavigateAndGetProcess("b.domain.test", kSetFalse));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest, SameProcess_Malformed) {
  EXPECT_EQ(NavigateAndGetProcess("a.domain.test", kMalformed),
            NavigateAndGetProcess("b.domain.test", kMalformed));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       SameProcess_Default) {
  EXPECT_EQ(NavigateAndGetProcess("a.domain.test", kUnset),
            NavigateAndGetProcess("b.domain.test", kUnset));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       SameProcess_Enabled) {
  EXPECT_NE(NavigateAndGetProcess("a.domain.test", kSetTrue),
            NavigateAndGetProcess("b.domain.test", kSetTrue));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       SameProcess_Disabled) {
  EXPECT_EQ(NavigateAndGetProcess("a.domain.test", kSetFalse),
            NavigateAndGetProcess("b.domain.test", kSetFalse));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       SameProcess_Malformed) {
  EXPECT_EQ(NavigateAndGetProcess("a.domain.test", kMalformed),
            NavigateAndGetProcess("b.domain.test", kMalformed));
}

// WarningMessage: Test whether setting document.domain triggers a console
// message, for each Origin-Agent-Cluster: header state
// (enabled/disabled/default/malformed), and each flag (none/enable/message).

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest, WarningMessage_Default) {
  EXPECT_TRUE(CanDocumentDomainMessage("a.domain.test", "domain.test", kUnset));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest, WarningMessage_Enabled) {
  EXPECT_FALSE(
      CanDocumentDomainMessage("a.domain.test", "domain.test", kSetTrue));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest, WarningMessage_Disabled) {
  EXPECT_TRUE(
      CanDocumentDomainMessage("a.domain.test", "domain.test", kSetFalse));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterBrowserTest,
                       WarningMessage_Malformed) {
  EXPECT_TRUE(
      CanDocumentDomainMessage("a.domain.test", "domain.test", kMalformed));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       WarningMessage_Default) {
  EXPECT_FALSE(
      CanDocumentDomainMessage("a.domain.test", "domain.test", kUnset));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       WarningMessage_Enabled) {
  EXPECT_FALSE(
      CanDocumentDomainMessage("a.domain.test", "domain.test", kSetTrue));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       WarningMessage_Disabled) {
  EXPECT_TRUE(
      CanDocumentDomainMessage("a.domain.test", "domain.test", kSetFalse));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       WarningMessage_Malformed) {
  EXPECT_FALSE(
      CanDocumentDomainMessage("a.domain.test", "domain.test", kMalformed));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterWarningBrowserTest,
                       WarningMessage_Default) {
  EXPECT_TRUE(CanDocumentDomainMessage("a.domain.test", "domain.test", kUnset));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterWarningBrowserTest,
                       WarningMessage_Enabled) {
  EXPECT_FALSE(
      CanDocumentDomainMessage("a.domain.test", "domain.test", kSetTrue));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterWarningBrowserTest,
                       WarningMessage_Disabled) {
  EXPECT_TRUE(
      CanDocumentDomainMessage("a.domain.test", "domain.test", kSetFalse));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterWarningBrowserTest,
                       WarningMessage_Malformed) {
  EXPECT_TRUE(
      CanDocumentDomainMessage("a.domain.test", "domain.test", kMalformed));
}

// Policy: Ensure that the legacy behaviour remains if the appropriate
// enterprise policy is set.
//
// (The case without policy is adequately covered by the tests above, since
// none of them modify the policy.)

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       Policy_SetTrue_Default) {
  SetEnterprisePolicy(false);
  EXPECT_TRUE(CanDocumentDomain("a.domain.test", "domain.test", kUnset));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       Policy_SetFalse_Default) {
  SetEnterprisePolicy(true);
  EXPECT_FALSE(CanDocumentDomain("a.domain.test", "domain.test", kUnset));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       Policy_SetTrue_Enabled) {
  SetEnterprisePolicy(false);
  EXPECT_FALSE(CanDocumentDomain("a.domain.test", "domain.test", kSetTrue));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       Policy_SetFalse_Enabled) {
  SetEnterprisePolicy(true);
  EXPECT_FALSE(CanDocumentDomain("a.domain.test", "domain.test", kSetTrue));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       Policy_SetTrue_Disabled) {
  SetEnterprisePolicy(false);
  EXPECT_TRUE(CanDocumentDomain("a.domain.test", "domain.test", kSetFalse));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       Policy_SetFalse_Disabled) {
  SetEnterprisePolicy(true);
  EXPECT_TRUE(CanDocumentDomain("a.domain.test", "domain.test", kSetFalse));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       Policy_SetTrue_Malformed) {
  SetEnterprisePolicy(false);
  EXPECT_TRUE(CanDocumentDomain("a.domain.test", "domain.test", kMalformed));
}

IN_PROC_BROWSER_TEST_F(OriginAgentClusterEnabledBrowserTest,
                       Policy_SetFalse_Malformed) {
  SetEnterprisePolicy(true);
  EXPECT_FALSE(CanDocumentDomain("a.domain.test", "domain.test", kMalformed));
}
}  // namespace content
