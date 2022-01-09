// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "content/browser/accessibility/browser_accessibility.h"
#include "content/browser/accessibility/browser_accessibility_manager.h"
#if defined(OS_WIN)
#include "content/browser/accessibility/browser_accessibility_win.h"
#endif
#include "content/browser/accessibility/test_browser_accessibility_delegate.h"
#include "content/public/browser/ax_event_notification_details.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_tree.h"

namespace content {

namespace {

class CountingAXTreeObserver : public ui::AXTreeObserver {
 public:
  CountingAXTreeObserver() {}
  ~CountingAXTreeObserver() override {}

  int update_count() { return update_count_; }
  int node_count() { return node_count_; }

 private:
  void OnAtomicUpdateFinished(ui::AXTree* tree,
                              bool root_changed,
                              const std::vector<Change>& changes) override {
    update_count_++;
    node_count_ += static_cast<int>(changes.size());
  }

  int update_count_ = 0;
  int node_count_ = 0;
};

}  // anonymous namespace

class BrowserAccessibilityManagerTest : public testing::Test {
 public:
  BrowserAccessibilityManagerTest() = default;

  BrowserAccessibilityManagerTest(const BrowserAccessibilityManagerTest&) =
      delete;
  BrowserAccessibilityManagerTest& operator=(
      const BrowserAccessibilityManagerTest&) = delete;

  ~BrowserAccessibilityManagerTest() override = default;

 protected:
  std::unique_ptr<TestBrowserAccessibilityDelegate>
      test_browser_accessibility_delegate_;
  const content::BrowserTaskEnvironment task_environment_;

 private:
  void SetUp() override;
};

void BrowserAccessibilityManagerTest::SetUp() {
  test_browser_accessibility_delegate_ =
      std::make_unique<TestBrowserAccessibilityDelegate>();
}

// Temporarily disabled due to bug http://crbug.com/765490
TEST_F(BrowserAccessibilityManagerTest, DISABLED_TestFatalError) {
  // Test that BrowserAccessibilityManager raises a fatal error
  // (which will crash the renderer) if the same id is used in
  // two places in the tree.

  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;
  root.child_ids.push_back(2);
  root.child_ids.push_back(2);

  std::unique_ptr<BrowserAccessibilityManager> manager;
  ASSERT_FALSE(test_browser_accessibility_delegate_->got_fatal_error());
  manager.reset(BrowserAccessibilityManager::Create(
      MakeAXTreeUpdate(root), test_browser_accessibility_delegate_.get()));
  ASSERT_TRUE(test_browser_accessibility_delegate_->got_fatal_error());

  ui::AXNodeData root2;
  root2.id = 1;
  root2.role = ax::mojom::Role::kRootWebArea;
  root2.child_ids.push_back(2);
  root2.child_ids.push_back(3);

  ui::AXNodeData child1;
  child1.id = 2;
  child1.child_ids.push_back(4);
  child1.child_ids.push_back(5);

  ui::AXNodeData child2;
  child2.id = 3;
  child2.child_ids.push_back(6);
  child2.child_ids.push_back(5);  // Duplicate

  ui::AXNodeData grandchild4;
  grandchild4.id = 4;

  ui::AXNodeData grandchild5;
  grandchild5.id = 5;

  ui::AXNodeData grandchild6;
  grandchild6.id = 6;

  test_browser_accessibility_delegate_->reset_got_fatal_error();
  manager.reset(BrowserAccessibilityManager::Create(
      MakeAXTreeUpdate(root2, child1, child2, grandchild4, grandchild5,
                       grandchild6),
      test_browser_accessibility_delegate_.get()));
  ASSERT_TRUE(test_browser_accessibility_delegate_->got_fatal_error());
}

// This test depends on hypertext, which is only used on
// Linux and Windows.
#if defined(OS_WIN) || BUILDFLAG(USE_ATK)
TEST_F(BrowserAccessibilityManagerTest, BoundsForRange) {
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;
  root.relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);

  ui::AXNodeData static_text;
  static_text.id = 2;
  static_text.role = ax::mojom::Role::kStaticText;
  static_text.SetName("Hello, world.");
  static_text.relative_bounds.bounds = gfx::RectF(100, 100, 29, 18);
  root.child_ids.push_back(2);

  ui::AXNodeData inline_text1;
  inline_text1.id = 3;
  inline_text1.role = ax::mojom::Role::kInlineTextBox;
  inline_text1.SetName("Hello, ");
  inline_text1.relative_bounds.bounds = gfx::RectF(100, 100, 29, 9);
  inline_text1.SetTextDirection(ax::mojom::WritingDirection::kLtr);
  std::vector<int32_t> character_offsets1;
  character_offsets1.push_back(6);   // 0
  character_offsets1.push_back(11);  // 1
  character_offsets1.push_back(16);  // 2
  character_offsets1.push_back(21);  // 3
  character_offsets1.push_back(26);  // 4
  character_offsets1.push_back(29);  // 5
  character_offsets1.push_back(29);  // 6 (note that the space has no width)
  inline_text1.AddIntListAttribute(
      ax::mojom::IntListAttribute::kCharacterOffsets, character_offsets1);
  static_text.child_ids.push_back(3);

  ui::AXNodeData inline_text2;
  inline_text2.id = 4;
  inline_text2.role = ax::mojom::Role::kInlineTextBox;
  inline_text2.SetName("world.");
  inline_text2.relative_bounds.bounds = gfx::RectF(100, 109, 28, 9);
  inline_text2.SetTextDirection(ax::mojom::WritingDirection::kLtr);
  std::vector<int32_t> character_offsets2;
  character_offsets2.push_back(5);
  character_offsets2.push_back(10);
  character_offsets2.push_back(15);
  character_offsets2.push_back(20);
  character_offsets2.push_back(25);
  character_offsets2.push_back(28);
  inline_text2.AddIntListAttribute(
      ax::mojom::IntListAttribute::kCharacterOffsets, character_offsets2);
  static_text.child_ids.push_back(4);

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, static_text, inline_text1, inline_text2),
          test_browser_accessibility_delegate_.get()));

  BrowserAccessibility* root_accessible = manager->GetRoot();
  ASSERT_NE(nullptr, root_accessible);
  BrowserAccessibility* static_text_accessible =
      root_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, static_text_accessible);

  EXPECT_EQ(gfx::Rect(100, 100, 6, 9).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    0, 1, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(100, 100, 26, 9).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    0, 5, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(100, 109, 5, 9).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    7, 1, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(100, 109, 25, 9).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    7, 5, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(100, 100, 29, 18).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    5, 3, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(100, 100, 29, 18).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    0, 13, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  // Note that each child in the parent element is represented by a single
  // embedded object character and not by its text.
  // TODO(nektar): Investigate failure on Linux.
  EXPECT_EQ(gfx::Rect(100, 100, 29, 18).ToString(),
            root_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    0, 13, ui::AXClippingBehavior::kUnclipped)
                .ToString());
}
#endif  // defined(OS_WIN) || BUILDFLAG(USE_ATK)

TEST_F(BrowserAccessibilityManagerTest, BoundsForRangeMultiElement) {
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;
  root.relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);

  ui::AXNodeData static_text;
  static_text.id = 2;
  static_text.role = ax::mojom::Role::kStaticText;
  static_text.SetName("ABC");
  static_text.relative_bounds.bounds = gfx::RectF(0, 20, 33, 9);
  root.child_ids.push_back(2);

  ui::AXNodeData inline_text1;
  inline_text1.id = 3;
  inline_text1.role = ax::mojom::Role::kInlineTextBox;
  inline_text1.SetName("ABC");
  inline_text1.relative_bounds.bounds = gfx::RectF(0, 20, 33, 9);
  inline_text1.SetTextDirection(ax::mojom::WritingDirection::kLtr);
  std::vector<int32_t> character_offsets{10, 21, 33};
  inline_text1.AddIntListAttribute(
      ax::mojom::IntListAttribute::kCharacterOffsets, character_offsets);
  static_text.child_ids.push_back(3);

  ui::AXNodeData static_text2;
  static_text2.id = 4;
  static_text2.role = ax::mojom::Role::kStaticText;
  static_text2.SetName("ABC");
  static_text2.relative_bounds.bounds = gfx::RectF(10, 40, 33, 9);
  root.child_ids.push_back(4);

  ui::AXNodeData inline_text2;
  inline_text2.id = 5;
  inline_text2.role = ax::mojom::Role::kInlineTextBox;
  inline_text2.SetName("ABC");
  inline_text2.relative_bounds.bounds = gfx::RectF(10, 40, 33, 9);
  inline_text2.SetTextDirection(ax::mojom::WritingDirection::kLtr);
  inline_text2.AddIntListAttribute(
      ax::mojom::IntListAttribute::kCharacterOffsets, character_offsets);
  static_text2.child_ids.push_back(5);

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, static_text, inline_text1, static_text2,
                           inline_text2),
          test_browser_accessibility_delegate_.get()));

  BrowserAccessibility* root_accessible = manager->GetRoot();
  ASSERT_NE(nullptr, root_accessible);
  BrowserAccessibility* static_text_accessible =
      root_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, static_text_accessible);
  BrowserAccessibility* static_text_accessible2 =
      root_accessible->PlatformGetChild(1);
  ASSERT_NE(nullptr, static_text_accessible);

  // The first line.
  EXPECT_EQ(gfx::Rect(0, 20, 33, 9).ToString(),
            manager
                ->GetRootFrameInnerTextRangeBoundsRect(
                    *static_text_accessible, 0, *static_text_accessible, 3)
                .ToString());

  // Part of the first line.
  EXPECT_EQ(gfx::Rect(0, 20, 21, 9).ToString(),
            manager
                ->GetRootFrameInnerTextRangeBoundsRect(
                    *static_text_accessible, 0, *static_text_accessible, 2)
                .ToString());

  // Part of the first line.
  EXPECT_EQ(gfx::Rect(10, 20, 23, 9).ToString(),
            manager
                ->GetRootFrameInnerTextRangeBoundsRect(
                    *static_text_accessible, 1, *static_text_accessible, 3)
                .ToString());

  // The second line.
  EXPECT_EQ(gfx::Rect(10, 40, 33, 9).ToString(),
            manager
                ->GetRootFrameInnerTextRangeBoundsRect(
                    *static_text_accessible2, 0, *static_text_accessible2, 3)
                .ToString());

  // All of both lines.
  EXPECT_EQ(gfx::Rect(0, 20, 43, 29).ToString(),
            manager
                ->GetRootFrameInnerTextRangeBoundsRect(
                    *static_text_accessible, 0, *static_text_accessible2, 3)
                .ToString());

  // Part of both lines.
  EXPECT_EQ(gfx::Rect(10, 20, 23, 29).ToString(),
            manager
                ->GetRootFrameInnerTextRangeBoundsRect(
                    *static_text_accessible, 2, *static_text_accessible2, 1)
                .ToString());

  // Part of both lines in reverse order.
  EXPECT_EQ(gfx::Rect(10, 20, 23, 29).ToString(),
            manager
                ->GetRootFrameInnerTextRangeBoundsRect(
                    *static_text_accessible2, 1, *static_text_accessible, 2)
                .ToString());
}

// This test depends on hypertext, which is only used on
// Linux and Windows.
#if defined(OS_WIN) || BUILDFLAG(USE_ATK)
TEST_F(BrowserAccessibilityManagerTest, BoundsForRangeBiDi) {
  // In this example, we assume that the string "123abc" is rendered with
  // "123" going left-to-right and "abc" going right-to-left. In other
  // words, on-screen it would look like "123cba". This is possible to
  // achieve if the source string had unicode control characters
  // to switch directions. This test doesn't worry about how, though - it just
  // tests that if something like that were to occur,
  // GetRootFrameRangeBoundsRect returns the correct bounds for different
  // ranges.

  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;
  root.relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);

  ui::AXNodeData static_text;
  static_text.id = 2;
  static_text.role = ax::mojom::Role::kStaticText;
  static_text.SetName("123abc");
  static_text.relative_bounds.bounds = gfx::RectF(100, 100, 60, 20);
  root.child_ids.push_back(2);

  ui::AXNodeData inline_text1;
  inline_text1.id = 3;
  inline_text1.role = ax::mojom::Role::kInlineTextBox;
  inline_text1.SetName("123");
  inline_text1.relative_bounds.bounds = gfx::RectF(100, 100, 30, 20);
  inline_text1.SetTextDirection(ax::mojom::WritingDirection::kLtr);
  std::vector<int32_t> character_offsets1;
  character_offsets1.push_back(10);  // 0
  character_offsets1.push_back(20);  // 1
  character_offsets1.push_back(30);  // 2
  inline_text1.AddIntListAttribute(
      ax::mojom::IntListAttribute::kCharacterOffsets, character_offsets1);
  static_text.child_ids.push_back(3);

  ui::AXNodeData inline_text2;
  inline_text2.id = 4;
  inline_text2.role = ax::mojom::Role::kInlineTextBox;
  inline_text2.SetName("abc");
  inline_text2.relative_bounds.bounds = gfx::RectF(130, 100, 30, 20);
  inline_text2.SetTextDirection(ax::mojom::WritingDirection::kRtl);
  std::vector<int32_t> character_offsets2;
  character_offsets2.push_back(10);
  character_offsets2.push_back(20);
  character_offsets2.push_back(30);
  inline_text2.AddIntListAttribute(
      ax::mojom::IntListAttribute::kCharacterOffsets, character_offsets2);
  static_text.child_ids.push_back(4);

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, static_text, inline_text1, inline_text2),
          test_browser_accessibility_delegate_.get()));

  BrowserAccessibility* root_accessible = manager->GetRoot();
  ASSERT_NE(nullptr, root_accessible);
  BrowserAccessibility* static_text_accessible =
      root_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, static_text_accessible);

  EXPECT_EQ(gfx::Rect(100, 100, 60, 20).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    0, 6, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(100, 100, 10, 20).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    0, 1, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(100, 100, 30, 20).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    0, 3, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(150, 100, 10, 20).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    3, 1, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(130, 100, 30, 20).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    3, 3, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  // This range is only two characters, but because of the direction switch
  // the bounds are as wide as four characters.
  EXPECT_EQ(gfx::Rect(120, 100, 40, 20).ToString(),
            static_text_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    2, 2, ui::AXClippingBehavior::kUnclipped)
                .ToString());
}
#endif  // defined(OS_WIN) || BUILDFLAG(USE_ATK)

// This test depends on hypertext, which is only used on
// Linux and Windows.
#if defined(OS_WIN) || BUILDFLAG(USE_ATK)
TEST_F(BrowserAccessibilityManagerTest, BoundsForRangeScrolledWindow) {
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;
  root.AddIntAttribute(ax::mojom::IntAttribute::kScrollX, 25);
  root.AddIntAttribute(ax::mojom::IntAttribute::kScrollY, 50);
  root.relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);

  ui::AXNodeData static_text;
  static_text.id = 2;
  static_text.role = ax::mojom::Role::kStaticText;
  static_text.SetName("ABC");
  static_text.relative_bounds.bounds = gfx::RectF(100, 100, 16, 9);
  root.child_ids.push_back(2);

  ui::AXNodeData inline_text;
  inline_text.id = 3;
  inline_text.role = ax::mojom::Role::kInlineTextBox;
  inline_text.SetName("ABC");
  inline_text.relative_bounds.bounds = gfx::RectF(100, 100, 16, 9);
  inline_text.SetTextDirection(ax::mojom::WritingDirection::kLtr);
  std::vector<int32_t> character_offsets1;
  character_offsets1.push_back(6);   // 0
  character_offsets1.push_back(11);  // 1
  character_offsets1.push_back(16);  // 2
  inline_text.AddIntListAttribute(
      ax::mojom::IntListAttribute::kCharacterOffsets, character_offsets1);
  static_text.child_ids.push_back(3);

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, static_text, inline_text),
          test_browser_accessibility_delegate_.get()));

  BrowserAccessibility* root_accessible = manager->GetRoot();
  ASSERT_NE(nullptr, root_accessible);
  BrowserAccessibility* static_text_accessible =
      root_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, static_text_accessible);

  if (manager->UseRootScrollOffsetsWhenComputingBounds()) {
    EXPECT_EQ(gfx::Rect(75, 50, 16, 9).ToString(),
              static_text_accessible
                  ->GetRootFrameHypertextRangeBoundsRect(
                      0, 3, ui::AXClippingBehavior::kUnclipped)
                  .ToString());
  } else {
    EXPECT_EQ(gfx::Rect(100, 100, 16, 9).ToString(),
              static_text_accessible
                  ->GetRootFrameHypertextRangeBoundsRect(
                      0, 3, ui::AXClippingBehavior::kUnclipped)
                  .ToString());
  }
}
#endif  // defined(OS_WIN) || BUILDFLAG(USE_ATK)

// This test depends on hypertext, which is only used on
// Linux and Windows.
#if defined(OS_WIN) || BUILDFLAG(USE_ATK)
TEST_F(BrowserAccessibilityManagerTest, BoundsForRangeOnParentElement) {
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;
  root.child_ids.push_back(2);
  root.relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);

  ui::AXNodeData div;
  div.id = 2;
  div.role = ax::mojom::Role::kGenericContainer;
  div.relative_bounds.bounds = gfx::RectF(100, 100, 100, 20);
  div.child_ids.push_back(3);
  div.child_ids.push_back(4);
  div.child_ids.push_back(5);

  ui::AXNodeData static_text1;
  static_text1.id = 3;
  static_text1.role = ax::mojom::Role::kStaticText;
  static_text1.SetName("AB");
  static_text1.relative_bounds.bounds = gfx::RectF(100, 100, 40, 20);
  static_text1.child_ids.push_back(6);

  ui::AXNodeData img;
  img.id = 4;
  img.role = ax::mojom::Role::kImage;
  img.SetName("Test image");
  img.relative_bounds.bounds = gfx::RectF(140, 100, 20, 20);

  ui::AXNodeData static_text2;
  static_text2.id = 5;
  static_text2.role = ax::mojom::Role::kStaticText;
  static_text2.SetName("CD");
  static_text2.relative_bounds.bounds = gfx::RectF(160, 100, 40, 20);
  static_text2.child_ids.push_back(7);

  ui::AXNodeData inline_text1;
  inline_text1.id = 6;
  inline_text1.role = ax::mojom::Role::kInlineTextBox;
  inline_text1.SetName("AB");
  inline_text1.relative_bounds.bounds = gfx::RectF(100, 100, 40, 20);
  inline_text1.SetTextDirection(ax::mojom::WritingDirection::kLtr);
  std::vector<int32_t> character_offsets1;
  character_offsets1.push_back(20);  // 0
  character_offsets1.push_back(40);  // 1
  inline_text1.AddIntListAttribute(
      ax::mojom::IntListAttribute::kCharacterOffsets, character_offsets1);

  ui::AXNodeData inline_text2;
  inline_text2.id = 7;
  inline_text2.role = ax::mojom::Role::kInlineTextBox;
  inline_text2.SetName("CD");
  inline_text2.relative_bounds.bounds = gfx::RectF(160, 100, 40, 20);
  inline_text2.SetTextDirection(ax::mojom::WritingDirection::kLtr);
  std::vector<int32_t> character_offsets2;
  character_offsets2.push_back(20);  // 0
  character_offsets2.push_back(40);  // 1
  inline_text2.AddIntListAttribute(
      ax::mojom::IntListAttribute::kCharacterOffsets, character_offsets2);

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, div, static_text1, img, static_text2,
                           inline_text1, inline_text2),
          test_browser_accessibility_delegate_.get()));
  BrowserAccessibility* root_accessible = manager->GetRoot();
  ASSERT_NE(nullptr, root_accessible);
  BrowserAccessibility* div_accessible = root_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, div_accessible);

  EXPECT_EQ(gfx::Rect(100, 100, 20, 20).ToString(),
            div_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    0, 1, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(100, 100, 40, 20).ToString(),
            div_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    0, 2, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(100, 100, 80, 20).ToString(),
            div_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    0, 4, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(120, 100, 60, 20).ToString(),
            div_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    1, 3, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(120, 100, 80, 20).ToString(),
            div_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    1, 4, ui::AXClippingBehavior::kUnclipped)
                .ToString());

  EXPECT_EQ(gfx::Rect(100, 100, 100, 20).ToString(),
            div_accessible
                ->GetRootFrameHypertextRangeBoundsRect(
                    0, 5, ui::AXClippingBehavior::kUnclipped)
                .ToString());
}
#endif  // defined(OS_WIN) || BUILDFLAG(USE_ATK)

TEST_F(BrowserAccessibilityManagerTest, TestNextPreviousInTreeOrder) {
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;

  ui::AXNodeData node2;
  node2.id = 2;
  root.child_ids.push_back(2);

  ui::AXNodeData node3;
  node3.id = 3;
  root.child_ids.push_back(3);

  ui::AXNodeData node4;
  node4.id = 4;
  node3.child_ids.push_back(4);

  ui::AXNodeData node5;
  node5.id = 5;
  root.child_ids.push_back(5);

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, node2, node3, node4, node5),
          test_browser_accessibility_delegate_.get()));

  BrowserAccessibility* root_accessible = manager->GetRoot();
  ASSERT_NE(nullptr, root_accessible);
  ASSERT_EQ(3U, root_accessible->PlatformChildCount());
  BrowserAccessibility* node2_accessible = root_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, node2_accessible);
  BrowserAccessibility* node3_accessible = root_accessible->PlatformGetChild(1);
  ASSERT_NE(nullptr, node3_accessible);
  ASSERT_EQ(1U, node3_accessible->PlatformChildCount());
  BrowserAccessibility* node4_accessible =
      node3_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, node4_accessible);
  BrowserAccessibility* node5_accessible = root_accessible->PlatformGetChild(2);
  ASSERT_NE(nullptr, node5_accessible);

  EXPECT_EQ(nullptr, manager->NextInTreeOrder(nullptr));
  EXPECT_EQ(node2_accessible, manager->NextInTreeOrder(root_accessible));
  EXPECT_EQ(node3_accessible, manager->NextInTreeOrder(node2_accessible));
  EXPECT_EQ(node4_accessible, manager->NextInTreeOrder(node3_accessible));
  EXPECT_EQ(node5_accessible, manager->NextInTreeOrder(node4_accessible));
  EXPECT_EQ(nullptr, manager->NextInTreeOrder(node5_accessible));

  EXPECT_EQ(nullptr, manager->PreviousInTreeOrder(nullptr, false));
  EXPECT_EQ(node4_accessible,
            manager->PreviousInTreeOrder(node5_accessible, false));
  EXPECT_EQ(node3_accessible,
            manager->PreviousInTreeOrder(node4_accessible, false));
  EXPECT_EQ(node2_accessible,
            manager->PreviousInTreeOrder(node3_accessible, false));
  EXPECT_EQ(root_accessible,
            manager->PreviousInTreeOrder(node2_accessible, false));
  EXPECT_EQ(nullptr, manager->PreviousInTreeOrder(root_accessible, false));

  EXPECT_EQ(nullptr, manager->PreviousInTreeOrder(nullptr, true));
  EXPECT_EQ(node4_accessible,
            manager->PreviousInTreeOrder(node5_accessible, true));
  EXPECT_EQ(node3_accessible,
            manager->PreviousInTreeOrder(node4_accessible, true));
  EXPECT_EQ(node2_accessible,
            manager->PreviousInTreeOrder(node3_accessible, true));
  EXPECT_EQ(root_accessible,
            manager->PreviousInTreeOrder(node2_accessible, true));
  EXPECT_EQ(node5_accessible,
            manager->PreviousInTreeOrder(root_accessible, true));

  EXPECT_EQ(ax::mojom::TreeOrder::kEqual,
            BrowserAccessibilityManager::CompareNodes(*root_accessible,
                                                      *root_accessible));

  EXPECT_EQ(ax::mojom::TreeOrder::kBefore,
            BrowserAccessibilityManager::CompareNodes(*node2_accessible,
                                                      *node3_accessible));
  EXPECT_EQ(ax::mojom::TreeOrder::kAfter,
            BrowserAccessibilityManager::CompareNodes(*node3_accessible,
                                                      *node2_accessible));

  EXPECT_EQ(ax::mojom::TreeOrder::kBefore,
            BrowserAccessibilityManager::CompareNodes(*node2_accessible,
                                                      *node4_accessible));
  EXPECT_EQ(ax::mojom::TreeOrder::kAfter,
            BrowserAccessibilityManager::CompareNodes(*node4_accessible,
                                                      *node2_accessible));

  EXPECT_EQ(ax::mojom::TreeOrder::kBefore,
            BrowserAccessibilityManager::CompareNodes(*node3_accessible,
                                                      *node4_accessible));
  EXPECT_EQ(ax::mojom::TreeOrder::kAfter,
            BrowserAccessibilityManager::CompareNodes(*node4_accessible,
                                                      *node3_accessible));

  EXPECT_EQ(ax::mojom::TreeOrder::kBefore,
            BrowserAccessibilityManager::CompareNodes(*root_accessible,
                                                      *node2_accessible));
  EXPECT_EQ(ax::mojom::TreeOrder::kAfter,
            BrowserAccessibilityManager::CompareNodes(*node2_accessible,
                                                      *root_accessible));
}

TEST_F(BrowserAccessibilityManagerTest, TestNextNonDescendantInTreeOrder) {
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;

  ui::AXNodeData node2;
  node2.id = 2;
  root.child_ids.push_back(2);

  ui::AXNodeData node3;
  node3.id = 3;
  root.child_ids.push_back(3);

  ui::AXNodeData node4;
  node4.id = 4;
  node3.child_ids.push_back(4);

  ui::AXNodeData node5;
  node5.id = 5;
  root.child_ids.push_back(5);

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, node2, node3, node4, node5),
          test_browser_accessibility_delegate_.get()));

  BrowserAccessibility* root_accessible = manager->GetRoot();
  ASSERT_NE(nullptr, root_accessible);
  ASSERT_EQ(3U, root_accessible->PlatformChildCount());
  BrowserAccessibility* node2_accessible = root_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, node2_accessible);
  BrowserAccessibility* node3_accessible = root_accessible->PlatformGetChild(1);
  ASSERT_NE(nullptr, node3_accessible);
  ASSERT_EQ(1U, node3_accessible->PlatformChildCount());
  BrowserAccessibility* node4_accessible =
      node3_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, node4_accessible);
  BrowserAccessibility* node5_accessible = root_accessible->PlatformGetChild(2);
  ASSERT_NE(nullptr, node5_accessible);

  EXPECT_EQ(nullptr, manager->NextNonDescendantInTreeOrder(nullptr));
  EXPECT_EQ(node2_accessible, manager->NextInTreeOrder(root_accessible));
  EXPECT_EQ(node3_accessible,
            manager->NextNonDescendantInTreeOrder(node2_accessible));
  EXPECT_EQ(node5_accessible,
            manager->NextNonDescendantInTreeOrder(node3_accessible));
  EXPECT_EQ(nullptr, manager->NextNonDescendantInTreeOrder(node5_accessible));
}

TEST_F(BrowserAccessibilityManagerTest, TestNextPreviousTextOnlyObject) {
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;

  ui::AXNodeData node2;
  node2.id = 2;
  root.child_ids.push_back(2);

  ui::AXNodeData text1;
  text1.id = 3;
  text1.role = ax::mojom::Role::kStaticText;
  root.child_ids.push_back(3);

  ui::AXNodeData node3;
  node3.id = 4;
  root.child_ids.push_back(4);

  ui::AXNodeData text2;
  text2.id = 5;
  text2.role = ax::mojom::Role::kStaticText;
  node3.child_ids.push_back(5);

  ui::AXNodeData node4;
  node4.id = 6;
  node3.child_ids.push_back(6);

  ui::AXNodeData text3;
  text3.id = 7;
  text3.role = ax::mojom::Role::kStaticText;
  node3.child_ids.push_back(7);

  ui::AXNodeData node5;
  node5.id = 8;
  node5.role = ax::mojom::Role::kGenericContainer;
  root.child_ids.push_back(8);

  ui::AXNodeData text4;
  text4.id = 9;
  text4.role = ax::mojom::Role::kLineBreak;
  node5.child_ids.push_back(9);

  ui::AXNodeData link;
  link.id = 10;
  link.role = ax::mojom::Role::kLink;
  node5.child_ids.push_back(10);

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, node2, node3, node4, node5, text1, text2,
                           text3, text4, link),
          test_browser_accessibility_delegate_.get()));

  BrowserAccessibility* root_accessible = manager->GetRoot();
  ASSERT_NE(nullptr, root_accessible);
  ASSERT_EQ(4U, root_accessible->PlatformChildCount());
  BrowserAccessibility* node2_accessible = root_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, node2_accessible);
  BrowserAccessibility* text1_accessible = root_accessible->PlatformGetChild(1);
  ASSERT_NE(nullptr, text1_accessible);
  BrowserAccessibility* node3_accessible = root_accessible->PlatformGetChild(2);
  ASSERT_NE(nullptr, node3_accessible);
  ASSERT_EQ(3U, node3_accessible->PlatformChildCount());
  BrowserAccessibility* text2_accessible =
      node3_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, text2_accessible);
  BrowserAccessibility* node4_accessible =
      node3_accessible->PlatformGetChild(1);
  ASSERT_NE(nullptr, node4_accessible);
  BrowserAccessibility* text3_accessible =
      node3_accessible->PlatformGetChild(2);
  ASSERT_NE(nullptr, text3_accessible);
  BrowserAccessibility* node5_accessible = root_accessible->PlatformGetChild(3);
  ASSERT_NE(nullptr, node5_accessible);
  ASSERT_EQ(2U, node5_accessible->PlatformChildCount());
  BrowserAccessibility* text4_accessible =
      node5_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, text4_accessible);

  EXPECT_EQ(nullptr, manager->NextTextOnlyObject(nullptr));
  EXPECT_EQ(text1_accessible, manager->NextTextOnlyObject(root_accessible));
  EXPECT_EQ(text1_accessible, manager->NextTextOnlyObject(node2_accessible));
  EXPECT_EQ(text2_accessible, manager->NextTextOnlyObject(text1_accessible));
  EXPECT_EQ(text2_accessible, manager->NextTextOnlyObject(node3_accessible));
  EXPECT_EQ(text3_accessible, manager->NextTextOnlyObject(text2_accessible));
  EXPECT_EQ(text3_accessible, manager->NextTextOnlyObject(node4_accessible));
  EXPECT_EQ(text4_accessible, manager->NextTextOnlyObject(text3_accessible));
  EXPECT_EQ(text4_accessible, manager->NextTextOnlyObject(node5_accessible));
  EXPECT_EQ(nullptr, manager->NextTextOnlyObject(text4_accessible));

  EXPECT_EQ(nullptr, manager->PreviousTextOnlyObject(nullptr));
  EXPECT_EQ(text3_accessible,
            manager->PreviousTextOnlyObject(text4_accessible));
  EXPECT_EQ(text3_accessible,
            manager->PreviousTextOnlyObject(node5_accessible));
  EXPECT_EQ(text2_accessible,
            manager->PreviousTextOnlyObject(text3_accessible));
  EXPECT_EQ(text2_accessible,
            manager->PreviousTextOnlyObject(node4_accessible));
  EXPECT_EQ(text1_accessible,
            manager->PreviousTextOnlyObject(text2_accessible));
  EXPECT_EQ(text1_accessible,
            manager->PreviousTextOnlyObject(node3_accessible));
  EXPECT_EQ(nullptr, manager->PreviousTextOnlyObject(node2_accessible));
  EXPECT_EQ(nullptr, manager->PreviousTextOnlyObject(root_accessible));
}

// This test depends on hypertext, which is only used on
// Linux and Windows.
#if defined(OS_WIN) || BUILDFLAG(USE_ATK)
TEST_F(BrowserAccessibilityManagerTest, TestFindIndicesInCommonParent) {
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;

  ui::AXNodeData div;
  div.id = 2;
  div.role = ax::mojom::Role::kGenericContainer;
  root.child_ids.push_back(div.id);

  ui::AXNodeData button;
  button.id = 3;
  button.role = ax::mojom::Role::kButton;
  div.child_ids.push_back(button.id);

  ui::AXNodeData button_text;
  button_text.id = 4;
  button_text.role = ax::mojom::Role::kStaticText;
  button_text.SetName("Button");
  button.child_ids.push_back(button_text.id);

  ui::AXNodeData line_break;
  line_break.id = 5;
  line_break.role = ax::mojom::Role::kLineBreak;
  line_break.SetName("\n");
  div.child_ids.push_back(line_break.id);

  ui::AXNodeData paragraph;
  paragraph.id = 6;
  paragraph.role = ax::mojom::Role::kParagraph;
  root.child_ids.push_back(paragraph.id);

  ui::AXNodeData paragraph_text;
  paragraph_text.id = 7;
  paragraph_text.role = ax::mojom::Role::kStaticText;
  paragraph.child_ids.push_back(paragraph_text.id);

  ui::AXNodeData paragraph_line1;
  paragraph_line1.id = 8;
  paragraph_line1.role = ax::mojom::Role::kInlineTextBox;
  paragraph_line1.SetName("Hello ");
  paragraph_text.child_ids.push_back(paragraph_line1.id);

  ui::AXNodeData paragraph_line2;
  paragraph_line2.id = 9;
  paragraph_line2.role = ax::mojom::Role::kInlineTextBox;
  paragraph_line2.SetName("world.");
  paragraph_text.child_ids.push_back(paragraph_line2.id);

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, div, button, button_text, line_break,
                           paragraph, paragraph_text, paragraph_line1,
                           paragraph_line2),
          test_browser_accessibility_delegate_.get()));

  BrowserAccessibility* root_accessible = manager->GetRoot();
  ASSERT_NE(nullptr, root_accessible);
  ASSERT_EQ(2U, root_accessible->PlatformChildCount());
  BrowserAccessibility* div_accessible = root_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, div_accessible);
  ASSERT_EQ(2U, div_accessible->PlatformChildCount());
  BrowserAccessibility* button_accessible = div_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, button_accessible);
  BrowserAccessibility* button_text_accessible =
      button_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, button_text_accessible);
  BrowserAccessibility* line_break_accessible =
      div_accessible->PlatformGetChild(1);
  ASSERT_NE(nullptr, line_break_accessible);
  BrowserAccessibility* paragraph_accessible =
      root_accessible->PlatformGetChild(1);
  ASSERT_NE(nullptr, paragraph_accessible);
  BrowserAccessibility* paragraph_text_accessible =
      paragraph_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, paragraph_text_accessible);
  ASSERT_EQ(2U, paragraph_text_accessible->InternalChildCount());
  BrowserAccessibility* paragraph_line1_accessible =
      paragraph_text_accessible->InternalGetChild(0);
  ASSERT_NE(nullptr, paragraph_line1_accessible);
  BrowserAccessibility* paragraph_line2_accessible =
      paragraph_text_accessible->InternalGetChild(1);
  ASSERT_NE(nullptr, paragraph_line2_accessible);

  BrowserAccessibility* common_parent = nullptr;
  int child_index1, child_index2;
  EXPECT_FALSE(BrowserAccessibilityManager::FindIndicesInCommonParent(
      *root_accessible, *root_accessible, &common_parent, &child_index1,
      &child_index2));
  EXPECT_EQ(nullptr, common_parent);
  EXPECT_EQ(-1, child_index1);
  EXPECT_EQ(-1, child_index2);

  EXPECT_TRUE(BrowserAccessibilityManager::FindIndicesInCommonParent(
      *div_accessible, *paragraph_accessible, &common_parent, &child_index1,
      &child_index2));
  EXPECT_EQ(root_accessible, common_parent);
  EXPECT_EQ(0, child_index1);
  EXPECT_EQ(1, child_index2);

  EXPECT_TRUE(BrowserAccessibilityManager::FindIndicesInCommonParent(
      *div_accessible, *paragraph_line1_accessible, &common_parent,
      &child_index1, &child_index2));
  EXPECT_EQ(root_accessible, common_parent);
  EXPECT_EQ(0, child_index1);
  EXPECT_EQ(1, child_index2);

  EXPECT_TRUE(BrowserAccessibilityManager::FindIndicesInCommonParent(
      *line_break_accessible, *paragraph_text_accessible, &common_parent,
      &child_index1, &child_index2));
  EXPECT_EQ(root_accessible, common_parent);
  EXPECT_EQ(0, child_index1);
  EXPECT_EQ(1, child_index2);

  EXPECT_TRUE(BrowserAccessibilityManager::FindIndicesInCommonParent(
      *button_text_accessible, *line_break_accessible, &common_parent,
      &child_index1, &child_index2));
  EXPECT_EQ(div_accessible, common_parent);
  EXPECT_EQ(0, child_index1);
  EXPECT_EQ(1, child_index2);

  EXPECT_TRUE(BrowserAccessibilityManager::FindIndicesInCommonParent(
      *paragraph_accessible, *paragraph_line2_accessible, &common_parent,
      &child_index1, &child_index2));
  EXPECT_EQ(root_accessible, common_parent);
  EXPECT_EQ(1, child_index1);
  EXPECT_EQ(1, child_index2);

  EXPECT_TRUE(BrowserAccessibilityManager::FindIndicesInCommonParent(
      *paragraph_text_accessible, *paragraph_line1_accessible, &common_parent,
      &child_index1, &child_index2));
  EXPECT_EQ(paragraph_accessible, common_parent);
  EXPECT_EQ(0, child_index1);
  EXPECT_EQ(0, child_index2);

  EXPECT_TRUE(BrowserAccessibilityManager::FindIndicesInCommonParent(
      *paragraph_line1_accessible, *paragraph_line2_accessible, &common_parent,
      &child_index1, &child_index2));
  EXPECT_EQ(paragraph_text_accessible, common_parent);
  EXPECT_EQ(0, child_index1);
  EXPECT_EQ(1, child_index2);
}
#endif  // defined(OS_WIN) || BUILDFLAG(USE_ATK)

// This test depends on hypertext, which is only used on
// Linux and Windows.
#if defined(OS_WIN) || BUILDFLAG(USE_ATK)
TEST_F(BrowserAccessibilityManagerTest, TestGetTextForRange) {
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;

  ui::AXNodeData div;
  div.id = 2;
  div.role = ax::mojom::Role::kGenericContainer;
  root.child_ids.push_back(div.id);

  ui::AXNodeData button;
  button.id = 3;
  button.role = ax::mojom::Role::kButton;
  div.child_ids.push_back(button.id);

  ui::AXNodeData button_text;
  button_text.id = 4;
  button_text.role = ax::mojom::Role::kStaticText;
  button_text.SetName("Button");
  button.child_ids.push_back(button_text.id);

  ui::AXNodeData container;
  container.id = 5;
  container.role = ax::mojom::Role::kGenericContainer;
  div.child_ids.push_back(container.id);

  ui::AXNodeData container_text;
  container_text.id = 6;
  container_text.role = ax::mojom::Role::kStaticText;
  container_text.SetName("Text");
  container.child_ids.push_back(container_text.id);

  ui::AXNodeData line_break;
  line_break.id = 7;
  line_break.role = ax::mojom::Role::kLineBreak;
  line_break.SetName("\n");
  div.child_ids.push_back(line_break.id);

  ui::AXNodeData paragraph;
  paragraph.id = 8;
  paragraph.role = ax::mojom::Role::kParagraph;
  root.child_ids.push_back(paragraph.id);

  ui::AXNodeData paragraph_text;
  paragraph_text.id = 9;
  paragraph_text.role = ax::mojom::Role::kStaticText;
  paragraph_text.SetName("Hello world.");
  paragraph.child_ids.push_back(paragraph_text.id);

  ui::AXNodeData paragraph_line1;
  paragraph_line1.id = 10;
  paragraph_line1.role = ax::mojom::Role::kInlineTextBox;
  paragraph_line1.SetName("Hello ");
  paragraph_text.child_ids.push_back(paragraph_line1.id);

  ui::AXNodeData paragraph_line2;
  paragraph_line2.id = 11;
  paragraph_line2.role = ax::mojom::Role::kInlineTextBox;
  paragraph_line2.SetName("world.");
  paragraph_text.child_ids.push_back(paragraph_line2.id);

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, div, button, button_text, container,
                           container_text, line_break, paragraph,
                           paragraph_text, paragraph_line1, paragraph_line2),
          test_browser_accessibility_delegate_.get()));

  BrowserAccessibility* root_accessible = manager->GetRoot();
  ASSERT_NE(nullptr, root_accessible);
  ASSERT_EQ(2U, root_accessible->PlatformChildCount());
  BrowserAccessibility* div_accessible = root_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, div_accessible);
  ASSERT_EQ(3U, div_accessible->PlatformChildCount());
  BrowserAccessibility* button_accessible = div_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, button_accessible);
  BrowserAccessibility* button_text_accessible =
      button_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, button_text_accessible);
  BrowserAccessibility* container_accessible =
      div_accessible->PlatformGetChild(1);
  ASSERT_NE(nullptr, container_accessible);
  BrowserAccessibility* container_text_accessible =
      container_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, container_text_accessible);
  BrowserAccessibility* line_break_accessible =
      div_accessible->PlatformGetChild(2);
  ASSERT_NE(nullptr, line_break_accessible);
  BrowserAccessibility* paragraph_accessible =
      root_accessible->PlatformGetChild(1);
  ASSERT_NE(nullptr, paragraph_accessible);
  BrowserAccessibility* paragraph_text_accessible =
      paragraph_accessible->PlatformGetChild(0);
  ASSERT_NE(nullptr, paragraph_text_accessible);
  ASSERT_EQ(2U, paragraph_text_accessible->InternalChildCount());
  BrowserAccessibility* paragraph_line1_accessible =
      paragraph_text_accessible->InternalGetChild(0);
  ASSERT_NE(nullptr, paragraph_line1_accessible);
  BrowserAccessibility* paragraph_line2_accessible =
      paragraph_text_accessible->InternalGetChild(1);
  ASSERT_NE(nullptr, paragraph_line2_accessible);

  std::vector<const BrowserAccessibility*> text_only_objects =
      BrowserAccessibilityManager::FindTextOnlyObjectsInRange(*root_accessible,
                                                              *root_accessible);

  EXPECT_EQ(3U, text_only_objects.size());
  EXPECT_EQ(container_text_accessible, text_only_objects[0]);
  EXPECT_EQ(line_break_accessible, text_only_objects[1]);
  EXPECT_EQ(paragraph_text_accessible, text_only_objects[2]);

  text_only_objects = BrowserAccessibilityManager::FindTextOnlyObjectsInRange(
      *div_accessible, *paragraph_accessible);
  EXPECT_EQ(3U, text_only_objects.size());
  EXPECT_EQ(container_text_accessible, text_only_objects[0]);
  EXPECT_EQ(line_break_accessible, text_only_objects[1]);
  EXPECT_EQ(paragraph_text_accessible, text_only_objects[2]);

  EXPECT_EQ(u"Text\nHello world.",
            BrowserAccessibilityManager::GetTextForRange(*root_accessible, 0,
                                                         *root_accessible, 16));
  EXPECT_EQ(u"xt\nHello world.",
            BrowserAccessibilityManager::GetTextForRange(*root_accessible, 2,
                                                         *root_accessible, 12));
  EXPECT_EQ(u"Text\nHello world.",
            BrowserAccessibilityManager::GetTextForRange(
                *div_accessible, 0, *paragraph_accessible, 12));
  EXPECT_EQ(u"xt\nHello world.",
            BrowserAccessibilityManager::GetTextForRange(
                *div_accessible, 2, *paragraph_accessible, 12));
  EXPECT_EQ(u"Text\n", BrowserAccessibilityManager::GetTextForRange(
                           *div_accessible, 0, *div_accessible, 4));
  EXPECT_EQ(u"Text\n", BrowserAccessibilityManager::GetTextForRange(
                           *button_accessible, 0, *line_break_accessible, 4));

  EXPECT_EQ(u"Hello world.",
            BrowserAccessibilityManager::GetTextForRange(
                *paragraph_accessible, 0, *paragraph_accessible, 12));
  EXPECT_EQ(u"Hello wor",
            BrowserAccessibilityManager::GetTextForRange(
                *paragraph_accessible, 0, *paragraph_accessible, 9));
  EXPECT_EQ(u"Hello world.",
            BrowserAccessibilityManager::GetTextForRange(
                *paragraph_text_accessible, 0, *paragraph_text_accessible, 12));
  EXPECT_EQ(u" world.",
            BrowserAccessibilityManager::GetTextForRange(
                *paragraph_text_accessible, 5, *paragraph_text_accessible, 12));
  EXPECT_EQ(u"Hello world.",
            BrowserAccessibilityManager::GetTextForRange(
                *paragraph_accessible, 0, *paragraph_text_accessible, 12));
  EXPECT_EQ(u"Hello ", BrowserAccessibilityManager::GetTextForRange(
                           *paragraph_line1_accessible, 0,
                           *paragraph_line1_accessible, 6));
  EXPECT_EQ(u"Hello", BrowserAccessibilityManager::GetTextForRange(
                          *paragraph_line1_accessible, 0,
                          *paragraph_line1_accessible, 5));
  EXPECT_EQ(u"ello ", BrowserAccessibilityManager::GetTextForRange(
                          *paragraph_line1_accessible, 1,
                          *paragraph_line1_accessible, 6));
  EXPECT_EQ(u"world.", BrowserAccessibilityManager::GetTextForRange(
                           *paragraph_line2_accessible, 0,
                           *paragraph_line2_accessible, 6));
  EXPECT_EQ(u"orld", BrowserAccessibilityManager::GetTextForRange(
                         *paragraph_line2_accessible, 1,
                         *paragraph_line2_accessible, 5));
  EXPECT_EQ(u"Hello world.", BrowserAccessibilityManager::GetTextForRange(
                                 *paragraph_line1_accessible, 0,
                                 *paragraph_line2_accessible, 6));
  // Start and end positions could be reversed.
  EXPECT_EQ(u"Hello world.", BrowserAccessibilityManager::GetTextForRange(
                                 *paragraph_line2_accessible, 6,
                                 *paragraph_line1_accessible, 0));
}
#endif  // defined(OS_WIN) || BUILDFLAG(USE_ATK)

TEST_F(BrowserAccessibilityManagerTest, DeletingFocusedNodeDoesNotCrash) {
  // Create a really simple tree with one root node and one focused child.
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;
  root.child_ids.push_back(2);

  ui::AXNodeData node2;
  node2.id = 2;

  ui::AXTreeUpdate initial_state = MakeAXTreeUpdate(root, node2);
  initial_state.has_tree_data = true;
  initial_state.tree_data.focus_id = 2;
  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          initial_state, test_browser_accessibility_delegate_.get()));

  EXPECT_EQ(1, manager->GetRoot()->GetId());
  ASSERT_NE(nullptr, manager->GetFocus());
  EXPECT_EQ(2, manager->GetFocus()->GetId());

  // Now replace the tree with a new tree consisting of a single root.
  ui::AXNodeData root2;
  root2.id = 3;
  root2.role = ax::mojom::Role::kRootWebArea;

  ui::AXTreeUpdate update2 = MakeAXTreeUpdate(root2);
  update2.node_id_to_clear = root.id;
  update2.root_id = root2.id;
  AXEventNotificationDetails events2;
  events2.updates = {update2};
  ASSERT_TRUE(manager->OnAccessibilityEvents(events2));

  // Make sure that the focused node was updated to the new root and
  // that this doesn't crash.
  EXPECT_EQ(3, manager->GetRoot()->GetId());
  ASSERT_NE(nullptr, manager->GetFocus());
  EXPECT_EQ(3, manager->GetFocus()->GetId());
}

TEST_F(BrowserAccessibilityManagerTest, DeletingFocusedNodeDoesNotCrash2) {
  // Create a really simple tree with one root node and one focused child.
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;
  root.child_ids.push_back(2);
  root.child_ids.push_back(3);
  root.child_ids.push_back(4);

  ui::AXNodeData node2;
  node2.id = 2;

  ui::AXNodeData node3;
  node3.id = 3;

  ui::AXNodeData node4;
  node4.id = 4;

  ui::AXTreeUpdate initial_state = MakeAXTreeUpdate(root, node2, node3, node4);
  initial_state.has_tree_data = true;
  initial_state.tree_data.focus_id = 2;
  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          initial_state, test_browser_accessibility_delegate_.get()));

  EXPECT_EQ(1, manager->GetRoot()->GetId());
  ASSERT_NE(nullptr, manager->GetFocus());
  EXPECT_EQ(2, manager->GetFocus()->GetId());

  // Now replace the tree with a new tree consisting of a single root.
  ui::AXNodeData root2;
  root2.id = 3;
  root2.role = ax::mojom::Role::kRootWebArea;

  // Make an update that explicitly clears the previous root.
  ui::AXTreeUpdate update2 = MakeAXTreeUpdate(root2);
  update2.node_id_to_clear = root.id;
  update2.root_id = root2.id;
  AXEventNotificationDetails events2;
  events2.updates = {update2};
  ASSERT_TRUE(manager->OnAccessibilityEvents(events2));

  // Make sure that the focused node was updated to the new root and
  // that this doesn't crash.
  EXPECT_EQ(3, manager->GetRoot()->GetId());
  ASSERT_NE(nullptr, manager->GetFocus());
  EXPECT_EQ(3, manager->GetFocus()->GetId());
}

TEST_F(BrowserAccessibilityManagerTest, IsIgnoredChangedDueToFocusChange) {
  ui::AXTree::SetFocusedNodeShouldNeverBeIgnored();
  ui::AXNodeData root;
  ui::AXNodeData button;
  root.id = 1;
  button.id = 2;

  root.role = ax::mojom::Role::kRootWebArea;
  root.child_ids = {button.id};

  button.role = ax::mojom::Role::kButton;
  button.AddState(ax::mojom::State::kIgnored);

  ui::AXTreeUpdate initial_state = MakeAXTreeUpdate(root, button);
  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          initial_state, test_browser_accessibility_delegate_.get()));
  ASSERT_EQ(0u, manager->GetRoot()->PlatformChildCount());

  // On purpose we omit "button" from the list of updated nodes. It should be
  // added due to the focus changing.
  ui::AXTreeUpdate update = MakeAXTreeUpdate(root);
  ui::AXTreeData tree_data;
  tree_data.focused_tree_id = manager->GetTreeID();
  tree_data.focus_id = button.id;
  update.has_tree_data = true;
  update.tree_data = tree_data;
  AXEventNotificationDetails events;
  events.updates = {update};
  ASSERT_TRUE(manager->OnAccessibilityEvents(events));

  // Now that the button is focused, it is no longer ignored.
  ASSERT_EQ(1u, manager->GetRoot()->PlatformChildCount());
  EXPECT_EQ(ax::mojom::Role::kButton,
            manager->GetRoot()->PlatformGetChild(0)->GetRole());
}

TEST_F(BrowserAccessibilityManagerTest, TreeUpdatesAreMergedWhenPossible) {
  ui::AXTreeUpdate tree;
  tree.root_id = 1;
  tree.nodes.resize(4);
  tree.nodes[0].id = 1;
  tree.nodes[0].role = ax::mojom::Role::kMenu;
  tree.nodes[0].child_ids = {2, 3, 4};
  tree.nodes[1].id = 2;
  tree.nodes[1].role = ax::mojom::Role::kMenuItem;
  tree.nodes[2].id = 3;
  tree.nodes[2].role = ax::mojom::Role::kMenuItemCheckBox;
  tree.nodes[3].id = 4;
  tree.nodes[3].role = ax::mojom::Role::kMenuItemRadio;

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          tree, test_browser_accessibility_delegate_.get()));

  CountingAXTreeObserver observer;
  manager->ax_tree()->AddObserver(&observer);

  // Update each of the children using separate AXTreeUpdates.
  AXEventNotificationDetails events;
  events.updates.resize(3);
  for (int i = 0; i < 3; i++) {
    ui::AXTreeUpdate update;
    update.root_id = 1;
    update.nodes.resize(1);
    update.nodes[0].id = 2 + i;
    events.updates[i] = update;
  }
  events.updates[0].nodes[0].role = ax::mojom::Role::kMenuItemCheckBox;
  events.updates[1].nodes[0].role = ax::mojom::Role::kMenuItemRadio;
  events.updates[2].nodes[0].role = ax::mojom::Role::kMenuItem;
  ASSERT_TRUE(manager->OnAccessibilityEvents(events));

  // These should have been merged into a single tree update.
  EXPECT_EQ(1, observer.update_count());

  EXPECT_EQ(ax::mojom::Role::kMenuItemCheckBox,
            manager->GetFromID(2)->GetRole());
  EXPECT_EQ(ax::mojom::Role::kMenuItemRadio, manager->GetFromID(3)->GetRole());
  EXPECT_EQ(ax::mojom::Role::kMenuItem, manager->GetFromID(4)->GetRole());

  // Remove the observer before the manager is destroyed.
  manager->ax_tree()->RemoveObserver(&observer);
}

TEST_F(BrowserAccessibilityManagerTest, TestHitTestScaled) {
  // We're creating two linked trees, each of which has two nodes; first create
  // the child tree's nodes & tree-update.
  ui::AXNodeData child_root;
  child_root.id = 1;
  child_root.role = ax::mojom::Role::kRootWebArea;
  child_root.SetName("child_root");
  child_root.relative_bounds.bounds = gfx::RectF(0, 0, 100, 100);
  child_root.child_ids = {2};

  ui::AXNodeData child_child;
  child_child.id = 2;
  child_child.role = ax::mojom::Role::kGenericContainer;
  child_child.SetName("child_child");
  child_child.relative_bounds.bounds = gfx::RectF(0, 0, 100, 100);

  ui::AXTreeUpdate child_update = MakeAXTreeUpdate(child_root, child_child);

  // Next, create the parent tree's nodes and tree-update, with kChildTreeId
  // pointing to the child tree.
  ui::AXNodeData parent_root;
  parent_root.id = 1;
  parent_root.role = ax::mojom::Role::kRootWebArea;
  parent_root.SetName("parent_root");
  parent_root.relative_bounds.bounds = gfx::RectF(0, 0, 200, 200);
  parent_root.child_ids = {2, 3};

  ui::AXNodeData parent_child;
  parent_child.id = 2;
  parent_child.role = ax::mojom::Role::kGenericContainer;
  parent_child.SetName("parent_child");
  parent_child.relative_bounds.bounds = gfx::RectF(0, 0, 100, 100);

  ui::AXNodeData parent_childtree;
  parent_childtree.id = 3;
  parent_childtree.AddChildTreeId(child_update.tree_data.tree_id);
  parent_childtree.SetName("parent_childtree");
  parent_childtree.relative_bounds.bounds = gfx::RectF(100, 100, 100, 100);

  ui::AXTreeUpdate parent_update =
      MakeAXTreeUpdate(parent_root, parent_child, parent_childtree);

  // Link the child tree to its parent.
  child_update.tree_data.parent_tree_id = parent_update.tree_data.tree_id;

  // Create the two managers.
  std::unique_ptr<BrowserAccessibilityManager> parent_manager(
      BrowserAccessibilityManager::Create(
          parent_update, test_browser_accessibility_delegate_.get()));

  std::unique_ptr<BrowserAccessibilityManager> child_manager(
      BrowserAccessibilityManager::Create(
          child_update, test_browser_accessibility_delegate_.get()));

  // Verify that they're properly connected.
  ASSERT_EQ(parent_manager.get(), child_manager->GetRootManager());

  // Set scaling factor for testing to be 200%
  parent_manager->UseCustomDeviceScaleFactorForTesting(2.0f);
  child_manager->UseCustomDeviceScaleFactorForTesting(2.0f);

  // Run the hit-test; we should get the same result regardless of whether we
  // start from the parent_manager or the child_manager.
  auto* hittest1 = parent_manager->CachingAsyncHitTest(gfx::Point(75, 75));
  ASSERT_NE(nullptr, hittest1);
  ASSERT_EQ("parent_child",
            hittest1->GetStringAttribute(ax::mojom::StringAttribute::kName));

  auto* hittest2 = child_manager->CachingAsyncHitTest(gfx::Point(75, 75));
  ASSERT_NE(nullptr, hittest2);
  ASSERT_EQ("parent_child",
            hittest2->GetStringAttribute(ax::mojom::StringAttribute::kName));
}

TEST_F(BrowserAccessibilityManagerTest, TestShouldFireEventForNode) {
  ui::AXNodeData inline_text;
  inline_text.id = 1111;
  inline_text.role = ax::mojom::Role::kInlineTextBox;
  inline_text.SetName("One two three.");

  ui::AXNodeData text;
  text.id = 111;
  text.role = ax::mojom::Role::kStaticText;
  text.SetName("One two three.");
  text.child_ids = {inline_text.id};

  ui::AXNodeData paragraph;
  paragraph.id = 11;
  paragraph.role = ax::mojom::Role::kParagraph;
  paragraph.child_ids = {text.id};

  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;
  root.child_ids = {paragraph.id};

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, paragraph, text, inline_text),
          test_browser_accessibility_delegate_.get()));

  EXPECT_TRUE(manager->ShouldFireEventForNode(manager->GetFromID(1)));
  EXPECT_TRUE(manager->ShouldFireEventForNode(manager->GetFromID(11)));
  EXPECT_TRUE(manager->ShouldFireEventForNode(manager->GetFromID(111)));
#if defined(OS_ANDROID)
  // On Android, ShouldFireEventForNode walks up the ancestor that's a leaf node
  // node and the event is fired on the updated target.
  EXPECT_TRUE(manager->ShouldFireEventForNode(manager->GetFromID(1111)));
#else
  EXPECT_FALSE(manager->ShouldFireEventForNode(manager->GetFromID(1111)));
#endif
}

TEST_F(BrowserAccessibilityManagerTest, NestedChildRoot) {
  ui::AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kRootWebArea;

  ui::AXNodeData popup_button;
  popup_button.id = 2;
  popup_button.role = ax::mojom::Role::kPopUpButton;
  root.child_ids.push_back(2);

  ui::AXNodeData child_tree_root;
  child_tree_root.id = 3;
  child_tree_root.role = ax::mojom::Role::kRootWebArea;
  root.child_ids.push_back(3);

  std::unique_ptr<BrowserAccessibilityManager> manager(
      BrowserAccessibilityManager::Create(
          MakeAXTreeUpdate(root, popup_button, child_tree_root),
          test_browser_accessibility_delegate_.get()));

  ASSERT_NE(manager->GetPopupRoot(), nullptr);
  EXPECT_EQ(manager->GetPopupRoot()->GetId(), 3);

  // Update tree to change the role of the nested child root, add new child root
  // in same update.
  child_tree_root.role = ax::mojom::Role::kGroup;
  ui::AXNodeData second_child_tree_root;
  second_child_tree_root.id = 4;
  second_child_tree_root.role = ax::mojom::Role::kRootWebArea;
  root.child_ids.push_back(4);

  manager->Initialize(
      MakeAXTreeUpdate(root, child_tree_root, second_child_tree_root));

  ASSERT_NE(manager->GetPopupRoot(), nullptr);
  EXPECT_EQ(manager->GetPopupRoot()->GetId(), 4);

  // Update tree to change the role of the nested child root, so that there is
  // no longer any nested child root.
  second_child_tree_root.role = ax::mojom::Role::kGroup;
  manager->Initialize(MakeAXTreeUpdate(second_child_tree_root));
  EXPECT_EQ(manager->GetPopupRoot(), nullptr);

  // Test deleting child root.

  // First, ensure a child root exists.
  second_child_tree_root.role = ax::mojom::Role::kRootWebArea;
  manager->Initialize(MakeAXTreeUpdate(second_child_tree_root));
  ASSERT_NE(manager->GetPopupRoot(), nullptr);
  EXPECT_EQ(manager->GetPopupRoot()->GetId(), 4);

  // Now remove the child root from the tree.
  root.child_ids = {2, 3};
  manager->Initialize(MakeAXTreeUpdate(root));
  EXPECT_EQ(manager->GetPopupRoot(), nullptr);
}

}  // namespace content
