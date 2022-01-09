// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_TELEMETRY_API_FAKE_API_GUARD_DELEGATE_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_TELEMETRY_API_FAKE_API_GUARD_DELEGATE_H_

#include <memory>
#include <string>

#include "chrome/browser/chromeos/extensions/telemetry/api/api_guard_delegate.h"

namespace content {
class BrowserContext;
}

namespace extensions {
class Extension;
}

namespace chromeos {

class FakeApiGuardDelegate : public ApiGuardDelegate {
 public:
  class Factory : public ApiGuardDelegate::Factory {
   public:
    explicit Factory(std::string error_message);
    ~Factory() override;

   protected:
    // ApiGuardDelegate::Factory:
    std::unique_ptr<ApiGuardDelegate> CreateInstance() override;

   private:
    std::string error_message_;
  };

  FakeApiGuardDelegate(const FakeApiGuardDelegate&) = delete;
  FakeApiGuardDelegate& operator=(const FakeApiGuardDelegate&) = delete;
  ~FakeApiGuardDelegate() override;

  // ApiGuardDelegate:
  void CanAccessApi(content::BrowserContext* context,
                    const extensions::Extension* extension,
                    CanAccessApiCallback callback) override;

 protected:
  explicit FakeApiGuardDelegate(std::string error_message);

 private:
  // Error message returned when calling CanAccessApi().
  std::string error_message_;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_TELEMETRY_API_FAKE_API_GUARD_DELEGATE_H_
