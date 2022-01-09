// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_SKIA_OUTPUT_DEVICE_OFFSCREEN_H_
#define COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_SKIA_OUTPUT_DEVICE_OFFSCREEN_H_

#include <vector>

#include "components/viz/service/display_embedder/skia_output_device.h"
#include "gpu/command_buffer/service/shared_context_state.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "third_party/skia/include/core/SkImageInfo.h"

namespace viz {

class SkiaOutputDeviceOffscreen : public SkiaOutputDevice {
 public:
  SkiaOutputDeviceOffscreen(
      scoped_refptr<gpu::SharedContextState> context_state,
      gfx::SurfaceOrigin origin,
      bool has_alpha,
      gpu::MemoryTracker* memory_tracker,
      DidSwapBufferCompleteCallback did_swap_buffer_complete_callback);

  SkiaOutputDeviceOffscreen(const SkiaOutputDeviceOffscreen&) = delete;
  SkiaOutputDeviceOffscreen& operator=(const SkiaOutputDeviceOffscreen&) =
      delete;

  ~SkiaOutputDeviceOffscreen() override;

  // SkiaOutputDevice implementation:
  bool Reshape(const gfx::Size& size,
               float device_scale_factor,
               const gfx::ColorSpace& color_space,
               gfx::BufferFormat format,
               gfx::OverlayTransform transform) override;
  void SwapBuffers(BufferPresentedCallback feedback,
                   OutputSurfaceFrame frame) override;
  void PostSubBuffer(const gfx::Rect& rect,
                     BufferPresentedCallback feedback,
                     OutputSurfaceFrame frame) override;
  void EnsureBackbuffer() override;
  void DiscardBackbuffer() override;
  SkSurface* BeginPaint(
      bool allocate_frame_buffer,
      std::vector<GrBackendSemaphore>* end_semaphores) override;
  void EndPaint() override;

 protected:
  scoped_refptr<gpu::SharedContextState> context_state_;
  const bool has_alpha_;
  sk_sp<SkSurface> sk_surface_;
  GrBackendTexture backend_texture_;
  bool supports_rgbx_ = true;
  gfx::Size size_;
  gfx::BufferFormat format_ = gfx::BufferFormat::RGBA_8888;
  sk_sp<SkColorSpace> sk_color_space_;

 private:
  uint64_t backbuffer_estimated_size_ = 0;
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_SKIA_OUTPUT_DEVICE_OFFSCREEN_H_
