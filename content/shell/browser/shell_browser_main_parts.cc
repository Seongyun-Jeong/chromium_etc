// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/shell_browser_main_parts.h"

#include <utility>

#include "base/base_switches.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/current_thread.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "cc/base/switches.h"
#include "components/performance_manager/embedder/graph_features.h"
#include "components/performance_manager/embedder/performance_manager_lifetime.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/main_function_params.h"
#include "content/public/common/result_codes.h"
#include "content/public/common/url_constants.h"
#include "content/shell/android/shell_descriptors.h"
#include "content/shell/browser/shell.h"
#include "content/shell/browser/shell_browser_context.h"
#include "content/shell/browser/shell_devtools_manager_delegate.h"
#include "content/shell/browser/shell_platform_delegate.h"
#include "content/shell/common/shell_switches.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"
#include "net/base/filename_util.h"
#include "net/base/net_module.h"
#include "net/grit/net_resources.h"
#include "ui/base/buildflags.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

#if defined(OS_ANDROID)
#include "components/crash/content/browser/child_exit_observer_android.h"
#include "components/crash/content/browser/child_process_crash_observer_android.h"
#include "net/android/network_change_notifier_factory_android.h"
#include "net/base/network_change_notifier.h"
#endif

#if defined(USE_AURA) && (defined(OS_LINUX) || BUILDFLAG(IS_CHROMEOS_LACROS))
#include "ui/base/ime/init/input_method_initializer.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chromeos/dbus/dbus_thread_manager.h"
#include "device/bluetooth/dbus/bluez_dbus_manager.h"
#elif defined(OS_LINUX)
#include "device/bluetooth/dbus/dbus_bluez_manager_wrapper_linux.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chromeos/lacros/dbus/lacros_dbus_thread_manager.h"
#endif

#if BUILDFLAG(USE_GTK)
#include "ui/gtk/gtk_ui_factory.h"
#include "ui/views/linux_ui/linux_ui.h"  // nogncheck
#endif

namespace content {

namespace {

GURL GetStartupURL() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kBrowserTest))
    return GURL();

#if defined(OS_ANDROID)
  // Delay renderer creation on Android until surface is ready.
  return GURL();
#else
  const base::CommandLine::StringVector& args = command_line->GetArgs();
  if (args.empty())
    return GURL("https://www.google.com/");

#if defined(OS_WIN)
  GURL url(base::WideToUTF16(args[0]));
#else
  GURL url(args[0]);
#endif
  if (url.is_valid() && url.has_scheme())
    return url;

  return net::FilePathToFileURL(
      base::MakeAbsoluteFilePath(base::FilePath(args[0])));
#endif
}

scoped_refptr<base::RefCountedMemory> PlatformResourceProvider(int key) {
  if (key == IDR_DIR_HEADER_HTML) {
    return ui::ResourceBundle::GetSharedInstance().LoadDataResourceBytes(
        IDR_DIR_HEADER_HTML);
  }
  return nullptr;
}

}  // namespace

ShellBrowserMainParts::ShellBrowserMainParts(MainFunctionParams parameters)
    : parameters_(std::move(parameters)) {}

ShellBrowserMainParts::~ShellBrowserMainParts() = default;

void ShellBrowserMainParts::PostCreateMainMessageLoop() {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  chromeos::DBusThreadManager::Initialize();
#elif BUILDFLAG(IS_CHROMEOS_LACROS)
  chromeos::LacrosDBusThreadManager::Initialize();
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_CHROMEOS_LACROS)
  bluez::BluezDBusManager::InitializeFake();
#elif defined(OS_LINUX)
  bluez::DBusBluezManagerWrapperLinux::Initialize();
#endif
}

int ShellBrowserMainParts::PreEarlyInitialization() {
#if defined(USE_AURA) && (defined(OS_LINUX) || BUILDFLAG(IS_CHROMEOS_LACROS))
  ui::InitializeInputMethodForTesting();
#endif
#if defined(OS_ANDROID)
  net::NetworkChangeNotifier::SetFactory(
      new net::NetworkChangeNotifierFactoryAndroid());
#endif
  return RESULT_CODE_NORMAL_EXIT;
}

void ShellBrowserMainParts::InitializeBrowserContexts() {
  set_browser_context(new ShellBrowserContext(false));
  set_off_the_record_browser_context(new ShellBrowserContext(true));
}

void ShellBrowserMainParts::InitializeMessageLoopContext() {
  Shell::CreateNewWindow(browser_context_.get(), GetStartupURL(), nullptr,
                         gfx::Size());
}

// Copied from ChromeBrowserMainExtraPartsViewsLinux::ToolkitInitialized().
// See that function for details.
void ShellBrowserMainParts::ToolkitInitialized() {
#if BUILDFLAG(USE_GTK)
  if (switches::IsRunWebTestsSwitchPresent())
    return;

  auto linux_ui = BuildGtkUi();
  linux_ui->Initialize();
  views::LinuxUI::SetInstance(std::move(linux_ui));
#endif
}

int ShellBrowserMainParts::PreCreateThreads() {
#if defined(OS_ANDROID)
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  crash_reporter::ChildExitObserver::Create();
  if (command_line->HasSwitch(switches::kEnableCrashReporter)) {
    crash_reporter::ChildExitObserver::GetInstance()->RegisterClient(
        std::make_unique<crash_reporter::ChildProcessCrashObserver>());
  }
#endif
  return 0;
}

void ShellBrowserMainParts::PostCreateThreads() {
  performance_manager_lifetime_ =
      std::make_unique<performance_manager::PerformanceManagerLifetime>(
          performance_manager::GraphFeatures::WithMinimal(), base::DoNothing());
}

int ShellBrowserMainParts::PreMainMessageLoopRun() {
  InitializeBrowserContexts();
  Shell::Initialize(CreateShellPlatformDelegate());
  net::NetModule::SetResourceProvider(PlatformResourceProvider);
  ShellDevToolsManagerDelegate::StartHttpHandler(browser_context_.get());
  InitializeMessageLoopContext();
  return 0;
}

void ShellBrowserMainParts::WillRunMainMessageLoop(
    std::unique_ptr<base::RunLoop>& run_loop) {
  Shell::SetMainMessageLoopQuitClosure(run_loop->QuitClosure());
}

void ShellBrowserMainParts::PostMainMessageLoopRun() {
  DCHECK_EQ(Shell::windows().size(), 0u);
  ShellDevToolsManagerDelegate::StopHttpHandler();
  browser_context_.reset();
  off_the_record_browser_context_.reset();
#if BUILDFLAG(USE_GTK)
  views::LinuxUI::SetInstance(nullptr);
#endif
  performance_manager_lifetime_.reset();
}

void ShellBrowserMainParts::PostDestroyThreads() {
#if BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_CHROMEOS_LACROS)
  device::BluetoothAdapterFactory::Shutdown();
  bluez::BluezDBusManager::Shutdown();
#elif defined(OS_LINUX)
  device::BluetoothAdapterFactory::Shutdown();
  bluez::DBusBluezManagerWrapperLinux::Shutdown();
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
  chromeos::DBusThreadManager::Shutdown();
#elif BUILDFLAG(IS_CHROMEOS_LACROS)
  chromeos::LacrosDBusThreadManager::Shutdown();
#endif
}

std::unique_ptr<ShellPlatformDelegate>
ShellBrowserMainParts::CreateShellPlatformDelegate() {
  return std::make_unique<ShellPlatformDelegate>();
}

}  // namespace content
