// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/userdataauth/userdataauth_client.h"

#include <utility>

#include <google/protobuf/message_lite.h>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "chromeos/dbus/userdataauth/fake_userdataauth_client.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "third_party/cros_system_api/dbus/cryptohome/dbus-constants.h"

namespace chromeos {
namespace {

// This suffix is appended to cryptohome_id to get hash in stub implementation:
// stub_hash = "[cryptohome_id]-hash";
constexpr char kUserIdStubHashSuffix[] = "-hash";

// The default timeout for all userdataauth method call.
// Note that it is known that cryptohomed could be slow to respond to calls
// certain conditions, especially Mount(). D-Bus call blocking for as long as 2
// minutes have been observed in testing conditions/CQ.
constexpr int kUserDataAuthDefaultTimeoutMS = 5 * 60 * 1000;

UserDataAuthClient* g_instance = nullptr;

// Tries to parse a proto message from |response| into |proto|.
// Returns false if |response| is nullptr or the message cannot be parsed.
bool ParseProto(dbus::Response* response,
                google::protobuf::MessageLite* proto) {
  if (!response) {
    LOG(ERROR) << "Failed to call cryptohomed";
    return false;
  }

  dbus::MessageReader reader(response);
  if (!reader.PopArrayOfBytesAsProto(proto)) {
    LOG(ERROR) << "Failed to parse response message from cryptohomed";
    return false;
  }

  return true;
}

void OnSignalConnected(const std::string& interface_name,
                       const std::string& signal_name,
                       bool success) {
  DCHECK_EQ(interface_name, ::user_data_auth::kUserDataAuthInterface);
  LOG_IF(DFATAL, !success) << "Failed to connect to D-Bus signal; interface: "
                           << interface_name << "; signal: " << signal_name;
}

// "Real" implementation of UserDataAuthClient talking to the cryptohomed's
// UserDataAuth interface on the Chrome OS side.
class UserDataAuthClientImpl : public UserDataAuthClient {
 public:
  UserDataAuthClientImpl() = default;
  ~UserDataAuthClientImpl() override = default;

  // Not copyable or movable.
  UserDataAuthClientImpl(const UserDataAuthClientImpl&) = delete;
  UserDataAuthClientImpl& operator=(const UserDataAuthClientImpl&) = delete;

  void Init(dbus::Bus* bus) {
    proxy_ = bus->GetObjectProxy(
        ::user_data_auth::kUserDataAuthServiceName,
        dbus::ObjectPath(::user_data_auth::kUserDataAuthServicePath));
    ConnectToSignals();
  }

  // UserDataAuthClient override:

  void AddObserver(Observer* observer) override {
    observer_list_.AddObserver(observer);
  }

  void RemoveObserver(Observer* observer) override {
    observer_list_.RemoveObserver(observer);
  }

  void WaitForServiceToBeAvailable(
      chromeos::WaitForServiceToBeAvailableCallback callback) override {
    proxy_->WaitForServiceToBeAvailable(std::move(callback));
  }

  void IsMounted(const ::user_data_auth::IsMountedRequest& request,
                 IsMountedCallback callback) override {
    CallProtoMethod(::user_data_auth::kIsMounted,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void Unmount(const ::user_data_auth::UnmountRequest& request,
               UnmountCallback callback) override {
    CallProtoMethod(::user_data_auth::kUnmount,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void Mount(const ::user_data_auth::MountRequest& request,
             MountCallback callback) override {
    CallProtoMethod(::user_data_auth::kMount,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void Remove(const ::user_data_auth::RemoveRequest& request,
              RemoveCallback callback) override {
    CallProtoMethod(::user_data_auth::kRemove,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void GetKeyData(const ::user_data_auth::GetKeyDataRequest& request,
                  GetKeyDataCallback callback) override {
    CallProtoMethod(::user_data_auth::kGetKeyData,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void CheckKey(const ::user_data_auth::CheckKeyRequest& request,
                CheckKeyCallback callback) override {
    CallProtoMethod(::user_data_auth::kCheckKey,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void AddKey(const ::user_data_auth::AddKeyRequest& request,
              AddKeyCallback callback) override {
    CallProtoMethod(::user_data_auth::kAddKey,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void RemoveKey(const ::user_data_auth::RemoveKeyRequest& request,
                 RemoveKeyCallback callback) override {
    CallProtoMethod(::user_data_auth::kRemoveKey,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void MassRemoveKeys(const ::user_data_auth::MassRemoveKeysRequest& request,
                      MassRemoveKeysCallback callback) override {
    CallProtoMethod(::user_data_auth::kMassRemoveKeys,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void MigrateKey(const ::user_data_auth::MigrateKeyRequest& request,
                  MigrateKeyCallback callback) override {
    CallProtoMethod(::user_data_auth::kMigrateKey,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void StartFingerprintAuthSession(
      const ::user_data_auth::StartFingerprintAuthSessionRequest& request,
      StartFingerprintAuthSessionCallback callback) override {
    CallProtoMethod(::user_data_auth::kStartFingerprintAuthSession,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void EndFingerprintAuthSession(
      const ::user_data_auth::EndFingerprintAuthSessionRequest& request,
      EndFingerprintAuthSessionCallback callback) override {
    CallProtoMethod(::user_data_auth::kEndFingerprintAuthSession,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void StartMigrateToDircrypto(
      const ::user_data_auth::StartMigrateToDircryptoRequest& request,
      StartMigrateToDircryptoCallback callback) override {
    CallProtoMethod(::user_data_auth::kStartMigrateToDircrypto,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void NeedsDircryptoMigration(
      const ::user_data_auth::NeedsDircryptoMigrationRequest& request,
      NeedsDircryptoMigrationCallback callback) override {
    CallProtoMethod(::user_data_auth::kNeedsDircryptoMigration,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void GetSupportedKeyPolicies(
      const ::user_data_auth::GetSupportedKeyPoliciesRequest& request,
      GetSupportedKeyPoliciesCallback callback) override {
    CallProtoMethod(::user_data_auth::kGetSupportedKeyPolicies,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void GetAccountDiskUsage(
      const ::user_data_auth::GetAccountDiskUsageRequest& request,
      GetAccountDiskUsageCallback callback) override {
    CallProtoMethod(::user_data_auth::kGetAccountDiskUsage,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void StartAuthSession(
      const ::user_data_auth::StartAuthSessionRequest& request,
      StartAuthSessionCallback callback) override {
    CallProtoMethod(::user_data_auth::kStartAuthSession,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void AuthenticateAuthSession(
      const ::user_data_auth::AuthenticateAuthSessionRequest& request,
      AuthenticateAuthSessionCallback callback) override {
    CallProtoMethod(::user_data_auth::kAuthenticateAuthSession,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

  void AddCredentials(const ::user_data_auth::AddCredentialsRequest& request,
                      AddCredentialsCallback callback) override {
    CallProtoMethod(::user_data_auth::kAddCredentials,
                    ::user_data_auth::kUserDataAuthInterface, request,
                    std::move(callback));
  }

 private:
  // Calls cryptohomed's |method_name| method in |interface_name| interface,
  // passing in |request| as input with |timeout_ms|. Once the (asynchronous)
  // call finishes, |callback| is called with the response proto.
  template <typename RequestType, typename ReplyType>
  void CallProtoMethodWithTimeout(const char* method_name,
                                  const char* interface_name,
                                  int timeout_ms,
                                  const RequestType& request,
                                  DBusMethodCallback<ReplyType> callback) {
    dbus::MethodCall method_call(interface_name, method_name);
    dbus::MessageWriter writer(&method_call);
    if (!writer.AppendProtoAsArrayOfBytes(request)) {
      LOG(ERROR)
          << "Failed to append protobuf when calling UserDataAuth method "
          << method_name;
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), absl::nullopt));
      return;
    }
    // Bind with the weak pointer of |this| so the response is not
    // handled once |this| is already destroyed.
    proxy_->CallMethod(
        &method_call, timeout_ms,
        base::BindOnce(&UserDataAuthClientImpl::HandleResponse<ReplyType>,
                       weak_factory_.GetWeakPtr(), std::move(callback)));
  }

  // Calls cryptohomed's |method_name| method in |interface_name| interface,
  // passing in |request| as input with the default UserDataAuth timeout. Once
  // the (asynchronous) call finishes, |callback| is called with the response
  // proto.
  template <typename RequestType, typename ReplyType>
  void CallProtoMethod(const char* method_name,
                       const char* interface_name,
                       const RequestType& request,
                       DBusMethodCallback<ReplyType> callback) {
    CallProtoMethodWithTimeout(method_name, interface_name,
                               kUserDataAuthDefaultTimeoutMS, request,
                               std::move(callback));
  }

  // Parses the response proto message from |response| and calls |callback| with
  // the decoded message. Calls |callback| with std::nullopt on error, including
  // timeout.
  template <typename ReplyType>
  void HandleResponse(DBusMethodCallback<ReplyType> callback,
                      dbus::Response* response) {
    ReplyType reply_proto;
    if (!ParseProto(response, &reply_proto)) {
      LOG(ERROR) << "Failed to parse reply protobuf from UserDataAuth method";
      std::move(callback).Run(absl::nullopt);
      return;
    }
    std::move(callback).Run(reply_proto);
  }

  void OnDircryptoMigrationProgress(dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    ::user_data_auth::DircryptoMigrationProgress proto;
    if (!reader.PopArrayOfBytesAsProto(&proto)) {
      LOG(ERROR) << "Failed to parse DircryptoMigrationProgress protobuf from "
                    "UserDataAuth signal";
      return;
    }
    for (auto& observer : observer_list_) {
      observer.DircryptoMigrationProgress(proto);
    }
  }

  void OnLowDiskSpace(dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    ::user_data_auth::LowDiskSpace proto;
    if (!reader.PopArrayOfBytesAsProto(&proto)) {
      LOG(ERROR)
          << "Failed to parse LowDiskSpace protobuf from UserDataAuth signal";
      return;
    }
    for (auto& observer : observer_list_) {
      observer.LowDiskSpace(proto);
    }
  }

  // Connects the dbus signals.
  void ConnectToSignals() {
    proxy_->ConnectToSignal(
        ::user_data_auth::kUserDataAuthInterface,
        ::user_data_auth::kDircryptoMigrationProgress,
        base::BindRepeating(
            &UserDataAuthClientImpl::OnDircryptoMigrationProgress,
            weak_factory_.GetWeakPtr()),
        base::BindOnce(&OnSignalConnected));
    proxy_->ConnectToSignal(
        ::user_data_auth::kUserDataAuthInterface,
        ::user_data_auth::kLowDiskSpace,
        base::BindRepeating(&UserDataAuthClientImpl::OnLowDiskSpace,
                            weak_factory_.GetWeakPtr()),
        base::BindOnce(&OnSignalConnected));
  }

  // D-Bus proxy for cryptohomed, not owned.
  dbus::ObjectProxy* proxy_ = nullptr;

  // List of observers for dbus signals.
  base::ObserverList<Observer> observer_list_;

  base::WeakPtrFactory<UserDataAuthClientImpl> weak_factory_{this};
};

}  // namespace

UserDataAuthClient::UserDataAuthClient() {
  CHECK(!g_instance);
  g_instance = this;
}

UserDataAuthClient::~UserDataAuthClient() {
  CHECK_EQ(this, g_instance);
  g_instance = nullptr;
}

// static
void UserDataAuthClient::Initialize(dbus::Bus* bus) {
  CHECK(bus);
  (new UserDataAuthClientImpl())->Init(bus);
}

// static
void UserDataAuthClient::InitializeFake() {
  // Certain tests may create FakeUserDataAuthClient() before the browser starts
  // to set parameters.
  if (!FakeUserDataAuthClient::Get()) {
    new FakeUserDataAuthClient();
  }
}

// static
void UserDataAuthClient::Shutdown() {
  CHECK(g_instance);
  delete g_instance;
  // The destructor resets |g_instance|.
  DCHECK(!g_instance);
}

// static
UserDataAuthClient* UserDataAuthClient::Get() {
  return g_instance;
}

// static
std::string UserDataAuthClient::GetStubSanitizedUsername(
    const cryptohome::AccountIdentifier& id) {
  return id.account_id() + kUserIdStubHashSuffix;
}

}  // namespace chromeos
