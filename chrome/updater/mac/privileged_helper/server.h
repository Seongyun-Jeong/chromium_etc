// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_UPDATER_MAC_PRIVILEGED_HELPER_SERVER_H_
#define CHROME_UPDATER_MAC_PRIVILEGED_HELPER_SERVER_H_

#include "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "chrome/updater/app/app.h"
#include "chrome/updater/mac/privileged_helper/service.h"

namespace base {
class SequencedTaskRunner;
}

namespace updater {

class PrivilegedHelperServer : public App {
 public:
  PrivilegedHelperServer();

 protected:
  ~PrivilegedHelperServer() override;

 private:
  SEQUENCE_CHECKER(sequence_checker_);

  // Overrides for App.
  void Initialize() override;
  void FirstTaskRun() override;
  void Uninitialize() override;

  scoped_refptr<base::SequencedTaskRunner> main_task_runner_;
  scoped_refptr<PrivilegedHelperService> service_;
  base::scoped_nsobject<NSXPCListener> service_listener_;
  base::scoped_nsobject<PrivilegedHelperServiceXPCDelegate> service_delegate_;
};

scoped_refptr<App> PrivilegedHelperServerInstance();

}  // namespace updater

#endif  // CHROME_UPDATER_MAC_PRIVILEGED_HELPER_SERVER_H_