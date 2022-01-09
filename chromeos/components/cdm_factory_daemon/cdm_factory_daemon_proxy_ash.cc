// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/cdm_factory_daemon/cdm_factory_daemon_proxy_ash.h"

#include "ash/shell.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chromeos/components/cdm_factory_daemon/output_protection_impl.h"
#include "chromeos/dbus/cdm_factory_daemon/cdm_factory_daemon_client.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/invitation.h"
#include "ui/display/manager/display_manager.h"

namespace {
constexpr char kCdmFactoryDaemonPipeName[] = "cdm-factory-daemon-pipe";
}  // namespace

namespace chromeos {

CdmFactoryDaemonProxyAsh::CdmFactoryDaemonProxyAsh()
    : CdmFactoryDaemonProxy() {}

CdmFactoryDaemonProxyAsh::~CdmFactoryDaemonProxyAsh() = default;

void CdmFactoryDaemonProxyAsh::Create(
    mojo::PendingReceiver<BrowserCdmFactory> receiver) {
  // We do not want to use a SelfOwnedReceiver for the main implementation here
  // because if the GPU process or Lacros goes down, we don't want to destruct
  // and drop our connection to the daemon. It's not possible to reconnect to
  // the daemon from the browser process w/out restarting both processes (which
  // happens if the browser goes down). However, the connection between ash-GPU
  // and ash-browser uses a ReceiverSet, which is self-destructing on
  // disconnect.
  GetInstance().BindReceiver(std::move(receiver));
}

// static
CdmFactoryDaemonProxyAsh& CdmFactoryDaemonProxyAsh::GetInstance() {
  static base::NoDestructor<CdmFactoryDaemonProxyAsh> instance;
  return *instance;
}

void CdmFactoryDaemonProxyAsh::ConnectOemCrypto(
    mojo::PendingReceiver<arc::mojom::OemCryptoService> oemcryptor,
    mojo::PendingRemote<arc::mojom::ProtectedBufferManager>
        protected_buffer_manager,
    mojo::PendingRemote<cdm::mojom::OutputProtection> output_protection) {
  // This gets invoked from ArcBridge which uses a different thread.
  if (!mojo_task_runner_->RunsTasksInCurrentSequence()) {
    mojo_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&CdmFactoryDaemonProxyAsh::ConnectOemCrypto,
                                  base::Unretained(this), std::move(oemcryptor),
                                  std::move(protected_buffer_manager),
                                  std::move(output_protection)));
    return;
  }

  DVLOG(1) << "CdmFactoryDaemonProxyAsh::ConnectOemCrypto called";
  if (daemon_remote_) {
    DVLOG(1) << "CdmFactoryDaemon mojo connection already exists, re-use it";
    CompleteOemCryptoConnection(std::move(oemcryptor),
                                std::move(protected_buffer_manager),
                                std::move(output_protection));
    return;
  }

  EstablishDaemonConnection(base::BindOnce(
      &CdmFactoryDaemonProxyAsh::CompleteOemCryptoConnection,
      base::Unretained(this), std::move(oemcryptor),
      std::move(protected_buffer_manager), std::move(output_protection)));
}

void CdmFactoryDaemonProxyAsh::CreateFactory(const std::string& key_system,
                                             CreateFactoryCallback callback) {
  DCHECK(mojo_task_runner_->RunsTasksInCurrentSequence());
  DVLOG(1) << "CdmFactoryDaemonProxyAsh::CreateFactory called";
  if (daemon_remote_) {
    DVLOG(1) << "CdmFactoryDaemon mojo connection already exists, re-use it";
    GetFactoryInterface(key_system, std::move(callback));
    return;
  }

  EstablishDaemonConnection(
      base::BindOnce(&CdmFactoryDaemonProxyAsh::GetFactoryInterface,
                     base::Unretained(this), key_system, std::move(callback)));
}

void CdmFactoryDaemonProxyAsh::GetHwConfigData(
    GetHwConfigDataCallback callback) {
  DCHECK(mojo_task_runner_->RunsTasksInCurrentSequence());
  DVLOG(1) << "CdmFactoryDaemonProxyAsh::GetHwConfigData called";
  if (daemon_remote_) {
    DVLOG(1) << "CdmFactoryDaemon mojo connection already exists, re-use it";
    ProxyGetHwConfigData(std::move(callback));
    return;
  }

  EstablishDaemonConnection(
      base::BindOnce(&CdmFactoryDaemonProxyAsh::ProxyGetHwConfigData,
                     base::Unretained(this), std::move(callback)));
}

void CdmFactoryDaemonProxyAsh::GetOutputProtection(
    mojo::PendingReceiver<cdm::mojom::OutputProtection> output_protection) {
  OutputProtectionImpl::Create(std::move(output_protection));
}

void CdmFactoryDaemonProxyAsh::GetScreenResolutions(
    GetScreenResolutionsCallback callback) {
  std::vector<gfx::Size> resolutions;
  const std::vector<display::DisplaySnapshot*>& displays =
      ash::Shell::Get()->display_manager()->configurator()->cached_displays();
  for (display::DisplaySnapshot* display : displays)
    resolutions.emplace_back(display->native_mode()->size());

  std::move(callback).Run(std::move(resolutions));
}

void CdmFactoryDaemonProxyAsh::EstablishDaemonConnection(
    base::OnceClosure callback) {
  // This may have happened already.
  if (daemon_remote_) {
    std::move(callback).Run();
    return;
  }

  // Bootstrap the Mojo connection to the daemon.
  mojo::OutgoingInvitation invitation;
  mojo::PlatformChannel channel;
  mojo::ScopedMessagePipeHandle server_pipe =
      invitation.AttachMessagePipe(kCdmFactoryDaemonPipeName);
  mojo::OutgoingInvitation::Send(std::move(invitation),
                                 base::kNullProcessHandle,
                                 channel.TakeLocalEndpoint());
  base::ScopedFD fd =
      channel.TakeRemoteEndpoint().TakePlatformHandle().TakeFD();

  // Bind the Mojo pipe to the interface before we send the D-Bus message
  // to avoid any kind of race condition with detecting it's been bound.
  // It's safe to do this before the other end binds anyways.
  daemon_remote_.Bind(mojo::PendingRemote<cdm::mojom::CdmFactoryDaemon>(
      std::move(server_pipe), 0u));

  // Disconnect handler is setup for when the daemon crashes so we can drop our
  // connection to it and signal it needs to be reconnected on next entry.
  daemon_remote_.set_disconnect_handler(
      base::BindOnce(&CdmFactoryDaemonProxyAsh::OnDaemonMojoConnectionError,
                     base::Unretained(this)));

  // We need to invoke this call on the D-Bus (UI) thread.
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&CdmFactoryDaemonProxyAsh::SendDBusRequest,
                                base::Unretained(this), std::move(fd),
                                std::move(callback)));
}

void CdmFactoryDaemonProxyAsh::GetFactoryInterface(
    const std::string& key_system,
    CreateFactoryCallback callback) {
  if (!daemon_remote_) {
    LOG(ERROR) << "daemon_remote_ interface is not connected";
    std::move(callback).Run(mojo::PendingRemote<cdm::mojom::CdmFactory>());
    return;
  }
  daemon_remote_->CreateFactory(key_system, std::move(callback));
}

void CdmFactoryDaemonProxyAsh::ProxyGetHwConfigData(
    GetHwConfigDataCallback callback) {
  if (!daemon_remote_) {
    LOG(ERROR) << "daemon_remote_ interface is not connected";
    std::move(callback).Run(false, std::vector<uint8_t>());
    return;
  }
  daemon_remote_->GetHwConfigData(std::move(callback));
}

void CdmFactoryDaemonProxyAsh::SendDBusRequest(base::ScopedFD fd,
                                               base::OnceClosure callback) {
  chromeos::CdmFactoryDaemonClient::Get()->BootstrapMojoConnection(
      std::move(fd),
      base::BindOnce(&CdmFactoryDaemonProxyAsh::OnBootstrapMojoConnection,
                     base::Unretained(this), std::move(callback)));
}

void CdmFactoryDaemonProxyAsh::OnBootstrapMojoConnection(
    base::OnceClosure callback,
    bool result) {
  if (!mojo_task_runner_->RunsTasksInCurrentSequence()) {
    mojo_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&CdmFactoryDaemonProxyAsh::OnBootstrapMojoConnection,
                       base::Unretained(this), std::move(callback), result));
    return;
  }
  if (!result) {
    LOG(ERROR) << "CdmFactoryDaemon had a failure in D-Bus with the daemon";
    daemon_remote_.reset();
  } else {
    DVLOG(1) << "Succeeded with CdmFactoryDaemon bootstrapping";
  }
  std::move(callback).Run();
}

void CdmFactoryDaemonProxyAsh::CompleteOemCryptoConnection(
    mojo::PendingReceiver<arc::mojom::OemCryptoService> oemcryptor,
    mojo::PendingRemote<arc::mojom::ProtectedBufferManager>
        protected_buffer_manager,
    mojo::PendingRemote<cdm::mojom::OutputProtection> output_protection) {
  if (!daemon_remote_) {
    LOG(ERROR) << "daemon_remote_ interface is not connected";
    // Just let the mojo objects go out of scope and be destructed to signal
    // failure.
    return;
  }
  daemon_remote_->ConnectOemCrypto(std::move(oemcryptor),
                                   std::move(protected_buffer_manager),
                                   std::move(output_protection));
}

void CdmFactoryDaemonProxyAsh::OnDaemonMojoConnectionError() {
  DVLOG(1) << "CdmFactoryDaemon daemon Mojo connection lost.";
  // Reset the remote here to trigger reconnection to the daemon on the next
  // call to CreateFactory.
  daemon_remote_.reset();
}

}  // namespace chromeos
