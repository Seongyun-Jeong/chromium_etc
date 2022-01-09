// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_MESSAGING_LAYER_UPLOAD_UPLOAD_PROVIDER_H_
#define CHROME_BROWSER_POLICY_MESSAGING_LAYER_UPLOAD_UPLOAD_PROVIDER_H_

#include <memory>
#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/policy/messaging_layer/upload/upload_client.h"
#include "chrome/browser/policy/messaging_layer/util/get_cloud_policy_client.h"
#include "components/reporting/proto/synced/record.pb.h"
#include "components/reporting/storage/storage_module_interface.h"

namespace reporting {

// EncryptedReportingUploadProvider is an adapter for UploadClient
// which uploads reporting messages to the backend service.
class EncryptedReportingUploadProvider {
 public:
  // Resulting `UploadClient` will handle uploading requests to the
  // server. In order to do this it requires a `policy::CloudPolicyClient`.
  // |policy::CloudPolicyClient| may or may not be ready, so we attempt to get
  // it, and if we fail we repost with a backoff. Until an UploadClient is
  // built, all requests to `RequestUploadEncryptedRecords` will fail.
  using UploadClientBuilderCb =
      base::OnceCallback<void(policy::CloudPolicyClient*,
                              UploadClient::CreatedCallback)>;

  explicit EncryptedReportingUploadProvider(
      UploadClient::ReportSuccessfulUploadCallback report_successful_upload_cb,
      UploadClient::EncryptionKeyAttachedCallback encryption_key_attached_cb,
      GetCloudPolicyClientCallback build_cloud_policy_client_cb =
          GetCloudPolicyClientCb(),
      UploadClientBuilderCb upload_client_builder_cb =
          EncryptedReportingUploadProvider::GetUploadClientBuilder());
  EncryptedReportingUploadProvider(
      const EncryptedReportingUploadProvider& other) = delete;
  EncryptedReportingUploadProvider& operator=(
      const EncryptedReportingUploadProvider& other) = delete;
  virtual ~EncryptedReportingUploadProvider();

  // Called to upload records and/or request encryption key.
  void RequestUploadEncryptedRecords(
      bool need_encryption_key,
      std::unique_ptr<std::vector<EncryptedRecord>> records,
      base::OnceCallback<void(Status)> result_cb);

 private:
  // EncryptedReportingUploadProvider helper class.
  class UploadHelper;

  // Default provider of upload client builder.
  static UploadClientBuilderCb GetUploadClientBuilder();

  // UploadHelper object.
  const scoped_refptr<UploadHelper> helper_;

  // Keep this last so that all weak pointers will be invalidated at the
  // beginning of destruction.
  base::WeakPtrFactory<EncryptedReportingUploadProvider> weak_ptr_factory_{
      this};
};

}  // namespace reporting

#endif  // CHROME_BROWSER_POLICY_MESSAGING_LAYER_UPLOAD_UPLOAD_PROVIDER_H_
