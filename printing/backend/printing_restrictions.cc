// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printing/backend/printing_restrictions.h"

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"

namespace printing {

#if BUILDFLAG(IS_CHROMEOS)
const char kAllowedColorModes[] = "allowedColorModes";
const char kAllowedDuplexModes[] = "allowedDuplexModes";
const char kAllowedPinModes[] = "allowedPinModes";
const char kDefaultColorMode[] = "defaultColorMode";
const char kDefaultDuplexMode[] = "defaultDuplexMode";
const char kDefaultPinMode[] = "defaultPinMode";
#endif  // BUILDFLAG(IS_CHROMEOS)

const char kPaperSizeName[] = "name";
const char kPaperSizeNameCustomOption[] = "custom";
const char kPaperSizeCustomSize[] = "custom_size";
const char kPaperSizeWidth[] = "width";
const char kPaperSizeHeight[] = "height";

}  // namespace printing
