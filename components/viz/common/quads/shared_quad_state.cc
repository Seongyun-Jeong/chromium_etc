// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/common/quads/shared_quad_state.h"

#include "base/trace_event/trace_event.h"
#include "base/trace_event/traced_value.h"
#include "base/values.h"
#include "cc/base/math_util.h"
#include "components/viz/common/traced_value.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/skia/include/core/SkBlendMode.h"

namespace viz {

SharedQuadState::SharedQuadState() = default;
SharedQuadState::SharedQuadState(const SharedQuadState& other) = default;
SharedQuadState::~SharedQuadState() {
  TRACE_EVENT_OBJECT_DELETED_WITH_ID(TRACE_DISABLED_BY_DEFAULT("viz.quads"),
                                     "viz::SharedQuadState", this);
}

void SharedQuadState::SetAll(const gfx::Transform& transform,
                             const gfx::Rect& layer_rect,
                             const gfx::Rect& visible_layer_rect,
                             const gfx::MaskFilterInfo& filter_info,
                             const absl::optional<gfx::Rect>& clip,
                             bool contents_opaque,
                             float opacity_f,
                             SkBlendMode blend,
                             int sorting_context) {
  quad_to_target_transform = transform;
  quad_layer_rect = layer_rect;
  visible_quad_layer_rect = visible_layer_rect;
  mask_filter_info = filter_info;
  clip_rect = clip;
  are_contents_opaque = contents_opaque;
  opacity = opacity_f;
  blend_mode = blend;
  sorting_context_id = sorting_context;
}

void SharedQuadState::AsValueInto(base::trace_event::TracedValue* value) const {
  cc::MathUtil::AddToTracedValue("transform", quad_to_target_transform, value);
  cc::MathUtil::AddToTracedValue("layer_content_rect", quad_layer_rect, value);
  cc::MathUtil::AddToTracedValue("layer_visible_content_rect",
                                 visible_quad_layer_rect, value);
  cc::MathUtil::AddToTracedValue("mask_filter_bounds",
                                 mask_filter_info.bounds(), value);
  cc::MathUtil::AddCornerRadiiToTracedValue(
      "mask_filter_rounded_corners_radii",
      mask_filter_info.rounded_corner_bounds(), value);

  if (clip_rect) {
    cc::MathUtil::AddToTracedValue("clip_rect", *clip_rect, value);
  }

  value->SetBoolean("are_contents_opaque", are_contents_opaque);
  value->SetDouble("opacity", opacity);
  value->SetString("blend_mode", SkBlendMode_Name(blend_mode));
  value->SetInteger("sorting_context_id", sorting_context_id);
  value->SetBoolean("is_fast_rounded_corner", is_fast_rounded_corner);
  value->SetDouble("de_jelly_delta_y", de_jelly_delta_y);
  TracedValue::MakeDictIntoImplicitSnapshotWithCategory(
      TRACE_DISABLED_BY_DEFAULT("viz.quads"), value, "viz::SharedQuadState",
      this);
}

}  // namespace viz
