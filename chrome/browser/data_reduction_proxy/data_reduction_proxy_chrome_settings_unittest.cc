// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/data_reduction_proxy/data_reduction_proxy_chrome_settings.h"

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/test/metrics/histogram_tester.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_test_utils.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "content/public/test/mock_navigation_handle.h"
#include "net/http/http_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
using testing::_;
using testing::Return;

constexpr char kProxyPac[] = "PROXY proxy.net";
}  // namespace

class DataReductionProxyChromeSettingsTest
    : public ChromeRenderViewHostTestHarness {
 public:
  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    auto settings = std::make_unique<DataReductionProxyChromeSettings>(false);
    drp_chrome_settings_ = settings.get();
    test_context_ =
        data_reduction_proxy::DataReductionProxyTestContext::Builder()
            .WithMockConfig()
            .WithSettings(std::move(settings))
            .Build();
    net::ProxyList proxies;
    proxies.SetFromPacString(kProxyPac);
    dict_ = std::make_unique<base::DictionaryValue>();

    PrefRegistrySimple* registry = test_context_->pref_service()->registry();
    registry->RegisterDictionaryPref(proxy_config::prefs::kProxy);
  }

  void TearDown() override {
    // Make sure |test_context_| is destroyed before message loop is destroyed.
    test_context_.reset();
    ChromeRenderViewHostTestHarness::TearDown();
  }

  raw_ptr<DataReductionProxyChromeSettings> drp_chrome_settings_;
  std::unique_ptr<base::DictionaryValue> dict_;
  std::unique_ptr<data_reduction_proxy::DataReductionProxyTestContext>
      test_context_;
};

TEST_F(DataReductionProxyChromeSettingsTest, MigrateNonexistentProxyPref) {
  base::HistogramTester histogram_tester;
  drp_chrome_settings_->MigrateDataReductionProxyOffProxyPrefs(
      test_context_->pref_service());

  EXPECT_EQ(NULL, test_context_->pref_service()->GetUserPref(
                      proxy_config::prefs::kProxy));
  histogram_tester.ExpectUniqueSample(
      "DataReductionProxy.ProxyPrefMigrationResult",
      DataReductionProxyChromeSettings::PROXY_PREF_NOT_CLEARED, 1);
}

TEST_F(DataReductionProxyChromeSettingsTest, MigrateBadlyFormedProxyPref) {
  const struct {
    // NULL indicates that mode is unset.
    const char* proxy_mode_string;
    // NULL indicates that server is unset.
    const char* proxy_server_string;
  } test_cases[] = {
      // The pref should not be cleared if mode is unset.
      {nullptr, "http=compress.googlezip.net"},
      // The pref should not be cleared for modes other than "fixed_servers" and
      // "pac_script".
      {"auto_detect", "http=compress.googlezip.net"},
      // The pref should not be cleared when the server field is unset.
      {"fixed_servers", nullptr},
  };

  for (const auto& test : test_cases) {
    base::HistogramTester histogram_tester;
    dict_ = std::make_unique<base::DictionaryValue>();
    if (test.proxy_mode_string)
      dict_->SetString("mode", test.proxy_mode_string);
    if (test.proxy_server_string)
      dict_->SetString("server", test.proxy_server_string);
    test_context_->pref_service()->Set(proxy_config::prefs::kProxy,
                                       *dict_.get());

    drp_chrome_settings_->MigrateDataReductionProxyOffProxyPrefs(
        test_context_->pref_service());

    const base::DictionaryValue* final_value;
    test_context_->pref_service()
        ->GetUserPref(proxy_config::prefs::kProxy)
        ->GetAsDictionary(&final_value);
    EXPECT_NE(nullptr, final_value);
    EXPECT_TRUE(dict_->Equals(final_value));

    histogram_tester.ExpectUniqueSample(
        "DataReductionProxy.ProxyPrefMigrationResult",
        DataReductionProxyChromeSettings::PROXY_PREF_NOT_CLEARED, 1);
  }
}

TEST_F(DataReductionProxyChromeSettingsTest, MigrateEmptyProxy) {
  base::HistogramTester histogram_tester;
  test_context_->pref_service()->Set(proxy_config::prefs::kProxy, *dict_.get());
  drp_chrome_settings_->MigrateDataReductionProxyOffProxyPrefs(
      test_context_->pref_service());

  EXPECT_EQ(NULL, test_context_->pref_service()->GetUserPref(
                      proxy_config::prefs::kProxy));
  histogram_tester.ExpectUniqueSample(
      "DataReductionProxy.ProxyPrefMigrationResult",
      DataReductionProxyChromeSettings::PROXY_PREF_CLEARED_EMPTY, 1);
}

TEST_F(DataReductionProxyChromeSettingsTest, MigrateSystemProxy) {
  base::HistogramTester histogram_tester;
  dict_->SetString("mode", "system");
  test_context_->pref_service()->Set(proxy_config::prefs::kProxy, *dict_.get());

  drp_chrome_settings_->MigrateDataReductionProxyOffProxyPrefs(
      test_context_->pref_service());

  EXPECT_EQ(NULL, test_context_->pref_service()->GetUserPref(
                      proxy_config::prefs::kProxy));
  histogram_tester.ExpectUniqueSample(
      "DataReductionProxy.ProxyPrefMigrationResult",
      DataReductionProxyChromeSettings::PROXY_PREF_CLEARED_MODE_SYSTEM, 1);
}

TEST_F(DataReductionProxyChromeSettingsTest,
       MigrateGooglezipDataReductionProxy) {
  const std::string kTestServers[] = {
      "http=http://proxy-dev.googlezip.net",
      "http=https://arbitraryprefix.googlezip.net",
      "https=https://tunnel.googlezip.net"};

  for (const std::string& test_server : kTestServers) {
    base::HistogramTester histogram_tester;
    dict_ = std::make_unique<base::DictionaryValue>();
    // The proxy pref is set to a Data Reduction Proxy that doesn't match the
    // currently configured DRP, but the pref should still be cleared.
    dict_->SetString("mode", "fixed_servers");
    dict_->SetString("server", test_server);
    test_context_->pref_service()->Set(proxy_config::prefs::kProxy,
                                       *dict_.get());
    drp_chrome_settings_->MigrateDataReductionProxyOffProxyPrefs(
        test_context_->pref_service());

    EXPECT_EQ(NULL, test_context_->pref_service()->GetUserPref(
                        proxy_config::prefs::kProxy));
    histogram_tester.ExpectUniqueSample(
        "DataReductionProxy.ProxyPrefMigrationResult",
        DataReductionProxyChromeSettings::PROXY_PREF_CLEARED_GOOGLEZIP, 1);
  }
}

TEST_F(DataReductionProxyChromeSettingsTest,
       MigratePacGooglezipDataReductionProxy) {
  const struct {
    const char* pac_url;
    bool expect_pref_cleared;
  } test_cases[] = {
      // PAC with bypass rules that returns 'HTTPS proxy.googlezip.net:443;
      // PROXY compress.googlezip.net:80; DIRECT'.
      {"data:application/"
       "x-ns-proxy-autoconfig;base64,"
       "ZnVuY3Rpb24gRmluZFByb3h5Rm9yVVJMKHVybCwgaG9zdCkgeyAgaWYgKChzaEV4cE1hdGN"
       "oKHVybCwgJ2h0dHA6Ly93d3cuZ29vZ2xlLmNvbS9wb2xpY2llcy9wcml2YWN5KicpKSkgey"
       "AgICByZXR1cm4gJ0RJUkVDVCc7ICB9ICAgaWYgKHVybC5zdWJzdHJpbmcoMCwgNSkgPT0gJ"
       "2h0dHA6JykgeyAgICByZXR1cm4gJ0hUVFBTIHByb3h5Lmdvb2dsZXppcC5uZXQ6NDQzOyBQ"
       "Uk9YWSBjb21wcmVzcy5nb29nbGV6aXAubmV0OjgwOyBESVJFQ1QnOyAgfSAgcmV0dXJuICd"
       "ESVJFQ1QnO30=",
       true},
      // PAC with bypass rules that returns 'PROXY compress.googlezip.net:80;
      // DIRECT'.
      {"data:application/"
       "x-ns-proxy-autoconfig;base64,"
       "ZnVuY3Rpb24gRmluZFByb3h5Rm9yVVJMKHVybCwgaG9zdCkgeyAgaWYgKChzaEV4cE1hdGN"
       "oKHVybCwgJ2h0dHA6Ly93d3cuZ29vZ2xlLmNvbS9wb2xpY2llcy9wcml2YWN5KicpKSkgey"
       "AgICByZXR1cm4gJ0RJUkVDVCc7ICB9ICAgaWYgKHVybC5zdWJzdHJpbmcoMCwgNSkgPT0gJ"
       "2h0dHA6JykgeyAgICByZXR1cm4gJ1BST1hZIGNvbXByZXNzLmdvb2dsZXppcC5uZXQ6ODA7"
       "IERJUkVDVCc7ICB9ICByZXR1cm4gJ0RJUkVDVCc7fQ==",
       true},
      // PAC with bypass rules that returns 'PROXY proxy-dev.googlezip.net:80;
      // DIRECT'.
      {"data:application/"
       "x-ns-proxy-autoconfig;base64,"
       "ZnVuY3Rpb24gRmluZFByb3h5Rm9yVVJMKHVybCwgaG9zdCkgeyAgaWYgKChzaEV4cE1hdGN"
       "oKHVybCwgJ2h0dHA6Ly93d3cuZ29vZ2xlLmNvbS9wb2xpY2llcy9wcml2YWN5KicpKSkgey"
       "AgICByZXR1cm4gJ0RJUkVDVCc7ICB9ICAgaWYgKHVybC5zdWJzdHJpbmcoMCwgNSkgPT0gJ"
       "2h0dHA6JykgeyAgICByZXR1cm4gJ1BST1hZIHByb3h5LWRldi5nb29nbGV6aXAubmV0Ojgw"
       "OyBESVJFQ1QnOyAgfSAgcmV0dXJuICdESVJFQ1QnO30=",
       true},
      // Simple PAC that returns 'PROXY compress.googlezip.net:80'.
      {"data:application/"
       "x-ns-proxy-autoconfig;base64,"
       "ZnVuY3Rpb24gRmluZFByb3h5Rm9yVVJMKHVybCwgaG9zdCkge3JldHVybiAnUFJPWFkgY29"
       "tcHJlc3MuZ29vZ2xlemlwLm5ldDo4MCc7fQo=",
       true},
      // Simple PAC that returns 'PROXY compress.googlezip.net'. Note that since
      // the port is not specified, the pref will not be cleared.
      {"data:application/"
       "x-ns-proxy-autoconfig;base64,"
       "ZnVuY3Rpb24gRmluZFByb3h5Rm9yVVJMKHVybCwgaG9zdCkge3JldHVybiAnUFJPWFkgY29"
       "tcHJlc3MuZ29vZ2xlemlwLm5ldCc7fQ==",
       false},
      // Simple PAC that returns 'PROXY mycustomdrp.net:80'.
      {"data:application/"
       "x-ns-proxy-autoconfig;base64,"
       "ZnVuY3Rpb24gRmluZFByb3h5Rm9yVVJMKHVybCwgaG9zdCkge3JldHVybiAnUFJPWFkgb3J"
       "pZ2luLm5ldDo4MCc7fQo=",
       false},
      // Simple PAC that returns 'PROXY myprefixgooglezip.net:80'.
      {"data:application/"
       "x-ns-proxy-autoconfig;base64,"
       "ZnVuY3Rpb24gRmluZFByb3h5Rm9yVVJMKHVybCwgaG9zdCkge3JldHVybiAnUFJPWFkgbXl"
       "wcmVmaXhnb29nbGV6aXAubmV0OjgwJzt9Cg==",
       false},
      // Simple PAC that returns 'PROXY compress.googlezip.net.mydomain.com:80'.
      {"data:application/"
       "x-ns-proxy-autoconfig;base64,"
       "ZnVuY3Rpb24gRmluZFByb3h5Rm9yVVJMKHVybCwgaG9zdCkge3JldHVybiAnUFJPWFkgY29"
       "tcHJlc3MuZ29vZ2xlemlwLm5ldC5teWRvbWFpbi5jb206ODAnO30K",
       false},
      // PAC URL that doesn't embed a script.
      {"http://compress.googlezip.net/pac", false},
      // Complicated PAC that returns a JavaScript function that can evaluate to
      // proxy.googlezip.net:443 and compress.googlezip.net:80.
      {"data:application/"
       "x-ns-proxy-autoconfig;base64,"
       "ZnVuY3Rpb24gRmluZFByb3h5Rm9yVVJMKHVybCwgaG9zdCkgewogIGlmICh1cmwuc3Vic3R"
       "yaW5nKDAsNSkgPT0gJ2h0dHA6JyAmJiAKICAgICAgIWlzUGxhaW5Ib3N0TmFtZShob3N0KS"
       "AmJiAKICAgICAgIXNoRXhwTWF0Y2goaG9zdCwgJyoubG9jYWwnKSAmJiAKICAgICAgIWlzS"
       "W5OZXQoZG5zUmVzb2x2ZShob3N0KSwgJzEwLjAuMC4wJywgJzI1NS4wLjAuMCcpICYmIAog"
       "ICAgICAhaXNJbk5ldChkbnNSZXNvbHZlKGhvc3QpLCAnMTcyLjE2LjAuMCcsICAnMjU1LjI"
       "0MC4wLjAnKSAmJiAKICAgICAgIWlzSW5OZXQoZG5zUmVzb2x2ZShob3N0KSwgJzE5Mi4xNj"
       "guMC4wJywgICcyNTUuMjU1LjAuMCcpICYmIAogICAgICAhaXNJbk5ldChkbnNSZXNvbHZlK"
       "Ghvc3QpLCAnMTI3LjAuMC4wJywgJzI1NS4yNTUuMjU1LjAnKSAmJiAKICAgICAgIXNoRXhw"
       "TWF0Y2goaG9zdCwgJygqLm1ldHJpYy5nc3RhdGljLmNvbSknKSkKICAgIHJldHVybiAnSFR"
       "UUFMgcHJveHkuZ29vZ2xlemlwLm5ldDo0NDM7IFBST1hZIGNvbXByZXNzLmdvb2dsZXppcC"
       "5uZXQ6ODA7IFBST1hZIDc0LjEyNS4yMDUuMjExOjgwOyBESVJFQ1QnOwogIHJldHVybiAnR"
       "ElSRUNUJzsKfQ==",
       true},
  };

  for (const auto& test : test_cases) {
    base::HistogramTester histogram_tester;
    dict_ = std::make_unique<base::DictionaryValue>();
    dict_->SetString("mode", "pac_script");
    dict_->SetString("pac_url", test.pac_url);
    test_context_->pref_service()->Set(proxy_config::prefs::kProxy,
                                       *dict_.get());

    drp_chrome_settings_->MigrateDataReductionProxyOffProxyPrefs(
        test_context_->pref_service());

    if (test.expect_pref_cleared) {
      EXPECT_EQ(NULL, test_context_->pref_service()->GetUserPref(
                          proxy_config::prefs::kProxy));
      histogram_tester.ExpectUniqueSample(
          "DataReductionProxy.ProxyPrefMigrationResult",
          DataReductionProxyChromeSettings::PROXY_PREF_CLEARED_PAC_GOOGLEZIP,
          1);
    } else {
      const base::DictionaryValue* value;
      EXPECT_TRUE(test_context_->pref_service()
                      ->GetUserPref(proxy_config::prefs::kProxy)
                      ->GetAsDictionary(&value));
      std::string mode;
      EXPECT_TRUE(value->GetString("mode", &mode));
      EXPECT_EQ("pac_script", mode);
      std::string pac_url;
      EXPECT_TRUE(value->GetString("pac_url", &pac_url));
      EXPECT_EQ(test.pac_url, pac_url);

      histogram_tester.ExpectUniqueSample(
          "DataReductionProxy.ProxyPrefMigrationResult",
          DataReductionProxyChromeSettings::PROXY_PREF_NOT_CLEARED, 1);
    }
  }
}

TEST_F(DataReductionProxyChromeSettingsTest, MigrateIgnoreOtherProxy) {
  const std::string kTestServers[] = {
      "http=https://youtube.com", "http=http://googlezip.net",
      "http=http://thisismyproxynotgooglezip.net",
      "https=http://arbitraryprefixgooglezip.net"};

  for (const std::string& test_server : kTestServers) {
    base::HistogramTester histogram_tester;
    dict_ = std::make_unique<base::DictionaryValue>();
    dict_->SetString("mode", "fixed_servers");
    dict_->SetString("server", test_server);
    test_context_->pref_service()->Set(proxy_config::prefs::kProxy,
                                       *dict_.get());

    drp_chrome_settings_->MigrateDataReductionProxyOffProxyPrefs(
        test_context_->pref_service());

    base::DictionaryValue* value =
        (base::DictionaryValue*)test_context_->pref_service()->GetUserPref(
            proxy_config::prefs::kProxy);
    std::string mode;
    EXPECT_TRUE(value->GetString("mode", &mode));
    EXPECT_EQ("fixed_servers", mode);
    std::string server;
    EXPECT_TRUE(value->GetString("server", &server));
    EXPECT_EQ(test_server, server);

    histogram_tester.ExpectUniqueSample(
        "DataReductionProxy.ProxyPrefMigrationResult",
        DataReductionProxyChromeSettings::PROXY_PREF_NOT_CLEARED, 1);
  }
}
