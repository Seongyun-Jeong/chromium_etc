// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/device/hid/hid_connection.h"

#include <algorithm>

#include "base/containers/contains.h"
#include "base/memory/ref_counted_memory.h"
#include "components/device_event_log/device_event_log.h"
#include "services/device/public/cpp/hid/hid_usage_and_page.h"
#include "services/device/public/mojom/hid.mojom.h"

namespace device {

namespace {

// Functor used to filter collections by report ID.
struct CollectionHasReportId {
  explicit CollectionHasReportId(uint8_t report_id) : report_id_(report_id) {}

  bool operator()(const mojom::HidCollectionInfoPtr& info) const {
    if (info->report_ids.size() == 0 ||
        report_id_ == HidConnection::kNullReportId)
      return false;

    if (report_id_ == HidConnection::kAnyReportId)
      return true;

    return base::Contains(info->report_ids, report_id_);
  }

 private:
  const uint8_t report_id_;
};

// Functor returning true if collection has a protected usage.
struct CollectionIsAlwaysProtected {
  bool operator()(const mojom::HidCollectionInfoPtr& info) const {
    return IsAlwaysProtected(*info->usage);
  }
};

const mojom::HidCollectionInfo* FindCollectionByReportId(
    const std::vector<mojom::HidCollectionInfoPtr>& collections,
    uint8_t report_id) {
  auto collection_iter = std::find_if(collections.begin(), collections.end(),
                                      CollectionHasReportId(report_id));
  if (collection_iter != collections.end()) {
    DCHECK(collection_iter->get());
    return collection_iter->get();
  }

  return nullptr;
}

bool HasAlwaysProtectedCollection(
    const std::vector<mojom::HidCollectionInfoPtr>& collections) {
  return std::find_if(collections.begin(), collections.end(),
                      CollectionIsAlwaysProtected()) != collections.end();
}

}  // namespace

HidConnection::HidConnection(scoped_refptr<HidDeviceInfo> device_info,
                             bool allow_protected_reports,
                             bool allow_fido_reports)
    : device_info_(device_info),
      allow_protected_reports_(allow_protected_reports),
      allow_fido_reports_(allow_fido_reports),
      closed_(false) {
  has_always_protected_collection_ =
      HasAlwaysProtectedCollection(device_info->collections());
}

HidConnection::~HidConnection() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(closed_);
}

void HidConnection::SetClient(Client* client) {
  if (client) {
    DCHECK(pending_reads_.empty());
    DCHECK(pending_reports_.empty());
  }
  client_ = client;
}

void HidConnection::Close() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!closed_);

  PlatformClose();
  closed_ = true;
}

void HidConnection::Read(ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!client_);
  if (device_info_->max_input_report_size() == 0) {
    HID_LOG(USER) << "This device does not support input reports.";
    std::move(callback).Run(false, nullptr, 0);
    return;
  }

  pending_reads_.emplace(std::move(callback));
  ProcessReadQueue();
}

void HidConnection::Write(scoped_refptr<base::RefCountedBytes> buffer,
                          WriteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (device_info_->max_output_report_size() == 0) {
    HID_LOG(USER) << "This device does not support output reports.";
    std::move(callback).Run(false);
    return;
  }
  if (buffer->size() > device_info_->max_output_report_size() + 1) {
    HID_LOG(USER) << "Output report buffer too long (" << buffer->size()
                  << " > " << (device_info_->max_output_report_size() + 1)
                  << ").";
    std::move(callback).Run(false);
    return;
  }
  DCHECK_GE(buffer->size(), 1u);
  uint8_t report_id = buffer->data()[0];
  if (device_info_->has_report_id() != (report_id != 0)) {
    HID_LOG(USER) << "Invalid output report ID.";
    std::move(callback).Run(false);
    return;
  }
  if (IsReportIdProtected(report_id, kOutput)) {
    HID_LOG(USER) << "Attempt to set a protected output report.";
    std::move(callback).Run(false);
    return;
  }

  PlatformWrite(buffer, std::move(callback));
}

void HidConnection::GetFeatureReport(uint8_t report_id, ReadCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (device_info_->max_feature_report_size() == 0) {
    HID_LOG(USER) << "This device does not support feature reports.";
    std::move(callback).Run(false, nullptr, 0);
    return;
  }
  if (device_info_->has_report_id() != (report_id != 0)) {
    HID_LOG(USER) << "Invalid feature report ID.";
    std::move(callback).Run(false, nullptr, 0);
    return;
  }
  if (IsReportIdProtected(report_id, kFeature)) {
    HID_LOG(USER) << "Attempt to get a protected feature report.";
    std::move(callback).Run(false, nullptr, 0);
    return;
  }

  PlatformGetFeatureReport(report_id, std::move(callback));
}

void HidConnection::SendFeatureReport(
    scoped_refptr<base::RefCountedBytes> buffer,
    WriteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (device_info_->max_feature_report_size() == 0) {
    HID_LOG(USER) << "This device does not support feature reports.";
    std::move(callback).Run(false);
    return;
  }
  DCHECK_GE(buffer->size(), 1u);
  uint8_t report_id = buffer->data()[0];
  if (device_info_->has_report_id() != (report_id != 0)) {
    HID_LOG(USER) << "Invalid feature report ID.";
    std::move(callback).Run(false);
    return;
  }
  if (IsReportIdProtected(report_id, kFeature)) {
    HID_LOG(USER) << "Attempt to set a protected feature report.";
    std::move(callback).Run(false);
    return;
  }

  PlatformSendFeatureReport(buffer, std::move(callback));
}

bool HidConnection::IsReportIdProtected(uint8_t report_id,
                                        HidReportType report_type) {
  if (!allow_protected_reports_) {
    // If |allow_fido_reports_| is true, allow access to reports in collections
    // with a usage from the FIDO usage page. FIDO reports are normally blocked
    // by the HID blocklist.
    if (allow_fido_reports_) {
      auto* collection_info =
          FindCollectionByReportId(device_info_->collections(), report_id);
      if (collection_info &&
          collection_info->usage->usage_page == mojom::kPageFido) {
        return false;
      }
    }

    // Deny access to reports that match HID blocklist rules.
    if (report_type == kInput) {
      if (device_info_->device()->protected_input_report_ids.has_value() &&
          base::Contains(*device_info_->device()->protected_input_report_ids,
                         report_id)) {
        return true;
      }
    } else if (report_type == kOutput) {
      if (device_info_->device()->protected_output_report_ids.has_value() &&
          base::Contains(*device_info_->device()->protected_output_report_ids,
                         report_id)) {
        return true;
      }
    } else if (report_type == kFeature) {
      if (device_info_->device()->protected_feature_report_ids.has_value() &&
          base::Contains(*device_info_->device()->protected_feature_report_ids,
                         report_id)) {
        return true;
      }
    }
  }

  // Some types of reports are always blocked regardless of
  // |allow_protected_reports_|.
  auto* collection_info =
      FindCollectionByReportId(device_info_->collections(), report_id);
  if (collection_info) {
    return IsAlwaysProtected(*collection_info->usage);
  }

  return has_always_protected_collection();
}

void HidConnection::ProcessInputReport(
    scoped_refptr<base::RefCountedBytes> buffer,
    size_t size) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_GE(size, 1u);

  uint8_t report_id = buffer->data()[0];
  if (IsReportIdProtected(report_id, kInput))
    return;

  if (client_) {
    client_->OnInputReport(buffer, size);
  } else {
    pending_reports_.emplace(buffer, size);
    ProcessReadQueue();
  }
}

void HidConnection::ProcessReadQueue() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!client_);

  // Hold a reference to |this| to prevent a callback from freeing this object
  // during the loop.
  scoped_refptr<HidConnection> self(this);
  while (pending_reads_.size() && pending_reports_.size()) {
    ReadCallback callback = std::move(pending_reads_.front());

    scoped_refptr<base::RefCountedBytes> buffer;
    size_t size;
    std::tie(buffer, size) = std::move(pending_reports_.front());

    pending_reads_.pop();
    pending_reports_.pop();
    std::move(callback).Run(true, std::move(buffer), size);
  }
}

}  // namespace device
