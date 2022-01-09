// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_EXO_SURFACE_OBSERVER_H_
#define COMPONENTS_EXO_SURFACE_OBSERVER_H_

#include <cstdint>

namespace exo {
class Surface;

// Observers can listen to various events on the Surfaces.
class SurfaceObserver {
 public:
  // Called at the top of the surface's destructor, to give observers a
  // chance to remove themselves.
  virtual void OnSurfaceDestroying(Surface* surface) = 0;

  // Called when the occlusion of the aura window corresponding to |surface|
  // changes.
  virtual void OnWindowOcclusionChanged(Surface* surface) {}

  // Called when frame is locked to normal state or unlocked from
  // previously locked state.
  virtual void OnFrameLockingChanged(Surface* surface, bool lock) {}

  // Called on each commit.
  virtual void OnCommit(Surface* surface) {}

  // Called when the content size changes.
  virtual void OnContentSizeChanged(Surface* surface) {}

  // Called when desk state of the window changes.
  // |state| is the index of the desk which the window moved to,
  // or -1 for a window assigned to all desks.
  virtual void OnDeskChanged(Surface* surface, int state) {}

  // Called when the display of this surface has changed. Only called after
  // successfully updating sub-surfaces.
  virtual void OnDisplayChanged(Surface* surface,
                                int64_t old_display,
                                int64_t new_display) {}

 protected:
  virtual ~SurfaceObserver() {}
};

}  // namespace exo

#endif  // COMPONENTS_EXO_SURFACE_OBSERVER_H_
