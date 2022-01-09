// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_DEMO_SKIA_SKIA_RENDERER_FACTORY_H_
#define UI_OZONE_DEMO_SKIA_SKIA_RENDERER_FACTORY_H_

#include <memory>

#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/ozone/demo/renderer_factory.h"

namespace ui {

class Renderer;

class SkiaRendererFactory : public RendererFactory {
 public:
  enum RendererType {
    SKIA,
    SOFTWARE,
  };

  SkiaRendererFactory();

  SkiaRendererFactory(const SkiaRendererFactory&) = delete;
  SkiaRendererFactory& operator=(const SkiaRendererFactory&) = delete;

  ~SkiaRendererFactory() override;

  bool Initialize() override;
  std::unique_ptr<Renderer> CreateRenderer(gfx::AcceleratedWidget widget,
                                           const gfx::Size& size) override;
};

}  // namespace ui

#endif  // UI_OZONE_DEMO_SKIA_SKIA_RENDERER_FACTORY_H_
