// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/test_support/embedded_policy_test_server.h"

#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "components/policy/test_support/embedded_policy_test_server.h"
#include "components/policy/test_support/embedded_policy_test_server_test_base.h"
#include "components/policy/test_support/test_server_helpers.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace policy {

namespace {

constexpr char kFakeDeviceId[] = "fake_device_id";
constexpr char kFakeRequestType[] = "fake_request_type";
constexpr char kInvalidRequestType[] = "invalid_request_type";
constexpr char kResponseBodyYay[] = "Yay!!!";

class FakeRequestHandler : public EmbeddedPolicyTestServer::RequestHandler {
 public:
  FakeRequestHandler() : RequestHandler(nullptr, nullptr) {}
  ~FakeRequestHandler() override = default;

  std::string RequestType() override { return kFakeRequestType; }

  std::unique_ptr<net::test_server::HttpResponse> HandleRequest(
      const net::test_server::HttpRequest& request) override {
    return CreateHttpResponse(net::HTTP_OK, kResponseBodyYay);
  }
};

}  // namespace

class EmbeddedPolicyTestServerTest : public EmbeddedPolicyTestServerTestBase {
 public:
  EmbeddedPolicyTestServerTest() = default;
  ~EmbeddedPolicyTestServerTest() override = default;

  void SetUp() override {
    EmbeddedPolicyTestServerTestBase::SetUp();

    test_server()->RegisterHandler(std::make_unique<FakeRequestHandler>());
  }
};

TEST_F(EmbeddedPolicyTestServerTest, HandleRequest_InvalidRequestType) {
  SetRequestTypeParam(kInvalidRequestType);

  StartRequestAndWait();

  EXPECT_EQ(GetResponseCode(), net::HTTP_NOT_FOUND);
}

TEST_F(EmbeddedPolicyTestServerTest, HandleRequest_Success) {
  SetRequestTypeParam(kFakeRequestType);
  SetAppType(dm_protocol::kValueAppType);
  SetDeviceIdParam(kFakeDeviceId);
  SetDeviceType(dm_protocol::kValueDeviceType);

  StartRequestAndWait();

  EXPECT_EQ(GetResponseCode(), net::HTTP_OK);
  ASSERT_TRUE(HasResponseBody());
  EXPECT_EQ(kResponseBodyYay, GetResponseBody());
}

TEST_F(EmbeddedPolicyTestServerTest, HandleRequest_MissingAppType) {
  SetRequestTypeParam(kFakeRequestType);
  SetDeviceIdParam(kFakeDeviceId);
  SetDeviceType(dm_protocol::kValueDeviceType);

  StartRequestAndWait();

  EXPECT_EQ(GetResponseCode(), net::HTTP_BAD_REQUEST);
}

TEST_F(EmbeddedPolicyTestServerTest, HandleRequest_MissingDeviceId) {
  SetRequestTypeParam(kFakeRequestType);
  SetAppType(dm_protocol::kValueAppType);
  SetDeviceType(dm_protocol::kValueDeviceType);

  StartRequestAndWait();

  EXPECT_EQ(GetResponseCode(), net::HTTP_BAD_REQUEST);
}

TEST_F(EmbeddedPolicyTestServerTest, HandleRequest_MissingDeviceType) {
  SetRequestTypeParam(kFakeRequestType);
  SetAppType(dm_protocol::kValueAppType);
  SetDeviceIdParam(kFakeDeviceId);

  StartRequestAndWait();

  EXPECT_EQ(GetResponseCode(), net::HTTP_BAD_REQUEST);
}

}  // namespace policy
