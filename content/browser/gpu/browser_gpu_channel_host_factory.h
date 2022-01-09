// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_GPU_BROWSER_GPU_CHANNEL_HOST_FACTORY_H_
#define CONTENT_BROWSER_GPU_BROWSER_GPU_CHANNEL_HOST_FACTORY_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/task/single_thread_task_runner.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "ipc/message_filter.h"

namespace gpu {
class GpuMemoryBufferManager;
}

namespace content {

class BrowserGpuChannelHostFactory : public gpu::GpuChannelEstablishFactory {
 public:
  static void Initialize(bool establish_gpu_channel);
  static void Terminate();
  static BrowserGpuChannelHostFactory* instance() { return instance_; }

  BrowserGpuChannelHostFactory(const BrowserGpuChannelHostFactory&) = delete;
  BrowserGpuChannelHostFactory& operator=(const BrowserGpuChannelHostFactory&) =
      delete;

  gpu::GpuChannelHost* GetGpuChannel();
  int GetGpuChannelId() { return gpu_client_id_; }

  // Close the channel if there is no usage other usage of the channel.
  // Note this is different from |CloseChannel| as this can be called at
  // any point. The next EstablishGpuChannel will simply return a new channel.
  void MaybeCloseChannel();

  // Closes the channel to the GPU process. This should be called before the IO
  // thread stops.
  void CloseChannel();

  // Notify the BrowserGpuChannelHostFactory of visibility, used to prevent
  // timeouts while backgrounded.
  void SetApplicationVisible(bool is_visible);

  // Overridden from gpu::GpuChannelEstablishFactory:
  // The factory will return a null GpuChannelHost in the callback during
  // shutdown.
  void EstablishGpuChannel(
      gpu::GpuChannelEstablishedCallback callback) override;
  scoped_refptr<gpu::GpuChannelHost> EstablishGpuChannelSync() override;
  gpu::GpuMemoryBufferManager* GetGpuMemoryBufferManager() override;

 private:
  class EstablishRequest;

  BrowserGpuChannelHostFactory();
  ~BrowserGpuChannelHostFactory() override;

  void EstablishGpuChannel(gpu::GpuChannelEstablishedCallback callback,
                           bool sync);

  void GpuChannelEstablished(EstablishRequest* request);
  void RestartTimeout();

  static void InitializeShaderDiskCacheOnIO(int gpu_client_id,
                                            const base::FilePath& cache_dir);
  static void InitializeGrShaderDiskCacheOnIO(const base::FilePath& cache_dir);

  const int gpu_client_id_;
  const uint64_t gpu_client_tracing_id_;
  scoped_refptr<gpu::GpuChannelHost> gpu_channel_;
  std::unique_ptr<gpu::GpuMemoryBufferManager> gpu_memory_buffer_manager_;
  scoped_refptr<EstablishRequest> pending_request_;
  bool is_visible_ = true;

  base::OneShotTimer timeout_;

  static BrowserGpuChannelHostFactory* instance_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_GPU_BROWSER_GPU_CHANNEL_HOST_FACTORY_H_
