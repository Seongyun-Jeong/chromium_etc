// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_COMPONENTS_QUICK_ANSWERS_UTILS_UNIT_CONVERTER_H_
#define CHROMEOS_COMPONENTS_QUICK_ANSWERS_UTILS_UNIT_CONVERTER_H_

#include <string>

#include "base/values.h"

namespace quick_answers {

// Utility class for unit conversion.
class UnitConverter {
 public:
  explicit UnitConverter(const base::Value& rule_set);

  UnitConverter(const UnitConverter&) = delete;
  UnitConverter& operator=(const UnitConverter&) = delete;

  ~UnitConverter();

  // Convert the |src_value| from |src_unit| to |dst_unit|.
  const std::string Convert(const double src_value,
                            const base::Value& src_unit,
                            const base::Value& dst_unit);

  // Find the unit with the closest conversion rate within the preferred range.
  // Return nullptr if no proper unit type found.
  const base::Value* FindProperDestinationUnit(const base::Value& src_unit,
                                               const double preferred_range);

  // Get the list of conversion rates for the given category.
  const base::Value* GetConversionForCategory(
      const std::string& target_category);

  const base::Value* GetPossibleUnitsForCategory(
      const std::string& target_category);

 private:
  // Conversion rule set for supported unit types.
  // |rules_set_| needs to outlive the converter.
  const base::Value& rule_set_;
};

}  // namespace quick_answers

#endif  // CHROMEOS_COMPONENTS_QUICK_ANSWERS_UTILS_UNIT_CONVERTER_H_
