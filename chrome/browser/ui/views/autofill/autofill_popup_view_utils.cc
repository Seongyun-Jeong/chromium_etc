// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/autofill/autofill_popup_view_utils.h"

#include <algorithm>

#include "base/cxx17_backports.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/permission_bubble/permission_prompt_bubble_view.h"
#include "components/autofill/core/common/autofill_features.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/constants.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/widget/widget.h"

using views::BubbleBorder;

namespace {

// The minimum number of pixels the bubble should be distanced from the edge of
// the content area.
constexpr int kMinimalBubbleDistanceToContentAreaEdge = 8;

// Returns true if the arrow is either located on top or on the bottom of the
// bubble.
bool IsVerticalArrowSide(views::BubbleArrowSide side) {
  return side == views::BubbleArrowSide::kTop ||
         side == views::BubbleArrowSide::kBottom;
}

// Returns false if the element is not sufficiently visible to place an arrow.
bool IsElementSufficientlyVisibleForAVerticalArrow(
    const gfx::Rect& content_area_bounds,
    const gfx::Rect& element_bounds,
    views::BubbleArrowSide side) {
  // Only consider the visible size of the element for vertical arrows.
  if (!IsVerticalArrowSide(side)) {
    return true;
  }

  int visible_width =
      base::clamp(element_bounds.right(), content_area_bounds.x(),
                  content_area_bounds.right()) -
      base::clamp(element_bounds.x(), content_area_bounds.x(),
                  content_area_bounds.right());

  return visible_width > 3 * BubbleBorder::kVisibleArrowRadius;
}

// Returns a BubbleBorder::Arrow that is suitable for the supplied |side| and
// text direction.
BubbleBorder::Arrow GetBubbleArrowForBubbleArrowSide(
    views::BubbleArrowSide side,
    bool right_to_left) {
  switch (side) {
    case views::BubbleArrowSide::kTop:
      return right_to_left ? BubbleBorder::Arrow::TOP_RIGHT
                           : BubbleBorder::Arrow::TOP_LEFT;

    case views::BubbleArrowSide::kBottom:
      return right_to_left ? BubbleBorder::Arrow::BOTTOM_RIGHT
                           : BubbleBorder::Arrow::BOTTOM_LEFT;

    case views::BubbleArrowSide::kLeft:
      return BubbleBorder::Arrow::LEFT_TOP;

    case views::BubbleArrowSide::kRight:
      return BubbleBorder::Arrow::RIGHT_TOP;
  }
}

// Returns the size of bubble placed on the |side| of the |element_bounds| once
// the bubble is expanded to its |bubble_preferred_size| or the maximum size
// available on the |content_area_bounds|.
gfx::Size GetExpandedBubbleSize(const gfx::Rect& content_area_bounds,
                                const gfx::Rect& element_bounds,
                                const gfx::Size& bubble_preferred_size,
                                int scrollbar_width,
                                views::BubbleArrowSide side) {
  // Get the maximum available space for the bubble.
  int available_height = GetAvailableVerticalSpaceOnSideOfElement(
      content_area_bounds, element_bounds, side);
  int available_width = GetAvailableHorizontalSpaceOnSideOfElement(
      content_area_bounds, element_bounds, side);

  int height = std::min(available_height, bubble_preferred_size.height());
  int width = std::min(
      available_width,
      bubble_preferred_size.width() +
          (height < bubble_preferred_size.height() ? scrollbar_width : 0));

  return {width, height};
}

}  // namespace

void CalculatePopupXAndWidthHorizontallyCentered(
    int popup_preferred_width,
    const gfx::Rect& content_area_bounds,
    const gfx::Rect& element_bounds,
    bool is_rtl,
    gfx::Rect* bubble_bounds) {
  // The preferred horizontal starting point for the pop-up is at the horizontal
  // center of the field.
  int preferred_starting_point =
      base::clamp(element_bounds.x() + (element_bounds.size().width() / 2),
                  content_area_bounds.x(), content_area_bounds.right());

  // The space available to the left and to the right.
  int space_to_right = content_area_bounds.right() - preferred_starting_point;
  int space_to_left = preferred_starting_point - content_area_bounds.x();

  // Calculate the pop-up width. This is either the preferred pop-up width, or
  // alternatively the maximum space available if there is not sufficient space
  // for the preferred width.
  int popup_width =
      std::min(popup_preferred_width, space_to_left + space_to_right);

  // Calculates the space that is available to grow into the preferred
  // direction. In RTL, this is the space to the right side of the content
  // area, in LTR this is the space to the left side of the content area.
  int space_to_grow_in_preferred_direction =
      is_rtl ? space_to_left : space_to_right;

  // Calculate how much the pop-up needs to grow into the non-preferred
  // direction.
  int amount_to_grow_in_unpreffered_direction =
      std::max(0, popup_width - space_to_grow_in_preferred_direction);

  bubble_bounds->set_width(popup_width);
  if (is_rtl) {
    // Note, in RTL the |pop_up_width| must be subtracted to achieve
    // right-alignment of the pop-up with the element.
    bubble_bounds->set_x(preferred_starting_point - popup_width +
                         amount_to_grow_in_unpreffered_direction);
  } else {
    bubble_bounds->set_x(preferred_starting_point -
                         amount_to_grow_in_unpreffered_direction);
  }
}

void CalculatePopupXAndWidth(int popup_preferred_width,
                             const gfx::Rect& content_area_bounds,
                             const gfx::Rect& element_bounds,
                             bool is_rtl,
                             gfx::Rect* bubble_bounds) {
  int right_growth_start = base::clamp(
      element_bounds.x(), content_area_bounds.x(), content_area_bounds.right());
  int left_growth_end =
      base::clamp(element_bounds.right(), content_area_bounds.x(),
                  content_area_bounds.right());

  int right_available = content_area_bounds.right() - right_growth_start;
  int left_available = left_growth_end - content_area_bounds.x();

  int popup_width = std::min(popup_preferred_width,
                             std::max(left_available, right_available));

  // Prefer to grow towards the end (right for LTR, left for RTL). But if there
  // is not enough space available in the desired direction and more space in
  // the other direction, reverse it.
  bool grow_left = false;
  if (is_rtl) {
    grow_left =
        left_available >= popup_width || left_available >= right_available;
  } else {
    grow_left =
        right_available < popup_width && right_available < left_available;
  }

  bubble_bounds->set_width(popup_width);
  bubble_bounds->set_x(grow_left ? left_growth_end - popup_width
                                 : right_growth_start);
}

void CalculatePopupYAndHeight(int popup_preferred_height,
                              const gfx::Rect& content_area_bounds,
                              const gfx::Rect& element_bounds,
                              gfx::Rect* bubble_bounds) {
  int top_growth_end = base::clamp(element_bounds.y(), content_area_bounds.y(),
                                   content_area_bounds.bottom());
  int bottom_growth_start =
      base::clamp(element_bounds.bottom(), content_area_bounds.y(),
                  content_area_bounds.bottom());

  int top_available = top_growth_end - content_area_bounds.y();
  int bottom_available = content_area_bounds.bottom() - bottom_growth_start;

  bubble_bounds->set_height(popup_preferred_height);
  bubble_bounds->set_y(top_growth_end);

  if (bottom_available >= popup_preferred_height ||
      bottom_available >= top_available) {
    bubble_bounds->AdjustToFit(
        gfx::Rect(bubble_bounds->x(), element_bounds.bottom(),
                  bubble_bounds->width(), bottom_available));
  } else {
    bubble_bounds->AdjustToFit(
        gfx::Rect(bubble_bounds->x(), content_area_bounds.y(),
                  bubble_bounds->width(), top_available));
  }
}

gfx::Rect CalculatePopupBounds(const gfx::Size& desired_size,
                               const gfx::Rect& content_area_bounds,
                               const gfx::Rect& element_bounds,
                               bool is_rtl,
                               bool horizontally_centered) {
  gfx::Rect bubble_bounds;

  if (horizontally_centered) {
    CalculatePopupXAndWidthHorizontallyCentered(
        desired_size.width(), content_area_bounds, element_bounds, is_rtl,
        &bubble_bounds);
  } else {
    CalculatePopupXAndWidth(desired_size.width(), content_area_bounds,
                            element_bounds, is_rtl, &bubble_bounds);
  }
  CalculatePopupYAndHeight(desired_size.height(), content_area_bounds,
                           element_bounds, &bubble_bounds);

  return bubble_bounds;
}

bool CanShowDropdownHere(int item_height,
                         const gfx::Rect& content_area_bounds,
                         const gfx::Rect& element_bounds) {
  // Ensure that at least one row of the popup will be displayed within the
  // bounds of the content area so that the user notices the presence of the
  // popup.
  bool enough_space_for_one_item_in_content_area_above_element =
      element_bounds.y() - content_area_bounds.y() >= item_height;
  bool element_top_is_within_content_area_bounds =
      element_bounds.y() >= content_area_bounds.y() &&
      element_bounds.y() < content_area_bounds.bottom();

  bool enough_space_for_one_item_in_content_area_below_element =
      content_area_bounds.bottom() - element_bounds.bottom() >= item_height;
  bool element_bottom_is_within_content_area_bounds =
      element_bounds.bottom() > content_area_bounds.y() &&
      element_bounds.bottom() <= content_area_bounds.bottom();

  return (enough_space_for_one_item_in_content_area_above_element &&
          element_top_is_within_content_area_bounds) ||
         (enough_space_for_one_item_in_content_area_below_element &&
          element_bottom_is_within_content_area_bounds);
}

bool BoundsOverlapWithAnyOpenPrompt(const gfx::Rect& screen_bounds,
                                    content::WebContents* web_contents) {
  gfx::NativeView top_level_view =
      platform_util::GetViewForWindow(web_contents->GetTopLevelNativeWindow());
  if (!top_level_view)
    return false;
  // We generally want to ensure that no prompt overlaps with |screen_bounds|.
  // It is possible, however, that a <datalist> is part of a prompt (e.g. an
  // extension popup can render a <datalist>). Therefore, we exclude the widget
  // that hosts the |web_contents| from the prompts that are considered for
  // overlaps.
  views::Widget* web_contents_widget =
      views::Widget::GetTopLevelWidgetForNativeView(
          web_contents->GetContentNativeView());

  // On Aura-based systems, prompts are siblings to the top level native window,
  // and hence we need to go one level up to start searching from the root
  // window.
  top_level_view = platform_util::GetParent(top_level_view)
                       ? platform_util::GetParent(top_level_view)
                       : top_level_view;
  views::Widget::Widgets all_widgets;
  views::Widget::GetAllChildWidgets(top_level_view, &all_widgets);
  return base::ranges::any_of(
      all_widgets, [&screen_bounds, web_contents_widget](views::Widget* w) {
        return w->IsDialogBox() &&
               w->GetWindowBoundsInScreen().Intersects(screen_bounds) &&
               w != web_contents_widget;
      });
}

bool BoundsOverlapWithOpenPermissionsPrompt(
    const gfx::Rect& screen_bounds,
    content::WebContents* web_contents) {
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents);
  if (!browser)
    return false;

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view)
    return false;

  views::View* const permission_bubble_view =
      views::ElementTrackerViews::GetInstance()->GetFirstMatchingView(
          PermissionPromptBubbleView::kPermissionPromptBubbleViewIdentifier,
          views::ElementTrackerViews::GetInstance()->GetContextForView(
              browser_view));
  if (!permission_bubble_view)
    return false;

  return permission_bubble_view->GetWidget()
      ->GetWindowBoundsInScreen()
      .Intersects(screen_bounds);
}

bool PopupMayExceedContentAreaBounds(content::WebContents* web_contents) {
  if (!web_contents)  // May be null for tests.
    return false;
  const GURL& url = web_contents->GetLastCommittedURL();
  // Extensions may want to show <datalist> form controls whose popups cannot be
  // rendered within the bounds of an extension popup. For that reason they are
  // allow-listed to draw popups outside the boundary of the extension popup.
  if (url.SchemeIs(extensions::kExtensionScheme)) {
    views::Widget* widget = views::Widget::GetTopLevelWidgetForNativeView(
        web_contents->GetContentNativeView());
    return widget && widget->GetName() == ExtensionPopup::kViewClassName;
  }
  return false;
}

int GetAvailableVerticalSpaceOnSideOfElement(
    const gfx::Rect& content_area_bounds,
    const gfx::Rect& element_bounds,
    views::BubbleArrowSide side) {
  // Note that the side of the arrow is opposite to the side of the element the
  // bubble is located on.
  switch (side) {
    case views::BubbleArrowSide::kLeft:
    case views::BubbleArrowSide::kRight:
      // For a bubble that is either on the left of the right side of the
      // element, the height of the content area is the total available space.
      return content_area_bounds.height();

    case views::BubbleArrowSide::kBottom:
      // If the bubble sits above the element, return the space between the
      // upper edge of the element and the content area.
      return element_bounds.y() - content_area_bounds.y();

    case views::BubbleArrowSide::kTop:
      // If the bubble sits below the element, return the space between the
      // lower edge of the element and the content area.
      return content_area_bounds.bottom() - element_bounds.bottom();
  }
}

int GetAvailableHorizontalSpaceOnSideOfElement(
    const gfx::Rect& content_area_bounds,
    const gfx::Rect& element_bounds,
    views::BubbleArrowSide side) {
  // Note that the side of the arrow is opposite to the side of the element the
  // bubble is located on.
  switch (side) {
    case views::BubbleArrowSide::kRight:
      return element_bounds.x() - content_area_bounds.x() -
             kMinimalBubbleDistanceToContentAreaEdge;

    case views::BubbleArrowSide::kLeft:
      return content_area_bounds.right() - element_bounds.right() -
             kMinimalBubbleDistanceToContentAreaEdge;

    case views::BubbleArrowSide::kTop:
    case views::BubbleArrowSide::kBottom:
      return content_area_bounds.width() -
             2 * kMinimalBubbleDistanceToContentAreaEdge;
  }
}

bool IsBubblePlaceableOnSideOfElement(const gfx::Rect& content_area_bounds,
                                      const gfx::Rect& element_bounds,
                                      const gfx::Size& bubble_preferred_size,
                                      int additional_spacing,
                                      views::BubbleArrowSide side) {
  switch (side) {
    case views::BubbleArrowSide::kLeft:
    case views::BubbleArrowSide::kRight:
      return bubble_preferred_size.width() + additional_spacing <=
             GetAvailableHorizontalSpaceOnSideOfElement(content_area_bounds,
                                                        element_bounds, side);

    case views::BubbleArrowSide::kTop:
    case views::BubbleArrowSide::kBottom:
      return bubble_preferred_size.height() + additional_spacing <=
             GetAvailableVerticalSpaceOnSideOfElement(content_area_bounds,
                                                      element_bounds, side);
  }
}

views::BubbleArrowSide GetOptimalBubbleArrowSide(
    const gfx::Rect& content_area_bounds,
    const gfx::Rect& element_bounds,
    const gfx::Size& bubble_preferred_size) {
  // Probe for a side of the element on which the bubble can be shown entirely.
  const std::vector<views::BubbleArrowSide> sides_by_preference(
      {views::BubbleArrowSide::kTop, views::BubbleArrowSide::kBottom,
       views::BubbleArrowSide::kLeft, views::BubbleArrowSide::kRight});
  for (views::BubbleArrowSide possible_side : sides_by_preference) {
    if (IsBubblePlaceableOnSideOfElement(
            content_area_bounds, element_bounds, bubble_preferred_size,
            BubbleBorder::kVisibleArrowLength, possible_side) &&
        IsElementSufficientlyVisibleForAVerticalArrow(
            content_area_bounds, element_bounds, possible_side)) {
      return possible_side;
    }
  }

  return views::BubbleArrowSide::kBottom;
}

BubbleBorder::Arrow GetOptimalBubblePlacement(
    const gfx::Rect& content_area_bounds,
    const gfx::Rect& element_bounds,
    const gfx::Size& bubble_preferred_size,
    bool right_to_left,
    int scrollbar_width,
    int maximum_pixel_offset_to_center,
    int maximum_width_percentage_to_center,
    gfx::Rect& bubble_bounds) {
  // Determine the best side of the element to put the bubble and get a
  // corresponding arrow.
  views::BubbleArrowSide side = GetOptimalBubbleArrowSide(
      content_area_bounds, element_bounds, bubble_preferred_size);
  BubbleBorder::Arrow arrow =
      GetBubbleArrowForBubbleArrowSide(side, right_to_left);

  // Set the actual size of the bubble.
  bubble_bounds.set_size(
      GetExpandedBubbleSize(content_area_bounds, element_bounds,
                            bubble_preferred_size, scrollbar_width, side));

  // Move the origin of the bubble to the anchor position on the element
  // corresponding to |arrow|.
  //                   ------------------
  //  For TOP_LEFT    |      element     |
  //  anchor_point ->  ==============----
  //                  |              |
  //                  |    bubble    |
  //                  |              |
  //                  |              |
  //                   --------------
  bubble_bounds += views::GetContentBoundsOffsetToArrowAnchorPoint(
      bubble_bounds, arrow,
      views::GetArrowAnchorPointFromAnchorRect(arrow, element_bounds));

  if (!IsVerticalArrowSide(side)) {
    // For a horizontal arrow, move the bubble to the top if it leaves the
    // lower part of the screen. Note, that by default, the bubble's top is
    // aligned with the field.
    // The bubble top can never go above the content area since the bubble size
    // computed to fit in the screen by GetExpandedBubbleSize.
    bubble_bounds.Offset(0, -1 * std::max(0, bubble_bounds.bottom() -
                                                 content_area_bounds.bottom()));
    return arrow;
  }

  // The horizontal offset is the minimum of a fixed number of pixels
  // |maximum_pixel_offset_to_center| and a percentage of the element width.
  int horizontal_offset_pixels = std::min(
      maximum_pixel_offset_to_center,
      maximum_width_percentage_to_center * element_bounds.width() / 100);

  // In addition, the offset is shifted by the distance of the bubble's arrow to
  // the bubble's edge. By this, the arrow of the bubble is aligned with the
  // targeted pixel and not the edge of the bubble.
  horizontal_offset_pixels -=
      (BubbleBorder::kVisibleArrowBuffer + BubbleBorder::kVisibleArrowRadius);

  // Give the offset a direction.
  int horizontal_offset = horizontal_offset_pixels * (right_to_left ? -1 : 1);

  // Move the bubble bounds towards to center of the field.
  // Note that for |right_to_left|, this will be a negative value.
  //              ------------------
  //             |      element     |
  //              ----------========-------
  //                       |               |
  //             |---------|    bubble     |
  //   horizontal offset   |               |
  //                       |               |
  //                        ---------------
  bubble_bounds.Offset(horizontal_offset, 0);

  // In case the bubble the exceeds the right edge of the view port, move it
  // back until it completely fits.
  //              ------------------   |---| shift back
  //             |      element     |  |
  //              ----------========---+---
  //                       |           |   |
  //                       |    bubble |   |
  //                       |           |   |
  //                       |           |   |
  //                        -----------+---
  //                                   |
  //                          content_area.right()
  bubble_bounds.Offset(
      std::min(0, content_area_bounds.right() - bubble_bounds.right() -
                      kMinimalBubbleDistanceToContentAreaEdge),
      0);

  // Analogously, make move the bubble to the right if it exceeds the left edge
  // of the content area.
  bubble_bounds.Offset(std::max(0, content_area_bounds.x() - bubble_bounds.x() +
                                       kMinimalBubbleDistanceToContentAreaEdge),
                       0);

  return arrow;
}
