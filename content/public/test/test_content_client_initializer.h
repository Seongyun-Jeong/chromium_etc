// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_TEST_TEST_CONTENT_CLIENT_INITIALIZER_H_
#define CONTENT_PUBLIC_TEST_TEST_CONTENT_CLIENT_INITIALIZER_H_

#include <memory>

namespace network {
class TestNetworkConnectionTracker;
}

namespace content {

class ContentClient;
class MockAgentSchedulingGroupHostFactory;
class MockRenderProcessHostFactory;
class NotificationServiceImpl;
class TestContentBrowserClient;
class TestRenderViewHostFactory;

// Initializes various objects needed to run unit tests that use content::
// objects. Currently this includes setting up the notification service,
// creating and setting the content client and the content browser client.
class TestContentClientInitializer {
 public:
  TestContentClientInitializer();

  TestContentClientInitializer(const TestContentClientInitializer&) = delete;
  TestContentClientInitializer& operator=(const TestContentClientInitializer&) =
      delete;

  ~TestContentClientInitializer();

  // Enables switching RenderViewHost creation to use the test version instead
  // of the real implementation. This will last throughout the lifetime of this
  // class.
  void CreateTestRenderViewHosts();

 private:
  std::unique_ptr<network::TestNetworkConnectionTracker>
      test_network_connection_tracker_;
  std::unique_ptr<NotificationServiceImpl> notification_service_;
  std::unique_ptr<ContentClient> content_client_;
  std::unique_ptr<TestContentBrowserClient> content_browser_client_;
  std::unique_ptr<MockRenderProcessHostFactory> rph_factory_;
  std::unique_ptr<MockAgentSchedulingGroupHostFactory> asgh_factory_;
  std::unique_ptr<TestRenderViewHostFactory> test_render_view_host_factory_;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_TEST_TEST_CONTENT_CLIENT_INITIALIZER_H_
