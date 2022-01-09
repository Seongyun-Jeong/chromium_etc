// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_POLICY_CORE_BROWSER_POLICY_CONNECTOR_ASH_H_
#define CHROME_BROWSER_ASH_POLICY_CORE_BROWSER_POLICY_CONNECTOR_ASH_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ash/login/users/affiliation.h"
#include "chrome/browser/ash/policy/core/device_cloud_policy_manager_ash.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "components/policy/core/common/cloud/cloud_policy_constants.h"

class PrefRegistrySimple;
class PrefService;

namespace ash {
namespace attestation {
class AttestationFlow;
}  // namespace attestation
}  // namespace ash

namespace enterprise_management {
class PolicyData;
}  // namespace enterprise_management

namespace chromeos {
class InstallAttributes;
}  // namespace chromeos

namespace policy {

class AdbSideloadingAllowanceModePolicyHandler;
class AffiliatedCloudPolicyInvalidator;
class AffiliatedInvalidationServiceProvider;
class AffiliatedRemoteCommandsInvalidator;
class BluetoothPolicyHandler;
class DeviceActiveDirectoryPolicyManager;
class DeviceCloudPolicyInitializer;
class ActiveDirectoryDeviceStateUploader;
class DeviceDockMacAddressHandler;
class DeviceLocalAccountPolicyService;
class DeviceNamePolicyHandler;
class DeviceNetworkConfigurationUpdater;
class DeviceWiFiAllowedHandler;
struct EnrollmentConfig;
class MinimumVersionPolicyHandler;
class MinimumVersionPolicyHandlerDelegateImpl;
class ProxyPolicyProvider;
class ServerBackedStateKeysBroker;
class TPMAutoUpdateModePolicyHandler;
class DeviceScheduledUpdateChecker;
class DeviceCloudExternalDataPolicyHandler;
class SystemProxyHandler;
class DeviceScheduledRebootHandler;

// Extends ChromeBrowserPolicyConnector with the setup specific to Chrome OS.
class BrowserPolicyConnectorAsh : public ChromeBrowserPolicyConnector,
                                  public DeviceCloudPolicyManagerAsh::Observer {
 public:
  BrowserPolicyConnectorAsh();

  BrowserPolicyConnectorAsh(const BrowserPolicyConnectorAsh&) = delete;
  BrowserPolicyConnectorAsh& operator=(const BrowserPolicyConnectorAsh&) =
      delete;

  ~BrowserPolicyConnectorAsh() override;

  // Helper that returns a new BACKGROUND SequencedTaskRunner. Each
  // SequencedTaskRunner returned is independent from the others.
  static scoped_refptr<base::SequencedTaskRunner> CreateBackgroundTaskRunner();

  // ChromeBrowserPolicyConnector:
  void Init(PrefService* local_state,
            scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
      override;

  // Checks whether this devices is under any kind of enterprise management.
  bool IsDeviceEnterpriseManaged() const override;

  bool HasMachineLevelPolicies() override;

  // Shutdown() is called from BrowserProcessImpl::StartTearDown() but |this|
  // observes some objects that get destroyed earlier. PreShutdown() is called
  // from `ChromeBrowserMainPartsAsh::PostMainMessageLoopRun()`, allowing the
  // connection to these dependencies to be severed earlier.
  void PreShutdown();

  void Shutdown() override;

  // Checks whether this is a cloud (DM server) managed enterprise device.
  bool IsCloudManaged() const;

  // Checks whether this is an Active Directory managed enterprise device.
  bool IsActiveDirectoryManaged() const;

  // Returns the enterprise enrollment domain if device is managed.
  std::string GetEnterpriseEnrollmentDomain() const;

  // Returns the manager of the domain for use in UI if specified, otherwise the
  // enterprise display domain.
  // The policy needs to be loaded before the display manager can be used.
  std::string GetEnterpriseDomainManager() const;

  // Returns the SSO profile id for the managing OU of this device. Currently
  // identifies the SAML settings for the device.
  std::string GetSSOProfile() const;

  // Returns the Kerberos realm (aka Windows Domain) if the device is managed by
  // Active Directory.
  std::string GetRealm() const;

  // Returns the device asset ID if it is set.
  std::string GetDeviceAssetID() const;

  // Returns the machine name if it is set.
  std::string GetMachineName() const;

  // Returns the device annotated location if it is set.
  std::string GetDeviceAnnotatedLocation() const;

  // Returns the cloud directory API ID or an empty string if it is not set.
  std::string GetDirectoryApiID() const;

  // Returns the obfuscated customer's ID or an empty string if it not set.
  std::string GetObfuscatedCustomerID() const;

  // Returns the organization logo URL or an empty string if it is not set.
  std::string GetCustomerLogoURL() const;

  // Returns the device mode. For Chrome OS this function will return the mode
  // stored in the lockbox, or DEVICE_MODE_CONSUMER if the lockbox has been
  // locked empty, or DEVICE_MODE_UNKNOWN if the device has not been owned yet.
  // For other OSes the function will always return DEVICE_MODE_CONSUMER.
  DeviceMode GetDeviceMode() const;

  // Delegates to chromeos::InstallAttributes::Get()
  chromeos::InstallAttributes* GetInstallAttributes() const;

  // Get the enrollment configuration for the device as decided by various
  // factors. See DeviceCloudPolicyInitializer::GetPrescribedEnrollmentConfig()
  // for details.
  EnrollmentConfig GetPrescribedEnrollmentConfig() const;

  // May be nullptr, e.g. for devices managed by Active Directory.
  DeviceCloudPolicyManagerAsh* GetDeviceCloudPolicyManager() const {
    return device_cloud_policy_manager_;
  }

  // May be nullptr, e.g. for cloud-managed devices.
  DeviceActiveDirectoryPolicyManager* GetDeviceActiveDirectoryPolicyManager()
      const {
    return device_active_directory_policy_manager_;
  }

  // May be nullptr, e.g. for devices managed by Active Directory.
  DeviceLocalAccountPolicyService* GetDeviceLocalAccountPolicyService() const {
    return device_local_account_policy_service_.get();
  }

  // May be nullptr, e.g. for devices managed by Active Directory.
  ServerBackedStateKeysBroker* GetStateKeysBroker() const {
    return state_keys_broker_.get();
  }

  MinimumVersionPolicyHandler* GetMinimumVersionPolicyHandler() const {
    return minimum_version_policy_handler_.get();
  }

  DeviceNetworkConfigurationUpdater* GetDeviceNetworkConfigurationUpdater()
      const {
    return device_network_configuration_updater_.get();
  }

  TPMAutoUpdateModePolicyHandler* GetTPMAutoUpdateModePolicyHandler() const {
    return tpm_auto_update_mode_policy_handler_.get();
  }

  SystemProxyHandler* GetSystemProxyHandler() const {
    return system_proxy_handler_.get();
  }

  DeviceNamePolicyHandler* GetDeviceNamePolicyHandler() const {
    return device_name_policy_handler_.get();
  }

  AdbSideloadingAllowanceModePolicyHandler*
  GetAdbSideloadingAllowanceModePolicyHandler() const {
    return adb_sideloading_allowance_mode_policy_handler_.get();
  }

  // Return a pointer to the device-wide client certificate provisioning
  // scheduler. The callers do not take ownership of that pointer.
  ash::cert_provisioning::CertProvisioningScheduler*
  GetDeviceCertProvisioningScheduler() {
    return device_cert_provisioning_scheduler_.get();
  }

  // Returns a pointer to the attestation flow to be used during enrollment. The
  // only reason for this member and getter to exist is that sometimes fake
  // attestation flow is needed for testing.
  // TODO(crbug.com/1235325): Remove AttestationFlow completely from the
  // connector and a fake one directly to |EnterpriseEnrollmentHelperImpl|.
  ash::attestation::AttestationFlow* GetAttestationFlow() const {
    return attestation_flow_.get();
  }

  // Sets the attestation flow for testing.
  void SetAttestationFlowForTesting(
      std::unique_ptr<ash::attestation::AttestationFlow> attestation_flow);

  // Returns device's market segment.
  MarketSegment GetEnterpriseMarketSegment() const;

  // Returns a ProxyPolicyProvider that will be used to forward user policies
  // from the primary Profile to the device-wide PolicyService[1].
  // This means that user policies from the primary Profile will also affect
  // local state[2] Preferences.
  //
  // Note that the device-wide PolicyService[1] is created before Profiles are
  // ready / before a user has signed-in. As PolicyProviders can only be
  // configured during PolicyService creation, a ProxyPolicyProvider (which does
  // not have a delegate yet) is included in the device-wide PolicyService at
  // the time of its creation. This returns an unowned pointer to that
  // ProxyPolicyProvider so the caller can invoke SetDelegate on it. The
  // returned pointer is guaranteed to be valid as long as this instance is
  // valid.
  //
  // [1] i.e. g_browser_process->policy_service()
  // [2] i.e. g_browser_process->local_state()
  ProxyPolicyProvider* GetGlobalUserCloudPolicyProvider();

  // Registers device refresh rate pref.
  static void RegisterPrefs(PrefRegistrySimple* registry);

  // DeviceCloudPolicyManagerAsh::Observer:
  void OnDeviceCloudPolicyManagerConnected() override;
  void OnDeviceCloudPolicyManagerDisconnected() override;

  // TODO(crbug.com/1187628): Combine the following two functions into one to
  // simplify the API.
  base::flat_set<std::string> device_affiliation_ids() const override;
  ash::AffiliationIDSet GetDeviceAffiliationIDs() const;

  // BrowserPolicyConnector:
  // Always returns true as command line flag can be set under dev mode only.
  bool IsCommandLineSwitchSupported() const override;

 protected:
  // ChromeBrowserPolicyConnector:
  std::vector<std::unique_ptr<policy::ConfigurationPolicyProvider>>
  CreatePolicyProviders() override;

 private:
  // Set the timezone as soon as the policies are available.
  void SetTimezoneIfPolicyAvailable();

  // Restarts the device cloud policy initializer, because the device's
  // registration status changed from registered to unregistered.
  void RestartDeviceCloudPolicyInitializer();

  // Returns the device policy data or nullptr if it does not exist.
  const enterprise_management::PolicyData* GetDevicePolicy() const;

  // Components of the device cloud policy implementation.
  std::unique_ptr<ServerBackedStateKeysBroker> state_keys_broker_;
  std::unique_ptr<AffiliatedInvalidationServiceProvider>
      affiliated_invalidation_service_provider_;
  DeviceCloudPolicyManagerAsh* device_cloud_policy_manager_ = nullptr;
  DeviceActiveDirectoryPolicyManager* device_active_directory_policy_manager_ =
      nullptr;
  std::unique_ptr<ActiveDirectoryDeviceStateUploader>
      active_directory_device_state_uploader_;
  PrefService* local_state_ = nullptr;
  std::unique_ptr<DeviceCloudPolicyInitializer>
      device_cloud_policy_initializer_;
  std::unique_ptr<DeviceLocalAccountPolicyService>
      device_local_account_policy_service_;
  std::unique_ptr<AffiliatedCloudPolicyInvalidator>
      device_cloud_policy_invalidator_;
  std::unique_ptr<AffiliatedRemoteCommandsInvalidator>
      device_remote_commands_invalidator_;

  std::unique_ptr<BluetoothPolicyHandler> bluetooth_policy_handler_;
  std::unique_ptr<DeviceNamePolicyHandler> device_name_policy_handler_;
  std::unique_ptr<MinimumVersionPolicyHandler> minimum_version_policy_handler_;
  std::unique_ptr<MinimumVersionPolicyHandlerDelegateImpl>
      minimum_version_policy_handler_delegate_;
  std::unique_ptr<DeviceDockMacAddressHandler>
      device_dock_mac_address_source_handler_;
  std::unique_ptr<DeviceWiFiAllowedHandler> device_wifi_allowed_handler_;
  std::unique_ptr<TPMAutoUpdateModePolicyHandler>
      tpm_auto_update_mode_policy_handler_;
  std::unique_ptr<DeviceScheduledUpdateChecker>
      device_scheduled_update_checker_;
  std::vector<std::unique_ptr<policy::DeviceCloudExternalDataPolicyHandler>>
      device_cloud_external_data_policy_handlers_;
  std::unique_ptr<SystemProxyHandler> system_proxy_handler_;
  std::unique_ptr<AdbSideloadingAllowanceModePolicyHandler>
      adb_sideloading_allowance_mode_policy_handler_;
  std::unique_ptr<DeviceScheduledRebootHandler>
      device_scheduled_reboot_handler_;

  // This policy provider is used on Chrome OS to feed user policy into the
  // global PolicyService instance. This works by installing the cloud policy
  // provider of the primary profile as the delegate of the ProxyPolicyProvider,
  // after login.
  // The provider is owned by the base class; this field is just a typed weak
  // pointer to get to the ProxyPolicyProvider at SetUserPolicyDelegate().
  ProxyPolicyProvider* global_user_cloud_policy_provider_ = nullptr;

  std::unique_ptr<DeviceNetworkConfigurationUpdater>
      device_network_configuration_updater_;

  // The ConfigurationPolicyProviders created in the constructor are initially
  // added here, and then pushed to the super class in BuildPolicyProviders().
  std::vector<std::unique_ptr<ConfigurationPolicyProvider>> providers_for_init_;

  // Manages provisioning of certificates from
  // RequiredClientCertificateForDevice device policy.
  std::unique_ptr<ash::cert_provisioning::CertProvisioningScheduler>
      device_cert_provisioning_scheduler_;

  // Attestation flow to be used during enrollment. The only reason for this
  // member and getter to exist is that sometimes fake attestation flow is
  // needed for testing.
  // TODO(crbug.com/1235325): Remove AttestationFlow completely from the
  // connector and a fake one directly to |EnterpriseEnrollmentHelperImpl|.
  std::unique_ptr<ash::attestation::AttestationFlow> attestation_flow_;

  base::WeakPtrFactory<BrowserPolicyConnectorAsh> weak_ptr_factory_{this};
};

}  // namespace policy

#endif  // CHROME_BROWSER_ASH_POLICY_CORE_BROWSER_POLICY_CONNECTOR_ASH_H_
