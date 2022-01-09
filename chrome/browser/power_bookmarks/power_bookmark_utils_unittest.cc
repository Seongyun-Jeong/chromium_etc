// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "chrome/browser/power_bookmarks/power_bookmark_utils.h"
#include "chrome/browser/power_bookmarks/proto/power_bookmark_meta.pb.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace power_bookmarks {
namespace {

const std::string kLeadImageUrl = "image.png";

const char16_t kExampleTitle[] = u"Title";
const std::string kExampleUrl = "https://example.com";

class PowerBookmarkUtilsTest : public testing::Test {
 protected:
  bookmarks::BookmarkModel* CreateTestModel() {
    std::unique_ptr<bookmarks::BookmarkModel> model(
        bookmarks::TestBookmarkClient::CreateModel());
    const bookmarks::BookmarkNode* bookmark_bar = model->bookmark_bar_node();
    model->AddURL(bookmark_bar, 0, kExampleTitle, GURL(kExampleUrl));
    return model.release();
  }
};

// Ensure the list |nodes| contains |node|.
bool ListContainsNode(const std::vector<const bookmarks::BookmarkNode*>& nodes,
                      const bookmarks::BookmarkNode* node) {
  for (auto* cur_node : nodes) {
    if (cur_node == node)
      return true;
  }
  return false;
}

TEST_F(PowerBookmarkUtilsTest, TestAddAndAccess) {
  bookmarks::BookmarkModel* model = CreateTestModel();
  const bookmarks::BookmarkNode* node =
      model->bookmark_bar_node()->children().front().get();

  std::unique_ptr<PowerBookmarkMeta> meta =
      std::make_unique<PowerBookmarkMeta>();
  meta->mutable_lead_image()->set_url(kLeadImageUrl);

  SetNodePowerBookmarkMeta(model, node, std::move(meta));

  const std::unique_ptr<PowerBookmarkMeta> fetched_meta =
      GetNodePowerBookmarkMeta(model, node);

  EXPECT_EQ(kLeadImageUrl, fetched_meta->lead_image().url());
}

TEST_F(PowerBookmarkUtilsTest, TestAddAndDelete) {
  bookmarks::BookmarkModel* model = CreateTestModel();
  const bookmarks::BookmarkNode* node =
      model->bookmark_bar_node()->children().front().get();

  std::unique_ptr<PowerBookmarkMeta> meta =
      std::make_unique<PowerBookmarkMeta>();
  meta->mutable_lead_image()->set_url(kLeadImageUrl);

  SetNodePowerBookmarkMeta(model, node, std::move(meta));

  DeleteNodePowerBookmarkMeta(model, node);

  const std::unique_ptr<PowerBookmarkMeta> fetched_meta =
      GetNodePowerBookmarkMeta(model, node);

  EXPECT_EQ(nullptr, fetched_meta.get());
}

TEST_F(PowerBookmarkUtilsTest, GetBookmarksMatchingPropertiesFilterTags) {
  std::unique_ptr<bookmarks::BookmarkModel> model(
      bookmarks::TestBookmarkClient::CreateModel());
  const bookmarks::BookmarkNode* node1 = model->AddURL(
      model->other_node(), 0, u"foo bar", GURL("http://www.google.com"));
  std::unique_ptr<PowerBookmarkMeta> meta1 =
      std::make_unique<PowerBookmarkMeta>();
  meta1->add_tags()->set_display_name("search");
  SetNodePowerBookmarkMeta(model.get(), node1, std::move(meta1));

  const bookmarks::BookmarkNode* node2 = model->AddURL(
      model->other_node(), 0, u"baz buz", GURL("http://www.cnn.com"));
  std::unique_ptr<PowerBookmarkMeta> meta2 =
      std::make_unique<PowerBookmarkMeta>();
  meta2->add_tags()->set_display_name("news");
  SetNodePowerBookmarkMeta(model.get(), node2, std::move(meta2));

  std::vector<const bookmarks::BookmarkNode*> nodes;
  PowerBookmarkQueryFields query;
  query.word_phrase_query = std::make_unique<std::u16string>();

  // Test that the correct bookmark is returned for the "search" tag.
  query.tags.push_back(u"search");
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(1U, nodes.size());
  EXPECT_TRUE(node1 == nodes[0]);
  nodes.clear();
  query.tags.clear();

  // Test that the correct bookmark is returned for the "news" tag.
  query.tags.push_back(u"news");
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(1U, nodes.size());
  EXPECT_TRUE(node2 == nodes[0]);
  nodes.clear();
  query.tags.clear();

  // Test that there are no results when valid but mutually exclusive tags are
  // specified.
  query.tags.push_back(u"news");
  query.tags.push_back(u"search");
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(0U, nodes.size());
  nodes.clear();
  query.tags.clear();

  // Test that no bookmarks are returned for unknown tag.
  query.tags.push_back(u"foo");
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  EXPECT_TRUE(nodes.empty());
  nodes.clear();
  query.tags.clear();

  // Test that no bookmarks are returned for a totally empty query.
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_TRUE(nodes.empty());
  nodes.clear();
  query.tags.clear();

  // Test that a query plus tag returns the correct bookmark.
  query.tags.push_back(u"news");
  *query.word_phrase_query = u"baz";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(1U, nodes.size());
  EXPECT_TRUE(node2 == nodes[0]);
  nodes.clear();
  query.tags.clear();

  // Test that a mismatched query and tag returns nothing.
  query.tags.push_back(u"search");
  *query.word_phrase_query = u"baz";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  EXPECT_TRUE(nodes.empty());
  nodes.clear();
  query.tags.clear();
}

TEST_F(PowerBookmarkUtilsTest, GetBookmarksMatchingPropertiesSearchTags) {
  std::unique_ptr<bookmarks::BookmarkModel> model(
      bookmarks::TestBookmarkClient::CreateModel());
  const bookmarks::BookmarkNode* node1 = model->AddURL(
      model->other_node(), 0, u"foo bar", GURL("http://www.google.com"));
  std::unique_ptr<PowerBookmarkMeta> meta1 =
      std::make_unique<PowerBookmarkMeta>();
  meta1->add_tags()->set_display_name("search");
  SetNodePowerBookmarkMeta(model.get(), node1, std::move(meta1));

  const bookmarks::BookmarkNode* node2 = model->AddURL(
      model->other_node(), 0, u"baz buz", GURL("http://www.cnn.com"));
  std::unique_ptr<PowerBookmarkMeta> meta2 =
      std::make_unique<PowerBookmarkMeta>();
  meta2->add_tags()->set_display_name("news");
  SetNodePowerBookmarkMeta(model.get(), node2, std::move(meta2));

  std::vector<const bookmarks::BookmarkNode*> nodes;
  PowerBookmarkQueryFields query;
  query.word_phrase_query = std::make_unique<std::u16string>();

  // Test that a query for a substring in a tag and having a specified tag
  // finds the correct node.
  query.tags.push_back(u"news");
  *query.word_phrase_query = u"ews";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(1U, nodes.size());
  EXPECT_TRUE(node2 == nodes[0]);
  nodes.clear();
  query.tags.clear();

  // Test that a query for a substring in a tag finds the correct node.
  *query.word_phrase_query = u"ews";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(1U, nodes.size());
  EXPECT_TRUE(node2 == nodes[0]);
  nodes.clear();
  query.tags.clear();

  // Test that a query for the start of a tag finds the correct node.
  *query.word_phrase_query = u"sea";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(1U, nodes.size());
  EXPECT_TRUE(node1 == nodes[0]);
  nodes.clear();
  query.tags.clear();
}

TEST_F(PowerBookmarkUtilsTest,
       GetBookmarksMatchingPropertiesSearchMultipleTags) {
  std::unique_ptr<bookmarks::BookmarkModel> model(
      bookmarks::TestBookmarkClient::CreateModel());
  const bookmarks::BookmarkNode* node1 = model->AddURL(
      model->other_node(), 0, u"foo bar", GURL("http://www.google.com"));
  std::unique_ptr<PowerBookmarkMeta> meta1 =
      std::make_unique<PowerBookmarkMeta>();
  meta1->add_tags()->set_display_name("search");
  meta1->add_tags()->set_display_name("news");
  SetNodePowerBookmarkMeta(model.get(), node1, std::move(meta1));

  const bookmarks::BookmarkNode* node2 = model->AddURL(
      model->other_node(), 0, u"baz buz", GURL("http://www.cnn.com"));
  std::unique_ptr<PowerBookmarkMeta> meta2 =
      std::make_unique<PowerBookmarkMeta>();
  meta2->add_tags()->set_display_name("news");
  SetNodePowerBookmarkMeta(model.get(), node2, std::move(meta2));

  std::vector<const bookmarks::BookmarkNode*> nodes;
  PowerBookmarkQueryFields query;
  query.word_phrase_query = std::make_unique<std::u16string>();

  // Test that a query that contains multiple tags finds results that have all
  // of those tags.
  *query.word_phrase_query = u"news search";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(1U, nodes.size());
  EXPECT_TRUE(node1 == nodes[0]);
  nodes.clear();
  query.tags.clear();

  // Make sure searching for one tag finds both bookmarks.
  *query.word_phrase_query = u"news";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(2U, nodes.size());
  nodes.clear();
  query.tags.clear();
}

TEST_F(PowerBookmarkUtilsTest, GetBookmarksMatchingPropertiesStringSearch) {
  std::unique_ptr<bookmarks::BookmarkModel> model(
      bookmarks::TestBookmarkClient::CreateModel());
  const bookmarks::BookmarkNode* node1 = model->AddURL(
      model->other_node(), 0, u"foo bar", GURL("http://www.google.com"));

  const bookmarks::BookmarkNode* node2 = model->AddURL(
      model->other_node(), 0, u"baz buz", GURL("http://www.cnn.com"));

  std::vector<const bookmarks::BookmarkNode*> nodes;
  PowerBookmarkQueryFields query;
  query.word_phrase_query = std::make_unique<std::u16string>();

  *query.word_phrase_query = u"bar";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(1U, nodes.size());
  EXPECT_TRUE(node1 == nodes[0]);
  nodes.clear();

  *query.word_phrase_query = u"baz";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(1U, nodes.size());
  EXPECT_TRUE(node2 == nodes[0]);
  nodes.clear();

  // A string search for "ba" should find both nodes.
  *query.word_phrase_query = u"ba";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(2U, nodes.size());
  nodes.clear();

  // Ensure a search checks the URL.
  *query.word_phrase_query = u"goog";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(1U, nodes.size());
  EXPECT_TRUE(node1 == nodes[0]);
  nodes.clear();

  // Ensure a string that doesn't exist in the bookmarks returns nothing.
  *query.word_phrase_query = u"zzz";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(0U, nodes.size());
  nodes.clear();

  // Check that two strings from different bookmarks returns nothing.
  *query.word_phrase_query = u"foo buz";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(0U, nodes.size());
  nodes.clear();

  // Ensure an empty string returns no bookmarks.
  *query.word_phrase_query = u"";
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(0U, nodes.size());
  nodes.clear();
}

TEST_F(PowerBookmarkUtilsTest, GetBookmarksMatchingPropertiesFolderSearch) {
  std::unique_ptr<bookmarks::BookmarkModel> model(
      bookmarks::TestBookmarkClient::CreateModel());
  model->AddURL(model->other_node(), 0, u"foo example",
                GURL("http://www.google.com"));

  model->AddURL(model->other_node(), 0, u"baz example",
                GURL("http://www.cnn.com"));

  const bookmarks::BookmarkNode* folder =
      model->AddFolder(model->other_node(), 0, u"test folder");

  const bookmarks::BookmarkNode* node =
      model->AddURL(folder, 0, u"buz example", GURL("http://www.example.com"));

  std::vector<const bookmarks::BookmarkNode*> nodes;
  PowerBookmarkQueryFields query;
  query.word_phrase_query = std::make_unique<std::u16string>();

  *query.word_phrase_query = u"example";
  query.folder = nullptr;
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(3U, nodes.size());
  nodes.clear();

  *query.word_phrase_query = u"example";
  query.folder = folder;
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(1U, nodes.size());
  EXPECT_TRUE(node == nodes[0]);
  nodes.clear();
}

TEST_F(PowerBookmarkUtilsTest, GetBookmarksMatchingPropertiesTypeSearch) {
  std::unique_ptr<bookmarks::BookmarkModel> model(
      bookmarks::TestBookmarkClient::CreateModel());
  const bookmarks::BookmarkNode* node1 = model->AddURL(
      model->other_node(), 0, u"foo bar", GURL("http://www.google.com"));
  std::unique_ptr<PowerBookmarkMeta> meta1 =
      std::make_unique<PowerBookmarkMeta>();
  meta1->set_type(PowerBookmarkType::SHOPPING);
  SetNodePowerBookmarkMeta(model.get(), node1, std::move(meta1));

  const bookmarks::BookmarkNode* node2 = model->AddURL(
      model->other_node(), 0, u"baz buz", GURL("http://www.cnn.com"));
  std::unique_ptr<PowerBookmarkMeta> meta2 =
      std::make_unique<PowerBookmarkMeta>();
  meta2->set_type(PowerBookmarkType::SHOPPING);
  SetNodePowerBookmarkMeta(model.get(), node2, std::move(meta2));

  const bookmarks::BookmarkNode* node3 = model->AddURL(
      model->other_node(), 0, u"chromium", GURL("http://www.chromium.org"));
  std::unique_ptr<PowerBookmarkMeta> meta3 =
      std::make_unique<PowerBookmarkMeta>();
  meta3->set_type(PowerBookmarkType::UNSPECIFIED);
  SetNodePowerBookmarkMeta(model.get(), node3, std::move(meta3));

  const bookmarks::BookmarkNode* normal_node = model->AddURL(
      model->other_node(), 0, u"example page", GURL("http://www.example.com"));

  std::vector<const bookmarks::BookmarkNode*> nodes;
  PowerBookmarkQueryFields query;

  // Test that a query with no type returns all results.
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(4U, nodes.size());
  nodes.clear();

  // Test that a query for the SHOPPING type returns the correct results.
  query.type = PowerBookmarkType::SHOPPING;
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(2U, nodes.size());
  EXPECT_FALSE(ListContainsNode(nodes, normal_node));
  EXPECT_FALSE(ListContainsNode(nodes, node3));
  nodes.clear();

  // Test that a query for the UNSPECIFIED type returns the correct results.
  query.type = PowerBookmarkType::UNSPECIFIED;
  GetBookmarksMatchingProperties(model.get(), query, 100, &nodes);
  ASSERT_EQ(3U, nodes.size());
  EXPECT_FALSE(ListContainsNode(nodes, normal_node));
  nodes.clear();
}

TEST_F(PowerBookmarkUtilsTest, EncodeAndDecodeForPersistence) {
  PowerBookmarkMeta meta;
  meta.set_type(PowerBookmarkType::SHOPPING);
  meta.mutable_shopping_specifics()->set_title("Example Title");

  std::string encoded_data;
  EncodeMetaForStorage(meta, &encoded_data);

  PowerBookmarkMeta out_meta;
  EXPECT_TRUE(DecodeMetaFromStorage(encoded_data, &out_meta));

  ASSERT_EQ(meta.type(), out_meta.type());
  ASSERT_EQ(meta.shopping_specifics().title(),
            out_meta.shopping_specifics().title());
}

}  // namespace
}  // namespace power_bookmarks
