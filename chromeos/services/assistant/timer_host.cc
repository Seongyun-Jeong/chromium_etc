// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/assistant/timer_host.h"

#include "ash/public/cpp/assistant/controller/assistant_alarm_timer_controller.h"
#include "chromeos/services/assistant/public/cpp/features.h"
#include "chromeos/services/assistant/service_context.h"
#include "chromeos/services/libassistant/public/cpp/assistant_timer.h"
#include "chromeos/services/libassistant/public/mojom/timer_controller.mojom.h"

namespace chromeos {
namespace assistant {

////////////////////////////////////////////////////////////////////////////////
// TimerDelegateImpl
////////////////////////////////////////////////////////////////////////////////

class TimerHost::TimerDelegateImpl
    : public chromeos::libassistant::mojom::TimerDelegate {
 public:
  explicit TimerDelegateImpl(
      mojo::PendingReceiver<TimerDelegate> pending_receiver,
      ServiceContext* context)
      : receiver_(this, std::move(pending_receiver)), context_(*context) {}
  TimerDelegateImpl(const TimerDelegateImpl&) = delete;
  TimerDelegateImpl& operator=(const TimerDelegateImpl&) = delete;
  ~TimerDelegateImpl() override = default;

 private:
  // chromeos::libassistant::mojom::TimerDelegate implementation:
  void OnTimerStateChanged(const std::vector<AssistantTimer>& timers) override {
    assistant_alarm_timer_controller().OnTimerStateChanged(timers);
  }

  ash::AssistantAlarmTimerController& assistant_alarm_timer_controller() {
    auto* result = context_.assistant_alarm_timer_controller();
    DCHECK(result);
    return *result;
  }

  mojo::Receiver<TimerDelegate> receiver_;

  // Owned by the parent |Service|.
  ServiceContext& context_;
};

////////////////////////////////////////////////////////////////////////////////
// TimerHost
////////////////////////////////////////////////////////////////////////////////

TimerHost::TimerHost(ServiceContext* context) : context_(*context) {
  DCHECK(context);
}

TimerHost::~TimerHost() = default;

void TimerHost::Initialize(
    chromeos::libassistant::mojom::TimerController* libassistant_controller,
    mojo::PendingReceiver<chromeos::libassistant::mojom::TimerDelegate>
        delegate) {
  timer_delegate_ =
      std::make_unique<TimerDelegateImpl>(std::move(delegate), &context_);
  libassistant_controller_ = libassistant_controller;
}

void TimerHost::AddTimeToTimer(const std::string& id,
                               base::TimeDelta duration) {
  libassistant_controller().AddTimeToTimer(id, duration);
}

void TimerHost::PauseTimer(const std::string& id) {
  libassistant_controller().PauseTimer(id);
}

void TimerHost::RemoveTimer(const std::string& id) {
  libassistant_controller().RemoveTimer(id);
}

void TimerHost::ResumeTimer(const std::string& id) {
  libassistant_controller().ResumeTimer(id);
}

chromeos::libassistant::mojom::TimerController&
TimerHost::libassistant_controller() {
  DCHECK(libassistant_controller_);
  return *libassistant_controller_;
}

}  // namespace assistant
}  // namespace chromeos
