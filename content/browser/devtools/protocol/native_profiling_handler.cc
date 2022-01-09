// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/devtools/protocol/native_profiling_handler.h"

#include "content/browser/devtools/devtools_agent_host_impl.h"
#include "content/public/browser/profiling_utils.h"

namespace content {
namespace protocol {

NativeProfilingHandler::NativeProfilingHandler()
    : DevToolsDomainHandler(NativeProfiling::Metainfo::domainName) {}
NativeProfilingHandler::~NativeProfilingHandler() = default;

void NativeProfilingHandler::Wire(UberDispatcher* dispatcher) {
  frontend_ =
      std::make_unique<NativeProfiling::Frontend>(dispatcher->channel());
  NativeProfiling::Dispatcher::wire(dispatcher, this);
}

DispatchResponse NativeProfilingHandler::DumpProfilingDataOfAllProcesses() {
  content::WaitForAllChildrenToDumpProfilingData();
  return Response::Success();
}

}  // namespace protocol
}  // namespace content