// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_LIBASSISTANT_GRPC_UTILS_MEDIA_STATUS_UTILS_H_
#define CHROMEOS_SERVICES_LIBASSISTANT_GRPC_UTILS_MEDIA_STATUS_UTILS_H_

#include "chromeos/assistant/internal/proto/shared/proto/v2/device_state_event.pb.h"
#include "chromeos/services/libassistant/public/mojom/media_controller.mojom-forward.h"

namespace assistant_client {
struct MediaStatus;
}  // namespace assistant_client

namespace chromeos {
namespace libassistant {

using MediaStatus = ::assistant::api::events::DeviceState::MediaStatus;

void ConvertMediaStatusToV1FromV2(const MediaStatus& media_status_proto,
                                  assistant_client::MediaStatus* media_status);

void ConvertMediaStatusToV2FromV1(
    const assistant_client::MediaStatus& media_status,
    MediaStatus* media_status_proto);

mojom::MediaStatePtr ConvertMediaStatusToMojomFromV2(
    const MediaStatus& media_status_proto);

void ConvertMediaStatusToV2FromMojom(const mojom::MediaState& state,
                                     MediaStatus* media_status_proto);

}  // namespace libassistant
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_LIBASSISTANT_GRPC_UTILS_MEDIA_STATUS_UTILS_H_
