// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/common/switches.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "build/chromeos_buildflags.h"
#include "components/viz/common/constants.h"

namespace switches {

// Screen width is useful for debugging. Shipping implementations should detect
// this.
const char kDeJellyScreenWidth[] = "de-jelly-screen-width";

// The default number of the BeginFrames to wait to activate a surface with
// dependencies.
const char kDeadlineToSynchronizeSurfaces[] =
    "deadline-to-synchronize-surfaces";

// Disables begin frame limiting in both cc scheduler and display scheduler.
// Also implies --disable-gpu-vsync (see //ui/gl/gl_switches.h).
const char kDisableFrameRateLimit[] = "disable-frame-rate-limit";

// Slows down animations during a DocumentTransition for debugging.
const char kDocumentTransitionSlowdownFactor[] =
    "document-transition-slowdown-factor";

// Sets the number of max pending frames in the GL buffer queue to 1.
const char kDoubleBufferCompositing[] = "double-buffer-compositing";

// Experimental de-jelly support.
const char kEnableDeJelly[] = "enable-de-jelly";

// Enable compositing individual elements via hardware overlays when
// permitted by device.
// Setting the flag to "single-fullscreen" will try to promote a single
// fullscreen overlay and use it as main framebuffer where possible.
const char kEnableHardwareOverlays[] = "enable-hardware-overlays";

// Enables hit-test debug logging.
const char kEnableVizHitTestDebug[] = "enable-viz-hit-test-debug";

#if defined(OS_CHROMEOS)
// ChromeOS uses one of two VideoDecoder implementations based on SoC/board
// specific configurations that are signalled via this command line flag.
// TODO(b/159825227): remove when the "old" video decoder is fully launched.
const char kPlatformDisallowsChromeOSDirectVideoDecoder[] =
    "platform-disallows-chromeos-direct-video-decoder";
#endif

// Effectively disables pipelining of compositor frame production stages by
// waiting for each stage to finish before completing a frame.
const char kRunAllCompositorStagesBeforeDraw[] =
    "run-all-compositor-stages-before-draw";

// Adds a DebugBorderDrawQuad to the top of the root RenderPass showing the
// damage rect after surface aggregation. Note that when enabled this feature
// sets the entire output rect as damaged after adding the quad to highlight the
// real damage rect, which could hide damage rect problems.
const char kShowAggregatedDamage[] = "show-aggregated-damage";

// Modulates the debug compositor tint color so that damage and page flip
// updates are made clearly visible. This feature was useful in determining the
// root cause of https://b.corp.google.com/issues/183260320 . The tinting flag
// "tint-composited-content" must also be enabled for this flag to used.
const char kTintCompositedContentModulate[] =
    "tint-composited-content-modulate";

// Show debug borders for DC layers - red for overlays and blue for underlays.
// The debug borders are offset from the layer rect by a few pixels for clarity.
const char kShowDCLayerDebugBorders[] = "show-dc-layer-debug-borders";

absl::optional<uint32_t> GetDeadlineToSynchronizeSurfaces() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kRunAllCompositorStagesBeforeDraw)) {
    // In full-pipeline mode, surface deadlines should always be unlimited.
    return absl::nullopt;
  }
  std::string deadline_to_synchronize_surfaces_string =
      command_line->GetSwitchValueASCII(
          switches::kDeadlineToSynchronizeSurfaces);
  if (deadline_to_synchronize_surfaces_string.empty())
    return viz::kDefaultActivationDeadlineInFrames;

  uint32_t activation_deadline_in_frames;
  if (!base::StringToUint(deadline_to_synchronize_surfaces_string,
                          &activation_deadline_in_frames)) {
    return absl::nullopt;
  }
  return activation_deadline_in_frames;
}

int GetDocumentTransitionSlowDownFactor() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(kDocumentTransitionSlowdownFactor))
    return 1;

  auto factor_str =
      command_line->GetSwitchValueASCII(kDocumentTransitionSlowdownFactor);
  int factor = 0;
  LOG_IF(ERROR, !base::StringToInt(factor_str, &factor))
      << "Error parsing document transition slow down factor " << factor_str;
  return std::max(1, factor);
}

}  // namespace switches
