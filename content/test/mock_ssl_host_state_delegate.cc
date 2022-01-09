// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/mock_ssl_host_state_delegate.h"

#include "base/callback.h"
#include "base/containers/contains.h"

namespace content {

MockSSLHostStateDelegate::MockSSLHostStateDelegate() {}

MockSSLHostStateDelegate::~MockSSLHostStateDelegate() {}

void MockSSLHostStateDelegate::AllowCert(const std::string& host,
                                         const net::X509Certificate& cert,
                                         int error,
                                         WebContents* web_contents) {
  exceptions_.insert(host);
}

void MockSSLHostStateDelegate::Clear(
    base::RepeatingCallback<bool(const std::string&)> host_filter) {
  if (host_filter.is_null()) {
    exceptions_.clear();
  } else {
    for (auto it = exceptions_.begin(); it != exceptions_.end();) {
      auto next_it = std::next(it);

      if (host_filter.Run(*it))
        exceptions_.erase(it);

      it = next_it;
    }
  }
}

SSLHostStateDelegate::CertJudgment MockSSLHostStateDelegate::QueryPolicy(
    const std::string& host,
    const net::X509Certificate& cert,
    int error,
    WebContents* web_contents) {
  if (exceptions_.find(host) == exceptions_.end())
    return SSLHostStateDelegate::DENIED;

  return SSLHostStateDelegate::ALLOWED;
}

void MockSSLHostStateDelegate::HostRanInsecureContent(
    const std::string& host,
    int child_id,
    InsecureContentType content_type) {
  hosts_ran_insecure_content_.insert(host);
}

bool MockSSLHostStateDelegate::DidHostRunInsecureContent(
    const std::string& host,
    int child_id,
    InsecureContentType content_type) {
  return hosts_ran_insecure_content_.find(host) !=
         hosts_ran_insecure_content_.end();
}

void MockSSLHostStateDelegate::AllowHttpForHost(const std::string& host,
                                                WebContents* web_contents) {
  allow_http_hosts_.insert(host);
}

bool MockSSLHostStateDelegate::IsHttpAllowedForHost(const std::string& host,
                                                    WebContents* web_contents) {
  return base::Contains(allow_http_hosts_, host);
}

void MockSSLHostStateDelegate::RevokeUserAllowExceptions(
    const std::string& host) {
  exceptions_.erase(exceptions_.find(host));
}

bool MockSSLHostStateDelegate::HasAllowException(const std::string& host,
                                                 WebContents* web_contents) {
  return exceptions_.find(host) != exceptions_.end();
}

}  // namespace content
