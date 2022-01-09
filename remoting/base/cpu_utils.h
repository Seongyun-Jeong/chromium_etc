// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_BASE_CPU_UTILS_H_
#define REMOTING_BASE_CPU_UTILS_H_

namespace remoting {

// Returns true if the CPU meets the minimum hardware requirements (e.g. it
// supports a minimum required instruction set), otherwise false.
bool IsCpuSupported();

}  // namespace remoting

#endif  // REMOTING_BASE_CPU_UTILS_H_
