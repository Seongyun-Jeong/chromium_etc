// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/browser_frame_view_layout_linux_native.h"

#include "ui/views/linux_ui/nav_button_provider.h"

BrowserFrameViewLayoutLinuxNative::BrowserFrameViewLayoutLinuxNative(
    views::NavButtonProvider* nav_button_provider,
    views::WindowFrameProvider* window_frame_provider)
    : nav_button_provider_(nav_button_provider),
      window_frame_provider_(window_frame_provider) {}

BrowserFrameViewLayoutLinuxNative::~BrowserFrameViewLayoutLinuxNative() =
    default;

int BrowserFrameViewLayoutLinuxNative::CaptionButtonY(
    views::FrameButton button_id,
    bool restored) const {
  auto button_type = GetButtonDisplayType(button_id);
  gfx::Insets insets = nav_button_provider_->GetNavButtonMargin(button_type);
  return insets.top() + FrameEdgeInsets(false).top();
}

gfx::Insets BrowserFrameViewLayoutLinuxNative::RestoredFrameBorderInsets()
    const {
  return window_frame_provider_->GetFrameThicknessDip();
}

OpaqueBrowserFrameViewLayout::TopAreaPadding
BrowserFrameViewLayoutLinuxNative::GetTopAreaPadding(
    bool has_leading_buttons,
    bool has_trailing_buttons) const {
  gfx::Insets spacing = nav_button_provider_->GetTopAreaSpacing();
  gfx::Insets insets = spacing + FrameEdgeInsets(false);
  const auto padding = FrameBorderInsets(false);
  const auto leading = has_leading_buttons ? insets : padding;
  const auto trailing = has_trailing_buttons ? insets : padding;
  return TopAreaPadding{leading.left(), trailing.right()};
}

int BrowserFrameViewLayoutLinuxNative::GetWindowCaptionSpacing(
    views::FrameButton button_id,
    bool leading_spacing,
    bool is_leading_button) const {
  gfx::Insets insets =
      nav_button_provider_->GetNavButtonMargin(GetButtonDisplayType(button_id));
  if (!leading_spacing)
    return insets.right();
  int spacing = insets.left();
  if (!is_leading_button)
    spacing += nav_button_provider_->GetInterNavButtonSpacing();
  return spacing;
}

views::NavButtonProvider::FrameButtonDisplayType
BrowserFrameViewLayoutLinuxNative::GetButtonDisplayType(
    views::FrameButton button_id) const {
  switch (button_id) {
    case views::FrameButton::kMinimize:
      return views::NavButtonProvider::FrameButtonDisplayType::kMinimize;
    case views::FrameButton::kMaximize:
      return delegate_->IsMaximized()
                 ? views::NavButtonProvider::FrameButtonDisplayType::kRestore
                 : views::NavButtonProvider::FrameButtonDisplayType::kMaximize;
    case views::FrameButton::kClose:
      return views::NavButtonProvider::FrameButtonDisplayType::kClose;
    default:
      NOTREACHED();
      return views::NavButtonProvider::FrameButtonDisplayType::kClose;
  }
}
