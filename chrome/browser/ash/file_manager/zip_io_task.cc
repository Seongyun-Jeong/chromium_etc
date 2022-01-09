// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/file_manager/zip_io_task.h"

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/files/file.h"
#include "base/files/file_error_or.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/system/sys_info.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/ash/file_manager/fileapi_util.h"
#include "chrome/browser/ash/file_manager/io_task.h"
#include "chrome/browser/chromeos/fileapi/file_system_backend.h"
#include "chrome/browser/file_util_service.h"
#include "chrome/services/file_util/public/cpp/zip_file_creator.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "storage/browser/file_system/file_system_context.h"
#include "storage/browser/file_system/file_system_url.h"

namespace file_manager {
namespace io_task {

namespace {

int64_t ComputeSize(base::FilePath src_dir,
                    std::vector<base::FilePath> src_files) {
  VLOG(1) << ">>> Computing total size of " << src_files.size() << " items...";
  int64_t total_bytes = 0;
  base::File::Info info;
  for (const base::FilePath& relative_path : src_files) {
    const base::FilePath absolute_path = src_dir.Append(relative_path);
    if (base::GetFileInfo(absolute_path, &info))
      total_bytes += info.is_directory
                         ? base::ComputeDirectorySize(absolute_path)
                         : info.size;
  }
  VLOG(1) << "<<< Total size is " << total_bytes << " bytes";
  return total_bytes;
}

}  // namespace

ZipIOTask::ZipIOTask(
    std::vector<storage::FileSystemURL> source_urls,
    storage::FileSystemURL parent_folder,
    scoped_refptr<storage::FileSystemContext> file_system_context)
    : file_system_context_(file_system_context) {
  progress_.state = State::kQueued;
  progress_.type = OperationType::kZip;
  progress_.destination_folder = std::move(parent_folder);
  progress_.bytes_transferred = 0;
  progress_.total_bytes = 0;

  for (auto& url : source_urls) {
    progress_.sources.emplace_back(std::move(url), absl::nullopt);
  }
}

ZipIOTask::~ZipIOTask() {
  if (zip_file_creator_) {
    zip_file_creator_->Stop();
  }
}

void ZipIOTask::Execute(IOTask::ProgressCallback progress_callback,
                        IOTask::CompleteCallback complete_callback) {
  progress_callback_ = std::move(progress_callback);
  complete_callback_ = std::move(complete_callback);

  if (progress_.sources.size() == 0) {
    Complete(State::kSuccess);
    return;
  }
  progress_.state = State::kInProgress;

  // Convert the destination folder URL to absolute path.
  source_dir_ = progress_.destination_folder.path();
  if (!chromeos::FileSystemBackend::CanHandleURL(
          progress_.destination_folder) ||
      source_dir_.empty()) {
    progress_.outputs.emplace_back(progress_.destination_folder,
                                   base::File::FILE_ERROR_NOT_FOUND);
    Complete(State::kError);
    return;
  }

  // Convert source file URLs to relative paths.
  for (EntryStatus& source : progress_.sources) {
    const base::FilePath absolute_path = source.url.path();
    if (!chromeos::FileSystemBackend::CanHandleURL(source.url) ||
        absolute_path.empty()) {
      source.error = base::File::FILE_ERROR_NOT_FOUND;
      Complete(State::kError);
      return;
    }

    base::FilePath relative_path;
    if (!source_dir_.AppendRelativePath(absolute_path, &relative_path)) {
      source.error = base::File::FILE_ERROR_INVALID_OPERATION;
      Complete(State::kError);
      return;
    }
    source_relative_paths_.push_back(std::move(relative_path));
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ComputeSize, source_dir_, source_relative_paths_),
      base::BindOnce(&ZipIOTask::GenerateZipNameAfterGotTotalBytes,
                     weak_ptr_factory_.GetWeakPtr()));
}

void ZipIOTask::Cancel() {
  progress_.state = State::kCancelled;
  // Any inflight operation will be cancelled when the task is destroyed.
}

// Calls the completion callback for the task. |progress_| should not be
// accessed after calling this.
void ZipIOTask::Complete(State state) {
  progress_.state = state;
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(complete_callback_), std::move(progress_)));
}

// Generates the destination url for the ZIP file.
void ZipIOTask::GenerateZipNameAfterGotTotalBytes(int64_t total_bytes) {
  progress_.total_bytes = total_bytes;
  speedometer_.SetTotalBytes(progress_.total_bytes);

  // TODO(crbug.com/1238237) Localize the name.
  base::FilePath zip_name("Archive.zip");
  if (source_relative_paths_.size() == 1) {
    zip_name = source_relative_paths_[0].BaseName().ReplaceExtension("zip");
  }
  util::GenerateUnusedFilename(
      progress_.destination_folder, zip_name, file_system_context_,
      base::BindOnce(&ZipIOTask::ZipItems, weak_ptr_factory_.GetWeakPtr()));
}

// Starts the zip operation.
void ZipIOTask::ZipItems(
    base::FileErrorOr<storage::FileSystemURL> destination_result) {
  if (destination_result.is_error()) {
    progress_.outputs.emplace_back(progress_.destination_folder,
                                   destination_result.error());
    Complete(State::kError);
    return;
  }
  progress_.outputs.emplace_back(destination_result.value(), absl::nullopt);
  progress_callback_.Run(progress_);

  zip_file_creator_ = base::MakeRefCounted<ZipFileCreator>(
      std::move(source_dir_), std::move(source_relative_paths_),
      std::move(destination_result->path()));
  zip_file_creator_->SetProgressCallback(base::BindOnce(
      &ZipIOTask::OnZipProgress, weak_ptr_factory_.GetWeakPtr()));
  zip_file_creator_->SetCompletionCallback(base::BindOnce(
      &ZipIOTask::OnZipComplete, weak_ptr_factory_.GetWeakPtr()));
  zip_file_creator_->Start(LaunchFileUtilService());
}

void ZipIOTask::OnZipProgress() {
  DCHECK(zip_file_creator_);
  progress_.bytes_transferred = zip_file_creator_->GetProgress().bytes;
  speedometer_.Update(progress_.bytes_transferred);
  const double remaining_seconds = speedometer_.GetRemainingSeconds();

  // Speedometer can produce infinite result which can't be serialized to JSON
  // when sending the status via private API.
  if (std::isfinite(remaining_seconds)) {
    progress_.remaining_seconds = remaining_seconds;
  }

  progress_callback_.Run(progress_);
  if (zip_file_creator_->GetResult() == ZipFileCreator::kInProgress) {
    zip_file_creator_->SetProgressCallback(base::BindOnce(
        &ZipIOTask::OnZipProgress, weak_ptr_factory_.GetWeakPtr()));
  }
}

void ZipIOTask::OnZipComplete() {
  DCHECK(zip_file_creator_);
  progress_.bytes_transferred = zip_file_creator_->GetProgress().bytes;
  switch (zip_file_creator_->GetResult()) {
    case ZipFileCreator::kSuccess:
      progress_.outputs.back().error = base::File::FILE_OK;
      Complete(State::kSuccess);
      break;
    case ZipFileCreator::kError:
      progress_.outputs.back().error = base::File::FILE_ERROR_FAILED;
      LOG(ERROR) << "Cannot create Zip archive: "
                 << zip_file_creator_->GetResult();
      Complete(State::kError);
      break;
    case ZipFileCreator::kInProgress:
    case ZipFileCreator::kCancelled:
      // This class should be destroyed on cancel, so we should never get here.
      NOTREACHED();
  }
  zip_file_creator_.reset();
}

}  // namespace io_task
}  // namespace file_manager
