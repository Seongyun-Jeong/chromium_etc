// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/web_ui_browsertest_util.h"

#include <memory>
#include <string>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/hash/hash.h"
#include "base/memory/ref_counted_memory.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/threading/thread_restrictions.h"
#include "content/browser/webui/web_ui_impl.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/common/content_paths.h"
#include "content/public/common/url_constants.h"
#include "net/base/url_util.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "url/gurl.h"

namespace content {

namespace {

void GetResource(const std::string& id,
                 WebUIDataSource::GotDataCallback callback) {
  base::ScopedAllowBlockingForTesting allow_blocking;

  if (id == "error") {
    std::move(callback).Run(nullptr);
    return;
  }

  std::string contents;
  base::FilePath path;
  CHECK(base::PathService::Get(content::DIR_TEST_DATA, &path));
  path = path.AppendASCII(id.substr(0, id.find("?")));
  CHECK(base::ReadFileToString(path, &contents)) << path.value();

  base::RefCountedString* ref_contents = new base::RefCountedString;
  ref_contents->data() = contents;
  std::move(callback).Run(ref_contents);
}

struct WebUIControllerConfig {
  int bindings = BINDINGS_POLICY_WEB_UI;
  std::string child_src = "child-src 'self' chrome://web-ui-subframe/;";
  bool disable_xfo = false;
  bool disable_trusted_types = false;
  std::vector<std::string> requestable_schemes;
  absl::optional<std::vector<std::string>> frame_ancestors;
};

class TestWebUIController : public WebUIController {
 public:
  TestWebUIController(WebUI* web_ui,
                      const GURL& base_url,
                      const WebUIControllerConfig& config)
      : WebUIController(web_ui) {
    web_ui->SetBindings(config.bindings);

    WebUIImpl* web_ui_impl = static_cast<WebUIImpl*>(web_ui);
    for (const auto& scheme : config.requestable_schemes) {
      web_ui_impl->AddRequestableScheme(scheme.c_str());
    }

    WebUIDataSource* data_source = WebUIDataSource::Create(base_url.host());
    data_source->SetRequestFilter(
        base::BindRepeating([](const std::string& path) { return true; }),
        base::BindRepeating(&GetResource));

    data_source->OverrideContentSecurityPolicy(
        network::mojom::CSPDirectiveName::ChildSrc, config.child_src);
    if (config.frame_ancestors.has_value()) {
      for (const auto& frame_ancestor : config.frame_ancestors.value()) {
        data_source->AddFrameAncestor(GURL(frame_ancestor));
      }
    }
    if (config.disable_xfo)
      data_source->DisableDenyXFrameOptions();
    if (config.disable_trusted_types)
      data_source->DisableTrustedTypesCSP();

    WebUIDataSource::Add(web_ui->GetWebContents()->GetBrowserContext(),
                         data_source);
  }
  TestWebUIController(const TestWebUIController&) = delete;
  void operator=(const TestWebUIController&) = delete;
};

}  // namespace

TestUntrustedDataSourceHeaders::TestUntrustedDataSourceHeaders() = default;
TestUntrustedDataSourceHeaders::TestUntrustedDataSourceHeaders(
    const TestUntrustedDataSourceHeaders& other) = default;
TestUntrustedDataSourceHeaders::~TestUntrustedDataSourceHeaders() = default;

void AddUntrustedDataSource(
    BrowserContext* browser_context,
    const std::string& host,
    absl::optional<TestUntrustedDataSourceHeaders> headers) {
  auto* untrusted_data_source =
      WebUIDataSource::Create(GetChromeUntrustedUIURL(host).spec());
  untrusted_data_source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) { return true; }),
      base::BindRepeating(&GetResource));
  if (headers.has_value()) {
    if (headers->child_src.has_value()) {
      untrusted_data_source->OverrideContentSecurityPolicy(
          network::mojom::CSPDirectiveName::ChildSrc,
          headers->child_src.value());
    }
    if (headers->script_src.has_value()) {
      untrusted_data_source->OverrideContentSecurityPolicy(
          network::mojom::CSPDirectiveName::ScriptSrc,
          headers->script_src.value());
    }
    if (headers->default_src.has_value()) {
      untrusted_data_source->OverrideContentSecurityPolicy(
          network::mojom::CSPDirectiveName::DefaultSrc,
          headers->default_src.value());
    }
    if (headers->no_trusted_types)
      untrusted_data_source->DisableTrustedTypesCSP();
    if (headers->no_xfo)
      untrusted_data_source->DisableDenyXFrameOptions();
    if (headers->frame_ancestors.has_value()) {
      for (const auto& frame_ancestor : headers->frame_ancestors.value()) {
        untrusted_data_source->AddFrameAncestor(GURL(frame_ancestor));
      }
    }
    if (headers->cross_origin_opener_policy.has_value()) {
      switch (headers->cross_origin_opener_policy.value()) {
        case network::mojom::CrossOriginOpenerPolicyValue::kUnsafeNone:
          break;
        case network::mojom::CrossOriginOpenerPolicyValue::kSameOrigin:
          untrusted_data_source->OverrideCrossOriginOpenerPolicy("same-origin");
          break;
        case network::mojom::CrossOriginOpenerPolicyValue::kSameOriginPlusCoep:
          untrusted_data_source->OverrideCrossOriginOpenerPolicy("same-origin");
          untrusted_data_source->OverrideCrossOriginEmbedderPolicy(
              "require-corp");
          break;
        case network::mojom::CrossOriginOpenerPolicyValue::
            kSameOriginAllowPopups:
          NOTIMPLEMENTED();
          break;
      }
    }
  }

  WebUIDataSource::Add(browser_context, untrusted_data_source);
}

// static
GURL GetChromeUntrustedUIURL(const std::string& host_and_path) {
  return GURL(std::string(content::kChromeUIUntrustedScheme) +
              url::kStandardSchemeSeparator + host_and_path);
}

TestWebUIControllerFactory::TestWebUIControllerFactory() = default;

std::unique_ptr<WebUIController>
TestWebUIControllerFactory::CreateWebUIControllerForURL(WebUI* web_ui,
                                                        const GURL& url) {
  if (!url.SchemeIs(kChromeUIScheme))
    return nullptr;

  WebUIControllerConfig config;
  config.disable_xfo = disable_xfo_;

  if (url.has_query()) {
    std::string value;
    bool has_value = net::GetValueForKeyInQuery(url, "bindings", &value);
    if (has_value)
      CHECK(base::StringToInt(value, &(config.bindings)));

    has_value = net::GetValueForKeyInQuery(url, "noxfo", &value);
    if (has_value && value == "true")
      config.disable_xfo = true;

    has_value = net::GetValueForKeyInQuery(url, "notrustedtypes", &value);
    if (has_value && value == "true")
      config.disable_trusted_types = true;

    has_value = net::GetValueForKeyInQuery(url, "childsrc", &value);
    if (has_value)
      config.child_src = value;

    has_value = net::GetValueForKeyInQuery(url, "requestableschemes", &value);
    if (has_value) {
      DCHECK(!value.empty());
      std::vector<std::string> schemes = base::SplitString(
          value, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

      config.requestable_schemes.insert(config.requestable_schemes.end(),
                                        schemes.begin(), schemes.end());
    }

    has_value = net::GetValueForKeyInQuery(url, "frameancestors", &value);
    if (has_value) {
      std::vector<std::string> frame_ancestors = base::SplitString(
          value, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

      config.frame_ancestors.emplace(frame_ancestors.begin(),
                                     frame_ancestors.end());
    }
  }

  return std::make_unique<TestWebUIController>(web_ui, url, config);
}

WebUI::TypeID TestWebUIControllerFactory::GetWebUIType(
    BrowserContext* browser_context,
    const GURL& url) {
  if (!url.SchemeIs(kChromeUIScheme))
    return WebUI::kNoWebUI;

  return reinterpret_cast<WebUI::TypeID>(base::FastHash(url.host()));
}

bool TestWebUIControllerFactory::UseWebUIForURL(BrowserContext* browser_context,
                                                const GURL& url) {
  return GetWebUIType(browser_context, url) != WebUI::kNoWebUI;
}

}  // namespace content
