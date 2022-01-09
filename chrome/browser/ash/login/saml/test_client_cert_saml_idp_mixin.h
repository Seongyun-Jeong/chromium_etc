// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOGIN_SAML_TEST_CLIENT_CERT_SAML_IDP_MIXIN_H_
#define CHROME_BROWSER_ASH_LOGIN_SAML_TEST_CLIENT_CERT_SAML_IDP_MIXIN_H_

#include <memory>
#include <string>
#include <vector>

// TODO(https://crbug.com/1164001): move to forward declaration.
#include "chrome/browser/ash/login/test/fake_gaia_mixin.h"
#include "chrome/test/base/mixin_based_in_process_browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

class GURL;

namespace net {
namespace test_server {
struct HttpRequest;
class HttpResponse;
}  // namespace test_server
}  // namespace net

namespace ash {

class TestClientCertSamlIdpMixin final : public InProcessBrowserTestMixin {
 public:
  // `client_cert_authorities` is the list of DER-encoded X.509
  // DistinguishedName of certificate authorities that should be requested by
  // the SAML server during the client authentication.
  TestClientCertSamlIdpMixin(
      InProcessBrowserTestMixinHost* host,
      FakeGaiaMixin* gaia_mixin,
      const std::vector<std::string>& client_cert_authorities);
  TestClientCertSamlIdpMixin(const TestClientCertSamlIdpMixin&) = delete;
  TestClientCertSamlIdpMixin& operator=(const TestClientCertSamlIdpMixin&) =
      delete;
  ~TestClientCertSamlIdpMixin() override;

  // Returns the SAML IdP initial page URL, which should be configured in the
  // (fake) Gaia as the redirect endpoint for the tested users.
  GURL GetSamlPageUrl() const;

  // InProcessBrowserTestMixin:
  void SetUpOnMainThread() override;

 private:
  // Handles requests to `saml_server_`.
  std::unique_ptr<net::test_server::HttpResponse> HandleSamlServerRequest(
      const net::test_server::HttpRequest& request);
  // Handles requests to `saml_with_client_certs_server_`.
  std::unique_ptr<net::test_server::HttpResponse>
  HandleSamlWithClientCertsServerRequest(
      const net::test_server::HttpRequest& request);

  // Returns the URL to be used by the SAML page to redirect back to Gaia after
  // the authentication completion.
  GURL GetGaiaSamlAssertionUrl(const std::string& saml_relay_state);

  FakeGaiaMixin* const gaia_mixin_;
  net::EmbeddedTestServer saml_server_{net::EmbeddedTestServer::TYPE_HTTPS};
  net::EmbeddedTestServer saml_with_client_certs_server_{
      net::EmbeddedTestServer::TYPE_HTTPS};
};

}  // namespace ash

#endif  // CHROME_BROWSER_ASH_LOGIN_SAML_TEST_CLIENT_CERT_SAML_IDP_MIXIN_H_
