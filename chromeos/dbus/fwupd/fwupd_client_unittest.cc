// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/fwupd/fwupd_client.h"

#include "ash/constants/ash_features.h"
#include "base/files/scoped_file.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "chromeos/dbus/fwupd/fwupd_properties.h"
#include "dbus/message.h"
#include "dbus/mock_bus.h"
#include "dbus/mock_object_proxy.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;

namespace {
const char kFwupdServiceName[] = "org.freedesktop.fwupd";
const char kFwupdServicePath[] = "/";
const char kFwupdDeviceAddedSignalName[] = "DeviceAdded";
const char kFakeDeviceIdForTesting[] = "0123";
const char kFakeDeviceNameForTesting[] = "Fake Device";
const char kFakeUpdateVersionForTesting[] = "1.0.0";
const char kFakeUpdateDescriptionForTesting[] =
    "This is a fake update for testing.";
const uint32_t kFakeUpdatePriorityForTesting = 1;
const char kNameKey[] = "Name";
const char kIdKey[] = "DeviceId";
const char kVersionKey[] = "Version";
const char kDescriptionKey[] = "Description";
const char kPriorityKey[] = "Urgency";

void RunResponseOrErrorCallback(
    dbus::ObjectProxy::ResponseOrErrorCallback callback,
    std::unique_ptr<dbus::Response> response,
    std::unique_ptr<dbus::ErrorResponse> error_response) {
  std::move(callback).Run(response.get(), error_response.get());
}

class MockObserver : public chromeos::FwupdClient::Observer {
 public:
  MOCK_METHOD(void,
              OnDeviceListResponse,
              (chromeos::FwupdDeviceList * devices),
              (override));
  MOCK_METHOD(void,
              OnUpdateListResponse,
              (const std::string& device_id,
               chromeos::FwupdUpdateList* updates),
              (override));
  MOCK_METHOD(void, OnInstallResponse, (bool success), (override));
  MOCK_METHOD(void,
              OnPropertiesChangedResponse,
              (chromeos::FwupdProperties * properties),
              (override));
};

}  // namespace

namespace chromeos {

class FwupdClientTest : public testing::Test {
 public:
  FwupdClientTest() {
    scoped_feature_list_.InitAndEnableFeature(
        ::ash::features::kFirmwareUpdaterApp);

    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = base::MakeRefCounted<dbus::MockBus>(options);

    dbus::ObjectPath fwupd_service_path(kFwupdServicePath);
    proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        bus_.get(), kFwupdServiceName, fwupd_service_path);

    EXPECT_CALL(*bus_.get(),
                GetObjectProxy(kFwupdServiceName, fwupd_service_path))
        .WillRepeatedly(testing::Return(proxy_.get()));

    EXPECT_CALL(*proxy_, DoConnectToSignal(_, _, _, _))
        .WillRepeatedly(Invoke(this, &FwupdClientTest::ConnectToSignal));

    expected_properties_ = std::make_unique<chromeos::FwupdProperties>(
        bus_->GetObjectProxy(kFwupdServiceName, fwupd_service_path),
        base::DoNothing());

    fwupd_client_ = FwupdClient::Create();
    fwupd_client_->InitForTesting(bus_.get());
    fwupd_client_->client_is_in_testing_mode_ = true;
  }

  FwupdClientTest(const FwupdClientTest&) = delete;
  FwupdClientTest& operator=(const FwupdClientTest&) = delete;
  ~FwupdClientTest() override = default;

  int GetDeviceSignalCallCount() {
    return fwupd_client_->device_signal_call_count_for_testing_;
  }

  void OnMethodCalled(dbus::MethodCall* method_call,
                      int timeout_ms,
                      dbus::ObjectProxy::ResponseOrErrorCallback* callback) {
    ASSERT_FALSE(dbus_method_call_simulated_results_.empty());
    MethodCallResult result =
        std::move(dbus_method_call_simulated_results_.front());
    dbus_method_call_simulated_results_.pop_front();
    task_environment_.GetMainThreadTaskRunner()->PostTask(
        FROM_HERE,
        base::BindOnce(&RunResponseOrErrorCallback, std::move(*callback),
                       std::move(result.first), std::move(result.second)));
  }

  void CheckDevices(FwupdDeviceList* devices) {
    CHECK_EQ(kFakeDeviceNameForTesting, (*devices)[0].device_name);
    CHECK_EQ(kFakeDeviceIdForTesting, (*devices)[0].id);
  }

  void CheckUpdates(const std::string& device_id, FwupdUpdateList* updates) {
    CHECK_EQ(kFakeDeviceIdForTesting, device_id);
    CHECK_EQ(kFakeUpdateVersionForTesting, (*updates)[0].version);
    CHECK_EQ(kFakeUpdateDescriptionForTesting, (*updates)[0].description);
    // This value is returned by DBus as a uint32_t and is added to a dictionary
    // that doesn't support unsigned numbers. So it needs to be casted to int.
    CHECK_EQ(static_cast<int>(kFakeUpdatePriorityForTesting),
             (*updates)[0].priority);
  }

  void CheckInstallState(bool success) { CHECK_EQ(install_success_, success); }

  void SetInstallState(bool success) { install_success_ = success; }

  void CheckPropertyChanged(FwupdProperties* properties) {
    if (properties->percentage.is_valid()) {
      CHECK_EQ(expected_properties_->percentage.value(),
               properties->percentage.value());
    }

    if (properties->status.is_valid()) {
      CHECK_EQ(expected_properties_->status.value(),
               properties->status.value());
    }
  }

  void AddDbusMethodCallResultSimulation(
      std::unique_ptr<dbus::Response> response,
      std::unique_ptr<dbus::ErrorResponse> error_response) {
    dbus_method_call_simulated_results_.emplace_back(std::move(response),
                                                     std::move(error_response));
  }

  FwupdProperties* GetProperties() { return fwupd_client_->properties_.get(); }

 protected:
  // Synchronously passes |signal| to |client_|'s handler, simulating the signal
  // being emitted by fwupd.
  void EmitSignal(const std::string& signal_name) {
    dbus::Signal signal(kFwupdServiceName, signal_name);
    const auto callback = signal_callbacks_.find(signal_name);
    ASSERT_TRUE(callback != signal_callbacks_.end())
        << "Client didn't register for signal " << signal_name;
    callback->second.Run(&signal);
  }

  scoped_refptr<dbus::MockObjectProxy> proxy_;
  std::unique_ptr<FwupdClient> fwupd_client_;
  std::unique_ptr<chromeos::FwupdProperties> expected_properties_;

 private:
  // Handles calls to |proxy_|'s ConnectToSignal() method.
  void ConnectToSignal(
      const std::string& interface_name,
      const std::string& signal_name,
      dbus::ObjectProxy::SignalCallback signal_callback,
      dbus::ObjectProxy::OnConnectedCallback* on_connected_callback) {
    signal_callbacks_[signal_name] = signal_callback;

    task_environment_.GetMainThreadTaskRunner()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(*on_connected_callback), interface_name,
                       signal_name, true /* success */));
  }

  // Maps from fwupd signal name to the corresponding callback provided by
  // |client_|.
  base::flat_map<std::string, dbus::ObjectProxy::SignalCallback>
      signal_callbacks_;

  base::test::SingleThreadTaskEnvironment task_environment_;

  base::test::ScopedFeatureList scoped_feature_list_;

  // Mock bus for simulating calls.
  scoped_refptr<dbus::MockBus> bus_;
  using MethodCallResult = std::pair<std::unique_ptr<dbus::Response>,
                                     std::unique_ptr<dbus::ErrorResponse>>;
  std::deque<MethodCallResult> dbus_method_call_simulated_results_;

  bool install_success_ = false;
};

// TODO (swifton): Rewrite this test with an observer when it's available.
TEST_F(FwupdClientTest, AddOneDevice) {
  EmitSignal(kFwupdDeviceAddedSignalName);
  EXPECT_EQ(1, GetDeviceSignalCallCount());
}

TEST_F(FwupdClientTest, RequestDevices) {
  // The observer will check that the device description is parsed and passed
  // correctly.
  MockObserver observer;
  EXPECT_CALL(observer, OnDeviceListResponse(_))
      .Times(1)
      .WillRepeatedly(Invoke(this, &FwupdClientTest::CheckDevices));
  fwupd_client_->AddObserver(&observer);

  EXPECT_CALL(*proxy_, DoCallMethodWithErrorResponse(_, _, _))
      .WillRepeatedly(Invoke(this, &FwupdClientTest::OnMethodCalled));

  // Create a response simulation that contains one device description.
  auto response = dbus::Response::CreateEmpty();

  dbus::MessageWriter response_writer(response.get());
  dbus::MessageWriter response_array_writer(nullptr);
  dbus::MessageWriter device_array_writer(nullptr);
  dbus::MessageWriter dict_writer(nullptr);

  // The response is an array of arrays of dictionaries. Each dictionary is one
  // device description.
  response_writer.OpenArray("a{sv}", &response_array_writer);
  response_array_writer.OpenArray("{sv}", &device_array_writer);

  device_array_writer.OpenDictEntry(&dict_writer);
  dict_writer.AppendString(kNameKey);
  dict_writer.AppendVariantOfString(kFakeDeviceNameForTesting);
  device_array_writer.CloseContainer(&dict_writer);

  device_array_writer.OpenDictEntry(&dict_writer);
  dict_writer.AppendString(kIdKey);
  dict_writer.AppendVariantOfString(kFakeDeviceIdForTesting);
  device_array_writer.CloseContainer(&dict_writer);

  response_array_writer.CloseContainer(&device_array_writer);
  response_writer.CloseContainer(&response_array_writer);

  AddDbusMethodCallResultSimulation(std::move(response), nullptr);

  fwupd_client_->RequestDevices();

  base::RunLoop().RunUntilIdle();
}

TEST_F(FwupdClientTest, RequestUpgrades) {
  // The observer will check that the update description is parsed and passed
  // correctly.
  MockObserver observer;
  EXPECT_CALL(observer, OnUpdateListResponse(_, _))
      .Times(1)
      .WillRepeatedly(Invoke(this, &FwupdClientTest::CheckUpdates));
  fwupd_client_->AddObserver(&observer);

  EXPECT_CALL(*proxy_, DoCallMethodWithErrorResponse(_, _, _))
      .WillRepeatedly(Invoke(this, &FwupdClientTest::OnMethodCalled));

  auto response = dbus::Response::CreateEmpty();

  dbus::MessageWriter response_writer(response.get());
  dbus::MessageWriter response_array_writer(nullptr);
  dbus::MessageWriter device_array_writer(nullptr);
  dbus::MessageWriter dict_writer(nullptr);

  // The response is an array of arrays of dictionaries. Each dictionary is one
  // update description.
  response_writer.OpenArray("a{sv}", &response_array_writer);
  response_array_writer.OpenArray("{sv}", &device_array_writer);

  device_array_writer.OpenDictEntry(&dict_writer);
  dict_writer.AppendString(kDescriptionKey);
  dict_writer.AppendVariantOfString(kFakeUpdateDescriptionForTesting);
  device_array_writer.CloseContainer(&dict_writer);

  device_array_writer.OpenDictEntry(&dict_writer);
  dict_writer.AppendString(kVersionKey);
  dict_writer.AppendVariantOfString(kFakeUpdateVersionForTesting);
  device_array_writer.CloseContainer(&dict_writer);

  device_array_writer.OpenDictEntry(&dict_writer);
  dict_writer.AppendString(kPriorityKey);
  dict_writer.AppendVariantOfUint32(kFakeUpdatePriorityForTesting);
  device_array_writer.CloseContainer(&dict_writer);

  response_array_writer.CloseContainer(&device_array_writer);
  response_writer.CloseContainer(&response_array_writer);

  AddDbusMethodCallResultSimulation(std::move(response), nullptr);

  fwupd_client_->RequestUpdates(kFakeDeviceIdForTesting);

  base::RunLoop().RunUntilIdle();
}

TEST_F(FwupdClientTest, Install) {
  // The observer will check that the update description is parsed and passed
  // correctly.
  MockObserver observer;
  EXPECT_CALL(observer, OnInstallResponse(_))
      .Times(1)
      .WillRepeatedly(Invoke(this, &FwupdClientTest::CheckInstallState));
  fwupd_client_->AddObserver(&observer);

  EXPECT_CALL(*proxy_, DoCallMethodWithErrorResponse(_, _, _))
      .WillRepeatedly(Invoke(this, &FwupdClientTest::OnMethodCalled));

  auto response = dbus::Response::CreateEmpty();

  dbus::MessageWriter response_writer(response.get());

  // The response is an boolean for whether the install request was successful
  // or not.
  const bool install_success = true;
  SetInstallState(install_success);
  response_writer.AppendBool(install_success);

  AddDbusMethodCallResultSimulation(std::move(response), nullptr);

  fwupd_client_->InstallUpdate(kFakeDeviceIdForTesting, base::ScopedFD(0),
                               std::map<std::string, bool>());

  base::RunLoop().RunUntilIdle();
}

TEST_F(FwupdClientTest, PropertiesChanged) {
  const uint32_t expected_percentage = 50u;
  const uint32_t expected_status = 1u;

  expected_properties_->percentage.ReplaceValue(expected_percentage);
  expected_properties_->status.ReplaceValue(expected_status);

  MockObserver observer;
  EXPECT_CALL(observer, OnPropertiesChangedResponse(_))
      .Times(2)
      .WillRepeatedly(Invoke(this, &FwupdClientTest::CheckPropertyChanged));
  fwupd_client_->AddObserver(&observer);

  GetProperties()->percentage.ReplaceValue(expected_percentage);
  GetProperties()->status.ReplaceValue(expected_status);
}

}  // namespace chromeos
