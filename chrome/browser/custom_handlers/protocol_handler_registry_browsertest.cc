// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry_factory.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/permissions/permission_request_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/fenced_frame_test_util.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/mojom/context_menu/context_menu.mojom.h"

#if defined(OS_MAC)
#include "chrome/test/base/launchservices_utils_mac.h"
#endif

using content::WebContents;
using custom_handlers::ProtocolHandlerRegistry;

namespace {

class ProtocolHandlerChangeWaiter : public ProtocolHandlerRegistry::Observer {
 public:
  explicit ProtocolHandlerChangeWaiter(ProtocolHandlerRegistry* registry) {
    registry_observation_.Observe(registry);
  }
  ProtocolHandlerChangeWaiter(const ProtocolHandlerChangeWaiter&) = delete;
  ProtocolHandlerChangeWaiter& operator=(const ProtocolHandlerChangeWaiter&) =
      delete;
  ~ProtocolHandlerChangeWaiter() override = default;

  void Wait() { run_loop_.Run(); }
  // ProtocolHandlerRegistry::Observer:
  void OnProtocolHandlerRegistryChanged() override { run_loop_.Quit(); }

 private:
  base::ScopedObservation<custom_handlers::ProtocolHandlerRegistry,
                          custom_handlers::ProtocolHandlerRegistry::Observer>
      registry_observation_{this};
  base::RunLoop run_loop_;
};

}  // namespace

class RegisterProtocolHandlerBrowserTest : public InProcessBrowserTest {
 public:
  RegisterProtocolHandlerBrowserTest() { }

  void SetUpOnMainThread() override {
#if defined(OS_MAC)
    ASSERT_TRUE(test::RegisterAppWithLaunchServices());
#endif
  }

  TestRenderViewContextMenu* CreateContextMenu(GURL url) {
    content::ContextMenuParams params;
    params.media_type = blink::mojom::ContextMenuDataMediaType::kNone;
    params.link_url = url;
    params.unfiltered_link_url = url;
    WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    params.page_url =
        web_contents->GetController().GetLastCommittedEntry()->GetURL();
#if defined(OS_MAC)
    params.writing_direction_default = 0;
    params.writing_direction_left_to_right = 0;
    params.writing_direction_right_to_left = 0;
#endif  // OS_MAC
    TestRenderViewContextMenu* menu = new TestRenderViewContextMenu(
        *browser()->tab_strip_model()->GetActiveWebContents()->GetMainFrame(),
        params);
    menu->Init();
    return menu;
  }

  void AddProtocolHandler(const std::string& protocol, const GURL& url) {
    ProtocolHandler handler =
        ProtocolHandler::CreateProtocolHandler(protocol, url);
    ProtocolHandlerRegistry* registry =
        ProtocolHandlerRegistryFactory::GetForBrowserContext(
            browser()->profile());
    // Fake that this registration is happening on profile startup. Otherwise
    // it'll try to register with the OS, which causes DCHECKs on Windows when
    // running as admin on Windows 7.
    registry->SetIsLoading(true);
    registry->OnAcceptRegisterProtocolHandler(handler);
    registry->SetIsLoading(true);
    ASSERT_TRUE(registry->IsHandledProtocol(protocol));
  }

  void RemoveProtocolHandler(const std::string& protocol,
                             const GURL& url) {
    ProtocolHandler handler =
        ProtocolHandler::CreateProtocolHandler(protocol, url);
    ProtocolHandlerRegistry* registry =
        ProtocolHandlerRegistryFactory::GetForBrowserContext(
            browser()->profile());
    registry->RemoveHandler(handler);
    ASSERT_FALSE(registry->IsHandledProtocol(protocol));
  }

 protected:
  content::test::FencedFrameTestHelper& fenced_frame_test_helper() {
    return fenced_frame_helper_;
  }

 private:
  content::test::FencedFrameTestHelper fenced_frame_helper_;
};

IN_PROC_BROWSER_TEST_F(RegisterProtocolHandlerBrowserTest,
    ContextMenuEntryAppearsForHandledUrls) {
  std::unique_ptr<TestRenderViewContextMenu> menu(
      CreateContextMenu(GURL("https://www.google.com/")));
  ASSERT_FALSE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_OPENLINKWITH));

  AddProtocolHandler(std::string("web+search"),
                     GURL("https://www.google.com/%s"));
  GURL url("web+search:testing");
  ProtocolHandlerRegistry* registry =
      ProtocolHandlerRegistryFactory::GetForBrowserContext(
          browser()->profile());
  ASSERT_EQ(1u, registry->GetHandlersFor(url.scheme()).size());
  menu.reset(CreateContextMenu(url));
  ASSERT_TRUE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_OPENLINKWITH));
}

IN_PROC_BROWSER_TEST_F(RegisterProtocolHandlerBrowserTest,
    UnregisterProtocolHandler) {
  std::unique_ptr<TestRenderViewContextMenu> menu(
      CreateContextMenu(GURL("https://www.google.com/")));
  ASSERT_FALSE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_OPENLINKWITH));

  AddProtocolHandler(std::string("web+search"),
                     GURL("https://www.google.com/%s"));
  GURL url("web+search:testing");
  ProtocolHandlerRegistry* registry =
      ProtocolHandlerRegistryFactory::GetForBrowserContext(
          browser()->profile());
  ASSERT_EQ(1u, registry->GetHandlersFor(url.scheme()).size());
  menu.reset(CreateContextMenu(url));
  ASSERT_TRUE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_OPENLINKWITH));
  RemoveProtocolHandler(std::string("web+search"),
                        GURL("https://www.google.com/%s"));
  ASSERT_EQ(0u, registry->GetHandlersFor(url.scheme()).size());
  menu.reset(CreateContextMenu(url));
  ASSERT_FALSE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_OPENLINKWITH));
}

IN_PROC_BROWSER_TEST_F(RegisterProtocolHandlerBrowserTest, CustomHandler) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL handler_url = embedded_test_server()->GetURL("/custom_handler.html");
  AddProtocolHandler("news", handler_url);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("news:test")));

  ASSERT_EQ(handler_url, browser()
                             ->tab_strip_model()
                             ->GetActiveWebContents()
                             ->GetLastCommittedURL());

  // Also check redirects.
  GURL redirect_url =
      embedded_test_server()->GetURL("/server-redirect?news:test");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), redirect_url));

  ASSERT_EQ(handler_url, browser()
                             ->tab_strip_model()
                             ->GetActiveWebContents()
                             ->GetLastCommittedURL());
}

// FencedFrames can not register to handle any protocols.
IN_PROC_BROWSER_TEST_F(RegisterProtocolHandlerBrowserTest, FencedFrame) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));

  // Create a FencedFrame.
  content::RenderFrameHost* fenced_frame_host =
      fenced_frame_test_helper().CreateFencedFrame(
          browser()->tab_strip_model()->GetActiveWebContents()->GetMainFrame(),
          embedded_test_server()->GetURL("/fenced_frames/title1.html"));
  ASSERT_TRUE(fenced_frame_host);

  // Ensure the registry is currently empty.
  GURL url("web+search:testing");
  ProtocolHandlerRegistry* registry =
      ProtocolHandlerRegistryFactory::GetForBrowserContext(
          browser()->profile());
  ASSERT_EQ(0u, registry->GetHandlersFor(url.scheme()).size());

  // Attempt to add an entry.
  ProtocolHandlerChangeWaiter waiter(registry);
  ASSERT_TRUE(content::ExecuteScript(fenced_frame_host,
                                     "navigator.registerProtocolHandler('web+"
                                     "search', 'test.html?%s', 'test');"));
  waiter.Wait();

  // Ensure the registry is still empty.
  ASSERT_EQ(0u, registry->GetHandlersFor(url.scheme()).size());
}

class RegisterProtocolHandlerSubresourceWebBundlesBrowserTest
    : public RegisterProtocolHandlerBrowserTest {
 public:
  RegisterProtocolHandlerSubresourceWebBundlesBrowserTest() = default;
  ~RegisterProtocolHandlerSubresourceWebBundlesBrowserTest() override = default;

  void SetUp() override {
    feature_list_.InitWithFeatures({features::kSubresourceWebBundles}, {});
    RegisterProtocolHandlerBrowserTest::SetUp();
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

IN_PROC_BROWSER_TEST_F(RegisterProtocolHandlerSubresourceWebBundlesBrowserTest,
                       UrnProtocolHandler) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL handler_url = embedded_test_server()->GetURL("/%s");
  AddProtocolHandler("urn", handler_url);

  std::u16string expected_title = u"OK";
  content::TitleWatcher title_watcher(
      browser()->tab_strip_model()->GetActiveWebContents(), expected_title);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("/web_bundle/urn-handler-test.html")));

  EXPECT_EQ(expected_title, title_watcher.WaitAndGetTitle());
}

using RegisterProtocolHandlerExtensionBrowserTest =
    extensions::ExtensionBrowserTest;

IN_PROC_BROWSER_TEST_F(RegisterProtocolHandlerExtensionBrowserTest, Basic) {
#if defined(OS_MAC)
  ASSERT_TRUE(test::RegisterAppWithLaunchServices());
#endif
  permissions::PermissionRequestManager::FromWebContents(
      browser()->tab_strip_model()->GetActiveWebContents())
      ->set_auto_response_for_test(
          permissions::PermissionRequestManager::ACCEPT_ALL);

  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII("protocol_handler"));
  ASSERT_NE(nullptr, extension);

  std::string handler_url =
      "chrome-extension://" + extension->id() + "/test.html";

  // Register the handler.
  {
    ProtocolHandlerRegistry* registry =
        ProtocolHandlerRegistryFactory::GetForBrowserContext(
            browser()->profile());
    ProtocolHandlerChangeWaiter waiter(registry);
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(handler_url)));
    ASSERT_TRUE(content::ExecuteScript(
        browser()->tab_strip_model()->GetActiveWebContents(),
        "navigator.registerProtocolHandler('geo', 'test.html?%s', 'test');"));
    waiter.Wait();
  }

  // Test the handler.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("geo:test")));
  ASSERT_EQ(GURL(handler_url + "?geo%3Atest"), browser()
                                                   ->tab_strip_model()
                                                   ->GetActiveWebContents()
                                                   ->GetLastCommittedURL());
}

class RegisterProtocolHandlerAndServiceWorkerInterceptor
    : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    ASSERT_TRUE(embedded_test_server()->Start());

    // Navigate to the test page.
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL(
                       "/protocol_handler/service_workers/"
                       "test_protocol_handler_and_service_workers.html")));

    // Bypass permission dialogs for registering new protocol handlers.
    permissions::PermissionRequestManager::FromWebContents(
        browser()->tab_strip_model()->GetActiveWebContents())
        ->set_auto_response_for_test(
            permissions::PermissionRequestManager::ACCEPT_ALL);
  }
};

// TODO(crbug.com/1204127): Fix flakiness.
IN_PROC_BROWSER_TEST_F(RegisterProtocolHandlerAndServiceWorkerInterceptor,
                       DISABLED_RegisterFetchListenerForHTMLHandler) {
  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  // Register a service worker intercepting requests to the HTML handler.
  EXPECT_EQ(true, content::EvalJs(web_contents,
                                  "registerFetchListenerForHTMLHandler();"));

  {
    // Register a HTML handler with a user gesture.
    ProtocolHandlerRegistry* registry =
        ProtocolHandlerRegistryFactory::GetForBrowserContext(
            browser()->profile());
    ProtocolHandlerChangeWaiter waiter(registry);
    ASSERT_TRUE(content::ExecJs(web_contents, "registerHTMLHandler();"));
    waiter.Wait();
  }

  // Verify that a page with the registered scheme is managed by the service
  // worker, not the HTML handler.
  EXPECT_EQ(true,
            content::EvalJs(web_contents,
                            "pageWithCustomSchemeHandledByServiceWorker();"));
}
