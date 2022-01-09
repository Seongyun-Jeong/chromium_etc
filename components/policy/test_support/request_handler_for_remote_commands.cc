// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/test_support/request_handler_for_remote_commands.h"

#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "components/policy/test_support/client_storage.h"
#include "components/policy/test_support/policy_storage.h"
#include "components/policy/test_support/test_server_helpers.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

namespace em = enterprise_management;

namespace policy {

RequestHandlerForRemoteCommands::RequestHandlerForRemoteCommands(
    ClientStorage* client_storage,
    PolicyStorage* policy_storage)
    : EmbeddedPolicyTestServer::RequestHandler(client_storage, policy_storage) {
}

RequestHandlerForRemoteCommands::~RequestHandlerForRemoteCommands() = default;

std::string RequestHandlerForRemoteCommands::RequestType() {
  return dm_protocol::kValueRequestRemoteCommands;
}

std::unique_ptr<HttpResponse> RequestHandlerForRemoteCommands::HandleRequest(
    const HttpRequest& request) {
  em::DeviceManagementResponse response;
  response.mutable_remote_command_response();
  return CreateHttpResponse(net::HTTP_OK, response.SerializeAsString());
}

}  // namespace policy
