// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/exo/wayland/weston_test.h"

#include <linux/input.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <weston-test-server-protocol.h>

#include "ash/shell.h"
#include "base/notreached.h"
#include "base/run_loop.h"
#include "components/exo/surface.h"
#include "components/exo/wayland/server_util.h"
#include "components/exo/wm_helper.h"
#include "components/exo/xkb_tracker.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/wm/core/window_util.h"

namespace exo {
namespace wayland {

// Tracks button and mouse states for testing.
struct WestonTest::WestonTestState {
  WestonTestState() {}

  WestonTestState(const WestonTestState&) = delete;
  WestonTestState& operator=(const WestonTestState&) = delete;

  bool left_button_pressed = false;
  bool middle_button_pressed = false;
  bool right_button_pressed = false;

  bool control_pressed = false;
  bool alt_pressed = false;
  bool shift_pressed = false;
  bool command_pressed = false;
};

namespace {

using WestonTestState = WestonTest::WestonTestState;

constexpr uint32_t kWestonTestVersion = 1;

static void weston_test_move_surface(struct wl_client* client,
                                     struct wl_resource* resource,
                                     struct wl_resource* surface_resource,
                                     int32_t x,
                                     int32_t y) {
  NOTIMPLEMENTED();
}

static void weston_test_move_pointer(struct wl_client* client,
                                     struct wl_resource* resource,
                                     struct wl_resource* surface_resource,
                                     uint32_t tv_sec_hi,
                                     uint32_t tv_sec_lo,
                                     uint32_t tv_nsec,
                                     int32_t x,
                                     int32_t y) {
  // Convert cursor point from window space to root space
  gfx::Point point_in_root(x, y);
  if (surface_resource) {
    aura::Window* window = GetUserDataAs<Surface>(surface_resource)->window();
    aura::Window::ConvertPointToTarget(window, window->GetRootWindow(),
                                       &point_in_root);
  }
  base::RunLoop run_loop;
  ui_controls::SendMouseMoveNotifyWhenDone(point_in_root.x(), point_in_root.y(),
                                           run_loop.QuitClosure());
  run_loop.Run();
  // TODO(https://crbug.com/1284726): This should not be necessary.
  // At this point the resource could have been destroyed.
  if (GetUserDataAs<WestonTestState>(resource))
    weston_test_send_pointer_position(resource, x, y);
}

static void weston_test_send_button(struct wl_client* client,
                                    struct wl_resource* resource,
                                    uint32_t tv_sec_hi,
                                    uint32_t tv_sec_lo,
                                    uint32_t tv_nsec,
                                    int32_t button,
                                    uint32_t state) {
  // Unsuppported button types
  DCHECK(button != BTN_FORWARD);
  DCHECK(button != BTN_BACK);

  // Track mouse click state
  auto* weston_test = GetUserDataAs<WestonTestState>(resource);
  ui_controls::MouseButton mouse_button = ui_controls::LEFT;
  switch (button) {
    case BTN_LEFT:
      mouse_button = ui_controls::LEFT;
      weston_test->left_button_pressed = state;
      break;
    case BTN_MIDDLE:
      mouse_button = ui_controls::MIDDLE;
      weston_test->middle_button_pressed = state;
      break;
    case BTN_RIGHT:
      mouse_button = ui_controls::RIGHT;
      weston_test->right_button_pressed = state;
      break;
    default:
      NOTIMPLEMENTED();
  }

  auto mouse_state = state == WL_POINTER_BUTTON_STATE_PRESSED
                         ? ui_controls::DOWN
                         : ui_controls::UP;
  base::RunLoop run_loop;
  ui_controls::SendMouseEventsNotifyWhenDone(mouse_button, mouse_state,
                                             run_loop.QuitClosure());
  run_loop.Run();

  // TODO(https://crbug.com/1284726): This should not be necessary.
  // At this point the resource could have been destroyed.
  if (GetUserDataAs<WestonTestState>(resource))
    weston_test_send_pointer_button(resource, button, state);
}

static void weston_test_reset_pointer(struct wl_client* client,
                                      struct wl_resource* resource) {
  auto* weston_test = GetUserDataAs<WestonTestState>(resource);
  if (weston_test->left_button_pressed) {
    weston_test->left_button_pressed = false;
    base::RunLoop run_loop;
    ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::LEFT, ui_controls::UP, run_loop.QuitClosure());
    run_loop.Run();
    // TODO(https://crbug.com/1284726): This should not be necessary.
    // At this point the resource could have been destroyed.
    if (GetUserDataAs<WestonTestState>(resource)) {
      weston_test_send_pointer_button(resource, BTN_LEFT,
                                      WL_POINTER_BUTTON_STATE_RELEASED);
    }
  }
  if (weston_test->middle_button_pressed) {
    weston_test->middle_button_pressed = false;
    base::RunLoop run_loop;
    ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::MIDDLE, ui_controls::UP, run_loop.QuitClosure());
    run_loop.Run();
    // TODO(https://crbug.com/1284726): This should not be necessary.
    // At this point the resource could have been destroyed.
    if (GetUserDataAs<WestonTestState>(resource)) {
      weston_test_send_pointer_button(resource, BTN_MIDDLE,
                                      WL_POINTER_BUTTON_STATE_RELEASED);
    }
  }
  if (weston_test->right_button_pressed) {
    weston_test->right_button_pressed = false;
    base::RunLoop run_loop;
    ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::RIGHT, ui_controls::UP, run_loop.QuitClosure());
    run_loop.Run();
    // TODO(https://crbug.com/1284726): This should not be necessary.
    // At this point the resource could have been destroyed.
    if (GetUserDataAs<WestonTestState>(resource)) {
      weston_test_send_pointer_button(resource, BTN_RIGHT,
                                      WL_POINTER_BUTTON_STATE_RELEASED);
    }
  }
}

static void weston_test_send_axis(struct wl_client* client,
                                  struct wl_resource* resource,
                                  uint32_t tv_sec_hi,
                                  uint32_t tv_sec_lo,
                                  uint32_t tv_nsec,
                                  uint32_t axis,
                                  wl_fixed_t value) {
  NOTIMPLEMENTED();
}

static void weston_test_activate_surface(struct wl_client* client,
                                         struct wl_resource* resource,
                                         struct wl_resource* surface_resource) {
  auto* surface = GetUserDataAs<Surface>(surface_resource);
  surface->RequestActivation();
  wm::ActivateWindow(wm::GetActivatableWindow(surface->window()));
}

static void weston_test_send_key(struct wl_client* client,
                                 struct wl_resource* resource,
                                 uint32_t tv_sec_hi,
                                 uint32_t tv_sec_lo,
                                 uint32_t tv_nsec,
                                 uint32_t key,
                                 uint32_t state) {
  ui::DomCode dom_code = ui::KeycodeConverter::EvdevCodeToDomCode(key);
  auto* weston_test = GetUserDataAs<WestonTestState>(resource);

  // Get keyboard modifiers
  switch (dom_code) {
    case ui::DomCode::CONTROL_LEFT:
    case ui::DomCode::CONTROL_RIGHT:
      weston_test->control_pressed = state;
      weston_test_send_keyboard_key(resource, key, state);
      return;
    case ui::DomCode::ALT_LEFT:
    case ui::DomCode::ALT_RIGHT:
      weston_test->alt_pressed = state;
      weston_test_send_keyboard_key(resource, key, state);
      return;
    case ui::DomCode::SHIFT_LEFT:
    case ui::DomCode::SHIFT_RIGHT:
      weston_test->shift_pressed = state;
      weston_test_send_keyboard_key(resource, key, state);
      return;
    case ui::DomCode::META_LEFT:
    case ui::DomCode::META_RIGHT:
      weston_test->command_pressed = state;
      weston_test_send_keyboard_key(resource, key, state);
      return;
    default:
      // Keep track of pressed keys, but send key press only after we have
      // received all keys
      if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        weston_test_send_keyboard_key(resource, key, state);
        return;
      }
  }

  aura::Window* window = ash::Shell::GetPrimaryRootWindow();
  DCHECK(window);
  ui::KeyboardCode key_code =
      ui::DomCodeToUsLayoutNonLocatedKeyboardCode(dom_code);
  base::RunLoop run_loop;
  ui_controls::SendKeyPressNotifyWhenDone(
      window, key_code, weston_test->control_pressed,
      weston_test->shift_pressed, weston_test->alt_pressed,
      weston_test->command_pressed,
      run_loop.QuitClosure());
  run_loop.Run();
  // TODO(https://crbug.com/1284726): This should not be necessary.
  // At this point the resource could have been destroyed.
  if (GetUserDataAs<WestonTestState>(resource))
    weston_test_send_keyboard_key(resource, key, state);
}

static void weston_test_device_release(struct wl_client* client,
                                       struct wl_resource* resource,
                                       const char* device) {
  NOTIMPLEMENTED();
}

static void weston_test_device_add(struct wl_client* client,
                                   struct wl_resource* resource,
                                   const char* device) {
  NOTIMPLEMENTED();
}

static void weston_test_capture_screenshot(struct wl_client* client,
                                           struct wl_resource* resource,
                                           struct wl_resource* output,
                                           struct wl_resource* buffer) {
  NOTIMPLEMENTED();
}

static void weston_test_send_touch(struct wl_client* client,
                                   struct wl_resource* resource,
                                   uint32_t tv_sec_hi,
                                   uint32_t tv_sec_lo,
                                   uint32_t tv_nsec,
                                   int32_t touch_id,
                                   wl_fixed_t x,
                                   wl_fixed_t y,
                                   uint32_t touch_type) {
  NOTIMPLEMENTED();
}

const struct weston_test_interface weston_test_implementation = {
    weston_test_move_surface, weston_test_move_pointer,
    weston_test_send_button,  weston_test_reset_pointer,
    weston_test_send_axis,    weston_test_activate_surface,
    weston_test_send_key,     weston_test_device_release,
    weston_test_device_add,   weston_test_capture_screenshot,
    weston_test_send_touch};

void bind_weston_test(wl_client* client,
                      void* data,
                      uint32_t version,
                      uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &weston_test_interface, version, id);

  wl_resource_set_implementation(resource, &weston_test_implementation, data,
                                 nullptr);
}

}  // namespace

WestonTest::WestonTest(wl_display* display)
    : data_(std::make_unique<WestonTestState>()) {
  wl_global_create(display, &weston_test_interface, kWestonTestVersion,
                   data_.get(), bind_weston_test);
}

WestonTest::~WestonTest() = default;

}  // namespace wayland
}  // namespace exo
