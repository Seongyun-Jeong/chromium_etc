// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/permissions/prediction_service/prediction_model_executor.h"

#include "base/notreached.h"
#include "components/permissions/prediction_service/prediction_common.h"
#include "components/permissions/prediction_service/prediction_request_features.h"
#include "third_party/tflite_support/src/tensorflow_lite_support/cc/task/core/task_utils.h"

namespace permissions {

PredictionModelExecutor::PredictionModelExecutor() = default;
PredictionModelExecutor::~PredictionModelExecutor() = default;

absl::Status PredictionModelExecutor::Preprocess(
    const std::vector<TfLiteTensor*>& input_tensors,
    const GeneratePredictionsRequest& input) {
  switch (input.permission_features()[0].permission_type_case()) {
    case PermissionFeatures::kNotificationPermission:
      request_type_ = RequestType::kNotifications;
      break;
    case PermissionFeatures::kGeolocationPermission:
      request_type_ = RequestType::kGeolocation;
      break;
    default:
      NOTREACHED();
  }

  absl::Status status = tflite::task::core::PopulateTensor<float>(
      input.client_features().client_stats().avg_deny_rate(), input_tensors[0]);
  if (!status.ok()) {
    return status;
  }

  status = tflite::task::core::PopulateTensor<float>(
      input.client_features().client_stats().avg_dismiss_rate(),
      input_tensors[1]);
  if (!status.ok()) {
    return status;
  }

  status = tflite::task::core::PopulateTensor<float>(
      input.client_features().client_stats().avg_grant_rate(),
      input_tensors[2]);
  if (!status.ok()) {
    return status;
  }

  status = tflite::task::core::PopulateTensor<float>(
      input.client_features().client_stats().avg_ignore_rate(),
      input_tensors[3]);
  if (!status.ok()) {
    return status;
  }

  status = tflite::task::core::PopulateTensor<float>(
      input.permission_features()[0].permission_stats().avg_deny_rate(),
      input_tensors[4]);
  if (!status.ok()) {
    return status;
  }

  status = tflite::task::core::PopulateTensor<float>(
      input.permission_features()[0].permission_stats().avg_dismiss_rate(),
      input_tensors[5]);
  if (!status.ok()) {
    return status;
  }

  status = tflite::task::core::PopulateTensor<float>(
      input.permission_features()[0].permission_stats().avg_grant_rate(),
      input_tensors[6]);
  if (!status.ok()) {
    return status;
  }

  status = tflite::task::core::PopulateTensor<float>(
      input.permission_features()[0].permission_stats().avg_ignore_rate(),
      input_tensors[7]);
  if (!status.ok()) {
    return status;
  }

  status = tflite::task::core::PopulateTensor<int64_t>(
      static_cast<int64_t>(
          input.permission_features()[0].permission_stats().prompts_count()),
      input_tensors[8]);
  if (!status.ok()) {
    return status;
  }

  status = tflite::task::core::PopulateTensor<int64_t>(
      static_cast<int64_t>(
          input.client_features().client_stats().prompts_count()),
      input_tensors[9]);
  if (!status.ok()) {
    return status;
  }

  status = tflite::task::core::PopulateTensor<int64_t>(
      static_cast<int64_t>(input.client_features().gesture_enum()),
      input_tensors[10]);
  if (!status.ok()) {
    return status;
  }

  status = tflite::task::core::PopulateTensor<int64_t>(
      static_cast<int64_t>(input.client_features().platform_enum()),
      input_tensors[11]);
  if (!status.ok()) {
    return status;
  }

  return absl::OkStatus();
}
GeneratePredictionsResponse PredictionModelExecutor::Postprocess(
    const std::vector<const TfLiteTensor*>& output_tensors) {
  DCHECK(request_type_ == RequestType::kNotifications ||
         request_type_ == RequestType::kGeolocation);
  std::vector<float> data;
  absl::Status status =
      tflite::task::core::PopulateVector<float>(output_tensors[0], &data);
  DCHECK(status.ok());

  GeneratePredictionsResponse response;
  float threshold = request_type_ == RequestType::kNotifications
                        ? kNotificationPredictionsThreshold
                        : kGeolocationPredictionsThreshold;
  response.mutable_prediction()
      ->Add()
      ->mutable_grant_likelihood()
      ->set_discretized_likelihood(
          data[1] >= threshold
              ? PermissionPrediction_Likelihood_DiscretizedLikelihood_VERY_UNLIKELY
              : PermissionPrediction_Likelihood_DiscretizedLikelihood_DISCRETIZED_LIKELIHOOD_UNSPECIFIED);

  return response;
}

}  // namespace permissions
