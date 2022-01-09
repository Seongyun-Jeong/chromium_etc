// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ENTERPRISE_CONNECTORS_CONNECTORS_SERVICE_H_
#define CHROME_BROWSER_ENTERPRISE_CONNECTORS_CONNECTORS_SERVICE_H_

#include <memory>

#include "base/feature_list.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/enterprise/connectors/connectors_manager.h"
#include "components/enterprise/common/proto/connectors.pb.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/policy/core/common/policy_types.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "content/public/browser/browser_context.h"

namespace base {
template <typename T>
struct DefaultSingletonTraits;
}

namespace enterprise_connectors {

// Controls whether the Enterprise Connectors policies should be read by
// ConnectorsManager.
extern const base::Feature kEnterpriseConnectorsEnabled;

// For the moment, service provider configurations are static and only support
// google endpoints.  Therefore the configuration is placed here directly.
// Once the configuration becomes more dynamic this static string will be
// removed and replaced with a service to keep it up to date.
extern const char kServiceProviderConfig[];

// Accessor for the ServiceProviderConfig.
ServiceProviderConfig* GetServiceProviderConfig();

// A keyed service to access ConnectorsManager, which tracks Connector policies.
class ConnectorsService : public KeyedService {
 public:
  ConnectorsService(content::BrowserContext* context,
                    std::unique_ptr<ConnectorsManager> manager);
  ~ConnectorsService() override;

  // Accessors that check kEnterpriseConnectorsEnabled is enabled, and then call
  // the corresponding method in ConnectorsManager.
  absl::optional<ReportingSettings> GetReportingSettings(
      ReportingConnector connector);
  absl::optional<AnalysisSettings> GetAnalysisSettings(
      const GURL& url,
      AnalysisConnector connector);
  absl::optional<FileSystemSettings> GetFileSystemGlobalSettings(
      FileSystemConnector connector);
  absl::optional<FileSystemSettings> GetFileSystemSettings(
      const GURL& url,
      FileSystemConnector connector);

  bool IsConnectorEnabled(AnalysisConnector connector) const;
  bool IsConnectorEnabled(ReportingConnector connector) const;
  bool IsConnectorEnabled(FileSystemConnector connector) const;

  bool DelayUntilVerdict(AnalysisConnector connector);
  absl::optional<std::u16string> GetCustomMessage(AnalysisConnector connector,
                                                  const std::string& tag);
  absl::optional<GURL> GetLearnMoreUrl(AnalysisConnector connector,
                                       const std::string& tag);
  bool HasCustomInfoToDisplay(AnalysisConnector connector,
                              const std::string& tag);

  std::vector<std::string> GetAnalysisServiceProviderNames(
      AnalysisConnector connector);
  std::vector<std::string> GetReportingServiceProviderNames(
      ReportingConnector connector);

  // DM token accessor function for real-time URL checks. Returns a profile or
  // browser DM token depending on the policy scope, and absl::nullopt if there
  // is no token to use.
  absl::optional<std::string> GetDMTokenForRealTimeUrlCheck() const;

  // Returns the value to used by the enterprise real-time URL check Connector
  // if it is set and if the scope it's set at has a valid browser-profile
  // affiliation.
  safe_browsing::EnterpriseRealTimeUrlCheckMode GetAppliedRealTimeUrlCheck()
      const;

  // Returns the CBCM domain or profile domain that enables connector policies.
  // If both set Connector policies, the CBCM domain is returned as it has
  // precedence.
  std::string GetManagementDomain();

  // Testing functions.
  ConnectorsManager* ConnectorsManagerForTesting();

 private:
  struct DmToken {
    DmToken(const std::string& value, policy::PolicyScope scope);
    DmToken(DmToken&&);
    DmToken& operator=(DmToken&&);
    ~DmToken();

    // The value of the token to use.
    std::string value;

    // The scope of the token. This is determined by the scope of the Connector
    // policy used to get a DM token.
    policy::PolicyScope scope;
  };

  // Returns the DM token to use with the given |scope_pref|. That pref should
  // contain either POLICY_SCOPE_MACHINE or POLICY_SCOPE_USER.
  absl::optional<DmToken> GetDmToken(const char* scope_pref) const;
  absl::optional<DmToken> GetBrowserDmToken() const;
#if !defined(OS_CHROMEOS)
  absl::optional<DmToken> GetProfileDmToken() const;

  // Returns true if the browser isn't managed by CBCM, otherwise this checks if
  // the affiliations IDs from the profile and browser policy fetching responses
  // indicate that the same customer manages both.
  bool CanUseProfileDmToken() const;
#endif

  // Returns the policy::PolicyScope stored in the given |scope_pref|.
  policy::PolicyScope GetPolicyScope(const char* scope_pref) const;

  // Returns whether Connectors are enabled at all. This can be false if:
  // - The kEnterpriseConnectorsEnabled feature is disabled
  // - The profile is incognito
  bool ConnectorsEnabled() const;

  // Obtain a ClientMetadata instance corresponding to the current
  // OnSecurityEvent policy value.
  std::unique_ptr<ClientMetadata> BuildClientMetadata();

  raw_ptr<content::BrowserContext> context_;
  std::unique_ptr<ConnectorsManager> connectors_manager_;
};

class ConnectorsServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  static ConnectorsServiceFactory* GetInstance();
  static ConnectorsService* GetForBrowserContext(
      content::BrowserContext* context);

 private:
  ConnectorsServiceFactory();
  ~ConnectorsServiceFactory() override;
  friend struct base::DefaultSingletonTraits<ConnectorsServiceFactory>;

  // BrowserContextKeyedServiceFactory:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

}  // namespace enterprise_connectors

#endif  // CHROME_BROWSER_ENTERPRISE_CONNECTORS_CONNECTORS_SERVICE_H_
