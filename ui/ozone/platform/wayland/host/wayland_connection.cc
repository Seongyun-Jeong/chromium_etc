// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/host/wayland_connection.h"

#include <extended-drag-unstable-v1-client-protocol.h>
#include <presentation-time-client-protocol.h>
#include <xdg-shell-client-protocol.h>
#include <xdg-shell-unstable-v6-client-protocol.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_util.h"
#include "base/task/current_thread.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "ui/events/devices/device_data_manager.h"
#include "ui/events/devices/input_device.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/gfx/geometry/point.h"
#include "ui/ozone/common/features.h"
#include "ui/ozone/platform/wayland/common/wayland_object.h"
#include "ui/ozone/platform/wayland/host/gtk_primary_selection_device_manager.h"
#include "ui/ozone/platform/wayland/host/gtk_shell1.h"
#include "ui/ozone/platform/wayland/host/org_kde_kwin_idle.h"
#include "ui/ozone/platform/wayland/host/overlay_prioritizer.h"
#include "ui/ozone/platform/wayland/host/proxy/wayland_proxy_impl.h"
#include "ui/ozone/platform/wayland/host/surface_augmenter.h"
#include "ui/ozone/platform/wayland/host/wayland_buffer_manager_host.h"
#include "ui/ozone/platform/wayland/host/wayland_clipboard.h"
#include "ui/ozone/platform/wayland/host/wayland_cursor.h"
#include "ui/ozone/platform/wayland/host/wayland_cursor_position.h"
#include "ui/ozone/platform/wayland/host/wayland_data_device_manager.h"
#include "ui/ozone/platform/wayland/host/wayland_data_drag_controller.h"
#include "ui/ozone/platform/wayland/host/wayland_drm.h"
#include "ui/ozone/platform/wayland/host/wayland_event_source.h"
#include "ui/ozone/platform/wayland/host/wayland_input_method_context.h"
#include "ui/ozone/platform/wayland/host/wayland_keyboard.h"
#include "ui/ozone/platform/wayland/host/wayland_output_manager.h"
#include "ui/ozone/platform/wayland/host/wayland_pointer.h"
#include "ui/ozone/platform/wayland/host/wayland_seat.h"
#include "ui/ozone/platform/wayland/host/wayland_shm.h"
#include "ui/ozone/platform/wayland/host/wayland_window.h"
#include "ui/ozone/platform/wayland/host/wayland_window_drag_controller.h"
#include "ui/ozone/platform/wayland/host/wayland_zaura_shell.h"
#include "ui/ozone/platform/wayland/host/wayland_zcr_cursor_shapes.h"
#include "ui/ozone/platform/wayland/host/wayland_zwp_linux_dmabuf.h"
#include "ui/ozone/platform/wayland/host/wayland_zwp_pointer_constraints.h"
#include "ui/ozone/platform/wayland/host/wayland_zwp_pointer_gestures.h"
#include "ui/ozone/platform/wayland/host/wayland_zwp_relative_pointer_manager.h"
#include "ui/ozone/platform/wayland/host/xdg_foreign_wrapper.h"
#include "ui/ozone/platform/wayland/host/zwp_idle_inhibit_manager.h"
#include "ui/ozone/platform/wayland/host/zwp_primary_selection_device_manager.h"
#include "ui/platform_window/common/platform_window_defaults.h"

#if defined(USE_LIBWAYLAND_STUBS)
#include <dlfcn.h>

#include "third_party/wayland/libwayland_stubs.h"  // nogncheck
#endif

namespace ui {

namespace {

// The maximum supported versions for a given interface.
// The version bound will be the minimum of the value and the version
// advertised by the server.
constexpr uint32_t kMaxCompositorVersion = 4;
constexpr uint32_t kMaxKeyboardExtensionVersion = 2;
constexpr uint32_t kMaxXdgShellVersion = 3;
constexpr uint32_t kMaxZXdgShellVersion = 1;
constexpr uint32_t kMaxWpPresentationVersion = 1;
constexpr uint32_t kMaxWpViewporterVersion = 1;
constexpr uint32_t kMaxTextInputManagerVersion = 1;
constexpr uint32_t kMaxTextInputExtensionVersion = 1;
constexpr uint32_t kMaxExplicitSyncVersion = 2;
constexpr uint32_t kMaxAlphaCompositingVersion = 1;
constexpr uint32_t kMaxXdgDecorationVersion = 1;
constexpr uint32_t kMaxExtendedDragVersion = 1;
constexpr uint32_t kMaxXdgOutputManagerVersion = 3;

int64_t ConvertTimespecToMicros(const struct timespec& ts) {
  // On 32-bit systems, the calculation cannot overflow int64_t.
  // 2**32 * 1000000 + 2**64 / 1000 < 2**63
  if (sizeof(ts.tv_sec) <= 4 && sizeof(ts.tv_nsec) <= 8) {
    int64_t result = ts.tv_sec;
    result *= base::Time::kMicrosecondsPerSecond;
    result += (ts.tv_nsec / base::Time::kNanosecondsPerMicrosecond);
    return result;
  }
  base::CheckedNumeric<int64_t> result(ts.tv_sec);
  result *= base::Time::kMicrosecondsPerSecond;
  result += (ts.tv_nsec / base::Time::kNanosecondsPerMicrosecond);
  return result.ValueOrDie();
}

int64_t ConvertTimespecResultToMicros(uint32_t tv_sec_hi,
                                      uint32_t tv_sec_lo,
                                      uint32_t tv_nsec) {
  base::CheckedNumeric<int64_t> result =
      (static_cast<int64_t>(tv_sec_hi) << 32) + tv_sec_lo;
  result *= base::Time::kMicrosecondsPerSecond;
  result += (tv_nsec / base::Time::kNanosecondsPerMicrosecond);
  return result.ValueOrDie();
}

}  // namespace

void ReportShellUMA(UMALinuxWaylandShell shell) {
  static std::set<UMALinuxWaylandShell> reported_shells;
  if (reported_shells.count(shell) > 0)
    return;
  base::UmaHistogramEnumeration("Linux.Wayland.Shell", shell);
  reported_shells.insert(shell);
}

WaylandConnection::WaylandConnection() = default;

WaylandConnection::~WaylandConnection() = default;

bool WaylandConnection::Initialize() {
#if defined(USE_LIBWAYLAND_STUBS)
  // Use RTLD_NOW to load all symbols, since the stubs will try to load all of
  // them anyway.  Use RTLD_GLOBAL to add the symbols to the global namespace.
  auto dlopen_flags = RTLD_NOW | RTLD_GLOBAL;
  if (void* libwayland_client =
          dlopen("libwayland-client.so.0", dlopen_flags)) {
    third_party_wayland::InitializeLibwaylandclient(libwayland_client);
  } else {
    LOG(ERROR) << "Failed to load wayland client libraries.";
    return false;
  }

  if (void* libwayland_egl = dlopen("libwayland-egl.so.1", dlopen_flags))
    third_party_wayland::InitializeLibwaylandegl(libwayland_egl);

  // TODO(crbug.com/1081784): consider handling this in more flexible way.
  // libwayland-cursor is said to be part of the standard shipment of Wayland,
  // and it seems unlikely (although possible) that it would be unavailable
  // while libwayland-client was present.  To handle that gracefully, chrome can
  // fall back to the generic Ozone behaviour.
  if (void* libwayland_cursor =
          dlopen("libwayland-cursor.so.0", dlopen_flags)) {
    third_party_wayland::InitializeLibwaylandcursor(libwayland_cursor);
  } else {
    LOG(ERROR) << "Failed to load libwayland-cursor.so.0.";
    return false;
  }
#endif

  // Register factories for classes that implement wl::GlobalObjectRegistrar<T>.
  // Keep alphabetical order for convenience.
  RegisterGlobalObjectFactory(GtkPrimarySelectionDeviceManager::kInterfaceName,
                              &GtkPrimarySelectionDeviceManager::Instantiate);
  RegisterGlobalObjectFactory(GtkShell1::kInterfaceName,
                              &GtkShell1::Instantiate);
  RegisterGlobalObjectFactory(OrgKdeKwinIdle::kInterfaceName,
                              &OrgKdeKwinIdle::Instantiate);
  RegisterGlobalObjectFactory(OverlayPrioritizer::kInterfaceName,
                              &OverlayPrioritizer::Instantiate);
  RegisterGlobalObjectFactory(SurfaceAugmenter::kInterfaceName,
                              &SurfaceAugmenter::Instantiate);
  RegisterGlobalObjectFactory(WaylandDataDeviceManager::kInterfaceName,
                              &WaylandDataDeviceManager::Instantiate);
  RegisterGlobalObjectFactory(WaylandDrm::kInterfaceName,
                              &WaylandDrm::Instantiate);
  RegisterGlobalObjectFactory(WaylandOutput::kInterfaceName,
                              &WaylandOutput::Instantiate);
  RegisterGlobalObjectFactory(WaylandSeat::kInterfaceName,
                              &WaylandSeat::Instantiate);
  RegisterGlobalObjectFactory(WaylandShm::kInterfaceName,
                              &WaylandShm::Instantiate);
  RegisterGlobalObjectFactory(WaylandZAuraShell::kInterfaceName,
                              &WaylandZAuraShell::Instantiate);
  RegisterGlobalObjectFactory(WaylandZcrCursorShapes::kInterfaceName,
                              &WaylandZcrCursorShapes::Instantiate);
  RegisterGlobalObjectFactory(WaylandZwpLinuxDmabuf::kInterfaceName,
                              &WaylandZwpLinuxDmabuf::Instantiate);
  RegisterGlobalObjectFactory(WaylandZwpPointerConstraints::kInterfaceName,
                              &WaylandZwpPointerConstraints::Instantiate);
  RegisterGlobalObjectFactory(WaylandZwpPointerGestures::kInterfaceName,
                              &WaylandZwpPointerGestures::Instantiate);
  RegisterGlobalObjectFactory(WaylandZwpRelativePointerManager::kInterfaceName,
                              &WaylandZwpRelativePointerManager::Instantiate);
  RegisterGlobalObjectFactory(XdgForeignWrapper::kInterfaceNameV1,
                              &XdgForeignWrapper::Instantiate);
  RegisterGlobalObjectFactory(XdgForeignWrapper::kInterfaceNameV2,
                              &XdgForeignWrapper::Instantiate);
  RegisterGlobalObjectFactory(ZwpIdleInhibitManager::kInterfaceName,
                              &ZwpIdleInhibitManager::Instantiate);
  RegisterGlobalObjectFactory(ZwpPrimarySelectionDeviceManager::kInterfaceName,
                              &ZwpPrimarySelectionDeviceManager::Instantiate);

  static constexpr wl_registry_listener registry_listener = {
      &Global,
      &GlobalRemove,
  };

  display_.reset(wl_display_connect(nullptr));
  if (!display_) {
    LOG(ERROR) << "Failed to connect to Wayland display";
    return false;
  }

  wrapped_display_.reset(
      reinterpret_cast<wl_proxy*>(wl_proxy_create_wrapper(display())));
  // Create a non-default event queue so that we wouldn't flush messages for
  // client applications.
  event_queue_.reset(wl_display_create_queue(display()));
  wl_proxy_set_queue(wrapped_display_.get(), event_queue_.get());

  registry_.reset(wl_display_get_registry(display_wrapper()));
  if (!registry_) {
    LOG(ERROR) << "Failed to get Wayland registry";
    return false;
  }

  // Now that the connection with the display server has been properly
  // estabilished, initialize the event source and input objects.
  DCHECK(!event_source_);
  event_source_ = std::make_unique<WaylandEventSource>(
      display(), event_queue_.get(), wayland_window_manager(), this);

  wl_registry_add_listener(registry_.get(), &registry_listener, this);
  while (!wayland_output_manager_ ||
         !wayland_output_manager_->IsOutputReady()) {
    RoundTripQueue();
  }

  buffer_manager_host_ = std::make_unique<WaylandBufferManagerHost>(this);

  if (!compositor_) {
    LOG(ERROR) << "No wl_compositor object";
    return false;
  }
  if (!shm_) {
    LOG(ERROR) << "No wl_shm object";
    return false;
  }
  if (!shell_v6_ && !shell_) {
    LOG(ERROR) << "No Wayland shell found";
    return false;
  }

  // When we are running tests with weston in headless mode, the seat is not
  // announced.
  if (!seat_)
    LOG(WARNING) << "No wl_seat object. The functionality may suffer.";

  if (UseTestConfigForPlatformWindows())
    wayland_proxy_ = std::make_unique<wl::WaylandProxyImpl>(this);
  return true;
}

void WaylandConnection::ScheduleFlush() {
  // When we are in tests, the message loop is set later when the
  // initialization of the OzonePlatform complete. Thus, just
  // flush directly. This doesn't happen in normal run.
  if (!base::CurrentUIThread::IsSet()) {
    Flush();
  } else if (!scheduled_flush_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&WaylandConnection::Flush, base::Unretained(this)));
    scheduled_flush_ = true;
  }
}

void WaylandConnection::RoundTripQueue() {
  if (roundtrip_closure_for_testing_) {
    roundtrip_closure_for_testing_.Run();
    return;
  }

  DCHECK(event_queue_.get());
  wl_display_roundtrip_queue(display(), event_queue_.get());
}

void WaylandConnection::SetShutdownCb(base::OnceCallback<void()> shutdown_cb) {
  event_source()->SetShutdownCb(std::move(shutdown_cb));
}

void WaylandConnection::SetPlatformCursor(wl_cursor* cursor_data,
                                          int buffer_scale) {
  if (!cursor_)
    return;
  cursor_->SetPlatformShape(cursor_data, buffer_scale);
}

void WaylandConnection::SetCursorBufferListener(
    WaylandCursorBufferListener* listener) {
  listener_ = listener;
  if (!cursor_)
    return;
  cursor_->set_listener(listener_);
}

void WaylandConnection::SetCursorBitmap(const std::vector<SkBitmap>& bitmaps,
                                        const gfx::Point& hotspot_in_dips,
                                        int buffer_scale) {
  if (!cursor_)
    return;
  cursor_->UpdateBitmap(bitmaps, hotspot_in_dips, buffer_scale);
}

bool WaylandConnection::IsDragInProgress() const {
  // |data_drag_controller_| can be null when running on headless weston.
  return (data_drag_controller_ &&
          data_drag_controller_->state() !=
              WaylandDataDragController::State::kIdle) ||
         (window_drag_controller_ &&
          window_drag_controller_->state() !=
              WaylandWindowDragController::State::kIdle);
}

bool WaylandConnection::SupportsSetWindowGeometry() const {
  return shell_ || shell_v6_;
}

wl::Object<wl_surface> WaylandConnection::CreateSurface() {
  DCHECK(compositor_);
  return wl::Object<wl_surface>(
      wl_compositor_create_surface(compositor_.get()));
}

void WaylandConnection::RegisterGlobalObjectFactory(
    const char* interface_name,
    wl::GlobalObjectFactory factory) {
  DCHECK_EQ(global_object_factories_.count(interface_name), 0U);

  global_object_factories_[interface_name] = factory;
}

void WaylandConnection::Flush() {
  wl_display_flush(display_.get());
  scheduled_flush_ = false;
}

void WaylandConnection::UpdateInputDevices() {
  // Container for devices. Can be empty.
  std::vector<InputDevice> devices;

  if (seat_->pointer()) {
    cursor_ = std::make_unique<WaylandCursor>(seat_->pointer(), this);
    cursor_->set_listener(listener_);
    wayland_cursor_position_ = std::make_unique<WaylandCursorPosition>();

    // Wayland doesn't expose InputDeviceType.
    devices.emplace_back(InputDevice(seat_->pointer()->id(),
                                     InputDeviceType::INPUT_DEVICE_UNKNOWN,
                                     "pointer"));

    // Pointer is required for PointerGestures to be functional.
    if (wayland_zwp_pointer_gestures_)
      wayland_zwp_pointer_gestures_->Init();
  } else {
    cursor_.reset();
    wayland_cursor_position_.reset();
  }

  // Notify about mouse changes.
  GetHotplugEventObserver()->OnMouseDevicesUpdated(devices);

  // Clear the local container to store a keyboard device now.
  devices.clear();
  if (seat_->keyboard()) {
    // Wayland doesn't expose InputDeviceType.
    devices.emplace_back(InputDevice(seat_->keyboard()->id(),
                                     InputDeviceType::INPUT_DEVICE_UNKNOWN,
                                     "keyboard"));
  }

  // Notify about keyboard changes.
  GetHotplugEventObserver()->OnKeyboardDevicesUpdated(devices);

  // TODO(msisov): wl_touch doesn't expose the display it belongs to. Thus, it's
  // impossible to figure out the size of the touchscreen for TouchscreenDevice
  // struct that should be passed to a DeviceDataManager instance.

  // Notify update completed.
  GetHotplugEventObserver()->OnDeviceListsComplete();
}

DeviceHotplugEventObserver* WaylandConnection::GetHotplugEventObserver() {
  return DeviceDataManager::GetInstance();
}

void WaylandConnection::CreateDataObjectsIfReady() {
  if (data_device_manager_ && seat_) {
    DCHECK(!data_drag_controller_);
    data_drag_controller_ = std::make_unique<WaylandDataDragController>(
        this, data_device_manager_.get(), event_source(), event_source());

    DCHECK(!window_drag_controller_);
    window_drag_controller_ = std::make_unique<WaylandWindowDragController>(
        this, data_device_manager_.get(), event_source(), event_source());

    DCHECK(!clipboard_);
    clipboard_ =
        std::make_unique<WaylandClipboard>(this, data_device_manager_.get());
  }
}

// static
void WaylandConnection::Global(void* data,
                               wl_registry* registry,
                               uint32_t name,
                               const char* interface,
                               uint32_t version) {
  static constexpr xdg_wm_base_listener shell_listener = {
      &Ping,
  };
  static constexpr zxdg_shell_v6_listener shell_v6_listener = {
      &PingV6,
  };
  static constexpr wp_presentation_listener presentation_listener = {
      &ClockId,
  };

  WaylandConnection* connection = static_cast<WaylandConnection*>(data);

  auto factory_it = connection->global_object_factories_.find(interface);
  if (factory_it != connection->global_object_factories_.end()) {
    (*factory_it->second)(connection, registry, name, interface, version);
  } else if (!connection->compositor_ &&
             strcmp(interface, "wl_compositor") == 0) {
    connection->compositor_ = wl::Bind<wl_compositor>(
        registry, name, std::min(version, kMaxCompositorVersion));
    connection->compositor_version_ = version;
    if (!connection->compositor_) {
      LOG(ERROR) << "Failed to bind to wl_compositor global";
      return;
    }
  } else if (!connection->subcompositor_ &&
             strcmp(interface, "wl_subcompositor") == 0) {
    connection->subcompositor_ = wl::Bind<wl_subcompositor>(registry, name, 1);
    if (!connection->subcompositor_) {
      LOG(ERROR) << "Failed to bind to wl_subcompositor global";
      return;
    }
  } else if (!connection->shell_v6_ &&
             strcmp(interface, "zxdg_shell_v6") == 0) {
    // Check for zxdg_shell_v6 first.
    connection->shell_v6_ = wl::Bind<zxdg_shell_v6>(
        registry, name, std::min(version, kMaxZXdgShellVersion));
    if (!connection->shell_v6_) {
      LOG(ERROR) << "Failed to bind to zxdg_shell_v6 global";
      return;
    }
    zxdg_shell_v6_add_listener(connection->shell_v6_.get(), &shell_v6_listener,
                               connection);
    ReportShellUMA(UMALinuxWaylandShell::kXdgShellV6);
  } else if (!connection->shell_ && strcmp(interface, "xdg_wm_base") == 0) {
    connection->shell_ = wl::Bind<xdg_wm_base>(
        registry, name, std::min(version, kMaxXdgShellVersion));
    if (!connection->shell_) {
      LOG(ERROR) << "Failed to bind to xdg_wm_base global";
      return;
    }
    xdg_wm_base_add_listener(connection->shell_.get(), &shell_listener,
                             connection);
    ReportShellUMA(UMALinuxWaylandShell::kXdgWmBase);
  } else if (!connection->alpha_compositing_ &&
             (strcmp(interface, "zcr_alpha_compositing_v1") == 0)) {
    connection->alpha_compositing_ = wl::Bind<zcr_alpha_compositing_v1>(
        registry, name, std::min(version, kMaxAlphaCompositingVersion));
    if (!connection->alpha_compositing_) {
      LOG(ERROR) << "Failed to bind zcr_alpha_compositing_v1";
      return;
    }
  } else if (!connection->linux_explicit_synchronization_ &&
             (strcmp(interface, "zwp_linux_explicit_synchronization_v1") ==
              0)) {
    connection->linux_explicit_synchronization_ =
        wl::Bind<zwp_linux_explicit_synchronization_v1>(
            registry, name, std::min(version, kMaxExplicitSyncVersion));
    if (!connection->linux_explicit_synchronization_) {
      LOG(ERROR) << "Failed to bind zwp_linux_explicit_synchronization_v1";
      return;
    }
  } else if (!connection->presentation_ &&
             (strcmp(interface, "wp_presentation") == 0)) {
    connection->presentation_ = wl::Bind<wp_presentation>(
        registry, name, std::min(version, kMaxWpPresentationVersion));
    if (!connection->presentation_) {
      LOG(ERROR) << "Failed to bind wp_presentation";
      return;
    }
    wp_presentation_add_listener(connection->presentation_.get(),
                                 &presentation_listener, connection);
  } else if (!connection->viewporter_ &&
             (strcmp(interface, "wp_viewporter") == 0)) {
    connection->viewporter_ = wl::Bind<wp_viewporter>(
        registry, name, std::min(version, kMaxWpViewporterVersion));
    if (!connection->viewporter_) {
      LOG(ERROR) << "Failed to bind wp_viewporter";
      return;
    }
  } else if (!connection->keyboard_extension_v1_ &&
             strcmp(interface, "zcr_keyboard_extension_v1") == 0) {
    connection->keyboard_extension_v1_ = wl::Bind<zcr_keyboard_extension_v1>(
        registry, name, std::min(version, kMaxKeyboardExtensionVersion));
    if (!connection->keyboard_extension_v1_) {
      LOG(ERROR) << "Failed to bind zcr_keyboard_extension_v1";
      return;
    }
    // CreateKeyboard may fail if we do not have keyboard seat capabilities yet.
    // We will create the keyboard when get them in that case.
    if (connection->seat_)
      connection->seat_->RefreshKeyboard();
  } else if (!connection->text_input_manager_v1_ &&
             strcmp(interface, "zwp_text_input_manager_v1") == 0) {
    connection->text_input_manager_v1_ = wl::Bind<zwp_text_input_manager_v1>(
        registry, name, std::min(version, kMaxTextInputManagerVersion));
    if (!connection->text_input_manager_v1_) {
      LOG(ERROR) << "Failed to bind to zwp_text_input_manager_v1 global";
      return;
    }
  } else if (!connection->text_input_extension_v1_ &&
             strcmp(interface, "zcr_text_input_extension_v1") == 0) {
    connection->text_input_extension_v1_ =
        wl::Bind<zcr_text_input_extension_v1>(
            registry, name, std::min(version, kMaxTextInputExtensionVersion));
  } else if (!connection->xdg_decoration_manager_ &&
             strcmp(interface, "zxdg_decoration_manager_v1") == 0) {
    connection->xdg_decoration_manager_ =
        wl::Bind<struct zxdg_decoration_manager_v1>(
            registry, name, std::min(version, kMaxXdgDecorationVersion));
    if (!connection->xdg_decoration_manager_) {
      LOG(ERROR) << "Failed to bind zxdg_decoration_manager_v1";
      return;
    }
  } else if (!connection->extended_drag_v1_ &&
             strcmp(interface, "zcr_extended_drag_v1") == 0) {
    connection->extended_drag_v1_ = wl::Bind<zcr_extended_drag_v1>(
        registry, name, std::min(version, kMaxExtendedDragVersion));
    if (!connection->extended_drag_v1_) {
      LOG(ERROR) << "Failed to bind to zcr_extended_drag_v1 global";
      return;
    }
  } else if (!connection->xdg_output_manager_ &&
             strcmp(interface, "zxdg_output_manager_v1") == 0) {
    connection->xdg_output_manager_ = wl::Bind<struct zxdg_output_manager_v1>(
        registry, name, std::min(version, kMaxXdgOutputManagerVersion));
    if (!connection->xdg_output_manager_) {
      LOG(ERROR) << "Failed to bind zxdg_outout_manager_v1";
      return;
    }
    if (connection->wayland_output_manager_)
      connection->wayland_output_manager_->InitializeAllXdgOutputs();
  } else if (strcmp(interface, "org_kde_plasma_shell") == 0) {
    NOTIMPLEMENTED_LOG_ONCE()
        << interface << " is recognized but not yet supported";
    ReportShellUMA(UMALinuxWaylandShell::kOrgKdePlasmaShell);
  } else if (strcmp(interface, "zwlr_layer_shell_v1") == 0) {
    NOTIMPLEMENTED_LOG_ONCE()
        << interface << " is recognized but not yet supported";
    ReportShellUMA(UMALinuxWaylandShell::kZwlrLayerShellV1);
  }

  connection->available_globals_.emplace_back(interface, version);

  connection->ScheduleFlush();
}

base::TimeTicks WaylandConnection::ConvertPresentationTime(uint32_t tv_sec_hi,
                                                           uint32_t tv_sec_lo,
                                                           uint32_t tv_nsec) {
  DCHECK(presentation());
  // base::TimeTicks::Now() uses CLOCK_MONOTONIC, no need to convert clock
  // domain if wp_presentation also uses it.
  if (presentation_clk_id_ == CLOCK_MONOTONIC) {
    return base::TimeTicks() + base::Microseconds(ConvertTimespecResultToMicros(
                                   tv_sec_hi, tv_sec_lo, tv_nsec));
  }

  struct timespec presentation_now;
  base::TimeTicks now = base::TimeTicks::Now();
  int ret = clock_gettime(presentation_clk_id_, &presentation_now);

  if (ret < 0) {
    presentation_now.tv_sec = 0;
    presentation_now.tv_nsec = 0;
    LOG(ERROR) << "Error: failure to read the wp_presentation clock "
               << presentation_clk_id_ << ": '" << strerror(errno) << "' "
               << errno;
    return base::TimeTicks::Now();
  }

  int64_t delta_us =
      ConvertTimespecResultToMicros(tv_sec_hi, tv_sec_lo, tv_nsec) -
      ConvertTimespecToMicros(presentation_now);

  return now + base::Microseconds(delta_us);
}

// static
void WaylandConnection::GlobalRemove(void* data,
                                     wl_registry* registry,
                                     uint32_t name) {
  WaylandConnection* connection = static_cast<WaylandConnection*>(data);
  // The Wayland protocol distinguishes global objects by unique numeric names,
  // which the WaylandOutputManager uses as unique output ids. But, it is only
  // possible to figure out, what global object is going to be removed on the
  // WaylandConnection::GlobalRemove call. Thus, whatever unique |name| comes,
  // it's forwarded to the WaylandOutputManager, which checks if such a global
  // output object exists and removes it.
  if (connection->wayland_output_manager_)
    connection->wayland_output_manager_->RemoveWaylandOutput(name);
}

// static
void WaylandConnection::PingV6(void* data,
                               zxdg_shell_v6* shell_v6,
                               uint32_t serial) {
  WaylandConnection* connection = static_cast<WaylandConnection*>(data);
  zxdg_shell_v6_pong(shell_v6, serial);
  connection->ScheduleFlush();
}

// static
void WaylandConnection::Ping(void* data, xdg_wm_base* shell, uint32_t serial) {
  WaylandConnection* connection = static_cast<WaylandConnection*>(data);
  xdg_wm_base_pong(shell, serial);
  connection->ScheduleFlush();
}

// static
void WaylandConnection::ClockId(void* data,
                                wp_presentation* shell_v6,
                                uint32_t clk_id) {
  DCHECK_EQ(base::TimeTicks::GetClock(),
            base::TimeTicks::Clock::LINUX_CLOCK_MONOTONIC);
  WaylandConnection* connection = static_cast<WaylandConnection*>(data);
  connection->presentation_clk_id_ = clk_id;
}

}  // namespace ui
