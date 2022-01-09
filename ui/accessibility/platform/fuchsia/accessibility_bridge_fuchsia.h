// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_ACCESSIBILITY_PLATFORM_FUCHSIA_ACCESSIBILITY_BRIDGE_FUCHSIA_H_
#define UI_ACCESSIBILITY_PLATFORM_FUCHSIA_ACCESSIBILITY_BRIDGE_FUCHSIA_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>

#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/accessibility/ax_export.h"

namespace ui {

// Interface for clients to interact with fuchsia's platform accessibility
// framework.
class AX_EXPORT AccessibilityBridgeFuchsia {
 public:
  virtual ~AccessibilityBridgeFuchsia() = default;

  // Translates AXNodeDescriptorFuchsias to fuchsia IDs, fills the
  // corresponding fields in |node_update.node_data|, and sends the update
  // to fuchsia.
  //
  // Note that |node_update.node_data| should not have any node ID fields
  // (node_id, child_ids, offset_container_id, etc.) filled initially.
  virtual void UpdateNode(fuchsia::accessibility::semantics::Node node) = 0;

  // Translates |node_id| to a fuchsia node ID, and sends the deletion to
  // fuchsia.
  virtual void DeleteNode(uint32_t node_id) = 0;

  // Sets focus to the fuchsia node specified by |new_focus|.
  virtual void FocusNode(uint32_t new_focus) = 0;

  // Removes focus from the fuchsia node specified by |old_focus|.
  virtual void UnfocusNode(uint32_t old_focus) = 0;

  // hit_test_request_id: A unique ID for the hit test, generated by the client.
  // result: The fuchsia node ID of the entity returned by the hit test.
  //
  // Method to notify the accessibility bridge when a hit test result is
  // received.
  virtual void OnAccessibilityHitTestResult(
      int hit_test_request_id,
      absl::optional<uint32_t> result) = 0;

  // Returns the device scale factor.
  virtual float GetDeviceScaleFactor() = 0;

  // Specifies the unique ID of the root platform node.
  virtual void SetRootID(uint32_t root_node_id) = 0;
};

}  // namespace ui

#endif  // UI_ACCESSIBILITY_PLATFORM_FUCHSIA_ACCESSIBILITY_BRIDGE_FUCHSIA_H_
