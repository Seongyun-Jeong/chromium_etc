// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/common/gl_nv12_converter.h"

#include "base/memory/ptr_util.h"
#include "components/viz/common/gl_i420_converter.h"
#include "components/viz/common/gpu/context_provider.h"

namespace viz {

// static
std::unique_ptr<GLNV12Converter> GLNV12Converter::CreateConverterForTest(
    ContextProvider* context_provider,
    bool allow_mrt_path) {
  return base::WrapUnique(
      new GLNV12Converter(context_provider, allow_mrt_path));
}

GLNV12Converter::GLNV12Converter(ContextProvider* context_provider)
    : GLNV12Converter(context_provider, true) {}

GLNV12Converter::GLNV12Converter(ContextProvider* context_provider,
                                 bool allow_mrt_path)
    : context_provider_(context_provider),
      step1_(context_provider_),
      step2_(context_provider_) {
  DCHECK(context_provider_);
  context_provider_->AddObserver(this);
  if (!allow_mrt_path || step1_.GetMaxDrawBuffersSupported() < 2) {
    step3_ = std::make_unique<GLScaler>(context_provider_);
  }
}

GLNV12Converter::~GLNV12Converter() {
  OnContextLost();  // Free context-related resources.
}

// static
gfx::Rect GLNV12Converter::ToAlignedRect(const gfx::Rect& rect) {
  // Origin coordinates: FLOOR(...)
  const int aligned_x =
      ((rect.x() < 0) ? ((rect.x() - 3) / 4) : (rect.x() / 4)) * 4;
  const int aligned_y =
      ((rect.y() < 0) ? ((rect.y() - 1) / 2) : (rect.y() / 2)) * 2;
  // Span coordinates: CEIL(...)
  const int aligned_right =
      ((rect.right() < 0) ? (rect.right() / 4) : ((rect.right() + 3) / 4)) * 4;
  const int aligned_bottom =
      ((rect.bottom() < 0) ? (rect.bottom() / 2) : ((rect.bottom() + 1) / 2)) *
      2;
  return gfx::Rect(aligned_x, aligned_y, aligned_right - aligned_x,
                   aligned_bottom - aligned_y);
}

// static
bool GLNV12Converter::ParametersAreEquivalent(const Parameters& a,
                                              const Parameters& b) {
  // Implemented in terms of GLI420Converter:
  return GLI420Converter::ParametersAreEquivalent(a, b);
}

void GLNV12Converter::EnsureIntermediateTextureDefined(
    const gfx::Size& required) {
  if (intermediate_texture_size_ == required) {
    return;
  }
  auto* const gl = context_provider_->ContextGL();
  if (intermediate_texture_ == 0) {
    gl->GenTextures(1, &intermediate_texture_);
  }
  gl->BindTexture(GL_TEXTURE_2D, intermediate_texture_);
  gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, required.width(), required.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  intermediate_texture_size_ = required;
}

bool GLNV12Converter::Configure(const Parameters& params) {
  Parameters step1_params = params;
  if (!step1_params.output_color_space.IsValid()) {
    step1_params.output_color_space = gfx::ColorSpace::CreateREC709();
  }

  // Configure the "step 1" scaler.
  if (is_using_mrt_path()) {
    step1_params.export_format = Parameters::ExportFormat::NV61;
    DCHECK_EQ(step1_params.swizzle[0], params.swizzle[0]);
    step1_params.swizzle[1] = GL_RGBA;  // Don't swizzle 2nd rendering target.
  } else {
    step1_params.export_format = Parameters::ExportFormat::INTERLEAVED_QUADS;
    step1_params.swizzle[0] = GL_RGBA;  // Will swizzle in steps 2-3.
  }
  if (!step1_.Configure(step1_params)) {
    return false;
  }

  // Configure the "step 2" scaler (and step 3 for the non-MRT path) that
  // further transform the output from the "step 1" scaler to produce the final
  // outputs.
  Parameters step2_params;
  step2_params.scale_to = gfx::Vector2d(1, 1);
  step2_params.source_color_space = step1_params.output_color_space;
  step2_params.output_color_space = step1_params.output_color_space;
  // Use FAST quality, a single bilinear pass, because there will either be no
  // scaling or exactly 50% scaling.
  step2_params.quality = Parameters::Quality::FAST;
  step2_params.swizzle[0] = params.swizzle[0];
  if (is_using_mrt_path()) {
    // NV61 provides half-width and full-height U/V. NV12 UV planes are
    // half-width and half-height. So, scale just the Y by 50%.
    step2_params.scale_from = gfx::Vector2d(1, 2);
    step2_params.export_format = Parameters::ExportFormat::INTERLEAVED_QUADS;
    step2_params.swizzle[1] = step2_params.swizzle[0];
    if (!step2_.Configure(step2_params)) {
      return false;
    }
  } else {
    // Extract a full-size Y plane from the interleaved YUVA from step 1.
    step2_params.scale_from = gfx::Vector2d(1, 1);
    step2_params.export_format = Parameters::ExportFormat::CHANNEL_0;
    if (!step2_.Configure(step2_params)) {
      return false;
    }
    // Extract a half-size UV plane from the interleaved YUVA from step 1.
    // UV_CHANNELS provides half-width and full-height UV plane. NV12 UV planes
    // are half-wifth and half-height. So, scale just the Y by 50%.
    step2_params.scale_from = gfx::Vector2d(1, 2);
    step2_params.export_format = Parameters::ExportFormat::UV_CHANNELS;
    if (!step3_->Configure(step2_params)) {
      return false;
    }
  }

  params_ = params;
  return true;
}

bool GLNV12Converter::Convert(GLuint src_texture,
                              const gfx::Size& src_texture_size,
                              const gfx::Vector2d& src_offset,
                              const gfx::Rect& aligned_output_rect,
                              const GLuint yuv_textures[2]) {
  DCHECK_EQ(aligned_output_rect.x() % 4, 0);
  DCHECK_EQ(aligned_output_rect.width() % 4, 0);
  DCHECK_EQ(aligned_output_rect.y() % 2, 0);
  DCHECK_EQ(aligned_output_rect.height() % 2, 0);

  if (!context_provider_) {
    return false;
  }

  if (is_using_mrt_path()) {
    const gfx::Rect luma_output_rect(
        aligned_output_rect.x() / 4, aligned_output_rect.y(),
        aligned_output_rect.width() / 4, aligned_output_rect.height());
    EnsureIntermediateTextureDefined(luma_output_rect.size());
    const gfx::Rect chroma_output_rect(
        gfx::Size(luma_output_rect.width(), luma_output_rect.height() / 2));
    return (step1_.ScaleToMultipleOutputs(
                src_texture, src_texture_size, src_offset, yuv_textures[0],
                intermediate_texture_, luma_output_rect) &&
            step2_.Scale(intermediate_texture_, intermediate_texture_size_,
                         gfx::Vector2d(), yuv_textures[1], chroma_output_rect));
  }

  // Non-MRT path:
  EnsureIntermediateTextureDefined(aligned_output_rect.size());
  const gfx::Rect luma_output_rect(0, 0, aligned_output_rect.width() / 4,
                                   aligned_output_rect.height());
  const gfx::Rect chroma_output_rect(0, 0, luma_output_rect.width(),
                                     luma_output_rect.height() / 2);
  return (step1_.Scale(src_texture, src_texture_size, src_offset,
                       intermediate_texture_, aligned_output_rect) &&
          step2_.Scale(intermediate_texture_, intermediate_texture_size_,
                       gfx::Vector2d(), yuv_textures[0], luma_output_rect) &&
          step3_->Scale(intermediate_texture_, intermediate_texture_size_,
                        gfx::Vector2d(), yuv_textures[1], chroma_output_rect));
}

void GLNV12Converter::OnContextLost() {
  if (intermediate_texture_ != 0) {
    if (auto* gl = context_provider_->ContextGL()) {
      gl->DeleteTextures(1, &intermediate_texture_);
    }
    intermediate_texture_ = 0;
    intermediate_texture_size_ = gfx::Size();
  }
  if (context_provider_) {
    context_provider_->RemoveObserver(this);
    context_provider_ = nullptr;
  }
}

}  // namespace viz
