// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PERMISSIONS_PREF_NAMES_H_
#define COMPONENTS_PERMISSIONS_PREF_NAMES_H_

#include "build/build_config.h"

namespace permissions {
namespace prefs {

extern const char kPermissionActions[];
#if defined(OS_ANDROID)
extern const char kLocationSettingsBackoffLevelDSE[];
extern const char kLocationSettingsBackoffLevelDefault[];
extern const char kLocationSettingsNextShowDSE[];
extern const char kLocationSettingsNextShowDefault[];
#endif

}  // namespace prefs
}  // namespace permissions

#endif  // COMPONENTS_PERMISSIONS_PREF_NAMES_H_
