// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/remote_open_url/remote_open_url_main.h"

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/i18n/icu_util.h"
#include "base/message_loop/message_pump_type.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/threading/thread_task_runner_handle.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "remoting/base/host_settings.h"
#include "remoting/base/logging.h"
#include "remoting/host/base/host_exit_codes.h"
#include "remoting/host/chromoting_host_services_client.h"
#include "remoting/host/remote_open_url/remote_open_url_client.h"
#include "remoting/host/resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

namespace remoting {

int RemoteOpenUrlMain(int argc, char** argv) {
  if (argc > 2) {
    printf("Usage: %s [URL]\n", argv[0]);
    return -1;
  }

  base::AtExitManager exit_manager;
  base::SingleThreadTaskExecutor io_task_executor(base::MessagePumpType::IO);

  base::CommandLine::Init(argc, argv);
  InitHostLogging();

  if (!ChromotingHostServicesClient::Initialize()) {
    return kInitializationFailed;
  }

  base::i18n::InitializeICU();
  LoadResources("");

  mojo::core::Init();
  mojo::core::ScopedIPCSupport ipc_support(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);
  HostSettings::Initialize();

  RemoteOpenUrlClient client_;
  if (argc == 1) {
    // remote_open_url will be called with no arguments when the user opens
    // "Web Browser" from the desktop environment (e.g. from XFCE's dock). If we
    // don't fallback to the previous default browser then the user will see
    // nothing in that case.
    HOST_LOG << "No argument. Fallback browser will be opened.";
    client_.OpenFallbackBrowser();
  } else if (argc == 2) {
    base::RunLoop run_loop;
    client_.OpenUrl(GURL(argv[1]), run_loop.QuitClosure());
    run_loop.Run();
  }

  return kSuccessExitCode;
}

}  // namespace remoting
