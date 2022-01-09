// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reporting/storage/missive_storage_module.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/memory/ptr_util.h"
#include "components/reporting/proto/synced/record.pb.h"
#include "components/reporting/proto/synced/record_constants.pb.h"
#include "components/reporting/util/status.h"

namespace reporting {

using MissiveStorageModuleDelegateInterface =
    MissiveStorageModule::MissiveStorageModuleDelegateInterface;

MissiveStorageModuleDelegateInterface::MissiveStorageModuleDelegateInterface() =
    default;
MissiveStorageModuleDelegateInterface::
    ~MissiveStorageModuleDelegateInterface() = default;

MissiveStorageModule::MissiveStorageModule(
    std::unique_ptr<MissiveStorageModuleDelegateInterface> delegate)
    : delegate_(std::move(delegate)) {}

MissiveStorageModule::~MissiveStorageModule() = default;

// static
scoped_refptr<MissiveStorageModule> MissiveStorageModule::Create(
    std::unique_ptr<MissiveStorageModuleDelegateInterface> delegate) {
  return base::WrapRefCounted(new MissiveStorageModule(std::move(delegate)));
}

void MissiveStorageModule::AddRecord(
    Priority priority,
    Record record,
    base::OnceCallback<void(Status)> callback) {
  delegate_->AddRecord(priority, std::move(record), std::move(callback));
}

void MissiveStorageModule::Flush(Priority priority,
                                 base::OnceCallback<void(Status)> callback) {
  delegate_->Flush(priority, std::move(callback));
}

void MissiveStorageModule::ReportSuccess(
    SequenceInformation sequence_information,
    bool force) {
  delegate_->ReportSuccess(sequence_information, force);
}

void MissiveStorageModule::UpdateEncryptionKey(
    SignedEncryptionInfo signed_encryption_info) {
  delegate_->UpdateEncryptionKey(signed_encryption_info);
}

}  // namespace reporting
