// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/at_exit.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
#include "base/test/test_timeouts.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/time/time.h"

int main(int argc, char* argv[]) {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);
  TestTimeouts::Initialize();
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::SYSTEM_TIME};

  if (argc <= 1) {
    LOG(INFO) << argv[0] << ": missing operand";
    return -1;
  }

  int duration_seconds = 0;
  if (!base::StringToInt(argv[1], &duration_seconds) || duration_seconds < 0) {
    LOG(INFO) << ": invalid time interval '" << argv[1] << "'";
    return -1;
  }

  base::RunLoop run_loop;
  base::TimeDelta duration = base::Seconds(duration_seconds);

  base::SequencedTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::BindOnce(run_loop.QuitClosure()), duration);

  // Tasks are run asynchronously, so this will print before the task runs.
  LOG(INFO) << "Going to sleep for " << duration_seconds << " seconds...";

  // Runs all the tasks that have been posted to the |SequencedTaskRunner|.
  run_loop.Run();

  // This will NOT complete until |run_loop.QuitClosure()| runs.
  LOG(INFO) << "I'm awake!";

  return 0;
}