// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/service/gpu_channel_manager.h"

#include <algorithm>
#include <memory>
#include <utility>

#if defined(OS_WIN)
#include <dxgi1_3.h>
#endif

#include "base/bind.h"
#include "base/command_line.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/location.h"
#include "base/metrics/histogram_macros.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/system/sys_info.h"
#include "base/task/bind_post_task.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/traced_value.h"
#include "build/build_config.h"
#include "components/viz/common/features.h"
#include "gpu/command_buffer/common/context_creation_attribs.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "gpu/command_buffer/service/feature_info.h"
#include "gpu/command_buffer/service/gl_utils.h"
#include "gpu/command_buffer/service/gpu_tracer.h"
#include "gpu/command_buffer/service/mailbox_manager_factory.h"
#include "gpu/command_buffer/service/memory_program_cache.h"
#include "gpu/command_buffer/service/passthrough_program_cache.h"
#include "gpu/command_buffer/service/scheduler.h"
#include "gpu/command_buffer/service/service_utils.h"
#include "gpu/command_buffer/service/sync_point_manager.h"
#include "gpu/config/gpu_crash_keys.h"
#include "gpu/config/gpu_finch_features.h"
#include "gpu/ipc/common/gpu_client_ids.h"
#include "gpu/ipc/common/memory_stats.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "gpu/ipc/service/gpu_channel_manager_delegate.h"
#include "gpu/ipc/service/gpu_memory_ablation_experiment.h"
#include "gpu/ipc/service/gpu_memory_buffer_factory.h"
#include "gpu/ipc/service/gpu_watchdog_thread.h"
#include "third_party/skia/include/core/SkGraphics.h"
#if defined(OS_WIN)
#include "ui/gl/gl_angle_util_win.h"
#endif
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_enums.h"
#include "ui/gl/gl_share_group.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/gl/gl_version_info.h"
#include "ui/gl/init/gl_factory.h"

namespace gpu {

namespace {
#if defined(OS_ANDROID)
// Amount of time we expect the GPU to stay powered up without being used.
const int kMaxGpuIdleTimeMs = 40;
// Maximum amount of time we keep pinging the GPU waiting for the client to
// draw.
const int kMaxKeepAliveTimeMs = 200;
#endif
#if defined(OS_WIN)
void TrimD3DResources() {
  // Graphics drivers periodically allocate internal memory buffers in
  // order to speed up subsequent rendering requests. These memory allocations
  // in general lead to increased memory usage by the overall system.
  // Calling Trim discards internal memory buffers allocated for the app,
  // reducing its memory footprint.
  // Calling Trim method does not change the rendering state of the
  // graphics device and has no effect on rendering operations.
  // There is a brief performance hit when internal buffers are reallocated
  // during the first rendering operations after the Trim call, therefore
  // apps should only call Trim when going idle for a period of time or during
  // low memory conditions.
  Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device =
      gl::QueryD3D11DeviceObjectFromANGLE();
  if (d3d11_device) {
    Microsoft::WRL::ComPtr<IDXGIDevice3> dxgi_device;
    if (SUCCEEDED(d3d11_device.As(&dxgi_device))) {
      dxgi_device->Trim();
    }
  }
}
#endif

void APIENTRY CrashReportOnGLErrorDebugCallback(GLenum source,
                                                GLenum type,
                                                GLuint id,
                                                GLenum severity,
                                                GLsizei length,
                                                const GLchar* message,
                                                const GLvoid* user_param) {
  if (type == GL_DEBUG_TYPE_ERROR && source == GL_DEBUG_SOURCE_API &&
      user_param) {
    // Note: log_message cannot contain any user data. The error strings
    // generated from ANGLE are all static strings and do not contain user
    // information such as shader source code. Be careful if updating the
    // contents of this string.
    std::string log_message = gl::GLEnums::GetStringEnum(id);
    if (message && length > 0) {
      log_message += ": " + std::string(message, length);
    }
    LOG(ERROR) << log_message;
    crash_keys::gpu_gl_error_message.Set(log_message);
    int* remaining_reports =
        const_cast<int*>(static_cast<const int*>(user_param));
    if (*remaining_reports > 0) {
      base::debug::DumpWithoutCrashing();
      (*remaining_reports)--;
    }
  }
}

void FormatAllocationSourcesForTracing(
    base::trace_event::TracedValue* dict,
    base::flat_map<GpuPeakMemoryAllocationSource, uint64_t>&
        allocation_sources) {
  dict->SetInteger("UNKNOWN",
                   allocation_sources[GpuPeakMemoryAllocationSource::UNKNOWN]);
  dict->SetInteger(
      "COMMAND_BUFFER",
      allocation_sources[GpuPeakMemoryAllocationSource::COMMAND_BUFFER]);
  dict->SetInteger(
      "SHARED_CONTEXT_STATE",
      allocation_sources[GpuPeakMemoryAllocationSource::SHARED_CONTEXT_STATE]);
  dict->SetInteger(
      "SHARED_IMAGE_STUB",
      allocation_sources[GpuPeakMemoryAllocationSource::SHARED_IMAGE_STUB]);
  dict->SetInteger("SKIA",
                   allocation_sources[GpuPeakMemoryAllocationSource::SKIA]);
}

void SetCrashKeyTimeDelta(base::debug::CrashKeyString* key,
                          base::TimeDelta time_delta) {
  auto str = base::StringPrintf(
      "%d hours, %d min, %lld sec, %lld ms", time_delta.InHours(),
      time_delta.InMinutes() % 60, time_delta.InSeconds() % 60ll,
      time_delta.InMilliseconds() % 1000ll);
  base::debug::SetCrashKeyString(key, str);
}

}  // namespace

GpuChannelManager::GpuPeakMemoryMonitor::GpuPeakMemoryMonitor(
    GpuChannelManager* channel_manager,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : ablation_experiment_(
          std::make_unique<GpuMemoryAblationExperiment>(channel_manager,
                                                        task_runner)),
      weak_factory_(this) {}

GpuChannelManager::GpuPeakMemoryMonitor::~GpuPeakMemoryMonitor() = default;

base::flat_map<GpuPeakMemoryAllocationSource, uint64_t>
GpuChannelManager::GpuPeakMemoryMonitor::GetPeakMemoryUsage(
    uint32_t sequence_num,
    uint64_t* out_peak_memory) {
  auto sequence = sequence_trackers_.find(sequence_num);
  base::flat_map<GpuPeakMemoryAllocationSource, uint64_t> allocation_per_source;
  *out_peak_memory = 0u;
  if (sequence != sequence_trackers_.end()) {
    *out_peak_memory = sequence->second.total_memory_;
    allocation_per_source = sequence->second.peak_memory_per_source_;

    uint64_t ablation_memory =
        ablation_experiment_->GetPeakMemory(sequence_num);
    *out_peak_memory += ablation_memory;
    allocation_per_source[GpuPeakMemoryAllocationSource::SHARED_IMAGE_STUB] +=
        ablation_memory;
  }
  return allocation_per_source;
}

void GpuChannelManager::GpuPeakMemoryMonitor::StartGpuMemoryTracking(
    uint32_t sequence_num) {
  sequence_trackers_.emplace(
      sequence_num,
      SequenceTracker(current_memory_, current_memory_per_source_));
  ablation_experiment_->StartSequence(sequence_num);
  TRACE_EVENT_ASYNC_BEGIN2("gpu", "PeakMemoryTracking", sequence_num, "start",
                           current_memory_, "start_sources",
                           StartTrackingTracedValue());
}

void GpuChannelManager::GpuPeakMemoryMonitor::StopGpuMemoryTracking(
    uint32_t sequence_num) {
  auto sequence = sequence_trackers_.find(sequence_num);
  if (sequence != sequence_trackers_.end()) {
    TRACE_EVENT_ASYNC_END2("gpu", "PeakMemoryTracking", sequence_num, "peak",
                           sequence->second.total_memory_, "end_sources",
                           StopTrackingTracedValue(sequence->second));
    sequence_trackers_.erase(sequence);
    ablation_experiment_->StopSequence(sequence_num);
  }
}

base::WeakPtr<MemoryTracker::Observer>
GpuChannelManager::GpuPeakMemoryMonitor::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void GpuChannelManager::GpuPeakMemoryMonitor::InvalidateWeakPtrs() {
  weak_factory_.InvalidateWeakPtrs();
}

GpuChannelManager::GpuPeakMemoryMonitor::SequenceTracker::SequenceTracker(
    uint64_t current_memory,
    base::flat_map<GpuPeakMemoryAllocationSource, uint64_t>
        current_memory_per_source)
    : initial_memory_(current_memory),
      total_memory_(current_memory),
      initial_memory_per_source_(current_memory_per_source),
      peak_memory_per_source_(std::move(current_memory_per_source)) {}

GpuChannelManager::GpuPeakMemoryMonitor::SequenceTracker::SequenceTracker(
    const SequenceTracker& other) = default;

GpuChannelManager::GpuPeakMemoryMonitor::SequenceTracker::~SequenceTracker() =
    default;

std::unique_ptr<base::trace_event::TracedValue>
GpuChannelManager::GpuPeakMemoryMonitor::StartTrackingTracedValue() {
  auto dict = std::make_unique<base::trace_event::TracedValue>();
  FormatAllocationSourcesForTracing(dict.get(), current_memory_per_source_);
  return dict;
}

std::unique_ptr<base::trace_event::TracedValue>
GpuChannelManager::GpuPeakMemoryMonitor::StopTrackingTracedValue(
    SequenceTracker& sequence) {
  auto dict = std::make_unique<base::trace_event::TracedValue>();
  dict->BeginDictionary("source_totals");
  FormatAllocationSourcesForTracing(dict.get(),
                                    sequence.peak_memory_per_source_);
  dict->EndDictionary();
  dict->BeginDictionary("difference");
  int total_diff = sequence.total_memory_ - sequence.initial_memory_;
  dict->SetInteger("TOTAL", total_diff);
  dict->EndDictionary();
  dict->BeginDictionary("source_difference");

  for (auto it : sequence.peak_memory_per_source_) {
    int diff = (it.second - sequence.initial_memory_per_source_[it.first]);
    switch (it.first) {
      case GpuPeakMemoryAllocationSource::UNKNOWN:
        dict->SetInteger("UNKNOWN", diff);
        break;
      case GpuPeakMemoryAllocationSource::COMMAND_BUFFER:
        dict->SetInteger("COMMAND_BUFFER", diff);
        break;
      case GpuPeakMemoryAllocationSource::SHARED_CONTEXT_STATE:
        dict->SetInteger("SHARED_CONTEXT_STATE", diff);
        break;
      case GpuPeakMemoryAllocationSource::SHARED_IMAGE_STUB:
        dict->SetInteger("SHARED_IMAGE_STUB", diff);
        break;
      case GpuPeakMemoryAllocationSource::SKIA:
        dict->SetInteger("SKIA", diff);
        break;
    }
  }

  dict->EndDictionary();
  return dict;
}

void GpuChannelManager::GpuPeakMemoryMonitor::OnMemoryAllocatedChange(
    CommandBufferId id,
    uint64_t old_size,
    uint64_t new_size,
    GpuPeakMemoryAllocationSource source) {
  uint64_t diff = new_size - old_size;
  current_memory_ += diff;
  current_memory_per_source_[source] += diff;

  ablation_experiment_->OnMemoryAllocated(old_size, new_size);
  if (old_size < new_size) {
    // When memory has increased, iterate over the sequences to update their
    // peak.
    // TODO(jonross): This should be fine if we typically have 1-2 sequences.
    // However if that grows we may end up iterating many times are memory
    // approaches peak. If that is the case we should track a
    // |peak_since_last_sequence_update_| on the the memory changes. Then only
    // update the sequences with a new one is added, or the peak is requested.
    for (auto& seq : sequence_trackers_) {
      if (current_memory_ > seq.second.total_memory_) {
        seq.second.total_memory_ = current_memory_;
        for (auto& sequence : sequence_trackers_) {
          TRACE_EVENT_ASYNC_STEP_INTO1("gpu", "PeakMemoryTracking",
                                       sequence.first, "Peak", "peak",
                                       current_memory_);
        }
        for (auto& memory_per_source : current_memory_per_source_) {
          seq.second.peak_memory_per_source_[memory_per_source.first] =
              memory_per_source.second;
        }
      }
    }
  }
}

GpuChannelManager::GpuChannelManager(
    const GpuPreferences& gpu_preferences,
    GpuChannelManagerDelegate* delegate,
    GpuWatchdogThread* watchdog,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    Scheduler* scheduler,
    SyncPointManager* sync_point_manager,
    SharedImageManager* shared_image_manager,
    GpuMemoryBufferFactory* gpu_memory_buffer_factory,
    const GpuFeatureInfo& gpu_feature_info,
    GpuProcessActivityFlags activity_flags,
    scoped_refptr<gl::GLSurface> default_offscreen_surface,
    ImageDecodeAcceleratorWorker* image_decode_accelerator_worker,
    viz::VulkanContextProvider* vulkan_context_provider,
    viz::MetalContextProvider* metal_context_provider,
    viz::DawnContextProvider* dawn_context_provider)
    : task_runner_(task_runner),
      io_task_runner_(io_task_runner),
      gpu_preferences_(gpu_preferences),
      gpu_driver_bug_workarounds_(
          gpu_feature_info.enabled_gpu_driver_bug_workarounds),
      delegate_(delegate),
      watchdog_(watchdog),
      share_group_(new gl::GLShareGroup()),
      mailbox_manager_(gles2::CreateMailboxManager(gpu_preferences)),
      scheduler_(scheduler),
      sync_point_manager_(sync_point_manager),
      shared_image_manager_(shared_image_manager),
      shader_translator_cache_(gpu_preferences_),
      default_offscreen_surface_(std::move(default_offscreen_surface)),
      gpu_memory_buffer_factory_(gpu_memory_buffer_factory),
      gpu_feature_info_(gpu_feature_info),
      discardable_manager_(gpu_preferences_),
      passthrough_discardable_manager_(gpu_preferences_),
      image_decode_accelerator_worker_(image_decode_accelerator_worker),
      activity_flags_(std::move(activity_flags)),
      memory_pressure_listener_(
          FROM_HERE,
          base::BindRepeating(&GpuChannelManager::HandleMemoryPressure,
                              base::Unretained(this))),
      vulkan_context_provider_(vulkan_context_provider),
      metal_context_provider_(metal_context_provider),
      dawn_context_provider_(dawn_context_provider),
      peak_memory_monitor_(this, task_runner) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(task_runner->BelongsToCurrentThread());
  DCHECK(io_task_runner);
  DCHECK(scheduler);

  const bool using_skia_renderer = features::IsUsingSkiaRenderer();
  const bool enable_gr_shader_cache =
      (gpu_feature_info_.status_values[GPU_FEATURE_TYPE_OOP_RASTERIZATION] ==
       gpu::kGpuFeatureStatusEnabled) ||
      using_skia_renderer;
  const bool disable_disk_cache =
      gpu_preferences_.disable_gpu_shader_disk_cache;
  if (enable_gr_shader_cache && !disable_disk_cache) {
    gr_shader_cache_.emplace(gpu_preferences.gpu_program_cache_size, this);
    if (using_skia_renderer) {
      gr_shader_cache_->CacheClientIdOnDisk(gpu::kDisplayCompositorClientId);
    }
  }
}

GpuChannelManager::~GpuChannelManager() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // Clear |gpu_channels_| first to prevent reentrancy problems from GpuChannel
  // destructor.
  auto gpu_channels = std::move(gpu_channels_);
  gpu_channels_.clear();
  gpu_channels.clear();

  if (default_offscreen_surface_.get()) {
    default_offscreen_surface_->Destroy();
    default_offscreen_surface_ = nullptr;
  }

  // Inavlidate here as the |shared_context_state_| attempts to call back to
  // |this| in the middle of the deletion.
  peak_memory_monitor_.InvalidateWeakPtrs();

  // Try to make the context current so that GPU resources can be destroyed
  // correctly.
  if (shared_context_state_)
    shared_context_state_->MakeCurrent(nullptr);
}

gles2::Outputter* GpuChannelManager::outputter() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (!outputter_) {
    outputter_ =
        std::make_unique<gles2::TraceOutputter>("GpuChannelManager Trace");
  }
  return outputter_.get();
}

gles2::ProgramCache* GpuChannelManager::program_cache() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (!program_cache_.get()) {
    const GpuDriverBugWorkarounds& workarounds = gpu_driver_bug_workarounds_;
    bool disable_disk_cache =
        gpu_preferences_.disable_gpu_shader_disk_cache ||
        workarounds.disable_program_disk_cache;

    // Use the EGL blob cache extension for the passthrough decoder.
    if (gpu_preferences_.use_passthrough_cmd_decoder &&
        gles2::PassthroughCommandDecoderSupported()) {
      program_cache_ = std::make_unique<gles2::PassthroughProgramCache>(
          gpu_preferences_.gpu_program_cache_size, disable_disk_cache);
    } else {
      program_cache_ = std::make_unique<gles2::MemoryProgramCache>(
          gpu_preferences_.gpu_program_cache_size, disable_disk_cache,
          workarounds.disable_program_caching_for_transform_feedback,
          &activity_flags_);
    }
  }
  return program_cache_.get();
}

void GpuChannelManager::RemoveChannel(int client_id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  auto it = gpu_channels_.find(client_id);
  if (it == gpu_channels_.end())
    return;

  delegate_->DidDestroyChannel(client_id);

  // Erase the |gpu_channels_| entry before destroying the GpuChannel object to
  // avoid reentrancy problems from the GpuChannel destructor.
  std::unique_ptr<GpuChannel> channel = std::move(it->second);
  gpu_channels_.erase(it);
  channel.reset();

  if (gpu_channels_.empty()) {
    delegate_->DidDestroyAllChannels();
  }
}

GpuChannel* GpuChannelManager::LookupChannel(int32_t client_id) const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  const auto& it = gpu_channels_.find(client_id);
  return it != gpu_channels_.end() ? it->second.get() : nullptr;
}

GpuChannel* GpuChannelManager::EstablishChannel(
    const base::UnguessableToken& channel_token,
    int client_id,
    uint64_t client_tracing_id,
    bool is_gpu_host,
    bool cache_shaders_on_disk) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (gr_shader_cache_ && cache_shaders_on_disk)
    gr_shader_cache_->CacheClientIdOnDisk(client_id);

  std::unique_ptr<GpuChannel> gpu_channel = GpuChannel::Create(
      this, channel_token, scheduler_, sync_point_manager_, share_group_,
      task_runner_, io_task_runner_, client_id, client_tracing_id, is_gpu_host,
      image_decode_accelerator_worker_);

  if (!gpu_channel)
    return nullptr;

  GpuChannel* gpu_channel_ptr = gpu_channel.get();
  gpu_channels_[client_id] = std::move(gpu_channel);
  return gpu_channel_ptr;
}

void GpuChannelManager::SetChannelClientPid(int client_id,
                                            base::ProcessId client_pid) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GpuChannel* gpu_channel = LookupChannel(client_id);
  if (gpu_channel) {
    // TODO(rockot): It's possible to receive different PIDs for the same
    // GpuChannel because some clients may reuse a client ID. For example, if a
    // Content renderer crashes and restarts, the new process will use the same
    // GPU client ID that the crashed process used. In such cases, this
    // SetChannelClientPid (which comes from the GPU host, not the client
    // process) may arrive late with the crashed process PID, followed shortly
    // thereafter by the current PID of the client.
    //
    // For a short window of time this means a GpuChannel may have a stale PID
    // value. It's not a serious issue since the PID is only informational and
    // not required for security or application correctness, but we should still
    // address it. One option is to introduce a separate host-controlled
    // interface that is paired with the GpuChannel during Establish, which the
    // host can then use to asynchronously push down a PID for the specific
    // channel instance.
    gpu_channel->set_client_pid(client_pid);
  }
}

void GpuChannelManager::InternalDestroyGpuMemoryBuffer(
    gfx::GpuMemoryBufferId id,
    int client_id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  gpu_memory_buffer_factory_->DestroyGpuMemoryBuffer(id, client_id);
}

void GpuChannelManager::DestroyGpuMemoryBuffer(gfx::GpuMemoryBufferId id,
                                               int client_id,
                                               const SyncToken& sync_token) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (!sync_point_manager_->WaitOutOfOrder(
          sync_token,
          base::BindOnce(&GpuChannelManager::InternalDestroyGpuMemoryBuffer,
                         base::Unretained(this), id, client_id))) {
    // No sync token or invalid sync token, destroy immediately.
    InternalDestroyGpuMemoryBuffer(id, client_id);
  }
}

void GpuChannelManager::PopulateShaderCache(int32_t client_id,
                                            const std::string& key,
                                            const std::string& program) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (client_id == kGrShaderCacheClientId) {
    if (gr_shader_cache_)
      gr_shader_cache_->PopulateCache(key, program);
    return;
  }

  if (program_cache())
    program_cache()->LoadProgram(key, program);
}

void GpuChannelManager::LoseAllContexts() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  discardable_manager_.OnContextLost();
  passthrough_discardable_manager_.OnContextLost();
  share_group_ = base::MakeRefCounted<gl::GLShareGroup>();
  for (auto& kv : gpu_channels_) {
    kv.second->MarkAllContextsLost();
  }
  task_runner_->PostTask(FROM_HERE,
                         base::BindOnce(&GpuChannelManager::DestroyAllChannels,
                                        weak_factory_.GetWeakPtr()));
  if (shared_context_state_) {
    shared_context_state_->MarkContextLost();
    shared_context_state_.reset();
  }
}

SharedContextState::ContextLostCallback
GpuChannelManager::GetContextLostCallback() {
  return base::BindPostTask(task_runner_,
                            base::BindOnce(&GpuChannelManager::OnContextLost,
                                           weak_factory_.GetWeakPtr()));
}

GpuChannelManager::OnMemoryAllocatedChangeCallback
GpuChannelManager::GetOnMemoryAllocatedChangeCallback() {
  return base::BindPostTask(
      task_runner_,
      base::BindOnce(
          [](base::WeakPtr<gpu::GpuChannelManager> gpu_channel_manager,
             gpu::CommandBufferId id, uint64_t old_size, uint64_t new_size,
             gpu::GpuPeakMemoryAllocationSource source) {
            if (gpu_channel_manager) {
              gpu_channel_manager->peak_memory_monitor()
                  ->OnMemoryAllocatedChange(id, old_size, new_size, source);
            }
          },
          weak_factory_.GetWeakPtr()));
}

void GpuChannelManager::DestroyAllChannels() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // Clear |gpu_channels_| first to prevent reentrancy problems from GpuChannel
  // destructor.
  auto gpu_channels = std::move(gpu_channels_);
  gpu_channels_.clear();
  gpu_channels.clear();
}

void GpuChannelManager::GetVideoMemoryUsageStats(
    VideoMemoryUsageStats* video_memory_usage_stats) const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // For each context group, assign its memory usage to its PID
  video_memory_usage_stats->process_map.clear();
  uint64_t total_size = 0;
  for (const auto& entry : gpu_channels_) {
    const GpuChannel* channel = entry.second.get();
    if (channel->client_pid() == base::kNullProcessId)
      continue;
    uint64_t size = channel->GetMemoryUsage();
    total_size += size;
    video_memory_usage_stats->process_map[channel->client_pid()].video_memory +=
        size;
  }

  if (shared_context_state_ && !shared_context_state_->context_lost())
    total_size += shared_context_state_->GetMemoryUsage();

  // Assign the total across all processes in the GPU process
  video_memory_usage_stats->process_map[base::GetCurrentProcId()].video_memory =
      total_size;
  video_memory_usage_stats->process_map[base::GetCurrentProcId()]
      .has_duplicates = true;

  video_memory_usage_stats->bytes_allocated = total_size;
}

void GpuChannelManager::StartPeakMemoryMonitor(uint32_t sequence_num) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  peak_memory_monitor_.StartGpuMemoryTracking(sequence_num);
}

base::flat_map<GpuPeakMemoryAllocationSource, uint64_t>
GpuChannelManager::GetPeakMemoryUsage(uint32_t sequence_num,
                                      uint64_t* out_peak_memory) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto allocation_per_source =
      peak_memory_monitor_.GetPeakMemoryUsage(sequence_num, out_peak_memory);
  peak_memory_monitor_.StopGpuMemoryTracking(sequence_num);
  return allocation_per_source;
}

#if defined(OS_ANDROID)
void GpuChannelManager::DidAccessGpu() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  last_gpu_access_time_ = base::TimeTicks::Now();
}

void GpuChannelManager::WakeUpGpu() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  begin_wake_up_time_ = base::TimeTicks::Now();
  ScheduleWakeUpGpu();
}

void GpuChannelManager::ScheduleWakeUpGpu() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  base::TimeTicks now = base::TimeTicks::Now();
  TRACE_EVENT2("gpu", "GpuChannelManager::ScheduleWakeUp", "idle_time",
               (now - last_gpu_access_time_).InMilliseconds(),
               "keep_awake_time", (now - begin_wake_up_time_).InMilliseconds());
  if (now - last_gpu_access_time_ < base::Milliseconds(kMaxGpuIdleTimeMs))
    return;
  if (now - begin_wake_up_time_ > base::Milliseconds(kMaxKeepAliveTimeMs))
    return;

  DoWakeUpGpu();

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&GpuChannelManager::ScheduleWakeUpGpu,
                     weak_factory_.GetWeakPtr()),
      base::Milliseconds(kMaxGpuIdleTimeMs));
}

void GpuChannelManager::DoWakeUpGpu() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  const CommandBufferStub* stub = nullptr;
  for (const auto& kv : gpu_channels_) {
    const GpuChannel* channel = kv.second.get();
    const CommandBufferStub* stub_candidate = channel->GetOneStub();
    if (stub_candidate) {
      DCHECK(stub_candidate->decoder_context());
      // With Vulkan, Dawn, etc, RasterDecoders don't use GL.
      if (stub_candidate->decoder_context()->GetGLContext()) {
        stub = stub_candidate;
        break;
      }
    }
  }
  if (!stub || !stub->decoder_context()->MakeCurrent())
    return;
  glFinish();
  DidAccessGpu();
}

void GpuChannelManager::OnBackgroundCleanup() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // Delete all the GL contexts when the channel does not use WebGL and Chrome
  // goes to background on low-end devices.
  std::vector<int> channels_to_clear;
  for (auto& kv : gpu_channels_) {
    // TODO(ssid): WebGL context loss event notification must be sent before
    // clearing WebGL contexts crbug.com/725306.
    if (kv.second->HasActiveWebGLContext())
      continue;
    channels_to_clear.push_back(kv.first);
    kv.second->MarkAllContextsLost();
  }
  for (int channel : channels_to_clear)
    RemoveChannel(channel);

  if (program_cache_)
    program_cache_->Trim(0u);

  if (shared_context_state_) {
    shared_context_state_->MarkContextLost();
    shared_context_state_.reset();
  }

  SkGraphics::PurgeAllCaches();
}
#endif

void GpuChannelManager::OnApplicationBackgrounded() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (shared_context_state_) {
    shared_context_state_->PurgeMemory(
        base::MemoryPressureListener::MemoryPressureLevel::
            MEMORY_PRESSURE_LEVEL_CRITICAL);
  }

  // Release all skia caching when the application is backgrounded.
  SkGraphics::PurgeAllCaches();
}

void GpuChannelManager::HandleMemoryPressure(
    base::MemoryPressureListener::MemoryPressureLevel memory_pressure_level) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (program_cache_)
    program_cache_->HandleMemoryPressure(memory_pressure_level);

  // These caches require a current context for cleanup.
  if (shared_context_state_ &&
      shared_context_state_->MakeCurrent(nullptr, true /* needs_gl */)) {
      discardable_manager_.HandleMemoryPressure(memory_pressure_level);
      passthrough_discardable_manager_.HandleMemoryPressure(
          memory_pressure_level);
    shared_context_state_->PurgeMemory(memory_pressure_level);
  }

  if (gr_shader_cache_)
    gr_shader_cache_->PurgeMemory(memory_pressure_level);
#if defined(OS_WIN)
  TrimD3DResources();
#endif
}

scoped_refptr<SharedContextState> GpuChannelManager::GetSharedContextState(
    ContextResult* result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (shared_context_state_ && !shared_context_state_->context_lost()) {
    *result = ContextResult::kSuccess;
    return shared_context_state_;
  }

  scoped_refptr<gl::GLSurface> surface = default_offscreen_surface();
  bool use_virtualized_gl_contexts = false;
#if defined(OS_MAC)
  // Virtualize GpuPreference::kLowPower contexts by default on OS X to prevent
  // performance regressions when enabling FCM.
  // http://crbug.com/180463
  use_virtualized_gl_contexts = true;
#endif
  use_virtualized_gl_contexts |=
      gpu_driver_bug_workarounds_.use_virtualized_gl_contexts;

  bool enable_angle_validation = features::IsANGLEValidationEnabled();
#if DCHECK_IS_ON()
  // Force validation on for all debug builds and testing
  enable_angle_validation = true;
#endif

  const bool use_passthrough_decoder =
      gles2::PassthroughCommandDecoderSupported() &&
      gpu_preferences_.use_passthrough_cmd_decoder;
  scoped_refptr<gl::GLShareGroup> share_group;
  if (use_passthrough_decoder) {
    share_group = new gl::GLShareGroup();
    // Virtualized contexts don't work with passthrough command decoder.
    // See https://crbug.com/914976
    use_virtualized_gl_contexts = false;
  } else {
    share_group = share_group_;
  }

  scoped_refptr<gl::GLContext> context =
      use_virtualized_gl_contexts ? share_group->shared_context() : nullptr;
  if (context && (!context->MakeCurrent(surface.get()) ||
                  context->CheckStickyGraphicsResetStatus() != GL_NO_ERROR)) {
    context = nullptr;
  }
  if (!context) {
    gl::GLContextAttribs attribs = gles2::GenerateGLContextAttribs(
        ContextCreationAttribs(), use_passthrough_decoder);

    // Disable robust resource initialization for raster decoder and compositor.
    // TODO(crbug.com/1192632): disable robust_resource_initialization for
    // SwANGLE.
    if (gl::GLSurfaceEGL::GetDisplayType() != gl::ANGLE_SWIFTSHADER &&
        features::IsUsingSkiaRenderer()) {
      attribs.robust_resource_initialization = false;
    }

    attribs.can_skip_validation = !enable_angle_validation;

    context =
        gl::init::CreateGLContext(share_group.get(), surface.get(), attribs);
    if (!context) {
      // TODO(piman): This might not be fatal, we could recurse into
      // CreateGLContext to get more info, tho it should be exceedingly
      // rare and may not be recoverable anyway.
      LOG(ERROR) << "ContextResult::kFatalFailure: "
                    "Failed to create shared context for virtualization.";
      *result = ContextResult::kFatalFailure;
      return nullptr;
    }
    // Ensure that context creation did not lose track of the intended share
    // group.
    DCHECK(context->share_group() == share_group.get());
    gpu_feature_info_.ApplyToGLContext(context.get());

    if (use_virtualized_gl_contexts)
      share_group->SetSharedContext(context.get());
  }

  // This should be either:
  // (1) a non-virtual GL context, or
  // (2) a mock/stub context.
  DCHECK(context->GetHandle() ||
         gl::GetGLImplementation() == gl::kGLImplementationMockGL ||
         gl::GetGLImplementation() == gl::kGLImplementationStubGL);

  if (!context->MakeCurrent(surface.get())) {
    LOG(ERROR)
        << "ContextResult::kTransientFailure, failed to make context current";
    *result = ContextResult::kTransientFailure;
    return nullptr;
  }

  // TODO(penghuang): https://crbug.com/899735 Handle device lost for Vulkan.
  auto shared_context_state = base::MakeRefCounted<SharedContextState>(
      std::move(share_group), std::move(surface), std::move(context),
      use_virtualized_gl_contexts,
      base::BindOnce(&GpuChannelManager::OnContextLost, base::Unretained(this)),
      gpu_preferences_.gr_context_type, vulkan_context_provider_,
      metal_context_provider_, dawn_context_provider_,
      peak_memory_monitor_.GetWeakPtr());

  // Initialize GL context, so Vulkan and GL interop can work properly.
  auto feature_info = base::MakeRefCounted<gles2::FeatureInfo>(
    gpu_driver_bug_workarounds(), gpu_feature_info());
  if (!shared_context_state->InitializeGL(gpu_preferences_,
                                          feature_info.get())) {
    LOG(ERROR) << "ContextResult::kFatalFailure: Failed to Initialize GL for "
        " SharedContextState";
    *result = ContextResult::kFatalFailure;
    return nullptr;
  }

  // Log crash reports when GL errors are generated.
  if (gl::GetGLImplementation() == gl::kGLImplementationEGLANGLE &&
      enable_angle_validation && feature_info->feature_flags().khr_debug) {
    // Limit the total number of gl error crash reports to 1 per GPU
    // process.
    static int remaining_gl_error_reports = 1;
    gles2::InitializeGLDebugLogging(false, CrashReportOnGLErrorDebugCallback,
                                    &remaining_gl_error_reports);
  }

  if (!shared_context_state->InitializeGrContext(
          gpu_preferences_, gpu_driver_bug_workarounds_, gr_shader_cache(),
          &activity_flags_, watchdog_)) {
    LOG(ERROR) << "ContextResult::kFatalFailure: Failed to Initialize"
                  "GrContext for SharedContextState";
    *result = ContextResult::kFatalFailure;
    return nullptr;
  }
  shared_context_state_ = std::move(shared_context_state);

  *result = ContextResult::kSuccess;
  return shared_context_state_;
}

void GpuChannelManager::OnContextLost(bool synthetic_loss) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // Add crash keys for context lost count and time.
  static auto* const lost_count_crash_key = base::debug::AllocateCrashKeyString(
      "context-lost-count", base::debug::CrashKeySize::Size32);
  // The context lost time since creation of |GpuChannelManager|.
  static auto* const lost_time_crash_key = base::debug::AllocateCrashKeyString(
      "context-lost-time", base::debug::CrashKeySize::Size64);
  // The context lost interval since last context lost event.
  static auto* const lost_interval_crash_key =
      base::debug::AllocateCrashKeyString("context-lost-interval",
                                          base::debug::CrashKeySize::Size64);

  base::debug::SetCrashKeyString(
      lost_count_crash_key, base::StringPrintf("%d", ++context_lost_count_));

  auto lost_time = base::TimeTicks::Now() - creation_time_;
  SetCrashKeyTimeDelta(lost_time_crash_key, lost_time);

  if (!context_lost_time_.is_zero()) {
    auto interval = lost_time - context_lost_time_;
    SetCrashKeyTimeDelta(lost_interval_crash_key, interval);
  }

  context_lost_time_ = lost_time;
  bool is_gl = gpu_preferences_.gr_context_type == GrContextType::kGL;
  if (synthetic_loss && is_gl)
    return;

  // Lose all other contexts.
  if (gl::GLContext::LosesAllContextsOnContextLost() ||
      (shared_context_state_ &&
       shared_context_state_->use_virtualized_gl_contexts())) {
    delegate_->LoseAllContexts();
  }

  // Work around issues with recovery by allowing a new GPU process to launch.
  if (gpu_driver_bug_workarounds_.exit_on_context_lost ||
      (shared_context_state_ && !shared_context_state_->GrContextIsGL())) {
    delegate_->MaybeExitOnContextLost();
  }
}

void GpuChannelManager::ScheduleGrContextCleanup() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  shared_context_state_->ScheduleGrContextCleanup();
}

void GpuChannelManager::StoreShader(const std::string& key,
                                    const std::string& shader) {
  delegate_->StoreShaderToDisk(kGrShaderCacheClientId, key, shader);
}

void GpuChannelManager::SetImageDecodeAcceleratorWorkerForTesting(
    ImageDecodeAcceleratorWorker* worker) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  DCHECK(gpu_channels_.empty());
  image_decode_accelerator_worker_ = worker;
}

}  // namespace gpu
