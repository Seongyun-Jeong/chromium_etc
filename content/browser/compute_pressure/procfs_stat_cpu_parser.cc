// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compute_pressure/procfs_stat_cpu_parser.h"

#include <stdint.h>

#include <limits>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/sequence_checker.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece_forward.h"
#include "base/strings/string_split.h"
#include "base/system/sys_info.h"

namespace content {

double ProcfsStatCpuParser::CoreTimes::TimeUtilization(
    const CoreTimes& baseline) const {
  // Each of the blocks below consists of a check and a subtraction. The check
  // is used to bail on invalid input (/proc/stat counters should never
  // decrease over time).
  //
  // The check is also essential for the correctness of the subtraction -- the
  // result of the subtraction is stored in a temporary `uint64_t` before being
  // accumulated in `active_delta`, and this intermediate result must not be
  // negative.

  if (user() < baseline.user())
    return -1;
  double active_delta = user() - baseline.user();

  if (nice() < baseline.nice())
    return -1;
  active_delta += nice() - baseline.nice();

  if (system() < baseline.system())
    return -1;
  active_delta += system() - baseline.system();

  if (idle() < baseline.idle())
    return -1;
  uint64_t idle_delta = idle() - baseline.idle();

  // iowait() is unreliable, according to the Linux kernel documentation at
  // https://www.kernel.org/doc/Documentation/filesystems/proc.txt.

  if (irq() < baseline.irq())
    return -1;
  active_delta += irq() - baseline.irq();

  if (softirq() < baseline.softirq())
    return -1;
  active_delta += softirq() - baseline.softirq();

  if (steal() < baseline.steal())
    return -1;
  active_delta += steal() - baseline.steal();

  // guest() and guest_nice() are included in user(). Full analysis in
  // https://unix.stackexchange.com/a/303224/

  double total_delta = active_delta + idle_delta;
  if (total_delta == 0) {
    // The two snapshots represent the same point in time, so the time interval
    // between the two snapshots is empty.
    return -1;
  }

  return active_delta / total_delta;
}

constexpr base::FilePath::CharType ProcfsStatCpuParser::kProcfsStatPath[];

ProcfsStatCpuParser::ProcfsStatCpuParser(base::FilePath stat_path)
    : stat_path_(std::move(stat_path)) {
  core_times_.reserve(base::SysInfo::NumberOfProcessors());
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

ProcfsStatCpuParser::~ProcfsStatCpuParser() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool ProcfsStatCpuParser::Update() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // This implementation takes advantage of the fact that /proc/stat has 8
  // lines in addition to the per-core lines (cpu0...cpuN). These 8 lines are
  // cpu, intr, ctxt, btime, processes, procs_running, procs_blocked, softirq.
  // Each of these lines consists of a small number of tokens. Each
  // token has a small upper-bound on its size, because tokens are 64-bit
  // base-10 numbers.
  //
  // This has the following consequences.
  // 1) Reading the whole file in memory has a constant size/memory overhead,
  //    relative to the class' usage of per-core CoreTime structs.
  // 2) Splitting the entire file into lines and processing each line has a
  //    constant size/memory overhead compared to a streaming parser that
  //    ignores irrelevant data and stops after the last per-core line (cpuN).
  std::string stat_bytes;

  // This implementation could use base::ReadFileToStringWithMaxSize() to avoid
  // the risk that a kernel bug leads to an OOM. The size limit depends on the
  // maximum number of cores we'd want to support.
  //
  // Each CPU line has ~220 bytes, and the other lines should amount to less
  // than 10,000 bytes. So, for example, a limit of 2.3Mb should be sufficient
  // to support systems up to 10,000 cores.
  if (!base::ReadFileToString(stat_path_, &stat_bytes))
    return false;

  static constexpr base::StringPiece kNewlineSeparator("\n", 1);
  std::vector<base::StringPiece> stat_lines = base::SplitStringPiece(
      stat_bytes, kNewlineSeparator, base::WhitespaceHandling::KEEP_WHITESPACE,
      base::SplitResult::SPLIT_WANT_ALL);
  for (base::StringPiece stat_line : stat_lines) {
    int core_id = CoreIdFromLine(stat_line);
    if (core_id < 0)
      continue;

    DCHECK_LE(core_times_.size(), size_t{std::numeric_limits<int>::max()});
    if (static_cast<int>(core_times_.size()) <= core_id)
      core_times_.resize(core_id + 1);

    CoreTimes& current_core_times = core_times_[core_id];
    UpdateCore(stat_line, current_core_times);
  }

  return true;
}

// static
int ProcfsStatCpuParser::CoreIdFromLine(base::StringPiece stat_line) {
  // The first token of valid lines is cpu<number>. The token is at least 4
  // characters ("cpu" plus one digit).
  auto space_index = stat_line.find(' ');
  if (space_index < 4 || space_index == base::StringPiece::npos)
    return -1;

  if (stat_line[0] != 'c' || stat_line[1] != 'p' || stat_line[2] != 'u')
    return -1;
  base::StringPiece core_id_string = stat_line.substr(3, space_index - 3);

  int core_id;
  if (!base::StringToInt(core_id_string, &core_id) || core_id < 0)
    return -1;

  return core_id;
}

// static
void ProcfsStatCpuParser::UpdateCore(base::StringPiece core_line,
                                     CoreTimes& core_times) {
  DCHECK_GE(CoreIdFromLine(core_line), 0);

  static constexpr base::StringPiece kSpaceSeparator(" ", 1);
  std::vector<base::StringPiece> tokens = base::SplitStringPiece(
      core_line, kSpaceSeparator, base::WhitespaceHandling::KEEP_WHITESPACE,
      base::SplitResult::SPLIT_WANT_ALL);

  // Accept lines with more than 10 numbers, so the code keeps working if
  // /proc/stat is extended with new per-core metrics.
  //
  // The first token on the line is the "cpuN" core ID. One core ID plus 10
  // numbers equals 11 tokens.
  if (tokens.size() < 11)
    return;

  for (int i = 0; i < 10; ++i) {
    uint64_t parsed_number;
    if (!base::StringToUint64(tokens[i + 1], &parsed_number))
      return;

    // Ensure that the reported core usage times are monotonically increasing.
    // We assume that by any decrease is a temporary blip.
    if (core_times.times[i] < parsed_number)
      core_times.times[i] = parsed_number;
  }
}

}  // namespace content
