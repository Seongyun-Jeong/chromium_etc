// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gin/public/isolate_holder.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <utility>

#include "base/check_op.h"
#include "base/system/sys_info.h"
#include "base/task/current_thread.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "gin/debug_impl.h"
#include "gin/function_template.h"
#include "gin/per_isolate_data.h"
#include "gin/v8_initializer.h"
#include "gin/v8_isolate_memory_dump_provider.h"
#include "gin/v8_shared_memory_dump_provider.h"
#include "v8/include/v8-isolate.h"
#include "v8/include/v8-snapshot.h"

namespace gin {

namespace {
v8::ArrayBuffer::Allocator* g_array_buffer_allocator = nullptr;
const intptr_t* g_reference_table = nullptr;
}  // namespace

IsolateHolder::IsolateHolder(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    IsolateType isolate_type)
    : IsolateHolder(std::move(task_runner),
                    AccessMode::kSingleThread,
                    isolate_type) {}

IsolateHolder::IsolateHolder(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    AccessMode access_mode,
    IsolateType isolate_type)
    : IsolateHolder(std::move(task_runner),
                    access_mode,
                    kAllowAtomicsWait,
                    isolate_type,
                    IsolateCreationMode::kNormal) {}

IsolateHolder::IsolateHolder(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    AccessMode access_mode,
    AllowAtomicsWaitMode atomics_wait_mode,
    IsolateType isolate_type,
    IsolateCreationMode isolate_creation_mode,
    v8::CreateHistogramCallback create_histogram_callback,
    v8::AddHistogramSampleCallback add_histogram_sample_callback)
    : access_mode_(access_mode), isolate_type_(isolate_type) {
  CHECK(Initialized())
      << "You need to invoke gin::IsolateHolder::Initialize first";

  DCHECK(task_runner);
  DCHECK(task_runner->BelongsToCurrentThread());

  v8::ArrayBuffer::Allocator* allocator = g_array_buffer_allocator;
  DCHECK(allocator);

  isolate_ = v8::Isolate::Allocate();
  isolate_data_ = std::make_unique<PerIsolateData>(isolate_, allocator,
                                                   access_mode_, task_runner);
  if (isolate_creation_mode == IsolateCreationMode::kCreateSnapshot) {
    // This branch is called when creating a V8 snapshot for Blink.
    // Note SnapshotCreator calls isolate->Enter() in its construction.
    snapshot_creator_ =
        std::make_unique<v8::SnapshotCreator>(isolate_, g_reference_table);
    DCHECK_EQ(isolate_, snapshot_creator_->GetIsolate());
  } else {
    v8::Isolate::CreateParams params;
    params.code_event_handler = DebugImpl::GetJitCodeEventHandler();
    params.constraints.ConfigureDefaults(
        base::SysInfo::AmountOfPhysicalMemory(),
        base::SysInfo::AmountOfVirtualMemory());
    params.array_buffer_allocator = allocator;
    params.allow_atomics_wait =
        atomics_wait_mode == AllowAtomicsWaitMode::kAllowAtomicsWait;
    params.external_references = g_reference_table;
    params.only_terminate_in_safe_scope = true;
    params.embedder_wrapper_type_index = kWrapperInfoIndex;
    params.embedder_wrapper_object_index = kEncodedValueIndex;
    params.create_histogram_callback = create_histogram_callback;
    params.add_histogram_sample_callback = add_histogram_sample_callback;

    v8::Isolate::Initialize(isolate_, params);
  }

  // This will attempt register the shared memory dump provider for every
  // IsolateHolder, but only the first registration will have any effect.
  gin::V8SharedMemoryDumpProvider::Register();

  isolate_memory_dump_provider_ =
      std::make_unique<V8IsolateMemoryDumpProvider>(this, task_runner);
}

IsolateHolder::~IsolateHolder() {
  isolate_memory_dump_provider_.reset();
  // Calling Isolate::Dispose makes sure all threads which might access
  // PerIsolateData are finished.
  isolate_->Dispose();
  isolate_data_.reset();
  isolate_ = nullptr;
}

// static
void IsolateHolder::Initialize(ScriptMode mode,
                               v8::ArrayBuffer::Allocator* allocator,
                               const intptr_t* reference_table,
                               const std::string js_command_line_flags) {
  CHECK(allocator);
  V8Initializer::Initialize(mode, js_command_line_flags);
  g_array_buffer_allocator = allocator;
  g_reference_table = reference_table;
}

// static
bool IsolateHolder::Initialized() {
  return g_array_buffer_allocator;
}

void IsolateHolder::EnableIdleTasks(
    std::unique_ptr<V8IdleTaskRunner> idle_task_runner) {
  DCHECK(isolate_data_.get());
  isolate_data_->EnableIdleTasks(std::move(idle_task_runner));
}

}  // namespace gin
