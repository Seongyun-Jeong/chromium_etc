// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/data_transfer_policy/data_transfer_endpoint.h"

#include "base/check_op.h"
#include "base/stl_util.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/origin.h"

namespace ui {

DataTransferEndpoint::DataTransferEndpoint(const url::Origin& origin,
                                           bool notify_if_restricted)
    : type_(EndpointType::kUrl),
      origin_(origin),
      notify_if_restricted_(notify_if_restricted) {}

DataTransferEndpoint::DataTransferEndpoint(EndpointType type,
                                           bool notify_if_restricted)
    : type_(type),
      origin_(absl::nullopt),
      notify_if_restricted_(notify_if_restricted) {
  DCHECK_NE(type, EndpointType::kUrl);
}

DataTransferEndpoint::DataTransferEndpoint(const DataTransferEndpoint& other) =
    default;

DataTransferEndpoint::DataTransferEndpoint(DataTransferEndpoint&& other) =
    default;

DataTransferEndpoint& DataTransferEndpoint::operator=(
    const DataTransferEndpoint& other) = default;

DataTransferEndpoint& DataTransferEndpoint::operator=(
    DataTransferEndpoint&& other) = default;

bool DataTransferEndpoint::operator==(const DataTransferEndpoint& other) const {
  return origin_ == other.origin_ && type_ == other.type_ &&
         notify_if_restricted_ == other.notify_if_restricted_;
}

DataTransferEndpoint::~DataTransferEndpoint() = default;

const url::Origin* DataTransferEndpoint::GetOrigin() const {
  return base::OptionalOrNullptr(origin_);
}

bool DataTransferEndpoint::IsSameOriginWith(
    const DataTransferEndpoint& other) const {
  return IsUrlType() && (type_ == other.type_) && (origin_ == other.origin_);
}

}  // namespace ui
