// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/updater/app/server/win/service_main.h"

#include <atlsecurity.h>
#include <sddl.h>

#include <string>
#include <type_traits>

#include "base/command_line.h"
#include "base/cxx17_backports.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_executor.h"
#include "base/win/scoped_com_initializer.h"
#include "chrome/updater/app/server/win/com_classes.h"
#include "chrome/updater/app/server/win/com_classes_legacy.h"
#include "chrome/updater/app/server/win/server.h"
#include "chrome/updater/constants.h"
#include "chrome/updater/win/win_constants.h"
#include "chrome/updater/win/win_util.h"

namespace updater {

namespace {

// Command line switch "--console" runs the service interactively for
// debugging purposes.
constexpr char kConsoleSwitchName[] = "console";

bool IsInternalService() {
  return base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
             kServerServiceSwitch) == kServerUpdateServiceInternalSwitchValue;
}

}  // namespace

int ServiceMain::RunComService(const base::CommandLine* command_line) {
  base::win::ScopedCOMInitializer com_initializer(
      base::win::ScopedCOMInitializer::kMTA);
  if (!com_initializer.Succeeded()) {
    LOG(ERROR) << "Failed to initialize COM";
    return CO_E_INITIALIZATIONFAILED;
  }

  // Run the COM service.
  ServiceMain* service = ServiceMain::GetInstance();
  if (!service->InitWithCommandLine(command_line))
    return ERROR_BAD_ARGUMENTS;

  int ret = service->Start();
  DCHECK_NE(ret, int{STILL_ACTIVE});
  return ret;
}

ServiceMain* ServiceMain::GetInstance() {
  static base::NoDestructor<ServiceMain> instance;
  return instance.get();
}

bool ServiceMain::InitWithCommandLine(const base::CommandLine* command_line) {
  const base::CommandLine::StringVector args = command_line->GetArgs();
  if (!args.empty()) {
    LOG(ERROR) << "No positional parameters expected.";
    return false;
  }

  // Run interactively if needed.
  if (command_line->HasSwitch(kConsoleSwitchName))
    run_routine_ = &ServiceMain::RunInteractive;

  return true;
}

// Start() is the entry point called by WinMain.
int ServiceMain::Start() {
  return (this->*run_routine_)();
}

ServiceMain::ServiceMain() {
  service_status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  service_status_.dwCurrentState = SERVICE_STOPPED;
  service_status_.dwControlsAccepted = SERVICE_ACCEPT_STOP;
}

ServiceMain::~ServiceMain() = default;

int ServiceMain::RunAsService() {
  const std::wstring service_name = GetServiceName(IsInternalService());
  const SERVICE_TABLE_ENTRY dispatch_table[] = {
      {const_cast<LPTSTR>(service_name.c_str()),
       &ServiceMain::ServiceMainEntry},
      {nullptr, nullptr}};

  if (!::StartServiceCtrlDispatcher(dispatch_table)) {
    service_status_.dwWin32ExitCode = ::GetLastError();
    PLOG(ERROR) << "Failed to connect to the service control manager";
  }

  return service_status_.dwWin32ExitCode;
}

void ServiceMain::ServiceMainImpl() {
  service_status_handle_ =
      ::RegisterServiceCtrlHandler(GetServiceName(IsInternalService()).c_str(),
                                   &ServiceMain::ServiceControlHandler);
  if (service_status_handle_ == nullptr) {
    PLOG(ERROR) << "RegisterServiceCtrlHandler failed";
    return;
  }
  SetServiceStatus(SERVICE_RUNNING);

  // When the Run function returns, the service has stopped.
  // `hr` can be either a HRESULT or a Windows error code.
  const HRESULT hr = Run();
  if (hr != S_OK) {
    service_status_.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
    service_status_.dwServiceSpecificExitCode = hr;
  }

  SetServiceStatus(SERVICE_STOPPED);
}

int ServiceMain::RunInteractive() {
  return Run();
}

// static
void ServiceMain::ServiceControlHandler(DWORD control) {
  ServiceMain* self = ServiceMain::GetInstance();
  switch (control) {
    case SERVICE_CONTROL_STOP:
      self->SetServiceStatus(SERVICE_STOP_PENDING);
      AppServerSingletonInstance()->Stop();
      break;

    default:
      break;
  }
}

// static
void WINAPI ServiceMain::ServiceMainEntry(DWORD argc, wchar_t* argv[]) {
  ServiceMain::GetInstance()->ServiceMainImpl();
}

void ServiceMain::SetServiceStatus(DWORD state) {
  ::InterlockedExchange(&service_status_.dwCurrentState, state);
  ::SetServiceStatus(service_status_handle_, &service_status_);
}

HRESULT ServiceMain::Run() {
  base::SingleThreadTaskExecutor service_task_executor(
      base::MessagePumpType::UI);

  // Initialize COM for the current thread.
  base::win::ScopedCOMInitializer com_initializer(
      base::win::ScopedCOMInitializer::kMTA);
  if (!com_initializer.Succeeded()) {
    LOG(ERROR) << "Failed to initialize COM";
    return CO_E_INITIALIZATIONFAILED;
  }

  HRESULT hr = InitializeComSecurity();
  if (FAILED(hr))
    return hr;

  return AppServerSingletonInstance()->Run();
}

// static
HRESULT ServiceMain::InitializeComSecurity() {
  CDacl dacl;
  constexpr auto com_rights_execute_local =
      COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_LOCAL;
  dacl.AddAllowedAce(Sids::System(), com_rights_execute_local);
  dacl.AddAllowedAce(Sids::Admins(), com_rights_execute_local);
  dacl.AddAllowedAce(Sids::Interactive(), com_rights_execute_local);

  CSecurityDesc sd;
  sd.SetDacl(dacl);
  sd.MakeAbsolute();
  sd.SetOwner(Sids::Admins());
  sd.SetGroup(Sids::Admins());

  return ::CoInitializeSecurity(
      const_cast<SECURITY_DESCRIPTOR*>(sd.GetPSECURITY_DESCRIPTOR()), -1,
      nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IDENTIFY,
      nullptr, EOAC_DYNAMIC_CLOAKING | EOAC_NO_CUSTOM_MARSHAL, nullptr);
}

}  // namespace updater
