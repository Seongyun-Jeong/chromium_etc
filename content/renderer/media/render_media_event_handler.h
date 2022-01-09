// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_RENDER_MEDIA_EVENT_HANDLER_H_
#define CONTENT_RENDERER_MEDIA_RENDER_MEDIA_EVENT_HANDLER_H_

#include <vector>

#include "content/common/media/media_log_records.mojom.h"
#include "content/renderer/media/batching_media_log.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace content {

// RenderMediaEventHandler is an implementation of
// BatchingMediaLog::EventHandler that forwards events to the browser process.
class RenderMediaEventHandler : public BatchingMediaLog::EventHandler {
 public:
  RenderMediaEventHandler();
  ~RenderMediaEventHandler() override;
  void SendQueuedMediaEvents(std::vector<media::MediaLogRecord>) override;
  void OnWebMediaPlayerDestroyed() override;

 private:
  content::mojom::MediaInternalLogRecords& GetMediaInternalRecordLogRemote();
  mojo::Remote<content::mojom::MediaInternalLogRecords>
      media_internal_log_remote_;
};

}  // namespace content

#endif  // CONTENT_RENDERER_MEDIA_RENDER_MEDIA_EVENT_HANDLER_H_
