// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gl/init/gl_initializer.h"

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/native_library.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_egl_api_implementation.h"
#include "ui/gl/gl_gl_api_implementation.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/gl/init/gl_initializer.h"

namespace gl {
namespace init {

namespace {

bool InitializeStaticNativeEGLInternal() {
  base::NativeLibrary gles_library = LoadLibraryAndPrintError("libGLESv2.so");
  if (!gles_library)
    return false;
  base::NativeLibrary egl_library = LoadLibraryAndPrintError("libEGL.so");
  if (!egl_library) {
    base::UnloadNativeLibrary(gles_library);
    return false;
  }

  GLGetProcAddressProc get_proc_address =
      reinterpret_cast<GLGetProcAddressProc>(
          base::GetFunctionPointerFromNativeLibrary(egl_library,
                                                    "eglGetProcAddress"));
  if (!get_proc_address) {
    LOG(ERROR) << "eglGetProcAddress not found.";
    base::UnloadNativeLibrary(egl_library);
    base::UnloadNativeLibrary(gles_library);
    return false;
  }

  SetGLGetProcAddressProc(get_proc_address);
  AddGLNativeLibrary(egl_library);
  AddGLNativeLibrary(gles_library);

  return true;
}

bool InitializeStaticEGLInternal(GLImplementationParts implementation) {
  bool initialized = false;

#if BUILDFLAG(USE_STATIC_ANGLE)
  // Use ANGLE if it is requested and it is statically linked
  if (implementation.gl == kGLImplementationEGLANGLE) {
    initialized = InitializeStaticANGLEEGL();
  }
#endif  // BUILDFLAG(USE_STATIC_ANGLE)

  if (!initialized) {
    initialized = InitializeStaticNativeEGLInternal();
  }

  if (!initialized) {
    return false;
  }

  SetGLImplementationParts(implementation);

  InitializeStaticGLBindingsGL();
  InitializeStaticGLBindingsEGL();

  return true;
}

}  // namespace

bool InitializeGLOneOffPlatform() {
  switch (GetGLImplementation()) {
    case kGLImplementationEGLGLES2:
    case kGLImplementationEGLANGLE:
      if (!GLSurfaceEGL::InitializeOneOff(
              EGLDisplayPlatform(EGL_DEFAULT_DISPLAY))) {
        LOG(ERROR) << "GLSurfaceEGL::InitializeOneOff failed.";
        return false;
      }
      return true;
    default:
      return true;
  }
}

bool InitializeStaticGLBindings(GLImplementationParts implementation) {
  // Prevent reinitialization with a different implementation. Once the gpu
  // unit tests have initialized with kGLImplementationMock, we don't want to
  // later switch to another GL implementation.
  DCHECK_EQ(kGLImplementationNone, GetGLImplementation());

  switch (implementation.gl) {
    case kGLImplementationEGLGLES2:
    case kGLImplementationEGLANGLE:
      return InitializeStaticEGLInternal(implementation);
    case kGLImplementationMockGL:
    case kGLImplementationStubGL:
      SetGLImplementationParts(implementation);
      InitializeStaticGLBindingsGL();
      return true;
    default:
      NOTREACHED();
  }

  return false;
}

void ShutdownGLPlatform() {
  GLSurfaceEGL::ShutdownOneOff();
  ClearBindingsEGL();
  ClearBindingsGL();
}

}  // namespace init
}  // namespace gl