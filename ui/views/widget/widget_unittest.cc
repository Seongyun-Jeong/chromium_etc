// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <set>

#include "base/bind.h"
#include "base/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/hit_test.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/compositor/test/draw_waiter_for_test.h"
#include "ui/events/event_observer.h"
#include "ui/events/event_utils.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/buildflags.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/event_monitor.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/test/native_widget_factory.h"
#include "ui/views/test/test_views.h"
#include "ui/views/test/test_widget_observer.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/view_test_api.h"
#include "ui/views/views_test_suite.h"
#include "ui/views/widget/native_widget_delegate.h"
#include "ui/views/widget/native_widget_private.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/widget_deletion_observer.h"
#include "ui/views/widget/widget_removals_observer.h"
#include "ui/views/widget/widget_utils.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/window/native_frame_view.h"

#if defined(OS_WIN)
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/view_prop.h"
#include "ui/base/win/window_event_target.h"
#include "ui/views/test/test_platform_native_widget.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/win/hwnd_util.h"
#endif

#if defined(OS_MAC)
#include "base/mac/mac_util.h"
#endif

#if defined(USE_AURA) && !BUILDFLAG(ENABLE_DESKTOP_AURA)
#include "ui/wm/core/base_focus_rules.h"
#include "ui/wm/core/focus_controller.h"
#include "ui/wm/core/shadow_controller.h"
#include "ui/wm/core/shadow_controller_delegate.h"
#endif

#if defined(USE_OZONE)
#include "ui/ozone/public/ozone_platform.h"
#include "ui/ozone/public/platform_gl_egl_utility.h"
#endif

namespace views {
namespace test {

namespace {

// TODO(tdanderson): This utility function is used in different unittest
//                   files. Move to a common location to avoid
//                   repeated code.
gfx::Point ConvertPointFromWidgetToView(View* view, const gfx::Point& p) {
  gfx::Point tmp(p);
  View::ConvertPointToTarget(view->GetWidget()->GetRootView(), view, &tmp);
  return tmp;
}

std::unique_ptr<ui::test::EventGenerator> CreateEventGenerator(
    gfx::NativeWindow root_window,
    gfx::NativeWindow target_window) {
  auto generator =
      std::make_unique<ui::test::EventGenerator>(root_window, target_window);
  return generator;
}

class TestBubbleDialogDelegateView : public BubbleDialogDelegateView {
 public:
  explicit TestBubbleDialogDelegateView(View* anchor)
      : BubbleDialogDelegateView(anchor, BubbleBorder::NONE),
        reset_controls_called_(false) {}
  ~TestBubbleDialogDelegateView() override = default;

  bool ShouldShowCloseButton() const override {
    reset_controls_called_ = true;
    return true;
  }

  mutable bool reset_controls_called_;
};

}  // namespace

// A view that keeps track of the events it receives, and consumes all scroll
// gesture events and ui::ET_SCROLL events.
class ScrollableEventCountView : public EventCountView {
 public:
  ScrollableEventCountView() = default;

  ScrollableEventCountView(const ScrollableEventCountView&) = delete;
  ScrollableEventCountView& operator=(const ScrollableEventCountView&) = delete;

  ~ScrollableEventCountView() override = default;

 private:
  // Overridden from ui::EventHandler:
  void OnGestureEvent(ui::GestureEvent* event) override {
    EventCountView::OnGestureEvent(event);
    switch (event->type()) {
      case ui::ET_GESTURE_SCROLL_BEGIN:
      case ui::ET_GESTURE_SCROLL_UPDATE:
      case ui::ET_GESTURE_SCROLL_END:
      case ui::ET_SCROLL_FLING_START:
        event->SetHandled();
        break;
      default:
        break;
    }
  }

  void OnScrollEvent(ui::ScrollEvent* event) override {
    EventCountView::OnScrollEvent(event);
    if (event->type() == ui::ET_SCROLL)
      event->SetHandled();
  }
};

// A view that implements GetMinimumSize.
class MinimumSizeFrameView : public NativeFrameView {
 public:
  explicit MinimumSizeFrameView(Widget* frame) : NativeFrameView(frame) {}

  MinimumSizeFrameView(const MinimumSizeFrameView&) = delete;
  MinimumSizeFrameView& operator=(const MinimumSizeFrameView&) = delete;

  ~MinimumSizeFrameView() override = default;

 private:
  // Overridden from View:
  gfx::Size GetMinimumSize() const override { return gfx::Size(300, 400); }
};

// An event handler that simply keeps a count of the different types of events
// it receives.
class EventCountHandler : public ui::EventHandler {
 public:
  EventCountHandler() = default;

  EventCountHandler(const EventCountHandler&) = delete;
  EventCountHandler& operator=(const EventCountHandler&) = delete;

  ~EventCountHandler() override = default;

  int GetEventCount(ui::EventType type) { return event_count_[type]; }

  void ResetCounts() { event_count_.clear(); }

 protected:
  // Overridden from ui::EventHandler:
  void OnEvent(ui::Event* event) override {
    RecordEvent(*event);
    ui::EventHandler::OnEvent(event);
  }

 private:
  void RecordEvent(const ui::Event& event) { ++event_count_[event.type()]; }

  std::map<ui::EventType, int> event_count_;
};

TEST_F(WidgetTest, WidgetInitParams) {
  // Widgets are not transparent by default.
  Widget::InitParams init1;
  EXPECT_EQ(Widget::InitParams::WindowOpacity::kInferred, init1.opacity);
}

// Tests that the internal name is propagated through widget initialization to
// the native widget and back.
class WidgetWithCustomParamsTest : public WidgetTest {
 public:
  using InitFunction = base::RepeatingCallback<void(Widget::InitParams*)>;
  void SetInitFunction(const InitFunction& init) { init_ = std::move(init); }
  Widget::InitParams CreateParams(Widget::InitParams::Type type) override {
    Widget::InitParams params = WidgetTest::CreateParams(type);
    DCHECK(init_) << "If you don't need an init function, use WidgetTest";
    init_.Run(&params);
    return params;
  }

 private:
  InitFunction init_;
};

TEST_F(WidgetWithCustomParamsTest, NamePropagatedFromParams) {
  SetInitFunction(base::BindLambdaForTesting(
      [](Widget::InitParams* params) { params->name = "MyWidget"; }));
  std::unique_ptr<Widget> widget = CreateTestWidget();

  EXPECT_EQ("MyWidget", widget->native_widget_private()->GetName());
  EXPECT_EQ("MyWidget", widget->GetName());
}

TEST_F(WidgetWithCustomParamsTest, NamePropagatedFromDelegate) {
  WidgetDelegate delegate;
  delegate.set_internal_name("Foobar");
  SetInitFunction(base::BindLambdaForTesting(
      [&](Widget::InitParams* params) { params->delegate = &delegate; }));

  std::unique_ptr<Widget> widget = CreateTestWidget();

  EXPECT_EQ(delegate.internal_name(),
            widget->native_widget_private()->GetName());
  EXPECT_EQ(delegate.internal_name(), widget->GetName());
}

TEST_F(WidgetWithCustomParamsTest, NamePropagatedFromContentsViewClassName) {
  class ViewWithClassName : public View {
   public:
    const char* GetClassName() const override { return "ViewWithClassName"; }
  };

  WidgetDelegate delegate;
  auto view = std::make_unique<ViewWithClassName>();
  auto* contents = delegate.SetContentsView(std::move(view));
  SetInitFunction(base::BindLambdaForTesting(
      [&](Widget::InitParams* params) { params->delegate = &delegate; }));

  std::unique_ptr<Widget> widget = CreateTestWidget();

  EXPECT_EQ(contents->GetClassName(),
            widget->native_widget_private()->GetName());
  EXPECT_EQ(contents->GetClassName(), widget->GetName());
}

TEST_F(WidgetTest, NativeWindowProperty) {
  const char* key = "foo";
  int value = 3;

  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  EXPECT_EQ(nullptr, widget->GetNativeWindowProperty(key));

  widget->SetNativeWindowProperty(key, &value);
  EXPECT_EQ(&value, widget->GetNativeWindowProperty(key));

  widget->SetNativeWindowProperty(key, nullptr);
  EXPECT_EQ(nullptr, widget->GetNativeWindowProperty(key));
}

TEST_F(WidgetTest, GetParent) {
  // Create a hierarchy of native widgets.
  WidgetAutoclosePtr toplevel(CreateTopLevelPlatformWidget());
  Widget* child = CreateChildPlatformWidget(toplevel->GetNativeView());
  Widget* grandchild = CreateChildPlatformWidget(child->GetNativeView());

  EXPECT_EQ(nullptr, toplevel->parent());
  EXPECT_EQ(child, grandchild->parent());
  EXPECT_EQ(toplevel.get(), child->parent());

  // children should be automatically destroyed with |toplevel|.
}

// Verify that there is no change in focus if |enable_arrow_key_traversal| is
// false (the default).
TEST_F(WidgetTest, ArrowKeyFocusTraversalOffByDefault) {
  WidgetAutoclosePtr toplevel(CreateTopLevelPlatformWidget());

  // Establish default value.
  DCHECK(!toplevel->widget_delegate()->enable_arrow_key_traversal());

  View* container = toplevel->client_view();
  container->SetLayoutManager(std::make_unique<FillLayout>());
  auto* const button1 =
      container->AddChildView(std::make_unique<LabelButton>());
  auto* const button2 =
      container->AddChildView(std::make_unique<LabelButton>());
  toplevel->Show();
  button1->RequestFocus();

  ui::KeyEvent right_arrow(ui::ET_KEY_PRESSED, ui::VKEY_RIGHT, ui::EF_NONE);
  toplevel->OnKeyEvent(&right_arrow);
  EXPECT_TRUE(button1->HasFocus());
  EXPECT_FALSE(button2->HasFocus());

  ui::KeyEvent left_arrow(ui::ET_KEY_PRESSED, ui::VKEY_LEFT, ui::EF_NONE);
  toplevel->OnKeyEvent(&left_arrow);
  EXPECT_TRUE(button1->HasFocus());
  EXPECT_FALSE(button2->HasFocus());

  ui::KeyEvent up_arrow(ui::ET_KEY_PRESSED, ui::VKEY_UP, ui::EF_NONE);
  toplevel->OnKeyEvent(&up_arrow);
  EXPECT_TRUE(button1->HasFocus());
  EXPECT_FALSE(button2->HasFocus());

  ui::KeyEvent down_arrow(ui::ET_KEY_PRESSED, ui::VKEY_DOWN, ui::EF_NONE);
  toplevel->OnKeyEvent(&down_arrow);
  EXPECT_TRUE(button1->HasFocus());
  EXPECT_FALSE(button2->HasFocus());
}

// Verify that arrow keys can change focus if |enable_arrow_key_traversal| is
// set to true.
TEST_F(WidgetTest, ArrowKeyTraversalMovesFocusBetweenViews) {
  WidgetAutoclosePtr toplevel(CreateTopLevelPlatformWidget());
  toplevel->widget_delegate()->SetEnableArrowKeyTraversal(true);

  View* container = toplevel->client_view();
  container->SetLayoutManager(std::make_unique<FillLayout>());
  auto* const button1 =
      container->AddChildView(std::make_unique<LabelButton>());
  auto* const button2 =
      container->AddChildView(std::make_unique<LabelButton>());
  auto* const button3 =
      container->AddChildView(std::make_unique<LabelButton>());
  toplevel->Show();
  button1->RequestFocus();

  // Right should advance focus (similar to TAB).
  ui::KeyEvent right_arrow(ui::ET_KEY_PRESSED, ui::VKEY_RIGHT, ui::EF_NONE);
  toplevel->OnKeyEvent(&right_arrow);
  EXPECT_FALSE(button1->HasFocus());
  EXPECT_TRUE(button2->HasFocus());
  EXPECT_FALSE(button3->HasFocus());

  // Down should also advance focus.
  ui::KeyEvent down_arrow(ui::ET_KEY_PRESSED, ui::VKEY_DOWN, ui::EF_NONE);
  toplevel->OnKeyEvent(&down_arrow);
  EXPECT_FALSE(button1->HasFocus());
  EXPECT_FALSE(button2->HasFocus());
  EXPECT_TRUE(button3->HasFocus());

  // Left should reverse focus (similar to SHIFT+TAB).
  ui::KeyEvent left_arrow(ui::ET_KEY_PRESSED, ui::VKEY_LEFT, ui::EF_NONE);
  toplevel->OnKeyEvent(&left_arrow);
  EXPECT_FALSE(button1->HasFocus());
  EXPECT_TRUE(button2->HasFocus());
  EXPECT_FALSE(button3->HasFocus());

  // Up should also reverse focus.
  ui::KeyEvent up_arrow(ui::ET_KEY_PRESSED, ui::VKEY_UP, ui::EF_NONE);
  toplevel->OnKeyEvent(&up_arrow);
  EXPECT_TRUE(button1->HasFocus());
  EXPECT_FALSE(button2->HasFocus());
  EXPECT_FALSE(button3->HasFocus());

  // Test backwards wrap-around.
  ui::KeyEvent up_arrow2(ui::ET_KEY_PRESSED, ui::VKEY_UP, ui::EF_NONE);
  toplevel->OnKeyEvent(&up_arrow2);
  EXPECT_FALSE(button1->HasFocus());
  EXPECT_FALSE(button2->HasFocus());
  EXPECT_TRUE(button3->HasFocus());

  // Test forward wrap-around.
  ui::KeyEvent down_arrow2(ui::ET_KEY_PRESSED, ui::VKEY_DOWN, ui::EF_NONE);
  toplevel->OnKeyEvent(&down_arrow2);
  EXPECT_TRUE(button1->HasFocus());
  EXPECT_FALSE(button2->HasFocus());
  EXPECT_FALSE(button3->HasFocus());
}

TEST_F(WidgetTest, ArrowKeyTraversalNotInheritedByChildWidgets) {
  WidgetAutoclosePtr parent(CreateTopLevelPlatformWidget());
  Widget* child = CreateChildPlatformWidget(parent->GetNativeView());

  parent->widget_delegate()->SetEnableArrowKeyTraversal(true);

  View* container = child->GetContentsView();
  DCHECK(container);
  container->SetLayoutManager(std::make_unique<FillLayout>());
  auto* const button1 =
      container->AddChildView(std::make_unique<LabelButton>());
  auto* const button2 =
      container->AddChildView(std::make_unique<LabelButton>());
  parent->Show();
  child->Show();
  button1->RequestFocus();

  // Arrow key should not cause focus change on child since only the parent
  // Widget has |enable_arrow_key_traversal| set.
  ui::KeyEvent right_arrow(ui::ET_KEY_PRESSED, ui::VKEY_RIGHT, ui::EF_NONE);
  child->OnKeyEvent(&right_arrow);
  EXPECT_TRUE(button1->HasFocus());
  EXPECT_FALSE(button2->HasFocus());
}

TEST_F(WidgetTest, ArrowKeyTraversalMayBeExplicitlyEnabledByChildWidgets) {
  WidgetAutoclosePtr parent(CreateTopLevelPlatformWidget());
  Widget* child = CreateChildPlatformWidget(parent->GetNativeView());

  child->widget_delegate()->SetEnableArrowKeyTraversal(true);

  View* container = child->GetContentsView();
  container->SetLayoutManager(std::make_unique<FillLayout>());
  auto* const button1 =
      container->AddChildView(std::make_unique<LabelButton>());
  auto* const button2 =
      container->AddChildView(std::make_unique<LabelButton>());
  parent->Show();
  child->Show();
  button1->RequestFocus();

  // Arrow key should cause focus key on child since child has flag set, even
  // if the parent Widget does not.
  ui::KeyEvent right_arrow(ui::ET_KEY_PRESSED, ui::VKEY_RIGHT, ui::EF_NONE);
  child->OnKeyEvent(&right_arrow);
  EXPECT_FALSE(button1->HasFocus());
  EXPECT_TRUE(button2->HasFocus());
}

////////////////////////////////////////////////////////////////////////////////
// Widget::GetTopLevelWidget tests.

TEST_F(WidgetTest, GetTopLevelWidget_Native) {
  // Create a hierarchy of native widgets.
  WidgetAutoclosePtr toplevel(CreateTopLevelPlatformWidget());
  gfx::NativeView parent = toplevel->GetNativeView();
  Widget* child = CreateChildPlatformWidget(parent);

  EXPECT_EQ(toplevel.get(), toplevel->GetTopLevelWidget());
  EXPECT_EQ(toplevel.get(), child->GetTopLevelWidget());

  // |child| should be automatically destroyed with |toplevel|.
}

// Test if a focus manager and an inputmethod work without CHECK failure
// when window activation changes.
TEST_F(WidgetTest, ChangeActivation) {
  WidgetAutoclosePtr top1(CreateTopLevelPlatformWidget());
  top1->Show();
  RunPendingMessages();

  WidgetAutoclosePtr top2(CreateTopLevelPlatformWidget());
  top2->Show();
  RunPendingMessages();

  top1->Activate();
  RunPendingMessages();

  top2->Activate();
  RunPendingMessages();

  top1->Activate();
  RunPendingMessages();
}

// Tests visibility of child widgets.
TEST_F(WidgetTest, Visibility) {
#if defined(OS_MAC)
  if (base::mac::IsAtLeastOS11()) {
    GTEST_SKIP() << "Window visibility notifications aren't delivered on "
                    "macOS 11. See https://crbug.com/1114243.";
  }
#endif
  WidgetAutoclosePtr toplevel(CreateTopLevelPlatformWidget());
  gfx::NativeView parent = toplevel->GetNativeView();
  Widget* child = CreateChildPlatformWidget(parent);

  EXPECT_FALSE(toplevel->IsVisible());
  EXPECT_FALSE(child->IsVisible());

  // Showing a child with a hidden parent keeps the child hidden.
  child->Show();
  EXPECT_FALSE(toplevel->IsVisible());
  EXPECT_FALSE(child->IsVisible());

  // Showing a hidden parent with a visible child shows both.
  toplevel->Show();
  EXPECT_TRUE(toplevel->IsVisible());
  EXPECT_TRUE(child->IsVisible());

  // Hiding a parent hides both parent and child.
  toplevel->Hide();
  EXPECT_FALSE(toplevel->IsVisible());
  EXPECT_FALSE(child->IsVisible());

  // Hiding a child while the parent is hidden keeps the child hidden when the
  // parent is shown.
  child->Hide();
  toplevel->Show();
  EXPECT_TRUE(toplevel->IsVisible());
  EXPECT_FALSE(child->IsVisible());

  // |child| should be automatically destroyed with |toplevel|.
}

// Test that child widgets are positioned relative to their parent.
TEST_F(WidgetTest, ChildBoundsRelativeToParent) {
  WidgetAutoclosePtr toplevel(CreateTopLevelPlatformWidget());
  Widget* child = CreateChildPlatformWidget(toplevel->GetNativeView());

  toplevel->SetBounds(gfx::Rect(160, 100, 320, 200));
  child->SetBounds(gfx::Rect(0, 0, 320, 200));

  child->Show();
  toplevel->Show();

  gfx::Rect toplevel_bounds = toplevel->GetWindowBoundsInScreen();

  // Check the parent origin. If it was (0, 0) the test wouldn't be interesting.
  EXPECT_NE(gfx::Vector2d(0, 0), toplevel_bounds.OffsetFromOrigin());

  // The child's origin is at (0, 0), but the same size, so bounds should match.
  EXPECT_EQ(toplevel_bounds, child->GetWindowBoundsInScreen());
}

////////////////////////////////////////////////////////////////////////////////
// Widget ownership tests.
//
// Tests various permutations of Widget ownership specified in the
// InitParams::Ownership param.

// A WidgetTest that supplies a toplevel widget for NativeWidget to parent to.
class WidgetOwnershipTest : public WidgetTest {
 public:
  WidgetOwnershipTest() = default;

  WidgetOwnershipTest(const WidgetOwnershipTest&) = delete;
  WidgetOwnershipTest& operator=(const WidgetOwnershipTest&) = delete;

  ~WidgetOwnershipTest() override = default;

  void SetUp() override {
    WidgetTest::SetUp();
    desktop_widget_ = CreateTopLevelPlatformWidget();
  }

  void TearDown() override {
    desktop_widget_->CloseNow();
    WidgetTest::TearDown();
  }

 private:
  raw_ptr<Widget> desktop_widget_;
};

// A bag of state to monitor destructions.
struct OwnershipTestState {
  OwnershipTestState() = default;

  bool widget_deleted = false;
  bool native_widget_deleted = false;
};

// A Widget subclass that updates a bag of state when it is destroyed.
class OwnershipTestWidget : public Widget {
 public:
  explicit OwnershipTestWidget(OwnershipTestState* state) : state_(state) {}

  OwnershipTestWidget(const OwnershipTestWidget&) = delete;
  OwnershipTestWidget& operator=(const OwnershipTestWidget&) = delete;

  ~OwnershipTestWidget() override { state_->widget_deleted = true; }

 private:
  raw_ptr<OwnershipTestState> state_;
};

// TODO(sky): add coverage of ownership for the desktop variants.

// NativeWidget owns its Widget, part 1: NativeWidget is a platform-native
// widget.
TEST_F(WidgetOwnershipTest, Ownership_PlatformNativeWidgetOwnsWidget) {
  OwnershipTestState state;

  Widget* widget = new OwnershipTestWidget(&state);
  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_POPUP);
  params.native_widget = CreatePlatformNativeWidgetImpl(
      widget, kStubCapture, &state.native_widget_deleted);
  widget->Init(std::move(params));

  // Now destroy the native widget.
  widget->CloseNow();

  EXPECT_TRUE(state.widget_deleted);
  EXPECT_TRUE(state.native_widget_deleted);
}

// NativeWidget owns its Widget, part 2: NativeWidget is a NativeWidget.
TEST_F(WidgetOwnershipTest, Ownership_ViewsNativeWidgetOwnsWidget) {
  OwnershipTestState state;

  Widget* toplevel = CreateTopLevelPlatformWidget();

  Widget* widget = new OwnershipTestWidget(&state);
  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_POPUP);
  params.parent = toplevel->GetNativeView();
  params.native_widget = CreatePlatformNativeWidgetImpl(
      widget, kStubCapture, &state.native_widget_deleted);
  widget->Init(std::move(params));

  // Now destroy the native widget. This is achieved by closing the toplevel.
  toplevel->CloseNow();

  // The NativeWidget won't be deleted until after a return to the message loop
  // so we have to run pending messages before testing the destruction status.
  RunPendingMessages();

  EXPECT_TRUE(state.widget_deleted);
  EXPECT_TRUE(state.native_widget_deleted);
}

// NativeWidget owns its Widget, part 3: NativeWidget is a platform-native
// widget, destroyed out from under it by the OS.
TEST_F(WidgetOwnershipTest,
       Ownership_PlatformNativeWidgetOwnsWidget_NativeDestroy) {
  OwnershipTestState state;

  Widget* widget = new OwnershipTestWidget(&state);
  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_POPUP);
  params.native_widget = CreatePlatformNativeWidgetImpl(
      widget, kStubCapture, &state.native_widget_deleted);
  widget->Init(std::move(params));

  // Now simulate a destroy of the platform native widget from the OS:
  SimulateNativeDestroy(widget);

  EXPECT_TRUE(state.widget_deleted);
  EXPECT_TRUE(state.native_widget_deleted);
}

// NativeWidget owns its Widget, part 4: NativeWidget is a NativeWidget,
// destroyed by the view hierarchy that contains it.
TEST_F(WidgetOwnershipTest,
       Ownership_ViewsNativeWidgetOwnsWidget_NativeDestroy) {
  OwnershipTestState state;

  Widget* toplevel = CreateTopLevelPlatformWidget();

  Widget* widget = new OwnershipTestWidget(&state);
  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_POPUP);
  params.parent = toplevel->GetNativeView();
  params.native_widget = CreatePlatformNativeWidgetImpl(
      widget, kStubCapture, &state.native_widget_deleted);
  widget->Init(std::move(params));

  // Destroy the widget (achieved by closing the toplevel).
  toplevel->CloseNow();

  // The NativeWidget won't be deleted until after a return to the message loop
  // so we have to run pending messages before testing the destruction status.
  RunPendingMessages();

  EXPECT_TRUE(state.widget_deleted);
  EXPECT_TRUE(state.native_widget_deleted);
}

// NativeWidget owns its Widget, part 5: NativeWidget is a NativeWidget,
// we close it directly.
TEST_F(WidgetOwnershipTest, Ownership_ViewsNativeWidgetOwnsWidget_Close) {
  OwnershipTestState state;

  Widget* toplevel = CreateTopLevelPlatformWidget();

  Widget* widget = new OwnershipTestWidget(&state);
  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_POPUP);
  params.parent = toplevel->GetNativeView();
  params.native_widget = CreatePlatformNativeWidgetImpl(
      widget, kStubCapture, &state.native_widget_deleted);
  widget->Init(std::move(params));

  // Destroy the widget.
  widget->Close();
  toplevel->CloseNow();

  // The NativeWidget won't be deleted until after a return to the message loop
  // so we have to run pending messages before testing the destruction status.
  RunPendingMessages();

  EXPECT_TRUE(state.widget_deleted);
  EXPECT_TRUE(state.native_widget_deleted);
}

class WidgetOwnsNativeWidgetTest : public WidgetOwnershipTest {
 public:
  WidgetOwnsNativeWidgetTest() = default;
  ~WidgetOwnsNativeWidgetTest() override = default;

  void TearDown() override {
    EXPECT_TRUE(state_.widget_deleted);
    EXPECT_TRUE(state_.native_widget_deleted);

    WidgetOwnershipTest::TearDown();
  }

  OwnershipTestState* state() { return &state_; }

 private:
  OwnershipTestState state_;
};

// Widget owns its NativeWidget, part 1.
TEST_F(WidgetOwnsNativeWidgetTest, Ownership) {
  auto widget = std::make_unique<OwnershipTestWidget>(state());
  Widget::InitParams params = CreateParamsForTestWidget();
  params.native_widget = CreatePlatformNativeWidgetImpl(
      widget.get(), kStubCapture, &state()->native_widget_deleted);
  widget->Init(std::move(params));

  // Now delete the Widget, which should delete the NativeWidget.
  widget.reset();

  // TODO(beng): write test for this ownership scenario and the NativeWidget
  //             being deleted out from under the Widget.
}

// Widget owns its NativeWidget, part 2: destroy the parent view.
TEST_F(WidgetOwnsNativeWidgetTest, DestroyParentView) {
  Widget* toplevel = CreateTopLevelPlatformWidget();

  auto widget = std::make_unique<OwnershipTestWidget>(state());
  Widget::InitParams params = CreateParamsForTestWidget();
  params.parent = toplevel->GetNativeView();
  params.native_widget = CreatePlatformNativeWidgetImpl(
      widget.get(), kStubCapture, &state()->native_widget_deleted);
  widget->Init(std::move(params));

  // Now close the toplevel, which deletes the view hierarchy.
  toplevel->CloseNow();

  RunPendingMessages();

  // This shouldn't delete the widget because it shouldn't be deleted
  // from the native side.
  EXPECT_FALSE(state()->widget_deleted);
  EXPECT_FALSE(state()->native_widget_deleted);
}

// Widget owns its NativeWidget, part 3: has a WidgetDelegateView as contents.
TEST_F(WidgetOwnsNativeWidgetTest, WidgetDelegateView) {
  auto widget = std::make_unique<OwnershipTestWidget>(state());
  Widget::InitParams params = CreateParamsForTestWidget();
  params.native_widget = CreatePlatformNativeWidgetImpl(
      widget.get(), kStubCapture, &state()->native_widget_deleted);
  params.delegate = new WidgetDelegateView();
  widget->Init(std::move(params));

  // Allow the Widget to go out of scope. There should be no crash or
  // use-after-free.
}

////////////////////////////////////////////////////////////////////////////////
// Test to verify using various Widget methods doesn't crash when the underlying
// NativeView is destroyed.
//
using WidgetWithDestroyedNativeViewTest = ViewsTestBaseWithNativeWidgetType;

TEST_P(WidgetWithDestroyedNativeViewTest, Test) {
  // TODO(pbos): Add a version of this that tests with params that use
  // NATIVE_WIDGET_OWNS_WIDGET. A lot of these implementations look like they
  // call `native_widget_->` which should be illegal after CloseNow().
  std::unique_ptr<Widget> widget = CreateTestWidget();
  widget->Show();

  widget->native_widget_private()->CloseNow();
  widget->GetNativeView();
  widget->GetNativeWindow();
  ui::Accelerator accelerator;
  widget->GetAccelerator(0, &accelerator);
  widget->GetTopLevelWidget();
  widget->GetWindowBoundsInScreen();
  widget->GetClientAreaBoundsInScreen();
  widget->SetBounds(gfx::Rect(0, 0, 100, 80));
  widget->SetSize(gfx::Size(10, 11));
  widget->SetBoundsConstrained(gfx::Rect(0, 0, 120, 140));
  widget->SetVisibilityChangedAnimationsEnabled(false);
  widget->StackAtTop();
  widget->IsClosed();
  widget->Close();
  widget->Hide();
  widget->Activate();
  widget->Deactivate();
  widget->IsActive();
  widget->SetZOrderLevel(ui::ZOrderLevel::kNormal);
  widget->GetZOrderLevel();
  widget->Maximize();
  widget->Minimize();
  widget->Restore();
  widget->IsMaximized();
  widget->IsFullscreen();
  widget->SetOpacity(0.f);
  widget->FlashFrame(true);
  widget->IsVisible();
  widget->GetThemeProvider();
  widget->GetNativeTheme();
  widget->GetFocusManager();
  widget->SchedulePaintInRect(gfx::Rect(0, 0, 1, 2));
  widget->IsMouseEventsEnabled();
  widget->SetNativeWindowProperty("xx", &widget);
  widget->GetNativeWindowProperty("xx");
  widget->GetFocusTraversable();
  widget->GetLayer();
  widget->ReorderNativeViews();
  widget->SetCapture(widget->GetRootView());
  widget->ReleaseCapture();
  widget->HasCapture();
  widget->GetWorkAreaBoundsInScreen();
  widget->IsTranslucentWindowOpacitySupported();
}

INSTANTIATE_TEST_SUITE_P(
    PlatformWidgetWithDestroyedNativeViewTest,
    WidgetWithDestroyedNativeViewTest,
    ::testing::Values(ViewsTestBase::NativeWidgetType::kDefault,
                      ViewsTestBase::NativeWidgetType::kDesktop));

////////////////////////////////////////////////////////////////////////////////
// Widget observer tests.
//

class WidgetObserverTest : public WidgetTest, public WidgetObserver {
 public:
  WidgetObserverTest() = default;
  ~WidgetObserverTest() override = default;

  // Set a widget to Close() the next time the Widget being observed is hidden.
  void CloseOnNextHide(Widget* widget) { widget_to_close_on_hide_ = widget; }

  // Overridden from WidgetObserver:
  void OnWidgetDestroying(Widget* widget) override {
    if (active_ == widget)
      active_ = nullptr;
    if (widget_activated_ == widget)
      widget_activated_ = nullptr;
    widget_closed_ = widget;
  }

  void OnWidgetActivationChanged(Widget* widget, bool active) override {
    if (active) {
      if (widget_activated_)
        widget_activated_->Deactivate();
      widget_activated_ = widget;
      active_ = widget;
    } else {
      if (widget_activated_ == widget)
        widget_activated_ = nullptr;
      widget_deactivated_ = widget;
    }
  }

  void OnWidgetVisibilityChanged(Widget* widget, bool visible) override {
    if (visible) {
      widget_shown_ = widget;
      return;
    }
    widget_hidden_ = widget;
    if (widget_to_close_on_hide_) {
      widget_to_close_on_hide_->Close();
      widget_to_close_on_hide_ = nullptr;
    }
  }

  void OnWidgetBoundsChanged(Widget* widget,
                             const gfx::Rect& new_bounds) override {
    widget_bounds_changed_ = widget;
  }

  void reset() {
    active_ = nullptr;
    widget_closed_ = nullptr;
    widget_activated_ = nullptr;
    widget_deactivated_ = nullptr;
    widget_shown_ = nullptr;
    widget_hidden_ = nullptr;
    widget_bounds_changed_ = nullptr;
  }

  Widget* NewWidget() {
    Widget* widget = CreateTopLevelNativeWidget();
    widget->AddObserver(this);
    return widget;
  }

  const Widget* active() const { return active_; }
  const Widget* widget_closed() const { return widget_closed_; }
  const Widget* widget_activated() const { return widget_activated_; }
  const Widget* widget_deactivated() const { return widget_deactivated_; }
  const Widget* widget_shown() const { return widget_shown_; }
  const Widget* widget_hidden() const { return widget_hidden_; }
  const Widget* widget_bounds_changed() const { return widget_bounds_changed_; }

 private:
  raw_ptr<Widget> active_ = nullptr;

  raw_ptr<Widget> widget_closed_ = nullptr;
  raw_ptr<Widget> widget_activated_ = nullptr;
  raw_ptr<Widget> widget_deactivated_ = nullptr;
  raw_ptr<Widget> widget_shown_ = nullptr;
  raw_ptr<Widget> widget_hidden_ = nullptr;
  raw_ptr<Widget> widget_bounds_changed_ = nullptr;

  raw_ptr<Widget> widget_to_close_on_hide_ = nullptr;
};

// This test appears to be flaky on Mac.
#if defined(OS_MAC)
#define MAYBE_ActivationChange DISABLED_ActivationChange
#else
#define MAYBE_ActivationChange ActivationChange
#endif

TEST_F(WidgetObserverTest, MAYBE_ActivationChange) {
  WidgetAutoclosePtr toplevel1(NewWidget());
  WidgetAutoclosePtr toplevel2(NewWidget());

  toplevel1->Show();
  toplevel2->Show();
  reset();

  toplevel1->Activate();
  RunPendingMessages();
  EXPECT_EQ(toplevel1.get(), widget_activated());

  toplevel2->Activate();
  RunPendingMessages();
  EXPECT_EQ(toplevel1.get(), widget_deactivated());
  EXPECT_EQ(toplevel2.get(), widget_activated());
  EXPECT_EQ(toplevel2.get(), active());
}

namespace {

// This class simulates a focus manager that moves focus to a second widget when
// the first one is closed. It simulates a situation where a sequence of widget
// observers might try to call Widget::Close in response to a OnWidgetClosing().
class WidgetActivationForwarder : public TestWidgetObserver {
 public:
  WidgetActivationForwarder(Widget* current_active_widget,
                            Widget* widget_to_activate)
      : TestWidgetObserver(current_active_widget),
        widget_to_activate_(widget_to_activate) {}

  WidgetActivationForwarder(const WidgetActivationForwarder&) = delete;
  WidgetActivationForwarder& operator=(const WidgetActivationForwarder&) =
      delete;

  ~WidgetActivationForwarder() override = default;

 private:
  // WidgetObserver overrides:
  void OnWidgetClosing(Widget* widget) override {
    widget->OnNativeWidgetActivationChanged(false);
    widget_to_activate_->Activate();
  }
  void OnWidgetActivationChanged(Widget* widget, bool active) override {
    if (!active)
      widget->Close();
  }

  raw_ptr<Widget> widget_to_activate_;
};

// This class observes a widget and counts the number of times OnWidgetClosing
// is called.
class WidgetCloseCounter : public TestWidgetObserver {
 public:
  explicit WidgetCloseCounter(Widget* widget) : TestWidgetObserver(widget) {}

  WidgetCloseCounter(const WidgetCloseCounter&) = delete;
  WidgetCloseCounter& operator=(const WidgetCloseCounter&) = delete;

  ~WidgetCloseCounter() override = default;

  int close_count() const { return close_count_; }

 private:
  // WidgetObserver overrides:
  void OnWidgetClosing(Widget* widget) override { close_count_++; }

  int close_count_ = 0;
};

}  // namespace

// Makes sure close notifications aren't sent more than once when a Widget is
// shutting down. Test for crbug.com/714334
TEST_F(WidgetObserverTest, CloseReentrancy) {
  Widget* widget1 = CreateTopLevelPlatformWidget();
  Widget* widget2 = CreateTopLevelPlatformWidget();
  WidgetCloseCounter counter(widget1);
  WidgetActivationForwarder focus_manager(widget1, widget2);
  widget1->Close();
  EXPECT_EQ(1, counter.close_count());
  widget2->Close();
}

TEST_F(WidgetObserverTest, VisibilityChange) {
  WidgetAutoclosePtr toplevel(CreateTopLevelPlatformWidget());
  WidgetAutoclosePtr child1(NewWidget());
  WidgetAutoclosePtr child2(NewWidget());

  toplevel->Show();
  child1->Show();
  child2->Show();

  reset();

  child1->Hide();
  EXPECT_EQ(child1.get(), widget_hidden());

  child2->Hide();
  EXPECT_EQ(child2.get(), widget_hidden());

  child1->Show();
  EXPECT_EQ(child1.get(), widget_shown());

  child2->Show();
  EXPECT_EQ(child2.get(), widget_shown());
}

TEST_F(WidgetObserverTest, DestroyBubble) {
  // This test expect NativeWidgetAura, force its creation.
  ViewsDelegate::GetInstance()->set_native_widget_factory(
      ViewsDelegate::NativeWidgetFactory());

  WidgetAutoclosePtr anchor(CreateTopLevelPlatformWidget());
  anchor->Show();

  BubbleDialogDelegateView* bubble_delegate =
      new TestBubbleDialogDelegateView(anchor->client_view());
  {
    WidgetAutoclosePtr bubble_widget(
        BubbleDialogDelegateView::CreateBubble(bubble_delegate));
    bubble_widget->Show();
  }

  anchor->Hide();
}

TEST_F(WidgetObserverTest, WidgetBoundsChanged) {
  WidgetAutoclosePtr child1(NewWidget());
  WidgetAutoclosePtr child2(NewWidget());

  child1->OnNativeWidgetMove();
  EXPECT_EQ(child1.get(), widget_bounds_changed());

  child2->OnNativeWidgetMove();
  EXPECT_EQ(child2.get(), widget_bounds_changed());

  child1->OnNativeWidgetSizeChanged(gfx::Size());
  EXPECT_EQ(child1.get(), widget_bounds_changed());

  child2->OnNativeWidgetSizeChanged(gfx::Size());
  EXPECT_EQ(child2.get(), widget_bounds_changed());
}

// An extension to WidgetBoundsChanged to ensure notifications are forwarded
// by the NativeWidget implementation.
TEST_F(WidgetObserverTest, WidgetBoundsChangedNative) {
  // Don't use NewWidget(), so that the Init() flow can be observed to ensure
  // consistency across platforms.
  Widget* widget = new Widget();  // Note: owned by NativeWidget.
  widget->AddObserver(this);

  EXPECT_FALSE(widget_bounds_changed());

  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_WINDOW);

  // Use an origin within the work area since platforms (e.g. Mac) may move a
  // window into the work area when showing, triggering a bounds change.
  params.bounds = gfx::Rect(50, 50, 100, 100);

  // Init causes a bounds change, even while not showing. Note some platforms
  // cause a bounds change even when the bounds are empty. Mac does not.
  widget->Init(std::move(params));
  EXPECT_TRUE(widget_bounds_changed());
  reset();

  // Resizing while hidden, triggers a change.
  widget->SetSize(gfx::Size(160, 100));
  EXPECT_FALSE(widget->IsVisible());
  EXPECT_TRUE(widget_bounds_changed());
  reset();

  // Setting the same size does nothing.
  widget->SetSize(gfx::Size(160, 100));
  EXPECT_FALSE(widget_bounds_changed());
  reset();

  // Showing does nothing to the bounds.
  widget->Show();
  EXPECT_TRUE(widget->IsVisible());
  EXPECT_FALSE(widget_bounds_changed());
  reset();

  // Resizing while shown.
  widget->SetSize(gfx::Size(170, 100));
  EXPECT_TRUE(widget_bounds_changed());
  reset();

  // Resize to the same thing while shown does nothing.
  widget->SetSize(gfx::Size(170, 100));
  EXPECT_FALSE(widget_bounds_changed());
  reset();

  // Move, but don't change the size.
  widget->SetBounds(gfx::Rect(110, 110, 170, 100));
  EXPECT_TRUE(widget_bounds_changed());
  reset();

  // Moving to the same place does nothing.
  widget->SetBounds(gfx::Rect(110, 110, 170, 100));
  EXPECT_FALSE(widget_bounds_changed());
  reset();

  // No bounds change when closing.
  widget->CloseNow();
  EXPECT_FALSE(widget_bounds_changed());
}

namespace {

class MoveTrackingTestDesktopWidgetDelegate : public TestDesktopWidgetDelegate {
 public:
  int move_count() const { return move_count_; }

  // WidgetDelegate:
  void OnWidgetMove() override { ++move_count_; }

 private:
  int move_count_ = 0;
};

}  // namespace

class DesktopWidgetObserverTest : public WidgetObserverTest {
 public:
  DesktopWidgetObserverTest() = default;

  DesktopWidgetObserverTest(const DesktopWidgetObserverTest&) = delete;
  DesktopWidgetObserverTest& operator=(const DesktopWidgetObserverTest&) =
      delete;

  ~DesktopWidgetObserverTest() override = default;

  // WidgetObserverTest:
  void SetUp() override {
    set_native_widget_type(NativeWidgetType::kDesktop);
    WidgetObserverTest::SetUp();
  }
};

// An extension to the WidgetBoundsChangedNative test above to ensure move
// notifications propagate to the WidgetDelegate.
TEST_F(DesktopWidgetObserverTest, OnWidgetMovedWhenOriginChangesNative) {
  MoveTrackingTestDesktopWidgetDelegate delegate;
  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_WINDOW);
  delegate.InitWidget(std::move(params));
  Widget* widget = delegate.GetWidget();
  widget->Show();
  widget->SetBounds(gfx::Rect(100, 100, 300, 200));

  const int moves_during_init = delegate.move_count();

  // Resize without changing origin. No move.
  widget->SetBounds(gfx::Rect(100, 100, 310, 210));
  EXPECT_EQ(moves_during_init, delegate.move_count());

  // Move without changing size. Moves.
  widget->SetBounds(gfx::Rect(110, 110, 310, 210));
  EXPECT_EQ(moves_during_init + 1, delegate.move_count());

  // Changing both moves.
  widget->SetBounds(gfx::Rect(90, 90, 330, 230));
  EXPECT_EQ(moves_during_init + 2, delegate.move_count());

  // Just grow vertically. On Mac, this changes the AppKit origin since it is
  // from the bottom left of the screen, but there is no move as far as views is
  // concerned.
  widget->SetBounds(gfx::Rect(90, 90, 330, 240));
  // No change.
  EXPECT_EQ(moves_during_init + 2, delegate.move_count());

  // For a similar reason, move the widget down by the same amount that it grows
  // vertically. The AppKit origin does not change, but it is a move.
  widget->SetBounds(gfx::Rect(90, 100, 330, 250));
  EXPECT_EQ(moves_during_init + 3, delegate.move_count());
}

// Test correct behavior when widgets close themselves in response to visibility
// changes.
TEST_F(WidgetObserverTest, ClosingOnHiddenParent) {
#if defined(OS_MAC)
  if (base::mac::IsAtLeastOS11()) {
    GTEST_SKIP() << "Window visibility notifications aren't delivered on "
                    "macOS 11. See https://crbug.com/1114243.";
  }
#endif
  WidgetAutoclosePtr parent(NewWidget());
  Widget* child = CreateChildPlatformWidget(parent->GetNativeView());

  TestWidgetObserver child_observer(child);

  EXPECT_FALSE(parent->IsVisible());
  EXPECT_FALSE(child->IsVisible());

  // Note |child| is TYPE_CONTROL, which start shown. So no need to show the
  // child separately.
  parent->Show();
  EXPECT_TRUE(parent->IsVisible());
  EXPECT_TRUE(child->IsVisible());

  // Simulate a child widget that closes itself when the parent is hidden.
  CloseOnNextHide(child);
  EXPECT_FALSE(child_observer.widget_closed());
  parent->Hide();
  RunPendingMessages();
  EXPECT_TRUE(child_observer.widget_closed());
}

// Test behavior of NativeWidget*::GetWindowPlacement on the native desktop.
#if defined(OS_LINUX)
// On desktop-Linux cheat and use non-desktop widgets. On X11, minimize is
// asynchronous. Also (harder) showing a window doesn't activate it without
// user interaction (or extra steps only done for interactive ui tests).
// Without that, show_state remains in ui::SHOW_STATE_INACTIVE throughout.
// TODO(tapted): Find a nice way to run this with desktop widgets on Linux.
TEST_F(WidgetTest, GetWindowPlacement) {
#else
TEST_F(DesktopWidgetTest, GetWindowPlacement) {
#endif
  WidgetAutoclosePtr widget;
  widget.reset(CreateTopLevelNativeWidget());

  gfx::Rect expected_bounds(100, 110, 200, 220);
  widget->SetBounds(expected_bounds);
  widget->Show();

  // Start with something invalid to ensure it changes.
  ui::WindowShowState show_state = ui::SHOW_STATE_END;
  gfx::Rect restored_bounds;

  internal::NativeWidgetPrivate* native_widget =
      widget->native_widget_private();

  native_widget->GetWindowPlacement(&restored_bounds, &show_state);
  EXPECT_EQ(expected_bounds, restored_bounds);
#if defined(OS_LINUX) || defined(OS_CHROMEOS)
  // Non-desktop/Ash widgets start off in "default" until a Restore().
  EXPECT_EQ(ui::SHOW_STATE_DEFAULT, show_state);
  widget->Restore();
  native_widget->GetWindowPlacement(&restored_bounds, &show_state);
#endif
  EXPECT_EQ(ui::SHOW_STATE_NORMAL, show_state);

  widget->Minimize();
  native_widget->GetWindowPlacement(&restored_bounds, &show_state);
  EXPECT_EQ(ui::SHOW_STATE_MINIMIZED, show_state);
  EXPECT_EQ(expected_bounds, restored_bounds);

  widget->Restore();
  native_widget->GetWindowPlacement(&restored_bounds, &show_state);
  EXPECT_EQ(ui::SHOW_STATE_NORMAL, show_state);
  EXPECT_EQ(expected_bounds, restored_bounds);

  expected_bounds = gfx::Rect(130, 140, 230, 250);
  widget->SetBounds(expected_bounds);
  native_widget->GetWindowPlacement(&restored_bounds, &show_state);
  EXPECT_EQ(ui::SHOW_STATE_NORMAL, show_state);
  EXPECT_EQ(expected_bounds, restored_bounds);

  widget->SetFullscreen(true);
  native_widget->GetWindowPlacement(&restored_bounds, &show_state);

#if defined(OS_WIN)
  // Desktop Aura widgets on Windows currently don't update show_state when
  // going fullscreen, and report restored_bounds as the full screen size.
  // See http://crbug.com/475813.
  EXPECT_EQ(ui::SHOW_STATE_NORMAL, show_state);
#else
  EXPECT_EQ(ui::SHOW_STATE_FULLSCREEN, show_state);
  EXPECT_EQ(expected_bounds, restored_bounds);
#endif

  widget->SetFullscreen(false);
  native_widget->GetWindowPlacement(&restored_bounds, &show_state);
  EXPECT_EQ(ui::SHOW_STATE_NORMAL, show_state);
  EXPECT_EQ(expected_bounds, restored_bounds);
}

// Test that widget size constraints are properly applied immediately after
// Init(), and that SetBounds() calls are appropriately clamped.
TEST_F(DesktopWidgetTest, MinimumSizeConstraints) {
  TestDesktopWidgetDelegate delegate;
  gfx::Size minimum_size(100, 100);
  const gfx::Size smaller_size(90, 90);

  delegate.set_contents_view(new StaticSizedView(minimum_size));
  delegate.InitWidget(CreateParams(Widget::InitParams::TYPE_WINDOW));
  Widget* widget = delegate.GetWidget();

  // On desktop Linux, the Widget must be shown to ensure the window is mapped.
  // On other platforms this line is optional.
  widget->Show();

  // Sanity checks.
  EXPECT_GT(delegate.initial_bounds().width(), minimum_size.width());
  EXPECT_GT(delegate.initial_bounds().height(), minimum_size.height());
  EXPECT_EQ(delegate.initial_bounds().size(),
            widget->GetWindowBoundsInScreen().size());
  // Note: StaticSizedView doesn't currently provide a maximum size.
  EXPECT_EQ(gfx::Size(), widget->GetMaximumSize());

  if (!widget->ShouldUseNativeFrame()) {
    // The test environment may have dwm disabled on Windows. In this case,
    // CustomFrameView is used instead of the NativeFrameView, which will
    // provide a minimum size that includes frame decorations.
    minimum_size = widget->non_client_view()
                       ->GetWindowBoundsForClientBounds(gfx::Rect(minimum_size))
                       .size();
  }

  EXPECT_EQ(minimum_size, widget->GetMinimumSize());
  EXPECT_EQ(minimum_size, GetNativeWidgetMinimumContentSize(widget));

  // Trying to resize smaller than the minimum size should restrict the content
  // size to the minimum size.
  widget->SetBounds(gfx::Rect(smaller_size));
  EXPECT_EQ(minimum_size, widget->GetClientAreaBoundsInScreen().size());

  widget->SetSize(smaller_size);
  EXPECT_EQ(minimum_size, widget->GetClientAreaBoundsInScreen().size());
}

// Tests that SetBounds() and GetWindowBoundsInScreen() is symmetric when the
// widget is visible and not maximized or fullscreen.
TEST_F(WidgetTest, GetWindowBoundsInScreen) {
  // Choose test coordinates away from edges and dimensions that are "small"
  // (but not too small) to ensure the OS doesn't try to adjust them.
  const gfx::Rect kTestBounds(150, 150, 400, 300);
  const gfx::Size kTestSize(200, 180);

  {
    // First test a toplevel widget.
    WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
    widget->Show();

    EXPECT_NE(kTestSize.ToString(),
              widget->GetWindowBoundsInScreen().size().ToString());
    widget->SetSize(kTestSize);
    EXPECT_EQ(kTestSize.ToString(),
              widget->GetWindowBoundsInScreen().size().ToString());

    EXPECT_NE(kTestBounds.ToString(),
              widget->GetWindowBoundsInScreen().ToString());
    widget->SetBounds(kTestBounds);
    EXPECT_EQ(kTestBounds.ToString(),
              widget->GetWindowBoundsInScreen().ToString());

    // Changing just the size should not change the origin.
    widget->SetSize(kTestSize);
    EXPECT_EQ(kTestBounds.origin().ToString(),
              widget->GetWindowBoundsInScreen().origin().ToString());
  }

  // Same tests with a frameless window.
  WidgetAutoclosePtr widget(CreateTopLevelFramelessPlatformWidget());
  widget->Show();

  EXPECT_NE(kTestSize.ToString(),
            widget->GetWindowBoundsInScreen().size().ToString());
  widget->SetSize(kTestSize);
  EXPECT_EQ(kTestSize.ToString(),
            widget->GetWindowBoundsInScreen().size().ToString());

  EXPECT_NE(kTestBounds.ToString(),
            widget->GetWindowBoundsInScreen().ToString());
  widget->SetBounds(kTestBounds);
  EXPECT_EQ(kTestBounds.ToString(),
            widget->GetWindowBoundsInScreen().ToString());

  // For a frameless widget, the client bounds should also match.
  EXPECT_EQ(kTestBounds.ToString(),
            widget->GetClientAreaBoundsInScreen().ToString());

  // Verify origin is stable for a frameless window as well.
  widget->SetSize(kTestSize);
  EXPECT_EQ(kTestBounds.origin().ToString(),
            widget->GetWindowBoundsInScreen().origin().ToString());
}

// Chrome OS widgets need the shell to maximize/fullscreen window.
// Disable on desktop Linux because windows restore to the wrong bounds.
// See http://crbug.com/515369.
#if defined(OS_CHROMEOS) || defined(OS_LINUX)
#define MAYBE_GetRestoredBounds DISABLED_GetRestoredBounds
#else
#define MAYBE_GetRestoredBounds GetRestoredBounds
#endif

// Test that GetRestoredBounds() returns the original bounds of the window.
TEST_F(DesktopWidgetTest, MAYBE_GetRestoredBounds) {
  WidgetAutoclosePtr toplevel(CreateTopLevelNativeWidget());
  toplevel->Show();
  // Initial restored bounds have non-zero size.
  EXPECT_FALSE(toplevel->GetRestoredBounds().IsEmpty());

  const gfx::Rect bounds(100, 100, 200, 200);
  toplevel->SetBounds(bounds);
  EXPECT_EQ(bounds, toplevel->GetWindowBoundsInScreen());
  EXPECT_EQ(bounds, toplevel->GetRestoredBounds());

  toplevel->Maximize();
  RunPendingMessages();
#if defined(OS_MAC)
  // Current expectation on Mac is to do nothing on Maximize.
  EXPECT_EQ(toplevel->GetWindowBoundsInScreen(), toplevel->GetRestoredBounds());
#else
  EXPECT_NE(toplevel->GetWindowBoundsInScreen(), toplevel->GetRestoredBounds());
#endif
  EXPECT_EQ(bounds, toplevel->GetRestoredBounds());

  toplevel->Restore();
  RunPendingMessages();
  EXPECT_EQ(bounds, toplevel->GetWindowBoundsInScreen());
  EXPECT_EQ(bounds, toplevel->GetRestoredBounds());

  toplevel->SetFullscreen(true);
  RunPendingMessages();

  EXPECT_NE(toplevel->GetWindowBoundsInScreen(), toplevel->GetRestoredBounds());
  EXPECT_EQ(bounds, toplevel->GetRestoredBounds());

  toplevel->SetFullscreen(false);
  RunPendingMessages();
  EXPECT_EQ(bounds, toplevel->GetWindowBoundsInScreen());
  EXPECT_EQ(bounds, toplevel->GetRestoredBounds());
}

// The key-event propagation from Widget happens differently on aura and
// non-aura systems because of the difference in IME. So this test works only on
// aura.
TEST_F(WidgetTest, KeyboardInputEvent) {
  WidgetAutoclosePtr toplevel(CreateTopLevelPlatformWidget());
  View* container = toplevel->client_view();

  Textfield* textfield = new Textfield();
  textfield->SetText(u"some text");
  container->AddChildView(textfield);
  toplevel->Show();
  textfield->RequestFocus();

  // The press gets handled. The release doesn't have an effect.
  ui::KeyEvent backspace_p(ui::ET_KEY_PRESSED, ui::VKEY_DELETE, ui::EF_NONE);
  toplevel->OnKeyEvent(&backspace_p);
  EXPECT_TRUE(backspace_p.stopped_propagation());
  ui::KeyEvent backspace_r(ui::ET_KEY_RELEASED, ui::VKEY_DELETE, ui::EF_NONE);
  toplevel->OnKeyEvent(&backspace_r);
  EXPECT_FALSE(backspace_r.handled());
}

TEST_F(WidgetTest, BubbleControlsResetOnInit) {
  WidgetAutoclosePtr anchor(CreateTopLevelPlatformWidget());
  anchor->Show();

  {
    TestBubbleDialogDelegateView* bubble_delegate =
        new TestBubbleDialogDelegateView(anchor->client_view());
    WidgetAutoclosePtr bubble_widget(
        BubbleDialogDelegateView::CreateBubble(bubble_delegate));
    EXPECT_TRUE(bubble_delegate->reset_controls_called_);
    bubble_widget->Show();
  }

  anchor->Hide();
}

#if defined(OS_WIN)
// Test to ensure that after minimize, view width is set to zero. This is only
// the case for desktop widgets on Windows. Other platforms retain the window
// size while minimized.
TEST_F(DesktopWidgetTest, TestViewWidthAfterMinimizingWidget) {
  // Create a widget.
  std::unique_ptr<Widget> widget =
      CreateTestWidget(Widget::InitParams::TYPE_WINDOW);
  NonClientView* non_client_view = widget->non_client_view();
  non_client_view->SetFrameView(
      std::make_unique<MinimumSizeFrameView>(widget.get()));
  // Setting the frame view doesn't do a layout, so force one.
  non_client_view->Layout();
  widget->Show();
  EXPECT_NE(0, non_client_view->frame_view()->width());
  widget->Minimize();
  EXPECT_EQ(0, non_client_view->frame_view()->width());
}
#endif

// Desktop native widget Aura tests are for non Chrome OS platforms.
// This class validates whether paints are received for a visible Widget.
// It observes Widget visibility and Close() and tracks whether subsequent
// paints are expected.
class DesktopAuraTestValidPaintWidget : public Widget, public WidgetObserver {
 public:
  explicit DesktopAuraTestValidPaintWidget(Widget::InitParams init_params)
      : Widget(std::move(init_params)) {
    observation_.Observe(this);
  }

  DesktopAuraTestValidPaintWidget(const DesktopAuraTestValidPaintWidget&) =
      delete;
  DesktopAuraTestValidPaintWidget& operator=(
      const DesktopAuraTestValidPaintWidget&) = delete;

  ~DesktopAuraTestValidPaintWidget() override = default;

  bool ReadReceivedPaintAndReset() {
    return std::exchange(received_paint_, false);
  }

  bool received_paint_while_hidden() const {
    return received_paint_while_hidden_;
  }

  void WaitUntilPaint() {
    if (received_paint_)
      return;
    base::RunLoop runloop;
    quit_closure_ = runloop.QuitClosure();
    runloop.Run();
    quit_closure_.Reset();
  }

  void OnWidgetClosing(Widget* widget) override { expect_paint_ = false; }

  void OnNativeWidgetPaint(const ui::PaintContext& context) override {
    received_paint_ = true;
    EXPECT_TRUE(expect_paint_);
    if (!expect_paint_)
      received_paint_while_hidden_ = true;
    views::Widget::OnNativeWidgetPaint(context);
    if (!quit_closure_.is_null())
      std::move(quit_closure_).Run();
  }

  // WidgetObserver:
  void OnWidgetVisibilityChanged(Widget* widget, bool visible) override {
    expect_paint_ = visible;
  }

 private:
  bool received_paint_ = false;
  bool expect_paint_ = true;
  bool received_paint_while_hidden_ = false;
  base::OnceClosure quit_closure_;
  base::ScopedObservation<Widget, WidgetObserver> observation_{this};
};

class DesktopAuraPaintWidgetTest : public DesktopWidgetTest {
 public:
  std::unique_ptr<views::Widget> CreateTestWidget(
      views::Widget::InitParams::Type type =
          views::Widget::InitParams::TYPE_WINDOW_FRAMELESS) override {
    auto widget = std::make_unique<DesktopAuraTestValidPaintWidget>(
        CreateParamsForTestWidget(type));
    paint_widget_ = widget.get();

    View* contents_view =
        widget->SetContentsView(std::make_unique<ContentsView>());
    contents_view->SetFocusBehavior(View::FocusBehavior::ALWAYS);

    widget->Show();
    widget->Activate();

    return widget;
  }

  DesktopAuraTestValidPaintWidget* paint_widget() { return paint_widget_; }

 private:
  class ContentsView : public View {
    void GetAccessibleNodeData(ui::AXNodeData* node_data) override {
      node_data->SetNameExplicitlyEmpty();
    }
  };

  raw_ptr<DesktopAuraTestValidPaintWidget> paint_widget_ = nullptr;
};

TEST_F(DesktopAuraPaintWidgetTest, DesktopNativeWidgetNoPaintAfterCloseTest) {
  std::unique_ptr<Widget> widget = CreateTestWidget();
  paint_widget()->WaitUntilPaint();
  EXPECT_TRUE(paint_widget()->ReadReceivedPaintAndReset());
  widget->SchedulePaintInRect(widget->GetRestoredBounds());
  widget->Close();
  RunPendingMessages();
  EXPECT_FALSE(paint_widget()->ReadReceivedPaintAndReset());
  EXPECT_FALSE(paint_widget()->received_paint_while_hidden());
}

TEST_F(DesktopAuraPaintWidgetTest, DesktopNativeWidgetNoPaintAfterHideTest) {
  std::unique_ptr<Widget> widget = CreateTestWidget();
  paint_widget()->WaitUntilPaint();
  EXPECT_TRUE(paint_widget()->ReadReceivedPaintAndReset());
  widget->SchedulePaintInRect(widget->GetRestoredBounds());
  widget->Hide();
  RunPendingMessages();
  EXPECT_FALSE(paint_widget()->ReadReceivedPaintAndReset());
  EXPECT_FALSE(paint_widget()->received_paint_while_hidden());
  widget->Close();
}

// Test to ensure that the aura Window's visibility state is set to visible if
// the underlying widget is hidden and then shown.
TEST_F(DesktopWidgetTest, TestWindowVisibilityAfterHide) {
  // Create a widget.
  std::unique_ptr<Widget> widget =
      CreateTestWidget(Widget::InitParams::TYPE_WINDOW);
  NonClientView* non_client_view = widget->non_client_view();
  non_client_view->SetFrameView(
      std::make_unique<MinimumSizeFrameView>(widget.get()));

  widget->Show();
  EXPECT_TRUE(IsNativeWindowVisible(widget->GetNativeWindow()));
  widget->Hide();
  EXPECT_FALSE(IsNativeWindowVisible(widget->GetNativeWindow()));
  widget->Show();
  EXPECT_TRUE(IsNativeWindowVisible(widget->GetNativeWindow()));
}

// Tests that wheel events generated from scroll events are targeted to the
// views under the cursor when the focused view does not processed them.
TEST_F(WidgetTest, WheelEventsFromScrollEventTarget) {
  EventCountView* cursor_view = new EventCountView;
  cursor_view->SetBounds(60, 0, 50, 40);

  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  widget->GetRootView()->AddChildView(cursor_view);

  // Generate a scroll event on the cursor view.
  ui::ScrollEvent scroll(ui::ET_SCROLL, gfx::Point(65, 5),
                         ui::EventTimeForNow(), 0, 0, 20, 0, 20, 2);
  widget->OnScrollEvent(&scroll);

  EXPECT_EQ(1, cursor_view->GetEventCount(ui::ET_SCROLL));
  EXPECT_EQ(1, cursor_view->GetEventCount(ui::ET_MOUSEWHEEL));

  cursor_view->ResetCounts();

  ui::ScrollEvent scroll2(ui::ET_SCROLL, gfx::Point(5, 5),
                          ui::EventTimeForNow(), 0, 0, 20, 0, 20, 2);
  widget->OnScrollEvent(&scroll2);

  EXPECT_EQ(0, cursor_view->GetEventCount(ui::ET_SCROLL));
  EXPECT_EQ(0, cursor_view->GetEventCount(ui::ET_MOUSEWHEEL));
}

// Tests that if a scroll-begin gesture is not handled, then subsequent scroll
// events are not dispatched to any view.
TEST_F(WidgetTest, GestureScrollEventDispatching) {
  EventCountView* noscroll_view = new EventCountView;
  EventCountView* scroll_view = new ScrollableEventCountView;

  noscroll_view->SetBounds(0, 0, 50, 40);
  scroll_view->SetBounds(60, 0, 40, 40);

  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  widget->GetRootView()->AddChildView(noscroll_view);
  widget->GetRootView()->AddChildView(scroll_view);

  {
    ui::GestureEvent begin(
        5, 5, 0, base::TimeTicks(),
        ui::GestureEventDetails(ui::ET_GESTURE_SCROLL_BEGIN));
    widget->OnGestureEvent(&begin);
    ui::GestureEvent update(
        25, 15, 0, base::TimeTicks(),
        ui::GestureEventDetails(ui::ET_GESTURE_SCROLL_UPDATE, 20, 10));
    widget->OnGestureEvent(&update);
    ui::GestureEvent end(25, 15, 0, base::TimeTicks(),
                         ui::GestureEventDetails(ui::ET_GESTURE_SCROLL_END));
    widget->OnGestureEvent(&end);

    EXPECT_EQ(1, noscroll_view->GetEventCount(ui::ET_GESTURE_SCROLL_BEGIN));
    EXPECT_EQ(0, noscroll_view->GetEventCount(ui::ET_GESTURE_SCROLL_UPDATE));
    EXPECT_EQ(0, noscroll_view->GetEventCount(ui::ET_GESTURE_SCROLL_END));
  }

  {
    ui::GestureEvent begin(
        65, 5, 0, base::TimeTicks(),
        ui::GestureEventDetails(ui::ET_GESTURE_SCROLL_BEGIN));
    widget->OnGestureEvent(&begin);
    ui::GestureEvent update(
        85, 15, 0, base::TimeTicks(),
        ui::GestureEventDetails(ui::ET_GESTURE_SCROLL_UPDATE, 20, 10));
    widget->OnGestureEvent(&update);
    ui::GestureEvent end(85, 15, 0, base::TimeTicks(),
                         ui::GestureEventDetails(ui::ET_GESTURE_SCROLL_END));
    widget->OnGestureEvent(&end);

    EXPECT_EQ(1, scroll_view->GetEventCount(ui::ET_GESTURE_SCROLL_BEGIN));
    EXPECT_EQ(1, scroll_view->GetEventCount(ui::ET_GESTURE_SCROLL_UPDATE));
    EXPECT_EQ(1, scroll_view->GetEventCount(ui::ET_GESTURE_SCROLL_END));
  }
}

// Tests that event-handlers installed on the RootView get triggered correctly.
// TODO(tdanderson): Clean up this test as part of crbug.com/355680.
TEST_F(WidgetTest, EventHandlersOnRootView) {
  WidgetAutoclosePtr widget(CreateTopLevelNativeWidget());
  View* root_view = widget->GetRootView();

  EventCountView* view =
      root_view->AddChildView(std::make_unique<EventCountView>());
  view->SetBounds(0, 0, 20, 20);

  EventCountHandler h1;
  root_view->AddPreTargetHandler(&h1);

  EventCountHandler h2;
  root_view->AddPostTargetHandler(&h2);

  widget->SetBounds(gfx::Rect(0, 0, 100, 100));
  widget->Show();

  // Dispatch a ui::ET_SCROLL event. The event remains unhandled and should
  // bubble up the views hierarchy to be re-dispatched on the root view.
  ui::ScrollEvent scroll(ui::ET_SCROLL, gfx::Point(5, 5), ui::EventTimeForNow(),
                         0, 0, 20, 0, 20, 2);
  widget->OnScrollEvent(&scroll);
  EXPECT_EQ(2, h1.GetEventCount(ui::ET_SCROLL));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_SCROLL));
  EXPECT_EQ(2, h2.GetEventCount(ui::ET_SCROLL));

  // Unhandled scroll events are turned into wheel events and re-dispatched.
  EXPECT_EQ(1, h1.GetEventCount(ui::ET_MOUSEWHEEL));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_MOUSEWHEEL));
  EXPECT_EQ(1, h2.GetEventCount(ui::ET_MOUSEWHEEL));

  h1.ResetCounts();
  view->ResetCounts();
  h2.ResetCounts();

  // Dispatch a ui::ET_SCROLL_FLING_START event. The event remains unhandled and
  // should bubble up the views hierarchy to be re-dispatched on the root view.
  ui::ScrollEvent fling(ui::ET_SCROLL_FLING_START, gfx::Point(5, 5),
                        ui::EventTimeForNow(), 0, 0, 20, 0, 20, 2);
  widget->OnScrollEvent(&fling);
  EXPECT_EQ(2, h1.GetEventCount(ui::ET_SCROLL_FLING_START));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_SCROLL_FLING_START));
  EXPECT_EQ(2, h2.GetEventCount(ui::ET_SCROLL_FLING_START));

  // Unhandled scroll events which are not of type ui::ET_SCROLL should not
  // be turned into wheel events and re-dispatched.
  EXPECT_EQ(0, h1.GetEventCount(ui::ET_MOUSEWHEEL));
  EXPECT_EQ(0, view->GetEventCount(ui::ET_MOUSEWHEEL));
  EXPECT_EQ(0, h2.GetEventCount(ui::ET_MOUSEWHEEL));

  h1.ResetCounts();
  view->ResetCounts();
  h2.ResetCounts();

  // Change the handle mode of |view| so that events are marked as handled at
  // the target phase.
  view->set_handle_mode(EventCountView::CONSUME_EVENTS);

  // Dispatch a ui::ET_GESTURE_TAP_DOWN and a ui::ET_GESTURE_TAP_CANCEL event.
  // The events are handled at the target phase and should not reach the
  // post-target handler.
  ui::GestureEvent tap_down(5, 5, 0, ui::EventTimeForNow(),
                            ui::GestureEventDetails(ui::ET_GESTURE_TAP_DOWN));
  widget->OnGestureEvent(&tap_down);
  EXPECT_EQ(1, h1.GetEventCount(ui::ET_GESTURE_TAP_DOWN));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_GESTURE_TAP_DOWN));
  EXPECT_EQ(0, h2.GetEventCount(ui::ET_GESTURE_TAP_DOWN));

  ui::GestureEvent tap_cancel(
      5, 5, 0, ui::EventTimeForNow(),
      ui::GestureEventDetails(ui::ET_GESTURE_TAP_CANCEL));
  widget->OnGestureEvent(&tap_cancel);
  EXPECT_EQ(1, h1.GetEventCount(ui::ET_GESTURE_TAP_CANCEL));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_GESTURE_TAP_CANCEL));
  EXPECT_EQ(0, h2.GetEventCount(ui::ET_GESTURE_TAP_CANCEL));

  h1.ResetCounts();
  view->ResetCounts();
  h2.ResetCounts();

  // Dispatch a ui::ET_SCROLL event. The event is handled at the target phase
  // and should not reach the post-target handler.
  ui::ScrollEvent consumed_scroll(ui::ET_SCROLL, gfx::Point(5, 5),
                                  ui::EventTimeForNow(), 0, 0, 20, 0, 20, 2);
  widget->OnScrollEvent(&consumed_scroll);
  EXPECT_EQ(1, h1.GetEventCount(ui::ET_SCROLL));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_SCROLL));
  EXPECT_EQ(0, h2.GetEventCount(ui::ET_SCROLL));

  // Handled scroll events are not turned into wheel events and re-dispatched.
  EXPECT_EQ(0, h1.GetEventCount(ui::ET_MOUSEWHEEL));
  EXPECT_EQ(0, view->GetEventCount(ui::ET_MOUSEWHEEL));
  EXPECT_EQ(0, h2.GetEventCount(ui::ET_MOUSEWHEEL));

  root_view->RemovePreTargetHandler(&h1);
}

TEST_F(WidgetTest, SynthesizeMouseMoveEvent) {
  WidgetAutoclosePtr widget(CreateTopLevelNativeWidget());
  View* root_view = widget->GetRootView();
  widget->SetBounds(gfx::Rect(0, 0, 100, 100));

  EventCountView* v1 =
      root_view->AddChildView(std::make_unique<EventCountView>());
  v1->SetBounds(5, 5, 10, 10);
  EventCountView* v2 =
      root_view->AddChildView(std::make_unique<EventCountView>());
  v2->SetBounds(5, 15, 10, 10);

  widget->Show();

  // SynthesizeMouseMoveEvent does nothing until the mouse is entered.
  widget->SynthesizeMouseMoveEvent();
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_MOUSE_MOVED));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_MOUSE_MOVED));

  gfx::Point cursor_location(v1->GetBoundsInScreen().CenterPoint());
  auto generator =
      CreateEventGenerator(GetContext(), widget->GetNativeWindow());
  generator->MoveMouseTo(cursor_location);

  EXPECT_EQ(1, v1->GetEventCount(ui::ET_MOUSE_MOVED));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_MOUSE_MOVED));

  // SynthesizeMouseMoveEvent dispatches an mousemove event.
  widget->SynthesizeMouseMoveEvent();
  EXPECT_EQ(2, v1->GetEventCount(ui::ET_MOUSE_MOVED));

  root_view->RemoveChildViewT(v1);
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_MOUSE_MOVED));
  v2->SetBounds(5, 5, 10, 10);
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_MOUSE_MOVED));

  widget->SynthesizeMouseMoveEvent();
  EXPECT_EQ(1, v2->GetEventCount(ui::ET_MOUSE_MOVED));
}

namespace {

// ui::EventHandler which handles all mouse press events.
class MousePressEventConsumer : public ui::EventHandler {
 public:
  MousePressEventConsumer() = default;

  MousePressEventConsumer(const MousePressEventConsumer&) = delete;
  MousePressEventConsumer& operator=(const MousePressEventConsumer&) = delete;

 private:
  // ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override {
    if (event->type() == ui::ET_MOUSE_PRESSED)
      event->SetHandled();
  }
};

}  // namespace

// No touch on desktop Mac. Tracked in http://crbug.com/445520.
#if !defined(OS_MAC) || defined(USE_AURA)

// Test that mouse presses and mouse releases are dispatched normally when a
// touch is down.
TEST_F(WidgetTest, MouseEventDispatchWhileTouchIsDown) {
  Widget* widget = CreateTopLevelNativeWidget();
  widget->Show();
  widget->SetSize(gfx::Size(300, 300));

  EventCountView* event_count_view =
      widget->GetRootView()->AddChildView(std::make_unique<EventCountView>());
  event_count_view->SetBounds(0, 0, 300, 300);

  MousePressEventConsumer consumer;
  event_count_view->AddPostTargetHandler(&consumer);

  auto generator =
      CreateEventGenerator(GetContext(), widget->GetNativeWindow());
  generator->PressTouch();
  generator->ClickLeftButton();

  EXPECT_EQ(1, event_count_view->GetEventCount(ui::ET_MOUSE_PRESSED));
  EXPECT_EQ(1, event_count_view->GetEventCount(ui::ET_MOUSE_RELEASED));

  // For mus it's important we destroy the widget before the EventGenerator.
  widget->CloseNow();
}

#endif  // !defined(OS_MAC) || defined(USE_AURA)

// Tests that when there is no active capture, that a mouse press causes capture
// to be set.
TEST_F(WidgetTest, MousePressCausesCapture) {
  Widget* widget = CreateTopLevelNativeWidget();
  widget->Show();
  widget->SetSize(gfx::Size(300, 300));

  EventCountView* event_count_view =
      widget->GetRootView()->AddChildView(std::make_unique<EventCountView>());
  event_count_view->SetBounds(0, 0, 300, 300);

  // No capture has been set.
  EXPECT_EQ(
      gfx::kNullNativeView,
      internal::NativeWidgetPrivate::GetGlobalCapture(widget->GetNativeView()));

  MousePressEventConsumer consumer;
  event_count_view->AddPostTargetHandler(&consumer);
  auto generator =
      CreateEventGenerator(GetContext(), widget->GetNativeWindow());
  generator->MoveMouseTo(widget->GetClientAreaBoundsInScreen().CenterPoint());
  generator->PressLeftButton();

  EXPECT_EQ(1, event_count_view->GetEventCount(ui::ET_MOUSE_PRESSED));
  EXPECT_EQ(
      widget->GetNativeView(),
      internal::NativeWidgetPrivate::GetGlobalCapture(widget->GetNativeView()));

  // For mus it's important we destroy the widget before the EventGenerator.
  widget->CloseNow();
}

namespace {

// An EventHandler which shows a Wiget upon receiving a mouse event. The Widget
// proceeds to take capture.
class CaptureEventConsumer : public ui::EventHandler {
 public:
  explicit CaptureEventConsumer(Widget* widget)
      : event_count_view_(new EventCountView()), widget_(widget) {}

  CaptureEventConsumer(const CaptureEventConsumer&) = delete;
  CaptureEventConsumer& operator=(const CaptureEventConsumer&) = delete;

  ~CaptureEventConsumer() override { widget_->CloseNow(); }

 private:
  // ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override {
    if (event->type() == ui::ET_MOUSE_PRESSED) {
      event->SetHandled();
      widget_->Show();
      widget_->SetSize(gfx::Size(200, 200));

      event_count_view_->SetBounds(0, 0, 200, 200);
      widget_->GetRootView()->AddChildView(event_count_view_.get());
      widget_->SetCapture(event_count_view_);
    }
  }

  raw_ptr<EventCountView> event_count_view_;
  raw_ptr<Widget> widget_;
};

}  // namespace

// Tests that if explicit capture occurs during a mouse press, that implicit
// capture is not applied.
TEST_F(WidgetTest, CaptureDuringMousePressNotOverridden) {
  Widget* widget = CreateTopLevelNativeWidget();
  widget->Show();
  widget->SetSize(gfx::Size(300, 300));

  EventCountView* event_count_view =
      widget->GetRootView()->AddChildView(std::make_unique<EventCountView>());
  event_count_view->SetBounds(0, 0, 300, 300);

  EXPECT_EQ(
      gfx::kNullNativeView,
      internal::NativeWidgetPrivate::GetGlobalCapture(widget->GetNativeView()));

  Widget* widget2 = CreateTopLevelNativeWidget();
  // Gives explicit capture to |widget2|
  CaptureEventConsumer consumer(widget2);
  event_count_view->AddPostTargetHandler(&consumer);
  auto generator =
      CreateEventGenerator(GetRootWindow(widget), widget->GetNativeWindow());
  generator->MoveMouseTo(widget->GetClientAreaBoundsInScreen().CenterPoint());
  // This event should implicitly give capture to |widget|, except that
  // |consumer| will explicitly set capture on |widget2|.
  generator->PressLeftButton();

  EXPECT_EQ(1, event_count_view->GetEventCount(ui::ET_MOUSE_PRESSED));
  EXPECT_NE(
      widget->GetNativeView(),
      internal::NativeWidgetPrivate::GetGlobalCapture(widget->GetNativeView()));
  EXPECT_EQ(
      widget2->GetNativeView(),
      internal::NativeWidgetPrivate::GetGlobalCapture(widget->GetNativeView()));

  // For mus it's important we destroy the widget before the EventGenerator.
  widget->CloseNow();
}

class ClosingEventObserver : public ui::EventObserver {
 public:
  explicit ClosingEventObserver(Widget* widget) : widget_(widget) {}

  ClosingEventObserver(const ClosingEventObserver&) = delete;
  ClosingEventObserver& operator=(const ClosingEventObserver&) = delete;

  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override {
    // Guard against attempting to close the widget twice.
    if (widget_)
      widget_->CloseNow();
    widget_ = nullptr;
  }

 private:
  raw_ptr<Widget> widget_;
};

class ClosingView : public View {
 public:
  explicit ClosingView(Widget* widget) : widget_(widget) {}

  ClosingView(const ClosingView&) = delete;
  ClosingView& operator=(const ClosingView&) = delete;

  // View:
  void OnEvent(ui::Event* event) override {
    // Guard against closing twice and writing to freed memory.
    if (widget_ && event->type() == ui::ET_MOUSE_PRESSED) {
      Widget* widget = widget_;
      widget_ = nullptr;
      widget->CloseNow();
    }
  }

 private:
  raw_ptr<Widget> widget_;
};

// Ensures that when multiple objects are intercepting OS-level events, that one
// can safely close a Widget that has capture.
TEST_F(WidgetTest, DestroyedWithCaptureViaEventMonitor) {
  Widget* widget = CreateTopLevelNativeWidget();
  TestWidgetObserver observer(widget);
  widget->Show();
  widget->SetSize(gfx::Size(300, 300));

  // ClosingView and ClosingEventObserver both try to close the Widget. On Mac
  // the order that EventMonitors receive OS events is not deterministic. If the
  // one installed via SetCapture() sees it first, the event is swallowed (so
  // both need to try). Note the regression test would only fail when the
  // SetCapture() handler did _not_ swallow the event, but it still needs to try
  // to close the Widget otherwise it will be left open, which fails elsewhere.
  ClosingView* closing_view = widget->GetContentsView()->AddChildView(
      std::make_unique<ClosingView>(widget));
  widget->SetCapture(closing_view);

  ClosingEventObserver closing_event_observer(widget);
  auto monitor = EventMonitor::CreateApplicationMonitor(
      &closing_event_observer, widget->GetNativeWindow(),
      {ui::ET_MOUSE_PRESSED});

  auto generator =
      CreateEventGenerator(GetContext(), widget->GetNativeWindow());
  generator->set_target(ui::test::EventGenerator::Target::APPLICATION);

  EXPECT_FALSE(observer.widget_closed());
  generator->PressLeftButton();
  EXPECT_TRUE(observer.widget_closed());
}

TEST_F(WidgetTest, LockPaintAsActive) {
  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  widget->ShowInactive();
  EXPECT_FALSE(widget->ShouldPaintAsActive());

  // First lock causes widget to paint as active.
  auto lock = widget->LockPaintAsActive();
  EXPECT_TRUE(widget->ShouldPaintAsActive());

  // Second lock has no effect.
  auto lock2 = widget->LockPaintAsActive();
  EXPECT_TRUE(widget->ShouldPaintAsActive());

  // Have to release twice to get back to inactive state.
  lock2.reset();
  EXPECT_TRUE(widget->ShouldPaintAsActive());
  lock.reset();
  EXPECT_FALSE(widget->ShouldPaintAsActive());
}

TEST_F(WidgetTest, LockPaintAsActive_AlreadyActive) {
  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  widget->Show();
  EXPECT_TRUE(widget->ShouldPaintAsActive());

  // Lock has no effect.
  auto lock = widget->LockPaintAsActive();
  EXPECT_TRUE(widget->ShouldPaintAsActive());

  // Remove lock has no effect.
  lock.reset();
  EXPECT_TRUE(widget->ShouldPaintAsActive());
}

TEST_F(WidgetTest, LockPaintAsActive_BecomesActive) {
  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  widget->ShowInactive();
  EXPECT_FALSE(widget->ShouldPaintAsActive());

  // Lock toggles render mode.
  auto lock = widget->LockPaintAsActive();
  EXPECT_TRUE(widget->ShouldPaintAsActive());

  widget->Activate();

  // Remove lock has no effect.
  lock.reset();
  EXPECT_TRUE(widget->ShouldPaintAsActive());
}

class PaintAsActiveCallbackCounter {
 public:
  explicit PaintAsActiveCallbackCounter(Widget* widget) {
    // Subscribe to |widget|'s paint-as-active change.
    paint_as_active_subscription_ =
        widget->RegisterPaintAsActiveChangedCallback(base::BindRepeating(
            &PaintAsActiveCallbackCounter::Call, base::Unretained(this)));
  }
  void Call() { count_++; }
  int CallCount() { return count_; }

 private:
  int count_ = 0;
  base::CallbackListSubscription paint_as_active_subscription_;
};

TEST_F(WidgetTest, LockParentPaintAsActive) {
  if (!PlatformStyle::kInactiveWidgetControlsAppearDisabled)
    return;

  WidgetAutoclosePtr parent(CreateTopLevelPlatformWidget());
  WidgetAutoclosePtr child(CreateChildPlatformWidget(parent->GetNativeView()));
  WidgetAutoclosePtr grandchild(
      CreateChildPlatformWidget(child->GetNativeView()));
  WidgetAutoclosePtr other(CreateTopLevelPlatformWidget());
  child->widget_delegate()->SetCanActivate(true);
  grandchild->widget_delegate()->SetCanActivate(true);

  PaintAsActiveCallbackCounter parent_control(parent.get());
  PaintAsActiveCallbackCounter child_control(child.get());
  PaintAsActiveCallbackCounter grandchild_control(grandchild.get());
  PaintAsActiveCallbackCounter other_control(other.get());

  parent->Show();
  EXPECT_TRUE(parent->ShouldPaintAsActive());
  EXPECT_TRUE(child->ShouldPaintAsActive());
  EXPECT_TRUE(grandchild->ShouldPaintAsActive());
  EXPECT_FALSE(other->ShouldPaintAsActive());
  EXPECT_EQ(parent_control.CallCount(), 1);
  EXPECT_EQ(child_control.CallCount(), 1);
  EXPECT_EQ(grandchild_control.CallCount(), 1);
  EXPECT_EQ(other_control.CallCount(), 0);

  other->Show();
  EXPECT_FALSE(parent->ShouldPaintAsActive());
  EXPECT_FALSE(child->ShouldPaintAsActive());
  EXPECT_FALSE(grandchild->ShouldPaintAsActive());
  EXPECT_TRUE(other->ShouldPaintAsActive());
  EXPECT_EQ(parent_control.CallCount(), 2);
  EXPECT_EQ(child_control.CallCount(), 2);
  EXPECT_EQ(grandchild_control.CallCount(), 2);
  EXPECT_EQ(other_control.CallCount(), 1);

  child->Show();
  EXPECT_TRUE(parent->ShouldPaintAsActive());
  EXPECT_TRUE(child->ShouldPaintAsActive());
  EXPECT_TRUE(grandchild->ShouldPaintAsActive());
  EXPECT_FALSE(other->ShouldPaintAsActive());
  EXPECT_EQ(parent_control.CallCount(), 3);
  EXPECT_EQ(child_control.CallCount(), 3);
  EXPECT_EQ(grandchild_control.CallCount(), 3);
  EXPECT_EQ(other_control.CallCount(), 2);

  other->Show();
  EXPECT_FALSE(parent->ShouldPaintAsActive());
  EXPECT_FALSE(child->ShouldPaintAsActive());
  EXPECT_FALSE(grandchild->ShouldPaintAsActive());
  EXPECT_TRUE(other->ShouldPaintAsActive());
  EXPECT_EQ(parent_control.CallCount(), 4);
  EXPECT_EQ(child_control.CallCount(), 4);
  EXPECT_EQ(grandchild_control.CallCount(), 4);
  EXPECT_EQ(other_control.CallCount(), 3);

  grandchild->Show();
  EXPECT_TRUE(parent->ShouldPaintAsActive());
  EXPECT_TRUE(child->ShouldPaintAsActive());
  EXPECT_TRUE(grandchild->ShouldPaintAsActive());
  EXPECT_FALSE(other->ShouldPaintAsActive());
  EXPECT_EQ(parent_control.CallCount(), 5);
  EXPECT_EQ(child_control.CallCount(), 5);
  EXPECT_EQ(grandchild_control.CallCount(), 5);
  EXPECT_EQ(other_control.CallCount(), 4);
}

// Widget used to destroy itself when OnNativeWidgetDestroyed is called.
class TestNativeWidgetDestroyedWidget : public Widget {
 public:
  // Overridden from NativeWidgetDelegate:
  void OnNativeWidgetDestroyed() override;
};

void TestNativeWidgetDestroyedWidget::OnNativeWidgetDestroyed() {
  Widget::OnNativeWidgetDestroyed();
  delete this;
}

// Verifies that widget destroyed itself in OnNativeWidgetDestroyed does not
// crash in ASan.
TEST_F(DesktopWidgetTest, WidgetDestroyedItselfDoesNotCrash) {
  TestDesktopWidgetDelegate delegate(new TestNativeWidgetDestroyedWidget);
  delegate.InitWidget(CreateParamsForTestWidget());
  delegate.GetWidget()->Show();
  delegate.GetWidget()->CloseNow();
}

// Verifies WindowClosing() is invoked correctly on the delegate when a Widget
// is closed.
TEST_F(DesktopWidgetTest, SingleWindowClosing) {
  TestDesktopWidgetDelegate delegate;
  delegate.InitWidget(CreateParams(Widget::InitParams::TYPE_WINDOW));
  EXPECT_EQ(0, delegate.window_closing_count());
  delegate.GetWidget()->CloseNow();
  EXPECT_EQ(1, delegate.window_closing_count());
}

TEST_F(DesktopWidgetTest, CloseRequested_AllowsClose) {
  constexpr Widget::ClosedReason kReason = Widget::ClosedReason::kLostFocus;
  TestDesktopWidgetDelegate delegate;
  delegate.set_can_close(true);
  delegate.InitWidget(CreateParams(Widget::InitParams::TYPE_WINDOW));
  WidgetDestroyedWaiter waiter(delegate.GetWidget());

  delegate.GetWidget()->CloseWithReason(kReason);
  EXPECT_TRUE(delegate.GetWidget()->IsClosed());
  EXPECT_EQ(kReason, delegate.GetWidget()->closed_reason());
  EXPECT_EQ(kReason, delegate.last_closed_reason());

  waiter.Wait();
}

TEST_F(DesktopWidgetTest, CloseRequested_DisallowClose) {
  constexpr Widget::ClosedReason kReason = Widget::ClosedReason::kLostFocus;
  TestDesktopWidgetDelegate delegate;
  delegate.set_can_close(false);
  delegate.InitWidget(CreateParams(Widget::InitParams::TYPE_WINDOW));

  delegate.GetWidget()->CloseWithReason(kReason);
  EXPECT_FALSE(delegate.GetWidget()->IsClosed());
  EXPECT_EQ(Widget::ClosedReason::kUnspecified,
            delegate.GetWidget()->closed_reason());
  EXPECT_EQ(kReason, delegate.last_closed_reason());

  delegate.GetWidget()->CloseNow();
}

TEST_F(DesktopWidgetTest, CloseRequested_SecondCloseIgnored) {
  constexpr Widget::ClosedReason kReason1 = Widget::ClosedReason::kLostFocus;
  constexpr Widget::ClosedReason kReason2 = Widget::ClosedReason::kUnspecified;
  TestDesktopWidgetDelegate delegate;
  delegate.set_can_close(true);
  delegate.InitWidget(CreateParams(Widget::InitParams::TYPE_WINDOW));
  WidgetDestroyedWaiter waiter(delegate.GetWidget());

  // Close for the first time.
  delegate.GetWidget()->CloseWithReason(kReason1);
  EXPECT_TRUE(delegate.GetWidget()->IsClosed());
  EXPECT_EQ(kReason1, delegate.last_closed_reason());

  // Calling close again should have no effect.
  delegate.GetWidget()->CloseWithReason(kReason2);
  EXPECT_TRUE(delegate.GetWidget()->IsClosed());
  EXPECT_EQ(kReason1, delegate.last_closed_reason());

  waiter.Wait();
}

class WidgetWindowTitleTest : public DesktopWidgetTest {
 protected:
  void RunTest(bool desktop_native_widget) {
    WidgetAutoclosePtr widget(new Widget());  // Destroyed by CloseNow().
    Widget::InitParams init_params =
        CreateParams(Widget::InitParams::TYPE_WINDOW);

    if (!desktop_native_widget) {
      init_params.native_widget =
          CreatePlatformNativeWidgetImpl(widget.get(), kStubCapture, nullptr);
    }
    widget->Init(std::move(init_params));

    internal::NativeWidgetPrivate* native_widget =
        widget->native_widget_private();

    std::u16string empty;
    std::u16string s1(u"Title1");
    std::u16string s2(u"Title2");
    std::u16string s3(u"TitleLong");

    // The widget starts with no title, setting empty should not change
    // anything.
    EXPECT_FALSE(native_widget->SetWindowTitle(empty));
    // Setting the title to something non-empty should cause a change.
    EXPECT_TRUE(native_widget->SetWindowTitle(s1));
    // Setting the title to something else with the same length should cause a
    // change.
    EXPECT_TRUE(native_widget->SetWindowTitle(s2));
    // Setting the title to something else with a different length should cause
    // a change.
    EXPECT_TRUE(native_widget->SetWindowTitle(s3));
    // Setting the title to the same thing twice should not cause a change.
    EXPECT_FALSE(native_widget->SetWindowTitle(s3));
  }
};

TEST_F(WidgetWindowTitleTest, SetWindowTitleChanged_NativeWidget) {
  // Use the default NativeWidget.
  bool desktop_native_widget = false;
  RunTest(desktop_native_widget);
}

TEST_F(WidgetWindowTitleTest, SetWindowTitleChanged_DesktopNativeWidget) {
  // Override to use a DesktopNativeWidget.
  bool desktop_native_widget = true;
  RunTest(desktop_native_widget);
}

TEST_F(WidgetTest, WidgetDeleted_InOnMousePressed) {
  Widget* widget = new Widget;
  Widget::InitParams params =
      CreateParams(views::Widget::InitParams::TYPE_POPUP);
  widget->Init(std::move(params));

  widget->SetContentsView(
      std::make_unique<CloseWidgetView>(ui::ET_MOUSE_PRESSED));

  widget->SetSize(gfx::Size(100, 100));
  widget->Show();

  auto generator =
      CreateEventGenerator(GetContext(), widget->GetNativeWindow());

  WidgetDeletionObserver deletion_observer(widget);
  generator->PressLeftButton();
  if (deletion_observer.IsWidgetAlive())
    generator->ReleaseLeftButton();
  EXPECT_FALSE(deletion_observer.IsWidgetAlive());

  // Yay we did not crash!
}

// No touch on desktop Mac. Tracked in http://crbug.com/445520.
#if !defined(OS_MAC) || defined(USE_AURA)

TEST_F(WidgetTest, WidgetDeleted_InDispatchGestureEvent) {
  Widget* widget = new Widget;
  Widget::InitParams params =
      CreateParams(views::Widget::InitParams::TYPE_POPUP);
  widget->Init(std::move(params));

  widget->SetContentsView(
      std::make_unique<CloseWidgetView>(ui::ET_GESTURE_TAP_DOWN));

  widget->SetSize(gfx::Size(100, 100));
  widget->Show();

  auto generator =
      CreateEventGenerator(GetContext(), widget->GetNativeWindow());

  WidgetDeletionObserver deletion_observer(widget);
  generator->GestureTapAt(widget->GetWindowBoundsInScreen().CenterPoint());
  EXPECT_FALSE(deletion_observer.IsWidgetAlive());

  // Yay we did not crash!
}

#endif  // !defined(OS_MAC) || defined(USE_AURA)

// See description of RunGetNativeThemeFromDestructor() for details.
class GetNativeThemeFromDestructorView : public WidgetDelegateView {
 public:
  GetNativeThemeFromDestructorView() = default;

  GetNativeThemeFromDestructorView(const GetNativeThemeFromDestructorView&) =
      delete;
  GetNativeThemeFromDestructorView& operator=(
      const GetNativeThemeFromDestructorView&) = delete;

  ~GetNativeThemeFromDestructorView() override { VerifyNativeTheme(); }

 private:
  void VerifyNativeTheme() { ASSERT_TRUE(GetNativeTheme() != nullptr); }
};

// Verifies GetNativeTheme() from the destructor of a WidgetDelegateView doesn't
// crash. |is_first_run| is true if this is the first call. A return value of
// true indicates this should be run again with a value of false.
// First run uses DesktopNativeWidgetAura (if possible). Second run doesn't.
bool RunGetNativeThemeFromDestructor(Widget::InitParams params,
                                     bool is_first_run) {
  bool needs_second_run = false;
  // Destroyed by CloseNow() below.
  WidgetTest::WidgetAutoclosePtr widget(new Widget);
  // Deletes itself when the Widget is destroyed.
  params.delegate = new GetNativeThemeFromDestructorView;
  if (!is_first_run) {
    params.native_widget =
        CreatePlatformNativeWidgetImpl(widget.get(), kStubCapture, nullptr);
    needs_second_run = true;
  }
  widget->Init(std::move(params));
  return needs_second_run;
}

// See description of RunGetNativeThemeFromDestructor() for details.
TEST_F(DesktopWidgetTest, GetNativeThemeFromDestructor) {
  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_POPUP);
  if (RunGetNativeThemeFromDestructor(std::move(params), true))
    RunGetNativeThemeFromDestructor(std::move(params), false);
}

// Used by HideCloseDestroy. Allows setting a boolean when the widget is
// destroyed.
class CloseDestroysWidget : public Widget {
 public:
  CloseDestroysWidget(bool* destroyed, base::OnceClosure quit_closure)
      : destroyed_(destroyed), quit_closure_(std::move(quit_closure)) {
    DCHECK(destroyed_);
    DCHECK(quit_closure_);
  }

  CloseDestroysWidget(const CloseDestroysWidget&) = delete;
  CloseDestroysWidget& operator=(const CloseDestroysWidget&) = delete;

  ~CloseDestroysWidget() override {
    *destroyed_ = true;
    std::move(quit_closure_).Run();
  }

  void Detach() { destroyed_ = nullptr; }

 private:
  raw_ptr<bool> destroyed_;
  base::OnceClosure quit_closure_;
};

// An observer that registers that an animation has ended.
class AnimationEndObserver : public ui::ImplicitAnimationObserver {
 public:
  AnimationEndObserver() = default;

  AnimationEndObserver(const AnimationEndObserver&) = delete;
  AnimationEndObserver& operator=(const AnimationEndObserver&) = delete;

  ~AnimationEndObserver() override = default;

  bool animation_completed() const { return animation_completed_; }

  // ui::ImplicitAnimationObserver:
  void OnImplicitAnimationsCompleted() override { animation_completed_ = true; }

 private:
  bool animation_completed_ = false;
};

// An observer that registers the bounds of a widget on destruction.
class WidgetBoundsObserver : public WidgetObserver {
 public:
  WidgetBoundsObserver() = default;

  WidgetBoundsObserver(const WidgetBoundsObserver&) = delete;
  WidgetBoundsObserver& operator=(const WidgetBoundsObserver&) = delete;

  ~WidgetBoundsObserver() override = default;

  gfx::Rect bounds() { return bounds_; }

  // WidgetObserver:
  void OnWidgetDestroying(Widget* widget) override {
    EXPECT_TRUE(widget->GetNativeWindow());
    EXPECT_TRUE(Widget::GetWidgetForNativeWindow(widget->GetNativeWindow()));
    bounds_ = widget->GetWindowBoundsInScreen();
  }

 private:
  gfx::Rect bounds_;
};

// Verifies Close() results in destroying.
TEST_F(DesktopWidgetTest, CloseDestroys) {
  bool destroyed = false;
  base::RunLoop run_loop;
  CloseDestroysWidget* widget =
      new CloseDestroysWidget(&destroyed, run_loop.QuitClosure());
  Widget::InitParams params =
      CreateParams(views::Widget::InitParams::TYPE_MENU);
  params.opacity = Widget::InitParams::WindowOpacity::kOpaque;
  params.bounds = gfx::Rect(50, 50, 250, 250);
  widget->Init(std::move(params));
  widget->Show();
  widget->Hide();
  widget->Close();
  EXPECT_FALSE(destroyed);
  // Run the message loop as Close() asynchronously deletes.
  run_loop.Run();
  EXPECT_TRUE(destroyed);
  // Close() should destroy the widget. If not we'll cleanup to avoid leaks.
  if (!destroyed) {
    widget->Detach();
    widget->CloseNow();
  }
}

// Tests that killing a widget while animating it does not crash.
TEST_F(WidgetTest, CloseWidgetWhileAnimating) {
  std::unique_ptr<Widget> widget = CreateTestWidget();
  AnimationEndObserver animation_observer;
  WidgetBoundsObserver widget_observer;
  gfx::Rect bounds(100, 100, 50, 50);
  {
    // Normal animations for tests have ZERO_DURATION, make sure we are actually
    // animating the movement.
    ui::ScopedAnimationDurationScaleMode animation_scale_mode(
        ui::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
    ui::ScopedLayerAnimationSettings animation_settings(
        widget->GetLayer()->GetAnimator());
    animation_settings.AddObserver(&animation_observer);
    widget->AddObserver(&widget_observer);
    widget->Show();

    // Animate the bounds change.
    widget->SetBounds(bounds);
    widget.reset();
    EXPECT_FALSE(animation_observer.animation_completed());
  }
  EXPECT_TRUE(animation_observer.animation_completed());
  EXPECT_EQ(widget_observer.bounds(), bounds);
}

// Test Widget::CloseAllSecondaryWidgets works as expected across platforms.
// ChromeOS doesn't implement or need CloseAllSecondaryWidgets() since
// everything is under a single root window.
#if BUILDFLAG(ENABLE_DESKTOP_AURA) || defined(OS_MAC)
TEST_F(DesktopWidgetTest, CloseAllSecondaryWidgets) {
  Widget* widget1 = CreateTopLevelNativeWidget();
  Widget* widget2 = CreateTopLevelNativeWidget();
  TestWidgetObserver observer1(widget1);
  TestWidgetObserver observer2(widget2);
  widget1->Show();  // Just show the first one.
  Widget::CloseAllSecondaryWidgets();
  EXPECT_TRUE(observer1.widget_closed());
  EXPECT_TRUE(observer2.widget_closed());
}
#endif

// Test that the NativeWidget is still valid during OnNativeWidgetDestroying(),
// and properties that depend on it are valid, when closed via CloseNow().
TEST_F(DesktopWidgetTest, ValidDuringOnNativeWidgetDestroyingFromCloseNow) {
  Widget* widget = CreateTopLevelNativeWidget();
  widget->Show();
  gfx::Rect screen_rect(50, 50, 100, 100);
  widget->SetBounds(screen_rect);
  WidgetBoundsObserver observer;
  widget->AddObserver(&observer);
  widget->CloseNow();
  EXPECT_EQ(screen_rect, observer.bounds());
}

// Test that the NativeWidget is still valid during OnNativeWidgetDestroying(),
// and properties that depend on it are valid, when closed via Close().
TEST_F(DesktopWidgetTest, ValidDuringOnNativeWidgetDestroyingFromClose) {
  Widget* widget = CreateTopLevelNativeWidget();
  widget->Show();
  gfx::Rect screen_rect(50, 50, 100, 100);
  widget->SetBounds(screen_rect);
  WidgetBoundsObserver observer;
  widget->AddObserver(&observer);
  widget->Close();
  EXPECT_EQ(gfx::Rect(), observer.bounds());
  base::RunLoop().RunUntilIdle();
// Broken on Linux. See http://crbug.com/515379.
// TODO(crbug.com/1052397): Revisit the macro expression once build flag switch
// of lacros-chrome is complete.
#if !(defined(OS_LINUX) || BUILDFLAG(IS_CHROMEOS_LACROS))
  EXPECT_EQ(screen_rect, observer.bounds());
#endif
}

// Tests that we do not crash when a Widget is destroyed by going out of
// scope (as opposed to being explicitly deleted by its NativeWidget).
TEST_F(WidgetTest, NoCrashOnWidgetDelete) {
  CreateTestWidget();
}

TEST_F(WidgetTest, NoCrashOnResizeConstraintsWindowTitleOnPopup) {
  CreateTestWidget(Widget::InitParams::TYPE_POPUP)->OnSizeConstraintsChanged();
}

// Tests that we do not crash when a Widget is destroyed before it finishes
// processing of pending input events in the message loop.
TEST_F(WidgetTest, NoCrashOnWidgetDeleteWithPendingEvents) {
  std::unique_ptr<Widget> widget = CreateTestWidget();
  widget->Show();

  auto generator =
      CreateEventGenerator(GetContext(), widget->GetNativeWindow());
  generator->MoveMouseTo(10, 10);

// No touch on desktop Mac. Tracked in http://crbug.com/445520.
#if defined(OS_MAC)
  generator->ClickLeftButton();
#else
  generator->PressTouch();
#endif
  widget.reset();
}

// A view that consumes mouse-pressed event and gesture-tap-down events.
class RootViewTestView : public View {
 public:
  RootViewTestView() = default;

 private:
  bool OnMousePressed(const ui::MouseEvent& event) override { return true; }

  void OnGestureEvent(ui::GestureEvent* event) override {
    if (event->type() == ui::ET_GESTURE_TAP_DOWN)
      event->SetHandled();
  }
};

// Checks if RootView::*_handler_ fields are unset when widget is hidden.
// Fails on chromium.webkit Windows bot, see crbug.com/264872.
#if defined(OS_WIN)
#define MAYBE_DisableTestRootViewHandlersWhenHidden \
  DISABLED_TestRootViewHandlersWhenHidden
#else
#define MAYBE_DisableTestRootViewHandlersWhenHidden \
  TestRootViewHandlersWhenHidden
#endif
TEST_F(WidgetTest, MAYBE_DisableTestRootViewHandlersWhenHidden) {
  Widget* widget = CreateTopLevelNativeWidget();
  widget->SetBounds(gfx::Rect(0, 0, 300, 300));
  View* view = new RootViewTestView();
  view->SetBounds(0, 0, 300, 300);
  internal::RootView* root_view =
      static_cast<internal::RootView*>(widget->GetRootView());
  root_view->AddChildView(view);

  // Check RootView::mouse_pressed_handler_.
  widget->Show();
  EXPECT_EQ(nullptr, GetMousePressedHandler(root_view));
  gfx::Point click_location(45, 15);
  ui::MouseEvent press(ui::ET_MOUSE_PRESSED, click_location, click_location,
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  widget->OnMouseEvent(&press);
  EXPECT_EQ(view, GetMousePressedHandler(root_view));
  widget->Hide();
  EXPECT_EQ(nullptr, GetMousePressedHandler(root_view));

  // Check RootView::mouse_move_handler_.
  widget->Show();
  EXPECT_EQ(nullptr, GetMouseMoveHandler(root_view));
  gfx::Point move_location(45, 15);
  ui::MouseEvent move(ui::ET_MOUSE_MOVED, move_location, move_location,
                      ui::EventTimeForNow(), 0, 0);
  widget->OnMouseEvent(&move);
  EXPECT_EQ(view, GetMouseMoveHandler(root_view));
  widget->Hide();
  EXPECT_EQ(nullptr, GetMouseMoveHandler(root_view));

  // Check RootView::gesture_handler_.
  widget->Show();
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  ui::GestureEvent tap_down(15, 15, 0, base::TimeTicks(),
                            ui::GestureEventDetails(ui::ET_GESTURE_TAP_DOWN));
  widget->OnGestureEvent(&tap_down);
  EXPECT_EQ(view, GetGestureHandler(root_view));
  widget->Hide();
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));

  widget->Close();
}

// Convenience to make constructing a GestureEvent simpler.
class GestureEventForTest : public ui::GestureEvent {
 public:
  GestureEventForTest(ui::EventType type, int x, int y)
      : GestureEvent(x,
                     y,
                     0,
                     base::TimeTicks(),
                     ui::GestureEventDetails(type)) {}

  GestureEventForTest(ui::GestureEventDetails details, int x, int y)
      : GestureEvent(x, y, 0, base::TimeTicks(), details) {}
};

// Tests that the |gesture_handler_| member in RootView is always NULL
// after the dispatch of a ui::ET_GESTURE_END event corresponding to
// the release of the final touch point on the screen, but that
// ui::ET_GESTURE_END events corresponding to the removal of any other touch
// point do not modify |gesture_handler_|.
TEST_F(WidgetTest, GestureEndEvents) {
  Widget* widget = CreateTopLevelNativeWidget();
  widget->SetBounds(gfx::Rect(0, 0, 300, 300));
  EventCountView* view = new EventCountView();
  view->SetBounds(0, 0, 300, 300);
  internal::RootView* root_view =
      static_cast<internal::RootView*>(widget->GetRootView());
  root_view->AddChildView(view);
  widget->Show();

  // If no gesture handler is set, a ui::ET_GESTURE_END event should not set
  // the gesture handler.
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  GestureEventForTest end(ui::ET_GESTURE_END, 15, 15);
  widget->OnGestureEvent(&end);
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));

  // Change the handle mode of |view| to indicate that it would like
  // to handle all events, then send a GESTURE_TAP to set the gesture handler.
  view->set_handle_mode(EventCountView::CONSUME_EVENTS);
  GestureEventForTest tap(ui::ET_GESTURE_TAP, 15, 15);
  widget->OnGestureEvent(&tap);
  EXPECT_TRUE(tap.handled());
  EXPECT_EQ(view, GetGestureHandler(root_view));

  // The gesture handler should remain unchanged on a ui::ET_GESTURE_END
  // corresponding to a second touch point, but should be reset to NULL by a
  // ui::ET_GESTURE_END corresponding to the final touch point.
  ui::GestureEventDetails details(ui::ET_GESTURE_END);
  details.set_touch_points(2);
  GestureEventForTest end_second_touch_point(details, 15, 15);
  widget->OnGestureEvent(&end_second_touch_point);
  EXPECT_EQ(view, GetGestureHandler(root_view));

  end = GestureEventForTest(ui::ET_GESTURE_END, 15, 15);
  widget->OnGestureEvent(&end);
  EXPECT_TRUE(end.handled());
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));

  // Send a GESTURE_TAP to set the gesture handler, then change the handle
  // mode of |view| to indicate that it does not want to handle any
  // further events.
  tap = GestureEventForTest(ui::ET_GESTURE_TAP, 15, 15);
  widget->OnGestureEvent(&tap);
  EXPECT_TRUE(tap.handled());
  EXPECT_EQ(view, GetGestureHandler(root_view));
  view->set_handle_mode(EventCountView::PROPAGATE_EVENTS);

  // The gesture handler should remain unchanged on a ui::ET_GESTURE_END
  // corresponding to a second touch point, but should be reset to NULL by a
  // ui::ET_GESTURE_END corresponding to the final touch point.
  end_second_touch_point = GestureEventForTest(details, 15, 15);
  widget->OnGestureEvent(&end_second_touch_point);
  EXPECT_EQ(view, GetGestureHandler(root_view));

  end = GestureEventForTest(ui::ET_GESTURE_END, 15, 15);
  widget->OnGestureEvent(&end);
  EXPECT_FALSE(end.handled());
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));

  widget->Close();
}

// Tests that gesture events which should not be processed (because
// RootView::OnEventProcessingStarted() has marked them as handled) are not
// dispatched to any views.
TEST_F(WidgetTest, GestureEventsNotProcessed) {
  Widget* widget = CreateTopLevelNativeWidget();
  widget->SetBounds(gfx::Rect(0, 0, 300, 300));

  // Define a hierarchy of four views (coordinates are in
  // their parent coordinate space).
  // v1 (0, 0, 300, 300)
  //   v2 (0, 0, 100, 100)
  //     v3 (0, 0, 50, 50)
  //       v4(0, 0, 10, 10)
  EventCountView* v1 = new EventCountView();
  v1->SetBounds(0, 0, 300, 300);
  EventCountView* v2 = new EventCountView();
  v2->SetBounds(0, 0, 100, 100);
  EventCountView* v3 = new EventCountView();
  v3->SetBounds(0, 0, 50, 50);
  EventCountView* v4 = new EventCountView();
  v4->SetBounds(0, 0, 10, 10);
  internal::RootView* root_view =
      static_cast<internal::RootView*>(widget->GetRootView());
  root_view->AddChildView(v1);
  v1->AddChildView(v2);
  v2->AddChildView(v3);
  v3->AddChildView(v4);

  widget->Show();

  // ui::ET_GESTURE_BEGIN events should never be seen by any view, but
  // they should be marked as handled by OnEventProcessingStarted().
  GestureEventForTest begin(ui::ET_GESTURE_BEGIN, 5, 5);
  widget->OnGestureEvent(&begin);
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_GESTURE_BEGIN));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_BEGIN));
  EXPECT_EQ(0, v3->GetEventCount(ui::ET_GESTURE_BEGIN));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_BEGIN));
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  EXPECT_TRUE(begin.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  // ui::ET_GESTURE_END events should not be seen by any view when there is
  // no default gesture handler set, but they should be marked as handled by
  // OnEventProcessingStarted().
  GestureEventForTest end(ui::ET_GESTURE_END, 5, 5);
  widget->OnGestureEvent(&end);
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(0, v3->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  EXPECT_TRUE(end.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  // ui::ET_GESTURE_END events not corresponding to the release of the
  // final touch point should never be seen by any view, but they should
  // be marked as handled by OnEventProcessingStarted().
  ui::GestureEventDetails details(ui::ET_GESTURE_END);
  details.set_touch_points(2);
  GestureEventForTest end_second_touch_point(details, 5, 5);
  widget->OnGestureEvent(&end_second_touch_point);
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(0, v3->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  EXPECT_TRUE(end_second_touch_point.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  // ui::ET_GESTURE_SCROLL_UPDATE events should never be seen by any view when
  // there is no default gesture handler set, but they should be marked as
  // handled by OnEventProcessingStarted().
  GestureEventForTest scroll_update(ui::ET_GESTURE_SCROLL_UPDATE, 5, 5);
  widget->OnGestureEvent(&scroll_update);
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_GESTURE_SCROLL_UPDATE));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_SCROLL_UPDATE));
  EXPECT_EQ(0, v3->GetEventCount(ui::ET_GESTURE_SCROLL_UPDATE));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_SCROLL_UPDATE));
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  EXPECT_TRUE(scroll_update.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  // ui::ET_GESTURE_SCROLL_END events should never be seen by any view when
  // there is no default gesture handler set, but they should be marked as
  // handled by OnEventProcessingStarted().
  GestureEventForTest scroll_end(ui::ET_GESTURE_SCROLL_END, 5, 5);
  widget->OnGestureEvent(&scroll_end);
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_GESTURE_SCROLL_END));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_SCROLL_END));
  EXPECT_EQ(0, v3->GetEventCount(ui::ET_GESTURE_SCROLL_END));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_SCROLL_END));
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  EXPECT_TRUE(scroll_end.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  // ui::ET_SCROLL_FLING_START events should never be seen by any view when
  // there is no default gesture handler set, but they should be marked as
  // handled by OnEventProcessingStarted().
  GestureEventForTest scroll_fling_start(ui::ET_SCROLL_FLING_START, 5, 5);
  widget->OnGestureEvent(&scroll_fling_start);
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_SCROLL_FLING_START));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_SCROLL_FLING_START));
  EXPECT_EQ(0, v3->GetEventCount(ui::ET_SCROLL_FLING_START));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_SCROLL_FLING_START));
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  EXPECT_TRUE(scroll_fling_start.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  widget->Close();
}

// Tests that a (non-scroll) gesture event is dispatched to the correct views
// in a view hierarchy and that the default gesture handler in RootView is set
// correctly.
TEST_F(WidgetTest, GestureEventDispatch) {
  Widget* widget = CreateTopLevelNativeWidget();
  widget->SetBounds(gfx::Rect(0, 0, 300, 300));

  // Define a hierarchy of four views (coordinates are in
  // their parent coordinate space).
  // v1 (0, 0, 300, 300)
  //   v2 (0, 0, 100, 100)
  //     v3 (0, 0, 50, 50)
  //       v4(0, 0, 10, 10)
  EventCountView* v1 = new EventCountView();
  v1->SetBounds(0, 0, 300, 300);
  EventCountView* v2 = new EventCountView();
  v2->SetBounds(0, 0, 100, 100);
  EventCountView* v3 = new EventCountView();
  v3->SetBounds(0, 0, 50, 50);
  EventCountView* v4 = new EventCountView();
  v4->SetBounds(0, 0, 10, 10);
  internal::RootView* root_view =
      static_cast<internal::RootView*>(widget->GetRootView());
  root_view->AddChildView(v1);
  v1->AddChildView(v2);
  v2->AddChildView(v3);
  v3->AddChildView(v4);

  widget->Show();

  // No gesture handler is set in the root view and none of the views in the
  // view hierarchy handle a ui::ET_GESTURE_TAP event. In this case the tap
  // event should be dispatched to all views in the hierarchy, the gesture
  // handler should remain unset, and the event should remain unhandled.
  GestureEventForTest tap(ui::ET_GESTURE_TAP, 5, 5);
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  widget->OnGestureEvent(&tap);
  EXPECT_EQ(1, v1->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(1, v2->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(1, v3->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(1, v4->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  EXPECT_FALSE(tap.handled());

  // No gesture handler is set in the root view and |v1|, |v2|, and |v3| all
  // handle a ui::ET_GESTURE_TAP event. In this case the tap event should be
  // dispatched to |v4| and |v3|, the gesture handler should be set to |v3|,
  // and the event should be marked as handled.
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();
  v1->set_handle_mode(EventCountView::CONSUME_EVENTS);
  v2->set_handle_mode(EventCountView::CONSUME_EVENTS);
  v3->set_handle_mode(EventCountView::CONSUME_EVENTS);
  tap = GestureEventForTest(ui::ET_GESTURE_TAP, 5, 5);
  widget->OnGestureEvent(&tap);
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(1, v3->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(1, v4->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(v3, GetGestureHandler(root_view));
  EXPECT_TRUE(tap.handled());

  // The gesture handler is set to |v3| and all views handle all gesture event
  // types. In this case subsequent gesture events should only be dispatched to
  // |v3| and marked as handled. The gesture handler should remain as |v3|.
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();
  v4->set_handle_mode(EventCountView::CONSUME_EVENTS);
  tap = GestureEventForTest(ui::ET_GESTURE_TAP, 5, 5);
  widget->OnGestureEvent(&tap);
  EXPECT_TRUE(tap.handled());
  GestureEventForTest show_press(ui::ET_GESTURE_SHOW_PRESS, 5, 5);
  widget->OnGestureEvent(&show_press);
  tap = GestureEventForTest(ui::ET_GESTURE_TAP, 5, 5);
  widget->OnGestureEvent(&tap);
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(2, v3->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_GESTURE_SHOW_PRESS));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_SHOW_PRESS));
  EXPECT_EQ(1, v3->GetEventCount(ui::ET_GESTURE_SHOW_PRESS));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_SHOW_PRESS));
  EXPECT_TRUE(tap.handled());
  EXPECT_TRUE(show_press.handled());
  EXPECT_EQ(v3, GetGestureHandler(root_view));

  // The gesture handler is set to |v3|, but |v3| does not handle
  // ui::ET_GESTURE_TAP events. In this case a tap gesture should be dispatched
  // only to |v3|, but the event should remain unhandled. The gesture handler
  // should remain as |v3|.
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();
  v3->set_handle_mode(EventCountView::PROPAGATE_EVENTS);
  tap = GestureEventForTest(ui::ET_GESTURE_TAP, 5, 5);
  widget->OnGestureEvent(&tap);
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(1, v3->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_FALSE(tap.handled());
  EXPECT_EQ(v3, GetGestureHandler(root_view));

  widget->Close();
}

// Tests that gesture scroll events will change the default gesture handler in
// RootView if the current handler to which they are dispatched does not handle
// gesture scroll events.
TEST_F(WidgetTest, ScrollGestureEventDispatch) {
  Widget* widget = CreateTopLevelNativeWidget();
  widget->SetBounds(gfx::Rect(0, 0, 300, 300));

  // Define a hierarchy of four views (coordinates are in
  // their parent coordinate space).
  // v1 (0, 0, 300, 300)
  //   v2 (0, 0, 100, 100)
  //     v3 (0, 0, 50, 50)
  //       v4(0, 0, 10, 10)
  EventCountView* v1 = new EventCountView();
  v1->SetBounds(0, 0, 300, 300);
  EventCountView* v2 = new EventCountView();
  v2->SetBounds(0, 0, 100, 100);
  EventCountView* v3 = new EventCountView();
  v3->SetBounds(0, 0, 50, 50);
  EventCountView* v4 = new EventCountView();
  v4->SetBounds(0, 0, 10, 10);
  internal::RootView* root_view =
      static_cast<internal::RootView*>(widget->GetRootView());
  root_view->AddChildView(v1);
  v1->AddChildView(v2);
  v2->AddChildView(v3);
  v3->AddChildView(v4);

  widget->Show();

  // Change the handle mode of |v3| to indicate that it would like to handle
  // gesture events.
  v3->set_handle_mode(EventCountView::CONSUME_EVENTS);

  // When no gesture handler is set, dispatching a ui::ET_GESTURE_TAP_DOWN
  // should bubble up the views hierarchy until it reaches the first view
  // that will handle it (|v3|) and then sets the handler to |v3|.
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  GestureEventForTest tap_down(ui::ET_GESTURE_TAP_DOWN, 5, 5);
  widget->OnGestureEvent(&tap_down);
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_GESTURE_TAP_DOWN));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_TAP_DOWN));
  EXPECT_EQ(1, v3->GetEventCount(ui::ET_GESTURE_TAP_DOWN));
  EXPECT_EQ(1, v4->GetEventCount(ui::ET_GESTURE_TAP_DOWN));
  EXPECT_EQ(v3, GetGestureHandler(root_view));
  EXPECT_TRUE(tap_down.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  // A ui::ET_GESTURE_TAP_CANCEL event should be dispatched to |v3| directly.
  GestureEventForTest tap_cancel(ui::ET_GESTURE_TAP_CANCEL, 5, 5);
  widget->OnGestureEvent(&tap_cancel);
  EXPECT_EQ(0, v1->GetEventCount(ui::ET_GESTURE_TAP_CANCEL));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_TAP_CANCEL));
  EXPECT_EQ(1, v3->GetEventCount(ui::ET_GESTURE_TAP_CANCEL));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_TAP_CANCEL));
  EXPECT_EQ(v3, GetGestureHandler(root_view));
  EXPECT_TRUE(tap_cancel.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  // Change the handle mode of |v3| to indicate that it would no longer like
  // to handle events, and change the mode of |v1| to indicate that it would
  // like to handle events.
  v3->set_handle_mode(EventCountView::PROPAGATE_EVENTS);
  v1->set_handle_mode(EventCountView::CONSUME_EVENTS);

  // Dispatch a ui::ET_GESTURE_SCROLL_BEGIN event. Because the current gesture
  // handler (|v3|) does not handle scroll events, the event should bubble up
  // the views hierarchy until it reaches the first view that will handle
  // it (|v1|) and then sets the handler to |v1|.
  GestureEventForTest scroll_begin(ui::ET_GESTURE_SCROLL_BEGIN, 5, 5);
  widget->OnGestureEvent(&scroll_begin);
  EXPECT_EQ(1, v1->GetEventCount(ui::ET_GESTURE_SCROLL_BEGIN));
  EXPECT_EQ(1, v2->GetEventCount(ui::ET_GESTURE_SCROLL_BEGIN));
  EXPECT_EQ(1, v3->GetEventCount(ui::ET_GESTURE_SCROLL_BEGIN));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_SCROLL_BEGIN));
  EXPECT_EQ(v1, GetGestureHandler(root_view));
  EXPECT_TRUE(scroll_begin.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  // A ui::ET_GESTURE_SCROLL_UPDATE event should be dispatched to |v1|
  // directly.
  GestureEventForTest scroll_update(ui::ET_GESTURE_SCROLL_UPDATE, 5, 5);
  widget->OnGestureEvent(&scroll_update);
  EXPECT_EQ(1, v1->GetEventCount(ui::ET_GESTURE_SCROLL_UPDATE));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_SCROLL_UPDATE));
  EXPECT_EQ(0, v3->GetEventCount(ui::ET_GESTURE_SCROLL_UPDATE));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_SCROLL_UPDATE));
  EXPECT_EQ(v1, GetGestureHandler(root_view));
  EXPECT_TRUE(scroll_update.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  // A ui::ET_GESTURE_SCROLL_END event should be dispatched to |v1|
  // directly and should not reset the gesture handler.
  GestureEventForTest scroll_end(ui::ET_GESTURE_SCROLL_END, 5, 5);
  widget->OnGestureEvent(&scroll_end);
  EXPECT_EQ(1, v1->GetEventCount(ui::ET_GESTURE_SCROLL_END));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_SCROLL_END));
  EXPECT_EQ(0, v3->GetEventCount(ui::ET_GESTURE_SCROLL_END));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_SCROLL_END));
  EXPECT_EQ(v1, GetGestureHandler(root_view));
  EXPECT_TRUE(scroll_end.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  // A ui::ET_GESTURE_PINCH_BEGIN event (which is a non-scroll event) should
  // still be dispatched to |v1| directly.
  GestureEventForTest pinch_begin(ui::ET_GESTURE_PINCH_BEGIN, 5, 5);
  widget->OnGestureEvent(&pinch_begin);
  EXPECT_EQ(1, v1->GetEventCount(ui::ET_GESTURE_PINCH_BEGIN));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_PINCH_BEGIN));
  EXPECT_EQ(0, v3->GetEventCount(ui::ET_GESTURE_PINCH_BEGIN));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_PINCH_BEGIN));
  EXPECT_EQ(v1, GetGestureHandler(root_view));
  EXPECT_TRUE(pinch_begin.handled());
  v1->ResetCounts();
  v2->ResetCounts();
  v3->ResetCounts();
  v4->ResetCounts();

  // A ui::ET_GESTURE_END event should be dispatched to |v1| and should
  // set the gesture handler to NULL.
  GestureEventForTest end(ui::ET_GESTURE_END, 5, 5);
  widget->OnGestureEvent(&end);
  EXPECT_EQ(1, v1->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(0, v2->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(0, v3->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(0, v4->GetEventCount(ui::ET_GESTURE_END));
  EXPECT_EQ(nullptr, GetGestureHandler(root_view));
  EXPECT_TRUE(end.handled());

  widget->Close();
}

// A class used in WidgetTest.GestureEventLocationWhileBubbling to verify
// that when a gesture event bubbles up a View hierarchy, the location
// of a gesture event seen by each View is in the local coordinate space
// of that View.
class GestureLocationView : public EventCountView {
 public:
  GestureLocationView() = default;

  GestureLocationView(const GestureLocationView&) = delete;
  GestureLocationView& operator=(const GestureLocationView&) = delete;

  ~GestureLocationView() override = default;

  void set_expected_location(gfx::Point expected_location) {
    expected_location_ = expected_location;
  }

  // EventCountView:
  void OnGestureEvent(ui::GestureEvent* event) override {
    EventCountView::OnGestureEvent(event);

    // Verify that the location of |event| is in the local coordinate
    // space of |this|.
    EXPECT_EQ(expected_location_, event->location());
  }

 private:
  // The expected location of a gesture event dispatched to |this|.
  gfx::Point expected_location_;
};

// Verifies that the location of a gesture event is always in the local
// coordinate space of the View receiving the event while bubbling.
TEST_F(WidgetTest, GestureEventLocationWhileBubbling) {
  Widget* widget = CreateTopLevelNativeWidget();
  widget->SetBounds(gfx::Rect(0, 0, 300, 300));

  // Define a hierarchy of three views (coordinates shown below are in the
  // coordinate space of the root view, but the coordinates used for
  // SetBounds() are in their parent coordinate space).
  // v1 (50, 50, 150, 150)
  //   v2 (100, 70, 50, 80)
  //     v3 (120, 100, 10, 10)
  GestureLocationView* v1 = new GestureLocationView();
  v1->SetBounds(50, 50, 150, 150);
  GestureLocationView* v2 = new GestureLocationView();
  v2->SetBounds(50, 20, 50, 80);
  GestureLocationView* v3 = new GestureLocationView();
  v3->SetBounds(20, 30, 10, 10);
  internal::RootView* root_view =
      static_cast<internal::RootView*>(widget->GetRootView());
  root_view->AddChildView(v1);
  v1->AddChildView(v2);
  v2->AddChildView(v3);

  widget->Show();

  // Define a GESTURE_TAP event located at (125, 105) in root view coordinates.
  // This event is contained within all of |v1|, |v2|, and |v3|.
  gfx::Point location_in_root(125, 105);
  GestureEventForTest tap(ui::ET_GESTURE_TAP, location_in_root.x(),
                          location_in_root.y());

  // Calculate the location of the event in the local coordinate spaces
  // of each of the views.
  gfx::Point location_in_v1(ConvertPointFromWidgetToView(v1, location_in_root));
  EXPECT_EQ(gfx::Point(75, 55), location_in_v1);
  gfx::Point location_in_v2(ConvertPointFromWidgetToView(v2, location_in_root));
  EXPECT_EQ(gfx::Point(25, 35), location_in_v2);
  gfx::Point location_in_v3(ConvertPointFromWidgetToView(v3, location_in_root));
  EXPECT_EQ(gfx::Point(5, 5), location_in_v3);

  // Dispatch the event. When each view receives the event, its location should
  // be in the local coordinate space of that view (see the check made by
  // GestureLocationView). After dispatch is complete the event's location
  // should be in the root coordinate space.
  v1->set_expected_location(location_in_v1);
  v2->set_expected_location(location_in_v2);
  v3->set_expected_location(location_in_v3);
  widget->OnGestureEvent(&tap);
  EXPECT_EQ(location_in_root, tap.location());

  // Verify that each view did in fact see the event.
  EventCountView* view1 = v1;
  EventCountView* view2 = v2;
  EventCountView* view3 = v3;
  EXPECT_EQ(1, view1->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(1, view2->GetEventCount(ui::ET_GESTURE_TAP));
  EXPECT_EQ(1, view3->GetEventCount(ui::ET_GESTURE_TAP));

  widget->Close();
}

// Test the result of Widget::GetAllChildWidgets().
TEST_F(WidgetTest, GetAllChildWidgets) {
  // Create the following widget hierarchy:
  //
  // toplevel
  // +-- w1
  //     +-- w11
  // +-- w2
  //     +-- w21
  //     +-- w22
  WidgetAutoclosePtr toplevel(CreateTopLevelPlatformWidget());
  Widget* w1 = CreateChildPlatformWidget(toplevel->GetNativeView());
  Widget* w11 = CreateChildPlatformWidget(w1->GetNativeView());
  Widget* w2 = CreateChildPlatformWidget(toplevel->GetNativeView());
  Widget* w21 = CreateChildPlatformWidget(w2->GetNativeView());
  Widget* w22 = CreateChildPlatformWidget(w2->GetNativeView());

  std::set<Widget*> expected;
  expected.insert(toplevel.get());
  expected.insert(w1);
  expected.insert(w11);
  expected.insert(w2);
  expected.insert(w21);
  expected.insert(w22);

  std::set<Widget*> child_widgets;
  Widget::GetAllChildWidgets(toplevel->GetNativeView(), &child_widgets);

  EXPECT_EQ(expected.size(), child_widgets.size());
  EXPECT_TRUE(
      std::equal(expected.begin(), expected.end(), child_widgets.begin()));

  // Check GetAllOwnedWidgets(). On Aura, this includes "transient" children.
  // Otherwise (on all platforms), it should be the same as GetAllChildWidgets()
  // except the root Widget is not included.
  EXPECT_EQ(1u, expected.erase(toplevel.get()));

  std::set<Widget*> owned_widgets;
  Widget::GetAllOwnedWidgets(toplevel->GetNativeView(), &owned_widgets);

  EXPECT_EQ(expected.size(), owned_widgets.size());
  EXPECT_TRUE(
      std::equal(expected.begin(), expected.end(), owned_widgets.begin()));
}

// Used by DestroyChildWidgetsInOrder. On destruction adds the supplied name to
// a vector.
class DestroyedTrackingView : public View {
 public:
  DestroyedTrackingView(const std::string& name,
                        std::vector<std::string>* add_to)
      : name_(name), add_to_(add_to) {}

  DestroyedTrackingView(const DestroyedTrackingView&) = delete;
  DestroyedTrackingView& operator=(const DestroyedTrackingView&) = delete;

  ~DestroyedTrackingView() override { add_to_->push_back(name_); }

 private:
  const std::string name_;
  raw_ptr<std::vector<std::string>> add_to_;
};

class WidgetChildDestructionTest : public DesktopWidgetTest {
 public:
  WidgetChildDestructionTest() = default;

  WidgetChildDestructionTest(const WidgetChildDestructionTest&) = delete;
  WidgetChildDestructionTest& operator=(const WidgetChildDestructionTest&) =
      delete;

  // Creates a top level and a child, destroys the child and verifies the views
  // of the child are destroyed before the views of the parent.
  void RunDestroyChildWidgetsTest(bool top_level_has_desktop_native_widget_aura,
                                  bool child_has_desktop_native_widget_aura) {
    // When a View is destroyed its name is added here.
    std::vector<std::string> destroyed;

    Widget* top_level = new Widget;
    Widget::InitParams params =
        CreateParams(views::Widget::InitParams::TYPE_WINDOW);
    if (!top_level_has_desktop_native_widget_aura) {
      params.native_widget =
          CreatePlatformNativeWidgetImpl(top_level, kStubCapture, nullptr);
    }
    top_level->Init(std::move(params));
    top_level->GetRootView()->AddChildView(
        new DestroyedTrackingView("parent", &destroyed));
    top_level->Show();

    Widget* child = new Widget;
    Widget::InitParams child_params =
        CreateParams(views::Widget::InitParams::TYPE_POPUP);
    child_params.parent = top_level->GetNativeView();
    if (!child_has_desktop_native_widget_aura) {
      child_params.native_widget =
          CreatePlatformNativeWidgetImpl(child, kStubCapture, nullptr);
    }
    child->Init(std::move(child_params));
    child->GetRootView()->AddChildView(
        new DestroyedTrackingView("child", &destroyed));
    child->Show();

    // Should trigger destruction of the child too.
    top_level->native_widget_private()->CloseNow();

    // Child should be destroyed first.
    ASSERT_EQ(2u, destroyed.size());
    EXPECT_EQ("child", destroyed[0]);
    EXPECT_EQ("parent", destroyed[1]);
  }
};

// See description of RunDestroyChildWidgetsTest(). Parent uses
// DesktopNativeWidgetAura.
TEST_F(WidgetChildDestructionTest,
       DestroyChildWidgetsInOrderWithDesktopNativeWidget) {
  RunDestroyChildWidgetsTest(true, false);
}

// See description of RunDestroyChildWidgetsTest(). Both parent and child use
// DesktopNativeWidgetAura.
TEST_F(WidgetChildDestructionTest,
       DestroyChildWidgetsInOrderWithDesktopNativeWidgetForBoth) {
  RunDestroyChildWidgetsTest(true, true);
}

// See description of RunDestroyChildWidgetsTest().
TEST_F(WidgetChildDestructionTest, DestroyChildWidgetsInOrder) {
  RunDestroyChildWidgetsTest(false, false);
}

// Verifies nativeview visbility matches that of Widget visibility when
// SetFullscreen is invoked.
TEST_F(WidgetTest, FullscreenStatePropagated) {
  std::unique_ptr<Widget> top_level_widget = CreateTestWidget();
  top_level_widget->SetFullscreen(true);
  EXPECT_EQ(top_level_widget->IsVisible(),
            IsNativeWindowVisible(top_level_widget->GetNativeWindow()));
}

// Verifies nativeview visbility matches that of Widget visibility when
// SetFullscreen is invoked, for a widget provided with a desktop widget.
TEST_F(DesktopWidgetTest, FullscreenStatePropagated_DesktopWidget) {
  std::unique_ptr<Widget> top_level_widget = CreateTestWidget();
  top_level_widget->SetFullscreen(true);
  EXPECT_EQ(top_level_widget->IsVisible(),
            IsNativeWindowVisible(top_level_widget->GetNativeWindow()));
}

// Used to delete the widget when the supplied bounds changes.
class DestroyingWidgetBoundsObserver : public WidgetObserver {
 public:
  explicit DestroyingWidgetBoundsObserver(std::unique_ptr<Widget> widget)
      : widget_(std::move(widget)) {
    widget_->AddObserver(this);
  }

  // There are no assertions here as not all platforms call
  // OnWidgetBoundsChanged() when going fullscreen.
  ~DestroyingWidgetBoundsObserver() override = default;

  // WidgetObserver:
  void OnWidgetBoundsChanged(Widget* widget,
                             const gfx::Rect& new_bounds) override {
    widget_->RemoveObserver(this);
    widget_.reset();
  }

 private:
  std::unique_ptr<Widget> widget_;
};

// Deletes a Widget when the bounds change as part of toggling fullscreen.
// This is a regression test for https://crbug.com/1197436 .
TEST_F(DesktopWidgetTest, DeleteInSetFullscreen) {
  std::unique_ptr<Widget> widget = std::make_unique<Widget>();
  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_WINDOW);
  params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  widget->Init(std::move(params));
  Widget* w = widget.get();
  DestroyingWidgetBoundsObserver destroyer(std::move(widget));
  w->SetFullscreen(true);
}

namespace {

class FullscreenAwareFrame : public views::NonClientFrameView {
 public:
  explicit FullscreenAwareFrame(views::Widget* widget)
      : widget_(widget), fullscreen_layout_called_(false) {}

  FullscreenAwareFrame(const FullscreenAwareFrame&) = delete;
  FullscreenAwareFrame& operator=(const FullscreenAwareFrame&) = delete;

  ~FullscreenAwareFrame() override = default;

  // views::NonClientFrameView overrides:
  gfx::Rect GetBoundsForClientView() const override { return gfx::Rect(); }
  gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const override {
    return gfx::Rect();
  }
  int NonClientHitTest(const gfx::Point& point) override { return HTNOWHERE; }
  void GetWindowMask(const gfx::Size& size, SkPath* window_mask) override {}
  void ResetWindowControls() override {}
  void UpdateWindowIcon() override {}
  void UpdateWindowTitle() override {}
  void SizeConstraintsChanged() override {}

  // views::View overrides:
  void Layout() override {
    if (widget_->IsFullscreen())
      fullscreen_layout_called_ = true;
  }

  bool fullscreen_layout_called() const { return fullscreen_layout_called_; }

 private:
  raw_ptr<views::Widget> widget_;
  bool fullscreen_layout_called_;
};

}  // namespace

// Tests that frame Layout is called when a widget goes fullscreen without
// changing its size or title.
TEST_F(WidgetTest, FullscreenFrameLayout) {
  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  auto frame_view = std::make_unique<FullscreenAwareFrame>(widget.get());
  FullscreenAwareFrame* frame = frame_view.get();
  widget->non_client_view()->SetFrameView(std::move(frame_view));

  widget->Maximize();
  RunPendingMessages();

  EXPECT_FALSE(frame->fullscreen_layout_called());
  widget->SetFullscreen(true);
  widget->Show();
  EXPECT_TRUE(ViewTestApi(frame).needs_layout());
  widget->LayoutRootViewIfNecessary();
  RunPendingMessages();

  EXPECT_TRUE(frame->fullscreen_layout_called());
}

namespace {

// Trivial WidgetObserverTest that invokes Widget::IsActive() from
// OnWindowDestroying.
class IsActiveFromDestroyObserver : public WidgetObserver {
 public:
  IsActiveFromDestroyObserver() = default;

  IsActiveFromDestroyObserver(const IsActiveFromDestroyObserver&) = delete;
  IsActiveFromDestroyObserver& operator=(const IsActiveFromDestroyObserver&) =
      delete;

  ~IsActiveFromDestroyObserver() override = default;
  void OnWidgetDestroying(Widget* widget) override { widget->IsActive(); }
};

}  // namespace

class ChildDesktopWidgetTest : public DesktopWidgetTest {
 public:
  Widget::InitParams CreateParams(Widget::InitParams::Type type) override {
    Widget::InitParams params = DesktopWidgetTest::CreateParams(type);
    if (context_)
      params.context = context_;
    return params;
  }

  std::unique_ptr<Widget> CreateChildWidget(gfx::NativeWindow context) {
    context_ = context;
    return CreateTestWidget();
  }

 private:
  gfx::NativeWindow context_ = nullptr;
};

// Verifies Widget::IsActive() invoked from
// WidgetObserver::OnWidgetDestroying() in a child widget doesn't crash.
TEST_F(ChildDesktopWidgetTest, IsActiveFromDestroy) {
  // Create two widgets, one a child of the other.
  IsActiveFromDestroyObserver observer;
  std::unique_ptr<Widget> parent_widget = CreateTestWidget();
  parent_widget->Show();

  std::unique_ptr<Widget> child_widget =
      CreateChildWidget(parent_widget->GetNativeWindow());
  child_widget->AddObserver(&observer);
  child_widget->Show();

  parent_widget->CloseNow();
}

// Tests that events propagate through from the dispatcher with the correct
// event type, and that the different platforms behave the same.
TEST_F(WidgetTest, MouseEventTypesViaGenerator) {
  WidgetAutoclosePtr widget(CreateTopLevelFramelessPlatformWidget());
  EventCountView* view =
      widget->GetRootView()->AddChildView(std::make_unique<EventCountView>());
  view->set_handle_mode(EventCountView::CONSUME_EVENTS);
  view->SetBounds(10, 10, 50, 40);

  widget->SetBounds(gfx::Rect(0, 0, 100, 80));
  widget->Show();

  auto generator =
      CreateEventGenerator(GetContext(), widget->GetNativeWindow());
  const gfx::Point view_center_point = view->GetBoundsInScreen().CenterPoint();
  generator->set_current_screen_location(view_center_point);

  generator->ClickLeftButton();
  EXPECT_EQ(1, view->GetEventCount(ui::ET_MOUSE_PRESSED));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_MOUSE_RELEASED));
  EXPECT_EQ(ui::EF_LEFT_MOUSE_BUTTON, view->last_flags());

  generator->PressRightButton();
  EXPECT_EQ(2, view->GetEventCount(ui::ET_MOUSE_PRESSED));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_MOUSE_RELEASED));
  EXPECT_EQ(ui::EF_RIGHT_MOUSE_BUTTON, view->last_flags());

  generator->ReleaseRightButton();
  EXPECT_EQ(2, view->GetEventCount(ui::ET_MOUSE_PRESSED));
  EXPECT_EQ(2, view->GetEventCount(ui::ET_MOUSE_RELEASED));
  EXPECT_EQ(ui::EF_RIGHT_MOUSE_BUTTON, view->last_flags());

  // Test mouse move events.
  EXPECT_EQ(0, view->GetEventCount(ui::ET_MOUSE_MOVED));
  EXPECT_EQ(0, view->GetEventCount(ui::ET_MOUSE_ENTERED));

  // Move the mouse a displacement of (10, 10).
  generator->MoveMouseTo(view_center_point + gfx::Vector2d(10, 10));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_MOUSE_MOVED));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_MOUSE_ENTERED));
  EXPECT_EQ(ui::EF_NONE, view->last_flags());

  // Move it again - entered count shouldn't change.
  generator->MoveMouseTo(view_center_point + gfx::Vector2d(11, 11));
  EXPECT_EQ(2, view->GetEventCount(ui::ET_MOUSE_MOVED));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_MOUSE_ENTERED));
  EXPECT_EQ(0, view->GetEventCount(ui::ET_MOUSE_EXITED));

  // Move it off the view.
  const gfx::Point out_of_bounds_point =
      view->GetBoundsInScreen().bottom_right() + gfx::Vector2d(10, 10);
  generator->MoveMouseTo(out_of_bounds_point);
  EXPECT_EQ(2, view->GetEventCount(ui::ET_MOUSE_MOVED));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_MOUSE_ENTERED));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_MOUSE_EXITED));

  // Move it back on.
  generator->MoveMouseTo(view_center_point);
  EXPECT_EQ(3, view->GetEventCount(ui::ET_MOUSE_MOVED));
  EXPECT_EQ(2, view->GetEventCount(ui::ET_MOUSE_ENTERED));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_MOUSE_EXITED));

  // Drargging. Cover HasCapture() and NativeWidgetPrivate::IsMouseButtonDown().
  generator->DragMouseTo(out_of_bounds_point);
  EXPECT_EQ(3, view->GetEventCount(ui::ET_MOUSE_PRESSED));
  EXPECT_EQ(3, view->GetEventCount(ui::ET_MOUSE_RELEASED));
  EXPECT_EQ(1, view->GetEventCount(ui::ET_MOUSE_DRAGGED));
  EXPECT_EQ(ui::EF_LEFT_MOUSE_BUTTON, view->last_flags());
}

// Tests that the root view is correctly set up for Widget types that do not
// require a non-client view, before any other views are added to the widget.
// That is, before Widget::ReorderNativeViews() is called which, if called with
// a root view not set, could cause the root view to get resized to the widget.
TEST_F(WidgetTest, NonClientWindowValidAfterInit) {
  WidgetAutoclosePtr widget(CreateTopLevelFramelessPlatformWidget());
  View* root_view = widget->GetRootView();

  // Size the root view to exceed the widget bounds.
  const gfx::Rect test_rect(0, 0, 500, 500);
  root_view->SetBoundsRect(test_rect);

  EXPECT_NE(test_rect.size(), widget->GetWindowBoundsInScreen().size());

  EXPECT_EQ(test_rect, root_view->bounds());
  widget->ReorderNativeViews();
  EXPECT_EQ(test_rect, root_view->bounds());
}

#if defined(OS_WIN)
// Provides functionality to subclass a window and keep track of messages
// received.
class SubclassWindowHelper {
 public:
  explicit SubclassWindowHelper(HWND window)
      : window_(window), message_to_destroy_on_(0) {
    EXPECT_EQ(instance_, nullptr);
    instance_ = this;
    EXPECT_TRUE(Subclass());
  }

  SubclassWindowHelper(const SubclassWindowHelper&) = delete;
  SubclassWindowHelper& operator=(const SubclassWindowHelper&) = delete;

  ~SubclassWindowHelper() {
    Unsubclass();
    instance_ = nullptr;
  }

  // Returns true if the |message| passed in was received.
  bool received_message(unsigned int message) {
    return (messages_.find(message) != messages_.end());
  }

  void Clear() { messages_.clear(); }

  void set_message_to_destroy_on(unsigned int message) {
    message_to_destroy_on_ = message;
  }

 private:
  bool Subclass() {
    old_proc_ = reinterpret_cast<WNDPROC>(::SetWindowLongPtr(
        window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
    return old_proc_ != nullptr;
  }

  void Unsubclass() {
    ::SetWindowLongPtr(window_, GWLP_WNDPROC,
                       reinterpret_cast<LONG_PTR>(old_proc_));
  }

  static LRESULT CALLBACK WndProc(HWND window,
                                  unsigned int message,
                                  WPARAM w_param,
                                  LPARAM l_param) {
    EXPECT_NE(instance_, nullptr);
    EXPECT_EQ(window, instance_->window_);

    // Keep track of messags received for this window.
    instance_->messages_.insert(message);

    LRESULT ret = ::CallWindowProc(instance_->old_proc_, window, message,
                                   w_param, l_param);
    if (message == instance_->message_to_destroy_on_) {
      instance_->Unsubclass();
      ::DestroyWindow(window);
    }
    return ret;
  }

  WNDPROC old_proc_;
  HWND window_;
  static SubclassWindowHelper* instance_;
  std::set<unsigned int> messages_;
  unsigned int message_to_destroy_on_;
};

SubclassWindowHelper* SubclassWindowHelper::instance_ = nullptr;

// This test validates whether the WM_SYSCOMMAND message for SC_MOVE is
// received when we post a WM_NCLBUTTONDOWN message for the caption in the
// following scenarios:-
// 1. Posting a WM_NCMOUSEMOVE message for a different location.
// 2. Posting a WM_NCMOUSEMOVE message with a different hittest code.
// 3. Posting a WM_MOUSEMOVE message.
// Disabled because of flaky timeouts: http://crbug.com/592742
TEST_F(DesktopWidgetTest,
       DISABLED_SysCommandMoveOnNCLButtonDownOnCaptionAndMoveTest) {
  std::unique_ptr<Widget> widget =
      CreateTestWidget(Widget::InitParams::TYPE_WINDOW);
  widget->Show();
  ::SetCursorPos(500, 500);

  HWND window = widget->GetNativeWindow()->GetHost()->GetAcceleratedWidget();

  SubclassWindowHelper subclass_helper(window);

  // Posting just a WM_NCLBUTTONDOWN message should not result in a
  // WM_SYSCOMMAND
  ::PostMessage(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(100, 100));
  RunPendingMessages();
  EXPECT_TRUE(subclass_helper.received_message(WM_NCLBUTTONDOWN));
  EXPECT_FALSE(subclass_helper.received_message(WM_SYSCOMMAND));

  subclass_helper.Clear();
  // Posting a WM_NCLBUTTONDOWN message followed by a WM_NCMOUSEMOVE at the
  // same location should not result in a WM_SYSCOMMAND message.
  ::PostMessage(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(100, 100));
  ::PostMessage(window, WM_NCMOUSEMOVE, HTCAPTION, MAKELPARAM(100, 100));
  RunPendingMessages();

  EXPECT_TRUE(subclass_helper.received_message(WM_NCLBUTTONDOWN));
  EXPECT_TRUE(subclass_helper.received_message(WM_NCMOUSEMOVE));
  EXPECT_FALSE(subclass_helper.received_message(WM_SYSCOMMAND));

  subclass_helper.Clear();
  // Posting a WM_NCLBUTTONDOWN message followed by a WM_NCMOUSEMOVE at a
  // different location should result in a WM_SYSCOMMAND message.
  ::PostMessage(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(100, 100));
  ::PostMessage(window, WM_NCMOUSEMOVE, HTCAPTION, MAKELPARAM(110, 110));
  RunPendingMessages();

  EXPECT_TRUE(subclass_helper.received_message(WM_NCLBUTTONDOWN));
  EXPECT_TRUE(subclass_helper.received_message(WM_NCMOUSEMOVE));
  EXPECT_TRUE(subclass_helper.received_message(WM_SYSCOMMAND));

  subclass_helper.Clear();
  // Posting a WM_NCLBUTTONDOWN message followed by a WM_NCMOUSEMOVE at a
  // different location with a different hittest code should result in a
  // WM_SYSCOMMAND message.
  ::PostMessage(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(100, 100));
  ::PostMessage(window, WM_NCMOUSEMOVE, HTTOP, MAKELPARAM(110, 102));
  RunPendingMessages();

  EXPECT_TRUE(subclass_helper.received_message(WM_NCLBUTTONDOWN));
  EXPECT_TRUE(subclass_helper.received_message(WM_NCMOUSEMOVE));
  EXPECT_TRUE(subclass_helper.received_message(WM_SYSCOMMAND));

  subclass_helper.Clear();
  // Posting a WM_NCLBUTTONDOWN message followed by a WM_MOUSEMOVE should
  // result in a WM_SYSCOMMAND message.
  ::PostMessage(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(100, 100));
  ::PostMessage(window, WM_MOUSEMOVE, HTCLIENT, MAKELPARAM(110, 110));
  RunPendingMessages();

  EXPECT_TRUE(subclass_helper.received_message(WM_NCLBUTTONDOWN));
  EXPECT_TRUE(subclass_helper.received_message(WM_MOUSEMOVE));
  EXPECT_TRUE(subclass_helper.received_message(WM_SYSCOMMAND));
}

// This test validates that destroying the window in the context of the
// WM_SYSCOMMAND message with SC_MOVE does not crash.
// Disabled because of flaky timeouts: http://crbug.com/592742
TEST_F(DesktopWidgetTest, DISABLED_DestroyInSysCommandNCLButtonDownOnCaption) {
  std::unique_ptr<Widget> widget =
      CreateTestWidget(Widget::InitParams::TYPE_WINDOW);
  widget->Show();
  ::SetCursorPos(500, 500);

  HWND window = widget->GetNativeWindow()->GetHost()->GetAcceleratedWidget();

  SubclassWindowHelper subclass_helper(window);

  // Destroying the window in the context of the WM_SYSCOMMAND message
  // should not crash.
  subclass_helper.set_message_to_destroy_on(WM_SYSCOMMAND);

  ::PostMessage(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(100, 100));
  ::PostMessage(window, WM_NCMOUSEMOVE, HTCAPTION, MAKELPARAM(110, 110));
  RunPendingMessages();

  EXPECT_TRUE(subclass_helper.received_message(WM_NCLBUTTONDOWN));
  EXPECT_TRUE(subclass_helper.received_message(WM_SYSCOMMAND));
}

#endif

// Test that the z-order levels round-trip.
TEST_F(WidgetTest, ZOrderLevel) {
  WidgetAutoclosePtr widget(CreateTopLevelNativeWidget());
  EXPECT_EQ(ui::ZOrderLevel::kNormal, widget->GetZOrderLevel());
  widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);
  EXPECT_EQ(ui::ZOrderLevel::kFloatingWindow, widget->GetZOrderLevel());
  widget->SetZOrderLevel(ui::ZOrderLevel::kNormal);
  EXPECT_EQ(ui::ZOrderLevel::kNormal, widget->GetZOrderLevel());
}

namespace {

class ScaleFactorView : public View {
 public:
  ScaleFactorView() = default;

  ScaleFactorView(const ScaleFactorView&) = delete;
  ScaleFactorView& operator=(const ScaleFactorView&) = delete;

  // Overridden from ui::LayerDelegate:
  void OnDeviceScaleFactorChanged(float old_device_scale_factor,
                                  float new_device_scale_factor) override {
    last_scale_factor_ = new_device_scale_factor;
    View::OnDeviceScaleFactorChanged(old_device_scale_factor,
                                     new_device_scale_factor);
  }

  float last_scale_factor() const { return last_scale_factor_; }

 private:
  float last_scale_factor_ = 0.f;
};

}  // namespace

// Ensure scale factor changes are propagated from the native Widget.
TEST_F(WidgetTest, OnDeviceScaleFactorChanged) {
  // Automatically close the widget, but not delete it.
  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  ScaleFactorView* view = new ScaleFactorView;
  widget->GetRootView()->AddChildView(view);
  float scale_factor = widget->GetLayer()->device_scale_factor();
  EXPECT_NE(scale_factor, 0.f);

  // For views that are not layer-backed, adding the view won't notify the view
  // about the initial scale factor. Fake it.
  view->OnDeviceScaleFactorChanged(0.f, scale_factor);
  EXPECT_EQ(scale_factor, view->last_scale_factor());

  // Changes should be propagated.
  scale_factor *= 2.0f;
  widget->GetLayer()->OnDeviceScaleFactorChanged(scale_factor);
  EXPECT_EQ(scale_factor, view->last_scale_factor());
}

namespace {

class TestWidgetRemovalsObserver : public WidgetRemovalsObserver {
 public:
  TestWidgetRemovalsObserver() = default;

  TestWidgetRemovalsObserver(const TestWidgetRemovalsObserver&) = delete;
  TestWidgetRemovalsObserver& operator=(const TestWidgetRemovalsObserver&) =
      delete;

  ~TestWidgetRemovalsObserver() override = default;

  void OnWillRemoveView(Widget* widget, View* view) override {
    removed_views_.insert(view);
  }

  bool DidRemoveView(View* view) {
    return removed_views_.find(view) != removed_views_.end();
  }

 private:
  std::set<View*> removed_views_;
};

}  // namespace

// Test that WidgetRemovalsObserver::OnWillRemoveView is called when deleting
// a view.
TEST_F(WidgetTest, WidgetRemovalsObserverCalled) {
  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  TestWidgetRemovalsObserver removals_observer;
  widget->AddRemovalsObserver(&removals_observer);

  View* parent = new View();
  widget->client_view()->AddChildView(parent);

  View* child = new View();
  parent->AddChildView(child);

  widget->client_view()->RemoveChildView(parent);
  EXPECT_TRUE(removals_observer.DidRemoveView(parent));
  EXPECT_FALSE(removals_observer.DidRemoveView(child));

  // Calling RemoveChildView() doesn't delete the view, but deleting
  // |parent| will automatically delete |child|.
  delete parent;

  widget->RemoveRemovalsObserver(&removals_observer);
}

// Test that WidgetRemovalsObserver::OnWillRemoveView is called when deleting
// the root view.
TEST_F(WidgetTest, WidgetRemovalsObserverCalledWhenRemovingRootView) {
  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  TestWidgetRemovalsObserver removals_observer;
  widget->AddRemovalsObserver(&removals_observer);
  views::View* root_view = widget->GetRootView();

  widget.reset();
  EXPECT_TRUE(removals_observer.DidRemoveView(root_view));
}

// Test that WidgetRemovalsObserver::OnWillRemoveView is called when moving
// a view from one widget to another, but not when moving a view within
// the same widget.
TEST_F(WidgetTest, WidgetRemovalsObserverCalledWhenMovingBetweenWidgets) {
  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  TestWidgetRemovalsObserver removals_observer;
  widget->AddRemovalsObserver(&removals_observer);

  View* parent = new View();
  widget->client_view()->AddChildView(parent);

  View* child = new View();
  widget->client_view()->AddChildView(child);

  // Reparenting the child shouldn't call the removals observer.
  parent->AddChildView(child);
  EXPECT_FALSE(removals_observer.DidRemoveView(child));

  // Moving the child to a different widget should call the removals observer.
  WidgetAutoclosePtr widget2(CreateTopLevelPlatformWidget());
  widget2->client_view()->AddChildView(child);
  EXPECT_TRUE(removals_observer.DidRemoveView(child));

  widget->RemoveRemovalsObserver(&removals_observer);
}

// Test dispatch of ui::ET_MOUSEWHEEL.
TEST_F(WidgetTest, MouseWheelEvent) {
  WidgetAutoclosePtr widget(CreateTopLevelPlatformWidget());
  widget->SetBounds(gfx::Rect(0, 0, 600, 600));
  EventCountView* event_count_view =
      widget->client_view()->AddChildView(std::make_unique<EventCountView>());
  event_count_view->SetBounds(0, 0, 600, 600);
  widget->Show();

  auto event_generator =
      CreateEventGenerator(GetContext(), widget->GetNativeWindow());

  event_generator->MoveMouseWheel(1, 1);
  EXPECT_EQ(1, event_count_view->GetEventCount(ui::ET_MOUSEWHEEL));
}

class WidgetShadowTest : public WidgetTest {
 public:
  WidgetShadowTest() = default;

  WidgetShadowTest(const WidgetShadowTest&) = delete;
  WidgetShadowTest& operator=(const WidgetShadowTest&) = delete;

  ~WidgetShadowTest() override = default;

  // WidgetTest:
  void SetUp() override {
    set_native_widget_type(NativeWidgetType::kDesktop);
    WidgetTest::SetUp();
    InitControllers();
  }

  void TearDown() override {
#if defined(USE_AURA) && !BUILDFLAG(ENABLE_DESKTOP_AURA)
    shadow_controller_.reset();
    focus_controller_.reset();
#endif
    WidgetTest::TearDown();
  }

  Widget::InitParams CreateParams(Widget::InitParams::Type type) override {
    Widget::InitParams params =
        WidgetTest::CreateParams(override_type_.value_or(type));
    params.shadow_type = Widget::InitParams::ShadowType::kDrop;
    params.shadow_elevation = 10;
    params.name = name_;
    params.child = force_child_;
    return params;
  }

 protected:
  absl::optional<Widget::InitParams::Type> override_type_;
  std::string name_;
  bool force_child_ = false;

 private:
#if BUILDFLAG(ENABLE_DESKTOP_AURA) || defined(OS_MAC)
  void InitControllers() {}
#else
  class TestFocusRules : public wm::BaseFocusRules {
   public:
    TestFocusRules() = default;

    TestFocusRules(const TestFocusRules&) = delete;
    TestFocusRules& operator=(const TestFocusRules&) = delete;

    bool SupportsChildActivation(const aura::Window* window) const override {
      return true;
    }
  };

  void InitControllers() {
    focus_controller_ =
        std::make_unique<wm::FocusController>(new TestFocusRules);
    shadow_controller_ = std::make_unique<wm::ShadowController>(
        focus_controller_.get(), nullptr);
  }

  std::unique_ptr<wm::FocusController> focus_controller_;
  std::unique_ptr<wm::ShadowController> shadow_controller_;
#endif  // !BUILDFLAG(ENABLE_DESKTOP_AURA) && !defined(OS_MAC)
};

// Disabled on Mac: All drop shadows are managed out of process for now.
#if defined(OS_MAC)
#define MAYBE_ShadowsInRootWindow DISABLED_ShadowsInRootWindow
#else
#define MAYBE_ShadowsInRootWindow ShadowsInRootWindow
#endif

// Test that shadows are not added to root windows when created or upon
// activation. Test that shadows are added to non-root windows even if not
// activated.
TEST_F(WidgetShadowTest, MAYBE_ShadowsInRootWindow) {
#if defined(USE_AURA) && !BUILDFLAG(ENABLE_DESKTOP_AURA)
  // On ChromeOS, top-levels have shadows.
  bool top_level_window_should_have_shadow = true;
#else
  // On non-chromeos platforms, the hosting OS is responsible for the shadow.
  bool top_level_window_should_have_shadow = false;
#endif

  // To start, just create a Widget. This constructs the first ShadowController
  // which will start observing the environment for additional aura::Window
  // initialization. The very first ShadowController in DesktopNativeWidgetAura
  // is created after the call to aura::Window::Init(), so the ShadowController
  // Impl class won't ever see this first Window being initialized.
  name_ = "other_top_level";
  Widget* other_top_level = CreateTopLevelNativeWidget();

  name_ = "top_level";
  Widget* top_level = CreateTopLevelNativeWidget();
  top_level->SetBounds(gfx::Rect(100, 100, 320, 200));

  EXPECT_FALSE(WidgetHasInProcessShadow(top_level));
  EXPECT_FALSE(top_level->IsVisible());
  top_level->ShowInactive();
  EXPECT_EQ(top_level_window_should_have_shadow,
            WidgetHasInProcessShadow(top_level));
  top_level->Show();
  EXPECT_EQ(top_level_window_should_have_shadow,
            WidgetHasInProcessShadow(top_level));

  name_ = "control";
  Widget* control = CreateChildNativeWidgetWithParent(top_level);
  control->SetBounds(gfx::Rect(20, 20, 160, 100));

  // Widgets of TYPE_CONTROL become visible during Init, so start with a shadow.
  EXPECT_TRUE(WidgetHasInProcessShadow(control));
  control->ShowInactive();
  EXPECT_TRUE(WidgetHasInProcessShadow(control));
  control->Show();
  EXPECT_TRUE(WidgetHasInProcessShadow(control));

  name_ = "child";
  override_type_ = Widget::InitParams::TYPE_POPUP;
  force_child_ = true;
  Widget* child = CreateChildNativeWidgetWithParent(top_level);
  child->SetBounds(gfx::Rect(20, 20, 160, 100));

  // Now false: the Widget hasn't been shown yet.
  EXPECT_FALSE(WidgetHasInProcessShadow(child));
  child->ShowInactive();
  EXPECT_TRUE(WidgetHasInProcessShadow(child));
  child->Show();
  EXPECT_TRUE(WidgetHasInProcessShadow(child));

  other_top_level->Show();

  // Re-activate the top level window. This handles a hypothetical case where
  // a shadow is added via the ActivationChangeObserver rather than by the
  // aura::WindowObserver. Activation changes only modify an existing shadow
  // (if there is one), but should never install a Shadow, even if the Window
  // properties otherwise say it should have one.
  top_level->Show();
  EXPECT_EQ(top_level_window_should_have_shadow,
            WidgetHasInProcessShadow(top_level));

  top_level->Close();
  other_top_level->Close();
}

#if defined(OS_WIN)

// Tests the case where an intervening owner popup window is destroyed out from
// under the currently active modal top-level window. In this instance, the
// remaining top-level windows should be re-enabled.
TEST_F(DesktopWidgetTest, WindowModalOwnerDestroyedEnabledTest) {
  // top_level_widget owns owner_dialog_widget which owns owned_dialog_widget.
  std::unique_ptr<Widget> top_level_widget = CreateTestWidget();
  top_level_widget->Show();

  // Create the owner modal dialog.
  const auto create_params = [this](Widget* widget, gfx::NativeView parent) {
    Widget::InitParams init_params =
        CreateParamsForTestWidget(Widget::InitParams::TYPE_WINDOW);
    init_params.delegate = new WidgetDelegate();
    init_params.delegate->SetModalType(ui::MODAL_TYPE_WINDOW);
    init_params.parent = parent;
    init_params.native_widget =
        new test::TestPlatformNativeWidget<DesktopNativeWidgetAura>(
            widget, false, nullptr);
    return init_params;
  };
  Widget owner_dialog_widget(
      create_params(&owner_dialog_widget, top_level_widget->GetNativeView()));
  owner_dialog_widget.Show();
  HWND owner_hwnd = HWNDForWidget(&owner_dialog_widget);

  // Create the owned modal dialog.
  Widget owned_dialog_widget(
      create_params(&owned_dialog_widget, owner_dialog_widget.GetNativeView()));
  owned_dialog_widget.Show();
  HWND owned_hwnd = HWNDForWidget(&owned_dialog_widget);

  RunPendingMessages();

  HWND top_hwnd = HWNDForWidget(top_level_widget.get());

  EXPECT_FALSE(!!IsWindowEnabled(owner_hwnd));
  EXPECT_FALSE(!!IsWindowEnabled(top_hwnd));
  EXPECT_TRUE(!!IsWindowEnabled(owned_hwnd));

  owner_dialog_widget.CloseNow();
  RunPendingMessages();

  EXPECT_FALSE(!!IsWindow(owner_hwnd));
  EXPECT_FALSE(!!IsWindow(owned_hwnd));
  EXPECT_TRUE(!!IsWindowEnabled(top_hwnd));

  top_level_widget->CloseNow();
}

#endif  // defined(OS_WIN)

#if BUILDFLAG(ENABLE_DESKTOP_AURA) || defined(OS_MAC)

namespace {

bool CanHaveCompositingManager() {
#if defined(USE_OZONE)
  auto* const egl_utility =
      ui::OzonePlatform::GetInstance()->GetPlatformGLEGLUtility();
  return (egl_utility != nullptr) && egl_utility->HasVisualManager();
#else
  return false;
#endif
}

bool ExpectWidgetTransparency(Widget::InitParams::WindowOpacity opacity) {
  switch (opacity) {
    case Widget::InitParams::WindowOpacity::kOpaque:
      return false;
    case Widget::InitParams::WindowOpacity::kTranslucent:
      return true;
    case Widget::InitParams::WindowOpacity::kInferred:
      ADD_FAILURE() << "WidgetOpacity must be explicitly set";
      return false;
  }
}

class CompositingWidgetTest : public DesktopWidgetTest {
 public:
  CompositingWidgetTest()
      : widget_types_{Widget::InitParams::TYPE_WINDOW,
                      Widget::InitParams::TYPE_WINDOW_FRAMELESS,
                      Widget::InitParams::TYPE_CONTROL,
                      Widget::InitParams::TYPE_POPUP,
                      Widget::InitParams::TYPE_MENU,
                      Widget::InitParams::TYPE_TOOLTIP,
                      Widget::InitParams::TYPE_BUBBLE,
                      Widget::InitParams::TYPE_DRAG} {}

  CompositingWidgetTest(const CompositingWidgetTest&) = delete;
  CompositingWidgetTest& operator=(const CompositingWidgetTest&) = delete;

  ~CompositingWidgetTest() override = default;

  Widget::InitParams CreateParams(Widget::InitParams::Type type) override {
    Widget::InitParams params = DesktopWidgetTest::CreateParams(type);
    params.opacity = opacity_;
    return params;
  }

  void CheckAllWidgetsForOpacity(
      const Widget::InitParams::WindowOpacity opacity) {
    opacity_ = opacity;
    for (const auto& widget_type : widget_types_) {
#if defined(OS_MAC)
      // Tooltips are native on Mac. See NativeWidgetNSWindowBridge::Init.
      if (widget_type == Widget::InitParams::TYPE_TOOLTIP)
        continue;
#elif defined(OS_WIN)
      // Other widget types would require to create a parent window and the
      // the purpose of this test is mainly X11 in the first place.
      if (widget_type != Widget::InitParams::TYPE_WINDOW)
        continue;
#endif
      std::unique_ptr<Widget> widget = CreateTestWidget(widget_type);

      // Use NativeWidgetAura directly.
      if (widget_type == Widget::InitParams::TYPE_WINDOW_FRAMELESS ||
          widget_type == Widget::InitParams::TYPE_CONTROL)
        continue;

#if defined(OS_MAC)
      // Mac always always has a compositing window manager, but doesn't have
      // transparent titlebars which is what ShouldWindowContentsBeTransparent()
      // is currently used for. Asking for transparency should get it. Note that
      // TestViewsDelegate::use_transparent_windows_ determines the result of
      // kInferOpacity: assume it is false.
      bool should_be_transparent =
          opacity_ == Widget::InitParams::WindowOpacity::kTranslucent;
#else
      bool should_be_transparent = widget->ShouldWindowContentsBeTransparent();
#endif

      EXPECT_EQ(IsNativeWindowTransparent(widget->GetNativeWindow()),
                should_be_transparent);

      if (CanHaveCompositingManager()) {
        if (HasCompositingManager() && ExpectWidgetTransparency(opacity))
          EXPECT_TRUE(widget->IsTranslucentWindowOpacitySupported());
        else
          EXPECT_FALSE(widget->IsTranslucentWindowOpacitySupported());
      }
    }
  }

 protected:
  const std::vector<Widget::InitParams::Type> widget_types_;
  Widget::InitParams::WindowOpacity opacity_ =
      Widget::InitParams::WindowOpacity::kInferred;
};

}  // namespace

// Only test manually set opacity via kOpaque or kTranslucent.  kInferred is
// unpredictable and depends on the platform and window type.
TEST_F(CompositingWidgetTest, Transparency_DesktopWidgetOpaque) {
  CheckAllWidgetsForOpacity(Widget::InitParams::WindowOpacity::kOpaque);
}

TEST_F(CompositingWidgetTest, Transparency_DesktopWidgetTranslucent) {
  CheckAllWidgetsForOpacity(Widget::InitParams::WindowOpacity::kTranslucent);
}

#endif  // BUILDFLAG(ENABLE_DESKTOP_AURA) || defined(OS_MAC)

}  // namespace test
}  // namespace views
