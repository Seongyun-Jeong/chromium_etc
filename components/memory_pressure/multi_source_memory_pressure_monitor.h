// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MEMORY_PRESSURE_MULTI_SOURCE_MEMORY_PRESSURE_MONITOR_H_
#define COMPONENTS_MEMORY_PRESSURE_MULTI_SOURCE_MEMORY_PRESSURE_MONITOR_H_

#include "base/memory/memory_pressure_monitor.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/memory_pressure/memory_pressure_level_reporter.h"
#include "components/memory_pressure/memory_pressure_voter.h"

namespace memory_pressure {

class SystemMemoryPressureEvaluator;

// This is a specialization of a MemoryPressureMonitor that relies on a set of
// MemoryPressureVoters to determine the memory pressure state. The
// MemoryPressureVoteAggregator is in charge of receiving votes from these
// voters and notifying MemoryPressureListeners of the MemoryPressureLevel via
// the monitor's |dispatch_callback_|. The pressure level is calculated as the
// most critical of all votes collected.
// This class is not thread safe and should be used from a single sequence.
class MultiSourceMemoryPressureMonitor
    : public base::MemoryPressureMonitor,
      public MemoryPressureVoteAggregator::Delegate {
 public:
  using MemoryPressureLevel = base::MemoryPressureMonitor::MemoryPressureLevel;
  using DispatchCallback = base::MemoryPressureMonitor::DispatchCallback;

  MultiSourceMemoryPressureMonitor();
  ~MultiSourceMemoryPressureMonitor() override;

  MultiSourceMemoryPressureMonitor(const MultiSourceMemoryPressureMonitor&) =
      delete;
  MultiSourceMemoryPressureMonitor& operator=(
      const MultiSourceMemoryPressureMonitor&) = delete;

  // Start monitoring memory pressure using the platform-specific voter.
  void Start();

  // MemoryPressureMonitor implementation.
  MemoryPressureLevel GetCurrentPressureLevel() const override;
  void SetDispatchCallback(const DispatchCallback& callback) override;

  // Creates a MemoryPressureVoter to be owned/used by a source that wishes to
  // have input on the overall memory pressure level.
  std::unique_ptr<MemoryPressureVoter> CreateVoter();

  MemoryPressureVoteAggregator* aggregator_for_testing() {
    return &aggregator_;
  }

  void ResetSystemEvaluatorForTesting();

  void SetSystemEvaluator(
      std::unique_ptr<SystemMemoryPressureEvaluator> evaluator);

 private:
  // Delegate implementation.
  void OnMemoryPressureLevelChanged(MemoryPressureLevel level) override;
  void OnNotifyListenersRequested() override;

  MemoryPressureLevel current_pressure_level_;

  DispatchCallback dispatch_callback_;

  MemoryPressureVoteAggregator aggregator_;

  std::unique_ptr<SystemMemoryPressureEvaluator> system_evaluator_;

  // The timestamp of the last pressure change event.
  base::TimeTicks last_pressure_change_timestamp_;

  MemoryPressureLevelReporter level_reporter_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace memory_pressure

#endif  // COMPONENTS_MEMORY_PRESSURE_MULTI_SOURCE_MEMORY_PRESSURE_MONITOR_H_
