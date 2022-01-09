// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/profile_policy_connector.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_switcher/browser_switcher_policy_migrator.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/cloud/cloud_policy_core.h"
#include "components/policy/core/common/cloud/cloud_policy_manager.h"
#include "components/policy/core/common/cloud/cloud_policy_store.h"
#include "components/policy/core/common/cloud/machine_level_user_cloud_policy_manager.h"
#include "components/policy/core/common/configuration_policy_provider.h"
#include "components/policy/core/common/legacy_chrome_policy_migrator.h"
#include "components/policy/core/common/policy_bundle.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/policy_namespace.h"
#include "components/policy/core/common/policy_service_impl.h"
#include "components/policy/core/common/proxy_policy_provider.h"
#include "components/policy/core/common/schema_registry_tracking_policy_provider.h"
#include "components/policy/policy_constants.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chrome/browser/ash/policy/active_directory/active_directory_policy_manager.h"
#include "chrome/browser/ash/policy/core/browser_policy_connector_ash.h"
#include "chrome/browser/ash/policy/core/device_cloud_policy_manager_ash.h"
#include "chrome/browser/ash/policy/core/device_local_account.h"
#include "chrome/browser/ash/policy/core/device_local_account_policy_provider.h"
#include "chrome/browser/ash/policy/login/login_profile_policy_provider.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "components/policy/core/common/proxy_policy_provider.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/profiles/profile_manager.h"
#include "components/user_manager/user_manager.h"
#endif

namespace policy {

#if BUILDFLAG(IS_CHROMEOS_ASH)
namespace internal {

// This class allows observing a |device_wide_policy_service| for policy updates
// during which the |source_policy_provider| has already been initialized.
// It is used to know when propagation of primary user policies proxied to the
// device-wide PolicyService has finished.
class ProxiedPoliciesPropagatedWatcher : PolicyService::ProviderUpdateObserver {
 public:
  ProxiedPoliciesPropagatedWatcher(
      PolicyService* device_wide_policy_service,
      ProxyPolicyProvider* proxy_policy_provider,
      ConfigurationPolicyProvider* source_policy_provider,
      base::OnceClosure proxied_policies_propagated_callback)
      : device_wide_policy_service_(device_wide_policy_service),
        proxy_policy_provider_(proxy_policy_provider),
        source_policy_provider_(source_policy_provider),
        proxied_policies_propagated_callback_(
            std::move(proxied_policies_propagated_callback)) {
    device_wide_policy_service->AddProviderUpdateObserver(this);

    timeout_timer_.Start(
        FROM_HERE, base::Seconds(kProxiedPoliciesPropagationTimeoutInSeconds),
        this,
        &ProxiedPoliciesPropagatedWatcher::OnProviderUpdatePropagationTimedOut);
  }

  ProxiedPoliciesPropagatedWatcher(const ProxiedPoliciesPropagatedWatcher&) =
      delete;
  ProxiedPoliciesPropagatedWatcher& operator=(
      const ProxiedPoliciesPropagatedWatcher&) = delete;
  ~ProxiedPoliciesPropagatedWatcher() override {
    device_wide_policy_service_->RemoveProviderUpdateObserver(this);
  }

  // PolicyService::Observer:
  void OnProviderUpdatePropagated(
      ConfigurationPolicyProvider* provider) override {
    if (!proxied_policies_propagated_callback_)
      return;
    if (provider != proxy_policy_provider_)
      return;

    if (!source_policy_provider_->IsInitializationComplete(
            POLICY_DOMAIN_CHROME)) {
      return;
    }

    std::move(proxied_policies_propagated_callback_).Run();
  }

  void OnProviderUpdatePropagationTimedOut() {
    if (!proxied_policies_propagated_callback_)
      return;
    LOG(WARNING) << "Waiting for proxied policies to propagate timed out.";
    std::move(proxied_policies_propagated_callback_).Run();
  }

 private:
  static constexpr int kProxiedPoliciesPropagationTimeoutInSeconds = 5;

  PolicyService* const device_wide_policy_service_;
  const ProxyPolicyProvider* const proxy_policy_provider_;
  const ConfigurationPolicyProvider* const source_policy_provider_;
  base::OnceClosure proxied_policies_propagated_callback_;
  base::OneShotTimer timeout_timer_;
};

}  // namespace internal

namespace {
// Returns the PolicyService that holds device-wide policies.
PolicyService* GetDeviceWidePolicyService() {
  BrowserPolicyConnectorAsh* browser_policy_connector =
      g_browser_process->platform_part()->browser_policy_connector_ash();
  return browser_policy_connector->GetPolicyService();
}

// Returns the ProxyPolicyProvider which is used to forward primary Profile
// policies into the device-wide PolicyService.
ProxyPolicyProvider* GetProxyPolicyProvider() {
  BrowserPolicyConnectorAsh* browser_policy_connector =
      g_browser_process->platform_part()->browser_policy_connector_ash();
  return browser_policy_connector->GetGlobalUserCloudPolicyProvider();
}
}  // namespace

#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

ProfilePolicyConnector::ProfilePolicyConnector() = default;

ProfilePolicyConnector::~ProfilePolicyConnector() = default;

void ProfilePolicyConnector::Init(
    const user_manager::User* user,
    SchemaRegistry* schema_registry,
    ConfigurationPolicyProvider* configuration_policy_provider,
    const CloudPolicyStore* policy_store,
    policy::ChromeBrowserPolicyConnector* connector,
    bool force_immediate_load) {
  configuration_policy_provider_ = configuration_policy_provider;
  policy_store_ = policy_store;

#if BUILDFLAG(IS_CHROMEOS_ASH)
  auto* browser_policy_connector =
      static_cast<BrowserPolicyConnectorAsh*>(connector);
#else
  DCHECK_EQ(nullptr, user);
#endif

#if BUILDFLAG(IS_CHROMEOS_LACROS)
  browser_policy_connector_ = connector;
#endif

  ConfigurationPolicyProvider* platform_provider =
      connector->GetPlatformProvider();
  if (platform_provider) {
    AppendPolicyProviderWithSchemaTracking(platform_provider, schema_registry);
  }

#if BUILDFLAG(IS_CHROMEOS_ASH)
  if (browser_policy_connector->GetDeviceCloudPolicyManager()) {
    policy_providers_.push_back(
        browser_policy_connector->GetDeviceCloudPolicyManager());
  }
  if (browser_policy_connector->GetDeviceActiveDirectoryPolicyManager()) {
    policy_providers_.push_back(
        browser_policy_connector->GetDeviceActiveDirectoryPolicyManager());
  }
#else
  ConfigurationPolicyProvider* machine_level_user_cloud_policy_provider =
      connector->proxy_policy_provider();
  if (machine_level_user_cloud_policy_provider) {
    AppendPolicyProviderWithSchemaTracking(
        machine_level_user_cloud_policy_provider, schema_registry);
  }

  if (connector->command_line_policy_provider())
    policy_providers_.push_back(connector->command_line_policy_provider());
#endif

  if (configuration_policy_provider)
    policy_providers_.push_back(configuration_policy_provider);

#if BUILDFLAG(IS_CHROMEOS_ASH)
  if (!user) {
    DCHECK(schema_registry);
    // This case occurs for the signin and the lock screen app profiles.
    special_user_policy_provider_ =
        std::make_unique<LoginProfilePolicyProvider>(
            browser_policy_connector->GetPolicyService());
  } else {
    // |user| should never be nullptr except for the signin and the lock screen
    // app profile.
    is_primary_user_ =
        user == user_manager::UserManager::Get()->GetPrimaryUser();
    // Note that |DeviceLocalAccountPolicyProvider::Create| returns nullptr when
    // the user supplied is not a device-local account user.
    special_user_policy_provider_ = DeviceLocalAccountPolicyProvider::Create(
        user->GetAccountId().GetUserEmail(),
        browser_policy_connector->GetDeviceLocalAccountPolicyService(),
        force_immediate_load);
  }
  if (special_user_policy_provider_) {
    special_user_policy_provider_->Init(schema_registry);
    policy_providers_.push_back(special_user_policy_provider_.get());
  }
#endif

  std::vector<std::unique_ptr<PolicyMigrator>> migrators;
#if defined(OS_WIN)
  migrators.push_back(
      std::make_unique<browser_switcher::BrowserSwitcherPolicyMigrator>());
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
  migrators.push_back(std::make_unique<LegacyChromePolicyMigrator>(
      policy::key::kDeviceNativePrinters, policy::key::kDevicePrinters));
  migrators.push_back(std::make_unique<LegacyChromePolicyMigrator>(
      policy::key::kDeviceUserWhitelist, policy::key::kDeviceUserAllowlist));
  migrators.push_back(std::make_unique<LegacyChromePolicyMigrator>(
      policy::key::kNativePrintersBulkConfiguration,
      policy::key::kPrintersBulkConfiguration));

  ConfigurationPolicyProvider* user_policy_delegate_candidate =
      configuration_policy_provider ? configuration_policy_provider
                                    : special_user_policy_provider_.get();

  // Only proxy primary user policies to the device_wide policy service if all
  // of the following are true:
  // (*) This ProfilePolicyConnector has been created for the primary user.
  // (*) There is a policy provider for this profile. Note that for unmanaged
  //     users, |user_policy_delegate_candidate| will be nullptr.
  // (*) The ProxyPolicyProvider is actually used by the device-wide policy
  //     service. This may not be the case  e.g. in tests that use
  //     bBrowserPolicyConnectorBase::SetPolicyProviderForTesting.
  if (is_primary_user_ && user_policy_delegate_candidate &&
      GetDeviceWidePolicyService()->HasProvider(GetProxyPolicyProvider())) {
    GetProxyPolicyProvider()->SetDelegate(user_policy_delegate_candidate);

    // When proxying primary user policies to the device-wide PolicyService,
    // delay signaling that initialization is complete until the policies have
    // propagated. See CreatePolicyServiceWithInitializationThrottled for
    // details.
    policy_service_ = CreatePolicyServiceWithInitializationThrottled(
        policy_providers_, std::move(migrators),
        user_policy_delegate_candidate);
  } else {
    policy_service_ = std::make_unique<PolicyServiceImpl>(policy_providers_,
                                                          std::move(migrators));
  }
#else   // BUILDFLAG(IS_CHROMEOS_ASH)
  policy_service_ = std::make_unique<PolicyServiceImpl>(policy_providers_,
                                                        std::move(migrators));
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
}

void ProfilePolicyConnector::InitForTesting(
    std::unique_ptr<PolicyService> service) {
  DCHECK(!policy_service_);
  policy_service_ = std::move(service);
}

void ProfilePolicyConnector::OverrideIsManagedForTesting(bool is_managed) {
  is_managed_override_ = std::make_unique<bool>(is_managed);
}

void ProfilePolicyConnector::Shutdown() {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  if (is_primary_user_)
    GetProxyPolicyProvider()->SetDelegate(nullptr);

  if (special_user_policy_provider_)
    special_user_policy_provider_->Shutdown();
#endif

  for (auto& wrapped_policy_provider : wrapped_policy_providers_) {
    wrapped_policy_provider->Shutdown();
  }
}

bool ProfilePolicyConnector::IsManaged() const {
  if (is_managed_override_)
    return *is_managed_override_;
  const CloudPolicyStore* actual_policy_store = GetActualPolicyStore();
  if (actual_policy_store)
    return actual_policy_store->is_managed();
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  // As Lacros uses different ways to handle the main and the secondary
  // profiles, these profiles need to be handled differently:
  // ChromeOS's way is using mirror and we need to check with Ash using the
  // device account (via IsManagedDeviceAccount).
  // Desktop's way is used for secondary profiles and is using dice, which
  // can be read directly from the profile.
  // TODO(crbug/1245077): Remove this once Lacros only uses mirror.
  if (browser_policy_connector_ && IsMainProfile())
    return browser_policy_connector_->IsMainUserManaged();
#endif
  return false;
}

#if BUILDFLAG(IS_CHROMEOS_LACROS)
bool ProfilePolicyConnector::IsMainProfile() const {
  // If there is only a single profile or this connector object is owned by the
  // main profile, it must be the main profile.
  // TODO(crbug/1245077): Remove this once Lacros only uses mirror.
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (profile_manager->GetNumberOfProfiles() <= 1)
    return true;

  auto profiles = profile_manager->GetLoadedProfiles();
  const auto main_it = base::ranges::find_if(
      profiles, [](Profile* profile) { return profile->IsMainProfile(); });
  if (main_it == profiles.end())
    return false;
  return (*main_it)->GetProfilePolicyConnector() == this;
}
#endif

bool ProfilePolicyConnector::IsProfilePolicy(const char* policy_key) const {
  const ConfigurationPolicyProvider* const provider =
      DeterminePolicyProviderForPolicy(policy_key);
  return provider == configuration_policy_provider_;
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
void ProfilePolicyConnector::TriggerProxiedPoliciesWaitTimeoutForTesting() {
  CHECK(proxied_policies_propagated_watcher_);
  proxied_policies_propagated_watcher_->OnProviderUpdatePropagationTimedOut();
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

base::flat_set<std::string> ProfilePolicyConnector::user_affiliation_ids()
    const {
  auto* store = GetActualPolicyStore();
  if (!store || !store->has_policy())
    return {};
  const auto& ids = store->policy()->user_affiliation_ids();
  return {ids.begin(), ids.end()};
}

const CloudPolicyStore* ProfilePolicyConnector::GetActualPolicyStore() const {
  if (policy_store_)
    return policy_store_;
#if BUILDFLAG(IS_CHROMEOS_ASH)
  if (special_user_policy_provider_) {
    // |special_user_policy_provider_| is non-null for device-local accounts,
    // for the login profile, and the lock screen app profile.
    const DeviceCloudPolicyManagerAsh* const device_cloud_policy_manager =
        g_browser_process->platform_part()
            ->browser_policy_connector_ash()
            ->GetDeviceCloudPolicyManager();
    // The device_cloud_policy_manager can be a nullptr in unit tests.
    if (device_cloud_policy_manager)
      return device_cloud_policy_manager->core()->store();
  }
#endif
  return nullptr;
}

const ConfigurationPolicyProvider*
ProfilePolicyConnector::DeterminePolicyProviderForPolicy(
    const char* policy_key) const {
  const PolicyNamespace chrome_ns(POLICY_DOMAIN_CHROME, "");
  for (const ConfigurationPolicyProvider* provider : policy_providers_) {
    if (provider->policies().Get(chrome_ns).Get(policy_key))
      return provider;
  }
  return nullptr;
}

void ProfilePolicyConnector::AppendPolicyProviderWithSchemaTracking(
    ConfigurationPolicyProvider* policy_provider,
    SchemaRegistry* schema_registry) {
  auto wrapped_policy_provider =
      std::make_unique<SchemaRegistryTrackingPolicyProvider>(policy_provider);
  wrapped_policy_provider->Init(schema_registry);
  policy_providers_.push_back(wrapped_policy_provider.get());
  wrapped_policy_providers_.push_back(std::move(wrapped_policy_provider));
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
std::unique_ptr<PolicyService>
ProfilePolicyConnector::CreatePolicyServiceWithInitializationThrottled(
    const std::vector<ConfigurationPolicyProvider*>& policy_providers,
    std::vector<std::unique_ptr<PolicyMigrator>> migrators,
    ConfigurationPolicyProvider* user_policy_delegate) {
  DCHECK(user_policy_delegate);

  auto policy_service = PolicyServiceImpl::CreateWithThrottledInitialization(
      policy_providers, std::move(migrators));

  // base::Unretained is OK for |this| because
  // |proxied_policies_propagated_watcher_| is guaranteed not to call its
  // callback after it has been destroyed. base::Unretained is also OK for
  // |policy_service.get()| because it will be owned by |*this| and is never
  // explicitly destroyed.
  proxied_policies_propagated_watcher_ =
      std::make_unique<internal::ProxiedPoliciesPropagatedWatcher>(
          GetDeviceWidePolicyService(), GetProxyPolicyProvider(),
          user_policy_delegate,
          base::BindOnce(&ProfilePolicyConnector::OnProxiedPoliciesPropagated,
                         base::Unretained(this),
                         base::Unretained(policy_service.get())));
  return std::move(policy_service);
}

void ProfilePolicyConnector::OnProxiedPoliciesPropagated(
    PolicyServiceImpl* policy_service) {
  policy_service->UnthrottleInitialization();
  // Do not delete |proxied_policies_propagated_watcher_| synchronously, as the
  // PolicyService it is observing is expected to be iterating its observer
  // list.
  base::ThreadTaskRunnerHandle::Get()->DeleteSoon(
      FROM_HERE, std::move(proxied_policies_propagated_watcher_));
}
#endif

}  // namespace policy
