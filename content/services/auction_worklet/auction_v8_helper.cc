// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/services/auction_worklet/auction_v8_helper.h"

#include <limits>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/thread_annotations.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "content/services/auction_worklet/auction_v8_devtools_agent.h"
#include "content/services/auction_worklet/debug_command_queue.h"
#include "gin/array_buffer.h"
#include "gin/converter.h"
#include "gin/gin_features.h"
#include "gin/public/isolate_holder.h"
#include "gin/v8_initializer.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-exception.h"
#include "v8/include/v8-function.h"
#include "v8/include/v8-inspector.h"
#include "v8/include/v8-json.h"
#include "v8/include/v8-message.h"
#include "v8/include/v8-object.h"
#include "v8/include/v8-primitive.h"
#include "v8/include/v8-script.h"
#include "v8/include/v8-template.h"
#include "v8/include/v8-wasm.h"

namespace auction_worklet {

namespace {

// Initialize V8 (and gin).
void InitV8() {
  // TODO(mmenke): All these calls touch global state, which seems rather unsafe
  // if the process is shared with anything else (e.g. --single-process mode, or
  // on Android?).  Is there some safer way to do this?
#if defined(V8_USE_EXTERNAL_STARTUP_DATA)
  gin::V8Initializer::LoadV8Snapshot();
#endif

  gin::IsolateHolder::Initialize(gin::IsolateHolder::kNonStrictMode,
                                 gin::ArrayBufferAllocator::SharedInstance());
}

// Helper class to notify debugger of context creation/destruction.
// Does nothing if passed in `inspector` is nullptr or `debug_id` is nullptr.
class DebugContextScope {
 public:
  DebugContextScope(v8_inspector::V8Inspector* inspector,
                    v8::Local<v8::Context> context,
                    const AuctionV8Helper::DebugId* debug_id,
                    const std::string& name)
      : inspector_(inspector), context_(context), debug_id_(debug_id) {
    if (!inspector_ || !debug_id_)
      return;

    v8_inspector::V8ContextInfo context_info(
        context_, debug_id_->context_group_id(),
        v8_inspector::StringView(reinterpret_cast<const uint8_t*>(name.data()),
                                 name.size()));
    inspector_->contextCreated(context_info);
  }

  ~DebugContextScope() {
    if (!inspector_ || !debug_id_)
      return;

    inspector_->contextDestroyed(context_);
  }

  DebugContextScope(const DebugContextScope&) = delete;
  DebugContextScope& operator=(const DebugContextScope&) = delete;

 private:
  const raw_ptr<v8_inspector::V8Inspector> inspector_;
  const v8::Local<v8::Context> context_;
  const raw_ptr<const AuctionV8Helper::DebugId> debug_id_;
};

}  // namespace

// Utility class to timeout running a v8::Script or calling a v8::Function.
// Instantiate a ScriptTimeoutHelper, and it will terminate script if
// kScriptTimeout passes before it is destroyed.
//
// Creates a v8::SafeForTerminationScope(), so the caller doesn't have to.
class AuctionV8Helper::ScriptTimeoutHelper {
 public:
  ScriptTimeoutHelper(
      AuctionV8Helper* v8_helper,
      scoped_refptr<base::SequencedTaskRunner> timer_task_runner,
      base::TimeDelta script_timeout)
      : v8_helper_(v8_helper),
        termination_scope_(v8_helper->isolate()),
        remaining_delay_(script_timeout),
        timer_task_runner_(std::move(timer_task_runner)) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(v8_sequence_checker_);
    DCHECK(v8_helper->v8_runner()->RunsTasksInCurrentSequence());
    DCHECK_EQ(v8_helper_->timeout_helper_, nullptr);
    v8_helper_->timeout_helper_ = this;
    StartTimer();
  }

  ScriptTimeoutHelper(const ScriptTimeoutHelper&) = delete;
  ScriptTimeoutHelper& operator=(const ScriptTimeoutHelper&) = delete;

  ~ScriptTimeoutHelper() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(v8_sequence_checker_);
    StopTimer();
    DCHECK_EQ(v8_helper_->timeout_helper_, this);
    v8_helper_->timeout_helper_ = nullptr;
  }

  // Actual implementation for AuctionV8Helper::PauseTimeoutTimer.
  void PauseTimeoutTimer() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(v8_sequence_checker_);
    StopTimer();
    // Compute how much of the timeout is left, and clamp from below to 1us
    // to avoid weirdness if it rounds up to 0.
    remaining_delay_ -= (base::TimeTicks::Now() - last_start_);
    if (remaining_delay_ < base::Microseconds(1))
      remaining_delay_ = base::Microseconds(1);
  }

  // Actual implementation for AuctionV8Helper::ResumeTimeoutTimer.
  void ResumeTimeoutTimer() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(v8_sequence_checker_);
    StartTimer();
  }

 private:
  // Class to call TerminateExecution on an Isolate on a specified thread once
  // `AuctionV8Helper::kScriptTimeout` has passed. Create on the sequence the
  // Isolate is running scripts on, but must be destroyed on the task runner the
  // timer is run on.
  class OffThreadTimer {
   public:
    OffThreadTimer(scoped_refptr<base::SequencedTaskRunner> timer_task_runner,
                   v8::Isolate* isolate,
                   base::TimeDelta script_timeout)
        : isolate_(isolate) {
      DCHECK_CALLED_ON_VALID_SEQUENCE(v8_sequence_checker_);
      DETACH_FROM_SEQUENCE(timer_sequence_checker_);
      timer_task_runner->PostTask(
          FROM_HERE, base::BindOnce(&OffThreadTimer::StartTimer,
                                    base::Unretained(this), script_timeout));
    }

    ~OffThreadTimer() {
      DCHECK_CALLED_ON_VALID_SEQUENCE(timer_sequence_checker_);
      DCHECK(!isolate_);
    }

    // Must be called on the Isolate sequence before a task is posted to destroy
    // the OffThreadTimer on the timer sequence.
    void CancelTimer() {
      DCHECK_CALLED_ON_VALID_SEQUENCE(v8_sequence_checker_);
      base::AutoLock autolock(lock_);
      // In the unlikely case AbortScript() was executed just after a script
      // completed, but before CancelTimer() was invoked, clear the exception.
      if (terminate_execution_called_)
        isolate_->CancelTerminateExecution();
      isolate_ = nullptr;
    }

   private:
    void StartTimer(base::TimeDelta script_timeout) {
      DCHECK_CALLED_ON_VALID_SEQUENCE(timer_sequence_checker_);
      timer_.Start(
          FROM_HERE, script_timeout,
          base::BindOnce(&OffThreadTimer::AbortScript, base::Unretained(this)));
    }

    void AbortScript() {
      DCHECK_CALLED_ON_VALID_SEQUENCE(timer_sequence_checker_);
      base::AutoLock autolock(lock_);
      if (!isolate_)
        return;
      terminate_execution_called_ = true;
      isolate_->TerminateExecution();
    }

    // Used solely on `timer_task_runner`.
    base::OneShotTimer timer_;

    base::Lock lock_;

    // Isolate to terminate execution of when time expires. Set to nullptr on
    // the Isolate thread before destruction, to avoid any teardown races with
    // script execution ending.
    raw_ptr<v8::Isolate> isolate_ GUARDED_BY(lock_);

    bool terminate_execution_called_ GUARDED_BY(lock_) = false;
    SEQUENCE_CHECKER(v8_sequence_checker_);
    SEQUENCE_CHECKER(timer_sequence_checker_);
  };

  void StartTimer() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(v8_sequence_checker_);
    DCHECK(!off_thread_timer_);  // Should be stopped cleanly.
    DCHECK_GT(remaining_delay_, base::TimeDelta());
    last_start_ = base::TimeTicks::Now();
    off_thread_timer_ = std::make_unique<OffThreadTimer>(
        timer_task_runner_, v8_helper_->isolate(), remaining_delay_);
  }

  void StopTimer() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(v8_sequence_checker_);
    DCHECK(off_thread_timer_);
    off_thread_timer_->CancelTimer();
    timer_task_runner_->DeleteSoon(FROM_HERE, std::move(off_thread_timer_));
  }

  // `this` exists a local in `v8_helper_`'s method.
  const raw_ptr<AuctionV8Helper> v8_helper_;
  v8::Isolate::SafeForTerminationScope termination_scope_;
  base::TimeDelta remaining_delay_;
  base::TimeTicks last_start_;

  const scoped_refptr<base::SequencedTaskRunner> timer_task_runner_;

  std::unique_ptr<OffThreadTimer> off_thread_timer_;
  SEQUENCE_CHECKER(v8_sequence_checker_);
};

constexpr base::TimeDelta AuctionV8Helper::kScriptTimeout =
    base::Milliseconds(50);

AuctionV8Helper::FullIsolateScope::FullIsolateScope(AuctionV8Helper* v8_helper)
    : locker_(v8_helper->isolate()),
      isolate_scope_(v8_helper->isolate()),
      handle_scope_(v8_helper->isolate()) {}

AuctionV8Helper::FullIsolateScope::~FullIsolateScope() = default;

AuctionV8Helper::DebugId::DebugId(AuctionV8Helper* v8_helper)
    : v8_helper_(v8_helper),
      context_group_id_(v8_helper->AllocContextGroupId()) {}

void AuctionV8Helper::DebugId::SetResumeCallback(
    base::OnceClosure resume_callback) {
  v8_helper_->SetResumeCallback(context_group_id_, std::move(resume_callback));
}

void AuctionV8Helper::DebugId::AbortDebuggerPauses() {
  v8_helper_->AbortDebuggerPauses(context_group_id_);
}

AuctionV8Helper::DebugId::~DebugId() {
  v8_helper_->FreeContextGroupId(context_group_id_);
}

// static
scoped_refptr<AuctionV8Helper> AuctionV8Helper::Create(
    scoped_refptr<base::SingleThreadTaskRunner> v8_runner) {
  scoped_refptr<AuctionV8Helper> result(new AuctionV8Helper(v8_runner));

  // This can't be in the constructor since something else needs to also keep
  // a reference to the object, hence this factory method.
  v8_runner->PostTask(FROM_HERE,
                      base::BindOnce(&AuctionV8Helper::CreateIsolate, result));

  return result;
}

// static
scoped_refptr<base::SingleThreadTaskRunner>
AuctionV8Helper::CreateTaskRunner() {
  // We want a dedicated thread for V8 execution since it may block indefinitely
  // if breakpointed in a debugger.
  return base::ThreadPool::CreateSingleThreadTaskRunner(
      {base::WithBaseSyncPrimitives(),
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::SingleThreadTaskRunnerThreadMode::DEDICATED);
}

v8::Local<v8::Context> AuctionV8Helper::CreateContext(
    v8::Handle<v8::ObjectTemplate> global_template) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  v8::Local<v8::Context> context =
      v8::Context::New(isolate(), nullptr /* extensions */, global_template);
  auto result =
      context->Global()->Delete(context, CreateStringFromLiteral("Date"));

  v8::Local<v8::ObjectTemplate> console_emulation =
      console_.GetConsoleTemplate();
  v8::Local<v8::Object> console_obj;
  if (console_emulation->NewInstance(context).ToLocal(&console_obj)) {
    result = context->Global()->Set(context, CreateStringFromLiteral("console"),
                                    console_obj);
    DCHECK(!result.IsNothing());
  } else {
    DCHECK(false);
  }

  return context;
}

v8::Local<v8::String> AuctionV8Helper::CreateStringFromLiteral(
    const char* ascii_string) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(base::IsStringASCII(ascii_string));
  return v8::String::NewFromUtf8(isolate(), ascii_string,
                                 v8::NewStringType::kNormal,
                                 strlen(ascii_string))
      .ToLocalChecked();
}

v8::MaybeLocal<v8::String> AuctionV8Helper::CreateUtf8String(
    base::StringPiece utf8_string) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!base::IsStringUTF8(utf8_string))
    return v8::MaybeLocal<v8::String>();
  return v8::String::NewFromUtf8(isolate(), utf8_string.data(),
                                 v8::NewStringType::kNormal,
                                 utf8_string.length());
}

v8::MaybeLocal<v8::Value> AuctionV8Helper::CreateValueFromJson(
    v8::Local<v8::Context> context,
    base::StringPiece utf8_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  v8::Local<v8::String> v8_string;
  if (!CreateUtf8String(utf8_json).ToLocal(&v8_string))
    return v8::MaybeLocal<v8::Value>();
  return v8::JSON::Parse(context, v8_string);
}

bool AuctionV8Helper::AppendUtf8StringValue(
    base::StringPiece utf8_string,
    std::vector<v8::Local<v8::Value>>* args) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  v8::Local<v8::String> value;
  if (!CreateUtf8String(utf8_string).ToLocal(&value))
    return false;
  args->push_back(value);
  return true;
}

bool AuctionV8Helper::AppendJsonValue(v8::Local<v8::Context> context,
                                      base::StringPiece utf8_json,
                                      std::vector<v8::Local<v8::Value>>* args) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  v8::Local<v8::Value> value;
  if (!CreateValueFromJson(context, utf8_json).ToLocal(&value))
    return false;
  args->push_back(value);
  return true;
}

bool AuctionV8Helper::InsertValue(base::StringPiece key,
                                  v8::Local<v8::Value> value,
                                  v8::Local<v8::Object> object) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  v8::Local<v8::String> v8_key;
  if (!CreateUtf8String(key).ToLocal(&v8_key))
    return false;
  v8::Maybe<bool> result =
      object->Set(isolate()->GetCurrentContext(), v8_key, value);
  return !result.IsNothing() && result.FromJust();
}

bool AuctionV8Helper::InsertJsonValue(v8::Local<v8::Context> context,
                                      base::StringPiece key,
                                      base::StringPiece utf8_json,
                                      v8::Local<v8::Object> object) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  v8::Local<v8::Value> v8_value;
  return CreateValueFromJson(context, utf8_json).ToLocal(&v8_value) &&
         InsertValue(key, v8_value, object);
}

// Attempts to convert |value| to JSON and write it to |out|. Returns false on
// failure.
bool AuctionV8Helper::ExtractJson(v8::Local<v8::Context> context,
                                  v8::Local<v8::Value> value,
                                  std::string* out) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  v8::MaybeLocal<v8::String> maybe_json = v8::JSON::Stringify(context, value);
  v8::Local<v8::String> json;
  if (!maybe_json.ToLocal(&json))
    return false;
  bool success = gin::ConvertFromV8(isolate(), json, out);
  if (!success)
    return false;
  // Stringify can return the string "undefined" for certain inputs, which is
  // not actually JSON. Treat those as failures.
  if (*out == "undefined") {
    out->clear();
    return false;
  }
  return true;
}

v8::MaybeLocal<v8::UnboundScript> AuctionV8Helper::Compile(
    const std::string& src,
    const GURL& src_url,
    const DebugId* debug_id,
    absl::optional<std::string>& error_out) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  v8::Isolate* v8_isolate = isolate();

  DebugContextScope maybe_debug(inspector(), v8_isolate->GetCurrentContext(),
                                debug_id, src_url.spec());

  v8::MaybeLocal<v8::String> src_string = CreateUtf8String(src);
  v8::MaybeLocal<v8::String> origin_string = CreateUtf8String(src_url.spec());
  if (src_string.IsEmpty() || origin_string.IsEmpty())
    return v8::MaybeLocal<v8::UnboundScript>();

  // Compile script.
  v8::TryCatch try_catch(isolate());
  v8::ScriptCompiler::Source script_source(
      src_string.ToLocalChecked(),
      v8::ScriptOrigin(v8_isolate, origin_string.ToLocalChecked()));
  auto result = v8::ScriptCompiler::CompileUnboundScript(
      v8_isolate, &script_source, v8::ScriptCompiler::kNoCompileOptions,
      v8::ScriptCompiler::NoCacheReason::kNoCacheNoReason);
  if (try_catch.HasCaught()) {
    error_out = FormatExceptionMessage(v8_isolate->GetCurrentContext(),
                                       try_catch.Message());
  }
  return result;
}

v8::MaybeLocal<v8::WasmModuleObject> AuctionV8Helper::CompileWasm(
    const std::string& payload,
    const GURL& src_url,
    const DebugId* debug_id,
    absl::optional<std::string>& error_out) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  v8::Isolate* v8_isolate = isolate();

  DebugContextScope maybe_debug(inspector(), v8_isolate->GetCurrentContext(),
                                debug_id, src_url.spec());

  v8::TryCatch try_catch(isolate());
  v8::MaybeLocal<v8::WasmModuleObject> result = v8::WasmModuleObject::Compile(
      isolate(),
      v8::MemorySpan<const uint8_t>(
          reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
  if (try_catch.HasCaught()) {
    // WasmModuleObject::Compile doesn't know the URL, so FormatExceptionMessage
    // would produce unhelpful message w/o that important bit of context.
    error_out =
        base::StrCat({src_url.spec(), " ",
                      try_catch.Message().IsEmpty()
                          ? "Unknown exception"
                          : FormatValue(isolate(), try_catch.Message()->Get()),
                      "."});
  }
  return result;
}

v8::MaybeLocal<v8::WasmModuleObject> AuctionV8Helper::CloneWasmModule(
    v8::Local<v8::WasmModuleObject> in) {
  return v8::WasmModuleObject::FromCompiledModule(isolate(),
                                                  in->GetCompiledModule());
}

v8::MaybeLocal<v8::Value> AuctionV8Helper::RunScript(
    v8::Local<v8::Context> context,
    v8::Local<v8::UnboundScript> script,
    const DebugId* debug_id,
    base::StringPiece function_name,
    base::span<v8::Local<v8::Value>> args,
    std::vector<std::string>& error_out) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(isolate(), context->GetIsolate());

  std::string script_name = FormatScriptName(script);
  DebugContextScope maybe_debug(inspector(), context, debug_id, script_name);
  ScopedConsoleTarget direct_console(this, script_name, &error_out);

  v8::Local<v8::String> v8_function_name;
  if (!CreateUtf8String(function_name).ToLocal(&v8_function_name))
    return v8::MaybeLocal<v8::Value>();

  v8::Local<v8::Script> local_script = script->BindToCurrentContext();

  // Run script.
  v8::TryCatch try_catch(isolate());
  ScriptTimeoutHelper timeout_helper(this, timer_task_runner_, script_timeout_);
  auto result = local_script->Run(context);

  if (try_catch.HasTerminated()) {
    error_out.push_back(
        base::StrCat({script_name, " top-level execution timed out."}));
    return v8::MaybeLocal<v8::Value>();
  }

  if (try_catch.HasCaught()) {
    error_out.push_back(FormatExceptionMessage(context, try_catch.Message()));
    return v8::MaybeLocal<v8::Value>();
  }

  if (result.IsEmpty())
    return v8::MaybeLocal<v8::Value>();

  v8::Local<v8::Value> function;
  if (!context->Global()->Get(context, v8_function_name).ToLocal(&function)) {
    error_out.push_back(base::StrCat(
        {script_name, " function `", function_name, "` not found."}));
    return v8::MaybeLocal<v8::Value>();
  }

  if (!function->IsFunction()) {
    error_out.push_back(base::StrCat(
        {script_name, " `", function_name, "` is not a function."}));
    return v8::MaybeLocal<v8::Value>();
  }

  v8::MaybeLocal<v8::Value> func_result = v8::Function::Cast(*function)->Call(
      context, context->Global(), args.size(), args.data());
  if (try_catch.HasTerminated()) {
    error_out.push_back(base::StrCat(
        {script_name, " execution of `", function_name, "` timed out."}));
    return v8::MaybeLocal<v8::Value>();
  }
  if (try_catch.HasCaught()) {
    error_out.push_back(FormatExceptionMessage(context, try_catch.Message()));
    return v8::MaybeLocal<v8::Value>();
  }
  return func_result;
}

void AuctionV8Helper::AbortDebuggerPauses(int context_group_id) {
  debug_command_queue_->AbortPauses(context_group_id);
}

void AuctionV8Helper::MaybeTriggerInstrumentationBreakpoint(
    const DebugId& debug_id,
    const std::string& name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (devtools_agent_) {
    devtools_agent_->MaybeTriggerInstrumentationBreakpoint(
        debug_id.context_group_id(), name);
  }
}

void AuctionV8Helper::set_script_timeout_for_testing(
    base::TimeDelta script_timeout) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  script_timeout_ = script_timeout;
}

int AuctionV8Helper::AllocContextGroupId() {
  base::AutoLock hold_lock(context_groups_lock_);
  while (true) {
    if (last_context_group_id_ == std::numeric_limits<int>::max())
      last_context_group_id_ = 0;
    int candidate = ++last_context_group_id_;
    DCHECK_GT(candidate, 0);

    if (resume_callbacks_.find(candidate) == resume_callbacks_.end()) {
      resume_callbacks_.emplace(candidate, base::OnceClosure());
      return candidate;
    }
  }
}

void AuctionV8Helper::SetResumeCallback(int context_group_id,
                                        base::OnceClosure resume_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::AutoLock hold_lock(context_groups_lock_);
  auto it = resume_callbacks_.find(context_group_id);
  DCHECK(it != resume_callbacks_.end());
  DCHECK(it->second.is_null());
  it->second = std::move(resume_callback);
}

void AuctionV8Helper::FreeContextGroupId(int context_group_id) {
  debug_command_queue_->RecycleContextGroupId(context_group_id);
  {
    base::AutoLock hold_lock(context_groups_lock_);
    size_t removed = resume_callbacks_.erase(context_group_id);
    DCHECK_EQ(1u, removed);
  }
}

void AuctionV8Helper::Resume(int context_group_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::OnceClosure resume_closure;
  {
    base::AutoLock hold_lock(context_groups_lock_);
    auto it = resume_callbacks_.find(context_group_id);
    if (it == resume_callbacks_.end())
      return;

    resume_closure = std::move(it->second);
  }

  if (resume_closure)
    std::move(resume_closure).Run();
}

void AuctionV8Helper::SetLastContextGroupIdForTesting(int new_last_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::AutoLock hold_lock(context_groups_lock_);
  last_context_group_id_ = new_last_id;
}

void AuctionV8Helper::ResumeAllForTesting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<int> live_ids;
  {
    base::AutoLock hold_lock(context_groups_lock_);
    for (const auto& kv : resume_callbacks_)
      live_ids.push_back(kv.first);
  }

  for (int id : live_ids)
    Resume(id);
}

void AuctionV8Helper::ConnectDevToolsAgent(
    mojo::PendingReceiver<blink::mojom::DevToolsAgent> agent,
    scoped_refptr<base::SequencedTaskRunner> mojo_sequence,
    const DebugId& debug_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!devtools_agent_) {
    devtools_agent_ = std::make_unique<AuctionV8DevToolsAgent>(
        this, debug_command_queue_.get(), std::move(mojo_sequence));
    v8_inspector_ =
        v8_inspector::V8Inspector::create(isolate(), devtools_agent_.get());
  }
  devtools_agent_->Connect(std::move(agent), debug_id.context_group_id());
}

v8_inspector::V8Inspector* AuctionV8Helper::inspector() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return v8_inspector_.get();
}

void AuctionV8Helper::SetV8InspectorForTesting(
    std::unique_ptr<v8_inspector::V8Inspector> v8_inspector) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  v8_inspector_ = std::move(v8_inspector);
}

void AuctionV8Helper::PauseTimeoutTimer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(timeout_helper_);
  timeout_helper_->PauseTimeoutTimer();
}

void AuctionV8Helper::ResumeTimeoutTimer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(timeout_helper_);
  timeout_helper_->ResumeTimeoutTimer();
}

scoped_refptr<base::SequencedTaskRunner>
AuctionV8Helper::GetTimeoutTimerRunnerForTesting() {
  return timer_task_runner_;
}

std::string AuctionV8Helper::FormatScriptName(
    v8::Local<v8::UnboundScript> script) {
  return FormatValue(isolate(), script->GetScriptName());
}

AuctionV8Helper::ScopedConsoleTarget::ScopedConsoleTarget(
    AuctionV8Helper* owner,
    const std::string& console_script_name,
    std::vector<std::string>* out)
    : owner_(owner) {
  DCHECK(!owner_->console_buffer_);
  DCHECK(owner_->console_script_name_.empty());
  owner_->console_buffer_ = out;
  owner_->console_script_name_ = console_script_name;
}

AuctionV8Helper::ScopedConsoleTarget::~ScopedConsoleTarget() {
  owner_->console_buffer_ = nullptr;
  owner_->console_script_name_ = std::string();
}

AuctionV8Helper::AuctionV8Helper(
    scoped_refptr<base::SingleThreadTaskRunner> v8_runner)
    : base::RefCountedDeleteOnSequence<AuctionV8Helper>(v8_runner),
      v8_runner_(v8_runner),
      timer_task_runner_(base::ThreadPool::CreateSequencedTaskRunner({})),
      debug_command_queue_(base::MakeRefCounted<DebugCommandQueue>(v8_runner)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);

  // InitV8 on main thread, to avoid races if multiple instances exist with
  // different runners.
  static int v8_initialized = false;
  if (!v8_initialized)
    InitV8();

  v8_initialized = true;
}

AuctionV8Helper::~AuctionV8Helper() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(resume_callbacks_.empty());
  // Need to destroy sessions before `v8_inspector_` which needs to be
  // destroyed before `devtools_agent_`.
  if (devtools_agent_)
    devtools_agent_->DestroySessions();
}

void AuctionV8Helper::CreateIsolate() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Now the initialization is completed, create an isolate.
  isolate_holder_ = std::make_unique<gin::IsolateHolder>(
      base::ThreadTaskRunnerHandle::Get(), gin::IsolateHolder::kUseLocker,
      gin::IsolateHolder::IsolateType::kUtility);
  FullIsolateScope v8_scope(this);
  scratch_context_.Reset(isolate(), CreateContext());
}

// static
std::string AuctionV8Helper::FormatExceptionMessage(
    v8::Local<v8::Context> context,
    v8::Local<v8::Message> message) {
  if (message.IsEmpty()) {
    return "Unknown exception.";
  } else {
    v8::Isolate* isolate = message->GetIsolate();
    int line_num;
    return base::StrCat(
        {FormatValue(isolate, message->GetScriptResourceName()),
         !context.IsEmpty() && message->GetLineNumber(context).To(&line_num)
             ? std::string(":") + base::NumberToString(line_num)
             : std::string(),
         " ", FormatValue(isolate, message->Get()), "."});
  }
}

// static
std::string AuctionV8Helper::FormatValue(v8::Isolate* isolate,
                                         v8::Local<v8::Value> val) {
  if (val.IsEmpty()) {
    return "\"\"";
  } else {
    v8::String::Utf8Value val_utf8(isolate, val);
    if (*val_utf8 == nullptr)
      return std::string();
    return std::string(*val_utf8, val_utf8.length());
  }
}

}  // namespace auction_worklet
