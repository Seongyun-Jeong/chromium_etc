// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_COMMON_QUADS_SHARED_ELEMENT_DRAW_QUAD_H_
#define COMPONENTS_VIZ_COMMON_QUADS_SHARED_ELEMENT_DRAW_QUAD_H_

#include "components/viz/common/quads/draw_quad.h"
#include "components/viz/common/shared_element_resource_id.h"
#include "components/viz/common/viz_common_export.h"

namespace viz {

class VIZ_COMMON_EXPORT SharedElementDrawQuad : public DrawQuad {
 public:
  SharedElementDrawQuad();
  SharedElementDrawQuad(const SharedElementDrawQuad& other);
  ~SharedElementDrawQuad() override;

  SharedElementDrawQuad& operator=(const SharedElementDrawQuad& other);

  void SetNew(const SharedQuadState* shared_quad_state,
              const gfx::Rect& rect,
              const gfx::Rect& visible_rect,
              const SharedElementResourceId& id);

  void SetAll(const SharedQuadState* shared_quad_state,
              const gfx::Rect& rect,
              const gfx::Rect& visible_rect,
              bool needs_blending,
              const SharedElementResourceId& id);

  SharedElementResourceId resource_id;

  static const SharedElementDrawQuad* MaterialCast(const DrawQuad* quad);

 private:
  void ExtendValue(base::trace_event::TracedValue* value) const override;
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_COMMON_QUADS_SHARED_ELEMENT_DRAW_QUAD_H_
