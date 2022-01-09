// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/surface_texture_gl_owner.h"

#include <stdint.h>

#include <memory>

#include "base/bind.h"
#include "base/test/task_environment.h"
#include "gpu/command_buffer/service/feature_info.h"
#include "gpu/command_buffer/service/mock_abstract_texture.h"
#include "gpu/config/gpu_feature_info.h"
#include "gpu/config/gpu_finch_features.h"
#include "gpu/config/gpu_preferences.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_context_egl.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_share_group.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/gl/init/gl_factory.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;

namespace gpu {

class SurfaceTextureGLOwnerTest : public testing::Test {
 public:
  SurfaceTextureGLOwnerTest() {}
  ~SurfaceTextureGLOwnerTest() override {}

 protected:
  void SetUp() override {
    gl::init::InitializeStaticGLBindingsImplementation(
        gl::GLImplementationParts(gl::kGLImplementationEGLGLES2), false);
    gl::init::InitializeGLOneOffPlatformImplementation(false, false, true);

    surface_ = new gl::PbufferGLSurfaceEGL(gfx::Size(320, 240));
    surface_->Initialize();

    share_group_ = new gl::GLShareGroup();
    context_ = new gl::GLContextEGL(share_group_.get());
    context_->Initialize(surface_.get(), gl::GLContextAttribs());
    ASSERT_TRUE(context_->MakeCurrent(surface_.get()));

    GpuDriverBugWorkarounds workarounds;
    workarounds.max_texture_size = INT_MAX - 1;
    auto context_state = base::MakeRefCounted<SharedContextState>(
        share_group_, surface_, context_,
        false /* use_virtualized_gl_contexts */, base::DoNothing());
    context_state->InitializeGrContext(GpuPreferences(), workarounds, nullptr);
    auto feature_info =
        base::MakeRefCounted<gles2::FeatureInfo>(workarounds, GpuFeatureInfo());
    context_state->InitializeGL(GpuPreferences(), std::move(feature_info));

    // Create a texture.
    glGenTextures(1, &texture_id_);

    std::unique_ptr<MockAbstractTexture> texture =
        std::make_unique<MockAbstractTexture>(texture_id_);
    abstract_texture_ = texture->AsWeakPtr();
    surface_texture_ = SurfaceTextureGLOwner::Create(
        std::move(texture), TextureOwner::Mode::kSurfaceTextureInsecure,
        std::move(context_state));
    texture_id_ = surface_texture_->GetTextureId();
    EXPECT_TRUE(abstract_texture_);
  }

  void TearDown() override {
    if (texture_id_ && context_->MakeCurrent(surface_.get()))
      glDeleteTextures(1, &texture_id_);
    surface_texture_ = nullptr;
    context_ = nullptr;
    share_group_ = nullptr;
    surface_ = nullptr;
    gl::init::ShutdownGL(false);
  }

  scoped_refptr<TextureOwner> surface_texture_;
  GLuint texture_id_ = 0;

  base::WeakPtr<MockAbstractTexture> abstract_texture_;

  scoped_refptr<gl::GLContext> context_;
  scoped_refptr<gl::GLShareGroup> share_group_;
  scoped_refptr<gl::GLSurface> surface_;
  base::test::TaskEnvironment task_environment_;
};

TEST_F(SurfaceTextureGLOwnerTest, OwnerReturnsServiceId) {
  // The owner should give us back the same service id we provided.
  EXPECT_EQ(texture_id_, surface_texture_->GetTextureId());
}

// Verify that SurfaceTextureGLOwner creates a bindable GL texture, and deletes
// it during destruction.
TEST_F(SurfaceTextureGLOwnerTest, GLTextureIsCreatedAndDestroyed) {
  // |texture_id| should not work anymore after we delete |surface_texture|.
  surface_texture_ = nullptr;
  EXPECT_FALSE(abstract_texture_);
}

// Calling ReleaseBackBuffers shouldn't deallocate the texture handle.
TEST_F(SurfaceTextureGLOwnerTest, ReleaseDoesntDestroyTexture) {
  surface_texture_->ReleaseBackBuffers();
  EXPECT_TRUE(abstract_texture_);
}

// Make sure that |surface_texture_| remembers the correct context and surface.
TEST_F(SurfaceTextureGLOwnerTest, ContextAndSurfaceAreCaptured) {
  ASSERT_EQ(context_, surface_texture_->GetContext());
  ASSERT_EQ(surface_, surface_texture_->GetSurface());
}

// Verify that destruction works even if some other context is current.
TEST_F(SurfaceTextureGLOwnerTest, DestructionWorksWithWrongContext) {
  scoped_refptr<gl::GLSurface> new_surface(
      new gl::PbufferGLSurfaceEGL(gfx::Size(320, 240)));
  new_surface->Initialize();

  scoped_refptr<gl::GLShareGroup> new_share_group(new gl::GLShareGroup());
  scoped_refptr<gl::GLContext> new_context(
      new gl::GLContextEGL(new_share_group.get()));
  new_context->Initialize(new_surface.get(), gl::GLContextAttribs());
  ASSERT_TRUE(new_context->MakeCurrent(new_surface.get()));

  surface_texture_ = nullptr;
  EXPECT_FALSE(abstract_texture_);

  // |new_context| should still be current.
  ASSERT_TRUE(new_context->IsCurrent(new_surface.get()));

  new_context = nullptr;
  new_share_group = nullptr;
  new_surface = nullptr;
}

}  // namespace gpu
