// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_POLICY_TEST_SUPPORT_POLICY_STORAGE_H_
#define COMPONENTS_POLICY_TEST_SUPPORT_POLICY_STORAGE_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/time/time.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "components/policy/test_support/signature_provider.h"

namespace policy {

class SignatureProvider;

// Stores preferences about policies to be applied to registered browsers.
class PolicyStorage {
 public:
  PolicyStorage();
  PolicyStorage(PolicyStorage&& policy_storage);
  PolicyStorage& operator=(PolicyStorage&& policy_storage);
  virtual ~PolicyStorage();

  // Returns the serialized proto associated with |policy_type|. Returns empty
  // string if there is no such association.
  std::string GetPolicyPayload(const std::string& policy_type) const;
  // Associates the serialized proto stored in |policy_payload| with
  // |policy_type|.
  void SetPolicyPayload(const std::string& policy_type,
                        const std::string& policy_payload);

  SignatureProvider* signature_provider() const {
    return signature_provider_.get();
  }
  void set_signature_provider(
      std::unique_ptr<SignatureProvider> signature_provider) {
    signature_provider_ = std::move(signature_provider);
  }

  const std::string& robot_api_auth_code() const {
    return robot_api_auth_code_;
  }
  void set_robot_api_auth_code(const std::string& robot_api_auth_code) {
    robot_api_auth_code_ = robot_api_auth_code;
  }

  const std::string& service_account_identity() const {
    return service_account_identity_;
  }
  void set_service_account_identity(
      const std::string& service_account_identity) {
    service_account_identity_ = service_account_identity;
  }

  const base::flat_set<std::string>& managed_users() const {
    return managed_users_;
  }
  void add_managed_user(const std::string& managed_user) {
    managed_users_.insert(managed_user);
  }

  std::string policy_user() const { return policy_user_; }
  void set_policy_user(const std::string& policy_user) {
    policy_user_ = policy_user;
  }

  const std::string& policy_invalidation_topic() const {
    return policy_invalidation_topic_;
  }
  void set_policy_invalidation_topic(
      const std::string& policy_invalidation_topic) {
    policy_invalidation_topic_ = policy_invalidation_topic;
  }

  base::Time timestamp() const { return timestamp_; }
  void set_timestamp(const base::Time& timestamp) { timestamp_ = timestamp; }

  bool allow_set_device_attributes() { return allow_set_device_attributes_; }
  void set_allow_set_device_attributes(bool allow_set_device_attributes) {
    allow_set_device_attributes_ = allow_set_device_attributes;
  }

  struct DeviceState {
    std::string management_domain;
    enterprise_management::DeviceStateRetrievalResponse::RestoreMode
        restore_mode;
  };

  const DeviceState& device_state() { return device_state_; }
  void set_device_state(const DeviceState& device_state) {
    device_state_ = device_state;
  }

  struct PsmEntry {
    int psm_execution_result;
    int64_t psm_determination_timestamp;
  };

  void SetPsmEntry(const std::string& brand_serial_id,
                   const PsmEntry& psm_entry);

  const PsmEntry* GetPsmEntry(const std::string& brand_serial_id) const;

  struct InitialEnrollmentState {
    enterprise_management::DeviceInitialEnrollmentStateResponse::
        InitialEnrollmentMode initial_enrollment_mode;
    std::string management_domain;
  };

  void SetInitialEnrollmentState(
      const std::string& brand_serial_id,
      const InitialEnrollmentState& initial_enrollment_state);

  const InitialEnrollmentState* GetInitialEnrollmentState(
      const std::string& brand_serial_id) const;

  // Returns hashes for brand serial IDs whose initial enrollment state is
  // registered on the server. Only hashes, which, when divied by |modulus|,
  // result in the specified |remainder|, are returned.
  std::vector<std::string> GetMatchingSerialHashes(uint64_t modulus,
                                                   uint64_t remainder) const;

 private:
  // Maps policy types to a serialized proto representing the policies to be
  // applied for the type (e.g. CloudPolicySettings,
  // ChromeDeviceSettingsProto).
  base::flat_map<std::string, std::string> policy_payloads_;

  std::unique_ptr<SignatureProvider> signature_provider_;

  std::string robot_api_auth_code_;

  std::string service_account_identity_;

  base::flat_set<std::string> managed_users_;

  std::string policy_user_;

  std::string policy_invalidation_topic_;

  base::Time timestamp_;

  bool allow_set_device_attributes_ = true;

  DeviceState device_state_;

  // Maps brand serial ID to PsmEntry.
  base::flat_map<std::string, PsmEntry> psm_entries_;

  // Maps brand serial ID to InitialEnrollmentState.
  base::flat_map<std::string, InitialEnrollmentState>
      initial_enrollment_states_;
};

}  // namespace policy

#endif  // COMPONENTS_POLICY_TEST_SUPPORT_POLICY_STORAGE_H_
