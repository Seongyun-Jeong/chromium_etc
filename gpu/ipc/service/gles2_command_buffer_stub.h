// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_SERVICE_GLES2_COMMAND_BUFFER_STUB_H_
#define GPU_IPC_SERVICE_GLES2_COMMAND_BUFFER_STUB_H_

#include "base/containers/circular_deque.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "gpu/ipc/service/command_buffer_stub.h"
#include "gpu/ipc/service/image_transport_surface_delegate.h"
#include "ui/gfx/gpu_fence_handle.h"

namespace gpu {

struct Mailbox;

class GPU_IPC_SERVICE_EXPORT GLES2CommandBufferStub
    : public CommandBufferStub,
      public ImageTransportSurfaceDelegate,
      public base::SupportsWeakPtr<GLES2CommandBufferStub> {
 public:
  GLES2CommandBufferStub(GpuChannel* channel,
                         const mojom::CreateCommandBufferParams& init_params,
                         CommandBufferId command_buffer_id,
                         SequenceId sequence_id,
                         int32_t stream_id,
                         int32_t route_id);

  GLES2CommandBufferStub(const GLES2CommandBufferStub&) = delete;
  GLES2CommandBufferStub& operator=(const GLES2CommandBufferStub&) = delete;

  ~GLES2CommandBufferStub() override;

  // This must leave the GL context associated with the newly-created
  // CommandBufferStub current, so the GpuChannel can initialize
  // the gpu::Capabilities.
  gpu::ContextResult Initialize(
      CommandBufferStub* share_group,
      const mojom::CreateCommandBufferParams& init_params,
      base::UnsafeSharedMemoryRegion shared_state_shm) override;
  MemoryTracker* GetContextGroupMemoryTracker() const override;

  // DecoderClient implementation.
  void OnGpuSwitched(gl::GpuPreference active_gpu_heuristic) override;

// ImageTransportSurfaceDelegate implementation:
#if defined(OS_WIN)
  void DidCreateAcceleratedSurfaceChildWindow(
      SurfaceHandle parent_window,
      SurfaceHandle child_window) override;
#endif
  void DidSwapBuffersComplete(SwapBuffersCompleteParams params,
                              gfx::GpuFenceHandle release_fence) override;
  const gles2::FeatureInfo* GetFeatureInfo() const override;
  const GpuPreferences& GetGpuPreferences() const override;
  void BufferPresented(const gfx::PresentationFeedback& feedback) override;
  viz::GpuVSyncCallback GetGpuVSyncCallback() override;
  base::TimeDelta GetGpuBlockedTimeSinceLastSwap() override;

 private:
  // CommandBufferStub overrides:
  void OnTakeFrontBuffer(const Mailbox& mailbox) override;
  void OnReturnFrontBuffer(const Mailbox& mailbox, bool is_lost) override;
  void CreateGpuFenceFromHandle(uint32_t id,
                                gfx::GpuFenceHandle handle) override;
  void GetGpuFenceHandle(uint32_t gpu_fence_id,
                         GetGpuFenceHandleCallback callback) override;
  void CreateImage(mojom::CreateImageParamsPtr params) override;
  void DestroyImage(int32_t id) override;

  void OnSwapBuffers(uint64_t swap_id, uint32_t flags) override;

  // The group of contexts that share namespaces with this context.
  scoped_refptr<gles2::ContextGroup> context_group_;

  // Keep a more specifically typed reference to the decoder to avoid
  // unnecessary casts. Owned by parent class.
  raw_ptr<gles2::GLES2Decoder> gles2_decoder_;

  // Params pushed each time we call OnSwapBuffers, and popped when a buffer
  // is presented or a swap completed.
  struct SwapBufferParams {
    uint64_t swap_id;
    uint32_t flags;
  };
  base::circular_deque<SwapBufferParams> pending_presented_params_;
  base::circular_deque<SwapBufferParams> pending_swap_completed_params_;

  base::WeakPtrFactory<GLES2CommandBufferStub> weak_ptr_factory_{this};
};

}  // namespace gpu

#endif  // GPU_IPC_SERVICE_GLES2_COMMAND_BUFFER_STUB_H_
