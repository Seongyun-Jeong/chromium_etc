// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/loader/navigation_early_hints_manager.h"

#include <memory>

#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_storage_partition.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/link_header.mojom.h"
#include "services/network/public/mojom/parsed_headers.mojom.h"
#include "services/network/test/test_network_context.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

using PreloadedResources = NavigationEarlyHintsManager::PreloadedResources;

namespace {

const char kNavigationPath[] = "https://a.test/";
const char kPreloadPath[] = "https://a.test/script.js";
const std::string kPreloadBody = "/*empty*/";

struct PreconnectRequest {
  PreconnectRequest(const GURL& url,
                    bool allow_credentials,
                    const net::NetworkIsolationKey& network_isolation_key)
      : url(url),
        allow_credentials(allow_credentials),
        network_isolation_key(network_isolation_key) {}

  GURL const url;
  bool const allow_credentials;
  net::NetworkIsolationKey const network_isolation_key;
};

class FakeNetworkContext : public network::TestNetworkContext {
 public:
  FakeNetworkContext() = default;
  ~FakeNetworkContext() override = default;

  void PreconnectSockets(
      uint32_t num_streams,
      const GURL& url,
      bool allow_credentials,
      const net::NetworkIsolationKey& network_isolation_key) override {
    preconnect_requests_.emplace_back(url, allow_credentials,
                                      network_isolation_key);
  }

  std::vector<PreconnectRequest>& preconnect_requests() {
    return preconnect_requests_;
  }

 private:
  std::vector<PreconnectRequest> preconnect_requests_;
};

}  // namespace

class NavigationEarlyHintsManagerTest : public testing::Test {
 public:
  NavigationEarlyHintsManagerTest()
      : task_environment_(base::test::TaskEnvironment::MainThreadType::IO) {
    url::Origin origin = url::Origin::Create(GURL(kNavigationPath));
    auto isolation_info = net::IsolationInfo::CreateForInternalRequest(origin);

    network_isolation_key_ = isolation_info.network_isolation_key();

    mojo::Remote<network::mojom::URLLoaderFactory> remote;
    loader_factory_.Clone(remote.BindNewPipeAndPassReceiver());
    early_hints_manager_ = std::make_unique<NavigationEarlyHintsManager>(
        browser_context_, storage_partition_,
        FrameTreeNode::kFrameTreeNodeInvalidId,
        NavigationEarlyHintsManagerParams(origin, std::move(isolation_info),
                                          std::move(remote)));
  }

  ~NavigationEarlyHintsManagerTest() override = default;

  void SetUp() override {
    feature_list_.InitAndEnableFeature(
        features::kEarlyHintsPreloadForNavigation);
    fake_network_context_ = std::make_unique<FakeNetworkContext>();
    early_hints_manager().SetNetworkContextForTesting(
        fake_network_context_.get());
  }

 protected:
  network::TestURLLoaderFactory& loader_factory() { return loader_factory_; }

  NavigationEarlyHintsManager& early_hints_manager() {
    return *early_hints_manager_;
  }

  FakeNetworkContext& fake_network_context() { return *fake_network_context_; }

  net::NetworkIsolationKey& network_isolation_key() {
    return network_isolation_key_;
  }

  network::mojom::URLResponseHeadPtr CreatePreloadResponseHead() {
    auto head = network::mojom::URLResponseHead::New();
    head->headers =
        base::MakeRefCounted<net::HttpResponseHeaders>("HTTP/1.1 200 OK");
    head->headers->AddHeader("content-type", "application/javascript");
    return head;
  }

  network::mojom::EarlyHintsPtr CreateEarlyHintWithPreload() {
    auto link_header = network::mojom::LinkHeader::New(
        GURL(kPreloadPath), network::mojom::LinkRelAttribute::kPreload,
        network::mojom::LinkAsAttribute::kScript,
        network::mojom::CrossOriginAttribute::kUnspecified,
        /*mime_type=*/absl::nullopt);
    auto hints = network::mojom::EarlyHints::New();
    hints->headers = network::mojom::ParsedHeaders::New();
    hints->headers->link_headers.push_back(std::move(link_header));
    return hints;
  }

  network::ResourceRequest CreateNavigationResourceRequest() {
    network::ResourceRequest request;
    request.is_main_frame = true;
    request.url = GURL(kNavigationPath);
    return request;
  }

  PreloadedResources WaitForPreloadedResources() {
    base::RunLoop loop;
    PreloadedResources result;
    early_hints_manager().WaitForPreloadsFinishedForTesting(
        base::BindLambdaForTesting([&](PreloadedResources preloaded_resources) {
          result = preloaded_resources;
          loop.Quit();
        }));
    loop.Run();
    return result;
  }

 private:
  base::test::ScopedFeatureList feature_list_;

  BrowserTaskEnvironment task_environment_;
  TestBrowserContext browser_context_;
  TestStoragePartition storage_partition_;
  network::TestURLLoaderFactory loader_factory_;
  std::unique_ptr<NavigationEarlyHintsManager> early_hints_manager_;
  std::unique_ptr<FakeNetworkContext> fake_network_context_;
  net::NetworkIsolationKey network_isolation_key_;
};

TEST_F(NavigationEarlyHintsManagerTest, SimpleResponse) {
  base::HistogramTester histograms;

  // Set up a response which simulates coming from network.
  network::mojom::URLResponseHeadPtr head = CreatePreloadResponseHead();
  network::URLLoaderCompletionStatus status;
  status.decoded_body_length = kPreloadBody.size();
  status.error_code = net::OK;
  loader_factory().AddResponse(GURL(kPreloadPath), std::move(head),
                               kPreloadBody, status);

  early_hints_manager().HandleEarlyHints(CreateEarlyHintWithPreload(),
                                         CreateNavigationResourceRequest());

  PreloadedResources preloads = WaitForPreloadedResources();
  ASSERT_EQ(preloads.size(), 1UL);
  auto it = preloads.find(GURL(kPreloadPath));
  ASSERT_TRUE(it != preloads.end());
  ASSERT_TRUE(it->second.error_code.has_value());
  EXPECT_EQ(it->second.error_code.value(), net::OK);
  EXPECT_FALSE(it->second.was_canceled);

  histograms.ExpectUniqueSample(
      kEarlyHintsPreloadRequestDestinationHistogramName,
      network::mojom::RequestDestination::kScript, 1);
}

TEST_F(NavigationEarlyHintsManagerTest, EmptyBody) {
  // Set up an empty response which simulates coming from network.
  network::mojom::URLResponseHeadPtr head = CreatePreloadResponseHead();
  network::URLLoaderCompletionStatus status;
  status.decoded_body_length = 0;
  status.error_code = net::OK;
  loader_factory().AddResponse(GURL(kPreloadPath), std::move(head), "", status);

  early_hints_manager().HandleEarlyHints(CreateEarlyHintWithPreload(),
                                         CreateNavigationResourceRequest());

  PreloadedResources preloads = WaitForPreloadedResources();
  ASSERT_EQ(preloads.size(), 1UL);
  auto it = preloads.find(GURL(kPreloadPath));
  ASSERT_TRUE(it != preloads.end());
  ASSERT_TRUE(it->second.error_code.has_value());
  EXPECT_EQ(it->second.error_code.value(), net::OK);
  EXPECT_FALSE(it->second.was_canceled);
}

TEST_F(NavigationEarlyHintsManagerTest, ResponseExistsInDiskCache) {
  base::HistogramTester histograms;

  // Set up a response which simulates coming from disk cache.
  network::mojom::URLResponseHeadPtr head = CreatePreloadResponseHead();
  head->was_fetched_via_cache = true;
  network::URLLoaderCompletionStatus status;
  status.decoded_body_length = kPreloadBody.size();
  status.error_code = net::OK;
  loader_factory().AddResponse(GURL(kPreloadPath), std::move(head),
                               kPreloadBody, status);

  early_hints_manager().HandleEarlyHints(CreateEarlyHintWithPreload(),
                                         CreateNavigationResourceRequest());

  PreloadedResources preloads = WaitForPreloadedResources();
  ASSERT_EQ(preloads.size(), 1UL);
  auto it = preloads.find(GURL(kPreloadPath));
  ASSERT_TRUE(it != preloads.end());
  EXPECT_TRUE(it->second.was_canceled);

  // The request destination histogram for a preload should not be recorded when
  // the preload is canceled.
  histograms.ExpectTotalCount(kEarlyHintsPreloadRequestDestinationHistogramName,
                              0);
}

TEST_F(NavigationEarlyHintsManagerTest, PreloadSchemeIsUnsupported) {
  auto link_header = network::mojom::LinkHeader::New(
      GURL("file:///"), network::mojom::LinkRelAttribute::kPreload,
      network::mojom::LinkAsAttribute::kUnspecified,
      network::mojom::CrossOriginAttribute::kUnspecified,
      /*mime_type=*/absl::nullopt);
  auto hints = network::mojom::EarlyHints::New();
  hints->headers = network::mojom::ParsedHeaders::New();
  hints->headers->link_headers.push_back(std::move(link_header));

  early_hints_manager().HandleEarlyHints(std::move(hints),
                                         CreateNavigationResourceRequest());

  EXPECT_TRUE(early_hints_manager().WasResourceHintsReceived());
  EXPECT_FALSE(early_hints_manager().HasInflightPreloads());
}

TEST_F(NavigationEarlyHintsManagerTest, SinglePreconnect) {
  auto preconnect_url = GURL("https://b.test");
  auto link_header = network::mojom::LinkHeader::New(
      preconnect_url, network::mojom::LinkRelAttribute::kPreconnect,
      network::mojom::LinkAsAttribute::kUnspecified,
      network::mojom::CrossOriginAttribute::kUnspecified,
      /*mime_type=*/absl::nullopt);
  auto hints = network::mojom::EarlyHints::New();
  hints->headers = network::mojom::ParsedHeaders::New();
  hints->headers->link_headers.push_back(std::move(link_header));

  early_hints_manager().HandleEarlyHints(std::move(hints),
                                         CreateNavigationResourceRequest());

  std::vector<PreconnectRequest>& requests =
      fake_network_context().preconnect_requests();
  ASSERT_EQ(requests.size(), 1UL);
  EXPECT_EQ(requests[0].url, preconnect_url);
  EXPECT_TRUE(requests[0].allow_credentials);
  EXPECT_EQ(requests[0].network_isolation_key, network_isolation_key());
}

TEST_F(NavigationEarlyHintsManagerTest, MultiplePreconnects) {
  auto preconnect_url1 = GURL("https://b.test");
  auto preconnect_url2 = GURL("https://c.test");

  auto hints = network::mojom::EarlyHints::New();
  hints->headers = network::mojom::ParsedHeaders::New();

  // Add four preconnect Link headers. The first three Links have the same
  // origin. The third and fourth Links specify crossorigin attribute. The
  // second Link should be skipped since it is a duplication of the first one.
  hints->headers->link_headers.push_back(network::mojom::LinkHeader::New(
      preconnect_url1, network::mojom::LinkRelAttribute::kPreconnect,
      network::mojom::LinkAsAttribute::kUnspecified,
      network::mojom::CrossOriginAttribute::kUnspecified,
      /*mime_type=*/absl::nullopt));
  hints->headers->link_headers.push_back(network::mojom::LinkHeader::New(
      preconnect_url1, network::mojom::LinkRelAttribute::kPreconnect,
      network::mojom::LinkAsAttribute::kUnspecified,
      network::mojom::CrossOriginAttribute::kUnspecified,
      /*mime_type=*/absl::nullopt));
  hints->headers->link_headers.push_back(network::mojom::LinkHeader::New(
      preconnect_url1, network::mojom::LinkRelAttribute::kPreconnect,
      network::mojom::LinkAsAttribute::kUnspecified,
      network::mojom::CrossOriginAttribute::kAnonymous,
      /*mime_type=*/absl::nullopt));
  hints->headers->link_headers.push_back(network::mojom::LinkHeader::New(
      preconnect_url2, network::mojom::LinkRelAttribute::kPreconnect,
      network::mojom::LinkAsAttribute::kUnspecified,
      network::mojom::CrossOriginAttribute::kAnonymous,
      /*mime_type=*/absl::nullopt));

  early_hints_manager().HandleEarlyHints(std::move(hints),
                                         CreateNavigationResourceRequest());

  std::vector<PreconnectRequest>& requests =
      fake_network_context().preconnect_requests();
  ASSERT_EQ(requests.size(), 3UL);

  EXPECT_EQ(requests[0].url, preconnect_url1);
  EXPECT_TRUE(requests[0].allow_credentials);
  EXPECT_EQ(requests[0].network_isolation_key, network_isolation_key());

  EXPECT_EQ(requests[1].url, preconnect_url1);
  EXPECT_FALSE(requests[1].allow_credentials);
  EXPECT_EQ(requests[1].network_isolation_key, network_isolation_key());

  EXPECT_EQ(requests[2].url, preconnect_url2);
  EXPECT_FALSE(requests[2].allow_credentials);
  EXPECT_EQ(requests[2].network_isolation_key, network_isolation_key());
}

TEST_F(NavigationEarlyHintsManagerTest, InvalidPreconnectLink) {
  auto preconnect_url = GURL("file:///");
  auto link_header = network::mojom::LinkHeader::New(
      preconnect_url, network::mojom::LinkRelAttribute::kPreconnect,
      network::mojom::LinkAsAttribute::kUnspecified,
      network::mojom::CrossOriginAttribute::kUnspecified,
      /*mime_type=*/absl::nullopt);
  auto hints = network::mojom::EarlyHints::New();
  hints->headers = network::mojom::ParsedHeaders::New();
  hints->headers->link_headers.push_back(std::move(link_header));

  early_hints_manager().HandleEarlyHints(std::move(hints),
                                         CreateNavigationResourceRequest());

  std::vector<PreconnectRequest>& requests =
      fake_network_context().preconnect_requests();
  EXPECT_TRUE(requests.empty());
}

}  // namespace content
