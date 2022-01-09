// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/service/gpu_channel_test_common.h"
#include "base/memory/raw_ptr.h"

#include <memory>

#include "base/ignore_result.h"
#include "base/memory/unsafe_shared_memory_region.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/test_simple_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/unguessable_token.h"
#include "build/build_config.h"
#include "gpu/command_buffer/common/activity_flags.h"
#include "gpu/command_buffer/service/scheduler.h"
#include "gpu/command_buffer/service/shared_image_manager.h"
#include "gpu/command_buffer/service/sync_point_manager.h"
#include "gpu/ipc/service/context_url.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "gpu/ipc/service/gpu_channel_manager.h"
#include "gpu/ipc/service/gpu_channel_manager_delegate.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "ui/gl/init/gl_factory.h"
#include "ui/gl/test/gl_surface_test_support.h"
#include "url/gurl.h"

namespace gpu {

class TestGpuChannelManagerDelegate : public GpuChannelManagerDelegate {
 public:
  TestGpuChannelManagerDelegate(Scheduler* scheduler) : scheduler_(scheduler) {}

  TestGpuChannelManagerDelegate(const TestGpuChannelManagerDelegate&) = delete;
  TestGpuChannelManagerDelegate& operator=(
      const TestGpuChannelManagerDelegate&) = delete;

  ~TestGpuChannelManagerDelegate() override = default;

  // GpuChannelManagerDelegate implementation:
  void RegisterDisplayContext(gpu::DisplayContext* context) override {}
  void UnregisterDisplayContext(gpu::DisplayContext* context) override {}
  void LoseAllContexts() override {}
  void DidCreateContextSuccessfully() override {}
  void DidCreateOffscreenContext(const GURL& active_url) override {}
  void DidDestroyChannel(int client_id) override {}
  void DidDestroyAllChannels() override {}
  void DidDestroyOffscreenContext(const GURL& active_url) override {}
  void DidLoseContext(bool offscreen,
                      error::ContextLostReason reason,
                      const GURL& active_url) override {}
  void StoreShaderToDisk(int32_t client_id,
                         const std::string& key,
                         const std::string& shader) override {}
  void MaybeExitOnContextLost() override { is_exiting_ = true; }
  bool IsExiting() const override { return is_exiting_; }
#if defined(OS_WIN)
  void DidUpdateOverlayInfo(const gpu::OverlayInfo& overlay_info) override {}
  void DidUpdateHDRStatus(bool hdr_enabled) override {}
  void SendCreatedChildWindow(SurfaceHandle parent_window,
                              SurfaceHandle child_window) override {}
#endif

  Scheduler* GetGpuScheduler() override { return scheduler_; }

 private:
  bool is_exiting_ = false;
  const raw_ptr<Scheduler> scheduler_;
};

GpuChannelTestCommon::GpuChannelTestCommon(bool use_stub_bindings)
    : GpuChannelTestCommon(std::vector<int32_t>(), use_stub_bindings) {}

GpuChannelTestCommon::GpuChannelTestCommon(
    std::vector<int32_t> enabled_workarounds,
    bool use_stub_bindings)
    : memory_dump_manager_(
          base::trace_event::MemoryDumpManager::CreateInstanceForTesting()),
      sync_point_manager_(new SyncPointManager()),
      shared_image_manager_(new SharedImageManager(false /* thread_safe */)),
      scheduler_(new Scheduler(sync_point_manager_.get(), GpuPreferences())),
      channel_manager_delegate_(
          new TestGpuChannelManagerDelegate(scheduler_.get())) {
  // We need GL bindings to actually initialize command buffers.
  if (use_stub_bindings) {
    gl::GLSurfaceTestSupport::InitializeOneOffWithStubBindings();
  } else {
    gl::GLSurfaceTestSupport::InitializeOneOff();
  }

  GpuFeatureInfo feature_info;
  feature_info.enabled_gpu_driver_bug_workarounds =
      std::move(enabled_workarounds);

  channel_manager_ = std::make_unique<GpuChannelManager>(
      GpuPreferences(), channel_manager_delegate_.get(), nullptr, /* watchdog */
      task_environment_.GetMainThreadTaskRunner(),
      task_environment_.GetMainThreadTaskRunner(), scheduler_.get(),
      sync_point_manager_.get(), shared_image_manager_.get(),
      nullptr, /* gpu_memory_buffer_factory */
      std::move(feature_info), GpuProcessActivityFlags(),
      gl::init::CreateOffscreenGLSurface(gfx::Size()),
      nullptr /* image_decode_accelerator_worker */);
}

GpuChannelTestCommon::~GpuChannelTestCommon() {
  // Command buffers can post tasks and run GL in destruction so do this first.
  channel_manager_ = nullptr;
  task_environment_.RunUntilIdle();
  gl::init::ShutdownGL(false);
}

GpuChannel* GpuChannelTestCommon::CreateChannel(int32_t client_id,
                                                bool is_gpu_host) {
  uint64_t kClientTracingId = 1;
  GpuChannel* channel = channel_manager()->EstablishChannel(
      base::UnguessableToken::Create(), client_id, kClientTracingId,
      is_gpu_host, true);
  base::ProcessId kProcessId = 1;
  channel->set_client_pid(kProcessId);
  return channel;
}

void GpuChannelTestCommon::CreateCommandBuffer(
    GpuChannel& channel,
    mojom::CreateCommandBufferParamsPtr init_params,
    int32_t routing_id,
    base::UnsafeSharedMemoryRegion shared_state,
    ContextResult* out_result,
    Capabilities* out_capabilities) {
  base::RunLoop loop;
  auto quit = loop.QuitClosure();
  mojo::PendingAssociatedRemote<mojom::CommandBuffer> remote;
  mojo::PendingAssociatedRemote<mojom::CommandBufferClient> client;
  ignore_result(client.InitWithNewEndpointAndPassReceiver());
  client.EnableUnassociatedUsage();
  channel.CreateCommandBuffer(
      std::move(init_params), routing_id, std::move(shared_state),
      remote.InitWithNewEndpointAndPassReceiver(), std::move(client),
      base::BindLambdaForTesting(
          [&](ContextResult result, const Capabilities& capabilities) {
            *out_result = result;
            *out_capabilities = capabilities;
            quit.Run();
          }));
  loop.Run();
}

base::UnsafeSharedMemoryRegion GpuChannelTestCommon::GetSharedMemoryRegion() {
  return base::UnsafeSharedMemoryRegion::Create(
      sizeof(CommandBufferSharedState));
}

}  // namespace gpu
