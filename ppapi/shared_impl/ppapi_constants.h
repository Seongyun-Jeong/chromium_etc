// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_SHARED_IMPL_PPAPI_CONSTANTS_H_
#define PPAPI_SHARED_IMPL_PPAPI_CONSTANTS_H_

#include "build/build_config.h"
#include "ppapi/shared_impl/ppapi_shared_export.h"

namespace ppapi {

#if defined(OS_WIN)
const char kCorbTestPluginName[] = "corb_test_plugin.dll";
#elif defined(OS_MAC)
const char kCorbTestPluginName[] = "corb_test_plugin.plugin";
#elif defined(OS_POSIX)
const char kCorbTestPluginName[] = "libcorb_test_plugin.so";
#endif

}  // namespace ppapi

#endif  // PPAPI_SHARED_IMPL_PPAPI_CONSTANTS_H_
