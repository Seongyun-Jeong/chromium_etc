// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_IME_ASH_IME_CANDIDATE_WINDOW_HANDLER_INTERFACE_H_
#define UI_BASE_IME_ASH_IME_CANDIDATE_WINDOW_HANDLER_INTERFACE_H_

#include <stdint.h>

#include <string>

#include "base/component_export.h"

namespace gfx {
class Rect;
}  // namespace gfx

namespace ui {
class CandidateWindow;
}  // namespace ui

namespace ash {

// A interface to handle the candidate window related method call.
class COMPONENT_EXPORT(UI_BASE_IME_ASH) IMECandidateWindowHandlerInterface {
 public:
  virtual ~IMECandidateWindowHandlerInterface() = default;

  // Called when the IME updates the lookup table.
  virtual void UpdateLookupTable(const ui::CandidateWindow& candidate_window,
                                 bool visible) = 0;

  // Called when the IME updates the preedit text. The |text| is given in
  // UTF-16 encoding.
  virtual void UpdatePreeditText(const std::u16string& text,
                                 uint32_t cursor_pos,
                                 bool visible) = 0;

  // Called when the application changes its caret bounds.
  virtual void SetCursorBounds(const gfx::Rect& cursor_bounds,
                               const gfx::Rect& composition_head) = 0;

  // Gets the cursor bounds that was set by |SetCursorBounds| method.
  virtual gfx::Rect GetCursorBounds() const = 0;

  // Called when the text field's focus state is changed.
  // |is_focused| is true when the text field gains the focus.
  virtual void FocusStateChanged(bool is_focused) {}

 protected:
  IMECandidateWindowHandlerInterface() = default;
};

}  // namespace ash

#endif  // UI_BASE_IME_ASH_IME_CANDIDATE_WINDOW_HANDLER_INTERFACE_H_
