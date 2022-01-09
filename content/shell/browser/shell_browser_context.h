// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_SHELL_BROWSER_CONTEXT_H_
#define CONTENT_SHELL_BROWSER_SHELL_BROWSER_CONTEXT_H_

#include <memory>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/resource_context.h"

class SimpleFactoryKey;

namespace content {

class BackgroundSyncController;
class ContentIndexProvider;
class ClientHintsControllerDelegate;
class DownloadManagerDelegate;
class PermissionControllerDelegate;
class ShellDownloadManagerDelegate;
class ZoomLevelDelegate;

class ShellBrowserContext : public BrowserContext {
 public:
  // If |delay_services_creation| is true, the owner is responsible for calling
  // CreateBrowserContextServices() for this BrowserContext.
  ShellBrowserContext(bool off_the_record,
                      bool delay_services_creation = false);

  ShellBrowserContext(const ShellBrowserContext&) = delete;
  ShellBrowserContext& operator=(const ShellBrowserContext&) = delete;

  ~ShellBrowserContext() override;

  void set_client_hints_controller_delegate(
      ClientHintsControllerDelegate* delegate) {
    client_hints_controller_delegate_ = delegate;
  }

  // BrowserContext implementation.
  base::FilePath GetPath() override;
  std::unique_ptr<ZoomLevelDelegate> CreateZoomLevelDelegate(
      const base::FilePath& partition_path) override;
  bool IsOffTheRecord() override;
  DownloadManagerDelegate* GetDownloadManagerDelegate() override;
  ResourceContext* GetResourceContext() override;
  BrowserPluginGuestManager* GetGuestManager() override;
  storage::SpecialStoragePolicy* GetSpecialStoragePolicy() override;
  PlatformNotificationService* GetPlatformNotificationService() override;
  PushMessagingService* GetPushMessagingService() override;
  StorageNotificationService* GetStorageNotificationService() override;
  SSLHostStateDelegate* GetSSLHostStateDelegate() override;
  PermissionControllerDelegate* GetPermissionControllerDelegate() override;
  BackgroundFetchDelegate* GetBackgroundFetchDelegate() override;
  BackgroundSyncController* GetBackgroundSyncController() override;
  BrowsingDataRemoverDelegate* GetBrowsingDataRemoverDelegate() override;
  ContentIndexProvider* GetContentIndexProvider() override;
  ClientHintsControllerDelegate* GetClientHintsControllerDelegate() override;

 protected:
  // Contains URLRequestContextGetter required for resource loading.
  class ShellResourceContext : public ResourceContext {
   public:
    ShellResourceContext();

    ShellResourceContext(const ShellResourceContext&) = delete;
    ShellResourceContext& operator=(const ShellResourceContext&) = delete;

    ~ShellResourceContext() override;
  };

  bool ignore_certificate_errors() const { return ignore_certificate_errors_; }

  std::unique_ptr<ShellResourceContext> resource_context_;
  std::unique_ptr<ShellDownloadManagerDelegate> download_manager_delegate_;
  std::unique_ptr<PermissionControllerDelegate> permission_manager_;
  std::unique_ptr<BackgroundSyncController> background_sync_controller_;
  std::unique_ptr<ContentIndexProvider> content_index_provider_;

 private:
  // Performs initialization of the ShellBrowserContext while IO is still
  // allowed on the current thread.
  void InitWhileIOAllowed();
  void FinishInitWhileIOAllowed();

  const bool off_the_record_;
  bool ignore_certificate_errors_ = false;
  base::FilePath path_;
  std::unique_ptr<SimpleFactoryKey> key_;
  raw_ptr<ClientHintsControllerDelegate> client_hints_controller_delegate_ =
      nullptr;
};

}  // namespace content

#endif  // CONTENT_SHELL_BROWSER_SHELL_BROWSER_CONTEXT_H_
