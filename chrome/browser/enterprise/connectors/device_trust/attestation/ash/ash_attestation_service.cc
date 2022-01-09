// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/connectors/device_trust/attestation/ash/ash_attestation_service.h"

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/check.h"
#include "base/time/time.h"
#include "chrome/browser/ash/attestation/tpm_challenge_key_result.h"
#include "chrome/browser/ash/attestation/tpm_challenge_key_with_timeout.h"
#include "chrome/browser/enterprise/connectors/device_trust/attestation/common/attestation_utils.h"
#include "chrome/browser/enterprise/connectors/device_trust/common/metrics_utils.h"

namespace enterprise_connectors {

namespace {

using ash::attestation::TpmChallengeKeyResultCode;

DTAttestationResult ToAttestationResult(TpmChallengeKeyResultCode code) {
  // Map the error codes as best as possible to the DTAttestationResult. The
  // `kFailedToGenerateResponse` will be considered the bucket of all unmappable
  // errors.
  switch (code) {
    case TpmChallengeKeyResultCode::kKeyRegistrationFailedError:
    case TpmChallengeKeyResultCode::kUserKeyNotAvailableError:
      return DTAttestationResult::kMissingSigningKey;
    case TpmChallengeKeyResultCode::kChallengeBadBase64Error:
      return DTAttestationResult::kBadChallengeFormat;
    default:
      return DTAttestationResult::kFailedToGenerateResponse;
  }
}

}  // namespace

AshAttestationService::AshAttestationService(Profile* profile)
    : profile_(profile) {}
AshAttestationService::~AshAttestationService() = default;

void AshAttestationService::BuildChallengeResponseForVAChallenge(
    const std::string& challenge,
    std::unique_ptr<attestation::DeviceTrustSignals> signals,
    AttestationCallback callback) {
  DCHECK(signals);
  auto tpm_key_challenger =
      std::make_unique<ash::attestation::TpmChallengeKeyWithTimeout>();
  auto* tpm_key_challenger_ptr = tpm_key_challenger.get();
  tpm_key_challenger_ptr->BuildResponse(
      base::Seconds(15), ash::attestation::KEY_DEVICE, profile_,
      base::BindOnce(&AshAttestationService::ReturnResult,
                     weak_factory_.GetWeakPtr(), std::move(tpm_key_challenger),
                     std::move(callback)),
      JsonChallengeToProtobufChallenge(challenge), /*register_key=*/false,
      /*key_name_for_spkac=*/std::string(), /*signals=*/*signals);
}

void AshAttestationService::ReturnResult(
    std::unique_ptr<ash::attestation::TpmChallengeKeyWithTimeout>
        tpm_key_challenger,
    AttestationCallback callback,
    const ash::attestation::TpmChallengeKeyResult& result) {
  std::string encoded_response;
  if (result.IsSuccess()) {
    encoded_response =
        ProtobufChallengeToJsonChallenge(result.challenge_response);
    LogAttestationResult(DTAttestationResult::kSuccess);
  } else {
    LogAttestationResult(ToAttestationResult(result.result_code));
  }
  std::move(callback).Run(encoded_response);
}

}  // namespace enterprise_connectors
