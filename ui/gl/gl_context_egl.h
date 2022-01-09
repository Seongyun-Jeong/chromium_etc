// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GL_GL_CONTEXT_EGL_H_
#define UI_GL_GL_CONTEXT_EGL_H_

#include <map>

#include "ui/gl/gl_context.h"
#include "ui/gl/gl_export.h"

typedef void* EGLContext;
typedef void* EGLDisplay;
typedef void* EGLConfig;

namespace gl {

class GLSurface;

// Encapsulates an EGL OpenGL ES context.
class GL_EXPORT GLContextEGL : public GLContextReal {
 public:
  explicit GLContextEGL(GLShareGroup* share_group);

  GLContextEGL(const GLContextEGL&) = delete;
  GLContextEGL& operator=(const GLContextEGL&) = delete;

  // Implement GLContext.
  bool Initialize(GLSurface* compatible_surface,
                  const GLContextAttribs& attribs) override;
  bool MakeCurrentImpl(GLSurface* surface) override;
  void ReleaseCurrent(GLSurface* surface) override;
  bool IsCurrent(GLSurface* surface) override;
  void* GetHandle() override;
  unsigned int CheckStickyGraphicsResetStatusImpl() override;
  void SetUnbindFboOnMakeCurrent() override;
  YUVToRGBConverter* GetYUVToRGBConverter(
      const gfx::ColorSpace& color_space) override;
  void SetVisibility(bool visibility) override;

 protected:
  ~GLContextEGL() override;

 private:
  void Destroy();
  void ReleaseYUVToRGBConvertersAndBackpressureFences();

  EGLContext context_ = nullptr;
  EGLDisplay display_ = nullptr;
  EGLConfig config_ = nullptr;
  unsigned int graphics_reset_status_ = 0;  // GL_NO_ERROR;
  bool unbind_fbo_on_makecurrent_ = false;
  bool lost_ = false;
  std::map<gfx::ColorSpace, std::unique_ptr<YUVToRGBConverter>>
      yuv_to_rgb_converters_;
};

}  // namespace gl

#endif  // UI_GL_GL_CONTEXT_EGL_H_
