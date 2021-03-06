// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_HIT_TEST_DEBUG_KEY_EVENT_OBSERVER_H_
#define CONTENT_BROWSER_RENDERER_HOST_HIT_TEST_DEBUG_KEY_EVENT_OBSERVER_H_

#include "base/memory/raw_ptr.h"
#include "content/public/browser/render_widget_host.h"

namespace viz {

class HitTestQuery;

}  // namespace viz

namespace content {

class RenderWidgetHostImpl;

// Implements the RenderWidgetHost::InputEventObserver interface, and acts on
// keyboard input events to print hit-test data.
class HitTestDebugKeyEventObserver
    : public RenderWidgetHost::InputEventObserver {
 public:
  explicit HitTestDebugKeyEventObserver(RenderWidgetHostImpl* host);
  ~HitTestDebugKeyEventObserver() override;

  // RenderWidgetHost::InputEventObserver:
  void OnInputEventAck(blink::mojom::InputEventResultSource source,
                       blink::mojom::InputEventResultState state,
                       const blink::WebInputEvent&) override;

 private:
  raw_ptr<RenderWidgetHostImpl> host_;
  raw_ptr<viz::HitTestQuery> hit_test_query_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_HIT_TEST_DEBUG_KEY_EVENT_OBSERVER_H_
