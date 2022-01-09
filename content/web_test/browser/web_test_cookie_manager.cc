// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/web_test/browser/web_test_cookie_manager.h"

#include "content/public/browser/storage_partition.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "url/gurl.h"

namespace content {

WebTestCookieManager::WebTestCookieManager(
    network::mojom::CookieManager* const cookie_manager,
    const GURL& url)
    : cookie_manager_(cookie_manager), url_(url) {
  DCHECK(url_.is_valid());
}

void WebTestCookieManager::DeleteAllCookies(
    blink::test::mojom::CookieManagerAutomation::DeleteAllCookiesCallback
        callback) {
  network::mojom::CookieDeletionFilterPtr deletion_filter =
      network::mojom::CookieDeletionFilter::New();
  deletion_filter->url = url_;
  cookie_manager_->DeleteCookies(
      std::move(deletion_filter),
      base::BindOnce(
          [](blink::test::mojom::CookieManagerAutomation::
                 DeleteAllCookiesCallback callback,
             uint32_t) {
            // There is no ability to detect rejection here.
            std::move(callback).Run();
          },
          std::move(callback)));
}

}  // namespace content
