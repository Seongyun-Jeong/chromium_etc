// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/file_system_access/safe_move_helper.h"

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/guid.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/test/gmock_callback_support.h"
#include "content/browser/file_system_access/file_system_access_write_lock_manager.h"
#include "content/browser/file_system_access/fixed_file_system_access_permission_grant.h"
#include "content/browser/file_system_access/mock_file_system_access_permission_context.h"
#include "content/browser/file_system_access/safe_move_helper.h"
#include "content/public/test/browser_task_environment.h"
#include "storage/browser/file_system/file_system_operation.h"
#include "storage/browser/quota/quota_manager_proxy.h"
#include "storage/browser/test/async_file_test_helper.h"
#include "storage/browser/test/test_file_system_backend.h"
#include "storage/browser/test/test_file_system_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/mojom/file_system_access/file_system_access_error.mojom.h"
#include "url/gurl.h"

using blink::mojom::FileSystemAccessStatus;
using storage::FileSystemURL;

using testing::_;
using testing::AllOf;
using testing::Eq;
using testing::Field;

namespace content {

namespace {

class MockQuarantine : public quarantine::mojom::Quarantine {
 public:
  MockQuarantine() = default;

  void QuarantineFile(const base::FilePath& full_path,
                      const GURL& source_url,
                      const GURL& referrer_url,
                      const std::string& client_guid,
                      QuarantineFileCallback callback) override {
    paths.push_back(full_path);
    std::move(callback).Run(result);
  }

  void MakeSecurityCheckFail() {
    result = quarantine::mojom::QuarantineFileResult::SECURITY_CHECK_FAILED;
  }

  std::vector<base::FilePath> paths;
  quarantine::mojom::QuarantineFileResult result =
      quarantine::mojom::QuarantineFileResult::OK;
};

// File System Backend that can notify whenever a FileSystemOperation is
// created. This lets tests simulate race conditions between file operations and
// other work.
class TestFileSystemBackend : public storage::TestFileSystemBackend {
 public:
  TestFileSystemBackend(base::SequencedTaskRunner* task_runner,
                        const base::FilePath& base_path)
      : storage::TestFileSystemBackend(task_runner, base_path) {}

  std::unique_ptr<storage::FileSystemOperation> CreateFileSystemOperation(
      const storage::FileSystemURL& url,
      storage::FileSystemContext* context,
      base::File::Error* error_code) const override {
    if (operation_created_callback_)
      std::move(operation_created_callback_).Run(url);
    return storage::TestFileSystemBackend::CreateFileSystemOperation(
        url, context, error_code);
  }

  void SetOperationCreatedCallback(
      base::OnceCallback<void(const storage::FileSystemURL&)> callback) {
    operation_created_callback_ = std::move(callback);
  }

 private:
  mutable base::OnceCallback<void(const storage::FileSystemURL&)>
      operation_created_callback_;
};

}  // namespace

std::string GetHexEncodedString(const std::string& input) {
  return base::HexEncode(base::as_bytes(base::make_span(input)));
}

class SafeMoveHelperTest : public testing::Test {
 public:
  SafeMoveHelperTest()
      : task_environment_(base::test::TaskEnvironment::MainThreadType::IO) {}

  virtual FileSystemAccessPermissionContext* permission_context() {
    return nullptr;
  }

  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    std::vector<std::unique_ptr<storage::FileSystemBackend>>
        additional_providers;
    additional_providers.push_back(std::make_unique<TestFileSystemBackend>(
        base::ThreadTaskRunnerHandle::Get().get(), dir_.GetPath()));
    test_file_system_backend_ =
        static_cast<TestFileSystemBackend*>(additional_providers[0].get());

    file_system_context_ =
        storage::CreateFileSystemContextWithAdditionalProvidersForTesting(
            base::ThreadTaskRunnerHandle::Get(),
            base::ThreadTaskRunnerHandle::Get(),
            /*quota_manager_proxy=*/nullptr, std::move(additional_providers),
            dir_.GetPath());

    test_dest_url_ = file_system_context_->CreateCrackedFileSystemURL(
        kTestStorageKey, storage::kFileSystemTypeLocal,
        dir_.GetPath().AppendASCII("dest"));

    test_source_url_ = file_system_context_->CreateCrackedFileSystemURL(
        kTestStorageKey, storage::kFileSystemTypeLocal,
        dir_.GetPath().AppendASCII("source"));

    ASSERT_EQ(base::File::FILE_OK,
              storage::AsyncFileTestHelper::CreateFile(
                  file_system_context_.get(), test_source_url_));

    chrome_blob_context_ = base::MakeRefCounted<ChromeBlobStorageContext>();
    chrome_blob_context_->InitializeOnIOThread(base::FilePath(),
                                               base::FilePath(), nullptr);
    blob_context_ = chrome_blob_context_->context();

    manager_ = base::MakeRefCounted<FileSystemAccessManagerImpl>(
        file_system_context_, chrome_blob_context_,
        /*permission_context=*/permission_context(),
        /*off_the_record=*/false);

    quarantine_callback_ = base::BindLambdaForTesting(
        [&](mojo::PendingReceiver<quarantine::mojom::Quarantine> receiver) {
          quarantine_receivers_.Add(&quarantine_, std::move(receiver));
        });

    auto lock = manager_->TakeWriteLock(
        test_dest_url_,
        FileSystemAccessWriteLockManager::WriteLockType::kShared);
    ASSERT_TRUE(lock.has_value());

    helper_ = std::make_unique<SafeMoveHelper>(
        manager_->AsWeakPtr(),
        FileSystemAccessManagerImpl::BindingContext(kTestStorageKey, kTestURL,
                                                    kFrameId),
        test_source_url_, test_dest_url_,
        storage::FileSystemOperation::CopyOrMoveOptionSet(
            storage::FileSystemOperation::CopyOrMoveOption::
                kPreserveDestinationPermissions),
        quarantine_callback_,
        /*has_transient_user_activation=*/false);
  }

  void TearDown() override {
    manager_.reset();

    task_environment_.RunUntilIdle();
    EXPECT_TRUE(dir_.Delete());
  }

 protected:
  const GURL kTestURL = GURL("https://example.com/test");
  const blink::StorageKey kTestStorageKey =
      blink::StorageKey::CreateFromStringForTesting("https://example.com/test");
  const int kProcessId = 1;
  const int kFrameRoutingId = 2;
  const GlobalRenderFrameHostId kFrameId{kProcessId, kFrameRoutingId};
  BrowserTaskEnvironment task_environment_;

  base::ScopedTempDir dir_;
  scoped_refptr<storage::FileSystemContext> file_system_context_;
  raw_ptr<TestFileSystemBackend> test_file_system_backend_;
  scoped_refptr<ChromeBlobStorageContext> chrome_blob_context_;
  raw_ptr<storage::BlobStorageContext> blob_context_;
  scoped_refptr<FileSystemAccessManagerImpl> manager_;

  FileSystemURL test_dest_url_;
  FileSystemURL test_source_url_;

  MockQuarantine quarantine_;
  mojo::ReceiverSet<quarantine::mojom::Quarantine> quarantine_receivers_;
  download::QuarantineConnectionCallback quarantine_callback_;

  scoped_refptr<FixedFileSystemAccessPermissionGrant> permission_grant_ =
      base::MakeRefCounted<FixedFileSystemAccessPermissionGrant>(
          FixedFileSystemAccessPermissionGrant::PermissionStatus::GRANTED,
          base::FilePath());

  std::unique_ptr<SafeMoveHelper> helper_;
};

TEST_F(SafeMoveHelperTest, HashSimpleOK) {
  EXPECT_TRUE(base::WriteFile(test_source_url_.path(), "abc"));

  base::RunLoop loop;
  helper_->ComputeHashForSourceFileForTesting(base::BindLambdaForTesting(
      [&](base::File::Error result, const std::string& hash_value,
          int64_t size) {
        EXPECT_EQ(base::File::FILE_OK, result);
        EXPECT_EQ(
            "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
            GetHexEncodedString(hash_value));
        EXPECT_EQ(3, size);
        loop.Quit();
      }));
  loop.Run();
}

TEST_F(SafeMoveHelperTest, HashEmptyOK) {
  base::RunLoop loop;
  helper_->ComputeHashForSourceFileForTesting(base::BindLambdaForTesting(
      [&](base::File::Error result, const std::string& hash_value,
          int64_t size) {
        EXPECT_EQ(base::File::FILE_OK, result);
        EXPECT_EQ(
            "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
            GetHexEncodedString(hash_value));
        EXPECT_EQ(0, size);
        loop.Quit();
      }));
  loop.Run();
}

TEST_F(SafeMoveHelperTest, HashNonExistingFileFails) {
  ASSERT_EQ(base::File::FILE_OK,
            storage::AsyncFileTestHelper::Remove(file_system_context_.get(),
                                                 test_source_url_,
                                                 /*recursive=*/false));
  base::RunLoop loop;
  helper_->ComputeHashForSourceFileForTesting(base::BindLambdaForTesting(
      [&](base::File::Error result, const std::string& hash_value,
          int64_t size) {
        EXPECT_EQ(base::File::FILE_ERROR_NOT_FOUND, result);
        loop.Quit();
      }));
  loop.Run();
}

TEST_F(SafeMoveHelperTest, HashLargerFileOK) {
  size_t target_size = 9 * 1024u;
  std::string file_data(target_size, '0');
  EXPECT_TRUE(base::WriteFile(test_source_url_.path(), file_data));

  base::RunLoop loop;
  helper_->ComputeHashForSourceFileForTesting(base::BindLambdaForTesting(
      [&](base::File::Error result, const std::string& hash_value,
          int64_t size) {
        EXPECT_EQ(base::File::FILE_OK, result);
        EXPECT_EQ(
            "34A82D28CB1E0BA92CADC4BE8497DC9EEA9AC4F63B9C445A9E52D298990AC491",
            GetHexEncodedString(hash_value));
        EXPECT_EQ(static_cast<int64_t>(target_size), size);
        loop.Quit();
      }));
  loop.Run();
}

TEST_F(SafeMoveHelperTest, Simple) {
  EXPECT_TRUE(base::WriteFile(test_source_url_.path(), "abc"));

  base::RunLoop loop;
  helper_->Start(base::BindLambdaForTesting(
      [&](blink::mojom::FileSystemAccessErrorPtr result) {
        EXPECT_EQ(result->status, FileSystemAccessStatus::kOk);
        loop.Quit();
      }));
  loop.Run();

  EXPECT_FALSE(storage::AsyncFileTestHelper::FileExists(
      file_system_context_.get(), test_source_url_,
      storage::AsyncFileTestHelper::kDontCheckSize));
  EXPECT_TRUE(storage::AsyncFileTestHelper::FileExists(
      file_system_context_.get(), test_dest_url_, 3));
}

TEST_F(SafeMoveHelperTest, DestExists) {
  EXPECT_TRUE(base::WriteFile(test_source_url_.path(), "abc"));

  ASSERT_EQ(base::File::FILE_OK,
            storage::AsyncFileTestHelper::CreateFile(file_system_context_.get(),
                                                     test_dest_url_));
  EXPECT_TRUE(base::WriteFile(test_dest_url_.path(), "hi"));

  base::RunLoop loop;
  helper_->Start(base::BindLambdaForTesting(
      [&](blink::mojom::FileSystemAccessErrorPtr result) {
        EXPECT_EQ(result->status, FileSystemAccessStatus::kOk);
        loop.Quit();
      }));
  loop.Run();

  EXPECT_FALSE(storage::AsyncFileTestHelper::FileExists(
      file_system_context_.get(), test_source_url_,
      storage::AsyncFileTestHelper::kDontCheckSize));
  EXPECT_TRUE(storage::AsyncFileTestHelper::FileExists(
      file_system_context_.get(), test_dest_url_, 3));
}

TEST_F(SafeMoveHelperTest, SecurityCheckFailed) {
  quarantine_.MakeSecurityCheckFail();

  EXPECT_TRUE(base::WriteFile(test_source_url_.path(), "abc"));

  base::RunLoop loop;
  helper_->Start(base::BindLambdaForTesting(
      [&](blink::mojom::FileSystemAccessErrorPtr result) {
        EXPECT_EQ(result->status, FileSystemAccessStatus::kOperationAborted);
        loop.Quit();
      }));
  loop.Run();

  // Even though the file failed quarantine, it's already been moved. There's
  // not much we can do other than return an error.
  EXPECT_FALSE(storage::AsyncFileTestHelper::FileExists(
      file_system_context_.get(), test_source_url_,
      storage::AsyncFileTestHelper::kDontCheckSize));
  EXPECT_TRUE(storage::AsyncFileTestHelper::FileExists(
      file_system_context_.get(), test_dest_url_, 3));
}

class SafeMoveHelperAfterWriteChecksTest : public SafeMoveHelperTest {
 public:
  FileSystemAccessPermissionContext* permission_context() override {
    return &permission_context_;
  }

 protected:
  testing::StrictMock<MockFileSystemAccessPermissionContext>
      permission_context_;
};

TEST_F(SafeMoveHelperAfterWriteChecksTest, Allow) {
  EXPECT_TRUE(base::WriteFile(test_source_url_.path(), "abc"));

  std::string expected_hash;
  ASSERT_TRUE(base::HexStringToString(
      "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
      &expected_hash));

  EXPECT_CALL(
      permission_context_,
      PerformAfterWriteChecks_(
          AllOf(
              Field(&FileSystemAccessWriteItem::target_file_path,
                    Eq(test_dest_url_.path())),
              Field(&FileSystemAccessWriteItem::full_path,
                    Eq(test_source_url_.path())),
              Field(&FileSystemAccessWriteItem::sha256_hash, Eq(expected_hash)),
              Field(&FileSystemAccessWriteItem::size, Eq(3)),
              Field(&FileSystemAccessWriteItem::frame_url, Eq(kTestURL)),
              Field(&FileSystemAccessWriteItem::has_user_gesture, Eq(false))),
          kFrameId, _))
      .WillOnce(base::test::RunOnceCallback<2>(
          FileSystemAccessPermissionContext::AfterWriteCheckResult::kAllow));

  helper_->Start(base::BindLambdaForTesting(
      [](blink::mojom::FileSystemAccessErrorPtr result) {
        EXPECT_EQ(result->status, FileSystemAccessStatus::kOk);
      }));

  task_environment_.RunUntilIdle();
  EXPECT_FALSE(storage::AsyncFileTestHelper::FileExists(
      file_system_context_.get(), test_source_url_,
      storage::AsyncFileTestHelper::kDontCheckSize));
  EXPECT_TRUE(storage::AsyncFileTestHelper::FileExists(
      file_system_context_.get(), test_dest_url_, 3));
}

TEST_F(SafeMoveHelperAfterWriteChecksTest, Block) {
  EXPECT_TRUE(base::WriteFile(test_source_url_.path(), "abc"));

  EXPECT_CALL(permission_context_, PerformAfterWriteChecks_(_, kFrameId, _))
      .WillOnce(base::test::RunOnceCallback<2>(
          FileSystemAccessPermissionContext::AfterWriteCheckResult::kBlock));

  helper_->Start(base::BindLambdaForTesting(
      [](blink::mojom::FileSystemAccessErrorPtr result) {
        EXPECT_EQ(result->status, FileSystemAccessStatus::kOperationAborted);
      }));

  task_environment_.RunUntilIdle();
  // File should not have been moved.
  EXPECT_TRUE(storage::AsyncFileTestHelper::FileExists(
      file_system_context_.get(), test_source_url_, 3));
  EXPECT_FALSE(storage::AsyncFileTestHelper::FileExists(
      file_system_context_.get(), test_dest_url_,
      storage::AsyncFileTestHelper::kDontCheckSize));
}

}  // namespace content
