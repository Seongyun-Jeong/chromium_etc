// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_PARSING_AUTOFILL_PARSING_UTILS_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_PARSING_AUTOFILL_PARSING_UTILS_H_

#include <string>

#include "components/autofill/core/common/dense_set.h"
#include "components/autofill/core/common/language_code.h"

namespace autofill {

// The sources from which strings are matched: the field's label or its name or
// id attribute value.
//
// For example, in
// <label for="mobile">Cellphone number:</label> <input type="tel" id="mobile">
// the kLabel is "Cellphone number" and the kName is "mobile".
enum class MatchAttribute { kLabel, kName, kMaxValue = kName };

// The types of fields which may be matched.
//
// For example, in
// <label for="mobile">Cellphone number:</label> <input type="tel" id="mobile">
// the MatchFieldType is kTelephone.
enum class MatchFieldType {
  kText,
  kEmail,
  kTelephone,
  kSelect,
  kTextArea,
  kPassword,
  kNumber,
  kSearch,
  kMaxValue = kSearch
};

// Contains all MatchAttribute constants.
constexpr DenseSet<MatchAttribute> kAllMatchAttributes{MatchAttribute::kLabel,
                                                       MatchAttribute::kName};

// Contains all MatchFieldType constants.
constexpr DenseSet<MatchFieldType> kAllMatchFieldTypes{
    MatchFieldType::kText,      MatchFieldType::kEmail,
    MatchFieldType::kTelephone, MatchFieldType::kSelect,
    MatchFieldType::kTextArea,  MatchFieldType::kPassword,
    MatchFieldType::kNumber,    MatchFieldType::kSearch};

// A pair of sets of MatchAttributes and MatchFieldTypes.
struct MatchParams {
  inline constexpr MatchParams(DenseSet<MatchAttribute> attributes,
                               DenseSet<MatchFieldType> field_types);
  inline constexpr MatchParams(const MatchParams&);
  inline constexpr MatchParams& operator=(const MatchParams&);
  inline constexpr MatchParams(MatchParams&&);
  inline constexpr MatchParams& operator=(MatchParams&&);

  DenseSet<MatchAttribute> attributes;
  DenseSet<MatchFieldType> field_types;
};

inline constexpr MatchParams::MatchParams(DenseSet<MatchAttribute> attributes,
                                          DenseSet<MatchFieldType> field_types)
    : attributes(attributes), field_types(field_types) {}
inline constexpr MatchParams::MatchParams(const MatchParams&) = default;
inline constexpr MatchParams& MatchParams::operator=(const MatchParams&) =
    default;
inline constexpr MatchParams::MatchParams(MatchParams&&) = default;
inline constexpr MatchParams& MatchParams::operator=(MatchParams&&) = default;

// By default match label and name for <input type="text"> elements.
template <MatchFieldType... additional_match_field_types>
constexpr MatchParams kDefaultMatchParamsWith{
    kAllMatchAttributes,
    {MatchFieldType::kText, additional_match_field_types...}};

constexpr MatchParams kDefaultMatchParams = kDefaultMatchParamsWith<>;

// Structure for a better organization of data and regular expressions
// for autofill regex_constants. In the future, to implement faster
// changes without global updates also for having a quick possibility
// to recognize incorrect matches.
struct MatchingPattern {
  MatchingPattern();
  MatchingPattern(const MatchingPattern&);
  MatchingPattern& operator=(const MatchingPattern&);
  MatchingPattern(MatchingPattern&&);
  MatchingPattern& operator=(MatchingPattern&&);
  ~MatchingPattern();

  LanguageCode language;
  std::u16string positive_pattern;
  std::u16string negative_pattern;
  float positive_score = 1.1;
  DenseSet<MatchAttribute> match_field_attributes;
  DenseSet<MatchFieldType> match_field_input_types;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_PARSING_AUTOFILL_PARSING_UTILS_H_
