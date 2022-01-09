// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/structured/structured_metrics_provider.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_mock_clock_override.h"
#include "base/test/task_environment.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/values.h"
#include "components/metrics/structured/event.h"
#include "components/metrics/structured/event_base.h"
#include "components/metrics/structured/recorder.h"
#include "components/metrics/structured/storage.pb.h"
#include "components/metrics/structured/structured_metrics_features.h"
#include "components/metrics/structured/structured_mojo_events.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/metrics_proto/chrome_user_metrics_extension.pb.h"

namespace metrics {
namespace structured {

namespace {

// These project, event, and metric names are used for testing.

// The name hash of "TestProjectOne".
constexpr uint64_t kProjectOneHash = UINT64_C(16881314472396226433);
// The name hash of "TestProjectTwo".
constexpr uint64_t kProjectTwoHash = UINT64_C(5876808001962504629);
// The name hash of "TestProjectThree".
constexpr uint64_t kProjectThreeHash = UINT64_C(10860358748803291132);
// The name hash of "TestProjectFour".
constexpr uint64_t kProjectFourHash = UINT64_C(6801665881746546626);

// The name hash of "chrome::TestProjectOne::TestEventOne".
constexpr uint64_t kEventOneHash = UINT64_C(13593049295042080097);
// The name hash of "chrome::TestProjectTwo::TestEventTwo".
constexpr uint64_t kEventTwoHash = UINT64_C(8995967733561999410);
// The name hash of "chrome::TestProjectTwo::TestEventThree".
constexpr uint64_t kEventThreeHash = UINT64_C(5848687377041124372);
// The name hash of "chrome::TestProjectThree::TestEventFour".
constexpr uint64_t kEventFourHash = UINT64_C(1718797808092246258);
// The name hash of "chrome::TestProjectFour::TestEventFive".
constexpr uint64_t kEventFiveHash = UINT64_C(7045523601811399253);
// The name hash of "chrome::TestProjectFour::TestEventSix".
constexpr uint64_t kEventSixHash = UINT64_C(2873337042686447043);

// The name hash of "TestMetricOne".
constexpr uint64_t kMetricOneHash = UINT64_C(637929385654885975);
// The name hash of "TestMetricTwo".
constexpr uint64_t kMetricTwoHash = UINT64_C(14083999144141567134);
// The name hash of "TestMetricThree".
constexpr uint64_t kMetricThreeHash = UINT64_C(13469300759843809564);
// The name hash of "TestMetricFour".
constexpr uint64_t kMetricFourHash = UINT64_C(2917855408523247722);
// The name hash of "TestMetricFive".
constexpr uint64_t kMetricFiveHash = UINT64_C(8665976921794972190);
// The name hash of "TestMetricSix".
constexpr uint64_t kMetricSixHash = UINT64_C(3431522567539822144);

// The hex-encoded first 8 bytes of SHA256("aaa...a")
constexpr char kProjectOneId[] = "3BA3F5F43B926026";
// The hex-encoded first 8 bytes of SHA256("bbb...b")
constexpr char kProjectTwoId[] = "BDB339768BC5E4FE";
// The hex-encoded first 8 bytes of SHA256("ddd...d")
constexpr char kProjectFourId[] = "FBBBB6DE2AA74C3C";

// Test values.
constexpr char kValueOne[] = "value one";
constexpr char kValueTwo[] = "value two";

std::string HashToHex(const uint64_t hash) {
  return base::HexEncode(&hash, sizeof(uint64_t));
}

// Make a simple testing proto with one |uma_events| message for each id in
// |ids|.
EventsProto MakeExternalEventProto(const std::vector<uint64_t>& ids) {
  EventsProto proto;

  for (const auto id : ids) {
    auto* event = proto.add_uma_events();
    event->set_profile_event_id(id);
  }

  return proto;
}

}  // namespace

class StructuredMetricsProviderTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    Recorder::GetInstance()->SetUiTaskRunner(
        task_environment_.GetMainThreadTaskRunner());
    StructuredMetricsClient::Get()->SetDelegate(Recorder::GetInstance());
    // Move the mock date forward from day 0, because KeyData assumes that day 0
    // is a bug.
    task_environment_.AdvanceClock(base::Days(1000));
  }

  base::FilePath TempDirPath() { return temp_dir_.GetPath(); }

  base::FilePath ProfileKeyFilePath() {
    return temp_dir_.GetPath().Append("structured_metrics").Append("keys");
  }

  base::FilePath DeviceKeyFilePath() {
    return temp_dir_.GetPath()
        .Append("structured_metrics")
        .Append("device_keys");
  }

  void Wait() { task_environment_.RunUntilIdle(); }

  void WriteTestingProfileKeys() {
    const int today = (base::Time::Now() - base::Time::UnixEpoch()).InDays();

    KeyDataProto proto;
    KeyProto& key_one = (*proto.mutable_keys())[kProjectOneHash];
    key_one.set_key("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    key_one.set_last_rotation(today);
    key_one.set_rotation_period(90);

    KeyProto& key_two = (*proto.mutable_keys())[kProjectTwoHash];
    key_two.set_key("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    key_two.set_last_rotation(today);
    key_two.set_rotation_period(90);

    KeyProto& key_three = (*proto.mutable_keys())[kProjectThreeHash];
    key_three.set_key("cccccccccccccccccccccccccccccccc");
    key_three.set_last_rotation(today);
    key_three.set_rotation_period(90);

    base::CreateDirectory(ProfileKeyFilePath().DirName());
    ASSERT_TRUE(
        base::WriteFile(ProfileKeyFilePath(), proto.SerializeAsString()));
    Wait();
  }

  void WriteTestingDeviceKeys() {
    const int today = (base::Time::Now() - base::Time::UnixEpoch()).InDays();

    KeyDataProto proto;
    KeyProto& key = (*proto.mutable_keys())[kProjectFourHash];
    key.set_key("dddddddddddddddddddddddddddddddd");
    key.set_last_rotation(today);
    key.set_rotation_period(90);

    base::CreateDirectory(DeviceKeyFilePath().DirName());
    ASSERT_TRUE(
        base::WriteFile(DeviceKeyFilePath(), proto.SerializeAsString()));
    Wait();
  }

  KeyDataProto ReadKeys(const base::FilePath& filepath) {
    base::ScopedBlockingCall scoped_blocking_call(
        FROM_HERE, base::BlockingType::MAY_BLOCK);
    Wait();
    CHECK(base::PathExists(filepath));

    std::string proto_str;
    CHECK(base::ReadFileToString(filepath, &proto_str));

    KeyDataProto proto;
    CHECK(proto.ParseFromString(proto_str));
    return proto;
  }

  // Simulates the three external events that the structure metrics system cares
  // about: the metrics service initializing and enabling its providers, and a
  // user logging in.
  void Init() {
    // Create the provider, normally done by the ChromeMetricsServiceClient.
    provider_ = std::make_unique<StructuredMetricsProvider>();
    // Set the device key data to be within the temp dir, rather than to
    // /var/lib/metrics/structured as is default.
    provider_->SetDeviceKeyDataPathForTest(DeviceKeyFilePath());
    // Enable recording, normally done after the metrics service has checked
    // consent allows recording.
    provider_->OnRecordingEnabled();
    // Add a profile, normally done by the ChromeMetricsServiceClient after a
    // user logs in.
    provider_->OnProfileAdded(TempDirPath());
    Wait();
  }

  bool is_initialized() {
    return provider_->init_state_ ==
           StructuredMetricsProvider::InitState::kInitialized;
  }

  bool is_recording_enabled() { return provider_->recording_enabled_; }

  void OnRecordingEnabled() { provider_->OnRecordingEnabled(); }

  void OnRecordingDisabled() { provider_->OnRecordingDisabled(); }

  void OnReportingStateChanged(bool enabled) {
    provider_->OnReportingStateChanged(enabled);
  }

  void OnProfileAdded(const base::FilePath& path) {
    provider_->OnProfileAdded(path);
  }

  void WriteNow() {
    provider_->WriteNowForTest();
    Wait();
  }

  StructuredDataProto GetSessionData() {
    ChromeUserMetricsExtension uma_proto;
    provider_->ProvideCurrentSessionData(&uma_proto);
    Wait();
    return uma_proto.structured_data();
  }

  StructuredDataProto GetIndependentMetrics() {
    // Independent metrics are only reported at intervals. So advance time to
    // ensure HasIndependentMetrics will return true if there are recorded
    // metrics.
    task_environment_.AdvanceClock(base::Hours(1));

    ChromeUserMetricsExtension uma_proto;
    if (provider_->HasIndependentMetrics()) {
      provider_->ProvideIndependentMetrics(
          base::BindOnce([](bool success) { CHECK(success); }), &uma_proto,
          nullptr);
      Wait();
      return uma_proto.structured_data();
    }

    auto p = StructuredDataProto();
    return p;
  }

  void ExpectNoErrors() {
    histogram_tester_.ExpectTotalCount("UMA.StructuredMetrics.InternalError",
                                       0);
  }

  void SetExternalMetricsDirForTest(const base::FilePath dir) {
    provider_->SetExternalMetricsDirForTest(dir);
  }

 protected:
  std::unique_ptr<StructuredMetricsProvider> provider_;
  // Feature list should be constructed before task environment.
  base::test::ScopedFeatureList scoped_feature_list_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::UI,
      base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED,
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::HistogramTester histogram_tester_;
  base::ScopedTempDir temp_dir_;
};

// Simple test to ensure initialization works correctly in the case of a
// first-time run.
TEST_F(StructuredMetricsProviderTest, ProviderInitializesFromBlankSlate) {
  Init();
  EXPECT_TRUE(is_initialized());
  EXPECT_TRUE(is_recording_enabled());
  ExpectNoErrors();
}

// Ensure a call to OnRecordingDisabled prevents reporting.
TEST_F(StructuredMetricsProviderTest, EventsNotReportedWhenRecordingDisabled) {
  Init();
  OnRecordingDisabled();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  events::v2::test_project_three::TestEventFour().SetTestMetricFour(1).Record();
  EXPECT_EQ(GetIndependentMetrics().events_size(), 0);
  EXPECT_EQ(GetSessionData().events_size(), 0);
  ExpectNoErrors();
}

// Ensure that disabling the structured metrics feature flag prevents all
// structured metrics reporting.
TEST_F(StructuredMetricsProviderTest, EventsNotReportedWhenFeatureDisabled) {
  scoped_feature_list_.InitAndDisableFeature(kStructuredMetrics);

  Init();
  // OnRecordingEnabled should not actually enable recording because the flag is
  // disabled.
  OnRecordingEnabled();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  events::v2::test_project_three::TestEventFour().SetTestMetricFour(1).Record();
  EXPECT_EQ(GetIndependentMetrics().events_size(), 0);
  EXPECT_EQ(GetSessionData().events_size(), 0);
  ExpectNoErrors();
}

// Ensure that keys and unsent logs are deleted when reporting is disabled, and
// that reporting resumes when re-enabled.
TEST_F(StructuredMetricsProviderTest, ReportingStateChangesHandledCorrectly) {
  Init();

  // Record an event and read the keys, there should be one.
  events::v2::test_project_one::TestEventOne().Record();
  EXPECT_EQ(GetIndependentMetrics().events_size(), 1);
  const KeyDataProto enabled_proto = ReadKeys(ProfileKeyFilePath());
  EXPECT_EQ(enabled_proto.keys_size(), 1);

  // Record an event, disable reporting, then record another event. Both of
  // these events should have been ignored.
  events::v2::test_project_one::TestEventOne().Record();
  OnReportingStateChanged(false);
  events::v2::test_project_one::TestEventOne().Record();
  EXPECT_EQ(GetIndependentMetrics().events_size(), 0);

  // Read the keys again, it should be empty.
  const KeyDataProto disabled_proto = ReadKeys(ProfileKeyFilePath());
  EXPECT_EQ(disabled_proto.keys_size(), 0);

  // Enable reporting again, and record an event.
  OnReportingStateChanged(true);
  OnRecordingEnabled();
  events::v2::test_project_one::TestEventOne().Record();
  EXPECT_EQ(GetIndependentMetrics().events_size(), 1);
  const KeyDataProto reenabled_proto = ReadKeys(ProfileKeyFilePath());
  EXPECT_EQ(reenabled_proto.keys_size(), 1);

  ExpectNoErrors();
}

// Ensure that disabling independent upload of non-client_id metrics via feature
// flag instead uploads them in the main UMA upload.
TEST_F(StructuredMetricsProviderTest, DisableIndependentUploads) {
  scoped_feature_list_.InitAndEnableFeatureWithParameters(
      kStructuredMetrics, {{"enable_independent_metrics_upload", "false"}});

  Init();
  OnRecordingEnabled();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  events::v2::test_project_three::TestEventFour().SetTestMetricFour(1).Record();
  EXPECT_EQ(GetIndependentMetrics().events_size(), 0);
  EXPECT_EQ(GetSessionData().events_size(), 2);
  ExpectNoErrors();
}

// Ensure that, if recording is disabled part-way through initialization, the
// initialization still completes correctly, but recording is correctly set to
// disabled.
TEST_F(StructuredMetricsProviderTest, RecordingDisabledDuringInitialization) {
  provider_ = std::make_unique<StructuredMetricsProvider>();

  OnProfileAdded(TempDirPath());
  OnRecordingDisabled();
  EXPECT_FALSE(is_initialized());
  EXPECT_FALSE(is_recording_enabled());

  Wait();
  EXPECT_TRUE(is_initialized());
  EXPECT_FALSE(is_recording_enabled());

  ExpectNoErrors();
}

// Ensure that recording is disabled until explicitly enabled with a call to
// OnRecordingEnabled.
TEST_F(StructuredMetricsProviderTest, RecordingDisabledByDefault) {
  provider_ = std::make_unique<StructuredMetricsProvider>();

  OnProfileAdded(TempDirPath());
  Wait();
  EXPECT_TRUE(is_initialized());
  EXPECT_FALSE(is_recording_enabled());

  OnRecordingEnabled();
  EXPECT_TRUE(is_recording_enabled());

  ExpectNoErrors();
}

TEST_F(StructuredMetricsProviderTest, RecordedEventAppearsInReport) {
  Init();

  events::v2::test_project_one::TestEventOne()
      .SetTestMetricOne("a string")
      .SetTestMetricTwo(12345)
      .Record();
  events::v2::test_project_one::TestEventOne()
      .SetTestMetricOne("a string")
      .SetTestMetricTwo(12345)
      .Record();
  events::v2::test_project_one::TestEventOne()
      .SetTestMetricOne("a string")
      .SetTestMetricTwo(12345)
      .Record();

  EXPECT_EQ(GetIndependentMetrics().events_size(), 3);
  // TestProjectOne is not UMA ID'd, so GetSessionData should be empty.
  EXPECT_EQ(GetSessionData().events_size(), 0);
  ExpectNoErrors();
}

TEST_F(StructuredMetricsProviderTest, UmaEventsReportedCorrectly) {
  WriteTestingProfileKeys();
  Init();

  events::v2::test_project_three::TestEventFour()
      .SetTestMetricFour(12345)
      .Record();
  events::v2::test_project_three::TestEventFour()
      .SetTestMetricFour(67890)
      .Record();

  const auto data = GetSessionData();
  ASSERT_EQ(data.events_size(), 2);

  {  // First event
    const auto& event = data.events(0);
    EXPECT_EQ(event.event_name_hash(), kEventFourHash);
    // TODO(crbug.com/1148168): The UMA ID currently isn't attached to UMA
    // events, so just check it isn't set.
    EXPECT_FALSE(event.has_profile_event_id());
    ASSERT_EQ(event.metrics_size(), 1);
    const auto& metric = event.metrics(0);
    EXPECT_EQ(metric.name_hash(), kMetricFourHash);
    EXPECT_EQ(metric.value_int64(), 12345);
  }

  {  // Second event
    const auto& event = data.events(1);
    EXPECT_EQ(event.event_name_hash(), kEventFourHash);
    // TODO(crbug.com/1148168): The UMA ID currently isn't attached to UMA
    // events, so just check it isn't set.
    EXPECT_FALSE(event.has_profile_event_id());
    ASSERT_EQ(event.metrics_size(), 1);
    const auto& metric = event.metrics(0);
    EXPECT_EQ(metric.name_hash(), kMetricFourHash);
    EXPECT_EQ(metric.value_int64(), 67890);
  }

  histogram_tester_.ExpectTotalCount("UMA.StructuredMetrics.InternalError", 0);
}

TEST_F(StructuredMetricsProviderTest, IndependentEventsReportedCorrectly) {
  WriteTestingProfileKeys();
  Init();

  events::v2::test_project_one::TestEventOne()
      .SetTestMetricOne(kValueOne)
      .SetTestMetricTwo(12345)
      .Record();
  events::v2::test_project_two::TestEventTwo()
      .SetTestMetricThree(kValueTwo)
      .Record();

  const auto data = GetIndependentMetrics();
  ASSERT_EQ(data.events_size(), 2);

  {  // First event
    const auto& event = data.events(0);
    EXPECT_EQ(event.event_name_hash(), kEventOneHash);
    EXPECT_EQ(HashToHex(event.profile_event_id()), kProjectOneId);
    ASSERT_EQ(event.metrics_size(), 2);

    {  // First metric
      const auto& metric = event.metrics(0);
      EXPECT_EQ(metric.name_hash(), kMetricOneHash);
      EXPECT_EQ(HashToHex(metric.value_hmac()),
                // Value of HMAC_256("aaa...a", concat(hex(kMetricOneHash),
                // kValueOne))
                "8C2469269D142715");
    }

    {  // Second metric
      const auto& metric = event.metrics(1);
      EXPECT_EQ(metric.name_hash(), kMetricTwoHash);
      EXPECT_EQ(metric.value_int64(), 12345);
    }
  }

  {  // Second event
    const auto& event = data.events(1);
    EXPECT_EQ(event.event_name_hash(), kEventTwoHash);
    EXPECT_EQ(HashToHex(event.profile_event_id()), kProjectTwoId);
    ASSERT_EQ(event.metrics_size(), 1);

    {  // First metric
      const auto& metric = event.metrics(0);
      EXPECT_EQ(metric.name_hash(), kMetricThreeHash);
      EXPECT_EQ(HashToHex(metric.value_hmac()),
                // Value of HMAC_256("bbb...b", concat(hex(kProjectTwoHash),
                // kValueTwo))
                "86F0169868588DC7");
    }
  }

  histogram_tester_.ExpectTotalCount("UMA.StructuredMetrics.InternalError", 0);
}

// Ensure that events containing raw string metrics are reported correctly.
TEST_F(StructuredMetricsProviderTest, RawStringMetricsReportedCorrectly) {
  Init();

  const std::string test_string = "a raw string value";
  events::v2::test_project_five::TestEventSix()
      .SetTestMetricSix(test_string)
      .Record();

  const auto data = GetIndependentMetrics();
  ASSERT_EQ(data.events_size(), 1);

  const auto& event = data.events(0);
  EXPECT_EQ(event.event_name_hash(), kEventSixHash);
  EXPECT_FALSE(event.has_profile_event_id());
  EXPECT_EQ(event.event_type(), StructuredEventProto_EventType_RAW_STRING);

  ASSERT_EQ(event.metrics_size(), 1);
  const auto& metric = event.metrics(0);

  EXPECT_EQ(metric.name_hash(), kMetricSixHash);
  EXPECT_EQ(metric.value_string(), test_string);
}

TEST_F(StructuredMetricsProviderTest, DeviceKeysUsedForDeviceScopedProjects) {
  WriteTestingProfileKeys();
  WriteTestingDeviceKeys();
  Init();

  // This event's project has device scope set, so should use the per-device
  // keys set by WriteTestingDeviceKeys. In this case the expected key is
  // "ddd...d", which we observe by checking the ID and HMAC have the correct
  // value given that key.
  events::v2::test_project_four::TestEventFive()
      .SetTestMetricFive("value")
      .Record();

  const auto data = GetIndependentMetrics();
  ASSERT_EQ(data.events_size(), 1);

  const auto& event = data.events(0);
  EXPECT_EQ(event.event_name_hash(), kEventFiveHash);
  // The hex-encoded first 8 bytes of SHA256("ddd...d").
  EXPECT_EQ(HashToHex(event.profile_event_id()), kProjectFourId);
  ASSERT_EQ(event.metrics_size(), 1);

  const auto& metric = event.metrics(0);
  EXPECT_EQ(metric.name_hash(), kMetricFiveHash);
  EXPECT_EQ(HashToHex(metric.value_hmac()),
            // Value of HMAC_256("ddd...d", concat(hex(kMetricFiveHash),
            // "value"))
            "4CC202FAA78FDC7A");

  histogram_tester_.ExpectTotalCount("UMA.StructuredMetrics.InternalError", 0);
}

// Check that a full int64 can be recorded, and is not truncated to an int32.
TEST_F(StructuredMetricsProviderTest, Int64MetricsNotTruncated) {
  Init();
  const int64_t big = 1ll << 60;
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(big).Record();

  const auto data = GetIndependentMetrics();
  ASSERT_EQ(data.events_size(), 1);
  const auto& event = data.events(0);
  ASSERT_EQ(event.metrics_size(), 1);
  const auto& metric = event.metrics(0);
  EXPECT_EQ(metric.value_int64(), big);
}

TEST_F(StructuredMetricsProviderTest, EventsWithinProjectReportedWithSameID) {
  WriteTestingProfileKeys();
  Init();

  events::v2::test_project_one::TestEventOne().Record();
  events::v2::test_project_two::TestEventTwo().Record();
  events::v2::test_project_two::TestEventThree().Record();

  const auto data = GetIndependentMetrics();
  // TestProjectOne is not UMA ID'd, so GetSessionData should be empty.
  EXPECT_EQ(GetSessionData().events_size(), 0);
  ASSERT_EQ(data.events_size(), 3);

  const auto& event_one = data.events(0);
  const auto& event_two = data.events(1);
  const auto& event_three = data.events(2);

  // Check events are in the right order.
  EXPECT_EQ(event_one.event_name_hash(), kEventOneHash);
  EXPECT_EQ(event_two.event_name_hash(), kEventTwoHash);
  EXPECT_EQ(event_three.event_name_hash(), kEventThreeHash);

  // Events two and three share a project, so should have the same ID. Event
  // one should have its own ID.
  EXPECT_EQ(HashToHex(event_one.profile_event_id()), kProjectOneId);
  EXPECT_EQ(HashToHex(event_two.profile_event_id()), kProjectTwoId);
  EXPECT_EQ(HashToHex(event_three.profile_event_id()), kProjectTwoId);

  histogram_tester_.ExpectTotalCount("UMA.StructuredMetrics.InternalError", 0);
}

// Test that a call to ProvideCurrentSessionData clears the provided events from
// the cache, and a subsequent call does not return those events again.
TEST_F(StructuredMetricsProviderTest, EventsClearedAfterReport) {
  Init();

  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(2).Record();
  // TestProjectOne is not UMA ID'd, so GetSessionData should be empty.
  EXPECT_EQ(GetSessionData().events_size(), 0);
  // Should provide both the previous events.
  EXPECT_EQ(GetIndependentMetrics().events_size(), 2);

  // But the previous events shouldn't appear in the second report.
  EXPECT_EQ(GetIndependentMetrics().events_size(), 0);

  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(3).Record();
  // The third request should only contain the third event.
  EXPECT_EQ(GetIndependentMetrics().events_size(), 1);

  ExpectNoErrors();
}

// Test that events recorded in one session are correctly persisted and are
// uploaded in the first report from a subsequent session.
TEST_F(StructuredMetricsProviderTest, EventsFromPreviousSessionAreReported) {
  // Start first session and record one event.
  Init();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1234).Record();

  // Write events to disk, then destroy the provider.
  WriteNow();
  provider_.reset();

  // Start a second session and ensure the event is reported.
  Init();
  const auto data = GetIndependentMetrics();
  ASSERT_EQ(data.events_size(), 1);
  ASSERT_EQ(data.events(0).metrics_size(), 1);
  EXPECT_EQ(data.events(0).metrics(0).value_int64(), 1234);
  EXPECT_EQ(GetSessionData().events_size(), 0);

  ExpectNoErrors();
}

TEST_F(StructuredMetricsProviderTest, ExternalMetricsAreReported) {
  const base::FilePath events_dir(TempDirPath().Append("events"));
  base::CreateDirectory(events_dir);

  const auto proto = MakeExternalEventProto({111, 222, 333});
  ASSERT_TRUE(
      base::WriteFile(events_dir.Append("event"), proto.SerializeAsString()));

  provider_ = std::make_unique<StructuredMetricsProvider>();
  OnProfileAdded(TempDirPath());
  OnRecordingEnabled();
  SetExternalMetricsDirForTest(events_dir);
  task_environment_.AdvanceClock(base::Hours(10));
  Wait();
  EXPECT_EQ(GetSessionData().events_size(), 3);
}

// Test that events reported at various stages before and during initialization
// are ignored (and don't cause a crash).
TEST_F(StructuredMetricsProviderTest, EventsNotRecordedBeforeInitialization) {
  // Manually create and initialize the provider, adding recording calls between
  // each step. All of these events should be ignored.
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  provider_ = std::make_unique<StructuredMetricsProvider>();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  OnRecordingEnabled();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  OnProfileAdded(TempDirPath());
  // This one should still fail even though all of the initialization calls are
  // done, because the provider hasn't finished loading the keys from disk.
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  Wait();
  EXPECT_EQ(GetSessionData().events_size(), 0);
  EXPECT_EQ(GetIndependentMetrics().events_size(), 0);

  ExpectNoErrors();
}

// Ensure a call to OnRecordingDisabled not only prevents the reporting of new
// events, but also clears the cache of any existing events that haven't yet
// been reported.
TEST_F(StructuredMetricsProviderTest,
       ExistingEventsClearedWhenRecordingDisabled) {
  Init();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  events::v2::test_project_three::TestEventFour().SetTestMetricFour(1).Record();
  OnRecordingDisabled();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  events::v2::test_project_three::TestEventFour().SetTestMetricFour(1).Record();
  EXPECT_EQ(GetSessionData().events_size(), 0);
  EXPECT_EQ(GetIndependentMetrics().events_size(), 0);

  ExpectNoErrors();
}

// Ensure that recording and reporting is re-enabled after recording is disabled
// and then enabled again.
TEST_F(StructuredMetricsProviderTest, ReportingResumesWhenEnabled) {
  Init();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  events::v2::test_project_three::TestEventFour().SetTestMetricFour(1).Record();

  OnRecordingDisabled();
  OnRecordingEnabled();

  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  events::v2::test_project_one::TestEventOne().SetTestMetricTwo(1).Record();
  events::v2::test_project_three::TestEventFour().SetTestMetricFour(1).Record();

  EXPECT_EQ(GetSessionData().events_size(), 2);
  EXPECT_EQ(GetIndependentMetrics().events_size(), 4);

  ExpectNoErrors();
}

// Ensure that a call to ProvideCurrentSessionData before initialization
// completes returns no events.
TEST_F(StructuredMetricsProviderTest,
       ReportsNothingBeforeInitializationComplete) {
  provider_ = std::make_unique<StructuredMetricsProvider>();
  EXPECT_EQ(GetSessionData().events_size(), 0);
  EXPECT_EQ(GetIndependentMetrics().events_size(), 0);
  OnRecordingEnabled();
  EXPECT_EQ(GetSessionData().events_size(), 0);
  EXPECT_EQ(GetIndependentMetrics().events_size(), 0);
  OnProfileAdded(TempDirPath());
  EXPECT_EQ(GetSessionData().events_size(), 0);
  EXPECT_EQ(GetIndependentMetrics().events_size(), 0);
}

// Check that LastKeyRotation returns a value in the correct range of possible
// last rotations for a newly generated key.
TEST_F(StructuredMetricsProviderTest, LastKeyRotation) {
  Init();

  events::v2::test_project_one::TestEventOne event;
  auto event_base = EventBase::FromEvent(event);

  // Record a metric so that the key is created.
  event.Record();

  const int today = (base::Time::Now() - base::Time::UnixEpoch()).InDays();
  const absl::optional<int> last_rotation = event_base->LastKeyRotation();

  // The last rotation should be a random day between today and 90 days in the
  // past, ie. the rotation period for this project.
  ASSERT_TRUE(last_rotation.has_value());
  EXPECT_GE(last_rotation, today - 90);
}

}  // namespace structured
}  // namespace metrics
