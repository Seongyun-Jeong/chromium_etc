// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/host/wayland_seat.h"

#include <idle-client-protocol.h>

#include "base/logging.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/ozone/platform/wayland/host/wayland_connection.h"
#include "ui/ozone/platform/wayland/host/wayland_event_source.h"
#include "ui/ozone/platform/wayland/host/wayland_keyboard.h"
#include "ui/ozone/platform/wayland/host/wayland_pointer.h"
#include "ui/ozone/platform/wayland/host/wayland_touch.h"

namespace ui {

namespace {
constexpr uint32_t kMinVersion = 1;
constexpr uint32_t kMaxVersion = 5;
}  // namespace

// static
constexpr char WaylandSeat::kInterfaceName[];

// static
void WaylandSeat::Instantiate(WaylandConnection* connection,
                              wl_registry* registry,
                              uint32_t name,
                              const std::string& interface,
                              uint32_t version) {
  DCHECK_EQ(interface, kInterfaceName);

  if (connection->seat_ ||
      !wl::CanBind(interface, version, kMinVersion, kMaxVersion)) {
    return;
  }

  auto seat =
      wl::Bind<struct wl_seat>(registry, name, std::min(version, kMaxVersion));
  if (!seat) {
    LOG(ERROR) << "Failed to bind to wl_seat global";
    return;
  }
  connection->seat_ = std::make_unique<WaylandSeat>(seat.release(), connection);

  // The seat is one of objects needed for data exchange.  Notify the connection
  // so it might set up the rest if all other parts are in place.
  connection->CreateDataObjectsIfReady();
}

WaylandSeat::WaylandSeat(wl_seat* seat, WaylandConnection* connection)
    : obj_(seat), connection_(connection) {
  DCHECK(connection_);
  DCHECK(obj_);

  static constexpr wl_seat_listener kSeatListener = {
      &Capabilities,
      &Name,
  };
  wl_seat_add_listener(wl_object(), &kSeatListener, this);
}

WaylandSeat::~WaylandSeat() = default;

bool WaylandSeat::RefreshKeyboard() {
  // Make sure to destroy the old WaylandKeyboard (if it exists) before creating
  // the new one.
  keyboard_.reset();

  wl_keyboard* keyboard = wl_seat_get_keyboard(wl_object());
  if (!keyboard)
    return false;

  auto* layout_engine = KeyboardLayoutEngineManager::GetKeyboardLayoutEngine();
  keyboard_ = std::make_unique<WaylandKeyboard>(
      keyboard, connection_->keyboard_extension_v1_.get(), connection_,
      layout_engine, connection_->event_source());
  return true;
}

// static
void WaylandSeat::Capabilities(void* data,
                               wl_seat* seat,
                               uint32_t capabilities) {
  WaylandSeat* self = static_cast<WaylandSeat*>(data);
  DCHECK(self);
  DCHECK(self->connection_->event_source());

  if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
    if (!self->keyboard_ && !self->RefreshKeyboard())
      LOG(ERROR) << "Failed to get wl_keyboard from seat";
  } else {
    self->keyboard_.reset();
  }

  if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
    if (!self->pointer_) {
      if (wl_pointer* pointer = wl_seat_get_pointer(seat)) {
        self->pointer_ = std::make_unique<WaylandPointer>(
            pointer, self->connection_, self->connection_->event_source());
      } else {
        LOG(ERROR) << "Failed to get wl_pointer from seat";
      }
    }
  } else {
    self->pointer_.reset();
  }

  if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
    if (!self->touch_) {
      if (wl_touch* touch = wl_seat_get_touch(seat)) {
        self->touch_ = std::make_unique<WaylandTouch>(
            touch, self->connection_, self->connection_->event_source());
      } else {
        LOG(ERROR) << "Failed to get wl_touch from seat";
      }
    }
  } else {
    self->touch_.reset();
  }

  self->connection_->UpdateInputDevices();

  self->connection_->ScheduleFlush();
}

// static
void WaylandSeat::Name(void* data, wl_seat* seat, const char* name) {
  NOTIMPLEMENTED_LOG_ONCE();
}

}  // namespace ui
