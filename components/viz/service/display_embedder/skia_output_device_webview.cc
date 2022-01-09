// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/display_embedder/skia_output_device_webview.h"

#include <utility>

#include "gpu/command_buffer/common/swap_buffers_complete_params.h"
#include "gpu/command_buffer/service/gl_utils.h"
#include "gpu/command_buffer/service/shared_context_state.h"
#include "skia/ext/legacy_display_globals.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "ui/gfx/buffer_format_util.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_surface.h"

namespace viz {

namespace {
constexpr auto kSurfaceColorType = kRGBA_8888_SkColorType;
}

SkiaOutputDeviceWebView::SkiaOutputDeviceWebView(
    gpu::SharedContextState* context_state,
    scoped_refptr<gl::GLSurface> gl_surface,
    gpu::MemoryTracker* memory_tracker,
    DidSwapBufferCompleteCallback did_swap_buffer_complete_callback)
    : SkiaOutputDevice(context_state->gr_context(),
                       memory_tracker,
                       std::move(did_swap_buffer_complete_callback)),
      context_state_(context_state),
      gl_surface_(std::move(gl_surface)) {
  // Always set uses_default_gl_framebuffer to true, since
  // SkSurfaceCharacterization created for  GL fbo0 is compatible with
  // SkSurface wrappers non GL fbo0.
  capabilities_.uses_default_gl_framebuffer = true;
  capabilities_.output_surface_origin = gl_surface_->GetOrigin();
  capabilities_.pending_swap_params.max_pending_swaps =
      gl_surface_->GetBufferCount() - 1;

  DCHECK(context_state_->gr_context());
  DCHECK(context_state_->context());

  capabilities_.sk_color_types[static_cast<int>(gfx::BufferFormat::RGBA_8888)] =
      kSurfaceColorType;
  capabilities_.sk_color_types[static_cast<int>(gfx::BufferFormat::BGRA_8888)] =
      kSurfaceColorType;
}

SkiaOutputDeviceWebView::~SkiaOutputDeviceWebView() = default;

bool SkiaOutputDeviceWebView::Reshape(const gfx::Size& size,
                                      float device_scale_factor,
                                      const gfx::ColorSpace& color_space,
                                      gfx::BufferFormat format,
                                      gfx::OverlayTransform transform) {
  DCHECK_EQ(transform, gfx::OVERLAY_TRANSFORM_NONE);

  if (!gl_surface_->Resize(size, device_scale_factor, color_space,
                           gfx::AlphaBitsForBufferFormat(format))) {
    DLOG(ERROR) << "Failed to resize.";
    return false;
  }

  size_ = size;
  color_space_ = color_space;
  InitSkiaSurface(gl_surface_->GetBackingFramebufferObject());
  return !!sk_surface_;
}

void SkiaOutputDeviceWebView::SwapBuffers(BufferPresentedCallback feedback,
                                          OutputSurfaceFrame frame) {
  StartSwapBuffers({});

  gfx::Size surface_size =
      gfx::Size(sk_surface_->width(), sk_surface_->height());

  FinishSwapBuffers(
      gfx::SwapCompletionResult(gl_surface_->SwapBuffers(std::move(feedback))),
      surface_size, std::move(frame));
}

SkSurface* SkiaOutputDeviceWebView::BeginPaint(
    bool allocate_frame_buffer,
    std::vector<GrBackendSemaphore>* end_semaphores) {
  DCHECK(!allocate_frame_buffer);
  DCHECK(sk_surface_);

  unsigned int fbo = gl_surface_->GetBackingFramebufferObject();

  if (last_frame_buffer_object_ != fbo) {
    InitSkiaSurface(fbo);
  }

  return sk_surface_.get();
}

void SkiaOutputDeviceWebView::EndPaint() {}

void SkiaOutputDeviceWebView::InitSkiaSurface(unsigned int fbo) {
  last_frame_buffer_object_ = fbo;

  GrGLFramebufferInfo framebuffer_info;
  framebuffer_info.fFBOID = fbo;
  framebuffer_info.fFormat = GL_RGBA8;
  SkColorType color_type = kSurfaceColorType;

  GrBackendRenderTarget render_target(size_.width(), size_.height(),
                                      /*sampleCnt=*/0,
                                      /*stencilBits=*/0, framebuffer_info);
  auto origin = (gl_surface_->GetOrigin() == gfx::SurfaceOrigin::kTopLeft)
                    ? kTopLeft_GrSurfaceOrigin
                    : kBottomLeft_GrSurfaceOrigin;

  SkSurfaceProps surface_props{0, kUnknown_SkPixelGeometry};
  sk_surface_ = SkSurface::MakeFromBackendRenderTarget(
      context_state_->gr_context(), render_target, origin, color_type,
      color_space_.ToSkColorSpace(), &surface_props);

  if (!sk_surface_) {
    LOG(ERROR) << "Couldn't create surface: "
               << context_state_->gr_context()->abandoned() << " " << color_type
               << " " << framebuffer_info.fFBOID << " "
               << framebuffer_info.fFormat << " " << color_space_.ToString()
               << " " << size_.ToString();
  }
}

}  // namespace viz
