// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SERVICES_AUCTION_WORKLET_AUCTION_V8_HELPER_H_
#define CONTENT_SERVICES_AUCTION_WORKLET_AUCTION_V8_HELPER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/ref_counted_delete_on_sequence.h"
#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "base/strings/string_piece.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "content/services/auction_worklet/console.h"
#include "gin/public/isolate_holder.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/devtools/devtools_agent.mojom.h"
#include "url/gurl.h"
#include "v8/include/v8-forward.h"
#include "v8/include/v8-isolate.h"
#include "v8/include/v8-locker.h"
#include "v8/include/v8-persistent-handle.h"

namespace v8 {
class UnboundScript;
class WasmModuleObject;
}  // namespace v8

namespace v8_inspector {
class V8Inspector;
}  // namespace v8_inspector

namespace auction_worklet {

class AuctionV8DevToolsAgent;
class DebugCommandQueue;

// Helper for Javascript operations. Owns a V8 isolate, and manages operations
// on it. Must be deleted after all V8 objects created using its isolate. It
// facilitates creating objects from JSON and running scripts in isolated
// contexts.
//
// Currently, multiple AuctionV8Helpers can be in use at once, each will have
// its own V8 isolate.  All AuctionV8Helpers are assumed to be created on the
// same thread (V8 startup is done only once per process, and not behind a
// lock).  After creation, all public operations on the helper must be done on
// the thread represented by the `v8_runner` argument to Create(). It's the
// caller's responsibility to ensure that all other methods are used from the v8
// runner.
class AuctionV8Helper
    : public base::RefCountedDeleteOnSequence<AuctionV8Helper> {
 public:
  // Timeout for script execution.
  static const base::TimeDelta kScriptTimeout;

  // Helper class to set up v8 scopes to use Isolate. All methods expect a
  // FullIsolateScope to be have been created on the current thread, and a
  // context to be entered.
  class FullIsolateScope {
   public:
    explicit FullIsolateScope(AuctionV8Helper* v8_helper);
    explicit FullIsolateScope(const FullIsolateScope&) = delete;
    FullIsolateScope& operator=(const FullIsolateScope&) = delete;
    ~FullIsolateScope();

   private:
    const v8::Locker locker_;
    const v8::Isolate::Scope isolate_scope_;
    const v8::HandleScope handle_scope_;
  };

  // A wrapper for identifiers used to associate V8 context's with debugging
  // primitives.  Passed to methods like Compile and RunScript. If one is
  // created, AbortDebuggerPauses() must be called before its destruction.
  //
  // This class is thread-safe, except SetResumeCallback must be used from V8
  // thread.
  class DebugId : public base::RefCountedThreadSafe<DebugId> {
   public:
    explicit DebugId(AuctionV8Helper* v8_helper);

    // Returns V8 context group ID associated with this debug id.
    int context_group_id() const { return context_group_id_; }

    // Sets the callback to use to resume a worklet that's paused on startup.
    // Must be called from the V8 thread.
    //
    // `resume_callback` will be invoked on the V8 thread; and should probably
    // be bound to a a WeakPtr, since the invocation is ultimately via debugger
    // mojo pipes, making its timing hard to relate to worklet lifetime.
    void SetResumeCallback(base::OnceClosure resume_callback);

    // If the JS thread is currently within AuctionV8Helper::RunScript() running
    // code with this debug id, and the execution has been paused by the
    // debugger, aborts the execution.
    //
    // Always prevents further debugger pauses of code associated with this
    // debug id.
    //
    // This may be called from any thread, but note that posting this to the V8
    // thread is unlikely to work, since this method is in particular useful for
    // the cases where the V8 thread is blocked.
    void AbortDebuggerPauses();

   private:
    friend class base::RefCountedThreadSafe<DebugId>;
    ~DebugId();

    const scoped_refptr<AuctionV8Helper> v8_helper_;
    const int context_group_id_;
  };

  explicit AuctionV8Helper(const AuctionV8Helper&) = delete;
  AuctionV8Helper& operator=(const AuctionV8Helper&) = delete;

  static scoped_refptr<AuctionV8Helper> Create(
      scoped_refptr<base::SingleThreadTaskRunner> v8_runner);
  static scoped_refptr<base::SingleThreadTaskRunner> CreateTaskRunner();

  scoped_refptr<base::SequencedTaskRunner> v8_runner() const {
    return v8_runner_;
  }

  v8::Isolate* isolate() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return isolate_holder_->isolate();
  }

  // Context that can be used for persistent items that can then be used in
  // other contexts - compiling functions, creating objects, etc.
  v8::Local<v8::Context> scratch_context() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return scratch_context_.Get(isolate());
  }

  // Create a v8::Context. The one thing this does that v8::Context::New() does
  // not is remove access the Date object. It also (for now) installs some
  // rudimentary console emulation.
  v8::Local<v8::Context> CreateContext(
      v8::Local<v8::ObjectTemplate> global_template =
          v8::Local<v8::ObjectTemplate>());

  // Creates a v8::String from an ASCII string literal, which should never fail.
  v8::Local<v8::String> CreateStringFromLiteral(const char* ascii_string);

  // Attempts to create a v8::String from a UTF-8 string. Returns empty string
  // if input is not UTF-8.
  v8::MaybeLocal<v8::String> CreateUtf8String(base::StringPiece utf8_string);

  // The passed in JSON must be a valid UTF-8 JSON string.
  v8::MaybeLocal<v8::Value> CreateValueFromJson(v8::Local<v8::Context> context,
                                                base::StringPiece utf8_json);

  // Convenience wrappers around the above Create* methods. Attempt to create
  // the corresponding value type and append it to the passed in argument
  // vector. Useful for assembling arguments to a Javascript function. Return
  // false on failure.
  bool AppendUtf8StringValue(base::StringPiece utf8_string,
                             std::vector<v8::Local<v8::Value>>* args)
      WARN_UNUSED_RESULT;
  bool AppendJsonValue(v8::Local<v8::Context> context,
                       base::StringPiece utf8_json,
                       std::vector<v8::Local<v8::Value>>* args)
      WARN_UNUSED_RESULT;

  // Convenience wrapper that adds the specified value into the provided Object.
  bool InsertValue(base::StringPiece key,
                   v8::Local<v8::Value> value,
                   v8::Local<v8::Object> object) WARN_UNUSED_RESULT;

  // Convenience wrapper that creates an Object by parsing `utf8_json` as JSON
  // and then inserts it into the provided Object.
  bool InsertJsonValue(v8::Local<v8::Context> context,
                       base::StringPiece key,
                       base::StringPiece utf8_json,
                       v8::Local<v8::Object> object) WARN_UNUSED_RESULT;

  // Attempts to convert |value| to JSON and write it to |out|. Returns false on
  // failure.
  bool ExtractJson(v8::Local<v8::Context> context,
                   v8::Local<v8::Value> value,
                   std::string* out);

  // Compiles the provided script. Despite not being bound to a context, there
  // still must be an active context for this method to be invoked. In case of
  // an error sets `error_out`.
  v8::MaybeLocal<v8::UnboundScript> Compile(
      const std::string& src,
      const GURL& src_url,
      const DebugId* debug_id,
      absl::optional<std::string>& error_out);

  // Compiles the provided WASM module from bytecode. A context must be active
  // for this method to be invoked, and the object would be created for it (but
  // may be cloned efficiently for other contexts via CloneWasmModule). In case
  // of an error sets `error_out`.
  //
  // Note that since the returned object is a JS Object, so to properly isolate
  // different executions it should not be used directly but rather fresh copies
  // should be made via CloneWasmModule.
  v8::MaybeLocal<v8::WasmModuleObject> CompileWasm(
      const std::string& payload,
      const GURL& src_url,
      const DebugId* debug_id,
      absl::optional<std::string>& error_out);

  // Creates a fresh object describing the same WASM module as `in`, which must
  // not be empty. Can return an empty handle on an error.
  //
  // An execution context must be active, and the object will be created for it.
  v8::MaybeLocal<v8::WasmModuleObject> CloneWasmModule(
      v8::Local<v8::WasmModuleObject> in);

  // Binds a script and runs it in the passed in context, returning the result.
  // Note that the returned value could include references to objects or
  // functions contained within the context, so is likely not safe to use in
  // other contexts without sanitization.
  //
  // If `debug_id` is not nullptr, and a debugger connection has been
  // instantiated, will notify debugger of `context`.
  //
  // Assumes passed in context is the active context. Passed in context must be
  // using the Helper's isolate.
  //
  // Running this multiple times in the same context will re-load the entire
  // script file in the context, and then run the script again.
  //
  // In case of an error or console output sets `error_out`.
  v8::MaybeLocal<v8::Value> RunScript(v8::Local<v8::Context> context,
                                      v8::Local<v8::UnboundScript> script,
                                      const DebugId* debug_id,
                                      base::StringPiece function_name,
                                      base::span<v8::Local<v8::Value>> args,
                                      std::vector<std::string>& error_out);

  // If any debugging session targeting `debug_id` has set an active
  // DOM instrumentation breakpoint `name`, asks for v8 to do a debugger pause
  // on the next statement.
  //
  // Expected to be run before a corresponding RunScript.
  void MaybeTriggerInstrumentationBreakpoint(const DebugId& debug_id,
                                             const std::string& name);

  void set_script_timeout_for_testing(base::TimeDelta script_timeout);

  // If non-nullptr, this returns a pointer to the of vector representing the
  // debug output lines of the currently running script.  It's nullptr when
  // nothing is running.
  std::vector<std::string>* console_buffer() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return console_buffer_;
  }

  // Returns a string identifying the currently running script for purpose of
  // attributing its debug output in a human-understandable way. Empty if
  // nothing is running.
  const std::string& console_script_name() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return console_script_name_;
  }

  // Invokes the registered resume callback for given ID. Does nothing if it
  // was already invoked.
  void Resume(int context_group_id);

  // Overrides what ID will be remembered as last returned to help check the
  // allocation algorithm.
  void SetLastContextGroupIdForTesting(int new_last_id);

  // Calls Resume on all registered context group IDs.
  void ResumeAllForTesting();

  // Establishes a debugger connection, initializing debugging objects if
  // needed, and associating the connection with the given `debug_id`.
  //
  // The debugger Mojo objects will primarily live on the v8 thread, but
  // `mojo_sequence` will be used for a secondary communication channel in case
  // the v8 thread is blocked. It must be distinct from v8_runner(). Only the
  // value passed in for `mojo_sequence` the first time this method is called
  // will be used.
  void ConnectDevToolsAgent(
      mojo::PendingReceiver<blink::mojom::DevToolsAgent> agent,
      scoped_refptr<base::SequencedTaskRunner> mojo_sequence,
      const DebugId& debug_id);

  // Returns the v8 inspector if one has been set. null if ConnectDevToolsAgent
  // (or SetV8InspectorForTesting) hasn't been called.
  v8_inspector::V8Inspector* inspector();

  void SetV8InspectorForTesting(
      std::unique_ptr<v8_inspector::V8Inspector> v8_inspector);

  // Temporarily disables (and re-enables) script timeout for the currently
  // running script. Total time elapsed when not paused will be kept track of.
  //
  // Must be called when within RunScript() only.
  void PauseTimeoutTimer();
  void ResumeTimeoutTimer();

  // Returns the sequence where the timeout timer runs.
  // This may be called on any thread.
  scoped_refptr<base::SequencedTaskRunner> GetTimeoutTimerRunnerForTesting();

  // Helper for formatting script name for debug messages.
  std::string FormatScriptName(v8::Local<v8::UnboundScript> script);

 private:
  friend class base::RefCountedDeleteOnSequence<AuctionV8Helper>;
  friend class base::DeleteHelper<AuctionV8Helper>;
  class ScriptTimeoutHelper;

  // Sets values of console_buffer() and console_script_name() to those
  // passed-in to its constructor for duration of its existence, and clears
  // them afterward.
  class ScopedConsoleTarget {
   public:
    ScopedConsoleTarget(AuctionV8Helper* owner,
                        const std::string& console_script_name,
                        std::vector<std::string>* out);
    ~ScopedConsoleTarget();

   private:
    raw_ptr<AuctionV8Helper> owner_;
  };

  explicit AuctionV8Helper(
      scoped_refptr<base::SingleThreadTaskRunner> v8_runner);
  ~AuctionV8Helper();

  void CreateIsolate();

  // These methods are used by DebugId, and except SetResumeCallback can be
  // called from any thread.
  int AllocContextGroupId();
  void SetResumeCallback(int context_group_id,
                         base::OnceClosure resume_callback);
  void AbortDebuggerPauses(int context_group_id);
  void FreeContextGroupId(int context_group_id);

  static std::string FormatExceptionMessage(v8::Local<v8::Context> context,
                                            v8::Local<v8::Message> message);
  static std::string FormatValue(v8::Isolate* isolate,
                                 v8::Local<v8::Value> val);

  scoped_refptr<base::SequencedTaskRunner> v8_runner_;
  scoped_refptr<base::SequencedTaskRunner> timer_task_runner_;

  std::unique_ptr<gin::IsolateHolder> isolate_holder_
      GUARDED_BY_CONTEXT(sequence_checker_);
  Console console_ GUARDED_BY_CONTEXT(sequence_checker_){this};
  v8::Global<v8::Context> scratch_context_
      GUARDED_BY_CONTEXT(sequence_checker_);
  // Script timeout. Can be changed for testing.
  base::TimeDelta script_timeout_ GUARDED_BY_CONTEXT(sequence_checker_) =
      kScriptTimeout;

  // See corresponding getters for description.
  raw_ptr<std::vector<std::string>> console_buffer_
      GUARDED_BY_CONTEXT(sequence_checker_) = nullptr;
  std::string console_script_name_ GUARDED_BY_CONTEXT(sequence_checker_);

  raw_ptr<ScriptTimeoutHelper> timeout_helper_
      GUARDED_BY_CONTEXT(sequence_checker_) = nullptr;

  base::Lock context_groups_lock_;
  int last_context_group_id_ GUARDED_BY(context_groups_lock_) = 0;

  // This is keyed by group IDs, and is used to keep track of what's valid.
  std::map<int, base::OnceClosure> resume_callbacks_
      GUARDED_BY(context_groups_lock_);

  scoped_refptr<DebugCommandQueue> debug_command_queue_;

  // Destruction order between `devtools_agent_` and `v8_inspector_` is
  // relevant; see also comment in ~AuctionV8Helper().
  std::unique_ptr<AuctionV8DevToolsAgent> devtools_agent_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<v8_inspector::V8Inspector> v8_inspector_
      GUARDED_BY_CONTEXT(sequence_checker_);

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace auction_worklet

#endif  // CONTENT_SERVICES_AUCTION_WORKLET_AUCTION_V8_HELPER_H_
