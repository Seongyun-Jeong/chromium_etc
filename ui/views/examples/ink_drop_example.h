// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_EXAMPLES_INK_DROP_EXAMPLE_H_
#define UI_VIEWS_EXAMPLES_INK_DROP_EXAMPLE_H_

#include "base/callback.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_state.h"
#include "ui/views/examples/example_base.h"

namespace views {
namespace examples {

class VIEWS_EXAMPLES_EXPORT InkDropExample : public ExampleBase {
 public:
  InkDropExample();
  InkDropExample(const InkDropExample&) = delete;
  InkDropExample& operator=(const InkDropExample&) = delete;
  ~InkDropExample() override;

  // ExampleBase:
  void CreateExampleView(View* container) override;

 protected:
  explicit InkDropExample(const char* title);

  virtual void CreateInkDrop();

  View* ink_drop_view() { return ink_drop_view_; }

 private:
  void SetInkDropState(InkDropState state);

  View* ink_drop_view_ = nullptr;
};

}  // namespace examples
}  // namespace views

#endif  // UI_VIEWS_EXAMPLES_INK_DROP_EXAMPLE_H_
