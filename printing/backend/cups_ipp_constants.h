// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_BACKEND_CUPS_IPP_CONSTANTS_H_
#define PRINTING_BACKEND_CUPS_IPP_CONSTANTS_H_

#include "base/component_export.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"

namespace printing {

// property names
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kIppCollate[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kIppCopies[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kIppColor[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kIppMedia[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kIppDuplex[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kIppRequestingUserName[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kIppResolution[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kIppPin[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kIppPinEncryption[];

// collation values
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kCollated[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kUncollated[];

#if BUILDFLAG(IS_CHROMEOS)

COMPONENT_EXPORT(PRINT_BACKEND) extern const char kIppDocumentAttributes[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kIppJobAttributes[];

COMPONENT_EXPORT(PRINT_BACKEND) extern const char kPinEncryptionNone[];

COMPONENT_EXPORT(PRINT_BACKEND) extern const char kOptionFalse[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kOptionTrue[];

#endif  // BUILDFLAG(IS_CHROMEOS)

}  // namespace printing

#endif  // PRINTING_BACKEND_CUPS_IPP_CONSTANTS_H_
