// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_NATIVE_UNWINDER_MAC_H_
#define BASE_PROFILER_NATIVE_UNWINDER_MAC_H_

#include <libunwind.h>
#include <vector>

#include "base/profiler/unwinder.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {

// Native unwinder implementation for Mac, using libunwind.
class NativeUnwinderMac : public Unwinder {
 public:
  NativeUnwinderMac(ModuleCache* module_cache);

  NativeUnwinderMac(const NativeUnwinderMac&) = delete;
  NativeUnwinderMac& operator=(const NativeUnwinderMac&) = delete;

  // Unwinder:
  bool CanUnwindFrom(const Frame& current_frame) const override;
  UnwindResult TryUnwind(RegisterContext* thread_context,
                         uintptr_t stack_top,
                         std::vector<Frame>* stack) const override;

 private:
  absl::optional<UnwindResult> CheckPreconditions(const Frame* current_frame,
                                                  unw_cursor_t* unwind_cursor,
                                                  uintptr_t stack_top) const;

  // Returns the result from unw_step.
  int UnwindStep(unw_context_t* unwind_context,
                 unw_cursor_t* cursor,
                 bool at_first_frame) const;

  absl::optional<UnwindResult> CheckPostconditions(
      int step_result,
      unw_word_t prev_rsp,
      unw_word_t rsp,
      uintptr_t stack_top,
      bool* should_record_frame) const;

  // Cached pointer to the libsystem_kernel module.
  const ModuleCache::Module* const libsystem_kernel_module_;

  // The address range of |_sigtramp|, the signal trampoline function.
  uintptr_t sigtramp_start_;
  uintptr_t sigtramp_end_;
};

}  // namespace base

#endif  // BASE_PROFILER_NATIVE_UNWINDER_MAC_H_
