// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_GEO_AUTOFILL_COUNTRY_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_GEO_AUTOFILL_COUNTRY_H_

#include <string>

#include "components/autofill/core/browser/geo/country_data.h"

namespace autofill {

class LogBuffer;

// Stores data associated with a country. Strings are localized to the app
// locale.
class AutofillCountry {
 public:
  // Returns country data corresponding to the two-letter ISO code
  // |country_code|.
  AutofillCountry(const std::string& country_code, const std::string& locale);

  AutofillCountry(const AutofillCountry&) = delete;
  AutofillCountry& operator=(const AutofillCountry&) = delete;

  ~AutofillCountry();

  // Returns the likely country code for |locale|, or "US" as a fallback if no
  // mapping from the locale is available.
  static const std::string CountryCodeForLocale(const std::string& locale);

  const std::string& country_code() const { return country_code_; }
  const std::u16string& name() const { return name_; }

  // City is expected in a complete address for this country.
  bool requires_city() const {
    return (required_fields_for_address_import_ & ADDRESS_REQUIRES_CITY) != 0;
  }

  // State is expected in a complete address for this country.
  bool requires_state() const {
    return (required_fields_for_address_import_ & ADDRESS_REQUIRES_STATE) != 0;
  }

  // Zip is expected in a complete address for this country.
  bool requires_zip() const {
    return (required_fields_for_address_import_ & ADDRESS_REQUIRES_ZIP) != 0;
  }

  // An address line1 is expected in a complete address for this country.
  bool requires_line1() const {
    return (required_fields_for_address_import_ & ADDRESS_REQUIRES_LINE1) != 0;
  }

  // True if a complete address is expected to either contain a state or a ZIP
  // code. Not true if the address explicitly needs both.
  bool requires_zip_or_state() const {
    return (required_fields_for_address_import_ &
            ADDRESS_REQUIRES_ZIP_OR_STATE) != 0;
  }

  bool requires_line1_or_house_number() const {
    return (required_fields_for_address_import_ &
            ADDRESS_REQUIRES_LINE1_OR_HOUSE_NUMBER);
  }

 private:
  AutofillCountry(const std::string& country_code,
                  const std::u16string& name,
                  const std::u16string& postal_code_label,
                  const std::u16string& state_label);

  // The two-letter ISO-3166 country code.
  std::string country_code_;

  // The country's name, localized to the app locale.
  std::u16string name_;

  // Required fields for an address import for the country.
  RequiredFieldsForAddressImport required_fields_for_address_import_;
};

LogBuffer& operator<<(LogBuffer& buffer, const AutofillCountry& country);

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_GEO_AUTOFILL_COUNTRY_H_
