// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <utility>

#include "base/callback.h"
#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "base/values.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "components/search/ntp_features.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"

#if defined(OS_WIN) || defined(OS_MAC) || \
    (defined(OS_LINUX) || BUILDFLAG(IS_CHROMEOS_LACROS))
#include "base/command_line.h"
#include "chrome/test/pixel/browser_skia_gold_pixel_diff.h"
#endif

class NewTabPageTest : public InProcessBrowserTest,
                       public content::DevToolsAgentHostClient {
 public:
  NewTabPageTest() {
    features_.InitWithFeatures(
        {}, {ntp_features::kNtpOneGoogleBar, ntp_features::kNtpShortcuts,
             ntp_features::kNtpMiddleSlotPromo, ntp_features::kModules});
  }

  ~NewTabPageTest() override = default;

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override {
    absl::optional<base::Value> maybe_parsed_message =
        base::JSONReader::Read(base::StringPiece(
            reinterpret_cast<const char*>(message.data()), message.size()));
    CHECK(maybe_parsed_message.has_value());
    base::Value parsed_message = std::move(maybe_parsed_message.value());
    auto* method = parsed_message.FindStringPath("method");
    if (!method) {
      return;
    }
    if (*method == "Network.requestWillBeSent") {
      // We track all started network requests to match them to corresponding
      // load completions.
      auto request_id = *parsed_message.FindStringPath("params.requestId");
      auto url = GURL(*parsed_message.FindStringPath("params.request.url"));
      loading_resources_[request_id] = url;
    } else if (*method == "Network.loadingFinished") {
      // Cross off network request from pending loads. Once all loads have
      // completed we potentially unblock the test from waiting.
      auto request_id = *parsed_message.FindStringPath("params.requestId");
      auto url = loading_resources_[request_id];
      loading_resources_.erase(request_id);
      if (loading_resources_.empty() && network_load_quit_closure_) {
        std::move(network_load_quit_closure_).Run();
      }
    } else if (*method == "DOM.attributeModified") {
      // Check if lazy load has completed and potentially unblock waiting test.
      auto node_id = *parsed_message.FindIntPath("params.nodeId");
      auto name = *parsed_message.FindStringPath("params.name");
      auto value = *parsed_message.FindStringPath("params.value");
      if (node_id == 3 && name == "lazy-loaded" && value == "true") {
        lazy_loaded_ = true;
      }
      if (lazy_loaded_ && lazy_load_quit_closure_) {
        std::move(lazy_load_quit_closure_).Run();
      }
    }
  }

  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override {}

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    browser_view_ = static_cast<BrowserView*>(browser()->window());
    contents_ = browser_view_->GetActiveWebContents();

    // Wait for initial about:blank to load and attach DevTools before
    // navigating to the NTP.
    ASSERT_TRUE(WaitForLoadStop(contents_));
    agent_host_ = content::DevToolsAgentHost::GetOrCreateFor(contents_);
    agent_host_->AttachClient(this);
    // Enable network events. We use completion of network loads as a signal
    // of steady state.
    agent_host_->DispatchProtocolMessage(
        this, base::as_bytes(base::make_span(
                  std::string("{\"id\": 1, \"method\": \"Network.enable\"}"))));
    // Enable DOM events. We determine completion of lazy load by reading a DOM
    // attribute.
    agent_host_->DispatchProtocolMessage(
        this, base::as_bytes(base::make_span(
                  std::string("{\"id\": 2, \"method\": \"DOM.enable\"}"))));

    NavigateParams params(browser(), GURL(chrome::kChromeUINewTabPageURL),
                          ui::PageTransition::PAGE_TRANSITION_FIRST);
    Navigate(&params);
    ASSERT_TRUE(WaitForLoadStop(contents_));

    // Request the DOM. We will only receive DOM events for DOMs we have
    // requested.
    agent_host_->DispatchProtocolMessage(
        this, base::as_bytes(base::make_span(std::string(
                  "{\"id\": 3, \"method\": \"DOM.getDocument\"}"))));
    // Read initial value of lazy-loaded in case lazy load is already complete
    // at this point in time.
    lazy_loaded_ =
        EvalJs(contents_.get(),
               "document.documentElement.hasAttribute('lazy-loaded')",
               content::EXECUTE_SCRIPT_DEFAULT_OPTIONS, /*world_id=*/1)
            .ExtractBool();
  }

  // Blocks until the NTP has completed lazy load.
  void WaitForLazyLoad() {
    if (lazy_loaded_) {
      return;
    }
    base::RunLoop run_loop;
    lazy_load_quit_closure_ = run_loop.QuitClosure();
    run_loop.Run();
  }

  // Blocks until all network requests have completed.
  void WaitForNetworkLoad() {
    if (loading_resources_.empty()) {
      return;
    }
    base::RunLoop run_loop;
    network_load_quit_closure_ = run_loop.QuitClosure();
    run_loop.Run();
  }

  // Blocks until the next animation frame.
  void WaitForAnimationFrame() {
    CHECK(EvalJs(contents_.get(),
                 "new Promise(r => requestAnimationFrame(() => r(true)))",
                 content::EXECUTE_SCRIPT_DEFAULT_OPTIONS, /*world_id=*/1)
              .ExtractBool());
  }

  // If pixel verification is enabled(--browser-ui-tests-verify-pixels)
  // verifies pixels using Skia Gold. Returns true on success or if the pixel
  // verification is skipped.
  bool VerifyUi(const std::string& screenshot_prefix,
                const std::string& screenshot_name) {
#if defined(OS_WIN) || defined(OS_MAC) || \
    (defined(OS_LINUX) || BUILDFLAG(IS_CHROMEOS_LACROS))
    if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
            "browser-ui-tests-verify-pixels")) {
      return true;
    }
    BrowserSkiaGoldPixelDiff pixel_diff;
    pixel_diff.Init(views::Widget::GetWidgetForNativeWindow(
                        browser()->window()->GetNativeWindow()),
                    screenshot_prefix);
    return pixel_diff.CompareScreenshot(screenshot_name,
                                        browser_view_->contents_web_view());
#else
    return true;
#endif
  }

 protected:
  base::test::ScopedFeatureList features_;
  raw_ptr<content::WebContents> contents_;
  raw_ptr<BrowserView> browser_view_;
  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  std::map<std::string, GURL> loading_resources_;
  base::OnceClosure network_load_quit_closure_;
  bool lazy_loaded_ = false;
  base::OnceClosure lazy_load_quit_closure_;
};

// TODO(crbug.com/1250156): NewTabPageTest.LandingPagePixelTest is flaky
#if defined(OS_WIN)
#define MAYBE_LandingPagePixelTest DISABLED_LandingPagePixelTest
#else
#define MAYBE_LandingPagePixelTest LandingPagePixelTest
#endif
IN_PROC_BROWSER_TEST_F(NewTabPageTest, MAYBE_LandingPagePixelTest) {
  WaitForLazyLoad();
  WaitForNetworkLoad();
  WaitForAnimationFrame();

  EXPECT_TRUE(VerifyUi("NewTabPageTest", "LandingPagePixelTest"));
}
