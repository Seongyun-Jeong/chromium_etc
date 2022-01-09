// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_FILE_SYSTEM_PROVIDER_OPERATIONS_WRITE_FILE_H_
#define CHROME_BROWSER_ASH_FILE_SYSTEM_PROVIDER_OPERATIONS_WRITE_FILE_H_

#include <stdint.h>

#include <memory>

#include "base/files/file.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/ash/file_system_provider/operations/operation.h"
#include "chrome/browser/ash/file_system_provider/provided_file_system_info.h"
#include "chrome/browser/ash/file_system_provider/provided_file_system_interface.h"
#include "chrome/browser/ash/file_system_provider/request_value.h"
#include "net/base/io_buffer.h"
#include "storage/browser/file_system/async_file_util.h"

namespace extensions {
class EventRouter;
}  // namespace extensions

namespace ash {
namespace file_system_provider {
namespace operations {

// Bridge between fileapi write file and providing extension's write fil
// request.
// Created per request.
class WriteFile : public Operation {
 public:
  WriteFile(extensions::EventRouter* event_router,
            const ProvidedFileSystemInfo& file_system_info,
            int file_handle,
            scoped_refptr<net::IOBuffer> buffer,
            int64_t offset,
            int length,
            storage::AsyncFileUtil::StatusCallback callback);

  WriteFile(const WriteFile&) = delete;
  WriteFile& operator=(const WriteFile&) = delete;

  ~WriteFile() override;

  // Operation overrides.
  bool Execute(int request_id) override;
  void OnSuccess(int request_id,
                 std::unique_ptr<RequestValue> result,
                 bool has_more) override;
  void OnError(int request_id,
               std::unique_ptr<RequestValue> result,
               base::File::Error error) override;

 private:
  int file_handle_;
  scoped_refptr<net::IOBuffer> buffer_;
  int64_t offset_;
  int length_;
  storage::AsyncFileUtil::StatusCallback callback_;
};

}  // namespace operations
}  // namespace file_system_provider
}  // namespace ash

#endif  // CHROME_BROWSER_ASH_FILE_SYSTEM_PROVIDER_OPERATIONS_WRITE_FILE_H_
