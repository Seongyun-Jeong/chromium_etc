// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOGIN_SCREEN_MANAGER_H_
#define CHROME_BROWSER_ASH_LOGIN_SCREEN_MANAGER_H_

#include <map>
#include <memory>

#include "base/gtest_prod_util.h"
#include "chrome/browser/ash/login/oobe_screen.h"

namespace ash {
class BaseScreen;

// Class that manages creation and ownership of screens.
class ScreenManager {
 public:
  ScreenManager();

  ScreenManager(const ScreenManager&) = delete;
  ScreenManager& operator=(const ScreenManager&) = delete;

  ~ScreenManager();

  // Initialize all screen instances.
  void Init(std::vector<std::unique_ptr<BaseScreen>> screens);

  // Getter for screen. Does not create the screen.
  BaseScreen* GetScreen(OobeScreenId screen);

  bool HasScreen(OobeScreenId screen);

  void SetScreenForTesting(std::unique_ptr<BaseScreen> value);
  void DeleteScreenForTesting(OobeScreenId screen);

 private:
  // Created screens.
  std::map<OobeScreenId, std::unique_ptr<BaseScreen>> screens_;
};

}  // namespace ash

#endif  // CHROME_BROWSER_ASH_LOGIN_SCREEN_MANAGER_H_
