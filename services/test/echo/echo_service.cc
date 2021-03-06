// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/test/echo/echo_service.h"

#include <string.h>

#include "base/immediate_crash.h"
#include "base/memory/shared_memory_mapping.h"

namespace echo {

EchoService::EchoService(mojo::PendingReceiver<mojom::EchoService> receiver)
    : receiver_(this, std::move(receiver)) {}

EchoService::~EchoService() = default;

void EchoService::EchoString(const std::string& input,
                             EchoStringCallback callback) {
  std::move(callback).Run(input);
}

void EchoService::EchoStringToSharedMemory(
    const std::string& input,
    base::UnsafeSharedMemoryRegion region) {
  base::WritableSharedMemoryMapping mapping = region.Map();
  memcpy(mapping.memory(), input.data(), input.size());
}

void EchoService::Quit() {
  receiver_.reset();
}

void EchoService::Crash() {
  IMMEDIATE_CRASH();
}

}  // namespace echo
