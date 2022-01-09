// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_POLICY_REMOTE_COMMANDS_DEVICE_COMMAND_REFRESH_MACHINE_CERTIFICATE_JOB_H_
#define CHROME_BROWSER_ASH_POLICY_REMOTE_COMMANDS_DEVICE_COMMAND_REFRESH_MACHINE_CERTIFICATE_JOB_H_

#include "base/memory/weak_ptr.h"
#include "components/policy/core/common/remote_commands/remote_command_job.h"

namespace ash {
namespace attestation {
class MachineCertificateUploader;
}  // namespace attestation
}  // namespace ash

namespace policy {

class DeviceCommandRefreshMachineCertificateJob : public RemoteCommandJob {
 public:
  explicit DeviceCommandRefreshMachineCertificateJob(
      ash::attestation::MachineCertificateUploader*
          machine_certificate_uploader);

  DeviceCommandRefreshMachineCertificateJob(
      const DeviceCommandRefreshMachineCertificateJob&) = delete;
  DeviceCommandRefreshMachineCertificateJob& operator=(
      const DeviceCommandRefreshMachineCertificateJob&) = delete;

  ~DeviceCommandRefreshMachineCertificateJob() override;

  // RemoteCommandJob:
  enterprise_management::RemoteCommand_Type GetType() const override;

 private:
  ash::attestation::MachineCertificateUploader* machine_certificate_uploader_;

  // RemoteCommandJob:
  void RunImpl(CallbackWithResult succeeded_callback,
               CallbackWithResult failed_callback) override;

  // Handle the result of a refresh and upload of the machine certificate.
  void OnCertificateUploaded(CallbackWithResult succeeded_callback,
                             CallbackWithResult failed_callback,
                             bool success);

  base::WeakPtrFactory<DeviceCommandRefreshMachineCertificateJob>
      weak_ptr_factory_{this};
};

}  // namespace policy

#endif  // CHROME_BROWSER_ASH_POLICY_REMOTE_COMMANDS_DEVICE_COMMAND_REFRESH_MACHINE_CERTIFICATE_JOB_H_
