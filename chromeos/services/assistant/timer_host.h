// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_ASSISTANT_TIMER_HOST_H_
#define CHROMEOS_SERVICES_ASSISTANT_TIMER_HOST_H_

#include <memory>
#include <string>

#include "base/time/time.h"
#include "chromeos/services/libassistant/public/mojom/timer_controller.mojom-forward.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"

namespace ash {
class AssistantAlarmTimerController;
}  // namespace ash

namespace chromeos {
namespace assistant {

class ServiceContext;

// Handles all timer related interactions with Libassistant, which can broadly
// be separated in 2 responsibilities:
//   1) Let Libassistant know about updates to the timers (pause/add time/...).
//   2) Let |AssistantAlarmTimerController| know when Libassistant adds or
//      removes timers.
class TimerHost {
 public:
  explicit TimerHost(ServiceContext* context);
  TimerHost(const TimerHost&) = delete;
  TimerHost& operator=(const TimerHost&) = delete;
  ~TimerHost();

  void Initialize(
      chromeos::libassistant::mojom::TimerController* libassistant_controller,
      mojo::PendingReceiver<chromeos::libassistant::mojom::TimerDelegate>
          delegate);

  void AddTimeToTimer(const std::string& id, base::TimeDelta duration);
  void PauseTimer(const std::string& id);
  void RemoveTimer(const std::string& id);
  void ResumeTimer(const std::string& id);

 private:
  class TimerDelegateImpl;

  ash::AssistantAlarmTimerController* assistant_alarm_timer_controller();
  chromeos::libassistant::mojom::TimerController& libassistant_controller();

  bool IsStopped() const;

  // Owned by our parent |AssistantManagerServiceImpl|.
  chromeos::libassistant::mojom::TimerController* libassistant_controller_ =
      nullptr;
  std::unique_ptr<TimerDelegateImpl> timer_delegate_;

  // Owned by the parent |Service| which will destroy |this| before
  // |context_|.
  ServiceContext& context_;
};

}  // namespace assistant
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_ASSISTANT_TIMER_HOST_H_
