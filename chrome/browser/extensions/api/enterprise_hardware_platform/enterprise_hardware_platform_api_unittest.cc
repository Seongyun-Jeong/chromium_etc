// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/enterprise_hardware_platform/enterprise_hardware_platform_api.h"

#include <memory>
#include <string>
#include <utility>

#include "chrome/browser/extensions/extension_service_test_with_install.h"
#include "chrome/common/pref_names.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "extensions/browser/api_test_utils.h"
#include "extensions/common/extension_builder.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

class EnterpriseHardwarePlatformAPITest
    : public ExtensionServiceTestWithInstall {
 public:
  EnterpriseHardwarePlatformAPITest() = default;

  EnterpriseHardwarePlatformAPITest(const EnterpriseHardwarePlatformAPITest&) =
      delete;
  EnterpriseHardwarePlatformAPITest& operator=(
      const EnterpriseHardwarePlatformAPITest&) = delete;

  ~EnterpriseHardwarePlatformAPITest() override = default;

 protected:
  EnterpriseHardwarePlatformGetHardwarePlatformInfoFunction* function() {
    return function_.get();
  }

 private:
  void SetUp() override {
    ExtensionServiceTestWithInstall::SetUp();
    InitializeExtensionService(ExtensionServiceInitParams());

    extension_ = ExtensionBuilder("Test").Build();
    function_ = new EnterpriseHardwarePlatformGetHardwarePlatformInfoFunction();
    function_->set_extension(extension_.get());
    function_->set_has_callback(true);
  }

  void TearDown() override {
    function_.reset();
    extension_.reset();
    ExtensionServiceTestWithInstall::TearDown();
  }

  scoped_refptr<const Extension> extension_;
  scoped_refptr<EnterpriseHardwarePlatformGetHardwarePlatformInfoFunction>
      function_;
};

TEST_F(EnterpriseHardwarePlatformAPITest, GetHardwarePlatformInfoAllowed) {
  testing_pref_service()->SetManagedPref(
      prefs::kEnterpriseHardwarePlatformAPIEnabled,
      std::make_unique<base::Value>(true));

  std::unique_ptr<base::Value> result =
      api_test_utils::RunFunctionAndReturnSingleResult(function(), "[]",
                                                       browser_context());
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_dict());
  ASSERT_EQ(result->DictSize(), 2u);

  const base::Value* val =
      result->FindKeyOfType("manufacturer", base::Value::Type::STRING);
  ASSERT_TRUE(val);
  const std::string& manufacturer = val->GetString();

  val = result->FindKeyOfType("model", base::Value::Type::STRING);
  ASSERT_TRUE(val);
  const std::string& model = val->GetString();

  EXPECT_FALSE(manufacturer.empty());
  EXPECT_FALSE(model.empty());
}

TEST_F(EnterpriseHardwarePlatformAPITest,
       GetHardwarePlatformInfoNotAllowedExplicit) {
  testing_pref_service()->SetDefaultPrefValue(
      prefs::kEnterpriseHardwarePlatformAPIEnabled, base::Value(false));
  std::string error = api_test_utils::RunFunctionAndReturnError(
      function(), "[]", browser_context());
  EXPECT_FALSE(error.empty());
}

TEST_F(EnterpriseHardwarePlatformAPITest,
       GetHardwarePlatformInfoNotAllowedImplicit) {
  std::string error = api_test_utils::RunFunctionAndReturnError(
      function(), "[]", browser_context());
  EXPECT_FALSE(error.empty());
}

}  // namespace extensions
