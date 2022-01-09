// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/cellular_esim_profile.h"

#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "dbus/object_path.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace chromeos {

TEST(CellularESimProfileTest, ConvertToAndFromDictionary) {
  CellularESimProfile profile(CellularESimProfile::State::kPending,
                              dbus::ObjectPath("/test/path/123"), "eid",
                              "iccid", u"name", u"nickname", u"serviceProvider",
                              "activationCode");

  base::Value dictionary = profile.ToDictionaryValue();
  absl::optional<CellularESimProfile> from_dictionary =
      CellularESimProfile::FromDictionaryValue(dictionary);
  EXPECT_TRUE(from_dictionary);

  EXPECT_EQ(CellularESimProfile::State::kPending, from_dictionary->state());
  EXPECT_EQ(dbus::ObjectPath("/test/path/123"), from_dictionary->path());
  EXPECT_EQ("eid", from_dictionary->eid());
  EXPECT_EQ("iccid", from_dictionary->iccid());
  EXPECT_EQ(u"name", from_dictionary->name());
  EXPECT_EQ(u"nickname", from_dictionary->nickname());
  EXPECT_EQ(u"serviceProvider", from_dictionary->service_provider());
  EXPECT_EQ("activationCode", from_dictionary->activation_code());
}

TEST(CellularESimProfileTest, InvalidDictionary) {
  // Try to convert a non-dictionary.
  base::Value non_dictionary(1337);
  absl::optional<CellularESimProfile> from_non_dictionary =
      CellularESimProfile::FromDictionaryValue(non_dictionary);
  EXPECT_FALSE(from_non_dictionary);

  // Try to convert a dictionary without the required keys.
  base::Value dictionary(base::Value::Type::DICTIONARY);
  dictionary.SetPath("sampleKey", base::Value("sampleValue"));
  absl::optional<CellularESimProfile> from_dictionary =
      CellularESimProfile::FromDictionaryValue(dictionary);
  EXPECT_FALSE(from_dictionary);
}

}  // namespace chromeos
