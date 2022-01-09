// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_H_
#define CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/strings/string_split.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/web/web_ax_enums.h"
#include "ui/accessibility/ax_enums.mojom-forward.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_node_position.h"
#include "ui/accessibility/ax_range.h"
#include "ui/accessibility/ax_text_attributes.h"
#include "ui/accessibility/platform/ax_platform_node.h"
#include "ui/accessibility/platform/ax_platform_node_delegate.h"
#include "ui/base/buildflags.h"

#if defined(OS_MAC) && __OBJC__
@class BrowserAccessibilityCocoa;
#endif

namespace content {
class BrowserAccessibilityManager;

// A `BrowserAccessibility` object represents one node in the accessibility tree
// on the browser side. It wraps an `AXNode` and assists in exposing
// web-specific information from the node. It's owned by a
// `BrowserAccessibilityManager`.
//
// There are subclasses of BrowserAccessibility for each platform where we
// implement some of the native accessibility APIs that are only specific to the
// Web.
class CONTENT_EXPORT BrowserAccessibility : public ui::AXPlatformNodeDelegate {
 public:
  // Creates a platform specific BrowserAccessibility. Ownership passes to the
  // caller.
  static std::unique_ptr<BrowserAccessibility> Create(
      BrowserAccessibilityManager* manager,
      ui::AXNode* node);

  // Returns |delegate| as a BrowserAccessibility object, if |delegate| is
  // non-null and an object in the BrowserAccessibility class hierarchy.
  static BrowserAccessibility* FromAXPlatformNodeDelegate(
      ui::AXPlatformNodeDelegate* delegate);

  ~BrowserAccessibility() override;
  BrowserAccessibility(const BrowserAccessibility&) = delete;
  BrowserAccessibility& operator=(const BrowserAccessibility&) = delete;

  // Called after the object is first initialized and again every time
  // its data changes.
  virtual void OnDataChanged();

  // Called when the location changed.
  virtual void OnLocationChanged() {}

  // This is called when the platform-specific attributes for a node need
  // to be recomputed, which may involve firing native events, due to a
  // change other than an update from OnAccessibilityEvents.
  virtual void UpdatePlatformAttributes() {}

  // Return true if this object is equal to or a descendant of |ancestor|.
  bool IsDescendantOf(const BrowserAccessibility* ancestor) const;

  bool IsIgnoredForTextNavigation() const;

  bool IsLineBreakObject() const;

  // See `AXNode::IsEmptyLeaf()`.
  bool IsEmptyLeaf() const;

  // Returns true if this object can fire events.
  virtual bool CanFireEvents() const;

  // Return the AXPlatformNode corresponding to this node, if applicable
  // on this platform.
  virtual ui::AXPlatformNode* GetAXPlatformNode() const;

  // Returns the number of children of this object, or 0 if PlatformIsLeaf()
  // returns true.
  virtual uint32_t PlatformChildCount() const;

  // Return a pointer to the child at the given index, or NULL for an
  // invalid index. Returns nullptr if PlatformIsLeaf() returns true.
  virtual BrowserAccessibility* PlatformGetChild(uint32_t child_index) const;

  BrowserAccessibility* PlatformGetParent() const;

  // The following methods are virtual so that they can be overridden on Mac to
  // take into account the "extra Mac nodes".
  //
  // TODO(nektar): Refactor `AXNode` so that it can handle "extra Mac nodes"
  // itself when using any of its tree traversal methods.
  virtual BrowserAccessibility* PlatformGetFirstChild() const;
  virtual BrowserAccessibility* PlatformGetLastChild() const;
  virtual BrowserAccessibility* PlatformGetNextSibling() const;
  virtual BrowserAccessibility* PlatformGetPreviousSibling() const;

  // Iterator over platform children.
  class CONTENT_EXPORT PlatformChildIterator : public ChildIterator {
   public:
    PlatformChildIterator(const BrowserAccessibility* parent,
                          BrowserAccessibility* child);
    PlatformChildIterator(const PlatformChildIterator& it);
    ~PlatformChildIterator() override;
    bool operator==(const ChildIterator& rhs) const override;
    bool operator!=(const ChildIterator& rhs) const override;
    void operator++() override;
    void operator++(int) override;
    void operator--() override;
    void operator--(int) override;
    gfx::NativeViewAccessible GetNativeViewAccessible() const override;
    BrowserAccessibility* get() const;
    int GetIndexInParent() const override;
    BrowserAccessibility& operator*() const override;
    BrowserAccessibility* operator->() const override;

   private:
    raw_ptr<const BrowserAccessibility> parent_;
    ui::AXNode::ChildIteratorBase<
        BrowserAccessibility,
        &BrowserAccessibility::PlatformGetNextSibling,
        &BrowserAccessibility::PlatformGetPreviousSibling,
        &BrowserAccessibility::PlatformGetFirstChild,
        &BrowserAccessibility::PlatformGetLastChild>
        platform_iterator;
  };

  // C++ range implementation for platform children, see PlatformChildren().
  class PlatformChildrenRange {
   public:
    explicit PlatformChildrenRange(const BrowserAccessibility* parent)
        : parent_(parent) {}
    PlatformChildrenRange(const PlatformChildrenRange&) = default;

    PlatformChildIterator begin() { return parent_->PlatformChildrenBegin(); }
    PlatformChildIterator end() { return parent_->PlatformChildrenEnd(); }

   private:
    const BrowserAccessibility* const parent_;
  };

  // Returns a range for platform children which can be used in range-based for
  // loops, for example, for (const auto& child : PlatformChildren()) {}.
  PlatformChildrenRange PlatformChildren() const {
    return PlatformChildrenRange(this);
  }

  PlatformChildIterator PlatformChildrenBegin() const;
  PlatformChildIterator PlatformChildrenEnd() const;

  // If this object is exposed to the platform's accessibility layer, returns
  // this object. Otherwise, returns the lowest ancestor that is exposed to the
  // platform.
  virtual BrowserAccessibility* PlatformGetLowestPlatformAncestor() const;

  // If this node is within an editable region, such as a content editable,
  // returns the node that is at the root of that editable region, otherwise
  // returns nullptr. In accessibility, an editable region includes all types of
  // text fields, (see `AXNodeData::IsTextField()`).
  BrowserAccessibility* PlatformGetTextFieldAncestor() const;

  // If this node is within a container (or widget) that supports either single
  // or multiple selection, returns the node that represents the container.
  BrowserAccessibility* PlatformGetSelectionContainer() const;

  bool IsPreviousSiblingOnSameLine() const;
  bool IsNextSiblingOnSameLine() const;

  // Returns nullptr if there are no children.
  BrowserAccessibility* PlatformDeepestFirstChild() const;
  // Returns nullptr if there are no children.
  BrowserAccessibility* PlatformDeepestLastChild() const;

  // Returns nullptr if there are no children.
  BrowserAccessibility* InternalDeepestFirstChild() const;
  // Returns nullptr if there are no children.
  BrowserAccessibility* InternalDeepestLastChild() const;

  // Range implementation for all children traversal see AllChildren().
  class AllChildrenRange final {
   public:
    explicit AllChildrenRange(const BrowserAccessibility* parent)
        : parent_(parent),
          child_tree_root_(parent->PlatformGetRootOfChildTree()) {}
    AllChildrenRange(const AllChildrenRange&) = default;

    class Iterator final
        : public std::iterator<std::input_iterator_tag, BrowserAccessibility*> {
     public:
      Iterator(const BrowserAccessibility* parent,
               const BrowserAccessibility* child_tree_root,
               unsigned int index = 0U)
          : parent_(parent), child_tree_root_(child_tree_root), index_(index) {}
      Iterator(const Iterator&) = default;
      ~Iterator() = default;

      Iterator& operator++() {
        ++index_;
        return *this;
      }
      Iterator operator++(int) {
        Iterator tmp(*this);
        operator++();
        return tmp;
      }
      bool operator==(const Iterator& rhs) const {
        return parent_ == rhs.parent_ && index_ == rhs.index_;
      }
      bool operator!=(const Iterator& rhs) const { return !operator==(rhs); }
      const BrowserAccessibility* operator*();

     private:
      const BrowserAccessibility* const parent_;
      const BrowserAccessibility* const child_tree_root_;
      unsigned int index_;
    };

    Iterator begin() { return {parent_, child_tree_root_}; }
    Iterator end() {
      unsigned int count =
          child_tree_root_ ? 1U : parent_->node()->children().size();
      return {parent_, child_tree_root_, count};
    }

   private:
    const BrowserAccessibility* const parent_;
    const BrowserAccessibility* const child_tree_root_;
  };

  // Returns a range for all children including ignored children, which can be
  // used in range-based for loops, for example,
  // for (const auto& child : AllChildren()) {}.
  AllChildrenRange AllChildren() const { return AllChildrenRange(this); }

  // Derivative utils for AXPlatformNodeDelegate::GetHypertextRangeBoundsRect
  gfx::Rect GetUnclippedRootFrameHypertextRangeBoundsRect(
      const int start_offset,
      const int end_offset,
      ui::AXOffscreenResult* offscreen_result = nullptr) const;

  // Derivative utils for AXPlatformNodeDelegate::GetInnerTextRangeBoundsRect
  gfx::Rect GetUnclippedScreenInnerTextRangeBoundsRect(
      const int start_offset,
      const int end_offset,
      ui::AXOffscreenResult* offscreen_result = nullptr) const;
  gfx::Rect GetUnclippedRootFrameInnerTextRangeBoundsRect(
      const int start_offset,
      const int end_offset,
      ui::AXOffscreenResult* offscreen_result = nullptr) const;

  // DEPRECATED: Prefer using the interfaces provided by AXPlatformNodeDelegate
  // when writing new code.
  gfx::Rect GetScreenHypertextRangeBoundsRect(
      int start,
      int len,
      const ui::AXClippingBehavior clipping_behavior,
      ui::AXOffscreenResult* offscreen_result = nullptr) const;

  // Returns the bounds of the given range in coordinates relative to the
  // top-left corner of the overall web area. Only valid when the role is
  // WebAXRoleStaticText.
  // DEPRECATED (for public use): Prefer using the interfaces provided by
  // AXPlatformNodeDelegate when writing new non-private code.
  gfx::Rect GetRootFrameHypertextRangeBoundsRect(
      int start,
      int len,
      const ui::AXClippingBehavior clipping_behavior,
      ui::AXOffscreenResult* offscreen_result = nullptr) const;

  // This is an approximate hit test that only uses the information in
  // the browser process to compute the correct result. It will not return
  // correct results in many cases of z-index, overflow, and absolute
  // positioning, so BrowserAccessibilityManager::CachingAsyncHitTest
  // should be used instead, which falls back on calling ApproximateHitTest
  // automatically.
  //
  // Note that unlike BrowserAccessibilityManager::CachingAsyncHitTest, this
  // method takes a parameter in Blink's definition of screen coordinates.
  // This is so that the scale factor is consistent with what we receive from
  // Blink and store in the AX tree.
  // Blink screen coordinates are 1:1 with physical pixels if use-zoom-for-dsf
  // is disabled; they're physical pixels divided by device scale factor if
  // use-zoom-for-dsf is disabled. For more information see:
  // http://www.chromium.org/developers/design-documents/blink-coordinate-spaces
  BrowserAccessibility* ApproximateHitTest(
      const gfx::Point& blink_screen_point);

  //
  // Accessors
  //

  BrowserAccessibilityManager* manager() const { return manager_; }
  ui::AXNode* node() const { return node_; }

  // These access the internal unignored accessibility tree, which doesn't
  // necessarily reflect the accessibility tree that should be exposed on each
  // platform. Use PlatformChildCount and PlatformGetChild to implement platform
  // accessibility APIs.
  uint32_t InternalChildCount() const;
  BrowserAccessibility* InternalGetChild(uint32_t child_index) const;
  BrowserAccessibility* InternalGetParent() const;
  BrowserAccessibility* InternalGetFirstChild() const;
  BrowserAccessibility* InternalGetLastChild() const;
  BrowserAccessibility* InternalGetNextSibling() const;
  BrowserAccessibility* InternalGetPreviousSibling() const;
  using InternalChildIterator = ui::AXNode::ChildIteratorBase<
      BrowserAccessibility,
      &BrowserAccessibility::InternalGetNextSibling,
      &BrowserAccessibility::InternalGetPreviousSibling,
      &BrowserAccessibility::InternalGetFirstChild,
      &BrowserAccessibility::InternalGetLastChild>;
  InternalChildIterator InternalChildrenBegin() const;
  InternalChildIterator InternalChildrenEnd() const;

  ui::AXNodeID GetId() const;
  gfx::RectF GetLocation() const;

  bool IsWebAreaForPresentationalIframe() const override;

  // See AXNodeData::IsClickable().
  virtual bool IsClickable() const;

  // See AXNodeData::IsTextField().
  bool IsTextField() const;

  // See AXNodeData::IsPasswordField().
  bool IsPasswordField() const;

  // See AXNodeData::IsAtomicTextField().
  bool IsAtomicTextField() const;

  // See AXNodeData::IsNonAtomicTextField().
  bool IsNonAtomicTextField() const;

  // Returns true if the accessible name was explicitly set to "" by the author
  bool HasExplicitlyEmptyName() const;

  // Get text to announce for a live region change, for ATs that do not
  // implement this functionality.
  //
  // TODO(nektar): Replace with `AXNode::GetTextContentUTF16()`.
  std::string GetLiveRegionText() const;

  // |offset| could only be a character offset. Depending on the platform, the
  // character offset could be either in the object's text content (Android and
  // Mac), or an offset in the object's hypertext (Linux ATK and Windows IA2).
  // Converts to a leaf text position if you pass a character offset on a
  // non-leaf node.
  AXPosition CreatePositionForSelectionAt(int offset) const;

  std::u16string GetNameAsString16() const;

  // `AXPlatformNodeDelegate` implementation.
  std::u16string GetAuthorUniqueId() const override;
  const ui::AXNodeData& GetData() const override;
  const ui::AXTreeData& GetTreeData() const override;
  ax::mojom::Role GetRole() const override;
  bool HasBoolAttribute(ax::mojom::BoolAttribute attribute) const override;
  bool GetBoolAttribute(ax::mojom::BoolAttribute attribute) const override;
  bool GetBoolAttribute(ax::mojom::BoolAttribute attribute,
                        bool* value) const override;
  bool HasFloatAttribute(ax::mojom::FloatAttribute attribute) const override;
  float GetFloatAttribute(ax::mojom::FloatAttribute attribute) const override;
  bool GetFloatAttribute(ax::mojom::FloatAttribute attribute,
                         float* value) const override;
  const std::vector<std::pair<ax::mojom::IntAttribute, int32_t>>&
  GetIntAttributes() const override;
  bool HasIntAttribute(ax::mojom::IntAttribute attribute) const override;
  int GetIntAttribute(ax::mojom::IntAttribute attribute) const override;
  bool GetIntAttribute(ax::mojom::IntAttribute attribute,
                       int* value) const override;
  const std::vector<std::pair<ax::mojom::StringAttribute, std::string>>&
  GetStringAttributes() const override;
  bool HasStringAttribute(ax::mojom::StringAttribute attribute) const override;
  const std::string& GetStringAttribute(
      ax::mojom::StringAttribute attribute) const override;
  bool GetStringAttribute(ax::mojom::StringAttribute attribute,
                          std::string* value) const override;
  std::u16string GetString16Attribute(
      ax::mojom::StringAttribute attribute) const override;
  bool GetString16Attribute(ax::mojom::StringAttribute attribute,
                            std::u16string* value) const override;
  const std::string& GetInheritedStringAttribute(
      ax::mojom::StringAttribute attribute) const override;
  std::u16string GetInheritedString16Attribute(
      ax::mojom::StringAttribute attribute) const override;
  const std::vector<
      std::pair<ax::mojom::IntListAttribute, std::vector<int32_t>>>&
  GetIntListAttributes() const override;
  bool HasIntListAttribute(
      ax::mojom::IntListAttribute attribute) const override;
  const std::vector<int32_t>& GetIntListAttribute(
      ax::mojom::IntListAttribute attribute) const override;
  bool GetIntListAttribute(ax::mojom::IntListAttribute attribute,
                           std::vector<int32_t>* value) const override;
  bool HasStringListAttribute(
      ax::mojom::StringListAttribute attribute) const override;
  const std::vector<std::string>& GetStringListAttribute(
      ax::mojom::StringListAttribute attribute) const override;
  bool GetStringListAttribute(ax::mojom::StringListAttribute attribute,
                              std::vector<std::string>* value) const override;
  typedef base::StringPairs HtmlAttributes;
  const HtmlAttributes& GetHtmlAttributes() const override;
  bool GetHtmlAttribute(const char* attribute,
                        std::string* value) const override;
  bool GetHtmlAttribute(const char* attribute,
                        std::u16string* value) const override;
  ui::AXTextAttributes GetTextAttributes() const override;
  bool HasState(ax::mojom::State state) const override;
  ax::mojom::State GetState() const override;
  bool HasAction(ax::mojom::Action action) const override;
  bool HasTextStyle(ax::mojom::TextStyle text_style) const override;
  ax::mojom::NameFrom GetNameFrom() const override;
  const ui::AXTree::Selection GetUnignoredSelection() const override;
  AXPosition CreatePositionAt(
      int offset,
      ax::mojom::TextAffinity affinity =
          ax::mojom::TextAffinity::kDownstream) const override;
  AXPosition CreateTextPositionAt(
      int offset,
      ax::mojom::TextAffinity affinity =
          ax::mojom::TextAffinity::kDownstream) const override;
  gfx::NativeViewAccessible GetNSWindow() override;
  gfx::NativeViewAccessible GetNativeViewAccessible() override;
  gfx::NativeViewAccessible GetParent() const override;
  int GetChildCount() const override;
  gfx::NativeViewAccessible ChildAtIndex(int index) override;
  bool HasModalDialog() const override;
  gfx::NativeViewAccessible GetFirstChild() override;
  gfx::NativeViewAccessible GetLastChild() override;
  gfx::NativeViewAccessible GetNextSibling() override;
  gfx::NativeViewAccessible GetPreviousSibling() override;
  bool IsChildOfLeaf() const override;
  bool IsDescendantOfAtomicTextField() const override;
  bool IsLeaf() const override;
  bool IsFocused() const override;
  bool IsIgnored() const override;
  bool IsInvisibleOrIgnored() const override;
  bool IsToplevelBrowserWindow() override;
  gfx::NativeViewAccessible GetLowestPlatformAncestor() const override;
  gfx::NativeViewAccessible GetTextFieldAncestor() const override;
  gfx::NativeViewAccessible GetSelectionContainer() const override;
  gfx::NativeViewAccessible GetTableAncestor() const override;

  std::unique_ptr<ChildIterator> ChildrenBegin() override;
  std::unique_ptr<ChildIterator> ChildrenEnd() override;

  const std::string& GetName() const override;
  std::u16string GetHypertext() const override;
  const std::map<int, int>& GetHypertextOffsetToHyperlinkChildIndex()
      const override;
  bool SetHypertextSelection(int start_offset, int end_offset) override;
  std::u16string GetTextContentUTF16() const override;
  std::u16string GetValueForControl() const override;
  gfx::Rect GetBoundsRect(
      const ui::AXCoordinateSystem coordinate_system,
      const ui::AXClippingBehavior clipping_behavior,
      ui::AXOffscreenResult* offscreen_result = nullptr) const override;
  gfx::Rect GetHypertextRangeBoundsRect(
      const int start_offset,
      const int end_offset,
      const ui::AXCoordinateSystem coordinate_system,
      const ui::AXClippingBehavior clipping_behavior,
      ui::AXOffscreenResult* offscreen_result = nullptr) const override;
  gfx::Rect GetInnerTextRangeBoundsRect(
      const int start_offset,
      const int end_offset,
      const ui::AXCoordinateSystem coordinate_system,
      const ui::AXClippingBehavior clipping_behavior,
      ui::AXOffscreenResult* offscreen_result = nullptr) const override;
  gfx::NativeViewAccessible HitTestSync(int physical_pixel_x,
                                        int physical_pixel_y) const override;
  gfx::NativeViewAccessible GetFocus() const override;
  ui::AXPlatformNode* GetFromNodeID(int32_t id) override;
  ui::AXPlatformNode* GetFromTreeIDAndNodeID(const ui::AXTreeID& ax_tree_id,
                                             int32_t id) override;
  int GetIndexInParent() override;
  gfx::AcceleratedWidget GetTargetForNativeAccessibilityEvent() override;

  const std::vector<gfx::NativeViewAccessible> GetUIADirectChildrenInRange(
      ui::AXPlatformNodeDelegate* start,
      ui::AXPlatformNodeDelegate* end) override;

  std::string GetLanguage() const override;

  bool IsTable() const override;
  absl::optional<int> GetTableColCount() const override;
  absl::optional<int> GetTableRowCount() const override;
  absl::optional<int> GetTableAriaColCount() const override;
  absl::optional<int> GetTableAriaRowCount() const override;
  absl::optional<int> GetTableCellCount() const override;
  absl::optional<bool> GetTableHasColumnOrRowHeaderNode() const override;
  std::vector<ui::AXNodeID> GetColHeaderNodeIds() const override;
  std::vector<ui::AXNodeID> GetColHeaderNodeIds(int col_index) const override;
  std::vector<ui::AXNodeID> GetRowHeaderNodeIds() const override;
  std::vector<ui::AXNodeID> GetRowHeaderNodeIds(int row_index) const override;
  ui::AXPlatformNode* GetTableCaption() const override;

  bool IsTableRow() const override;
  absl::optional<int> GetTableRowRowIndex() const override;

  bool IsTableCellOrHeader() const override;
  absl::optional<int> GetTableCellIndex() const override;
  absl::optional<int> GetTableCellColIndex() const override;
  absl::optional<int> GetTableCellRowIndex() const override;
  absl::optional<int> GetTableCellColSpan() const override;
  absl::optional<int> GetTableCellRowSpan() const override;
  absl::optional<int> GetTableCellAriaColIndex() const override;
  absl::optional<int> GetTableCellAriaRowIndex() const override;
  absl::optional<int32_t> GetCellId(int row_index,
                                    int col_index) const override;
  absl::optional<int32_t> CellIndexToId(int cell_index) const override;
  bool IsCellOrHeaderOfAriaGrid() const override;

  bool AccessibilityPerformAction(const ui::AXActionData& data) override;
  std::u16string GetLocalizedStringForImageAnnotationStatus(
      ax::mojom::ImageAnnotationStatus status) const override;
  std::u16string GetLocalizedRoleDescriptionForUnlabeledImage() const override;
  std::u16string GetLocalizedStringForLandmarkType() const override;
  std::u16string GetLocalizedStringForRoleDescription() const override;
  std::u16string GetStyleNameAttributeAsLocalizedString() const override;
  ui::TextAttributeMap ComputeTextAttributeMap(
      const ui::TextAttributeList& default_attributes) const override;
  std::string GetInheritedFontFamilyName() const override;
  bool ShouldIgnoreHoveredStateForTesting() override;
  bool IsOffscreen() const override;
  bool IsMinimized() const override;
  bool IsText() const override;
  bool IsWebContent() const override;
  bool HasVisibleCaretOrSelection() const override;
  ui::AXPlatformNode* GetTargetNodeForRelation(
      ax::mojom::IntAttribute attr) override;
  std::vector<ui::AXPlatformNode*> GetTargetNodesForRelation(
      ax::mojom::IntListAttribute attr) override;
  std::set<ui::AXPlatformNode*> GetReverseRelations(
      ax::mojom::IntAttribute attr) override;
  std::set<ui::AXPlatformNode*> GetReverseRelations(
      ax::mojom::IntListAttribute attr) override;
  bool IsOrderedSetItem() const override;
  bool IsOrderedSet() const override;
  absl::optional<int> GetPosInSet() const override;
  absl::optional<int> GetSetSize() const override;
  SkColor GetColor() const override;
  SkColor GetBackgroundColor() const override;

  // Returns true if this node is a list marker or if it's a descendant
  // of a list marker node. Returns false otherwise.
  bool IsInListMarker() const;

  // Returns true if this node is a collapsed popup button that is parent to a
  // menu list popup.
  bool IsCollapsedMenuListPopUpButton() const;

  // Returns the popup button ancestor of this current node if any. The popup
  // button needs to be the parent of a menu list popup and needs to be
  // collapsed.
  BrowserAccessibility* GetCollapsedMenuListPopUpButtonAncestor() const;

  // Returns true if:
  // 1. This node is a list, AND
  // 2. This node has a list ancestor or a list descendant.
  bool IsHierarchicalList() const;

  // Returns a string representation of this object for debugging purposes.
  std::string ToString() const;

 protected:
  BrowserAccessibility(BrowserAccessibilityManager* manager, ui::AXNode* node);

  virtual ui::TextAttributeList ComputeTextAttributes() const;

  // The manager of this tree of accessibility objects. Weak, owns us.
  const raw_ptr<BrowserAccessibilityManager> manager_;

  // The underlying node. Weak, `AXTree` owns this.
  const raw_ptr<ui::AXNode> node_;

  // Protected so that it can't be called directly on a BrowserAccessibility
  // where it could be confused with an id that comes from the node data,
  // which is only unique to the Blink process.
  // Does need to be called by subclasses such as BrowserAccessibilityAndroid.
  const ui::AXUniqueId& GetUniqueId() const override;

  // Returns a text attribute map indicating the offsets in the text of a leaf
  // object, such as a text field or static text, where spelling and grammar
  // errors are present.
  ui::TextAttributeMap GetSpellingAndGrammarAttributes() const;

  std::string SubtreeToStringHelper(size_t level) override;

  // The UIA tree formatter needs access to GetUniqueId() to identify the
  // starting point for tree dumps.
  friend class AccessibilityTreeFormatterUia;

 private:
  // Return the bounds after converting from this node's coordinate system
  // (which is relative to its nearest scrollable ancestor) to the coordinate
  // system specified. If the clipping behavior is set to clipped, clipping is
  // applied to all bounding boxes so that the resulting rect is within the
  // window. If the clipping behavior is unclipped, the resulting rect may be
  // outside of the window or offscreen. If an offscreen result address is
  // provided, it will be populated depending on whether the returned bounding
  // box is onscreen or offscreen.
  gfx::Rect RelativeToAbsoluteBounds(
      gfx::RectF bounds,
      const ui::AXCoordinateSystem coordinate_system,
      const ui::AXClippingBehavior clipping_behavior,
      ui::AXOffscreenResult* offscreen_result) const;

  // Return a rect for a 1-width character past the end of text. This is what
  // ATs expect when getting the character extents past the last character in
  // a line, and equals what the caret bounds would be when past the end of
  // the text.
  gfx::Rect GetRootFrameHypertextBoundsPastEndOfText(
      const ui::AXClippingBehavior clipping_behavior,
      ui::AXOffscreenResult* offscreen_result = nullptr) const;

  // Return the bounds of inline text in this node's coordinate system (which
  // is relative to its container node specified in AXRelativeBounds).
  gfx::RectF GetInlineTextRect(const int start_offset,
                               const int end_offset,
                               const int max_length) const;

  // Recursive helper function for GetInnerTextRangeBounds.
  gfx::Rect GetInnerTextRangeBoundsRectInSubtree(
      const int start_offset,
      const int end_offset,
      const ui::AXCoordinateSystem coordinate_system,
      const ui::AXClippingBehavior clipping_behavior,
      ui::AXOffscreenResult* offscreen_result) const;

  // Given a set of node ids, return the nodes in this delegate's tree to
  // which they correspond.
  std::set<ui::AXPlatformNode*> GetNodesForNodeIdSet(
      const std::set<int32_t>& ids);

  // If the node has a child tree, get the root node.
  BrowserAccessibility* PlatformGetRootOfChildTree() const;

  // Determines whether this object is valid.
  bool IsValid() const;

  // Given a set of map of spelling text attributes and a start offset, merge
  // them into the given map of existing text attributes. Merges the given
  // spelling attributes, i.e. document marker information, into the given
  // text attributes starting at the given character offset. This is required
  // because document markers that are present on text leaves need to be
  // propagated to their parent object for compatibility with Firefox.
  static void MergeSpellingAndGrammarIntoTextAttributes(
      const ui::TextAttributeMap& spelling_attributes,
      int start_offset,
      ui::TextAttributeMap* text_attributes);

  // Return true is the list of text attributes already includes an invalid
  // attribute originating from ARIA.
  static bool HasInvalidAttribute(const ui::TextAttributeList& attributes);

  // A unique ID, since node IDs are frame-local.
  ui::AXUniqueId unique_id_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_H_
