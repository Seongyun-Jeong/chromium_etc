// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/remote_commands/cbcm_remote_commands_factory.h"

#include "base/notreached.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/enterprise/remote_commands/clear_browsing_data_job.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/policy/core/common/remote_commands/remote_command_job.h"

#if defined(OS_LINUX) || defined(OS_WIN) || defined(OS_MAC)
#include "chrome/browser/enterprise/connectors/device_trust/device_trust_features.h"
#include "chrome/browser/enterprise/remote_commands/rotate_attestation_credential_job.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "components/enterprise/browser/controller/chrome_browser_cloud_management_controller.h"
#include "components/enterprise/browser/device_trust/device_trust_key_manager.h"
#endif  // defined(OS_LINUX) || defined(OS_WIN) || defined(OS_MAC)

namespace enterprise_commands {

std::unique_ptr<policy::RemoteCommandJob>
CBCMRemoteCommandsFactory::BuildJobForType(
    enterprise_management::RemoteCommand_Type type,
    policy::RemoteCommandsService* service) {
  if (type ==
      enterprise_management::RemoteCommand_Type_BROWSER_CLEAR_BROWSING_DATA) {
    return std::make_unique<ClearBrowsingDataJob>(
        g_browser_process->profile_manager());
  }

#if defined(OS_LINUX) || defined(OS_WIN) || defined(OS_MAC)
  if (enterprise_connectors::IsDeviceTrustConnectorFeatureEnabled() &&
      type == enterprise_management::
                  RemoteCommand_Type_BROWSER_ROTATE_ATTESTATION_CREDENTIAL) {
    return std::make_unique<RotateAttestationCredentialJob>(
        g_browser_process->browser_policy_connector()
            ->chrome_browser_cloud_management_controller()
            ->GetDeviceTrustKeyManager());
  }
#endif  // defined(OS_LINUX) || defined(OS_WIN) || defined(OS_MAC)

  NOTREACHED() << "Received an unsupported remote command type: " << type;
  return nullptr;
}

}  // namespace enterprise_commands
