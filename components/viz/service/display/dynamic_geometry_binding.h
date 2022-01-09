// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_DISPLAY_DYNAMIC_GEOMETRY_BINDING_H_
#define COMPONENTS_VIZ_SERVICE_DISPLAY_DYNAMIC_GEOMETRY_BINDING_H_

#include "base/memory/raw_ptr.h"
#include "components/viz/service/display/geometry_binding.h"
#include "components/viz/service/viz_service_export.h"

namespace gfx {
class QuadF;
}

namespace viz {

class VIZ_SERVICE_EXPORT DynamicGeometryBinding {
 public:
  explicit DynamicGeometryBinding(gpu::gles2::GLES2Interface* gl);

  DynamicGeometryBinding(const DynamicGeometryBinding&) = delete;
  DynamicGeometryBinding& operator=(const DynamicGeometryBinding&) = delete;

  ~DynamicGeometryBinding();

  void PrepareForDraw();
  void InitializeCustomQuad(const gfx::QuadF& quad);
  void InitializeCustomQuadWithUVs(const gfx::QuadF& quad, const float uv[8]);

 private:
  raw_ptr<gpu::gles2::GLES2Interface> gl_;

  GLuint quad_vertices_vbo_;
  GLuint quad_elements_vbo_;
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_DISPLAY_DYNAMIC_GEOMETRY_BINDING_H_
