// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/mojom/cors.mojom.h"

#include "mojo/public/cpp/test_support/test_utils.h"
#include "services/network/public/cpp/cors/cors_error_status.h"
#include "services/network/public/mojom/ip_address_space.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace network {
namespace {

TEST(CorsMojomTraitsTest, CorsErrorStatusMojoRoundTrip) {
  CorsErrorStatus original;
  original.cors_error = mojom::CorsError::kInsecurePrivateNetwork;
  original.failed_parameter = "bleep";
  original.target_address_space = mojom::IPAddressSpace::kPrivate;
  original.resource_address_space = mojom::IPAddressSpace::kLocal;
  original.has_authorization_covered_by_wildcard_on_preflight = true;

  CorsErrorStatus copy;
  EXPECT_TRUE(mojo::test::SerializeAndDeserialize<mojom::CorsErrorStatus>(
      original, copy));
  EXPECT_EQ(original, copy);
}

}  // namespace
}  // namespace network
