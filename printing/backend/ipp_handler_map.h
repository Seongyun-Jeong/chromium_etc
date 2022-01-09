// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_BACKEND_IPP_HANDLER_MAP_H_
#define PRINTING_BACKEND_IPP_HANDLER_MAP_H_

#include <map>

#include "base/callback.h"
#include "base/strings/string_piece.h"
#include "printing/backend/print_backend.h"

namespace printing {

class CupsOptionProvider;

// Handles IPP attribute, usually by adding 1 or more items to `caps`.
using AttributeHandler =
    base::RepeatingCallback<void(const CupsOptionProvider& printer,
                                 const char* name,
                                 AdvancedCapabilities* caps)>;

using HandlerMap = std::map<base::StringPiece, AttributeHandler>;

// Produces mapping from attribute names to handlers based on their type.
// Implementation is generated by //printing/backend/tools/code_generator.py
// based on list provided by IANA.
HandlerMap GenerateHandlers();

}  // namespace printing

#endif  // PRINTING_BACKEND_IPP_HANDLER_MAP_H_
