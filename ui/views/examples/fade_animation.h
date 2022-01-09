// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_EXAMPLES_FADE_ANIMATION_H_
#define UI_VIEWS_EXAMPLES_FADE_ANIMATION_H_

#include "ui/gfx/geometry/size.h"
#include "ui/views/examples/example_base.h"
#include "ui/views/metadata/view_factory.h"
#include "ui/views/view.h"

namespace views {

class BoxLayoutView;

namespace examples {

class FadingView : public View {
 public:
  FadingView();
  FadingView(const FadingView&) = delete;
  FadingView& operator=(const FadingView&) = delete;
  ~FadingView() override;

 private:
  static constexpr int kCornerRadius = 12;
  static constexpr float kCornerRadiusF = float{kCornerRadius};
  static constexpr int kSpacing = 2;
  static constexpr gfx::Size kSize = {200, 50};

  BoxLayoutView* primary_view_;
  BoxLayoutView* secondary_view_;
};

BEGIN_VIEW_BUILDER(, FadingView, View)
END_VIEW_BUILDER

class VIEWS_EXAMPLES_EXPORT FadeAnimationExample : public ExampleBase {
 public:
  FadeAnimationExample();
  FadeAnimationExample(const FadeAnimationExample&) = delete;
  FadeAnimationExample& operator=(const FadeAnimationExample&) = delete;
  ~FadeAnimationExample() override;

  // ExampleBase:
  void CreateExampleView(View* container) override;
};

}  // namespace examples
}  // namespace views

DEFINE_VIEW_BUILDER(, views::examples::FadingView)

#endif  // UI_VIEWS_EXAMPLES_FADE_ANIMATION_H_
