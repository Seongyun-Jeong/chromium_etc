// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/device/generic_sensor/platform_sensor_provider.h"

#include "build/chromeos_buildflags.h"

#if BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chromeos/components/sensors/buildflags.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_CHROMEOS_LACROS)

#if defined(OS_MAC)
#include "services/device/generic_sensor/platform_sensor_provider_mac.h"
#elif defined(OS_ANDROID)
#include "services/device/generic_sensor/platform_sensor_provider_android.h"
#elif defined(OS_WIN)
#include "base/win/windows_version.h"
#include "build/build_config.h"
#include "services/device/generic_sensor/platform_sensor_provider_win.h"
#include "services/device/generic_sensor/platform_sensor_provider_winrt.h"
#elif BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_CHROMEOS_LACROS)
#if BUILDFLAG(USE_IIOSERVICE)
#include "services/device/generic_sensor/platform_sensor_provider_chromeos.h"
#elif defined(USE_UDEV)
#include "services/device/generic_sensor/platform_sensor_provider_linux.h"
#endif  // BUILDFLAG(USE_IIOSERVICE)
#elif defined(OS_LINUX) && defined(USE_UDEV)
#include "services/device/generic_sensor/platform_sensor_provider_linux.h"
#endif

namespace device {

// static
std::unique_ptr<PlatformSensorProvider> PlatformSensorProvider::Create() {
#if defined(OS_MAC)
  return std::make_unique<PlatformSensorProviderMac>();
#elif defined(OS_ANDROID)
  return std::make_unique<PlatformSensorProviderAndroid>();
#elif defined(OS_WIN)
  if (PlatformSensorProvider::UseWindowsWinrt()) {
    return std::make_unique<PlatformSensorProviderWinrt>();
  } else {
    return std::make_unique<PlatformSensorProviderWin>();
  }
#elif BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_CHROMEOS_LACROS)
#if BUILDFLAG(USE_IIOSERVICE)
  return std::make_unique<PlatformSensorProviderChromeOS>();
#elif defined(USE_UDEV)
  return std::make_unique<PlatformSensorProviderLinux>();
#endif  // BUILDFLAG(USE_IIOSERVICE)
#elif defined(OS_LINUX) && defined(USE_UDEV)
  return std::make_unique<PlatformSensorProviderLinux>();
#else
  return nullptr;
#endif
}

#if defined(OS_WIN)
// static
bool PlatformSensorProvider::UseWindowsWinrt() {
  // TODO: Windows version dependency should eventually be updated to
  // a future version which supports WinRT sensor thresholding. Since
  // this Windows version has yet to be released, Win10 is being
  // provisionally used for testing. This also means sensors will
  // stream if this implementation path is enabled.

  // Note the fork occurs specifically on the 19H1 build of Win10
  // because a previous version (RS5) contains an access violation
  // issue in the WinRT APIs which causes the client code to crash.
  // See http://crbug.com/1063124
  return base::win::GetVersion() >= base::win::Version::WIN10_19H1;
}
#endif

}  // namespace device
