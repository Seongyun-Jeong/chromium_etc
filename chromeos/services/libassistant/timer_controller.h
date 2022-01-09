// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_LIBASSISTANT_TIMER_CONTROLLER_H_
#define CHROMEOS_SERVICES_LIBASSISTANT_TIMER_CONTROLLER_H_

#include <string>

#include "base/time/time.h"
#include "chromeos/services/libassistant/grpc/assistant_client_observer.h"
#include "chromeos/services/libassistant/public/mojom/timer_controller.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace chromeos {
namespace libassistant {

class TimerController : public mojom::TimerController,
                        public AssistantClientObserver {
 public:
  TimerController();
  TimerController(const TimerController&) = delete;
  TimerController& operator=(const TimerController&) = delete;
  ~TimerController() override;

  void Bind(mojo::PendingReceiver<mojom::TimerController> receiver,
            mojo::PendingRemote<mojom::TimerDelegate> delegate);

  // mojom::TimerController implementation:
  void AddTimeToTimer(const std::string& id,
                      ::base::TimeDelta duration) override;
  void PauseTimer(const std::string& id) override;
  void RemoveTimer(const std::string& id) override;
  void ResumeTimer(const std::string& id) override;

  // AssistantClientObserver implementation:
  void OnAssistantClientRunning(AssistantClient* assistant_client) override;
  void OnDestroyingAssistantClient(AssistantClient* assistant_client) override;

 private:
  class TimerListener;

  // Created when Libassistant is running, and destroyed when it stops.
  std::unique_ptr<TimerListener> timer_listener_;

  // Owned by |ServiceController|, set in OnAssistantClientRunning() and reset
  // in OnDestroyingAssistantClient().
  AssistantClient* assistant_client_ = nullptr;

  mojo::Receiver<mojom::TimerController> receiver_{this};
  mojo::Remote<mojom::TimerDelegate> delegate_;
};
}  // namespace libassistant
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_LIBASSISTANT_TIMER_CONTROLLER_H_
