// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/services/isolated_xr_device/xr_runtime_provider.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/trace_event/trace_event.h"
#include "content/public/common/content_switches.h"
#include "device/base/features.h"
#include "device/vr/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_OPENXR)
#include "content/public/common/gpu_stream_constants.h"
#include "device/vr/openxr/openxr_device.h"
#include "device/vr/openxr/openxr_statics.h"
#include "services/viz/public/cpp/gpu/context_provider_command_buffer.h"
#endif

enum class IsolatedXRRuntimeProvider::RuntimeStatus {
  kEnable,
  kDisable,
};

namespace {
// Poll for device add/remove every 5 seconds.
constexpr base::TimeDelta kTimeBetweenPollingEvents = base::Seconds(5);

template <typename VrDeviceT>
std::unique_ptr<VrDeviceT> CreateDevice() {
  return std::make_unique<VrDeviceT>();
}

template <typename VrDeviceT>
std::unique_ptr<VrDeviceT> EnableRuntime(
    device::mojom::IsolatedXRRuntimeProviderClient* client,
    base::OnceCallback<std::unique_ptr<VrDeviceT>()> create_device) {
  auto device = std::move(create_device).Run();
  TRACE_EVENT_INSTANT1("xr", "HardwareAdded", TRACE_EVENT_SCOPE_THREAD, "id",
                       static_cast<int>(device->GetId()));
  // "Device" here refers to a runtime + hardware pair, not necessarily
  // a physical device.
  client->OnDeviceAdded(device->BindXRRuntime(), device->BindCompositorHost(),
                        device->GetDeviceData(), device->GetId());
  return device;
}

template <typename VrDeviceT>
void DisableRuntime(device::mojom::IsolatedXRRuntimeProviderClient* client,
                    std::unique_ptr<VrDeviceT> device) {
  TRACE_EVENT_INSTANT1("xr", "HardwareRemoved", TRACE_EVENT_SCOPE_THREAD, "id",
                       static_cast<int>(device->GetId()));
  // "Device" here refers to a runtime + hardware pair, not necessarily physical
  // device.
  client->OnDeviceRemoved(device->GetId());
}

template <typename VrHardwareT>
void SetRuntimeStatus(
    device::mojom::IsolatedXRRuntimeProviderClient* client,
    IsolatedXRRuntimeProvider::RuntimeStatus status,
    base::OnceCallback<std::unique_ptr<VrHardwareT>()> create_device,
    std::unique_ptr<VrHardwareT>* out_device) {
  if (status == IsolatedXRRuntimeProvider::RuntimeStatus::kEnable &&
      !*out_device) {
    *out_device = EnableRuntime<VrHardwareT>(client, std::move(create_device));
  } else if (status == IsolatedXRRuntimeProvider::RuntimeStatus::kDisable &&
             *out_device) {
    DisableRuntime(client, std::move(*out_device));
  }
}

// If none of the runtimes are enabled, this function will be unused.
// This is a bit more scalable than wrapping it in all the typedefs
bool ALLOW_UNUSED_TYPE IsEnabled(const base::CommandLine* command_line,
                                 const base::Feature& feature,
                                 const std::string& name) {
  if (!command_line->HasSwitch(switches::kWebXrForceRuntime))
    return base::FeatureList::IsEnabled(feature);

  return (base::CompareCaseInsensitiveASCII(
              command_line->GetSwitchValueASCII(switches::kWebXrForceRuntime),
              name) == 0);
}

}  // namespace

// This function is called periodically to check the availability of hardware
// backed by the various supported VR runtimes. Only one "device" (hardware +
// runtime) should be enabled at once, so this chooses the most preferred among
// available options.
void IsolatedXRRuntimeProvider::PollForDeviceChanges() {
  bool preferred_device_enabled = false;

  // If none of the following runtimes are enabled,
  // we'll get an error for 'preferred_device_enabled' being unused.
  ALLOW_UNUSED_LOCAL(preferred_device_enabled);

#if BUILDFLAG(ENABLE_OPENXR)
  if (!preferred_device_enabled && IsOpenXrHardwareAvailable()) {
    SetOpenXrRuntimeStatus(RuntimeStatus::kEnable);
    preferred_device_enabled = true;
  } else {
    SetOpenXrRuntimeStatus(RuntimeStatus::kDisable);
  }
#endif

  // Schedule this function to run again later.
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&IsolatedXRRuntimeProvider::PollForDeviceChanges,
                     weak_ptr_factory_.GetWeakPtr()),
      kTimeBetweenPollingEvents);
}

void IsolatedXRRuntimeProvider::SetupPollingForDeviceChanges() {
  bool any_runtimes_available = false;
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  // If none of the following runtimes are enabled,
  // we'll get an error for 'command_line' being unused.
  ALLOW_UNUSED_LOCAL(command_line);

#if BUILDFLAG(ENABLE_OPENXR)
  if (IsEnabled(command_line, device::features::kOpenXR,
                switches::kWebXrRuntimeOpenXr)) {
    should_check_openxr_ =
        device::OpenXrStatics::GetInstance()->IsApiAvailable();
    any_runtimes_available |= should_check_openxr_;
  }
#endif

  // Begin polling for devices
  if (any_runtimes_available) {
    PollForDeviceChanges();
  }
}

void IsolatedXRRuntimeProvider::RequestDevices(
    mojo::PendingRemote<device::mojom::IsolatedXRRuntimeProviderClient>
        client) {
  // Start polling to detect devices being added/removed.
  client_.Bind(std::move(client));
  SetupPollingForDeviceChanges();
  client_->OnDevicesEnumerated();
}

#if BUILDFLAG(ENABLE_OPENXR)
bool IsolatedXRRuntimeProvider::IsOpenXrHardwareAvailable() {
  return should_check_openxr_ &&
         device::OpenXrStatics::GetInstance()->IsHardwareAvailable();
}

void IsolatedXRRuntimeProvider::SetOpenXrRuntimeStatus(RuntimeStatus status) {
  auto factory_async = base::BindRepeating(
      &IsolatedXRRuntimeProvider::CreateContextProviderAsync,
      weak_ptr_factory_.GetWeakPtr());
  SetRuntimeStatus(client_.get(), status,
                   base::BindOnce(
                       [](VizContextProviderFactoryAsync factory_async) {
                         return std::make_unique<device::OpenXrDevice>(
                             std::move(factory_async));
                       },
                       std::move(factory_async)),
                   &openxr_device_);
}

// A repeating callback to CreateContextProviderAsync is created in
// SetOpenXrRuntimeStatus and passed to OpenXrDevice. OpenXrRenderLoop posts a
// task with this callback onto the main thread's task runner while it is
// running on the render loop thread's task runner. The context provider and its
// supporting object, viz::Gpu, are required to be created on the main thread's
// task runner. Upon creating the context provider, CreateContextProviderAsync
// posts a callback back to the render loop's thread runner with the newly
// created context provider.
void IsolatedXRRuntimeProvider::CreateContextProviderAsync(
    VizContextProviderCallback viz_context_provider_callback,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
  // viz_gpu_ must be kept alive so long as there are outstanding context
  // providers attached to it, otherwise the GPU process channel gets closed out
  // from under it.
  if (!viz_gpu_ || !viz_gpu_->GetGpuChannel() ||
      viz_gpu_->GetGpuChannel()->IsLost()) {
    mojo::PendingRemote<viz::mojom::Gpu> remote_gpu;
    device_service_host_->BindGpu(remote_gpu.InitWithNewPipeAndPassReceiver());

    viz_gpu_ = viz::Gpu::Create(std::move(remote_gpu), io_task_runner_);

    scoped_refptr<gpu::GpuChannelHost> gpu_channel_host =
        viz_gpu_->EstablishGpuChannelSync();
  }

  scoped_refptr<viz::ContextProvider> context_provider =
      base::MakeRefCounted<viz::ContextProviderCommandBuffer>(
          viz_gpu_->GetGpuChannel(), nullptr /* gpu_memory_buffer_manager */,
          content::kGpuStreamIdDefault, content::kGpuStreamPriorityUI,
          gpu::kNullSurfaceHandle, GURL(std::string("chrome://gpu/XrRuntime")),
          false /* automatic flushes */, false /* support locking */,
          false /* support grcontext */,
          gpu::SharedMemoryLimits::ForMailboxContext(),
          gpu::ContextCreationAttribs(),
          viz::command_buffer_metrics::ContextType::XR_COMPOSITING);
  task_runner->PostTask(FROM_HERE,
                        base::BindOnce(std::move(viz_context_provider_callback),
                                       std::move(context_provider)));
}

#endif  // BUILDFLAG(ENABLE_OPENXR)

IsolatedXRRuntimeProvider::IsolatedXRRuntimeProvider(
    mojo::PendingRemote<device::mojom::XRDeviceServiceHost> device_service_host,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner)
    : device_service_host_(std::move(device_service_host)),
      io_task_runner_(std::move(io_task_runner)) {}

IsolatedXRRuntimeProvider::~IsolatedXRRuntimeProvider() = default;