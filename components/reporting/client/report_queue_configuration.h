// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REPORTING_CLIENT_REPORT_QUEUE_CONFIGURATION_H_
#define COMPONENTS_REPORTING_CLIENT_REPORT_QUEUE_CONFIGURATION_H_

#include <memory>
#include <string>
#include <utility>

#include "base/callback.h"
#include "components/reporting/proto/synced/record_constants.pb.h"
#include "components/reporting/util/status.h"
#include "components/reporting/util/statusor.h"

namespace reporting {

// |EventType| enum is used to distinguish between user and device event types,
// and inherently determine the type of DM tokens (user vs device) generated.
enum class EventType { kDevice, kUser };

// ReportQueueConfiguration configures a report queue.
// |dm_token| if set will be attached to all records generated with this queue.
// |event_type| describes the event type being reported and is indirectly used
// to retrieve DM tokens for downstream processing. |destination| indicates what
// server side handler will be handling the records that are generated by the
// ReportQueueImpl. |policy_check_callback_| is a RepeatingCallback that
// verifies the specific report queue is allowed.
class ReportQueueConfiguration {
 public:
  // PolicyCheckCallbacks should return error::UNAUTHENTICATED if a policy check
  // fails due to policies. Any other error as appropriate, and OK if a policy
  // check is successful.
  using PolicyCheckCallback = base::RepeatingCallback<Status(void)>;

  ~ReportQueueConfiguration();
  ReportQueueConfiguration(const ReportQueueConfiguration& other) = delete;
  ReportQueueConfiguration& operator=(const ReportQueueConfiguration& other) =
      delete;

  // Factory for generating a ReportQueueConfiguration.
  // If any of the parameters are invalid, will return error::INVALID_ARGUMENT.
  // |dm_token| is valid when dm_token.is_valid() is true.
  // |destination| is valid when it is any value other than
  // Destination::UNDEFINED_DESTINATION.
  static StatusOr<std::unique_ptr<ReportQueueConfiguration>> Create(
      base::StringPiece dm_token,
      Destination destination,
      PolicyCheckCallback policy_check_callback);

  // Factory for generating a ReportQueueConfiguration.
  // |event_type| is the type of event being reported, and is indirectly used to
  // retrieve DM tokens for downstream processing when building the report
  // queue. Using |EventType::kDevice| will skip DM token retrieval. If
  // any of the parameters are invalid, will return error::INVALID_ARGUMENT.
  // |destination| is valid when it is any value other than
  // Destination::UNDEFINED_DESTINATION.
  static StatusOr<std::unique_ptr<ReportQueueConfiguration>> Create(
      EventType event_type,
      Destination destination,
      PolicyCheckCallback policy_check_callback);

  reporting::Destination destination() const { return destination_; }

  std::string dm_token() { return dm_token_; }

  EventType event_type() const { return event_type_; }

  Status SetDMToken(base::StringPiece dm_token);

  Status CheckPolicy() const;

 private:
  ReportQueueConfiguration();

  Status SetEventType(EventType event_type);
  Status SetDestination(reporting::Destination destination);
  Status SetPolicyCheckCallback(PolicyCheckCallback policy_check_callback);

  std::string dm_token_;
  EventType event_type_;
  reporting::Destination destination_;

  PolicyCheckCallback policy_check_callback_;
};

}  // namespace reporting

#endif  // COMPONENTS_REPORTING_CLIENT_REPORT_QUEUE_CONFIGURATION_H_
