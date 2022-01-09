// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/metrics_state_manager.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <utility>

#include "base/base_switches.h"
#include "base/callback_helpers.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/debug/leak_annotations.h"
#include "base/guid.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/numerics/safe_conversions.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "components/metrics/cloned_install_detector.h"
#include "components/metrics/enabled_state_provider.h"
#include "components/metrics/entropy_state.h"
#include "components/metrics/metrics_data_validation.h"
#include "components/metrics/metrics_log.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/metrics/metrics_provider.h"
#include "components/metrics/metrics_switches.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/variations/entropy_provider.h"
#include "components/variations/field_trial_config/field_trial_util.h"
#include "components/variations/pref_names.h"
#include "components/variations/variations_switches.h"
#include "third_party/metrics_proto/chrome_user_metrics_extension.pb.h"
#include "third_party/metrics_proto/system_profile.pb.h"

namespace metrics {
namespace {

// The argument used to generate a non-identifying entropy source. We want no
// more than 13 bits of entropy, so use this max to return a number in the range
// [0, 7999] as the entropy source (12.97 bits of entropy).
const int kMaxLowEntropySize = 8000;

int64_t ReadEnabledDate(PrefService* local_state) {
  return local_state->GetInt64(prefs::kMetricsReportingEnabledTimestamp);
}

int64_t ReadInstallDate(PrefService* local_state) {
  return local_state->GetInt64(prefs::kInstallDate);
}

std::string ReadClientId(PrefService* local_state) {
  return local_state->GetString(prefs::kMetricsClientID);
}

// Round a timestamp measured in seconds since epoch to one with a granularity
// of an hour. This can be used before uploaded potentially sensitive
// timestamps.
int64_t RoundSecondsToHour(int64_t time_in_seconds) {
  return 3600 * (time_in_seconds / 3600);
}

// Records the cloned install histogram.
void LogClonedInstall() {
  // Equivalent to UMA_HISTOGRAM_BOOLEAN with the stability flag set.
  UMA_STABILITY_HISTOGRAM_ENUMERATION("UMA.IsClonedInstall", 1, 2);
}

// No-op function used to create a MetricsStateManager.
std::unique_ptr<metrics::ClientInfo> NoOpLoadClientInfoBackup() {
  return nullptr;
}

// Exits the browser with a helpful error message if an invalid,
// field-trial-related command-line flag was specified.
void ExitWithMessage(const std::string& message) {
  puts(message.c_str());
  exit(1);
}

// Returns a log normal distribution based on the feature params of
// |kNonUniformityValidationFeature|.
std::lognormal_distribution<double> GetLogNormalDist() {
  double mean = kLogNormalMean.Get();
  double delta = kLogNormalDelta.Get();
  double std_dev = kLogNormalStdDev.Get();
  return std::lognormal_distribution<double>(mean + std::log(1.0 + delta),
                                             std_dev);
}

// Used to draw a data point from a log normal distribution.
struct LogNormalMetricState {
  LogNormalMetricState()
      : dist(GetLogNormalDist()), gen(std::mt19937(base::RandUint64())) {}

  // Records the artificial non-uniformity histogram for data validation.
  void LogArtificialNonUniformity() {
    double rand = dist(gen);
    // We pick 10k as the upper bound for this histogram so as to avoid losing
    // precision. See comments for |kLogNormalMean|.
    base::UmaHistogramCounts10000("UMA.DataValidation.LogNormal",
                                  base::saturated_cast<int>(rand));
  }

  // A log normal distribution generator generated by the `GetLogNormalDist()`
  // function.
  std::lognormal_distribution<double> dist;
  // The pseudo-random generator used to generate a data point from |dist|.
  std::mt19937 gen;
};

class MetricsStateMetricsProvider : public MetricsProvider {
 public:
  MetricsStateMetricsProvider(
      PrefService* local_state,
      bool metrics_ids_were_reset,
      std::string previous_client_id,
      std::string initial_client_id,
      ClonedInstallDetector const& cloned_install_detector)
      : local_state_(local_state),
        metrics_ids_were_reset_(metrics_ids_were_reset),
        previous_client_id_(std::move(previous_client_id)),
        initial_client_id_(std::move(initial_client_id)),
        cloned_install_detector_(cloned_install_detector) {}

  MetricsStateMetricsProvider(const MetricsStateMetricsProvider&) = delete;
  MetricsStateMetricsProvider& operator=(const MetricsStateMetricsProvider&) =
      delete;

  // MetricsProvider:
  void ProvideSystemProfileMetrics(
      SystemProfileProto* system_profile) override {
    system_profile->set_uma_enabled_date(
        RoundSecondsToHour(ReadEnabledDate(local_state_)));
    system_profile->set_install_date(
        RoundSecondsToHour(ReadInstallDate(local_state_)));

    // Client id in the log shouldn't be different than the |local_state_| one
    // except when the client disabled UMA before we populate this field to the
    // log. If that's the case, the client id in the |local_state_| should be
    // empty and we should set |client_id_was_used_for_trial_assignment| to
    // false.
    std::string client_id = ReadClientId(local_state_);
    system_profile->set_client_id_was_used_for_trial_assignment(
        !client_id.empty() && client_id == initial_client_id_);

    ClonedInstallInfo cloned =
        ClonedInstallDetector::ReadClonedInstallInfo(local_state_);
    if (cloned.reset_count == 0)
      return;
    auto* cloned_install_info = system_profile->mutable_cloned_install_info();
    if (metrics_ids_were_reset_) {
      // Only report the cloned from client_id in the resetting session.
      if (!previous_client_id_.empty()) {
        cloned_install_info->set_cloned_from_client_id(
            MetricsLog::Hash(previous_client_id_));
      }
    }
    cloned_install_info->set_last_timestamp(
        RoundSecondsToHour(cloned.last_reset_timestamp));
    cloned_install_info->set_first_timestamp(
        RoundSecondsToHour(cloned.first_reset_timestamp));
    cloned_install_info->set_count(cloned.reset_count);
  }

  void ProvidePreviousSessionData(
      ChromeUserMetricsExtension* uma_proto) override {
    if (metrics_ids_were_reset_) {
      LogClonedInstall();
      if (!previous_client_id_.empty()) {
        // If we know the previous client id, overwrite the client id for the
        // previous session log so the log contains the client id at the time
        // of the previous session. This allows better attribution of crashes
        // to earlier behavior. If the previous client id is unknown, leave
        // the current client id.
#if BUILDFLAG(IS_CHROMEOS_ASH)
        metrics::structured::NeutrinoDevicesLogWithClientId(
            previous_client_id_, metrics::structured::NeutrinoDevicesLocation::
                                     kProvidePreviousSessionData);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
        uma_proto->set_client_id(MetricsLog::Hash(previous_client_id_));
      }
    }
  }

  void ProvideCurrentSessionData(
      ChromeUserMetricsExtension* uma_proto) override {
    if (cloned_install_detector_.ClonedInstallDetectedInCurrentSession())
      LogClonedInstall();
    log_normal_metric_state_.LogArtificialNonUniformity();
  }

  // Set a random seed for the random number generator.
  void SetRandomSeedForTesting(int64_t seed) {
    log_normal_metric_state_.gen = std::mt19937(seed);
  }

 private:
  const raw_ptr<PrefService> local_state_;
  const bool metrics_ids_were_reset_;
  // |previous_client_id_| is set only (if known) when
  // |metrics_ids_were_reset_|
  const std::string previous_client_id_;
  // The client id that was used to randomize field trials. An empty string if
  // the low entropy source was used to do randomization.
  const std::string initial_client_id_;
  const ClonedInstallDetector& cloned_install_detector_;
  LogNormalMetricState log_normal_metric_state_;
};

}  // namespace

// static
bool MetricsStateManager::instance_exists_ = false;

MetricsStateManager::MetricsStateManager(
    PrefService* local_state,
    EnabledStateProvider* enabled_state_provider,
    const std::wstring& backup_registry_key,
    const base::FilePath& user_data_dir,
    StartupVisibility startup_visibility,
    version_info::Channel channel,
    StoreClientInfoCallback store_client_info,
    LoadClientInfoCallback retrieve_client_info,
    base::StringPiece external_client_id)
    : local_state_(local_state),
      enabled_state_provider_(enabled_state_provider),
      store_client_info_(std::move(store_client_info)),
      load_client_info_(std::move(retrieve_client_info)),
      clean_exit_beacon_(backup_registry_key,
                         user_data_dir,
                         local_state,
                         channel),
      external_client_id_(external_client_id),
      entropy_state_(local_state),
      entropy_source_returned_(ENTROPY_SOURCE_NONE),
      metrics_ids_were_reset_(false),
      startup_visibility_(startup_visibility) {
  DCHECK(!store_client_info_.is_null());
  DCHECK(!load_client_info_.is_null());
  ResetMetricsIDsIfNecessary();

  bool is_first_run = false;
  int64_t install_date = local_state_->GetInt64(prefs::kInstallDate);

  // Set the install date if this is our first run.
  if (install_date == 0) {
    local_state_->SetInt64(prefs::kInstallDate, base::Time::Now().ToTimeT());
    is_first_run = true;
  }

  if (enabled_state_provider_->IsConsentGiven()) {
#if BUILDFLAG(IS_CHROMEOS_ASH)
    metrics::structured::NeutrinoDevicesLogWithClientId(
        client_id_,
        metrics::structured::NeutrinoDevicesLocation::kMetricsStateManager);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
    ForceClientIdCreation();
  }

#if defined(OS_WIN)
  ALLOW_UNUSED_LOCAL(is_first_run);
#else
  if (is_first_run) {
    // If this is a first run (no install date) and there's no client id, then
    // generate a provisional client id now. This id will be used for field
    // trial randomization on first run and will be promoted to become the
    // client id if UMA is enabled during this session, via the logic in
    // ForceClientIdCreation().
    //
    // Note: We don't do this on Windows because on Windows, there's no UMA
    // checkbox on first run and instead it comes from the install page. So if
    // UMA is not enabled at this point, it's unlikely it will be enabled in
    // the same session since that requires the user to manually do that via
    // settings page after they unchecked it on the download page.
    //
    // Note: Windows first run is covered by browser tests
    // FirstRunMasterPrefsVariationsSeedTest.PRE_SecondRun and
    // FirstRunMasterPrefsVariationsSeedTest.SecondRun. If the platform ifdef
    // for this logic changes, the tests should be updated as well.
    if (client_id_.empty())
      provisional_client_id_ = base::GenerateGUID();
  }
#endif  // !defined(OS_WIN)

  // The |initial_client_id_| should only be set if UMA is enabled or there's a
  // provisional client id.
  initial_client_id_ =
      (client_id_.empty() ? provisional_client_id_ : client_id_);
  DCHECK(!instance_exists_);
  instance_exists_ = true;
}

MetricsStateManager::~MetricsStateManager() {
  DCHECK(instance_exists_);
  instance_exists_ = false;
}

std::unique_ptr<MetricsProvider> MetricsStateManager::GetProvider() {
  return std::make_unique<MetricsStateMetricsProvider>(
      local_state_, metrics_ids_were_reset_, previous_client_id_,
      initial_client_id_, cloned_install_detector_);
}

std::unique_ptr<MetricsProvider>
MetricsStateManager::GetProviderAndSetRandomSeedForTesting(int64_t seed) {
  auto provider = std::make_unique<MetricsStateMetricsProvider>(
      local_state_, metrics_ids_were_reset_, previous_client_id_,
      initial_client_id_, cloned_install_detector_);
  provider->SetRandomSeedForTesting(seed);  // IN-TEST
  return provider;
}

bool MetricsStateManager::IsMetricsReportingEnabled() {
  return enabled_state_provider_->IsReportingEnabled();
}

int MetricsStateManager::GetLowEntropySource() {
  return entropy_state_.GetLowEntropySource();
}

void MetricsStateManager::InstantiateFieldTrialList(
    const char* enable_gpu_benchmarking_switch,
    EntropyProviderType entropy_provider_type) {
  // Instantiate the FieldTrialList to support field trials. If an instance
  // already exists, this is likely a test scenario with a ScopedFeatureList, so
  // use the existing instance so that any overrides are still applied.
  if (!base::FieldTrialList::GetInstance()) {
    std::unique_ptr<const base::FieldTrial::EntropyProvider> entropy_provider =
        entropy_provider_type == EntropyProviderType::kLow
            ? CreateLowEntropyProvider()
            : CreateDefaultEntropyProvider();

    // This is intentionally leaked since it needs to live for the duration of
    // the browser process and there's no benefit in cleaning it up at exit.
    base::FieldTrialList* leaked_field_trial_list =
        new base::FieldTrialList(std::move(entropy_provider));
    ANNOTATE_LEAKING_OBJECT_PTR(leaked_field_trial_list);
    std::ignore = leaked_field_trial_list;
  }

  // TODO(crbug/1257204): Some FieldTrial-setup-related code is here and some is
  // in VariationsFieldTrialCreator::SetUpFieldTrials(). It's not ideal that
  // it's in two places.
  //
  // When benchmarking is enabled, field trials' default groups are chosen, so
  // see whether benchmarking needs to be enabled here, before any field trials
  // are created.
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  // TODO(crbug/1251680): See whether it's possible to consolidate the switches.
  if (command_line->HasSwitch(variations::switches::kEnableBenchmarking) ||
      (enable_gpu_benchmarking_switch &&
       command_line->HasSwitch(enable_gpu_benchmarking_switch))) {
    base::FieldTrial::EnableBenchmarking();
  }

  if (command_line->HasSwitch(variations::switches::kForceFieldTrialParams)) {
    bool result =
        variations::AssociateParamsFromString(command_line->GetSwitchValueASCII(
            variations::switches::kForceFieldTrialParams));
    if (!result) {
      // Some field trial params implement things like csv or json with a
      // particular param. If some control characters are not %-encoded, it can
      // lead to confusing error messages, so add a hint here.
      ExitWithMessage(base::StringPrintf(
          "Invalid --%s list specified. Make sure you %%-"
          "encode the following characters in param values: %%:/.,",
          variations::switches::kForceFieldTrialParams));
    }
  }

  // Ensure any field trials specified on the command line are initialized.
  if (command_line->HasSwitch(::switches::kForceFieldTrials)) {
    // Create field trials without activating them, so that this behaves in a
    // consistent manner with field trials created from the server.
    bool result = base::FieldTrialList::CreateTrialsFromString(
        command_line->GetSwitchValueASCII(::switches::kForceFieldTrials));
    if (!result) {
      ExitWithMessage(base::StringPrintf("Invalid --%s list specified.",
                                         ::switches::kForceFieldTrials));
    }
  }

  // Initializing the CleanExitBeacon is done after FieldTrialList instantiation
  // to allow experimentation on the CleanExitBeacon.
  clean_exit_beacon_.Initialize();
}

void MetricsStateManager::LogHasSessionShutdownCleanly(
    bool has_session_shutdown_cleanly,
    bool write_synchronously) {
  clean_exit_beacon_.WriteBeaconValue(has_session_shutdown_cleanly,
                                      write_synchronously);
}

void MetricsStateManager::ForceClientIdCreation() {
  // TODO(asvitkine): Ideally, all tests would actually set up consent properly,
  // so the command-line checks wouldn't be needed here.
  // Currently, kForceEnableMetricsReporting is used by Java UkmTest and
  // kMetricsRecordingOnly is used by Chromedriver tests.
  DCHECK(enabled_state_provider_->IsConsentGiven() ||
         IsMetricsReportingForceEnabled() || IsMetricsRecordingOnlyEnabled());
  if (!external_client_id_.empty()) {
    client_id_ = external_client_id_;
    base::UmaHistogramEnumeration("UMA.ClientIdSource",
                                  ClientIdSource::kClientIdFromExternal);
    local_state_->SetString(prefs::kMetricsClientID, client_id_);
    return;
  }
#if BUILDFLAG(IS_CHROMEOS_ASH)
  std::string previous_client_id = client_id_;
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
  {
    std::string client_id_from_prefs = ReadClientId(local_state_);
    // If client id in prefs matches the cached copy, return early.
    if (!client_id_from_prefs.empty() && client_id_from_prefs == client_id_) {
      base::UmaHistogramEnumeration("UMA.ClientIdSource",
                                    ClientIdSource::kClientIdMatches);
      return;
    }
    client_id_.swap(client_id_from_prefs);
  }

  if (!client_id_.empty()) {
    base::UmaHistogramEnumeration("UMA.ClientIdSource",
                                  ClientIdSource::kClientIdFromLocalState);
#if BUILDFLAG(IS_CHROMEOS_ASH)
    LogClientIdChanged(
        metrics::structured::NeutrinoDevicesLocation::kClientIdFromLocalState,
        previous_client_id);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
    return;
  }

  const std::unique_ptr<ClientInfo> client_info_backup = LoadClientInfo();
  if (client_info_backup) {
    client_id_ = client_info_backup->client_id;

    const base::Time now = base::Time::Now();

    // Save the recovered client id and also try to reinstantiate the backup
    // values for the dates corresponding with that client id in order to avoid
    // weird scenarios where we could report an old client id with a recent
    // install date.
    local_state_->SetString(prefs::kMetricsClientID, client_id_);
    local_state_->SetInt64(prefs::kInstallDate,
                           client_info_backup->installation_date != 0
                               ? client_info_backup->installation_date
                               : now.ToTimeT());
    local_state_->SetInt64(prefs::kMetricsReportingEnabledTimestamp,
                           client_info_backup->reporting_enabled_date != 0
                               ? client_info_backup->reporting_enabled_date
                               : now.ToTimeT());

    base::TimeDelta recovered_installation_age;
    if (client_info_backup->installation_date != 0) {
      recovered_installation_age =
          now - base::Time::FromTimeT(client_info_backup->installation_date);
    }
    base::UmaHistogramEnumeration("UMA.ClientIdSource",
                                  ClientIdSource::kClientIdBackupRecovered);
    base::UmaHistogramCounts10000("UMA.ClientIdBackupRecoveredWithAge",
                                  recovered_installation_age.InHours());
#if BUILDFLAG(IS_CHROMEOS_ASH)
    LogClientIdChanged(
        metrics::structured::NeutrinoDevicesLocation::kClientIdBackupRecovered,
        previous_client_id);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

    // Flush the backup back to persistent storage in case we re-generated
    // missing data above.
    BackUpCurrentClientInfo();
    return;
  }

  // If we're here, there was no client ID yet (either in prefs or backup),
  // so generate a new one. If there's a provisional client id (e.g. UMA
  // was enabled as part of first run), promote that to the client id,
  // otherwise (e.g. UMA enabled in a future session), generate a new one.
  if (provisional_client_id_.empty()) {
    client_id_ = base::GenerateGUID();
    base::UmaHistogramEnumeration("UMA.ClientIdSource",
                                  ClientIdSource::kClientIdNew);
#if BUILDFLAG(IS_CHROMEOS_ASH)
    LogClientIdChanged(
        metrics::structured::NeutrinoDevicesLocation::kClientIdNew,
        previous_client_id);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
  } else {
    client_id_ = provisional_client_id_;
    provisional_client_id_.clear();
    base::UmaHistogramEnumeration("UMA.ClientIdSource",
                                  ClientIdSource::kClientIdFromProvisionalId);
#if BUILDFLAG(IS_CHROMEOS_ASH)
    LogClientIdChanged(metrics::structured::NeutrinoDevicesLocation::
                           kClientIdFromProvisionalId,
                       previous_client_id);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
  }
  local_state_->SetString(prefs::kMetricsClientID, client_id_);

  // Record the timestamp of when the user opted in to UMA.
  local_state_->SetInt64(prefs::kMetricsReportingEnabledTimestamp,
                         base::Time::Now().ToTimeT());

  BackUpCurrentClientInfo();
}

void MetricsStateManager::CheckForClonedInstall() {
  cloned_install_detector_.CheckForClonedInstall(local_state_);
}

bool MetricsStateManager::ShouldResetClientIdsOnClonedInstall() {
  return cloned_install_detector_.ShouldResetClientIds(local_state_);
}

std::unique_ptr<const base::FieldTrial::EntropyProvider>
MetricsStateManager::CreateDefaultEntropyProvider() {
  // |initial_client_id_| should be populated iff (a) we have the client's
  // consent to enable UMA on startup or (b) it's the first run, in which case
  // |initial_client_id_| corresponds to |provisional_client_id_|.
  if (!initial_client_id_.empty()) {
    UpdateEntropySourceReturnedValue(ENTROPY_SOURCE_HIGH);
    return std::make_unique<variations::SHA1EntropyProvider>(
        GetHighEntropySource());
  }

  UpdateEntropySourceReturnedValue(ENTROPY_SOURCE_LOW);
  return CreateLowEntropyProvider();
}

std::unique_ptr<const base::FieldTrial::EntropyProvider>
MetricsStateManager::CreateLowEntropyProvider() {
  int source = GetLowEntropySource();
  return std::make_unique<variations::NormalizedMurmurHashEntropyProvider>(
      base::checked_cast<uint16_t>(source), kMaxLowEntropySize);
}

// static
std::unique_ptr<MetricsStateManager> MetricsStateManager::Create(
    PrefService* local_state,
    EnabledStateProvider* enabled_state_provider,
    const std::wstring& backup_registry_key,
    const base::FilePath& user_data_dir,
    StartupVisibility startup_visibility,
    version_info::Channel channel,
    StoreClientInfoCallback store_client_info,
    LoadClientInfoCallback retrieve_client_info,
    base::StringPiece external_client_id) {
  std::unique_ptr<MetricsStateManager> result;
  // Note: |instance_exists_| is updated in the constructor and destructor.
  if (!instance_exists_) {
    result.reset(new MetricsStateManager(
        local_state, enabled_state_provider, backup_registry_key, user_data_dir,
        startup_visibility, channel,
        store_client_info.is_null() ? base::DoNothing()
                                    : std::move(store_client_info),
        retrieve_client_info.is_null()
            ? base::BindRepeating(&NoOpLoadClientInfoBackup)
            : std::move(retrieve_client_info),
        external_client_id));
  }
  return result;
}

// static
void MetricsStateManager::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kMetricsClientID, std::string());
  registry->RegisterInt64Pref(prefs::kMetricsReportingEnabledTimestamp, 0);
  registry->RegisterInt64Pref(prefs::kInstallDate, 0);

  EntropyState::RegisterPrefs(registry);
  ClonedInstallDetector::RegisterPrefs(registry);
}

void MetricsStateManager::BackUpCurrentClientInfo() {
  ClientInfo client_info;
  client_info.client_id = client_id_;
  client_info.installation_date = ReadInstallDate(local_state_);
  client_info.reporting_enabled_date = ReadEnabledDate(local_state_);
  store_client_info_.Run(client_info);
}

std::unique_ptr<ClientInfo> MetricsStateManager::LoadClientInfo() {
  // If a cloned install was detected, loading ClientInfo from backup will be
  // a race condition with clearing the backup. Skip all backup reads for this
  // session.
  if (metrics_ids_were_reset_)
    return nullptr;

  std::unique_ptr<ClientInfo> client_info = load_client_info_.Run();

  // The GUID retrieved should be valid unless retrieval failed.
  // If not, return nullptr. This will result in a new GUID being generated by
  // the calling function ForceClientIdCreation().
  if (client_info && !base::IsValidGUID(client_info->client_id))
    return nullptr;

  return client_info;
}

std::string MetricsStateManager::GetHighEntropySource() {
  // This should only be called if the |initial_client_id_| is not empty. The
  // user shouldn't be able to enable UMA between the constructor and calling
  // this, because field trial setup happens at Chrome initialization.
  DCHECK(!initial_client_id_.empty());
  return entropy_state_.GetHighEntropySource(initial_client_id_);
}

int MetricsStateManager::GetOldLowEntropySource() {
  return entropy_state_.GetOldLowEntropySource();
}

void MetricsStateManager::UpdateEntropySourceReturnedValue(
    EntropySourceType type) {
  if (entropy_source_returned_ != ENTROPY_SOURCE_NONE)
    return;

  entropy_source_returned_ = type;
  base::UmaHistogramEnumeration("UMA.EntropySourceType", type,
                                ENTROPY_SOURCE_ENUM_SIZE);
}

void MetricsStateManager::ResetMetricsIDsIfNecessary() {
  if (!ShouldResetClientIdsOnClonedInstall())
    return;
  metrics_ids_were_reset_ = true;
  previous_client_id_ = ReadClientId(local_state_);

  base::UmaHistogramBoolean("UMA.MetricsIDsReset", true);

  DCHECK(client_id_.empty());

  local_state_->ClearPref(prefs::kMetricsClientID);
  EntropyState::ClearPrefs(local_state_);

  ClonedInstallDetector::RecordClonedInstallInfo(local_state_);

  // Also clear the backed up client info. This is asynchronus; any reads
  // shortly after may retrieve the old ClientInfo from the backup.
  store_client_info_.Run(ClientInfo());
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
void MetricsStateManager::LogClientIdChanged(
    metrics::structured::NeutrinoDevicesLocation location,
    std::string previous_client_id) {
  metrics::structured::NeutrinoDevicesLogClientIdChanged(
      client_id_, previous_client_id, ReadInstallDate(local_state_),
      ReadEnabledDate(local_state_), location);
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

}  // namespace metrics