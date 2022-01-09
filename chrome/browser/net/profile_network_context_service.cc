// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/profile_network_context_service.h"

#include <string>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/check_op.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/notreached.h"
#include "base/strings/string_split.h"
#include "base/task/post_task.h"
#include "base/task/thread_pool.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_features.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/cookie_settings_factory.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/domain_reliability/service_factory.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_content_client.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/pref_names.h"
#include "components/certificate_transparency/pref_names.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/embedder_support/pref_names.h"
#include "components/embedder_support/switches.h"
#include "components/language/core/browser/language_prefs.h"
#include "components/language/core/browser/pref_names.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/shared_cors_origin_access_list.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "net/base/features.h"
#include "net/http/http_auth_preferences.h"
#include "net/http/http_util.h"
#include "net/ssl/client_cert_store.h"
#include "services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom.h"
#include "services/network/public/cpp/cors/origin_access_list.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "third_party/blink/public/common/features.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_features.h"
#include "ash/constants/ash_switches.h"
#include "chrome/browser/ash/certificate_provider/certificate_provider.h"
#include "chrome/browser/ash/certificate_provider/certificate_provider_service.h"
#include "chrome/browser/ash/certificate_provider/certificate_provider_service_factory.h"
#include "chrome/browser/ash/net/client_cert_store_ash.h"
#include "chrome/browser/ash/policy/networking/policy_cert_service.h"
#include "chrome/browser/ash/policy/networking/policy_cert_service_factory.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#endif

#if defined(USE_NSS_CERTS)
#include "chrome/browser/ui/crypto_module_delegate_nss.h"
#include "net/ssl/client_cert_store_nss.h"
#endif  // defined(USE_NSS_CERTS)

#if defined(OS_WIN)
#include "net/ssl/client_cert_store_win.h"
#endif  // defined(OS_WIN)

#if defined(OS_MAC)
#include "net/ssl/client_cert_store_mac.h"
#endif  // defined(OS_MAC)

#if BUILDFLAG(TRIAL_COMPARISON_CERT_VERIFIER_SUPPORTED)
#include "chrome/browser/net/trial_comparison_cert_verifier_controller.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/common/constants.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/lacros/cert_db_initializer_factory.h"
#include "chrome/browser/lacros/client_cert_store_lacros.h"
#endif

namespace {

bool* g_discard_domain_reliability_uploads_for_testing = nullptr;

const char kHttpCacheFinchExperimentGroups[] =
    "profile_network_context_service.http_cache_finch_experiment_groups";

std::vector<std::string> TranslateStringArray(const base::Value* list) {
  if (!list->is_list())
    return std::vector<std::string>();

  std::vector<std::string> strings;
  for (const base::Value& value : list->GetList()) {
    DCHECK(value.is_string());
    strings.push_back(value.GetString());
  }
  return strings;
}

std::string ComputeAcceptLanguageFromPref(const std::string& language_pref) {
  std::string accept_languages_str =
      net::HttpUtil::ExpandLanguageList(language_pref);
  return net::HttpUtil::GenerateAcceptLanguageHeader(accept_languages_str);
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
network::mojom::AdditionalCertificatesPtr GetAdditionalCertificates(
    const policy::PolicyCertService* policy_cert_service,
    const base::FilePath& storage_partition_path) {
  auto additional_certificates = network::mojom::AdditionalCertificates::New();
  policy_cert_service->GetPolicyCertificatesForStoragePartition(
      storage_partition_path, &(additional_certificates->all_certificates),
      &(additional_certificates->trust_anchors));
  return additional_certificates;
}
#endif  // defined (OS_CHROMEOS)

// Tests allowing ambient authentication with default credentials based on the
// profile type.
bool IsAmbientAuthAllowedForProfile(Profile* profile) {
  // Ambient authentication is always enabled for regular and system profiles.
  // System profiles (used in profile picker) may require authentication to let
  // user login.
  if (profile->IsRegularProfile() || profile->IsSystemProfile())
    return true;

  // Non-primary OTR profiles are not used to create browser windows and are
  // only technical means for a task that does not need to leave state after
  // it's completed.
  if (profile->IsOffTheRecord() && !profile->IsPrimaryOTRProfile())
    return true;

  PrefService* local_state = g_browser_process->local_state();
  DCHECK(local_state);
  DCHECK(local_state->FindPreference(
      prefs::kAmbientAuthenticationInPrivateModesEnabled));

  net::AmbientAuthAllowedProfileTypes type =
      static_cast<net::AmbientAuthAllowedProfileTypes>(local_state->GetInteger(
          prefs::kAmbientAuthenticationInPrivateModesEnabled));

  if (profile->IsGuestSession()) {
    return type == net::AmbientAuthAllowedProfileTypes::GUEST_AND_REGULAR ||
           type == net::AmbientAuthAllowedProfileTypes::ALL;
  } else if (profile->IsIncognitoProfile()) {
    return type == net::AmbientAuthAllowedProfileTypes::INCOGNITO_AND_REGULAR ||
           type == net::AmbientAuthAllowedProfileTypes::ALL;
  }

  // Profile type not yet supported.
  NOTREACHED();

  return false;
}

void UpdateCookieSettings(Profile* profile) {
  ContentSettingsForOneType settings;
  HostContentSettingsMapFactory::GetForProfile(profile)->GetSettingsForOneType(
      ContentSettingsType::COOKIES, &settings);
  profile->ForEachStoragePartition(base::BindRepeating(
      [](ContentSettingsForOneType settings,
         content::StoragePartition* storage_partition) {
        storage_partition->GetCookieManagerForBrowserProcess()
            ->SetContentSettings(settings);
      },
      settings));
}

void UpdateLegacyCookieSettings(Profile* profile) {
  ContentSettingsForOneType settings;
  HostContentSettingsMapFactory::GetForProfile(profile)->GetSettingsForOneType(
      ContentSettingsType::LEGACY_COOKIE_ACCESS, &settings);
  profile->ForEachStoragePartition(base::BindRepeating(
      [](ContentSettingsForOneType settings,
         content::StoragePartition* storage_partition) {
        storage_partition->GetCookieManagerForBrowserProcess()
            ->SetContentSettingsForLegacyCookieAccess(settings);
      },
      settings));
}

void UpdateStorageAccessSettings(Profile* profile) {
  if (base::FeatureList::IsEnabled(blink::features::kStorageAccessAPI)) {
    ContentSettingsForOneType settings;
    HostContentSettingsMapFactory::GetForProfile(profile)
        ->GetSettingsForOneType(ContentSettingsType::STORAGE_ACCESS, &settings);

    profile->ForEachStoragePartition(base::BindRepeating(
        [](ContentSettingsForOneType settings,
           content::StoragePartition* storage_partition) {
          storage_partition->GetCookieManagerForBrowserProcess()
              ->SetStorageAccessGrantSettings(settings, base::DoNothing());
        },
        settings));
  }
}

}  // namespace

ProfileNetworkContextService::ProfileNetworkContextService(Profile* profile)
    : profile_(profile), proxy_config_monitor_(profile) {
  PrefService* profile_prefs = profile->GetPrefs();
  quic_allowed_.Init(prefs::kQuicAllowed, profile_prefs,
                     base::BindRepeating(
                         &ProfileNetworkContextService::DisableQuicIfNotAllowed,
                         base::Unretained(this)));
  pref_accept_language_.Init(
      language::prefs::kAcceptLanguages, profile_prefs,
      base::BindRepeating(&ProfileNetworkContextService::UpdateAcceptLanguage,
                          base::Unretained(this)));
  enable_referrers_.Init(
      prefs::kEnableReferrers, profile_prefs,
      base::BindRepeating(&ProfileNetworkContextService::UpdateReferrersEnabled,
                          base::Unretained(this)));
  cookie_settings_ = CookieSettingsFactory::GetForProfile(profile);
  cookie_settings_observation_.Observe(cookie_settings_.get());

  DisableQuicIfNotAllowed();

  // Observe content settings so they can be synced to the network service.
  HostContentSettingsMapFactory::GetForProfile(profile_)->AddObserver(this);

  pref_change_registrar_.Init(profile_prefs);

  // When any of the following CT preferences change, we schedule an update
  // to aggregate the actual update using a |ct_policy_update_timer_|.
  pref_change_registrar_.Add(
      certificate_transparency::prefs::kCTRequiredHosts,
      base::BindRepeating(&ProfileNetworkContextService::ScheduleUpdateCTPolicy,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      certificate_transparency::prefs::kCTExcludedHosts,
      base::BindRepeating(&ProfileNetworkContextService::ScheduleUpdateCTPolicy,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      certificate_transparency::prefs::kCTExcludedSPKIs,
      base::BindRepeating(&ProfileNetworkContextService::ScheduleUpdateCTPolicy,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      certificate_transparency::prefs::kCTExcludedLegacySPKIs,
      base::BindRepeating(&ProfileNetworkContextService::ScheduleUpdateCTPolicy,
                          base::Unretained(this)));

  pref_change_registrar_.Add(
      prefs::kGloballyScopeHTTPAuthCacheEnabled,
      base::BindRepeating(&ProfileNetworkContextService::
                              UpdateSplitAuthCacheByNetworkIsolationKey,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kCorsNonWildcardRequestHeadersSupport,
      base::BindRepeating(&ProfileNetworkContextService::
                              UpdateCorsNonWildcardRequestHeadersSupport,
                          base::Unretained(this)));
}

ProfileNetworkContextService::~ProfileNetworkContextService() = default;

void ProfileNetworkContextService::ConfigureNetworkContextParams(
    bool in_memory,
    const base::FilePath& relative_partition_path,
    network::mojom::NetworkContextParams* network_context_params,
    cert_verifier::mojom::CertVerifierCreationParams*
        cert_verifier_creation_params) {
  ConfigureNetworkContextParamsInternal(in_memory, relative_partition_path,
                                        network_context_params,
                                        cert_verifier_creation_params);

  if ((!in_memory && !profile_->IsOffTheRecord())) {
    // TODO(jam): delete this code 1 year after Network Service shipped to all
    // stable users, which would be after M83 branches.
    base::FilePath base_cache_path;
    chrome::GetUserCacheDirectory(GetPartitionPath(relative_partition_path),
                                  &base_cache_path);
    base::FilePath media_cache_path =
        base_cache_path.Append(chrome::kMediaCacheDirname);
    base::ThreadPool::PostTask(
        FROM_HERE,
        {base::TaskPriority::BEST_EFFORT, base::MayBlock(),
         base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
        base::BindOnce(base::GetDeletePathRecursivelyCallback(),
                       media_cache_path));
  }
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
void ProfileNetworkContextService::UpdateAdditionalCertificates() {
  const policy::PolicyCertService* policy_cert_service =
      policy::PolicyCertServiceFactory::GetForProfile(profile_);
  if (!policy_cert_service)
    return;
  profile_->ForEachStoragePartition(base::BindRepeating(
      [](const policy::PolicyCertService* policy_cert_service,
         content::StoragePartition* storage_partition) {
        auto additional_certificates = GetAdditionalCertificates(
            policy_cert_service, storage_partition->GetPath());
        storage_partition->GetNetworkContext()->UpdateAdditionalCertificates(
            std::move(additional_certificates));
      },
      policy_cert_service));
}
#endif

void ProfileNetworkContextService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      embedder_support::kAlternateErrorPagesEnabled, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(prefs::kQuicAllowed, true);
  registry->RegisterBooleanPref(prefs::kGloballyScopeHTTPAuthCacheEnabled,
                                false);
}

// static
void ProfileNetworkContextService::RegisterLocalStatePrefs(
    PrefRegistrySimple* registry) {
  registry->RegisterListPref(prefs::kHSTSPolicyBypassList);
  registry->RegisterIntegerPref(
      prefs::kAmbientAuthenticationInPrivateModesEnabled,
      static_cast<int>(net::AmbientAuthAllowedProfileTypes::REGULAR_ONLY));

  // For information about whether to reset the HTTP Cache or not, defaults
  // to the empty string, which does not prompt a reset.
  registry->RegisterStringPref(kHttpCacheFinchExperimentGroups, "");
}

void ProfileNetworkContextService::DisableQuicIfNotAllowed() {
  if (!quic_allowed_.IsManaged())
    return;

  // If QUIC is allowed, do nothing (re-enabling QUIC is not supported).
  if (quic_allowed_.GetValue())
    return;

  g_browser_process->system_network_context_manager()->DisableQuic();
}

void ProfileNetworkContextService::UpdateAcceptLanguage() {
  profile_->ForEachStoragePartition(base::BindRepeating(
      [](const std::string& accept_language,
         content::StoragePartition* storage_partition) {
        storage_partition->GetNetworkContext()->SetAcceptLanguage(
            accept_language);
      },
      ComputeAcceptLanguage()));
}

void ProfileNetworkContextService::OnThirdPartyCookieBlockingChanged(
    bool block_third_party_cookies) {
  profile_->ForEachStoragePartition(base::BindRepeating(
      [](bool block_third_party_cookies,
         content::StoragePartition* storage_partition) {
        storage_partition->GetCookieManagerForBrowserProcess()
            ->BlockThirdPartyCookies(block_third_party_cookies);
      },
      block_third_party_cookies));
}

std::string ProfileNetworkContextService::ComputeAcceptLanguage() const {
  if (profile_->IsOffTheRecord()) {
    // In incognito mode return only the first language.
    return ComputeAcceptLanguageFromPref(
        language::GetFirstLanguage(pref_accept_language_.GetValue()));
  }
  return ComputeAcceptLanguageFromPref(pref_accept_language_.GetValue());
}

void ProfileNetworkContextService::UpdateReferrersEnabled() {
  profile_->ForEachStoragePartition(base::BindRepeating(
      [](bool enable_referrers, content::StoragePartition* storage_partition) {
        storage_partition->GetNetworkContext()->SetEnableReferrers(
            enable_referrers);
      },
      enable_referrers_.GetValue()));
}

network::mojom::CTPolicyPtr ProfileNetworkContextService::GetCTPolicy() {
  auto* prefs = profile_->GetPrefs();
  const base::Value* ct_required =
      prefs->GetList(certificate_transparency::prefs::kCTRequiredHosts);
  const base::Value* ct_excluded =
      prefs->GetList(certificate_transparency::prefs::kCTExcludedHosts);
  const base::Value* ct_excluded_spkis =
      prefs->GetList(certificate_transparency::prefs::kCTExcludedSPKIs);
  const base::Value* ct_excluded_legacy_spkis =
      prefs->GetList(certificate_transparency::prefs::kCTExcludedLegacySPKIs);

  std::vector<std::string> required(TranslateStringArray(ct_required));
  std::vector<std::string> excluded(TranslateStringArray(ct_excluded));
  std::vector<std::string> excluded_spkis(
      TranslateStringArray(ct_excluded_spkis));
  std::vector<std::string> excluded_legacy_spkis(
      TranslateStringArray(ct_excluded_legacy_spkis));

  return network::mojom::CTPolicy::New(std::move(required), std::move(excluded),
                                       std::move(excluded_spkis),
                                       std::move(excluded_legacy_spkis));
}

void ProfileNetworkContextService::UpdateCTPolicyForContexts(
    const std::vector<network::mojom::NetworkContext*>& contexts) {
  for (auto* context : contexts) {
    context->SetCTPolicy(GetCTPolicy());
  }
}

void ProfileNetworkContextService::UpdateCTPolicy() {
  std::vector<network::mojom::NetworkContext*> contexts;
  profile_->ForEachStoragePartition(base::BindRepeating(
      [](std::vector<network::mojom::NetworkContext*>* contexts_ptr,
         content::StoragePartition* storage_partition) {
        contexts_ptr->push_back(storage_partition->GetNetworkContext());
      },
      &contexts));

  UpdateCTPolicyForContexts(contexts);
}

void ProfileNetworkContextService::ScheduleUpdateCTPolicy() {
  ct_policy_update_timer_.Start(FROM_HERE, base::Seconds(0), this,
                                &ProfileNetworkContextService::UpdateCTPolicy);
}

bool ProfileNetworkContextService::ShouldSplitAuthCacheByNetworkIsolationKey()
    const {
  if (profile_->GetPrefs()->GetBoolean(
          prefs::kGloballyScopeHTTPAuthCacheEnabled))
    return false;
  return base::FeatureList::IsEnabled(
      network::features::kSplitAuthCacheByNetworkIsolationKey);
}

void ProfileNetworkContextService::UpdateSplitAuthCacheByNetworkIsolationKey() {
  bool split_auth_cache_by_network_isolation_key =
      ShouldSplitAuthCacheByNetworkIsolationKey();

  profile_->ForEachStoragePartition(base::BindRepeating(
      [](bool split_auth_cache_by_network_isolation_key,
         content::StoragePartition* storage_partition) {
        storage_partition->GetNetworkContext()
            ->SetSplitAuthCacheByNetworkIsolationKey(
                split_auth_cache_by_network_isolation_key);
      },
      split_auth_cache_by_network_isolation_key));
}

void ProfileNetworkContextService::
    UpdateCorsNonWildcardRequestHeadersSupport() {
  const bool value = profile_->GetPrefs()->GetBoolean(
      prefs::kCorsNonWildcardRequestHeadersSupport);

  profile_->ForEachStoragePartition(base::BindRepeating(
      [](bool value, content::StoragePartition* storage_partition) {
        storage_partition->GetNetworkContext()
            ->SetCorsNonWildcardRequestHeadersSupport(value);
      },
      value));
}

// static
network::mojom::CookieManagerParamsPtr
ProfileNetworkContextService::CreateCookieManagerParams(
    Profile* profile,
    const content_settings::CookieSettings& cookie_settings) {
  auto out = network::mojom::CookieManagerParams::New();
  out->block_third_party_cookies =
      cookie_settings.ShouldBlockThirdPartyCookies();
  // This allows cookies to be sent on https requests from chrome:// pages,
  // ignoring SameSite attribute rules. For example, this is needed for browser
  // UI to interact with SameSite cookies on accounts.google.com, which are used
  // for logging into Cloud Print from chrome://print, for displaying a list
  // of available accounts on the NTP (chrome://new-tab-page), etc.
  out->secure_origin_cookies_allowed_schemes.push_back(
      content::kChromeUIScheme);
#if BUILDFLAG(ENABLE_EXTENSIONS)
  // TODO(chlily): To be consistent with the content_settings version of
  // CookieSettings, we should probably also add kExtensionScheme to the list of
  // matching_scheme_cookies_allowed_schemes.
  out->third_party_cookies_allowed_schemes.push_back(
      extensions::kExtensionScheme);
#endif

  ContentSettingsForOneType settings;
  HostContentSettingsMap* host_content_settings_map =
      HostContentSettingsMapFactory::GetForProfile(profile);
  host_content_settings_map->GetSettingsForOneType(ContentSettingsType::COOKIES,
                                                   &settings);
  out->settings = std::move(settings);

  ContentSettingsForOneType settings_for_legacy_cookie_access;
  host_content_settings_map->GetSettingsForOneType(
      ContentSettingsType::LEGACY_COOKIE_ACCESS,
      &settings_for_legacy_cookie_access);
  out->settings_for_legacy_cookie_access =
      std::move(settings_for_legacy_cookie_access);

  ContentSettingsForOneType settings_for_storage_access;
  if (base::FeatureList::IsEnabled(blink::features::kStorageAccessAPI)) {
    host_content_settings_map->GetSettingsForOneType(
        ContentSettingsType::STORAGE_ACCESS, &settings_for_storage_access);
  }
  out->settings_for_storage_access = std::move(settings_for_storage_access);

  out->cookie_access_delegate_type =
      network::mojom::CookieAccessDelegateType::USE_CONTENT_SETTINGS;
  return out;
}

void ProfileNetworkContextService::FlushProxyConfigMonitorForTesting() {
  proxy_config_monitor_.FlushForTesting();
}

void ProfileNetworkContextService::SetDiscardDomainReliabilityUploadsForTesting(
    bool value) {
  g_discard_domain_reliability_uploads_for_testing = new bool(value);
}

std::unique_ptr<net::ClientCertStore>
ProfileNetworkContextService::CreateClientCertStore() {
  if (!client_cert_store_factory_.is_null())
    return client_cert_store_factory_.Run();

#if BUILDFLAG(IS_CHROMEOS_ASH)
  bool use_system_key_slot = false;
  // Enable client certificates for the Chrome OS sign-in frame, if this feature
  // is not disabled by a flag.
  // Note that while this applies to the whole sign-in profile / lock screen
  // profile, client certificates will only be selected for the StoragePartition
  // currently used in the sign-in frame (see SigninPartitionManager).
  if (chromeos::switches::IsSigninFrameClientCertsEnabled() &&
      (chromeos::ProfileHelper::IsSigninProfile(profile_) ||
       chromeos::ProfileHelper::IsLockScreenProfile(profile_))) {
    use_system_key_slot = true;
  }

  std::string username_hash;
  const user_manager::User* user =
      chromeos::ProfileHelper::Get()->GetUserByProfile(profile_);
  if (user && !user->username_hash().empty()) {
    username_hash = user->username_hash();

    // Use the device-wide system key slot only if the user is affiliated on
    // the device.
    if (user->IsAffiliated()) {
      use_system_key_slot = true;
    }
  }

  chromeos::CertificateProviderService* cert_provider_service =
      chromeos::CertificateProviderServiceFactory::GetForBrowserContext(
          profile_);
  std::unique_ptr<chromeos::CertificateProvider> certificate_provider;
  if (cert_provider_service) {
    certificate_provider = cert_provider_service->CreateCertificateProvider();
  }

  // `ClientCertStoreAsh` internally depends on NSS initialization that happens
  // when the `ResourceContext` is created. Call `GetResourceContext()` so the
  // dependency is explicit. See https://crbug.com/1018972.
  profile_->GetResourceContext();

  return std::make_unique<chromeos::ClientCertStoreAsh>(
      std::move(certificate_provider), use_system_key_slot, username_hash,
      base::BindRepeating(&CreateCryptoModuleBlockingPasswordDelegate,
                          kCryptoModulePasswordClientAuth));
#elif defined(USE_NSS_CERTS)
  std::unique_ptr<net::ClientCertStore> store =
      std::make_unique<net::ClientCertStoreNSS>(
          base::BindRepeating(&CreateCryptoModuleBlockingPasswordDelegate,
                              kCryptoModulePasswordClientAuth));
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  if (!profile_->IsMainProfile()) {
    // TODO(crbug.com/1148298): return some cert store for secondary profiles in
    // Lacros-Chrome.
    return nullptr;
  }

  CertDbInitializer* cert_db_initializer =
      CertDbInitializerFactory::GetForBrowserContext(profile_);
  store = std::make_unique<ClientCertStoreLacros>(cert_db_initializer,
                                                  std::move(store));
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

  return store;
#elif defined(OS_WIN)
  return std::make_unique<net::ClientCertStoreWin>();
#elif defined(OS_MAC)
  return std::make_unique<net::ClientCertStoreMac>();
#elif defined(OS_ANDROID)
  // Android does not use the ClientCertStore infrastructure. On Android client
  // cert matching is done by the OS as part of the call to show the cert
  // selection dialog.
  return nullptr;
#elif defined(OS_FUCHSIA)
  // TODO(crbug.com/1235293)
  NOTIMPLEMENTED_LOG_ONCE();
  return nullptr;
#else
#error Unknown platform.
#endif
}

bool GetHttpCacheBackendResetParam(PrefService* local_state) {
  // Get the field trial groups.  If the server cannot be reached, then
  // this corresponds to "None" for each experiment.
  base::FieldTrial* field_trial = base::FeatureList::GetFieldTrial(
      net::features::kSplitCacheByNetworkIsolationKey);
  std::string current_field_trial_status =
      (field_trial ? field_trial->group_name() : "None");
  // This used to be used for keying on main frame only vs main frame +
  // innermost frame, but the feature was removed, and now it's always keyed on
  // both.
  current_field_trial_status += " None";
  // This used to be for keying on scheme + eTLD+1 vs origin, but the trial was
  // removed, and now it's always keyed on eTLD+1. Still keeping a third "None"
  // to avoid resetting the disk cache.
  current_field_trial_status += " None ";

  field_trial = base::FeatureList::GetFieldTrial(
      net::features::kSplitCacheByIncludeCredentials);
  current_field_trial_status +=
      (field_trial ? field_trial->group_name() : "None");

  std::string previous_field_trial_status =
      local_state->GetString(kHttpCacheFinchExperimentGroups);
  local_state->SetString(kHttpCacheFinchExperimentGroups,
                         current_field_trial_status);

  return !previous_field_trial_status.empty() &&
         current_field_trial_status != previous_field_trial_status;
}

void ProfileNetworkContextService::ConfigureNetworkContextParamsInternal(
    bool in_memory,
    const base::FilePath& relative_partition_path,
    network::mojom::NetworkContextParams* network_context_params,
    cert_verifier::mojom::CertVerifierCreationParams*
        cert_verifier_creation_params) {
  if (profile_->IsOffTheRecord())
    in_memory = true;
  base::FilePath path(GetPartitionPath(relative_partition_path));

  g_browser_process->system_network_context_manager()
      ->ConfigureDefaultNetworkContextParams(network_context_params,
                                             cert_verifier_creation_params);

  network_context_params->accept_language = ComputeAcceptLanguage();
  network_context_params->enable_referrers = enable_referrers_.GetValue();

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(embedder_support::kShortReportingDelay)) {
    network_context_params->reporting_delivery_interval =
        base::Milliseconds(100);
  }

  // Always enable the HTTP cache.
  network_context_params->http_cache_enabled = true;

  network_context_params->http_auth_static_network_context_params =
      network::mojom::HttpAuthStaticNetworkContextParams::New();

  if (IsAmbientAuthAllowedForProfile(profile_)) {
    network_context_params->http_auth_static_network_context_params
        ->allow_default_credentials =
        net::HttpAuthPreferences::ALLOW_DEFAULT_CREDENTIALS;
  } else {
    network_context_params->http_auth_static_network_context_params
        ->allow_default_credentials =
        net::HttpAuthPreferences::DISALLOW_DEFAULT_CREDENTIALS;
  }

  network_context_params->cookie_manager_params =
      CreateCookieManagerParams(profile_, *cookie_settings_);

  // Configure on-disk storage for non-OTR profiles. OTR profiles just use
  // default behavior (in memory storage, default sizes).
  if (!in_memory) {
    PrefService* local_state = g_browser_process->local_state();
    // Configure the HTTP cache path and size.
    base::FilePath base_cache_path;
    chrome::GetUserCacheDirectory(path, &base_cache_path);
    base::FilePath disk_cache_dir =
        local_state->GetFilePath(prefs::kDiskCacheDir);
    if (!disk_cache_dir.empty())
      base_cache_path = disk_cache_dir.Append(base_cache_path.BaseName());
    base::FilePath http_cache_path =
        base_cache_path.Append(chrome::kCacheDirname);
    if (base::FeatureList::IsEnabled(features::kDisableHttpDiskCache)) {
      // Clear any existing on-disk cache first since if the user tries to
      // remove the cache it would only affect the in-memory cache while in the
      // experiment.
      base::ThreadPool::PostTask(
          FROM_HERE,
          {base::TaskPriority::BEST_EFFORT, base::MayBlock(),
           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
          base::BindOnce(base::GetDeletePathRecursivelyCallback(),
                         http_cache_path));
      network_context_params->http_cache_max_size =
          features::kDisableHttpDiskCacheMemoryCacheSizeParam.Get();
    } else {
      network_context_params->http_cache_path = http_cache_path;
      network_context_params->http_cache_max_size =
          local_state->GetInteger(prefs::kDiskCacheSize);
    }

    network_context_params->file_paths =
        ::network::mojom::NetworkContextFilePaths::New();

    network_context_params->file_paths->data_path =
        path.Append(chrome::kNetworkDataDirname);
    network_context_params->file_paths->unsandboxed_data_path = path;
    network_context_params->file_paths->trigger_migration =
        base::FeatureList::IsEnabled(features::kTriggerNetworkDataMigration);
    // Currently this just contains HttpServerProperties, but that will likely
    // change.
    network_context_params->file_paths->http_server_properties_file_name =
        base::FilePath(chrome::kNetworkPersistentStateFilename);
    network_context_params->file_paths->cookie_database_name =
        base::FilePath(chrome::kCookieFilename);
    network_context_params->file_paths->trust_token_database_name =
        base::FilePath(chrome::kTrustTokenFilename);

#if BUILDFLAG(ENABLE_REPORTING)
    network_context_params->file_paths->reporting_and_nel_store_database_name =
        base::FilePath(chrome::kReportingAndNelStoreFilename);
#endif  // BUILDFLAG(ENABLE_REPORTING)

    if (relative_partition_path.empty()) {  // This is the main partition.
      network_context_params->restore_old_session_cookies =
          profile_->ShouldRestoreOldSessionCookies();
      network_context_params->persist_session_cookies =
          profile_->ShouldPersistSessionCookies();
    } else {
      // Copy behavior of ProfileImplIOData::InitializeAppRequestContext.
      network_context_params->restore_old_session_cookies = false;
      network_context_params->persist_session_cookies = false;
    }

    network_context_params->file_paths->transport_security_persister_file_name =
        base::FilePath(chrome::kTransportSecurityPersisterFilename);
    network_context_params->file_paths->sct_auditing_pending_reports_file_name =
        base::FilePath(chrome::kSCTAuditingPendingReportsFileName);
  }
  const base::Value* hsts_policy_bypass_list =
      g_browser_process->local_state()->GetList(prefs::kHSTSPolicyBypassList);
  for (const auto& value : hsts_policy_bypass_list->GetList()) {
    const std::string* string_value = value.GetIfString();
    if (!string_value)
      continue;
    network_context_params->hsts_policy_bypass_list.push_back(*string_value);
  }

  proxy_config_monitor_.AddToNetworkContextParams(network_context_params);

  network_context_params->enable_certificate_reporting = true;
  network_context_params->enable_expect_ct_reporting = true;

  // Initialize the network context to do SCT auditing only if the current
  // profile is opted in to Safe Browsing Extended Reporting.
  if (!profile_->IsOffTheRecord() &&
      safe_browsing::IsExtendedReportingEnabled(*profile_->GetPrefs())) {
    network_context_params->enable_sct_auditing = true;
  }

  network_context_params->ct_policy = GetCTPolicy();

#if BUILDFLAG(TRIAL_COMPARISON_CERT_VERIFIER_SUPPORTED)
  // In order for the TrialComparisonCertVerifier to be useful, it needs to
  // provide comparisons between two well-defined verifier configurations; this
  // means the currently launched cert verifier (and root store) and the
  // prospective cert verifier (and root store).
  //
  // It's possible that, due to user configuration, such as enterprise policies,
  // the user may be requesting a non-standard configuration from the current
  // default. In these cases, the trial verifier is also disabled,
  // because all users in the trial should be running in the same configuration.
  //
  // To avoid any potential ambiguities between different layers of the network
  // stack, running the trial requires the `cert_verifier_creation_params` be
  // explicitly initialized, rather than using `kDefault` / `kRootDefault`, to
  // guarantee that the primary verifier is initialized as requested and
  // expected.  These checks here simply ensure that the caller explicitly
  // provided the expected default value.
  DCHECK(cert_verifier_creation_params);
  bool is_trial_comparison_supported = !in_memory;
#if BUILDFLAG(BUILTIN_CERT_VERIFIER_FEATURE_SUPPORTED)
  DCHECK_NE(cert_verifier_creation_params->use_builtin_cert_verifier,
            cert_verifier::mojom::CertVerifierCreationParams::CertVerifierImpl::
                kDefault);
  is_trial_comparison_supported &=
      cert_verifier_creation_params->use_builtin_cert_verifier ==
      cert_verifier::mojom::CertVerifierCreationParams::CertVerifierImpl::
          kSystem;
#endif
#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
  DCHECK_NE(cert_verifier_creation_params->use_chrome_root_store,
            cert_verifier::mojom::CertVerifierCreationParams::ChromeRootImpl::
                kRootDefault);
  is_trial_comparison_supported &=
      cert_verifier_creation_params->use_chrome_root_store ==
      cert_verifier::mojom::CertVerifierCreationParams::ChromeRootImpl::
          kRootSystem;
#endif
  if (is_trial_comparison_supported &&
      TrialComparisonCertVerifierController::MaybeAllowedForProfile(profile_)) {
    mojo::PendingRemote<
        cert_verifier::mojom::TrialComparisonCertVerifierConfigClient>
        config_client;
    auto config_client_receiver =
        config_client.InitWithNewPipeAndPassReceiver();

    cert_verifier_creation_params->trial_comparison_cert_verifier_params =
        cert_verifier::mojom::TrialComparisonCertVerifierParams::New();

    if (!trial_comparison_cert_verifier_controller_) {
      trial_comparison_cert_verifier_controller_ =
          std::make_unique<TrialComparisonCertVerifierController>(profile_);
    }
    trial_comparison_cert_verifier_controller_->AddClient(
        std::move(config_client),
        cert_verifier_creation_params->trial_comparison_cert_verifier_params
            ->report_client.InitWithNewPipeAndPassReceiver());
    cert_verifier_creation_params->trial_comparison_cert_verifier_params
        ->initial_allowed =
        trial_comparison_cert_verifier_controller_->IsAllowed();
    cert_verifier_creation_params->trial_comparison_cert_verifier_params
        ->config_client_receiver = std::move(config_client_receiver);
  }
#endif

  if (domain_reliability::DomainReliabilityServiceFactory::
          ShouldCreateService()) {
    network_context_params->enable_domain_reliability = true;
    network_context_params->domain_reliability_upload_reporter =
        domain_reliability::DomainReliabilityServiceFactory::
            kUploadReporterString;
    network_context_params->discard_domain_reliablity_uploads =
        g_discard_domain_reliability_uploads_for_testing
            ? *g_discard_domain_reliability_uploads_for_testing
            : !g_browser_process->local_state()->GetBoolean(
                  metrics::prefs::kMetricsReportingEnabled);
  }

#if BUILDFLAG(IS_CHROMEOS_ASH)
  bool profile_supports_policy_certs = false;
  if (chromeos::ProfileHelper::IsSigninProfile(profile_))
    profile_supports_policy_certs = true;
  user_manager::UserManager* user_manager = user_manager::UserManager::Get();
  if (user_manager) {
    const user_manager::User* user =
        chromeos::ProfileHelper::Get()->GetUserByProfile(profile_);
    // No need to initialize NSS for users with empty username hash:
    // Getters for a user's NSS slots always return NULL slot if the user's
    // username hash is empty, even when the NSS is not initialized for the
    // user.
    if (user && !user->username_hash().empty()) {
      cert_verifier_creation_params->username_hash = user->username_hash();
      cert_verifier_creation_params->nss_path = profile_->GetPath();
      profile_supports_policy_certs = true;
    }
  }
  if (profile_supports_policy_certs &&
      policy::PolicyCertServiceFactory::CreateAndStartObservingForProfile(
          profile_)) {
    const policy::PolicyCertService* policy_cert_service =
        policy::PolicyCertServiceFactory::GetForProfile(profile_);
    network_context_params->initial_additional_certificates =
        GetAdditionalCertificates(policy_cert_service,
                                  GetPartitionPath(relative_partition_path));
  }
  // Disable idle sockets close on memory pressure if configured by finch or
  // about://flags.
  if (base::FeatureList::IsEnabled(
          chromeos::features::kDisableIdleSocketsCloseOnMemoryPressure)) {
    network_context_params->disable_idle_sockets_close_on_memory_pressure =
        true;
  }
#endif

  network_context_params->reset_http_cache_backend =
      GetHttpCacheBackendResetParam(g_browser_process->local_state());

  network_context_params->split_auth_cache_by_network_isolation_key =
      ShouldSplitAuthCacheByNetworkIsolationKey();

  // All consumers of the main NetworkContext must provide NetworkIsolationKeys
  // / IsolationInfos, so storage can be isolated on a per-site basis.
  network_context_params->require_network_isolation_key = true;
}

base::FilePath ProfileNetworkContextService::GetPartitionPath(
    const base::FilePath& relative_partition_path) {
  base::FilePath path = profile_->GetPath();
  if (!relative_partition_path.empty())
    path = path.Append(relative_partition_path);
  return path;
}

void ProfileNetworkContextService::OnContentSettingChanged(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type) {
  switch (content_type) {
    case ContentSettingsType::COOKIES:
      UpdateCookieSettings(profile_);
      break;
    case ContentSettingsType::LEGACY_COOKIE_ACCESS:
      UpdateLegacyCookieSettings(profile_);
      break;
    case ContentSettingsType::STORAGE_ACCESS:
      UpdateStorageAccessSettings(profile_);
      break;
    case ContentSettingsType::DEFAULT:
      UpdateCookieSettings(profile_);
      UpdateLegacyCookieSettings(profile_);
      UpdateStorageAccessSettings(profile_);
      break;
    default:
      return;
  }
}
