// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_APP_LIST_TEST_UTIL_H_
#define CHROME_BROWSER_UI_APP_LIST_APP_LIST_TEST_UTIL_H_

#include "chrome/browser/extensions/extension_service_test_base.h"
#include "components/sync/model/sync_data.h"
#include "components/sync/protocol/app_list_specifics.pb.h"

namespace web_app {
class TestWebAppUrlLoader;
}  // namespace web_app

// Base class for app list unit tests that use the "app_list" test profile.
class AppListTestBase : public extensions::ExtensionServiceTestBase {
 public:
  static const char kHostedAppId[];
  static const char kPackagedApp1Id[];
  static const char kPackagedApp2Id[];

  AppListTestBase();
  ~AppListTestBase() override;

  void SetUp() override;

  web_app::TestWebAppUrlLoader& url_loader() { return *url_loader_; }

 private:
  void ConfigureWebAppProvider();

  web_app::TestWebAppUrlLoader* url_loader_ = nullptr;
};

// Test util constants --------------------------------------------------------

extern const char kUnset[];
extern const char kDefault[];
extern const char kOemAppName[];
extern const char kSomeAppName[];

// Test util functions ---------------------------------------------------------

scoped_refptr<extensions::Extension> MakeApp(
    const std::string& name,
    const std::string& id,
    extensions::Extension::InitFromValueFlags flags);

// Creates next by natural sort ordering application id. Application id has to
// have 32 chars each in range 'a' to 'p' inclusively.
std::string CreateNextAppId(const std::string& app_id);

syncer::SyncData CreateAppRemoteData(
    const std::string& id,
    const std::string& name,
    const std::string& parent_id,
    const std::string& item_ordinal,
    const std::string& item_pin_ordinal,
    sync_pb::AppListSpecifics_AppListItemType item_type =
        sync_pb::AppListSpecifics_AppListItemType_TYPE_APP);

#endif  // CHROME_BROWSER_UI_APP_LIST_APP_LIST_TEST_UTIL_H_
