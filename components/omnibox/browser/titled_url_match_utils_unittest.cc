// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/omnibox/browser/titled_url_match_utils.h"

#include <memory>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "components/bookmarks/browser/titled_url_match.h"
#include "components/bookmarks/browser/titled_url_node.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/omnibox_field_trial.h"
#include "components/omnibox/browser/test_scheme_classifier.h"
#include "components/omnibox/common/omnibox_features.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "url/gurl.h"

using bookmarks::TitledUrlMatchToAutocompleteMatch;

namespace {

// A simple AutocompleteProvider that does nothing.
class FakeAutocompleteProvider : public AutocompleteProvider {
 public:
  explicit FakeAutocompleteProvider(Type type) : AutocompleteProvider(type) {}

  void Start(const AutocompleteInput& input, bool minimal_changes) override {}

 private:
  ~FakeAutocompleteProvider() override = default;
};

class MockTitledUrlNode : public bookmarks::TitledUrlNode {
 public:
  MockTitledUrlNode(const std::u16string& title,
                    const GURL& url,
                    std::vector<std::u16string> ancestors = {})
      : title_(title), url_(url), ancestors_(ancestors) {}

  // TitledUrlNode
  const std::u16string& GetTitledUrlNodeTitle() const override {
    return title_;
  }
  const GURL& GetTitledUrlNodeUrl() const override { return url_; }
  std::vector<base::StringPiece16> GetTitledUrlNodeAncestorTitles()
      const override {
    std::vector<base::StringPiece16> ancestors;
    std::transform(
        ancestors_.begin(), ancestors_.end(), std::back_inserter(ancestors),
        [](auto& ancestor) { return base::StringPiece16(ancestor); });
    return ancestors;
  }

 private:
  std::u16string title_;
  GURL url_;
  std::vector<std::u16string> ancestors_;
};

std::string ACMatchClassificationsAsString(
    const ACMatchClassifications& classifications) {
  std::string position_string("{");
  for (auto classification : classifications) {
    position_string +=
        "{offset " + base::NumberToString(classification.offset) + ", style " +
        base::NumberToString(classification.style) + "}, ";
  }
  position_string += "}\n";
  return position_string;
}

}  // namespace

TEST(TitledUrlMatchUtilsTest, TitledUrlMatchToAutocompleteMatch) {
  std::u16string input_text(u"goo");
  std::u16string match_title(u"Google Search");
  GURL match_url("https://www.google.com/");
  AutocompleteMatchType::Type type = AutocompleteMatchType::BOOKMARK_TITLE;
  int relevance = 123;

  MockTitledUrlNode node(match_title, match_url);
  bookmarks::TitledUrlMatch titled_url_match;
  titled_url_match.node = &node;
  titled_url_match.title_match_positions = {{0, 3}};
  titled_url_match.url_match_positions = {{12, 15}};

  scoped_refptr<FakeAutocompleteProvider> provider =
      new FakeAutocompleteProvider(AutocompleteProvider::Type::TYPE_BOOKMARK);
  TestSchemeClassifier classifier;
  AutocompleteInput input(input_text, metrics::OmniboxEventProto::NTP,
                          classifier);
  const std::u16string fixed_up_input(input_text);

  AutocompleteMatch autocomplete_match = TitledUrlMatchToAutocompleteMatch(
      titled_url_match, type, relevance, provider.get(), classifier, input,
      fixed_up_input);

  ACMatchClassifications expected_contents_class = {
      {0, ACMatchClassification::URL | ACMatchClassification::MATCH},
      {3, ACMatchClassification::URL},
  };
  ACMatchClassifications expected_description_class = {
      {0, ACMatchClassification::MATCH}, {3, ACMatchClassification::NONE},
  };
  std::u16string expected_inline_autocompletion(u"gle.com");

  EXPECT_EQ(provider.get(), autocomplete_match.provider);
  EXPECT_EQ(type, autocomplete_match.type);
  EXPECT_EQ(relevance, autocomplete_match.relevance);
  EXPECT_EQ(match_url, autocomplete_match.destination_url);
  EXPECT_EQ(u"google.com", autocomplete_match.contents);
  EXPECT_TRUE(std::equal(expected_contents_class.begin(),
                         expected_contents_class.end(),
                         autocomplete_match.contents_class.begin()))
      << "EXPECTED: " << ACMatchClassificationsAsString(expected_contents_class)
      << "ACTUAL:   "
      << ACMatchClassificationsAsString(autocomplete_match.contents_class);
  EXPECT_EQ(match_title, autocomplete_match.description);
  EXPECT_TRUE(std::equal(expected_description_class.begin(),
                         expected_description_class.end(),
                         autocomplete_match.description_class.begin()));
  EXPECT_EQ(u"https://www.google.com", autocomplete_match.fill_into_edit);
  EXPECT_TRUE(autocomplete_match.allowed_to_be_default_match);
  EXPECT_EQ(expected_inline_autocompletion,
            autocomplete_match.inline_autocompletion);
}

AutocompleteMatch BuildTestAutocompleteMatch(
    const std::string& input_text_s,
    const GURL& match_url,
    const bookmarks::TitledUrlMatch::MatchPositions& match_positions) {
  std::u16string input_text(base::ASCIIToUTF16(input_text_s));
  std::u16string match_title(u"The Facebook");
  AutocompleteMatchType::Type type = AutocompleteMatchType::BOOKMARK_TITLE;
  int relevance = 123;

  MockTitledUrlNode node(match_title, match_url);
  bookmarks::TitledUrlMatch titled_url_match;
  titled_url_match.node = &node;
  titled_url_match.title_match_positions = {{0, 3}};
  // Don't capture the scheme, so that it doesn't match.
  titled_url_match.url_match_positions = match_positions;

  scoped_refptr<FakeAutocompleteProvider> provider =
      new FakeAutocompleteProvider(AutocompleteProvider::Type::TYPE_BOOKMARK);
  TestSchemeClassifier classifier;
  AutocompleteInput input(input_text, metrics::OmniboxEventProto::NTP,
                          classifier);
  const std::u16string fixed_up_input(input_text);

  return TitledUrlMatchToAutocompleteMatch(titled_url_match, type, relevance,
                                           provider.get(), classifier, input,
                                           fixed_up_input);
}

TEST(TitledUrlMatchUtilsTest, DoTrimHttpScheme) {
  GURL match_url("http://www.facebook.com/");
  AutocompleteMatch autocomplete_match =
      BuildTestAutocompleteMatch("face", match_url, {{11, 15}});

  ACMatchClassifications expected_contents_class = {
      {0, ACMatchClassification::URL | ACMatchClassification::MATCH},
      {4, ACMatchClassification::URL},
  };
  std::u16string expected_contents(u"facebook.com");

  EXPECT_EQ(match_url, autocomplete_match.destination_url);
  EXPECT_EQ(expected_contents, autocomplete_match.contents);
  EXPECT_TRUE(std::equal(expected_contents_class.begin(),
                         expected_contents_class.end(),
                         autocomplete_match.contents_class.begin()))
      << "EXPECTED: " << ACMatchClassificationsAsString(expected_contents_class)
      << "ACTUAL:   "
      << ACMatchClassificationsAsString(autocomplete_match.contents_class);
  EXPECT_TRUE(autocomplete_match.allowed_to_be_default_match);
}

TEST(TitledUrlMatchUtilsTest, DontTrimHttpSchemeIfInputHasScheme) {
  GURL match_url("http://www.facebook.com/");
  AutocompleteMatch autocomplete_match =
      BuildTestAutocompleteMatch("http://face", match_url, {{11, 15}});

  ACMatchClassifications expected_contents_class = {
      {0, ACMatchClassification::URL | ACMatchClassification::MATCH},
      {11, ACMatchClassification::URL},
  };
  std::u16string expected_contents(u"http://facebook.com");

  EXPECT_EQ(match_url, autocomplete_match.destination_url);
  EXPECT_EQ(expected_contents, autocomplete_match.contents);
  EXPECT_TRUE(std::equal(expected_contents_class.begin(),
                         expected_contents_class.end(),
                         autocomplete_match.contents_class.begin()))
      << "EXPECTED: " << ACMatchClassificationsAsString(expected_contents_class)
      << "ACTUAL:   "
      << ACMatchClassificationsAsString(autocomplete_match.contents_class);
  EXPECT_FALSE(autocomplete_match.allowed_to_be_default_match);
}

TEST(TitledUrlMatchUtilsTest, DoTrimHttpsScheme) {
  GURL match_url("https://www.facebook.com/");
  AutocompleteMatch autocomplete_match =
      BuildTestAutocompleteMatch("face", match_url, {{12, 16}});

  ACMatchClassifications expected_contents_class = {
      {0, ACMatchClassification::URL | ACMatchClassification::MATCH},
      {4, ACMatchClassification::URL},
  };
  std::u16string expected_contents(u"facebook.com");

  EXPECT_EQ(match_url, autocomplete_match.destination_url);
  EXPECT_EQ(expected_contents, autocomplete_match.contents);
  EXPECT_TRUE(std::equal(expected_contents_class.begin(),
                         expected_contents_class.end(),
                         autocomplete_match.contents_class.begin()))
      << "EXPECTED: " << ACMatchClassificationsAsString(expected_contents_class)
      << "ACTUAL:   "
      << ACMatchClassificationsAsString(autocomplete_match.contents_class);
  EXPECT_TRUE(autocomplete_match.allowed_to_be_default_match);
}

TEST(TitledUrlMatchUtilsTest, DontTrimHttpsSchemeIfInputHasScheme) {
  GURL match_url("https://www.facebook.com/");
  AutocompleteMatch autocomplete_match =
      BuildTestAutocompleteMatch("https://face", match_url, {{12, 16}});

  ACMatchClassifications expected_contents_class = {
      {0, ACMatchClassification::URL | ACMatchClassification::MATCH},
      {12, ACMatchClassification::URL},
  };
  std::u16string expected_contents(u"https://facebook.com");

  EXPECT_EQ(match_url, autocomplete_match.destination_url);
  EXPECT_EQ(expected_contents, autocomplete_match.contents);
  EXPECT_TRUE(std::equal(expected_contents_class.begin(),
                         expected_contents_class.end(),
                         autocomplete_match.contents_class.begin()))
      << "EXPECTED: " << ACMatchClassificationsAsString(expected_contents_class)
      << "ACTUAL:   "
      << ACMatchClassificationsAsString(autocomplete_match.contents_class);
  EXPECT_FALSE(autocomplete_match.allowed_to_be_default_match);
}

TEST(TitledUrlMatchUtilsTest, EmptyInlineAutocompletion) {
  // The search term matches the title but not the URL. Since there is no URL
  // match, the inline autocompletion string will be empty.
  std::u16string input_text(u"goo");
  std::u16string match_title(u"Email by Google");
  GURL match_url("http://www.gmail.com/");
  AutocompleteMatchType::Type type = AutocompleteMatchType::BOOKMARK_TITLE;
  int relevance = 123;

  MockTitledUrlNode node(match_title, match_url);
  bookmarks::TitledUrlMatch titled_url_match;
  titled_url_match.node = &node;
  titled_url_match.title_match_positions = {{9, 12}};
  titled_url_match.url_match_positions = {};

  scoped_refptr<FakeAutocompleteProvider> provider =
      new FakeAutocompleteProvider(AutocompleteProvider::Type::TYPE_BOOKMARK);
  TestSchemeClassifier classifier;
  AutocompleteInput input(input_text, metrics::OmniboxEventProto::NTP,
                          classifier);
  const std::u16string fixed_up_input(input_text);

  AutocompleteMatch autocomplete_match = TitledUrlMatchToAutocompleteMatch(
      titled_url_match, type, relevance, provider.get(), classifier, input,
      fixed_up_input);

  ACMatchClassifications expected_contents_class = {
      {0, ACMatchClassification::URL},
  };
  ACMatchClassifications expected_description_class = {
      {0, ACMatchClassification::NONE},
      {9, ACMatchClassification::MATCH},
      {12, ACMatchClassification::NONE},
  };

  EXPECT_EQ(provider.get(), autocomplete_match.provider);
  EXPECT_EQ(type, autocomplete_match.type);
  EXPECT_EQ(relevance, autocomplete_match.relevance);
  EXPECT_EQ(match_url, autocomplete_match.destination_url);
  EXPECT_EQ(u"gmail.com", autocomplete_match.contents);
  EXPECT_TRUE(std::equal(expected_contents_class.begin(),
                         expected_contents_class.end(),
                         autocomplete_match.contents_class.begin()))
      << "EXPECTED: " << ACMatchClassificationsAsString(expected_contents_class)
      << "ACTUAL:   "
      << ACMatchClassificationsAsString(autocomplete_match.contents_class);
  EXPECT_EQ(match_title, autocomplete_match.description);
  EXPECT_TRUE(std::equal(expected_description_class.begin(),
                         expected_description_class.end(),
                         autocomplete_match.description_class.begin()));
  EXPECT_EQ(u"www.gmail.com", autocomplete_match.fill_into_edit);
  EXPECT_FALSE(autocomplete_match.allowed_to_be_default_match);
  EXPECT_TRUE(autocomplete_match.inline_autocompletion.empty());
}

TEST(TitledUrlMatchUtilsTest, PathsInContentsAndDescription) {
  scoped_refptr<FakeAutocompleteProvider> provider =
      new FakeAutocompleteProvider(AutocompleteProvider::Type::TYPE_BOOKMARK);
  TestSchemeClassifier classifier;
  std::vector<std::u16string> ancestors = {u"parent", u"grandparent"};

  // Verifies contents and description of the AutocompleteMatch returned from
  // |bookmarks::TitledUrlMatchToAutocompleteMatch()|.
  auto test = [&](std::string title, std::string url, bool has_url_match,
                  bool has_ancestor_match, std::string expected_contents,
                  std::string expected_description) {
    SCOPED_TRACE("title [" + title + "], url [" + url + "], has_url_match [" +
                 std::string(has_url_match ? "true" : "false") +
                 "], has_ancestor_match [" +
                 std::string(has_ancestor_match ? "true" : "false") + "].");
    MockTitledUrlNode node(base::UTF8ToUTF16(title), GURL(url), ancestors);
    bookmarks::TitledUrlMatch titled_url_match;
    titled_url_match.node = &node;
    if (has_url_match)
      titled_url_match.url_match_positions.push_back(
          {8, 8});  // 8 in order to be after 'https://'
    titled_url_match.has_ancestor_match = has_ancestor_match;
    AutocompleteInput input(std::u16string(), metrics::OmniboxEventProto::NTP,
                            classifier);
    AutocompleteMatch autocomplete_match = TitledUrlMatchToAutocompleteMatch(
        titled_url_match, AutocompleteMatchType::BOOKMARK_TITLE, 1,
        provider.get(), classifier, input, std::u16string());
    EXPECT_EQ(base::UTF16ToUTF8(autocomplete_match.contents),
              expected_contents);
    EXPECT_EQ(base::UTF16ToUTF8(autocomplete_match.description),
              expected_description);
  };

  // Invokes |test()| with the 4 combinations of |has_url_match| true|false x
  // |has_ancestor_match| true|false.
  auto test_with_and_without_url_and_ancestor_matches =
      [&](std::string title, std::string url, std::string expected_contents,
          std::string expected_description) {
        for (bool has_url_match : {false, true}) {
          for (bool has_ancestor_match : {false, true}) {
            test(title, url, has_url_match, has_ancestor_match,
                 expected_contents, expected_description);
          }
        }
      };

  {
    SCOPED_TRACE("Feature disabled");
    test_with_and_without_url_and_ancestor_matches("title", "https://url.com",
                                                   "url.com", "title");
  }
  {
    SCOPED_TRACE("Feature enabled");
    base::test::ScopedFeatureList feature_list;
    feature_list.InitAndEnableFeature(omnibox::kBookmarkPaths);
    test_with_and_without_url_and_ancestor_matches("title", "https://url.com",
                                                   "url.com", "title");
  }
  {
    SCOPED_TRACE("Feature enabled, kBookmarkPathsUiReplaceTitle");
    base::test::ScopedFeatureList feature_list;
    feature_list.InitAndEnableFeatureWithParameters(
        omnibox::kBookmarkPaths,
        {{OmniboxFieldTrial::kBookmarkPathsUiReplaceTitle.name, "true"}});
    test_with_and_without_url_and_ancestor_matches(
        "title", "https://url.com", "url.com", "grandparent/parent/title");
  }
  {
    SCOPED_TRACE("Feature enabled, kBookmarkPathsUiReplaceUrl");
    base::test::ScopedFeatureList feature_list;
    feature_list.InitAndEnableFeatureWithParameters(
        omnibox::kBookmarkPaths,
        {{OmniboxFieldTrial::kBookmarkPathsUiReplaceUrl.name, "true"}});
    test_with_and_without_url_and_ancestor_matches(
        "title", "https://url.com", "grandparent/parent", "title");
  }
  {
    SCOPED_TRACE("Feature enabled, kBookmarkPathsUiAppendAfterTitle");
    base::test::ScopedFeatureList feature_list;
    feature_list.InitAndEnableFeatureWithParameters(
        omnibox::kBookmarkPaths,
        {{OmniboxFieldTrial::kBookmarkPathsUiAppendAfterTitle.name, "true"}});
    test_with_and_without_url_and_ancestor_matches(
        "title", "https://url.com", "url.com", "title : grandparent/parent");
  }
  {
    SCOPED_TRACE("Feature enabled, kBookmarkPathsUiDynamicReplaceUrl");
    base::test::ScopedFeatureList feature_list;
    feature_list.InitAndEnableFeatureWithParameters(
        omnibox::kBookmarkPaths,
        {{OmniboxFieldTrial::kBookmarkPathsUiDynamicReplaceUrl.name, "true"}});
    test("title", "https://url.com", false, false, "grandparent/parent",
         "title");
    test("title", "https://url.com", true, false, "url.com", "title");
    test("title", "https://url.com", false, true, "grandparent/parent",
         "title");
    test("title", "https://url.com", true, true, "grandparent/parent", "title");
  }
}
