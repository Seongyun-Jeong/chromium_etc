// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_RENDER_THREAD_IMPL_H_
#define CONTENT_RENDERER_RENDER_THREAD_IMPL_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/cancelable_callback.h"
#include "base/clang_profiling_buildflags.h"
#include "base/containers/unique_ptr_adapters.h"
#include "base/gtest_prod_util.h"
#include "base/memory/discardable_memory_allocator.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/user_metrics_action.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "base/types/pass_key.h"
#include "build/build_config.h"
#include "content/child/child_thread_impl.h"
#include "content/common/agent_scheduling_group.mojom.h"
#include "content/common/content_export.h"
#include "content/common/frame.mojom.h"
#include "content/common/render_message_filter.mojom.h"
#include "content/common/renderer.mojom.h"
#include "content/common/renderer_host.mojom.h"
#include "content/public/renderer/render_thread.h"
#include "content/renderer/discardable_memory_utils.h"
#include "content/services/shared_storage_worklet/public/mojom/shared_storage_worklet_service.mojom.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "ipc/ipc_sync_channel.h"
#include "media/media_buildflags.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/network_change_notifier.h"
#include "net/nqe/effective_connection_type.h"
#include "services/viz/public/mojom/compositing/compositing_mode_watcher.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/blink/public/platform/scheduler/web_rail_mode_observer.h"
#include "third_party/blink/public/platform/scheduler/web_thread_scheduler.h"
#include "third_party/blink/public/platform/url_loader_throttle_provider.h"
#include "third_party/blink/public/platform/web_connection_type.h"
#include "third_party/blink/public/web/web_memory_statistics.h"
#include "ui/gfx/native_widget_types.h"

class SkBitmap;

namespace blink {
class WebResourceRequestSenderDelegate;
class WebVideoCaptureImplManager;
}

namespace base {
class SingleThreadTaskRunner;
class Thread;
}

namespace cc {
class TaskGraphRunner;
}

namespace gpu {
class GpuChannelHost;
}

namespace media {
class DecoderFactory;
class GpuVideoAcceleratorFactories;
}

namespace mojo {
class BinderMap;
}

namespace viz {
class ContextProviderCommandBuffer;
class Gpu;
class RasterContextProvider;
}  // namespace viz

namespace content {
class AgentSchedulingGroup;
class CategorizedWorkerPool;
class GpuVideoAcceleratorFactoriesImpl;
class MediaInterfaceFactory;
class RenderFrameImpl;
class RenderThreadObserver;
class RendererBlinkPlatformImpl;
class VariationsRenderThreadObserver;

#if defined(OS_ANDROID)
class StreamTextureFactory;
#endif

#if defined(OS_WIN)
class DCOMPTextureFactory;
#endif

// The RenderThreadImpl class represents the main thread, where RenderView
// instances live.  The RenderThread supports an API that is used by its
// consumer to talk indirectly to the RenderViews and supporting objects.
// Likewise, it provides an API for the RenderViews to talk back to the main
// process (i.e., their corresponding WebContentsImpl).
//
// Most of the communication occurs in the form of IPC messages.  They are
// routed to the RenderThread according to the routing IDs of the messages.
// The routing IDs correspond to RenderView instances.
class CONTENT_EXPORT RenderThreadImpl
    : public RenderThread,
      public ChildThreadImpl,
      public mojom::Renderer,
      public viz::mojom::CompositingModeWatcher {
 public:
  static RenderThreadImpl* current();
  static mojom::RenderMessageFilter* current_render_message_filter();
  static RendererBlinkPlatformImpl* current_blink_platform_impl();

  static void SetRenderMessageFilterForTesting(
      mojom::RenderMessageFilter* render_message_filter);
  static void SetRendererBlinkPlatformImplForTesting(
      RendererBlinkPlatformImpl* blink_platform_impl);

  // Returns the task runner for the main thread where the RenderThread lives.
  static scoped_refptr<base::SingleThreadTaskRunner>
  DeprecatedGetMainTaskRunner();

  RenderThreadImpl(
      base::RepeatingClosure quit_closure,
      std::unique_ptr<blink::scheduler::WebThreadScheduler> scheduler);
  RenderThreadImpl(
      const InProcessChildThreadParams& params,
      int32_t client_id,
      std::unique_ptr<blink::scheduler::WebThreadScheduler> scheduler);

  RenderThreadImpl(const RenderThreadImpl&) = delete;
  RenderThreadImpl& operator=(const RenderThreadImpl&) = delete;

  ~RenderThreadImpl() override;
  void Shutdown() override;
  bool ShouldBeDestroyed() override;

  // When initializing WebKit, ensure that any schemes needed for the content
  // module are registered properly.  Static to allow sharing with tests.
  static void RegisterSchemes();

  // RenderThread implementation:
  IPC::SyncChannel* GetChannel() override;
  std::string GetLocale() override;
  IPC::SyncMessageFilter* GetSyncMessageFilter() override;
  void AddRoute(int32_t routing_id, IPC::Listener* listener) override;
  void AttachTaskRunnerToRoute(
      int32_t routing_id,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner) override;
  void RemoveRoute(int32_t routing_id) override;
  int GenerateRoutingID() override;
  bool GenerateFrameRoutingID(
      int32_t& routing_id,
      blink::LocalFrameToken& frame_token,
      base::UnguessableToken& devtools_frame_token) override;
  void AddFilter(IPC::MessageFilter* filter) override;
  void RemoveFilter(IPC::MessageFilter* filter) override;
  void AddObserver(RenderThreadObserver* observer) override;
  void RemoveObserver(RenderThreadObserver* observer) override;
  void SetResourceRequestSenderDelegate(
      blink::WebResourceRequestSenderDelegate* delegate) override;
  blink::WebResourceRequestSenderDelegate* GetResourceRequestSenderDelegate() {
    return resource_request_sender_delegate_;
  }
  void RegisterExtension(std::unique_ptr<v8::Extension> extension) override;
  int PostTaskToAllWebWorkers(base::RepeatingClosure closure) override;
  base::WaitableEvent* GetShutdownEvent() override;
  int32_t GetClientId() override;
  void SetRendererProcessType(
      blink::scheduler::WebRendererProcessType type) override;
  blink::WebString GetUserAgent() override;
  blink::WebString GetReducedUserAgent() override;
  const blink::UserAgentMetadata& GetUserAgentMetadata() override;
  bool IsUseZoomForDSF() override;
  void WriteIntoTrace(
      perfetto::TracedProto<perfetto::protos::pbzero::RenderProcessHost> proto)
      override;

  // IPC::Listener implementation via ChildThreadImpl:
  void OnAssociatedInterfaceRequest(
      const std::string& name,
      mojo::ScopedInterfaceEndpointHandle handle) override;

  blink::scheduler::WebThreadScheduler* GetWebMainThreadScheduler();
  cc::TaskGraphRunner* GetTaskGraphRunner();
  bool IsLcdTextEnabled();
  bool IsElasticOverscrollEnabled();
  bool IsScrollAnimatorEnabled();

  // TODO(crbug.com/1111231): The `enable_scroll_animator` flag is currently
  // being passed as part of `CreateViewParams`, despite it looking like a
  // global setting. It should probably be moved to some `mojom::Renderer` API
  // and this method should be removed.
  void SetScrollAnimatorEnabled(bool enable_scroll_animator,
                                base::PassKey<AgentSchedulingGroup>);

  bool IsThreadedAnimationEnabled();

  // viz::mojom::CompositingModeWatcher implementation.
  void CompositingModeFallbackToSoftware() override;

  // Whether gpu compositing is being used or is disabled for software
  // compositing. Clients of the compositor should give resources that match
  // the appropriate mode.
  bool IsGpuCompositingDisabled() const { return is_gpu_compositing_disabled_; }

  // Synchronously establish a channel to the GPU plugin if not previously
  // established or if it has been lost (for example if the GPU plugin crashed).
  // If there is a pending asynchronous request, it will be completed by the
  // time this routine returns.
  scoped_refptr<gpu::GpuChannelHost> EstablishGpuChannelSync();

  gpu::GpuMemoryBufferManager* GetGpuMemoryBufferManager();

  blink::AssociatedInterfaceRegistry* GetAssociatedInterfaceRegistry();

  base::DiscardableMemoryAllocator* GetDiscardableMemoryAllocatorForTest()
      const {
    return discardable_memory_allocator_.get();
  }

  RendererBlinkPlatformImpl* blink_platform_impl() const {
    DCHECK(blink_platform_impl_);
    return blink_platform_impl_.get();
  }

  // Returns the task runner on the compositor thread.
  //
  // Will be null if threaded compositing has not been enabled.
  scoped_refptr<base::SingleThreadTaskRunner> compositor_task_runner() const {
    return compositor_task_runner_;
  }

  const std::vector<std::string> cors_exempt_header_list() const {
    return cors_exempt_header_list_;
  }

  blink::URLLoaderThrottleProvider* url_loader_throttle_provider() const {
    return url_loader_throttle_provider_.get();
  }

#if defined(OS_ANDROID)
  scoped_refptr<StreamTextureFactory> GetStreamTexureFactory();
  bool EnableStreamTextureCopy();
#endif

#if defined(OS_WIN)
  scoped_refptr<DCOMPTextureFactory> GetDCOMPTextureFactory();
#endif

  blink::WebVideoCaptureImplManager* video_capture_impl_manager() const {
    return vc_manager_.get();
  }

  mojom::RenderMessageFilter* render_message_filter();

  // Get the GPU channel. Returns NULL if the channel is not established or
  // has been lost.
  gpu::GpuChannelHost* GetGpuChannel();

  base::PlatformThreadId GetIOPlatformThreadId() const;

  // Returns a SingleThreadTaskRunner instance corresponding to the message loop
  // of the thread on which media operations should be run. Must be called
  // on the renderer's main thread.
  scoped_refptr<base::SingleThreadTaskRunner> GetMediaThreadTaskRunner();

  // A TaskRunner instance that runs tasks on the raster worker pool.
  base::TaskRunner* GetWorkerTaskRunner();

  // Creates a ContextProvider if yet created, and returns it to be used for
  // video frame compositing. The ContextProvider given as an argument is
  // one that has been lost, and is a hint to the RenderThreadImpl to clear
  // it's |video_frame_compositor_context_provider_| if it matches.
  scoped_refptr<viz::RasterContextProvider>
      GetVideoFrameCompositorContextProvider(
          scoped_refptr<viz::RasterContextProvider>);

  // Returns a worker context provider that will be bound on the compositor
  // thread.
  scoped_refptr<viz::RasterContextProvider>
  SharedCompositorWorkerContextProvider(bool try_gpu_rasterization);

  media::GpuVideoAcceleratorFactories* GetGpuFactories();
  media::DecoderFactory* GetMediaDecoderFactory();

  scoped_refptr<viz::ContextProviderCommandBuffer>
  SharedMainThreadContextProvider();

  // For producing custom V8 histograms. Custom histograms are produced if all
  // RenderViews share the same host, and the host is in the pre-specified set
  // of hosts we want to produce custom diagrams for. The name for a custom
  // diagram is the name of the corresponding generic diagram plus a
  // host-specific suffix.
  class CONTENT_EXPORT HistogramCustomizer {
   public:
    HistogramCustomizer();

    HistogramCustomizer(const HistogramCustomizer&) = delete;
    HistogramCustomizer& operator=(const HistogramCustomizer&) = delete;

    ~HistogramCustomizer();

    // Called when a top frame of a RenderView navigates. This function updates
    // RenderThreadImpl's information about whether all RenderViews are
    // displaying a page from the same host. |host| is the host where a
    // RenderView navigated, and |view_count| is the number of RenderViews in
    // this process.
    void RenderViewNavigatedToHost(const std::string& host, size_t view_count);

    // Used for customizing some histograms if all RenderViews share the same
    // host. Returns the current custom histogram name to use for
    // |histogram_name|, or |histogram_name| if it shouldn't be customized.
    std::string ConvertToCustomHistogramName(const char* histogram_name) const;

   private:
    FRIEND_TEST_ALL_PREFIXES(RenderThreadImplUnittest,
                             IdentifyAlexaTop10NonGoogleSite);
    friend class RenderThreadImplUnittest;

    // Converts a host name to a suffix for histograms
    std::string HostToCustomHistogramSuffix(const std::string& host);

    // Helper function to identify a certain set of top pages
    bool IsAlexaTop10NonGoogleSite(const std::string& host);

    // Used for updating the information on which is the common host which all
    // RenderView's share (if any). If there is no common host, this function is
    // called with an empty string.
    void SetCommonHost(const std::string& host);

    // The current common host of the RenderViews; empty string if there is no
    // common host.
    std::string common_host_;
    // The corresponding suffix.
    std::string common_host_histogram_suffix_;
    // Set of histograms for which we want to produce a custom histogram if
    // possible.
    std::set<std::string> custom_histograms_;
  };

  HistogramCustomizer* histogram_customizer() {
    return &histogram_customizer_;
  }

  void RegisterPendingFrameCreate(
      int routing_id,
      mojo::PendingReceiver<mojom::Frame> frame);

  mojom::RendererHost* GetRendererHost();

  struct RendererMemoryMetrics {
    size_t partition_alloc_kb;
    size_t blink_gc_kb;
    size_t malloc_mb;
    size_t discardable_kb;
    size_t v8_main_thread_isolate_mb;
    size_t total_allocated_mb;
    size_t non_discardable_total_allocated_mb;
    size_t total_allocated_per_render_view_mb;
  };
  bool GetRendererMemoryMetrics(RendererMemoryMetrics* memory_metrics) const;

  void RecordMetricsForBackgroundedRendererPurge();

  // Sets the current pipeline rendering color space.
  void SetRenderingColorSpace(const gfx::ColorSpace& color_space);

  gfx::ColorSpace GetRenderingColorSpace();

  scoped_refptr<base::SingleThreadTaskRunner>
  CreateVideoFrameCompositorTaskRunner();

  // In the case of kOnDemand, we wont be using the task_runner created in
  // CreateVideoFrameCompositorTaskRunner.
  // TODO(https://crbug/901513): Remove once kOnDemand is removed.
  void SetVideoFrameCompositorTaskRunner(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
    video_frame_compositor_task_runner_ = task_runner;
  }

  void CreateSharedStorageWorkletService(
      mojo::PendingReceiver<
          shared_storage_worklet::mojom::SharedStorageWorkletService> receiver);

  // The time the run loop started for this thread.
  base::TimeTicks run_loop_start_time() const { return run_loop_start_time_; }

  void set_run_loop_start_time(base::TimeTicks run_loop_start_time) {
    run_loop_start_time_ = run_loop_start_time;
  }

 private:
  friend class RenderThreadImplBrowserTest;
  friend class AgentSchedulingGroup;

  void OnProcessFinalRelease() override;
  // IPC::Listener
  void OnChannelError() override;

  // ChildThread
  bool OnControlMessageReceived(const IPC::Message& msg) override;
  void RecordAction(const base::UserMetricsAction& action) override;
  void RecordComputedAction(const std::string& action) override;

  bool IsMainThread();

  void Init();
  void InitializeCompositorThread();
  void InitializeWebKit(mojo::BinderMap* binders);

  void OnTransferBitmap(const SkBitmap& bitmap, int resource_id);
  void OnGetAccessibilityTree();

  // mojom::Renderer:
  void CreateAgentSchedulingGroup(
      mojo::PendingReceiver<IPC::mojom::ChannelBootstrap> bootstrap,
      mojo::PendingRemote<blink::mojom::BrowserInterfaceBroker> broker_remote)
      override;
  void CreateAssociatedAgentSchedulingGroup(
      mojo::PendingAssociatedReceiver<mojom::AgentSchedulingGroup>
          agent_scheduling_group,
      mojo::PendingRemote<blink::mojom::BrowserInterfaceBroker> broker_remote)
      override;
  void OnNetworkConnectionChanged(
      net::NetworkChangeNotifier::ConnectionType type,
      double max_bandwidth_mbps) override;
  void OnNetworkQualityChanged(net::EffectiveConnectionType type,
                               base::TimeDelta http_rtt,
                               base::TimeDelta transport_rtt,
                               double bandwidth_kbps) override;
  void SetWebKitSharedTimersSuspended(bool suspend) override;
  void SetUserAgent(const std::string& user_agent) override;
  void SetReducedUserAgent(const std::string& user_agent) override;
  void SetUserAgentMetadata(const blink::UserAgentMetadata& metadata) override;
  void SetCorsExemptHeaderList(const std::vector<std::string>& list) override;
  void UpdateScrollbarTheme(
      mojom::UpdateScrollbarThemeParamsPtr params) override;
  void OnSystemColorsChanged(int32_t aqua_color_variant,
                             const std::string& highlight_text_color,
                             const std::string& highlight_color) override;
  void UpdateSystemColorInfo(
      mojom::UpdateSystemColorInfoParamsPtr params) override;
  void PurgePluginListCache(bool reload_pages) override;
  void SetProcessState(mojom::RenderProcessBackgroundState background_state,
                       mojom::RenderProcessVisibleState visible_state) override;
  void SetIsLockedToSite() override;
#if BUILDFLAG(CLANG_PROFILING_INSIDE_SANDBOX)
  void WriteClangProfilingProfile(
      WriteClangProfilingProfileCallback callback) override;
#endif
  void SetIsCrossOriginIsolated(bool value) override;
  void SetIsDirectSocketEnabled(bool value) override;
  void EnableBlinkRuntimeFeatures(
      const std::vector<std::string>& features) override;
  void OnMemoryPressure(
      base::MemoryPressureListener::MemoryPressureLevel memory_pressure_level);

  bool RendererIsHidden() const;
  void OnRendererHidden();
  void OnRendererVisible();

  bool RendererIsBackgrounded() const;
  void OnRendererBackgrounded();
  void OnRendererForegrounded();

  void RecordMemoryUsageAfterBackgrounded(const char* suffix,
                                          int foregrounded_count);
  void OnRecordMetricsForBackgroundedRendererPurgeTimerExpired(
      const char* suffix,
      int foregrounded_count_when_purged);

  void ReleaseFreeMemory();

  void OnSyncMemoryPressure(
      base::MemoryPressureListener::MemoryPressureLevel memory_pressure_level);

  void OnRendererInterfaceReceiver(
      mojo::PendingAssociatedReceiver<mojom::Renderer> receiver);

  scoped_refptr<discardable_memory::ClientDiscardableSharedMemoryManager>
      discardable_memory_allocator_;

  // These objects live solely on the render thread.
  std::unique_ptr<blink::scheduler::WebThreadScheduler> main_thread_scheduler_;
  std::unique_ptr<RendererBlinkPlatformImpl> blink_platform_impl_;
  std::unique_ptr<blink::URLLoaderThrottleProvider>
      url_loader_throttle_provider_;

  std::vector<std::string> cors_exempt_header_list_;

  // Used on the render thread.
  std::unique_ptr<blink::WebVideoCaptureImplManager> vc_manager_;

  // Used to keep track of the renderer's backgrounded and visibility state.
  // Updated via an IPC from the browser process. If nullopt, the browser
  // process has yet to send an update and the state is unknown.
  absl::optional<mojom::RenderProcessBackgroundState> background_state_;
  absl::optional<mojom::RenderProcessVisibleState> visible_state_;

  blink::WebString user_agent_;
  blink::WebString reduced_user_agent_;
  blink::UserAgentMetadata user_agent_metadata_;

  // Sticky once true, indicates that compositing is done without Gpu, so
  // resources given to the compositor or to the viz service should be
  // software-based.
  bool is_gpu_compositing_disabled_ = false;

  // Utility class to provide GPU functionalities to media.
  // TODO(dcastagna): This should be just one scoped_ptr once
  // http://crbug.com/580386 is fixed.
  // NOTE(dcastagna): At worst this accumulates a few bytes per context lost.
  std::vector<std::unique_ptr<GpuVideoAcceleratorFactoriesImpl>> gpu_factories_;

  // Utility classes to allow WebRTC to create video decoders.
  std::unique_ptr<MediaInterfaceFactory> media_interface_factory_;
  std::unique_ptr<media::DecoderFactory> media_decoder_factory_;

  // Thread for running multimedia operations (e.g., video decoding).
  std::unique_ptr<base::Thread> media_thread_;

  // Will point to appropriate task runner after initialization,
  // regardless of whether |compositor_thread_| is overriden.
  scoped_refptr<base::SingleThreadTaskRunner> compositor_task_runner_;

  // Task to run the VideoFrameCompositor on.
  scoped_refptr<base::SingleThreadTaskRunner>
      video_frame_compositor_task_runner_;

  // Pool of workers used for raster operations (e.g., tile rasterization).
  scoped_refptr<CategorizedWorkerPool> categorized_worker_pool_;

#if defined(OS_ANDROID)
  scoped_refptr<StreamTextureFactory> stream_texture_factory_;
#endif

#if defined(OS_WIN)
  scoped_refptr<DCOMPTextureFactory> dcomp_texture_factory_;
#endif

  scoped_refptr<viz::ContextProviderCommandBuffer> shared_main_thread_contexts_;

  base::ObserverList<RenderThreadObserver>::Unchecked observers_;

  scoped_refptr<viz::RasterContextProvider>
      video_frame_compositor_context_provider_;

  scoped_refptr<viz::RasterContextProvider> shared_worker_context_provider_;

  HistogramCustomizer histogram_customizer_;

  std::unique_ptr<base::MemoryPressureListener> memory_pressure_listener_;

  std::unique_ptr<viz::Gpu> gpu_;

  std::unique_ptr<VariationsRenderThreadObserver> variations_observer_;

  // Compositor settings.
  int gpu_rasterization_msaa_sample_count_;
  bool is_lcd_text_enabled_;
  bool is_zero_copy_enabled_;
  bool is_gpu_memory_buffer_compositor_resources_enabled_;
  bool is_partial_raster_enabled_;
  bool is_elastic_overscroll_enabled_;
  bool is_zoom_for_dsf_enabled_;
  bool is_threaded_animation_enabled_;
  bool is_scroll_animator_enabled_;

  // Target rendering ColorSpace.
  gfx::ColorSpace rendering_color_space_;

  // Used when AddRoute() is called and the RenderFrameImpl hasn't been created
  // yet.
  std::map<int, mojo::PendingReceiver<mojom::Frame>> pending_frames_;

  mojo::AssociatedRemote<mojom::RendererHost> renderer_host_;

  blink::AssociatedInterfaceRegistry associated_interfaces_;

  mojo::AssociatedReceiver<mojom::Renderer> renderer_receiver_{this};

  mojo::AssociatedRemote<mojom::RenderMessageFilter> render_message_filter_;

  std::set<std::unique_ptr<AgentSchedulingGroup>, base::UniquePtrComparator>
      agent_scheduling_groups_;

  RendererMemoryMetrics purge_and_suspend_memory_metrics_;
  int process_foregrounded_count_;

  int32_t client_id_;

  // A mojo connection to the CompositingModeReporter service.
  mojo::Remote<viz::mojom::CompositingModeReporter> compositing_mode_reporter_;
  // The class is a CompositingModeWatcher, which is bound to mojo through
  // this member.
  mojo::Receiver<viz::mojom::CompositingModeWatcher>
      compositing_mode_watcher_receiver_{this};

  // Delegate is expected to live as long as requests may be sent.
  blink::WebResourceRequestSenderDelegate* resource_request_sender_delegate_ =
      nullptr;

  // Tracks the time the run loop started for this thread.
  base::TimeTicks run_loop_start_time_;

  base::WeakPtrFactory<RenderThreadImpl> weak_factory_{this};
};

}  // namespace content

#endif  // CONTENT_RENDERER_RENDER_THREAD_IMPL_H_
