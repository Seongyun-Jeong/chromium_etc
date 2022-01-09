// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/media_factory.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/task_runner_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "build/chromecast_buildflags.h"
#include "cc/trees/layer_tree_settings.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/renderer/content_renderer_client.h"
#include "content/public/renderer/render_frame_media_playback_options.h"
#include "content/renderer/media/batching_media_log.h"
#include "content/renderer/media/inspector_media_event_handler.h"
#include "content/renderer/media/media_interface_factory.h"
#include "content/renderer/media/render_media_event_handler.h"
#include "content/renderer/media/renderer_webmediaplayer_delegate.h"
#include "content/renderer/render_frame_impl.h"
#include "content/renderer/render_thread_impl.h"
#include "content/renderer/render_view_impl.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/cdm_factory.h"
#include "media/base/decoder_factory.h"
#include "media/base/demuxer.h"
#include "media/base/media_switches.h"
#include "media/base/renderer_factory_selector.h"
#include "media/media_buildflags.h"
#include "media/mojo/buildflags.h"
#include "media/renderers/decrypting_renderer_factory.h"
#include "media/renderers/default_decoder_factory.h"
#include "media/renderers/default_renderer_factory.h"
#include "media/video/gpu_video_accelerator_factories.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/service_manager/public/cpp/connect.h"
#include "services/viz/public/cpp/gpu/context_provider_command_buffer.h"
#include "third_party/blink/public/common/browser_interface_broker_proxy.h"
#include "third_party/blink/public/common/thread_safe_browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/media/key_system_config_selector.h"
#include "third_party/blink/public/platform/media/remote_playback_client_wrapper_impl.h"
#include "third_party/blink/public/platform/media/resource_fetch_context.h"
#include "third_party/blink/public/platform/media/url_index.h"
#include "third_party/blink/public/platform/media/video_frame_compositor.h"
#include "third_party/blink/public/platform/media/web_encrypted_media_client_impl.h"
#include "third_party/blink/public/platform/media/web_media_player_impl.h"
#include "third_party/blink/public/platform/media/web_media_player_params.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/web_surface_layer_bridge.h"
#include "third_party/blink/public/platform/web_video_frame_submitter.h"
#include "third_party/blink/public/web/blink.h"
#include "third_party/blink/public/web/modules/media/audio/audio_device_factory.h"
#include "third_party/blink/public/web/modules/mediastream/webmediaplayer_ms.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "url/origin.h"

#if defined(OS_ANDROID)
#include "components/viz/common/features.h"
#include "content/renderer/media/android/flinging_renderer_client_factory.h"
#include "content/renderer/media/android/media_player_renderer_client_factory.h"
#include "content/renderer/media/android/stream_texture_wrapper_impl.h"
#include "media/base/android/media_codec_util.h"
#include "media/base/media.h"
#include "url/gurl.h"
#endif

#if BUILDFLAG(ENABLE_CAST_RENDERER)
#include "content/renderer/media/cast_renderer_client_factory.h"
#endif

#if defined(OS_FUCHSIA)
#include "media/fuchsia/cdm/client/fuchsia_cdm_util.h"
#elif BUILDFLAG(ENABLE_MOJO_CDM)
#include "media/mojo/clients/mojo_cdm_factory.h"  // nogncheck
#else
#include "media/cdm/default_cdm_factory.h"
#endif

#if defined(OS_FUCHSIA) && BUILDFLAG(ENABLE_MOJO_CDM)
#error "MojoCdm should be disabled for Fuchsia."
#endif

#if BUILDFLAG(ENABLE_MOJO_AUDIO_DECODER) || BUILDFLAG(ENABLE_MOJO_VIDEO_DECODER)
#include "media/mojo/clients/mojo_decoder_factory.h"  // nogncheck
#endif

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
// Enable remoting sender
#include "media/remoting/courier_renderer_factory.h"  // nogncheck
#include "media/remoting/renderer_controller.h"       // nogncheck
#endif

#if BUILDFLAG(ENABLE_CAST_STREAMING_RENDERER)
// Enable libcast streaming receiver.
#include "components/cast_streaming/public/cast_streaming_url.h"  // nogncheck
#include "components/cast_streaming/renderer/public/playback_command_forwarding_renderer_factory.h"  // nogncheck
#include "components/cast_streaming/renderer/public/renderer_controller_proxy.h"  // nogncheck
#endif

#if BUILDFLAG(ENABLE_CAST_AUDIO_RENDERER)
#include "content/renderer/media/cast_renderer_factory.h"
#endif  // BUILDFLAG(ENABLE_CAST_AUDIO_RENDERER)

#if BUILDFLAG(IS_CHROMECAST)
// Enable remoting receiver
#include "media/remoting/receiver_controller.h"        // nogncheck
#include "media/remoting/remoting_constants.h"         // nogncheck
#include "media/remoting/remoting_renderer_factory.h"  // nogncheck
#endif

#if BUILDFLAG(IS_WIN)
#include "content/renderer/media/win/dcomp_texture_wrapper_impl.h"
#include "media/base/win/mf_feature_checks.h"
#include "media/cdm/win/media_foundation_cdm.h"
#include "media/mojo/clients/win/media_foundation_renderer_client_factory.h"
#endif  // BUILDFLAG(IS_WIN)

namespace {

// This limit is much higher than it needs to be right now, because the logic
// is also capping audio-only media streams, and it is quite normal for their
// to be many of those. See http://crbug.com/1232649
constexpr size_t kDefaultMaxWebMediaPlayers = 1000;

size_t GetMaxWebMediaPlayers() {
  static const size_t kMaxWebMediaPlayers = []() {
    auto* command_line = base::CommandLine::ForCurrentProcess();
    if (command_line->HasSwitch(switches::kMaxWebMediaPlayerCount)) {
      std::string value =
          command_line->GetSwitchValueASCII(switches::kMaxWebMediaPlayerCount);
      size_t parsed_value = 0;
      if (base::StringToSizeT(value, &parsed_value) && parsed_value > 0)
        return parsed_value;
    }
    return kDefaultMaxWebMediaPlayers;
  }();
  return kMaxWebMediaPlayers;
}

class FrameFetchContext : public blink::ResourceFetchContext {
 public:
  explicit FrameFetchContext(blink::WebLocalFrame* frame) : frame_(frame) {
    DCHECK(frame_);
  }

  FrameFetchContext(const FrameFetchContext&) = delete;
  FrameFetchContext& operator=(const FrameFetchContext&) = delete;

  ~FrameFetchContext() override = default;

  blink::WebLocalFrame* frame() const { return frame_; }

  // blink::ResourceFetchContext implementation.
  std::unique_ptr<blink::WebAssociatedURLLoader> CreateUrlLoader(
      const blink::WebAssociatedURLLoaderOptions& options) override {
    return frame_->CreateAssociatedURLLoader(options);
  }

 private:
  blink::WebLocalFrame* frame_;
};

// Obtains the media ContextProvider and calls the given callback on the same
// thread this is called on. Obtaining the media ContextProvider requires
// establishing a GPUChannelHost, which must be done on the main thread.
void PostContextProviderToCallback(
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner,
    scoped_refptr<viz::RasterContextProvider> unwanted_context_provider,
    blink::WebSubmitterConfigurationCallback set_context_provider_callback) {
  // |unwanted_context_provider| needs to be destroyed on the current thread.
  // Therefore, post a reply-callback that retains a reference to it, so that it
  // doesn't get destroyed on the main thread.
  main_task_runner->PostTaskAndReply(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<viz::RasterContextProvider>
                 unwanted_context_provider,
             blink::WebSubmitterConfigurationCallback cb) {
            auto* rti = content::RenderThreadImpl::current();
            auto context_provider = rti->GetVideoFrameCompositorContextProvider(
                std::move(unwanted_context_provider));
            std::move(cb).Run(!rti->IsGpuCompositingDisabled(),
                              std::move(context_provider));
          },
          unwanted_context_provider,
          media::BindToCurrentLoop(std::move(set_context_provider_callback))),
      base::BindOnce([](scoped_refptr<viz::RasterContextProvider>
                            unwanted_context_provider) {},
                     unwanted_context_provider));
}

void LogRoughness(
    media::MediaLog* media_log,
    const cc::VideoPlaybackRoughnessReporter::Measurement& measurement) {
  // This function can be called from any thread. Don't do anything that assumes
  // a certain task runner.
  double fps =
      std::round(measurement.frames / measurement.duration.InSecondsF());
  media_log->SetProperty<media::MediaLogProperty::kVideoPlaybackRoughness>(
      measurement.roughness);
  media_log->SetProperty<media::MediaLogProperty::kVideoPlaybackFreezing>(
      measurement.freezing);
  media_log->SetProperty<media::MediaLogProperty::kFramerate>(fps);

  // TODO(eugene@chromium.org) All of this needs to be moved away from
  // media_factory.cc once a proper channel to report roughness is found.
  static constexpr char kRoughnessHistogramName[] = "Media.Video.Roughness";
  const char* suffix = nullptr;
  static std::tuple<double, const char*> fps_buckets[] = {
      {24, "24fps"}, {25, "25fps"}, {30, "30fps"}, {50, "50fps"}, {60, "60fps"},
  };
  for (auto& bucket : fps_buckets) {
    if (fps == std::get<0>(bucket)) {
      suffix = std::get<1>(bucket);
      break;
    }
  }

  // Only report known FPS buckets, on 60Hz displays and at least HD quality.
  if (suffix != nullptr && measurement.refresh_rate_hz == 60 &&
      measurement.frame_size.height() > 700) {
    base::UmaHistogramCustomTimes(
        base::JoinString({kRoughnessHistogramName, suffix}, "."),
        base::Milliseconds(measurement.roughness), base::Milliseconds(1),
        base::Milliseconds(99), 100);
    // TODO(liberato): Record freezing, once we're sure that we're computing the
    // score we want.  For now, don't record anything so we don't have a mis-
    // match of UMA values.
  }

  TRACE_EVENT2("media", "VideoPlaybackRoughness", "id", media_log->id(),
               "roughness", measurement.roughness);
  TRACE_EVENT2("media", "VideoPlaybackFreezing", "id", media_log->id(),
               "freezing", measurement.freezing.InMilliseconds());
}

std::unique_ptr<media::DefaultRendererFactory> CreateDefaultRendererFactory(
    media::MediaLog* media_log,
    media::DecoderFactory* decoder_factory,
    content::RenderThreadImpl* render_thread,
    content::RenderFrameImpl* render_frame) {
#if defined(OS_ANDROID)
  auto default_factory = std::make_unique<media::DefaultRendererFactory>(
      media_log, decoder_factory,
      base::BindRepeating(&content::RenderThreadImpl::GetGpuFactories,
                          base::Unretained(render_thread)));
#else
  auto default_factory = std::make_unique<media::DefaultRendererFactory>(
      media_log, decoder_factory,
      base::BindRepeating(&content::RenderThreadImpl::GetGpuFactories,
                          base::Unretained(render_thread)),
      render_frame->CreateSpeechRecognitionClient(base::OnceClosure()));
#endif
  return default_factory;
}

enum class MediaPlayerType {
  kNormal,       // WebMediaPlayerImpl backed.
  kMediaStream,  // MediaStream backed.
};

// Helper function returning whether SurfaceLayer should be enabled.
blink::WebMediaPlayer::SurfaceLayerMode GetSurfaceLayerMode(
    MediaPlayerType type) {
#if defined(OS_ANDROID)
  if (!::features::UseSurfaceLayerForVideo())
    return blink::WebMediaPlayer::SurfaceLayerMode::kNever;
#endif

  if (type != MediaPlayerType::kMediaStream)
    return blink::WebMediaPlayer::SurfaceLayerMode::kAlways;

  return base::FeatureList::IsEnabled(media::kSurfaceLayerForMediaStreams)
             ? blink::WebMediaPlayer::SurfaceLayerMode::kAlways
             : blink::WebMediaPlayer::SurfaceLayerMode::kNever;
}

// Creates the VideoFrameSubmitter and its task_runner based on the current
// SurfaceLayerMode;
std::unique_ptr<blink::WebVideoFrameSubmitter> CreateSubmitter(
    scoped_refptr<base::SingleThreadTaskRunner>
        main_thread_compositor_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner>*
        video_frame_compositor_task_runner,
    const cc::LayerTreeSettings& settings,
    media::MediaLog* media_log,
    content::RenderFrame* render_frame,
    blink::WebMediaPlayer::SurfaceLayerMode surface_layer_mode) {
  content::RenderThreadImpl* render_thread =
      content::RenderThreadImpl::current();
  *video_frame_compositor_task_runner = nullptr;

  if (!render_thread)
    return nullptr;

  bool use_sync_primitives = false;
  if (surface_layer_mode == blink::WebMediaPlayer::SurfaceLayerMode::kAlways) {
    // Run the compositor / frame submitter on its own thread.
    *video_frame_compositor_task_runner =
        render_thread->CreateVideoFrameCompositorTaskRunner();
    // We must use sync primitives on this thread.
    use_sync_primitives = true;
  } else {
    // Run on the cc thread, even if we may switch to SurfaceLayer mode later
    // if we're in kOnDemand mode.  We do this to avoid switching threads when
    // switching to SurfaceLayer.
    *video_frame_compositor_task_runner =
        render_thread->compositor_task_runner()
            ? render_thread->compositor_task_runner()
            : render_frame->GetTaskRunner(
                  blink::TaskType::kInternalMediaRealTime);
    render_thread->SetVideoFrameCompositorTaskRunner(
        *video_frame_compositor_task_runner);
  }

  if (surface_layer_mode == blink::WebMediaPlayer::SurfaceLayerMode::kNever)
    return nullptr;

  auto log_roughness_cb =
      base::BindRepeating(LogRoughness, base::Owned(media_log->Clone()));
  auto post_to_context_provider_cb = base::BindRepeating(
      &PostContextProviderToCallback, main_thread_compositor_task_runner);
  return blink::WebVideoFrameSubmitter::Create(
      std::move(post_to_context_provider_cb), std::move(log_roughness_cb),
      settings, use_sync_primitives);
}

}  // namespace

namespace content {

MediaFactory::MediaFactory(
    RenderFrameImpl* render_frame,
    media::RequestRoutingTokenCallback request_routing_token_cb)
    : render_frame_(render_frame),
      request_routing_token_cb_(std::move(request_routing_token_cb)) {}

MediaFactory::~MediaFactory() {
  // Release the DecoderFactory to the media thread since it may still be in use
  // there due to pending pipeline Stop() calls. Once each Stop() completes, no
  // new tasks using the DecoderFactory will execute, so we don't need to worry
  // about additional posted tasks from Stop().
  if (decoder_factory_) {
    // Prevent any new decoders from being created to avoid future access to the
    // external decoder factory (MojoDecoderFactory) since it requires access to
    // the (about to be destructed) RenderFrame.
    decoder_factory_->Shutdown();

    // DeleteSoon() shouldn't ever fail, we should always have a RenderThread at
    // this time and subsequently a media thread. To fail, the media thread must
    // be dead/dying (which only happens at ~RenderThreadImpl), in which case
    // the process is about to die anyways.
    RenderThreadImpl::current()->GetMediaThreadTaskRunner()->DeleteSoon(
        FROM_HERE, std::move(decoder_factory_));
  }
}

void MediaFactory::SetupMojo() {
  // Only do setup once.
  DCHECK(!interface_broker_);

  interface_broker_ = render_frame_->GetBrowserInterfaceBroker();
  DCHECK(interface_broker_);
}

blink::WebMediaPlayer* MediaFactory::CreateMediaPlayer(
    const blink::WebMediaPlayerSource& source,
    blink::WebMediaPlayerClient* client,
    blink::MediaInspectorContext* inspector_context,
    blink::WebMediaPlayerEncryptedMediaClient* encrypted_client,
    blink::WebContentDecryptionModule* initial_cdm,
    const blink::WebString& sink_id,
    viz::FrameSinkId parent_frame_sink_id,
    const cc::LayerTreeSettings& settings,
    scoped_refptr<base::SingleThreadTaskRunner>
        main_thread_compositor_task_runner) {
  blink::WebLocalFrame* web_frame = render_frame_->GetWebFrame();
  auto* delegate = GetWebMediaPlayerDelegate();

  // Prevent a frame from creating too many media players, as they are extremely
  // heavy objects and a common cause of browser memory leaks. See
  // crbug.com/1144736
  if (delegate->web_media_player_count() >= GetMaxWebMediaPlayers()) {
    blink::WebString message =
        "Blocked attempt to create a WebMediaPlayer as there are too many "
        "WebMediaPlayers already in existence. See crbug.com/1144736#c27";
    web_frame->GenerateInterventionReport("TooManyWebMediaPlayers", message);
    return nullptr;
  }

  if (source.IsMediaStream()) {
    return CreateWebMediaPlayerForMediaStream(
        client, inspector_context, sink_id, web_frame, parent_frame_sink_id,
        settings, main_thread_compositor_task_runner);
  }

  // If |source| was not a MediaStream, it must be a URL.
  // TODO(guidou): Fix this when support for other srcObject types is added.
  DCHECK(source.IsURL());
  blink::WebURL url = source.GetAsURL();

  RenderThreadImpl* render_thread = RenderThreadImpl::current();
  // Render thread may not exist in tests, returning nullptr if it does not.
  if (!render_thread)
    return nullptr;

  scoped_refptr<media::SwitchableAudioRendererSink> audio_renderer_sink =
      blink::AudioDeviceFactory::NewSwitchableAudioRendererSink(
          blink::WebAudioDeviceSourceType::kMediaElement,
          render_frame_->GetWebFrame()->GetLocalFrameToken(),
          media::AudioSinkParameters(/*session_id=*/base::UnguessableToken(),
                                     sink_id.Utf8()));

  const blink::web_pref::WebPreferences webkit_preferences =
      render_frame_->GetBlinkPreferences();
  bool embedded_media_experience_enabled = false;
#if defined(OS_ANDROID)
  embedded_media_experience_enabled =
      webkit_preferences.embedded_media_experience_enabled;
#endif  // defined(OS_ANDROID)

  // When memory pressure based garbage collection is enabled for MSE, the
  // |enable_instant_source_buffer_gc| flag controls whether the GC is done
  // immediately on memory pressure notification or during the next SourceBuffer
  // append (slower, but is MSE-spec compliant).
  bool enable_instant_source_buffer_gc =
      base::GetFieldTrialParamByFeatureAsBool(
          media::kMemoryPressureBasedSourceBufferGC,
          "enable_instant_source_buffer_gc", false);

  std::vector<std::unique_ptr<BatchingMediaLog::EventHandler>> handlers;
  handlers.push_back(
      std::make_unique<InspectorMediaEventHandler>(inspector_context));
  if (base::FeatureList::IsEnabled(media::kEnableMediaInternals))
    handlers.push_back(std::make_unique<RenderMediaEventHandler>());

  // This must be created for every new WebMediaPlayer, each instance generates
  // a new player id which is used to collate logs on the browser side.
  auto media_log = std::make_unique<BatchingMediaLog>(
      render_frame_->GetTaskRunner(blink::TaskType::kInternalMedia),
      std::move(handlers));

  base::WeakPtr<media::MediaObserver> media_observer;
  auto factory_selector = CreateRendererFactorySelector(
      media_log.get(), url, render_frame_->GetRenderFrameMediaPlaybackOptions(),
      GetDecoderFactory(),
      std::make_unique<blink::RemotePlaybackClientWrapperImpl>(client),
      &media_observer);

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
  DCHECK(media_observer);
#endif

  if (!fetch_context_) {
    fetch_context_ = std::make_unique<FrameFetchContext>(web_frame);
    DCHECK(!url_index_);
    url_index_ = std::make_unique<blink::UrlIndex>(
        fetch_context_.get(),
        render_frame_->GetTaskRunner(blink::TaskType::kInternalMedia));
  }
  DCHECK_EQ(static_cast<FrameFetchContext*>(fetch_context_.get())->frame(),
            web_frame);

  mojo::PendingRemote<media::mojom::MediaMetricsProvider> metrics_provider;
  interface_broker_->GetInterface(
      metrics_provider.InitWithNewPipeAndPassReceiver());

  scoped_refptr<base::SingleThreadTaskRunner>
      video_frame_compositor_task_runner;
  const auto surface_layer_mode = GetSurfaceLayerMode(MediaPlayerType::kNormal);
  std::unique_ptr<blink::WebVideoFrameSubmitter> submitter = CreateSubmitter(
      main_thread_compositor_task_runner, &video_frame_compositor_task_runner,
      settings, media_log.get(), render_frame_, surface_layer_mode);

  scoped_refptr<base::SingleThreadTaskRunner> media_task_runner =
      render_thread->GetMediaThreadTaskRunner();

  if (!media_task_runner) {
    // If the media thread failed to start, we will receive a null task runner.
    // Fail the creation by returning null, and let callers handle the error.
    // See https://crbug.com/775393.
    return nullptr;
  }

  auto params = std::make_unique<blink::WebMediaPlayerParams>(
      std::move(media_log),
      base::BindRepeating(&RenderFrameImpl::DeferMediaLoad,
                          base::Unretained(render_frame_),
                          delegate->has_played_media()),
      audio_renderer_sink, media_task_runner,
      render_thread->GetWorkerTaskRunner(),
      render_thread->compositor_task_runner(),
      video_frame_compositor_task_runner,
      base::BindRepeating(&v8::Isolate::AdjustAmountOfExternalAllocatedMemory,
                          base::Unretained(blink::MainThreadIsolate())),
      initial_cdm, request_routing_token_cb_, media_observer,
      enable_instant_source_buffer_gc, embedded_media_experience_enabled,
      std::move(metrics_provider),
      base::BindOnce(&blink::WebSurfaceLayerBridge::Create,
                     parent_frame_sink_id,
                     blink::WebSurfaceLayerBridge::ContainsVideo::kYes),
      RenderThreadImpl::current()->SharedMainThreadContextProvider(),
      surface_layer_mode,
      render_frame_->GetRenderFrameMediaPlaybackOptions()
          .is_background_suspend_enabled,
      render_frame_->GetRenderFrameMediaPlaybackOptions()
          .is_background_video_playback_enabled,
      render_frame_->GetRenderFrameMediaPlaybackOptions()
          .is_background_video_track_optimization_supported,
      GetContentClient()->renderer()->OverrideDemuxerForUrl(render_frame_, url,
                                                            media_task_runner));

  auto vfc = std::make_unique<blink::VideoFrameCompositor>(
      params->video_frame_compositor_task_runner(), std::move(submitter));

  auto* media_player = new blink::WebMediaPlayerImpl(
      web_frame, client, encrypted_client, delegate,
      std::move(factory_selector), url_index_.get(), std::move(vfc),
      blink::Platform::Current()->GetBrowserInterfaceBroker(),
      std::move(params));

  return media_player;
}

blink::WebEncryptedMediaClient* MediaFactory::EncryptedMediaClient() {
  if (!web_encrypted_media_client_) {
    web_encrypted_media_client_ = std::make_unique<
        blink::WebEncryptedMediaClientImpl>(
        GetCdmFactory(), render_frame_->GetMediaPermission(),
        std::make_unique<blink::KeySystemConfigSelector::WebLocalFrameDelegate>(
            render_frame_->GetWebFrame()));
  }
  return web_encrypted_media_client_.get();
}

std::unique_ptr<media::RendererFactorySelector>
MediaFactory::CreateRendererFactorySelector(
    media::MediaLog* media_log,
    blink::WebURL url,
    const RenderFrameMediaPlaybackOptions& renderer_media_playback_options,
    media::DecoderFactory* decoder_factory,
    std::unique_ptr<media::RemotePlaybackClientWrapper> client_wrapper,
    base::WeakPtr<media::MediaObserver>* out_media_observer) {
  using media::RendererType;

  RenderThreadImpl* render_thread = RenderThreadImpl::current();
  // Render thread may not exist in tests, returning nullptr if it does not.
  if (!render_thread)
    return nullptr;

  auto factory_selector = std::make_unique<media::RendererFactorySelector>();
  bool is_base_renderer_factory_set = false;

  auto factory = GetContentClient()->renderer()->GetBaseRendererFactory(
      render_frame_, media_log, decoder_factory,
      base::BindRepeating(&RenderThreadImpl::GetGpuFactories,
                          base::Unretained(render_thread)));
  if (factory) {
    is_base_renderer_factory_set = true;
    factory_selector->AddBaseFactory(RendererType::kContentEmbedderDefined,
                                     std::move(factory));
  }

#if defined(OS_ANDROID)
  DCHECK(interface_broker_);

  // MediaPlayerRendererClientFactory setup. It is used for HLS playback.
  auto media_player_factory =
      std::make_unique<MediaPlayerRendererClientFactory>(
          render_thread->compositor_task_runner(), CreateMojoRendererFactory(),
          base::BindRepeating(
              &StreamTextureWrapperImpl::Create,
              render_thread->EnableStreamTextureCopy(),
              render_thread->GetStreamTexureFactory(),
              render_frame_->GetTaskRunner(blink::TaskType::kInternalMedia)));

  // Always give |factory_selector| a MediaPlayerRendererClient factory. WMPI
  // might fallback to it if the final redirected URL is an HLS url.
  factory_selector->AddFactory(RendererType::kMediaPlayer,
                               std::move(media_player_factory));

  // FlingingRendererClientFactory (FRCF) setup.
  auto flinging_factory = std::make_unique<FlingingRendererClientFactory>(
      CreateMojoRendererFactory(), std::move(client_wrapper));

  // base::Unretained() is safe here because |factory_selector| owns and
  // outlives |flinging_factory|.
  factory_selector->StartRequestRemotePlayStateCB(
      base::BindOnce(&FlingingRendererClientFactory::SetRemotePlayStateChangeCB,
                     base::Unretained(flinging_factory.get())));

  // Must bind the callback first since |flinging_factory| will be moved.
  // base::Unretained() is also safe here, for the same reasons.
  auto is_flinging_cb =
      base::BindRepeating(&FlingingRendererClientFactory::IsFlingingActive,
                          base::Unretained(flinging_factory.get()));
  factory_selector->AddConditionalFactory(
      RendererType::kFlinging, std::move(flinging_factory), is_flinging_cb);
#endif  // defined(OS_ANDROID)

#if BUILDFLAG(ENABLE_MOJO_RENDERER)
  if (!is_base_renderer_factory_set &&
      renderer_media_playback_options.is_mojo_renderer_enabled()) {
    is_base_renderer_factory_set = true;
#if BUILDFLAG(ENABLE_CAST_RENDERER)
    factory_selector->AddBaseFactory(
        RendererType::kCast, std::make_unique<CastRendererClientFactory>(
                                 media_log, CreateMojoRendererFactory()));
#else
    // The "default" MojoRendererFactory can be wrapped by a
    // DecryptingRendererFactory without changing any behavior.
    // TODO(tguilbert/xhwang): Add "RendererType::DECRYPTING" if ever we need to
    // distinguish between a "pure" and "decrypting" MojoRenderer.
    factory_selector->AddBaseFactory(
        RendererType::kMojo, std::make_unique<media::DecryptingRendererFactory>(
                                 media_log, CreateMojoRendererFactory()));
#endif  // BUILDFLAG(ENABLE_CAST_RENDERER)
  }
#endif  // BUILDFLAG(ENABLE_MOJO_RENDERER)

#if BUILDFLAG(ENABLE_CAST_AUDIO_RENDERER)
  DCHECK(!is_base_renderer_factory_set);
  is_base_renderer_factory_set = true;
  factory_selector->AddBaseFactory(
      RendererType::kCast,
      std::make_unique<CastRendererFactory>(
          media_log, decoder_factory,
          base::BindRepeating(&RenderThreadImpl::GetGpuFactories,
                              base::Unretained(render_thread)),
          render_frame_->GetBrowserInterfaceBroker()));
#endif  // BUILDFLAG(ENABLE_CAST_AUDIO_RENDERER)

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
  mojo::PendingRemote<media::mojom::RemotingSource> remoting_source;
  auto remoting_source_receiver =
      remoting_source.InitWithNewPipeAndPassReceiver();
  mojo::PendingRemote<media::mojom::Remoter> remoter;
  GetRemoterFactory()->Create(std::move(remoting_source),
                              remoter.InitWithNewPipeAndPassReceiver());
  using RemotingController = media::remoting::RendererController;
  auto remoting_controller = std::make_unique<RemotingController>(
      std::move(remoting_source_receiver), std::move(remoter));
  *out_media_observer = remoting_controller->GetWeakPtr();

  auto courier_factory =
      std::make_unique<media::remoting::CourierRendererFactory>(
          std::move(remoting_controller));

  // Must bind the callback first since |courier_factory| will be moved.
  // base::Unretained is safe because |factory_selector| owns |courier_factory|.
  auto is_remoting_cb = base::BindRepeating(
      &media::remoting::CourierRendererFactory::IsRemotingActive,
      base::Unretained(courier_factory.get()));
  factory_selector->AddConditionalFactory(
      RendererType::kCourier, std::move(courier_factory), is_remoting_cb);
#endif

#if BUILDFLAG(IS_WIN)
  bool use_mf_for_clear = media::SupportMediaFoundationClearPlayback();
  // Only use MediaFoundationRenderer when MediaFoundationCdm is available or
  // MediaFoundation for Clear is supported.
  if (media::MediaFoundationCdm::IsAvailable() || use_mf_for_clear) {
    auto dcomp_texture_creation_cb =
        base::BindRepeating(&DCOMPTextureWrapperImpl::Create,
                            render_thread->GetDCOMPTextureFactory(),
                            render_thread->GetMediaThreadTaskRunner());

    factory_selector->AddFactory(
        RendererType::kMediaFoundation,
        std::make_unique<media::MediaFoundationRendererClientFactory>(
            media_log, std::move(dcomp_texture_creation_cb),
            CreateMojoRendererFactory()));

    if (use_mf_for_clear) {
      // We want to use Media Foundation even for non-explicit Media Foundation
      // clients, register Media Foundation as the base renderer type.
      // We don't use AddBaseFactory here because if ENABLE_MOJO_RENDERER
      // is set then we may have already called it previously and it is
      // expected that AddBaseFactory will only be called when there is not
      // already a base factory type set. Instead manually set the new base
      // factory type with SetBaseRendererType.
      factory_selector->SetBaseRendererType(RendererType::kMediaFoundation);
      is_base_renderer_factory_set = true;
    }
  }
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_CHROMECAST)
  if (renderer_media_playback_options.is_remoting_renderer_enabled()) {
#if BUILDFLAG(ENABLE_CAST_RENDERER)
    auto default_factory_remoting = std::make_unique<CastRendererClientFactory>(
        media_log, CreateMojoRendererFactory());
#else   // BUILDFLAG(ENABLE_CAST_RENDERER)
    auto default_factory_remoting = CreateDefaultRendererFactory(
        media_log, decoder_factory, render_thread, render_frame_);
#endif  // BUILDFLAG(ENABLE_CAST_RENDERER)
    mojo::PendingRemote<media::mojom::Remotee> remotee;
    interface_broker_->GetInterface(remotee.InitWithNewPipeAndPassReceiver());
    auto remoting_renderer_factory =
        std::make_unique<media::remoting::RemotingRendererFactory>(
            std::move(remotee), std::move(default_factory_remoting),
            render_thread->GetMediaThreadTaskRunner());
    auto is_remoting_media = base::BindRepeating(
        [](const GURL& url) -> bool {
          return url.SchemeIs(media::remoting::kRemotingScheme);
        },
        url);
    factory_selector->AddConditionalFactory(
        RendererType::kRemoting, std::move(remoting_renderer_factory),
        is_remoting_media);
  }

#if BUILDFLAG(ENABLE_CAST_STREAMING_RENDERER)
  if (cast_streaming::IsCastStreamingMediaSourceUrl(url)) {
    DCHECK(!is_base_renderer_factory_set);
    is_base_renderer_factory_set = true;
#if BUILDFLAG(ENABLE_CAST_RENDERER)
    auto default_factory_cast_streaming =
        std::make_unique<CastRendererClientFactory>(
            media_log, CreateMojoRendererFactory());
#else   // BUILDFLAG(ENABLE_CAST_RENDERER)
    // NOTE: Prior to the resolution of b/187332037, playback will not work
    // correctly with this renderer.
    // NOTE: This renderer is only expected to be used in TEST scenarios and
    // should not be used in production.
    auto default_factory_cast_streaming = CreateDefaultRendererFactory(
        media_log, decoder_factory, render_thread, render_frame_);
#endif  // BUILDFLAG(ENABLE_CAST_RENDERER)

    auto* renderer_controller_proxy =
        cast_streaming::RendererControllerProxy::GetInstance();
    DCHECK(renderer_controller_proxy);
    auto cast_streaming_renderer_factory = std::make_unique<
        cast_streaming::PlaybackCommandForwardingRendererFactory>(
        std::move(default_factory_cast_streaming),
        renderer_controller_proxy->GetReceiver(render_frame_));
    factory_selector->AddBaseFactory(
        RendererType::kCastStreaming,
        std::move(cast_streaming_renderer_factory));
  }
#endif  // BUILDFLAG(ENABLE_CAST_STREAMING_RENDERER)
#endif  // BUILDFLAG(IS_CHROMECAST)

  if (!is_base_renderer_factory_set) {
    // TODO(crbug.com/1265448): These sorts of checks shouldn't be necessary if
    // this method were significantly refactored to split things up by
    // Android/non-Android/Cast/etc...
    is_base_renderer_factory_set = true;
    auto default_factory = CreateDefaultRendererFactory(
        media_log, decoder_factory, render_thread, render_frame_);
    factory_selector->AddBaseFactory(RendererType::kDefault,
                                     std::move(default_factory));
  }

  return factory_selector;
}

blink::WebMediaPlayer* MediaFactory::CreateWebMediaPlayerForMediaStream(
    blink::WebMediaPlayerClient* client,
    blink::MediaInspectorContext* inspector_context,
    const blink::WebString& sink_id,
    blink::WebLocalFrame* frame,
    viz::FrameSinkId parent_frame_sink_id,
    const cc::LayerTreeSettings& settings,
    scoped_refptr<base::SingleThreadTaskRunner>
        main_thread_compositor_task_runner) {
  RenderThreadImpl* const render_thread = RenderThreadImpl::current();

  scoped_refptr<base::SingleThreadTaskRunner>
      video_frame_compositor_task_runner;

  std::vector<std::unique_ptr<BatchingMediaLog::EventHandler>> handlers;
  handlers.push_back(
      std::make_unique<InspectorMediaEventHandler>(inspector_context));
  if (base::FeatureList::IsEnabled(media::kEnableMediaInternals))
    handlers.push_back(std::make_unique<RenderMediaEventHandler>());

  // This must be created for every new WebMediaPlayer, each instance generates
  // a new player id which is used to collate logs on the browser side.
  auto media_log = std::make_unique<BatchingMediaLog>(
      render_frame_->GetTaskRunner(blink::TaskType::kInternalMedia),
      std::move(handlers));

  const auto surface_layer_mode =
      GetSurfaceLayerMode(MediaPlayerType::kMediaStream);
  std::unique_ptr<blink::WebVideoFrameSubmitter> submitter = CreateSubmitter(
      main_thread_compositor_task_runner, &video_frame_compositor_task_runner,
      settings, media_log.get(), render_frame_, surface_layer_mode);

  return new blink::WebMediaPlayerMS(
      frame, client, GetWebMediaPlayerDelegate(), std::move(media_log),
      render_frame_->GetTaskRunner(blink::TaskType::kInternalMedia),
      render_thread->GetIOTaskRunner(), video_frame_compositor_task_runner,
      render_thread->GetMediaThreadTaskRunner(),
      render_thread->GetWorkerTaskRunner(), render_thread->GetGpuFactories(),
      sink_id,
      base::BindOnce(&blink::WebSurfaceLayerBridge::Create,
                     parent_frame_sink_id,
                     blink::WebSurfaceLayerBridge::ContainsVideo::kYes),
      std::move(submitter), surface_layer_mode);
}

media::RendererWebMediaPlayerDelegate*
MediaFactory::GetWebMediaPlayerDelegate() {
  if (!media_player_delegate_) {
    media_player_delegate_ =
        new media::RendererWebMediaPlayerDelegate(render_frame_);
  }
  return media_player_delegate_;
}

media::DecoderFactory* MediaFactory::GetDecoderFactory() {
  if (!decoder_factory_) {
#if BUILDFLAG(ENABLE_MOJO_AUDIO_DECODER) || BUILDFLAG(ENABLE_MOJO_VIDEO_DECODER)
    media::mojom::InterfaceFactory* const interface_factory =
        GetMediaInterfaceFactory();
#else
    media::mojom::InterfaceFactory* const interface_factory = nullptr;
#endif
    decoder_factory_ = CreateDecoderFactory(interface_factory);
  }

  return decoder_factory_.get();
}

// static
std::unique_ptr<media::DefaultDecoderFactory>
MediaFactory::CreateDecoderFactory(
    media::mojom::InterfaceFactory* interface_factory) {
  std::unique_ptr<media::DecoderFactory> external_decoder_factory;
#if BUILDFLAG(ENABLE_MOJO_AUDIO_DECODER) || BUILDFLAG(ENABLE_MOJO_VIDEO_DECODER)
  if (interface_factory) {
    external_decoder_factory =
        std::make_unique<media::MojoDecoderFactory>(interface_factory);
  }
#endif
  return std::make_unique<media::DefaultDecoderFactory>(
      std::move(external_decoder_factory));
}

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
media::mojom::RemoterFactory* MediaFactory::GetRemoterFactory() {
  DCHECK(interface_broker_);

  if (!remoter_factory_) {
    interface_broker_->GetInterface(
        remoter_factory_.BindNewPipeAndPassReceiver());
  }
  return remoter_factory_.get();
}
#endif

media::CdmFactory* MediaFactory::GetCdmFactory() {
  if (cdm_factory_)
    return cdm_factory_.get();

#if defined(OS_FUCHSIA)
  DCHECK(interface_broker_);
  cdm_factory_ = media::CreateFuchsiaCdmFactory(interface_broker_);
#elif BUILDFLAG(ENABLE_MOJO_CDM)
  cdm_factory_ =
      std::make_unique<media::MojoCdmFactory>(GetMediaInterfaceFactory());
#else
  cdm_factory_ = std::make_unique<media::DefaultCdmFactory>();
#endif  // BUILDFLAG(ENABLE_MOJO_CDM)

  return cdm_factory_.get();
}

media::mojom::InterfaceFactory* MediaFactory::GetMediaInterfaceFactory() {
  DCHECK(interface_broker_);

  if (!media_interface_factory_) {
    media_interface_factory_ =
        std::make_unique<MediaInterfaceFactory>(interface_broker_);
  }

  return media_interface_factory_.get();
}

std::unique_ptr<media::MojoRendererFactory>
MediaFactory::CreateMojoRendererFactory() {
  return std::make_unique<media::MojoRendererFactory>(
      GetMediaInterfaceFactory());
}

}  // namespace content
