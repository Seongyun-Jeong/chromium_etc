// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/supervised_user/web_approvals_manager.h"

#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/run_loop.h"
#include "chrome/browser/supervised_user/permission_request_creator.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

class AsyncResultHolder {
 public:
  AsyncResultHolder() = default;

  AsyncResultHolder(const AsyncResultHolder&) = delete;
  AsyncResultHolder& operator=(const AsyncResultHolder&) = delete;

  ~AsyncResultHolder() = default;

  bool GetResult() {
    run_loop_.Run();
    return result_;
  }

  void SetResult(bool result) {
    result_ = result;
    run_loop_.Quit();
  }

 private:
  base::RunLoop run_loop_;
  bool result_ = false;
};

// TODO(agawronska): Check if this can be a real mock.
// Mocks PermissionRequestCreator to test the async responses.
class MockPermissionRequestCreator : public PermissionRequestCreator {
 public:
  MockPermissionRequestCreator() = default;

  MockPermissionRequestCreator(const MockPermissionRequestCreator&) = delete;
  MockPermissionRequestCreator& operator=(const MockPermissionRequestCreator&) =
      delete;

  ~MockPermissionRequestCreator() override {}

  void set_enabled(bool enabled) { enabled_ = enabled; }

  const std::vector<GURL>& requested_urls() const { return requested_urls_; }

  void AnswerRequest(size_t index, bool result) {
    ASSERT_LT(index, requested_urls_.size());
    std::move(callbacks_[index]).Run(result);
    callbacks_.erase(callbacks_.begin() + index);
    requested_urls_.erase(requested_urls_.begin() + index);
  }

 private:
  // PermissionRequestCreator:
  bool IsEnabled() const override { return enabled_; }

  void CreateURLAccessRequest(const GURL& url_requested,
                              SuccessCallback callback) override {
    ASSERT_TRUE(enabled_);
    requested_urls_.push_back(url_requested);
    callbacks_.push_back(std::move(callback));
  }

  bool enabled_ = false;
  std::vector<GURL> requested_urls_;
  std::vector<SuccessCallback> callbacks_;
};

}  // namespace

class WebApprovalsManagerTest : public ::testing::Test {
 protected:
  WebApprovalsManagerTest() = default;

  WebApprovalsManagerTest(const WebApprovalsManagerTest&) = delete;
  WebApprovalsManagerTest& operator=(const WebApprovalsManagerTest&) = delete;

  ~WebApprovalsManagerTest() override = default;

  WebApprovalsManager& web_approvals_manager() {
    return web_approvals_manager_;
  }

  void RequestRemoteApproval(const GURL& url,
                             AsyncResultHolder* result_holder) {
    web_approvals_manager_.RequestRemoteApproval(
        url, base::BindOnce(&AsyncResultHolder::SetResult,
                            base::Unretained(result_holder)));
  }

 private:
  content::BrowserTaskEnvironment task_environment_;
  WebApprovalsManager web_approvals_manager_;
};

TEST_F(WebApprovalsManagerTest, CreatePermissionRequest) {
  GURL url("http://www.example.com");

  // Without any permission request creators, it should be disabled, and any
  // AddURLAccessRequest() calls should fail.
  EXPECT_FALSE(web_approvals_manager().AreRemoteApprovalRequestsEnabled());
  {
    AsyncResultHolder result_holder;
    RequestRemoteApproval(url, &result_holder);
    EXPECT_FALSE(result_holder.GetResult());
  }

  // TODO(agawronska): Check if that can be eliminated by using a mock.
  // Add a disabled permission request creator. This should not change anything.
  MockPermissionRequestCreator* creator = new MockPermissionRequestCreator;
  web_approvals_manager().AddRemoteApprovalRequestCreator(
      base::WrapUnique(creator));

  EXPECT_FALSE(web_approvals_manager().AreRemoteApprovalRequestsEnabled());
  {
    AsyncResultHolder result_holder;
    RequestRemoteApproval(url, &result_holder);
    EXPECT_FALSE(result_holder.GetResult());
  }

  // Enable the permission request creator. This should enable permission
  // requests and queue them up.
  creator->set_enabled(true);
  EXPECT_TRUE(web_approvals_manager().AreRemoteApprovalRequestsEnabled());
  {
    AsyncResultHolder result_holder;
    RequestRemoteApproval(url, &result_holder);
    ASSERT_EQ(1u, creator->requested_urls().size());
    EXPECT_EQ(url.spec(), creator->requested_urls()[0].spec());

    creator->AnswerRequest(0, true);
    EXPECT_TRUE(result_holder.GetResult());
  }

  {
    AsyncResultHolder result_holder;
    RequestRemoteApproval(url, &result_holder);
    ASSERT_EQ(1u, creator->requested_urls().size());
    EXPECT_EQ(url.spec(), creator->requested_urls()[0].spec());

    creator->AnswerRequest(0, false);
    EXPECT_FALSE(result_holder.GetResult());
  }

  // Add a second permission request creator.
  MockPermissionRequestCreator* creator_2 = new MockPermissionRequestCreator;
  creator_2->set_enabled(true);
  web_approvals_manager().AddRemoteApprovalRequestCreator(
      base::WrapUnique(creator_2));

  {
    AsyncResultHolder result_holder;
    RequestRemoteApproval(url, &result_holder);
    ASSERT_EQ(1u, creator->requested_urls().size());
    EXPECT_EQ(url.spec(), creator->requested_urls()[0].spec());

    // Make the first creator succeed. This should make the whole thing succeed.
    creator->AnswerRequest(0, true);
    EXPECT_TRUE(result_holder.GetResult());
  }

  {
    AsyncResultHolder result_holder;
    RequestRemoteApproval(url, &result_holder);
    ASSERT_EQ(1u, creator->requested_urls().size());
    EXPECT_EQ(url.spec(), creator->requested_urls()[0].spec());

    // Make the first creator fail. This should fall back to the second one.
    creator->AnswerRequest(0, false);
    ASSERT_EQ(1u, creator_2->requested_urls().size());
    EXPECT_EQ(url.spec(), creator_2->requested_urls()[0].spec());

    // Make the second creator succeed, which will make the whole thing succeed.
    creator_2->AnswerRequest(0, true);
    EXPECT_TRUE(result_holder.GetResult());
  }
}
