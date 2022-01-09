// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/accessibility/platform/ax_platform_node_textrangeprovider_win.h"

#include <utility>
#include <vector>

#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/i18n/string_search.h"
#include "base/memory/raw_ptr.h"
#include "base/win/scoped_safearray.h"
#include "base/win/scoped_variant.h"
#include "base/win/variant_vector.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/platform/ax_platform_node_delegate.h"
#include "ui/accessibility/platform/ax_platform_tree_manager.h"

#define UIA_VALIDATE_TEXTRANGEPROVIDER_CALL()                  \
  if (!GetOwner() || !GetOwner()->GetDelegate() || !start() || \
      !start()->GetAnchor() || !end() || !end()->GetAnchor())  \
    return UIA_E_ELEMENTNOTAVAILABLE;                          \
  SetStart(start()->AsValidPosition());                        \
  SetEnd(end()->AsValidPosition());
#define UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_IN(in)           \
  if (!GetOwner() || !GetOwner()->GetDelegate() || !start() || \
      !start()->GetAnchor() || !end() || !end()->GetAnchor())  \
    return UIA_E_ELEMENTNOTAVAILABLE;                          \
  if (!in)                                                     \
    return E_POINTER;                                          \
  SetStart(start()->AsValidPosition());                        \
  SetEnd(end()->AsValidPosition());
#define UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_OUT(out)         \
  if (!GetOwner() || !GetOwner()->GetDelegate() || !start() || \
      !start()->GetAnchor() || !end() || !end()->GetAnchor())  \
    return UIA_E_ELEMENTNOTAVAILABLE;                          \
  if (!out)                                                    \
    return E_POINTER;                                          \
  *out = {};                                                   \
  SetStart(start()->AsValidPosition());                        \
  SetEnd(end()->AsValidPosition());
#define UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_IN_1_OUT(in, out) \
  if (!GetOwner() || !GetOwner()->GetDelegate() || !start() ||  \
      !start()->GetAnchor() || !end() || !end()->GetAnchor())   \
    return UIA_E_ELEMENTNOTAVAILABLE;                           \
  if (!in || !out)                                              \
    return E_POINTER;                                           \
  *out = {};                                                    \
  SetStart(start()->AsValidPosition());                         \
  SetEnd(end()->AsValidPosition());
// Validate bounds calculated by AXPlatformNodeDelegate. Degenerate bounds
// indicate the interface is not yet supported on the platform.
#define UIA_VALIDATE_BOUNDS(bounds)                           \
  if (bounds.OffsetFromOrigin().IsZero() && bounds.IsEmpty()) \
    return UIA_E_NOTSUPPORTED;

namespace ui {

class AXRangePhysicalPixelRectDelegate : public AXRangeRectDelegate {
 public:
  explicit AXRangePhysicalPixelRectDelegate(
      AXPlatformNodeTextRangeProviderWin* host)
      : host_(host) {}

  gfx::Rect GetInnerTextRangeBoundsRect(
      AXTreeID tree_id,
      AXNodeID node_id,
      int start_offset,
      int end_offset,
      ui::AXClippingBehavior clipping_behavior,
      AXOffscreenResult* offscreen_result) override {
    AXPlatformNodeDelegate* delegate = host_->GetDelegate(tree_id, node_id);
    DCHECK(delegate);
    return delegate->GetInnerTextRangeBoundsRect(
        start_offset, end_offset, ui::AXCoordinateSystem::kScreenPhysicalPixels,
        clipping_behavior, offscreen_result);
  }

  gfx::Rect GetBoundsRect(AXTreeID tree_id,
                          AXNodeID node_id,
                          AXOffscreenResult* offscreen_result) override {
    AXPlatformNodeDelegate* delegate = host_->GetDelegate(tree_id, node_id);
    DCHECK(delegate);
    return delegate->GetBoundsRect(
        ui::AXCoordinateSystem::kScreenPhysicalPixels,
        ui::AXClippingBehavior::kClipped, offscreen_result);
  }

 private:
  raw_ptr<AXPlatformNodeTextRangeProviderWin> host_;
};

AXPlatformNodeTextRangeProviderWin::AXPlatformNodeTextRangeProviderWin() {
  DVLOG(1) << __func__;
}

AXPlatformNodeTextRangeProviderWin::~AXPlatformNodeTextRangeProviderWin() {}

ITextRangeProvider* AXPlatformNodeTextRangeProviderWin::CreateTextRangeProvider(
    AXPositionInstance start,
    AXPositionInstance end) {
  CComObject<AXPlatformNodeTextRangeProviderWin>* text_range_provider = nullptr;
  if (SUCCEEDED(CComObject<AXPlatformNodeTextRangeProviderWin>::CreateInstance(
          &text_range_provider))) {
    DCHECK(text_range_provider);
    text_range_provider->SetStart(std::move(start));
    text_range_provider->SetEnd(std::move(end));
    text_range_provider->AddRef();
    return text_range_provider;
  }

  return nullptr;
}

ITextRangeProvider*
AXPlatformNodeTextRangeProviderWin::CreateTextRangeProviderForTesting(
    AXPlatformNodeWin* owner,
    AXPositionInstance start,
    AXPositionInstance end) {
  Microsoft::WRL::ComPtr<ITextRangeProvider> text_range_provider =
      CreateTextRangeProvider(start->Clone(), end->Clone());
  Microsoft::WRL::ComPtr<AXPlatformNodeTextRangeProviderWin>
      text_range_provider_win;
  if (SUCCEEDED(text_range_provider->QueryInterface(
          IID_PPV_ARGS(&text_range_provider_win)))) {
    text_range_provider_win->SetOwnerForTesting(owner);  // IN-TEST
    return text_range_provider_win.Get();
  }

  return nullptr;
}

//
// ITextRangeProvider methods.
//
HRESULT AXPlatformNodeTextRangeProviderWin::Clone(ITextRangeProvider** clone) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_CLONE);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_OUT(clone);

  *clone = CreateTextRangeProvider(start()->Clone(), end()->Clone());
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::Compare(ITextRangeProvider* other,
                                                    BOOL* result) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_COMPARE);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_COMPARE);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_IN_1_OUT(other, result);

  Microsoft::WRL::ComPtr<AXPlatformNodeTextRangeProviderWin> other_provider;
  if (other->QueryInterface(IID_PPV_ARGS(&other_provider)) != S_OK)
    return UIA_E_INVALIDOPERATION;

  if (*start() == *(other_provider->start()) &&
      *end() == *(other_provider->end())) {
    *result = TRUE;
  }
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::CompareEndpoints(
    TextPatternRangeEndpoint this_endpoint,
    ITextRangeProvider* other,
    TextPatternRangeEndpoint other_endpoint,
    int* result) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_COMPAREENDPOINTS);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_COMPAREENDPOINTS);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_IN_1_OUT(other, result);

  Microsoft::WRL::ComPtr<AXPlatformNodeTextRangeProviderWin> other_provider;
  if (other->QueryInterface(IID_PPV_ARGS(&other_provider)) != S_OK)
    return UIA_E_INVALIDOPERATION;

  const AXPositionInstance& this_provider_endpoint =
      (this_endpoint == TextPatternRangeEndpoint_Start) ? start() : end();
  const AXPositionInstance& other_provider_endpoint =
      (other_endpoint == TextPatternRangeEndpoint_Start)
          ? other_provider->start()
          : other_provider->end();

  absl::optional<int> comparison =
      this_provider_endpoint->CompareTo(*other_provider_endpoint);
  if (!comparison)
    return UIA_E_INVALIDOPERATION;

  if (comparison.value() < 0)
    *result = -1;
  else if (comparison.value() > 0)
    *result = 1;
  else
    *result = 0;
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::ExpandToEnclosingUnit(
    TextUnit unit) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_EXPANDTOENCLOSINGUNIT);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_EXPANDTOENCLOSINGUNIT);
  return ExpandToEnclosingUnitImpl(unit);
}

HRESULT AXPlatformNodeTextRangeProviderWin::ExpandToEnclosingUnitImpl(
    TextUnit unit) {
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL();
  {
    AXPositionInstance normalized_start = start()->Clone();
    AXPositionInstance normalized_end = end()->Clone();
    NormalizeTextRange(normalized_start, normalized_end);
    SetStart(std::move(normalized_start));
    SetEnd(std::move(normalized_end));
  }

  // Determine if start is on a boundary of the specified TextUnit, if it is
  // not, move backwards until it is. Move the end forwards from start until it
  // is on the next TextUnit boundary, if one exists.
  switch (unit) {
    case TextUnit_Character: {
      // For characters, the start endpoint will always be on a TextUnit
      // boundary, thus we only need to move the end position.
      AXPositionInstance end_backup = end()->Clone();
      SetEnd(start()->CreateNextCharacterPosition(
          AXBoundaryBehavior::kCrossBoundary));

      if (end()->IsNullPosition()) {
        // The previous could fail if the start is at the end of the last anchor
        // of the tree, try expanding to the previous character instead.
        AXPositionInstance start_backup = start()->Clone();
        SetStart(start()->CreatePreviousCharacterPosition(
            AXBoundaryBehavior::kCrossBoundary));

        if (start()->IsNullPosition()) {
          // Text representation is empty, undo everything and exit.
          SetStart(std::move(start_backup));
          SetEnd(std::move(end_backup));
          return S_OK;
        }
        SetEnd(start()->CreateNextCharacterPosition(
            AXBoundaryBehavior::kCrossBoundary));
        DCHECK(!end()->IsNullPosition());
      }

      AXPositionInstance normalized_start = start()->Clone();
      AXPositionInstance normalized_end = end()->Clone();
      NormalizeTextRange(normalized_start, normalized_end);
      SetStart(std::move(normalized_start));
      SetEnd(std::move(normalized_end));
      break;
    }
    case TextUnit_Format:
      SetStart(start()->CreatePreviousFormatStartPosition(
          AXBoundaryBehavior::kStopAtAnchorBoundaryOrIfAlreadyAtBoundary));
      SetEnd(start()->CreateNextFormatEndPosition(
          AXBoundaryBehavior::kStopAtLastAnchorBoundary));
      break;
    case TextUnit_Word: {
      AXPositionInstance start_backup = start()->Clone();
      SetStart(start()->CreatePreviousWordStartPosition(
          AXBoundaryBehavior::kStopAtAnchorBoundaryOrIfAlreadyAtBoundary));

      // Since start_ is already located at a word boundary, we need to cross it
      // in order to move to the next one. Because Windows ATs behave
      // undesirably when the start and end endpoints are not in the same anchor
      // (for character and word navigation), stop at anchor boundary.
      SetEnd(start()->CreateNextWordStartPosition(
          AXBoundaryBehavior::kStopAtAnchorBoundary));
      break;
    }
    case TextUnit_Line:
      SetStart(start()->CreateBoundaryStartPosition(
          AXBoundaryBehavior::kStopAtAnchorBoundaryOrIfAlreadyAtBoundary,
          ax::mojom::MoveDirection::kBackward,
          base::BindRepeating(&AtStartOfLinePredicate),
          base::BindRepeating(&AtEndOfLinePredicate)));
      SetEnd(start()->CreateBoundaryEndPosition(
          AXBoundaryBehavior::kStopAtAnchorBoundaryOrIfAlreadyAtBoundary,
          ax::mojom::MoveDirection::kForward,
          base::BindRepeating(&AtStartOfLinePredicate),
          base::BindRepeating(&AtEndOfLinePredicate)));
      break;
    case TextUnit_Paragraph:
      SetStart(
          start()->CreatePreviousParagraphStartPositionSkippingEmptyParagraphs(
              AXBoundaryBehavior::kStopAtAnchorBoundaryOrIfAlreadyAtBoundary));
      SetEnd(start()->CreateNextParagraphStartPositionSkippingEmptyParagraphs(
          AXBoundaryBehavior::kStopAtLastAnchorBoundary));
      break;
    case TextUnit_Page: {
      // Per UIA spec, if the document containing the current range doesn't
      // support pagination, default to document navigation.
      const AXNode* common_anchor = start()->LowestCommonAnchor(*end());
      if (common_anchor->tree()->HasPaginationSupport()) {
        SetStart(start()->CreatePreviousPageStartPosition(
            AXBoundaryBehavior::kStopAtAnchorBoundaryOrIfAlreadyAtBoundary));
        SetEnd(start()->CreateNextPageEndPosition(
            ui::AXBoundaryBehavior::
                kStopAtAnchorBoundaryOrIfAlreadyAtBoundary));
        break;
      }
    }
      [[fallthrough]];
    case TextUnit_Document:
      SetStart(start()->CreatePositionAtStartOfContent()->AsLeafTextPosition());
      SetEnd(start()->CreatePositionAtEndOfContent());
      break;
    default:
      return UIA_E_NOTSUPPORTED;
  }
  DCHECK(!start()->IsNullPosition());
  DCHECK(!end()->IsNullPosition());
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::FindAttribute(
    TEXTATTRIBUTEID text_attribute_id,
    VARIANT attribute_val,
    BOOL is_backward,
    ITextRangeProvider** result) {
  // Algorithm description:
  // Performs linear search. Expand forward or backward to fetch the first
  // instance of a sub text range that matches the attribute and its value.
  // |is_backward| determines the direction of our search.
  // |is_backward=true|, we search from the end of this text range to its
  // beginning.
  // |is_backward=false|, we search from the beginning of this text range to its
  // end.
  //
  // 1. Iterate through the vector of AXRanges in this text range in the
  //    direction denoted by |is_backward|.
  // 2. The |matched_range| is initially denoted as null since no range
  //    currently matches. We initialize |matched_range| to non-null value when
  //    we encounter the first AXRange instance that matches in attribute and
  //    value. We then set the |matched_range_start| to be the start (anchor) of
  //    the current AXRange, and |matched_range_end| to be the end (focus) of
  //    the current AXRange.
  // 3. If the current AXRange we are iterating on continues to match attribute
  //    and value, we extend |matched_range| in one of the two following ways:
  //    - If |is_backward=true|, we extend the |matched_range| by moving
  //      |matched_range_start| backward. We do so by setting
  //      |matched_range_start| to the start (anchor) of the current AXRange.
  //    - If |is_backward=false|, we extend the |matched_range| by moving
  //      |matched_range_end| forward. We do so by setting |matched_range_end|
  //      to the end (focus) of the current AXRange.
  // 4. We found a match when the current AXRange we are iterating on does not
  //    match the attribute and value and there is a previously matched range.
  //    The previously matched range is the final match we found.
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_FINDATTRIBUTE);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_FINDATTRIBUTE);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_OUT(result);
  // Use a cloned range so that FindAttribute does not introduce side-effects
  // while normalizing the original range.
  AXPositionInstance normalized_start = start()->Clone();
  AXPositionInstance normalized_end = end()->Clone();
  NormalizeTextRange(normalized_start, normalized_end);

  *result = nullptr;
  AXPositionInstance matched_range_start = nullptr;
  AXPositionInstance matched_range_end = nullptr;

  std::vector<AXNodeRange> anchors;
  AXNodeRange range(normalized_start->Clone(), normalized_end->Clone());
  for (AXNodeRange leaf_text_range : range)
    anchors.emplace_back(std::move(leaf_text_range));

  auto expand_match = [&matched_range_start, &matched_range_end, is_backward](
                          auto& current_start, auto& current_end) {
    // The current AXRange has the attribute and its value that we are looking
    // for, we expand the matched text range if a previously matched exists,
    // otherwise initialize a newly matched text range.
    if (matched_range_start != nullptr && matched_range_end != nullptr) {
      // Continue expanding the matched text range forward/backward based on
      // the search direction.
      if (is_backward)
        matched_range_start = current_start->Clone();
      else
        matched_range_end = current_end->Clone();
    } else {
      // Initialize the matched text range. The first AXRange instance that
      // matches the attribute and its value encountered.
      matched_range_start = current_start->Clone();
      matched_range_end = current_end->Clone();
    }
  };

  HRESULT hr_result =
      is_backward
          ? FindAttributeRange(text_attribute_id, attribute_val,
                               anchors.crbegin(), anchors.crend(), expand_match)
          : FindAttributeRange(text_attribute_id, attribute_val,
                               anchors.cbegin(), anchors.cend(), expand_match);
  if (FAILED(hr_result))
    return E_FAIL;

  if (matched_range_start != nullptr && matched_range_end != nullptr)
    *result = CreateTextRangeProvider(std::move(matched_range_start),
                                      std::move(matched_range_end));
  return S_OK;
}

template <typename AnchorIterator, typename ExpandMatchLambda>
HRESULT AXPlatformNodeTextRangeProviderWin::FindAttributeRange(
    const TEXTATTRIBUTEID text_attribute_id,
    VARIANT attribute_val,
    const AnchorIterator first,
    const AnchorIterator last,
    ExpandMatchLambda expand_match) {
  AXPlatformNodeWin* current_platform_node;
  bool is_match_found = false;

  for (auto it = first; it != last; ++it) {
    const auto& current_start = it->anchor();
    const auto& current_end = it->focus();

    DCHECK(current_start->GetAnchor() == current_end->GetAnchor());

    AXPlatformNodeDelegate* delegate = GetDelegate(current_start);
    DCHECK(delegate);

    current_platform_node = static_cast<AXPlatformNodeWin*>(
        delegate->GetFromNodeID(current_start->GetAnchor()->id()));

    base::win::VariantVector current_attribute_value;
    if (FAILED(current_platform_node->GetTextAttributeValue(
            text_attribute_id, current_start->text_offset(),
            current_end->text_offset(), &current_attribute_value))) {
      return E_FAIL;
    }

    if (!current_attribute_value.Compare(attribute_val)) {
      // When we encounter an AXRange instance that matches the attribute
      // and its value which we are looking for and no previously matched text
      // range exists, we expand or initialize the matched range.
      is_match_found = true;
      expand_match(current_start, current_end);
    } else if (is_match_found) {
      // When we encounter an AXRange instance that does not match the attribute
      // and its value which we are looking for and a previously matched text
      // range exists, the previously matched text range is the result we found.
      break;
    }
  }
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::FindText(
    BSTR string,
    BOOL backwards,
    BOOL ignore_case,
    ITextRangeProvider** result) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_FINDTEXT);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_FINDTEXT);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_IN_1_OUT(string, result);

  std::u16string search_string = base::WideToUTF16(string);
  if (search_string.length() <= 0)
    return E_INVALIDARG;

  size_t appended_newlines_count = 0;
  std::u16string text_range = GetString(-1, &appended_newlines_count);
  size_t find_start;
  size_t find_length;
  if (base::i18n::StringSearch(search_string, text_range, &find_start,
                               &find_length, !ignore_case, !backwards) &&
      find_length > appended_newlines_count) {
    // TODO(https://crbug.com/1023599): There is a known issue here related to
    // text searches of a |string| starting and ending with a "\n", e.g.
    // "\nsometext" or "sometext\n" if the newline is computed from a line
    // breaking object. FindText() is rarely called, and when it is, it's not to
    // look for a string starting or ending with a newline. This may change
    // someday, and if so, we'll have to address this issue.
    const AXNode* common_anchor = start()->LowestCommonAnchor(*end());
    AXPositionInstance start_ancestor_position =
        start()->CreateAncestorPosition(common_anchor,
                                        ax::mojom::MoveDirection::kForward);
    DCHECK(!start_ancestor_position->IsNullPosition());
    AXPositionInstance end_ancestor_position = end()->CreateAncestorPosition(
        common_anchor, ax::mojom::MoveDirection::kForward);
    DCHECK(!end_ancestor_position->IsNullPosition());
    AXTreeID tree_id = start_ancestor_position->tree_id();
    AXNodeID anchor_id = start_ancestor_position->anchor_id();
    const int start_offset =
        start_ancestor_position->text_offset() + find_start;
    const int end_offset = start_offset + find_length - appended_newlines_count;
    const int max_end_offset = end_ancestor_position->text_offset();
    DCHECK(start_offset <= end_offset && end_offset <= max_end_offset);

    AXPositionInstance start = ui::AXNodePosition::CreateTextPosition(
                                   tree_id, anchor_id, start_offset,
                                   ax::mojom::TextAffinity::kDownstream)
                                   ->AsLeafTextPosition();
    AXPositionInstance end = ui::AXNodePosition::CreateTextPosition(
                                 tree_id, anchor_id, end_offset,
                                 ax::mojom::TextAffinity::kDownstream)
                                 ->AsLeafTextPosition();

    *result = CreateTextRangeProvider(start->Clone(), end->Clone());
  }
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::GetAttributeValue(
    TEXTATTRIBUTEID attribute_id,
    VARIANT* value) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_GETATTRIBUTEVALUE);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_GETATTRIBUTEVALUE);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_OUT(value);
  // Use a cloned range so that GetAttributeValue does not introduce
  // side-effects while normalizing the original range.
  AXPositionInstance normalized_start = start()->Clone();
  AXPositionInstance normalized_end = end()->Clone();
  NormalizeTextRange(normalized_start, normalized_end);

  base::win::VariantVector attribute_value;

  // The range is inclusive, so advance our endpoint to the next position
  const auto end_leaf_text_position = normalized_end->AsLeafTextPosition();
  auto end = end_leaf_text_position->CreateNextAnchorPosition();

  // Iterate over anchor positions
  for (auto it = normalized_start->AsLeafTextPosition();
       it->anchor_id() != end->anchor_id() || it->tree_id() != end->tree_id();
       it = it->CreateNextAnchorPosition()) {
    // If the iterator creates a null position, then it has likely overrun the
    // range, return failure. This is unexpected but may happen if the range
    // became inverted.
    DCHECK(!it->IsNullPosition());
    if (it->IsNullPosition())
      return E_FAIL;

    AXPlatformNodeDelegate* delegate = GetDelegate(it.get());
    DCHECK(it && delegate);

    AXPlatformNodeWin* platform_node = static_cast<AXPlatformNodeWin*>(
        delegate->GetFromNodeID(it->anchor_id()));
    DCHECK(platform_node);

    // Only get attributes for nodes in the tree. Exclude descendants of leaves
    // and ignored objects.
    platform_node = static_cast<AXPlatformNodeWin*>(
        AXPlatformNode::FromNativeViewAccessible(
            platform_node->GetDelegate()->GetLowestPlatformAncestor()));
    DCHECK(platform_node);

    base::win::VariantVector current_value;
    const bool at_end_leaf_text_anchor =
        it->anchor_id() == end_leaf_text_position->anchor_id() &&
        it->tree_id() == end_leaf_text_position->tree_id();
    const absl::optional<int> start_offset =
        it->IsTextPosition() ? absl::make_optional(it->text_offset())
                             : absl::nullopt;
    const absl::optional<int> end_offset =
        at_end_leaf_text_anchor
            ? absl::make_optional(end_leaf_text_position->text_offset())
            : absl::nullopt;
    HRESULT hr = platform_node->GetTextAttributeValue(
        attribute_id, start_offset, end_offset, &current_value);
    if (FAILED(hr))
      return E_FAIL;

    if (attribute_value.Type() == VT_EMPTY) {
      attribute_value = std::move(current_value);
    } else if (attribute_value != current_value) {
      V_VT(value) = VT_UNKNOWN;
      return ::UiaGetReservedMixedAttributeValue(&V_UNKNOWN(value));
    }
  }

  if (ShouldReleaseTextAttributeAsSafearray(attribute_id, attribute_value))
    *value = attribute_value.ReleaseAsSafearrayVariant();
  else
    *value = attribute_value.ReleaseAsScalarVariant();
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::GetBoundingRectangles(
    SAFEARRAY** screen_physical_pixel_rectangles) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_GETBOUNDINGRECTANGLES);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_GETBOUNDINGRECTANGLES);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_OUT(screen_physical_pixel_rectangles);

  *screen_physical_pixel_rectangles = nullptr;
  AXNodeRange range(start()->Clone(), end()->Clone());
  AXRangePhysicalPixelRectDelegate rect_delegate(this);
  std::vector<gfx::Rect> rects = range.GetRects(&rect_delegate);

  // 4 array items per rect: left, top, width, height
  SAFEARRAY* safe_array = SafeArrayCreateVector(
      VT_R8 /* element type */, 0 /* lower bound */, rects.size() * 4);

  if (!safe_array)
    return E_OUTOFMEMORY;

  if (rects.size() > 0) {
    double* double_array = nullptr;
    HRESULT hr = SafeArrayAccessData(safe_array,
                                     reinterpret_cast<void**>(&double_array));

    if (SUCCEEDED(hr)) {
      for (size_t rect_index = 0; rect_index < rects.size(); rect_index++) {
        const gfx::Rect& rect = rects[rect_index];
        double_array[rect_index * 4] = rect.x();
        double_array[rect_index * 4 + 1] = rect.y();
        double_array[rect_index * 4 + 2] = rect.width();
        double_array[rect_index * 4 + 3] = rect.height();
      }
      hr = SafeArrayUnaccessData(safe_array);
    }

    if (FAILED(hr)) {
      DCHECK(safe_array);
      SafeArrayDestroy(safe_array);
      return E_FAIL;
    }
  }

  *screen_physical_pixel_rectangles = safe_array;
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::GetEnclosingElement(
    IRawElementProviderSimple** element) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_GETENCLOSINGELEMENT);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_GETENCLOSINGELEMENT);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_OUT(element);

  AXPlatformNodeWin* enclosing_node = GetLowestAccessibleCommonPlatformNode();
  if (!enclosing_node)
    return UIA_E_ELEMENTNOTAVAILABLE;

  enclosing_node->GetNativeViewAccessible()->QueryInterface(
      IID_PPV_ARGS(element));

  DCHECK(*element);
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::GetText(int max_count, BSTR* text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_GETTEXT);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_GETTEXT);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_OUT(text);

  // -1 is a valid value that signifies that the caller wants complete text.
  // Any other negative value is an invalid argument.
  if (max_count < -1)
    return E_INVALIDARG;

  std::wstring full_text = base::UTF16ToWide(GetString(max_count));
  if (!full_text.empty()) {
    size_t length = full_text.length();

    if (max_count != -1 && max_count < static_cast<int>(length))
      *text = SysAllocStringLen(full_text.c_str(), max_count);
    else
      *text = SysAllocStringLen(full_text.c_str(), length);
  } else {
    *text = SysAllocString(L"");
  }
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::Move(TextUnit unit,
                                                 int count,
                                                 int* units_moved) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_MOVE);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_MOVE);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_OUT(units_moved);

  // Per MSDN, move with zero count has no effect.
  if (count == 0)
    return S_OK;

  // Save a clone of start and end, in case one of the moves fails.
  auto start_backup = start()->Clone();
  auto end_backup = end()->Clone();
  bool is_degenerate_range = (*start() == *end());

  // Move the start of the text range forward or backward in the document by the
  // requested number of text unit boundaries.
  int start_units_moved = 0;
  HRESULT hr = MoveEndpointByUnitImpl(TextPatternRangeEndpoint_Start, unit,
                                      count, &start_units_moved);

  bool succeeded_move = SUCCEEDED(hr) && start_units_moved != 0;
  if (succeeded_move) {
    SetEnd(start()->Clone());
    if (!is_degenerate_range) {
      bool forwards = count > 0;
      if (forwards && start()->AtEndOfContent()) {
        // The start is at the end of the document, so move the start backward
        // by one text unit to expand the text range from the degenerate range
        // state.
        int current_start_units_moved = 0;
        hr = MoveEndpointByUnitImpl(TextPatternRangeEndpoint_Start, unit, -1,
                                    &current_start_units_moved);
        start_units_moved -= 1;
        succeeded_move = SUCCEEDED(hr) && current_start_units_moved == -1 &&
                         start_units_moved > 0;
      } else {
        // The start is not at the end of the document, so move the endpoint
        // forward by one text unit to expand the text range from the degenerate
        // state.
        int end_units_moved = 0;
        hr = MoveEndpointByUnitImpl(TextPatternRangeEndpoint_End, unit, 1,
                                    &end_units_moved);
        succeeded_move = SUCCEEDED(hr) && end_units_moved == 1;
      }

      // Because Windows ATs behave undesirably when the start and end endpoints
      // are not in the same anchor (for character and word navigation), make
      // sure to bring back the end endpoint to the end of the start's anchor.
      if (start()->anchor_id() != end()->anchor_id() &&
          (unit == TextUnit_Character || unit == TextUnit_Word)) {
        ExpandToEnclosingUnitImpl(unit);
      }
    }
  }

  if (!succeeded_move) {
    SetStart(std::move(start_backup));
    SetEnd(std::move(end_backup));
    start_units_moved = 0;
    if (!SUCCEEDED(hr))
      return hr;
  }

  *units_moved = start_units_moved;
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::MoveEndpointByUnit(
    TextPatternRangeEndpoint endpoint,
    TextUnit unit,
    int count,
    int* units_moved) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_MOVEENDPOINTBYUNIT);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_MOVEENDPOINTBYUNIT);
  return MoveEndpointByUnitImpl(endpoint, unit, count, units_moved);
}

HRESULT AXPlatformNodeTextRangeProviderWin::MoveEndpointByUnitImpl(
    TextPatternRangeEndpoint endpoint,
    TextUnit unit,
    int count,
    int* units_moved) {
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_OUT(units_moved);

  // Per MSDN, MoveEndpointByUnit with zero count has no effect.
  if (count == 0) {
    *units_moved = 0;
    return S_OK;
  }

  bool is_start_endpoint = endpoint == TextPatternRangeEndpoint_Start;
  AXPositionInstance position_to_move =
      is_start_endpoint ? start()->Clone() : end()->Clone();

  AXPositionInstance new_position;
  switch (unit) {
    case TextUnit_Character:
      new_position =
          MoveEndpointByCharacter(position_to_move, count, units_moved);
      break;
    case TextUnit_Format:
      new_position = MoveEndpointByFormat(position_to_move, is_start_endpoint,
                                          count, units_moved);
      break;
    case TextUnit_Word:
      new_position = MoveEndpointByWord(position_to_move, count, units_moved);
      break;
    case TextUnit_Line:
      new_position = MoveEndpointByLine(position_to_move, is_start_endpoint,
                                        count, units_moved);
      break;
    case TextUnit_Paragraph:
      new_position = MoveEndpointByParagraph(
          position_to_move, is_start_endpoint, count, units_moved);
      break;
    case TextUnit_Page:
      new_position = MoveEndpointByPage(position_to_move, is_start_endpoint,
                                        count, units_moved);
      break;
    case TextUnit_Document:
      new_position =
          MoveEndpointByDocument(position_to_move, count, units_moved);
      break;
    default:
      return UIA_E_NOTSUPPORTED;
  }
  if (is_start_endpoint)
    SetStart(std::move(new_position));
  else
    SetEnd(std::move(new_position));

  // If the start was moved past the end, create a degenerate range with the end
  // equal to the start; do the equivalent if the end moved past the start.
  absl::optional<int> endpoint_comparison =
      AXNodeRange::CompareEndpoints(start().get(), end().get());
  DCHECK(endpoint_comparison.has_value());

  if (endpoint_comparison.value_or(0) > 0) {
    if (is_start_endpoint)
      SetEnd(start()->Clone());
    else
      SetStart(end()->Clone());
  }
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::MoveEndpointByRange(
    TextPatternRangeEndpoint this_endpoint,
    ITextRangeProvider* other,
    TextPatternRangeEndpoint other_endpoint) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_MOVEENPOINTBYRANGE);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_MOVEENPOINTBYRANGE);

  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_IN(other);

  Microsoft::WRL::ComPtr<AXPlatformNodeTextRangeProviderWin> other_provider;
  if (other->QueryInterface(IID_PPV_ARGS(&other_provider)) != S_OK)
    return UIA_E_INVALIDOPERATION;

  const AXPositionInstance& other_provider_endpoint =
      (other_endpoint == TextPatternRangeEndpoint_Start)
          ? other_provider->start()
          : other_provider->end();

  if (this_endpoint == TextPatternRangeEndpoint_Start) {
    SetStart(other_provider_endpoint->Clone());
    if (*start() > *end())
      SetEnd(start()->Clone());
  } else {
    SetEnd(other_provider_endpoint->Clone());
    if (*start() > *end())
      SetStart(end()->Clone());
  }
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::Select() {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_SELECT);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL();

  AXPositionInstance selection_start = start()->Clone();
  AXPositionInstance selection_end = end()->Clone();

  // Blink only supports selections within a single tree. So if start_ and  end_
  // are in different trees, we can't directly pass them to the render process
  // for selection.
  if (selection_start->tree_id() != selection_end->tree_id()) {
    // Prioritize the end position's tree, as a selection's focus object is the
    // end of a selection.
    selection_start = selection_end->CreatePositionAtStartOfAXTree();
  }

  DCHECK(!selection_start->IsNullPosition());
  DCHECK(!selection_end->IsNullPosition());
  DCHECK_EQ(selection_start->tree_id(), selection_end->tree_id());

  // TODO(crbug.com/1124051): Blink does not support selection on the list
  // markers. So if |selection_start| or |selection_end| are in list markers, we
  // don't perform selection and return success. Remove this check once this bug
  // is fixed.
  if (selection_start->GetAnchor()->IsInListMarker() ||
      selection_end->GetAnchor()->IsInListMarker()) {
    return S_OK;
  }

  AXPlatformNodeDelegate* delegate =
      GetDelegate(selection_start->tree_id(), selection_start->anchor_id());
  DCHECK(delegate);

  AXNodeRange new_selection_range(std::move(selection_start),
                                  std::move(selection_end));
  RemoveFocusFromPreviousSelectionIfNeeded(new_selection_range);

  AXActionData action_data;
  action_data.anchor_node_id = new_selection_range.anchor()->anchor_id();
  action_data.anchor_offset = new_selection_range.anchor()->text_offset();
  action_data.focus_node_id = new_selection_range.focus()->anchor_id();
  action_data.focus_offset = new_selection_range.focus()->text_offset();
  action_data.action = ax::mojom::Action::kSetSelection;

  delegate->AccessibilityPerformAction(action_data);
  return S_OK;
}

HRESULT AXPlatformNodeTextRangeProviderWin::AddToSelection() {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_ADDTOSELECTION);
  // Blink does not support disjoint text selections.
  return UIA_E_INVALIDOPERATION;
}

HRESULT
AXPlatformNodeTextRangeProviderWin::RemoveFromSelection() {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_REMOVEFROMSELECTION);
  // Blink does not support disjoint text selections.
  return UIA_E_INVALIDOPERATION;
}

HRESULT AXPlatformNodeTextRangeProviderWin::ScrollIntoView(BOOL align_to_top) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_SCROLLINTOVIEW);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL();

  const AXPositionInstance start_common_ancestor =
      start()->LowestCommonAncestor(*end(),
                                    ax::mojom::MoveDirection::kBackward);
  const AXPositionInstance end_common_ancestor =
      end()->LowestCommonAncestor(*start(), ax::mojom::MoveDirection::kForward);
  if (start_common_ancestor->IsNullPosition() ||
      end_common_ancestor->IsNullPosition()) {
    return E_INVALIDARG;
  }

  const AXNode* common_ancestor_anchor = start_common_ancestor->GetAnchor();
  DCHECK(common_ancestor_anchor == end_common_ancestor->GetAnchor());

  const AXTreeID common_ancestor_tree_id = start_common_ancestor->tree_id();
  const AXPlatformNodeDelegate* root_delegate =
      GetRootDelegate(common_ancestor_tree_id);
  DCHECK(root_delegate);
  const gfx::Rect root_frame_bounds = root_delegate->GetBoundsRect(
      AXCoordinateSystem::kFrame, AXClippingBehavior::kUnclipped);
  UIA_VALIDATE_BOUNDS(root_frame_bounds);

  const AXPlatformNode* common_ancestor_platform_node =
      GetOwner()->GetDelegate()->GetFromTreeIDAndNodeID(
          common_ancestor_tree_id, common_ancestor_anchor->id());
  DCHECK(common_ancestor_platform_node);
  AXPlatformNodeDelegate* common_ancestor_delegate =
      common_ancestor_platform_node->GetDelegate();
  DCHECK(common_ancestor_delegate);
  const gfx::Rect text_range_container_frame_bounds =
      common_ancestor_delegate->GetBoundsRect(AXCoordinateSystem::kFrame,
                                              AXClippingBehavior::kUnclipped);
  UIA_VALIDATE_BOUNDS(text_range_container_frame_bounds);

  gfx::Point target_point;
  if (align_to_top) {
    target_point = gfx::Point(root_frame_bounds.x(), root_frame_bounds.y());
  } else {
    target_point =
        gfx::Point(root_frame_bounds.x(),
                   root_frame_bounds.y() + root_frame_bounds.height());
  }

  if ((align_to_top && start()->GetAnchor()->IsText()) ||
      (!align_to_top && end()->GetAnchor()->IsText())) {
    const gfx::Rect text_range_frame_bounds =
        common_ancestor_delegate->GetInnerTextRangeBoundsRect(
            start_common_ancestor->text_offset(),
            end_common_ancestor->text_offset(), AXCoordinateSystem::kFrame,
            AXClippingBehavior::kUnclipped);
    UIA_VALIDATE_BOUNDS(text_range_frame_bounds);

    if (align_to_top) {
      target_point.Offset(0, -(text_range_container_frame_bounds.height() -
                               text_range_frame_bounds.height()));
    } else {
      target_point.Offset(0, -text_range_frame_bounds.height());
    }
  } else {
    if (!align_to_top)
      target_point.Offset(0, -text_range_container_frame_bounds.height());
  }

  const gfx::Rect root_screen_bounds = root_delegate->GetBoundsRect(
      AXCoordinateSystem::kScreenDIPs, AXClippingBehavior::kUnclipped);
  UIA_VALIDATE_BOUNDS(root_screen_bounds);
  target_point += root_screen_bounds.OffsetFromOrigin();

  AXActionData action_data;
  action_data.action = ax::mojom::Action::kScrollToPoint;
  action_data.target_node_id = common_ancestor_anchor->id();
  action_data.target_point = target_point;
  if (!common_ancestor_delegate->AccessibilityPerformAction(action_data))
    return E_FAIL;
  return S_OK;
}

// This function is expected to return a subset of the *direct* children of the
// common ancestor node. The subset should only include the direct children
// included - fully or partially - in the range.
HRESULT AXPlatformNodeTextRangeProviderWin::GetChildren(SAFEARRAY** children) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TEXTRANGE_GETCHILDREN);
  WIN_ACCESSIBILITY_API_PERF_HISTOGRAM(UMA_API_TEXTRANGE_GETCHILDREN);
  UIA_VALIDATE_TEXTRANGEPROVIDER_CALL_1_OUT(children);
  std::vector<gfx::NativeViewAccessible> descendants;

  AXPlatformNodeWin* start_anchor =
      GetPlatformNodeFromAXNode(start()->GetAnchor());
  AXPlatformNodeWin* end_anchor = GetPlatformNodeFromAXNode(end()->GetAnchor());
  AXPlatformNodeWin* common_anchor = GetLowestAccessibleCommonPlatformNode();
  if (!common_anchor || !start_anchor || !end_anchor)
    return UIA_E_ELEMENTNOTAVAILABLE;

  AXPlatformNodeDelegate* start_delegate = start_anchor->GetDelegate();
  AXPlatformNodeDelegate* end_delegate = end_anchor->GetDelegate();
  AXPlatformNodeDelegate* common_delegate = common_anchor->GetDelegate();

  descendants = common_delegate->GetUIADirectChildrenInRange(start_delegate,
                                                             end_delegate);

  SAFEARRAY* safe_array =
      SafeArrayCreateVector(VT_UNKNOWN, 0, descendants.size());

  if (!safe_array)
    return E_OUTOFMEMORY;

  if (safe_array->rgsabound->cElements != descendants.size()) {
    DCHECK(safe_array);
    SafeArrayDestroy(safe_array);
    return E_OUTOFMEMORY;
  }

  LONG i = 0;
  for (const gfx::NativeViewAccessible& descendant : descendants) {
    IRawElementProviderSimple* raw_provider;
    descendant->QueryInterface(IID_PPV_ARGS(&raw_provider));
    SafeArrayPutElement(safe_array, &i, raw_provider);
    ++i;
  }

  *children = safe_array;
  return S_OK;
}

// static
bool AXPlatformNodeTextRangeProviderWin::AtStartOfLinePredicate(
    const AXPositionInstance& position) {
  return !position->IsIgnored() && position->AtStartOfAnchor() &&
         (position->AtStartOfLine() || position->AtStartOfInlineBlock());
}

// static
bool AXPlatformNodeTextRangeProviderWin::AtEndOfLinePredicate(
    const AXPositionInstance& position) {
  return !position->IsIgnored() && position->AtEndOfAnchor() &&
         (position->AtEndOfLine() || position->AtStartOfInlineBlock());
}

// static
AXPlatformNodeTextRangeProviderWin::AXPositionInstance
AXPlatformNodeTextRangeProviderWin::GetNextTextBoundaryPosition(
    const AXPositionInstance& position,
    ax::mojom::TextBoundary boundary_type,
    AXBoundaryBehavior boundary_behavior,
    ax::mojom::MoveDirection boundary_direction) {
  // Override At[Start|End]OfLinePredicate for behavior specific to UIA.
  DCHECK_NE(boundary_type, ax::mojom::TextBoundary::kNone);
  switch (boundary_type) {
    case ax::mojom::TextBoundary::kLineStart:
      return position->CreateBoundaryStartPosition(
          boundary_behavior, boundary_direction,
          base::BindRepeating(&AtStartOfLinePredicate),
          base::BindRepeating(&AtEndOfLinePredicate));
    case ax::mojom::TextBoundary::kLineEnd:
      return position->CreateBoundaryEndPosition(
          boundary_behavior, boundary_direction,
          base::BindRepeating(&AtStartOfLinePredicate),
          base::BindRepeating(&AtEndOfLinePredicate));
    default:
      return position->CreatePositionAtTextBoundary(
          boundary_type, boundary_direction, boundary_behavior);
  }
}

std::u16string AXPlatformNodeTextRangeProviderWin::GetString(
    int max_count,
    size_t* appended_newlines_count) {
  AXNodeRange range(start()->Clone(), end()->Clone());
  return range.GetText(AXTextConcatenationBehavior::kWithParagraphBreaks,
                       max_count, false, appended_newlines_count);
}

AXPlatformNodeWin* AXPlatformNodeTextRangeProviderWin::GetOwner() const {
  // Unit tests can't call |GetPlatformNodeFromTree|, so they must provide an
  // owner node.
  if (owner_for_test_.Get())
    return owner_for_test_.Get();

  const AXPositionInstance& position =
      !start()->IsNullPosition() ? start() : end();
  // If start and end are both null, there's no owner.
  if (position->IsNullPosition())
    return nullptr;

  const AXNode* anchor = position->GetAnchor();
  DCHECK(anchor);
  AXTreeID tree_id = anchor->tree()->GetAXTreeID();
  const AXTreeManager* ax_tree_manager =
      AXTreeManagerMap::GetInstance().GetManager(tree_id);
  DCHECK(ax_tree_manager);

  const AXPlatformTreeManager* platform_tree_manager =
      static_cast<const AXPlatformTreeManager*>(ax_tree_manager);
  DCHECK(platform_tree_manager);

  return static_cast<AXPlatformNodeWin*>(
      platform_tree_manager->GetPlatformNodeFromTree(*anchor));
}

AXPlatformNodeDelegate* AXPlatformNodeTextRangeProviderWin::GetDelegate(
    const AXPositionInstanceType* position) const {
  return GetDelegate(position->tree_id(), position->anchor_id());
}

AXPlatformNodeDelegate* AXPlatformNodeTextRangeProviderWin::GetDelegate(
    const AXTreeID tree_id,
    const AXNodeID node_id) const {
  AXPlatformNode* platform_node =
      GetOwner()->GetDelegate()->GetFromTreeIDAndNodeID(tree_id, node_id);
  if (!platform_node)
    return nullptr;

  return platform_node->GetDelegate();
}

AXPlatformNodeTextRangeProviderWin::AXPositionInstance
AXPlatformNodeTextRangeProviderWin::MoveEndpointByCharacter(
    const AXPositionInstance& endpoint,
    const int count,
    int* units_moved) {
  return MoveEndpointByUnitHelper(std::move(endpoint),
                                  ax::mojom::TextBoundary::kCharacter, count,
                                  units_moved);
}

AXPlatformNodeTextRangeProviderWin::AXPositionInstance
AXPlatformNodeTextRangeProviderWin::MoveEndpointByWord(
    const AXPositionInstance& endpoint,
    const int count,
    int* units_moved) {
  return MoveEndpointByUnitHelper(std::move(endpoint),
                                  ax::mojom::TextBoundary::kWordStart, count,
                                  units_moved);
}

AXPlatformNodeTextRangeProviderWin::AXPositionInstance
AXPlatformNodeTextRangeProviderWin::MoveEndpointByLine(
    const AXPositionInstance& endpoint,
    bool is_start_endpoint,
    const int count,
    int* units_moved) {
  return MoveEndpointByUnitHelper(std::move(endpoint),
                                  is_start_endpoint
                                      ? ax::mojom::TextBoundary::kLineStart
                                      : ax::mojom::TextBoundary::kLineEnd,
                                  count, units_moved);
}

AXPlatformNodeTextRangeProviderWin::AXPositionInstance
AXPlatformNodeTextRangeProviderWin::MoveEndpointByFormat(
    const AXPositionInstance& endpoint,
    const bool is_start_endpoint,
    const int count,
    int* units_moved) {
  return MoveEndpointByUnitHelper(std::move(endpoint),
                                  is_start_endpoint
                                      ? ax::mojom::TextBoundary::kFormatStart
                                      : ax::mojom::TextBoundary::kFormatEnd,
                                  count, units_moved);
}

AXPlatformNodeTextRangeProviderWin::AXPositionInstance
AXPlatformNodeTextRangeProviderWin::MoveEndpointByParagraph(
    const AXPositionInstance& endpoint,
    const bool is_start_endpoint,
    const int count,
    int* units_moved) {
  return MoveEndpointByUnitHelper(
      std::move(endpoint),
      ax::mojom::TextBoundary::kParagraphStartSkippingEmptyParagraphs, count,
      units_moved);
}

AXPlatformNodeTextRangeProviderWin::AXPositionInstance
AXPlatformNodeTextRangeProviderWin::MoveEndpointByPage(
    const AXPositionInstance& endpoint,
    const bool is_start_endpoint,
    const int count,
    int* units_moved) {
  // Per UIA spec, if the document containing the current endpoint doesn't
  // support pagination, default to document navigation.
  //
  // Note that the "ax::mojom::MoveDirection" should not matter when calculating
  // the ancestor position for use when navigating by page or document, so we
  // use a backward direction as the default.
  AXPositionInstance common_ancestor = start()->LowestCommonAncestor(
      *end(), ax::mojom::MoveDirection::kBackward);
  if (!common_ancestor->GetAnchor()->tree()->HasPaginationSupport())
    return MoveEndpointByDocument(std::move(endpoint), count, units_moved);

  return MoveEndpointByUnitHelper(std::move(endpoint),
                                  is_start_endpoint
                                      ? ax::mojom::TextBoundary::kPageStart
                                      : ax::mojom::TextBoundary::kPageEnd,
                                  count, units_moved);
}

AXPlatformNodeTextRangeProviderWin::AXPositionInstance
AXPlatformNodeTextRangeProviderWin::MoveEndpointByDocument(
    const AXPositionInstance& endpoint,
    const int count,
    int* units_moved) {
  DCHECK_NE(count, 0);

  if (count < 0) {
    *units_moved = !endpoint->AtStartOfContent() ? -1 : 0;
    return endpoint->CreatePositionAtStartOfContent();
  }
  *units_moved = !endpoint->AtEndOfContent() ? 1 : 0;
  return endpoint->CreatePositionAtEndOfContent();
}

AXPlatformNodeTextRangeProviderWin::AXPositionInstance
AXPlatformNodeTextRangeProviderWin::MoveEndpointByUnitHelper(
    const AXPositionInstance& endpoint,
    const ax::mojom::TextBoundary boundary_type,
    const int count,
    int* units_moved) {
  DCHECK_NE(count, 0);
  const ax::mojom::MoveDirection boundary_direction =
      (count > 0) ? ax::mojom::MoveDirection::kForward
                  : ax::mojom::MoveDirection::kBackward;

  // Most of the methods used to create the next/previous position go back and
  // forth creating a leaf text position and rooting the result to the original
  // position's anchor; avoid this by normalizing to a leaf text position.
  AXPositionInstance current_endpoint = endpoint->AsLeafTextPosition();

  for (int iteration = 0; iteration < std::abs(count); ++iteration) {
    do {
      AXPositionInstance next_endpoint = GetNextTextBoundaryPosition(
          current_endpoint, boundary_type,
          AXBoundaryBehavior::kStopAtLastAnchorBoundary, boundary_direction);
      DCHECK(next_endpoint->IsLeafTextPosition());

      // Since AXBoundaryBehavior::kStopAtLastAnchorBoundary forces the next
      // text boundary position to be different than the input position, the
      // only case where these are equal is when they're already located at the
      // last anchor boundary. In such case, there is no next position to move
      // to.
      if (next_endpoint->GetAnchor() == current_endpoint->GetAnchor() &&
          *next_endpoint == *current_endpoint) {
        *units_moved = (count > 0) ? iteration : -iteration;
        return current_endpoint;
      }
      current_endpoint = std::move(next_endpoint);
      // Loop until we're not on a position that is ignored for text navigation.
      // There is one exception for character navigation - since the ignored
      // anchor is represented by an embedded object character, we allow
      // navigation by character for consistency (i.e. you should be able to
      // move by character the same number of characters that are represented by
      // the ranges flat string buffer).
    } while (boundary_type != ax::mojom::TextBoundary::kCharacter &&
             current_endpoint->GetAnchor()->IsIgnoredForTextNavigation());
  }

  *units_moved = count;
  return current_endpoint;
}

void AXPlatformNodeTextRangeProviderWin::NormalizeTextRange(
    AXPositionInstance& start,
    AXPositionInstance& end) {
  if (!start->IsValid() || !end->IsValid())
    return;

  // If either endpoint is anchored to an ignored node,
  // first snap them both to be unignored positions.
  NormalizeAsUnignoredTextRange(start, end);

  // When a text range or one end of AXTree::Selection is inside the atomic text
  // field, the precise state of the TextPattern must be preserved so that the
  // UIA client can handle scenarios such as determining which characters were
  // deleted. So normalization must be bypassed.
  if (HasTextRangeOrSelectionInAtomicTextField(start, end))
    return;

  AXPositionInstance normalized_start =
      start->AsLeafTextPositionBeforeCharacter();

  // For a degenerate range, the |end_| will always be the same as the
  // normalized start, so there's no need to compute the normalized end.
  // However, a degenerate range might go undetected if there's an ignored node
  // (or many) between the two endpoints. For this reason, we need to
  // compare the |end_| with both the |start_| and the |normalized_start|.
  bool is_degenerate = *start == *end || *normalized_start == *end;
  AXPositionInstance normalized_end =
      is_degenerate ? normalized_start->Clone()
                    : end->AsLeafTextPositionAfterCharacter();

  if (!normalized_start->IsNullPosition() &&
      !normalized_end->IsNullPosition()) {
    start = std::move(normalized_start);
    end = std::move(normalized_end);
  }

  DCHECK_LE(*start, *end);
}

// static
void AXPlatformNodeTextRangeProviderWin::NormalizeAsUnignoredPosition(
    AXPositionInstance& position) {
  if (position->IsNullPosition() || !position->IsValid())
    return;

  if (position->IsIgnored()) {
    AXPositionInstance normalized_position = position->AsUnignoredPosition(
        AXPositionAdjustmentBehavior::kMoveForward);
    if (normalized_position->IsNullPosition()) {
      normalized_position = position->AsUnignoredPosition(
          AXPositionAdjustmentBehavior::kMoveBackward);
    }

    if (!normalized_position->IsNullPosition())
      position = std::move(normalized_position);
  }
  DCHECK(!position->IsNullPosition());
}

// static
void AXPlatformNodeTextRangeProviderWin::NormalizeAsUnignoredTextRange(
    AXPositionInstance& start,
    AXPositionInstance& end) {
  if (!start->IsValid() || !end->IsValid())
    return;

  if (!start->IsIgnored() && !end->IsIgnored())
    return;
  NormalizeAsUnignoredPosition(start);
  NormalizeAsUnignoredPosition(end);
  DCHECK_LE(*start, *end);
}

AXPlatformNodeDelegate* AXPlatformNodeTextRangeProviderWin::GetRootDelegate(
    const ui::AXTreeID tree_id) {
  const AXTreeManager* ax_tree_manager =
      AXTreeManagerMap::GetInstance().GetManager(tree_id);
  DCHECK(ax_tree_manager);
  AXNode* root_node = ax_tree_manager->GetRootAsAXNode();
  const AXPlatformNode* root_platform_node =
      GetOwner()->GetDelegate()->GetFromTreeIDAndNodeID(tree_id,
                                                        root_node->id());
  DCHECK(root_platform_node);
  return root_platform_node->GetDelegate();
}

void AXPlatformNodeTextRangeProviderWin::SetStart(
    AXPositionInstance new_start) {
  endpoints_.SetStart(std::move(new_start));
}

void AXPlatformNodeTextRangeProviderWin::SetEnd(AXPositionInstance new_end) {
  endpoints_.SetEnd(std::move(new_end));
}

void AXPlatformNodeTextRangeProviderWin::SetOwnerForTesting(
    AXPlatformNodeWin* owner) {
  owner_for_test_ = owner;
}

AXNode* AXPlatformNodeTextRangeProviderWin::GetSelectionCommonAnchor() {
  AXPlatformNodeDelegate* delegate = GetOwner()->GetDelegate();
  ui::AXTree::Selection unignored_selection = delegate->GetUnignoredSelection();
  AXPlatformNode* anchor_object =
      delegate->GetFromNodeID(unignored_selection.anchor_object_id);
  AXPlatformNode* focus_object =
      delegate->GetFromNodeID(unignored_selection.focus_object_id);

  if (!anchor_object || !focus_object)
    return nullptr;

  AXNodePosition::AXPositionInstance start =
      anchor_object->GetDelegate()->CreateTextPositionAt(
          unignored_selection.anchor_offset);
  AXNodePosition::AXPositionInstance end =
      focus_object->GetDelegate()->CreateTextPositionAt(
          unignored_selection.focus_offset);

  return start->LowestCommonAnchor(*end);
}

// When the current selection is inside a focusable element, the DOM focused
// element will correspond to this element. When we update the selection to be
// on a different element that is not focusable, the new selection won't be
// applied unless we remove the DOM focused element. For example, with Narrator,
// if we move by word from a text field (focusable) to a static text (not
// focusable), the selection will stay on the text field because the DOM focused
// element will still be the text field. To avoid that, we need to remove the
// focus from this element. Since |ax::mojom::Action::kBlur| is not implemented,
// we perform a |ax::mojom::Action::focus| action on the root node. The result
// is the same.
void AXPlatformNodeTextRangeProviderWin::
    RemoveFocusFromPreviousSelectionIfNeeded(const AXNodeRange& new_selection) {
  const AXNode* old_selection_node = GetSelectionCommonAnchor();
  const AXNode* new_selection_node =
      new_selection.anchor()->LowestCommonAnchor(*new_selection.focus());

  if (!old_selection_node)
    return;

  if (!new_selection_node ||
      (old_selection_node->HasState(ax::mojom::State::kFocusable) &&
       !new_selection_node->HasState(ax::mojom::State::kFocusable))) {
    AXPlatformNodeDelegate* root_delegate =
        GetRootDelegate(old_selection_node->tree()->GetAXTreeID());
    DCHECK(root_delegate);

    AXActionData focus_action;
    focus_action.action = ax::mojom::Action::kFocus;
    root_delegate->AccessibilityPerformAction(focus_action);
  }
}

AXPlatformNodeWin*
AXPlatformNodeTextRangeProviderWin::GetPlatformNodeFromAXNode(
    const AXNode* node) const {
  if (!node)
    return nullptr;

  // TODO(kschmi): Update to use AXTreeManager.
  AXPlatformNodeWin* platform_node =
      static_cast<AXPlatformNodeWin*>(AXPlatformNode::FromNativeViewAccessible(
          GetDelegate(node->tree()->GetAXTreeID(), node->id())
              ->GetNativeViewAccessible()));
  DCHECK(platform_node);

  return platform_node;
}

AXPlatformNodeWin*
AXPlatformNodeTextRangeProviderWin::GetLowestAccessibleCommonPlatformNode()
    const {
  AXNode* common_anchor = start()->LowestCommonAnchor(*end());
  if (!common_anchor)
    return nullptr;

  return GetPlatformNodeFromAXNode(common_anchor)
      ->GetLowestAccessibleElementForUIA();
}

bool AXPlatformNodeTextRangeProviderWin::
    HasTextRangeOrSelectionInAtomicTextField(
        const AXPositionInstance& start_position,
        const AXPositionInstance& end_position) const {
  // This condition fixes issues when the caret is inside an atomic text field,
  // but causes more issues when used inside of a non-atomic text field. An
  // atomic text field does not expose its internal implementation to assistive
  // software, appearing as a single leaf node in the accessibility tree. It
  // includes <input>, <textarea> and Views-based text fields.
  //
  // For this reason, if we have a caret or a selection inside of an editable
  // node, restrict this to an atomic text field as we gain nothing from using
  // it in a non-atomic text field.
  //
  // Note that "AXPlatformNodeDelegate::IsDescendantOfAtomicTextField()" also
  // returns true when this node is at the root of an atomic text field, i.e.
  // the node could either be a descendant or it could be equivalent to the
  // field's root node.
  bool is_start_in_text_field =
      start_position->GetAnchor()->IsDescendantOfAtomicTextField();
  bool is_end_in_text_field =
      end_position->GetAnchor()->IsDescendantOfAtomicTextField();
  AXPlatformNodeDelegate* start_delegate = GetDelegate(start_position.get());
  AXPlatformNodeDelegate* end_delegate = GetDelegate(start_position.get());

  // Return true when both ends of a text range are inside the atomic
  // text field (e.g. a caret perceived by the AT), or when either endpoint of
  // the AXTree::Selection is inside the atomic text field.
  return (is_start_in_text_field && is_end_in_text_field) ||
         (is_start_in_text_field && start_delegate &&
          start_delegate->HasVisibleCaretOrSelection()) ||
         (is_end_in_text_field && end_delegate &&
          end_delegate->HasVisibleCaretOrSelection());
}

// static
bool AXPlatformNodeTextRangeProviderWin::TextAttributeIsArrayType(
    TEXTATTRIBUTEID attribute_id) {
  // https://docs.microsoft.com/en-us/windows/win32/winauto/uiauto-textattribute-ids
  return attribute_id == UIA_AnnotationObjectsAttributeId ||
         attribute_id == UIA_AnnotationTypesAttributeId ||
         attribute_id == UIA_TabsAttributeId;
}

// static
bool AXPlatformNodeTextRangeProviderWin::TextAttributeIsUiaReservedValue(
    const base::win::VariantVector& vector) {
  // Reserved values are always IUnknown.
  if (vector.Type() != VT_UNKNOWN)
    return false;

  base::win::ScopedVariant mixed_attribute_value_variant;
  {
    Microsoft::WRL::ComPtr<IUnknown> mixed_attribute_value;
    HRESULT hr = ::UiaGetReservedMixedAttributeValue(&mixed_attribute_value);
    DCHECK(SUCCEEDED(hr));
    mixed_attribute_value_variant.Set(mixed_attribute_value.Get());
  }

  base::win::ScopedVariant not_supported_value_variant;
  {
    Microsoft::WRL::ComPtr<IUnknown> not_supported_value;
    HRESULT hr = ::UiaGetReservedNotSupportedValue(&not_supported_value);
    DCHECK(SUCCEEDED(hr));
    not_supported_value_variant.Set(not_supported_value.Get());
  }

  return !vector.Compare(mixed_attribute_value_variant) ||
         !vector.Compare(not_supported_value_variant);
}

// static
bool AXPlatformNodeTextRangeProviderWin::ShouldReleaseTextAttributeAsSafearray(
    TEXTATTRIBUTEID attribute_id,
    const base::win::VariantVector& attribute_value) {
  // |vector| may be pre-populated with a UIA reserved value. In such a case, we
  // must release as a scalar variant.
  return TextAttributeIsArrayType(attribute_id) &&
         !TextAttributeIsUiaReservedValue(attribute_value);
}

AXPlatformNodeTextRangeProviderWin::TextRangeEndpoints::TextRangeEndpoints() {
  start_ = AXNodePosition::CreateNullPosition();
  end_ = AXNodePosition::CreateNullPosition();
}

AXPlatformNodeTextRangeProviderWin::TextRangeEndpoints::~TextRangeEndpoints() {
  SetStart(AXNodePosition::CreateNullPosition());
  SetEnd(AXNodePosition::CreateNullPosition());
}

void AXPlatformNodeTextRangeProviderWin::TextRangeEndpoints::SetStart(
    AXPositionInstance new_start) {
  bool did_tree_change = start_->tree_id() != new_start->tree_id();
  // TODO(bebeaudr): We can't use IsNullPosition() here because of
  // https://crbug.com/1152939. Once this is fixed, we can go back to
  // IsNullPosition().
  if (did_tree_change && start_->kind() != AXPositionKind::NULL_POSITION &&
      start_->tree_id() != end_->tree_id()) {
    RemoveObserver(start_->tree_id());
  }

  start_ = std::move(new_start);

  if (did_tree_change && !start_->IsNullPosition() &&
      start_->tree_id() != end_->tree_id()) {
    AddObserver(start_->tree_id());
  }
}

void AXPlatformNodeTextRangeProviderWin::TextRangeEndpoints::SetEnd(
    AXPositionInstance new_end) {
  bool did_tree_change = end_->tree_id() != new_end->tree_id();
  // TODO(bebeaudr): We can't use IsNullPosition() here because of
  // https://crbug.com/1152939. Once this is fixed, we can go back to
  // IsNullPosition().
  if (did_tree_change && end_->kind() != AXPositionKind::NULL_POSITION &&
      end_->tree_id() != start_->tree_id()) {
    RemoveObserver(end_->tree_id());
  }

  end_ = std::move(new_end);

  if (did_tree_change && !end_->IsNullPosition() &&
      start_->tree_id() != end_->tree_id()) {
    AddObserver(end_->tree_id());
  }
}

void AXPlatformNodeTextRangeProviderWin::TextRangeEndpoints::AddObserver(
    const AXTreeID tree_id) {
  AXTreeManager* ax_tree_manager =
      AXTreeManagerMap::GetInstance().GetManager(tree_id);
  DCHECK(ax_tree_manager);
  ax_tree_manager->AddObserver(this);
}

void AXPlatformNodeTextRangeProviderWin::TextRangeEndpoints::RemoveObserver(
    const AXTreeID tree_id) {
  AXTreeManager* ax_tree_manager =
      AXTreeManagerMap::GetInstance().GetManager(tree_id);
  if (ax_tree_manager)
    ax_tree_manager->RemoveObserver(this);
}

// Ensures that our endpoints are located on non-deleted nodes (step 1, case A
// and B). See comment in header file for more details.
void AXPlatformNodeTextRangeProviderWin::TextRangeEndpoints::
    OnSubtreeWillBeDeleted(AXTree* tree, AXNode* node) {
  // If an endpoint is on a node that is included in a subtree that is about to
  // be deleted, move endpoint up to the parent of the deleted subtree's root
  // since we want to ensure that the endpoints of a text range provider are
  // always valid positions. Otherwise, the range will be stuck on nodes that
  // don't exist anymore.
  DCHECK(tree);
  DCHECK(node);
  DCHECK_EQ(tree->GetAXTreeID(), node->tree()->GetAXTreeID());

  AdjustEndpointForSubtreeDeletion(tree, node, true /* is_start_endpoint */);
  AdjustEndpointForSubtreeDeletion(tree, node, false /* is_start_endpoint */);
}

void AXPlatformNodeTextRangeProviderWin::TextRangeEndpoints::
    AdjustEndpointForSubtreeDeletion(AXTree* tree,
                                     AXNode* node,
                                     bool is_start_endpoint) {
  AXPositionInstance endpoint =
      is_start_endpoint ? start_->Clone() : end_->Clone();
  if (tree->GetAXTreeID() != endpoint->tree_id())
    return;

  // When the subtree of the root node will be deleted, we can be certain that
  // our endpoint should be invalidated. We know it's the root node when the
  // node doesn't have a parent.
  AXNode* endpoint_anchor = endpoint->GetAnchor();
  if (!node->GetParent() || !endpoint_anchor) {
    is_start_endpoint ? SetStart(AXNodePosition::CreateNullPosition())
                      : SetEnd(AXNodePosition::CreateNullPosition());
    return;
  }

  // Fast check for the common case - there are many tree updates and the
  // endpoints probably are not in the deleted subtree. Note that
  // CreateAncestorPosition/GetParentPosition can be expensive for text
  // positions.
  if (!endpoint_anchor->IsDescendantOfCrossingTreeBoundary(node))
    return;

  AXPositionInstance new_endpoint = endpoint->CreateAncestorPosition(
      node, ax::mojom::MoveDirection::kForward);

  // Obviously, we want the position to be on the parent of |node| and not on
  // |node| itself since it's about to be deleted.
  new_endpoint = new_endpoint->CreateParentPosition();
  AXPositionInstance other_endpoint =
      is_start_endpoint ? end_->Clone() : start_->Clone();

  // Convert |new_endpoint| and |other_endpoint| to unignored positions to avoid
  // AXPosition::SlowCompareTo in the < operator below.
  NormalizeAsUnignoredPosition(new_endpoint);
  NormalizeAsUnignoredPosition(other_endpoint);
  DCHECK(!new_endpoint->IsIgnored());
  DCHECK(!other_endpoint->IsIgnored());

  // If after all the above operations we're still left with a new endpoint that
  // is a descendant of the subtree root being deleted, just point at a null
  // position and don't crash later on. This can happen when the entire parent
  // chain of the subtree is ignored.
  endpoint_anchor = new_endpoint->GetAnchor();
  if (!endpoint_anchor ||
      endpoint_anchor->IsDescendantOfCrossingTreeBoundary(node))
    new_endpoint = AXNodePosition::CreateNullPosition();

  // Create a degenerate range at the new position if we have an inverted range
  // - which occurs when the |end_| comes before the |start_|. This could have
  // happened due to the new endpoint walking forwards or backwards when
  // normalizing above. If we don't set the opposite endpoint to something that
  // we know will be safe (i.e. not in a deleted subtree) we'll crash later on
  // when trying to create a valid position.
  if (is_start_endpoint) {
    if (*other_endpoint < *new_endpoint)
      SetEnd(new_endpoint->Clone());

    SetStart(std::move(new_endpoint));
    validation_necessary_for_start_ = {tree->GetAXTreeID(), node->id()};
  } else {
    if (*new_endpoint < *other_endpoint)
      SetStart(new_endpoint->Clone());

    SetEnd(std::move(new_endpoint));
    validation_necessary_for_end_ = {tree->GetAXTreeID(), node->id()};
  }
}

// Ensures that our endpoints are always valid (step 2, all scenarios). See
// comment in header file for more details.
void AXPlatformNodeTextRangeProviderWin::TextRangeEndpoints::OnNodeDeleted(
    AXTree* tree,
    AXNodeID node_id) {
  DCHECK(tree);

  if (validation_necessary_for_start_.has_value() &&
      validation_necessary_for_start_->tree_id == tree->GetAXTreeID() &&
      validation_necessary_for_start_->node_id == node_id) {
    if (!start_->IsNullPosition() && start_->GetAnchor()->IsDataValid())
      SetStart(start_->AsValidPosition());
    else
      SetStart(AXNodePosition::CreateNullPosition());

    validation_necessary_for_start_ = absl::nullopt;
  }

  if (validation_necessary_for_end_.has_value() &&
      validation_necessary_for_end_->tree_id == tree->GetAXTreeID() &&
      validation_necessary_for_end_->node_id == node_id) {
    if (!end_->IsNullPosition() && end_->GetAnchor()->IsDataValid())
      SetEnd(end_->AsValidPosition());
    else
      SetEnd(AXNodePosition::CreateNullPosition());

    validation_necessary_for_end_ = absl::nullopt;
  }
}

void AXPlatformNodeTextRangeProviderWin::TextRangeEndpoints::
    OnTreeManagerWillBeRemoved(AXTreeID previous_tree_id) {
  if (start_->tree_id() == previous_tree_id ||
      end_->tree_id() == previous_tree_id) {
    RemoveObserver(previous_tree_id);
  }
}

}  // namespace ui
