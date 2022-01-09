// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_SEQUENTIAL_ID_GENERATOR_H_
#define UI_GFX_SEQUENTIAL_ID_GENERATOR_H_

#include <stdint.h>

#include <map>
#include <unordered_map>

#include "ui/gfx/gfx_export.h"

namespace ui {

// This is used to generate a series of sequential ID numbers in a way that a
// new ID is always the lowest possible ID in the sequence.
class GFX_EXPORT SequentialIDGenerator {
 public:
   // Creates a new generator with the specified lower bound for the IDs.
  explicit SequentialIDGenerator(uint32_t min_id);

  SequentialIDGenerator(const SequentialIDGenerator&) = delete;
  SequentialIDGenerator& operator=(const SequentialIDGenerator&) = delete;

  ~SequentialIDGenerator();

  // Generates a unique ID to represent |number|. The generated ID is the
  // smallest available ID greater than or equal to the |min_id| specified
  // during creation of the generator.
  uint32_t GetGeneratedID(uint32_t number);

  // Checks to see if the generator currently has a unique ID generated for
  // |number|.
  bool HasGeneratedIDFor(uint32_t number) const;

  // Removes the ID previously generated for |number| by calling
  // |GetGeneratedID()| - does nothing if the number is not mapped.
  void ReleaseNumber(uint32_t number);

  // Releases ID previously generated by calling |GetGeneratedID()|. Does
  // nothing if the ID is not mapped.
  void ReleaseID(uint32_t id);

  void ResetForTest();

 private:
  typedef std::unordered_map<uint32_t, uint32_t> IDMap;

  uint32_t GetNextAvailableID();

  void UpdateNextAvailableIDAfterRelease(uint32_t id);

  IDMap number_to_id_;
  IDMap id_to_number_;

  const uint32_t min_id_;
  uint32_t min_available_id_;
};

}  // namespace ui

#endif  // UI_GFX_SEQUENTIAL_ID_GENERATOR_H_
