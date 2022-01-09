// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync_bookmarks/parent_guid_preprocessing.h"

#include <memory>

#include "base/guid.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "components/sync/engine/entity_data.h"
#include "components/sync/protocol/bookmark_specifics.pb.h"
#include "components/sync/protocol/entity_specifics.pb.h"
#include "components/sync/protocol/model_type_state.pb.h"
#include "components/sync_bookmarks/synced_bookmark_tracker.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace sync_bookmarks {

namespace {

using testing::Eq;

TEST(ParentGuidPreprocessingTest, ShouldReturnGuidForSyncIdIncludedInUpdates) {
  const std::string kId1 = "sync_id1";
  const std::string kId2 = "sync_id2";
  const std::string kGuid1 = "guid1";
  const std::string kGuid2 = "guid2";

  syncer::UpdateResponseDataList updates;
  updates.emplace_back();
  updates.back().entity.id = kId1;
  updates.back().entity.specifics.mutable_bookmark()->set_guid(kGuid1);
  updates.emplace_back();
  updates.back().entity.id = kId2;
  updates.back().entity.specifics.mutable_bookmark()->set_guid(kGuid2);

  EXPECT_THAT(GetGuidForSyncIdInUpdatesForTesting(updates, kId1), Eq(kGuid1));
  EXPECT_THAT(GetGuidForSyncIdInUpdatesForTesting(updates, kId2), Eq(kGuid2));
}

TEST(ParentGuidPreprocessingTest,
     ShouldReturnInvalidGuidForSyncIdMissingInUpdates) {
  const std::string kId1 = "sync_id1";
  const std::string kGuid1 = "guid1";

  syncer::UpdateResponseDataList updates;

  EXPECT_THAT(GetGuidForSyncIdInUpdatesForTesting(updates, "missing_id"),
              Eq(""));

  updates.emplace_back();
  updates.back().entity.id = kId1;
  updates.back().entity.specifics.mutable_bookmark()->set_guid(kGuid1);

  ASSERT_THAT(GetGuidForSyncIdInUpdatesForTesting(updates, kId1), Eq(kGuid1));
  EXPECT_THAT(GetGuidForSyncIdInUpdatesForTesting(updates, "missing_id"),
              Eq(""));
}

TEST(ParentGuidPreprocessingTest, ShouldReturnGuidForPermanentFolders) {
  const std::string kBookmarkBarId = "id1";
  const std::string kMobileBookmarksId = "id2";
  const std::string kOtherBookmarksId = "id3";

  // Permanent folders may not include their GUIDs.
  syncer::UpdateResponseDataList updates;
  updates.emplace_back();
  updates.back().entity.id = kBookmarkBarId;
  updates.back().entity.server_defined_unique_tag = "bookmark_bar";
  updates.emplace_back();
  updates.back().entity.id = kMobileBookmarksId;
  updates.back().entity.server_defined_unique_tag = "synced_bookmarks";
  updates.emplace_back();
  updates.back().entity.id = kOtherBookmarksId;
  updates.back().entity.server_defined_unique_tag = "other_bookmarks";

  EXPECT_THAT(GetGuidForSyncIdInUpdatesForTesting(updates, kBookmarkBarId),
              Eq(bookmarks::BookmarkNode::kBookmarkBarNodeGuid));
  EXPECT_THAT(GetGuidForSyncIdInUpdatesForTesting(updates, kMobileBookmarksId),
              Eq(bookmarks::BookmarkNode::kMobileBookmarksNodeGuid));
  EXPECT_THAT(GetGuidForSyncIdInUpdatesForTesting(updates, kOtherBookmarksId),
              Eq(bookmarks::BookmarkNode::kOtherBookmarksNodeGuid));
}

TEST(ParentGuidPreprocessingTest, ShouldPopulateParentGuidInInitialUpdates) {
  const std::string kBookmarkBarId = "bookmark_bar_id";
  const std::string kParentFolderId = "parent_folder_id";
  const std::string kParentFolderGuid =
      base::GUID::GenerateRandomV4().AsLowercaseString();

  // Populate updates representing:
  // bookmark_bar
  //  |- folder 1
  //    |- folder 2
  syncer::UpdateResponseDataList updates;
  updates.emplace_back();
  updates.back().entity.id = kBookmarkBarId;
  updates.back().entity.server_defined_unique_tag = "bookmark_bar";
  updates.emplace_back();
  updates.back().entity.id = kParentFolderId;
  updates.back().entity.legacy_parent_id = kBookmarkBarId;
  updates.back().entity.specifics.mutable_bookmark()->set_guid(
      kParentFolderGuid);
  updates.emplace_back();
  updates.back().entity.legacy_parent_id = kParentFolderId;
  updates.back().entity.specifics.mutable_bookmark()->set_guid("child_guid");

  PopulateParentGuidInSpecifics(/*tracker=*/nullptr, &updates);

  EXPECT_THAT(updates[0].entity.specifics.bookmark().parent_guid(), Eq(""));
  EXPECT_THAT(updates[1].entity.specifics.bookmark().parent_guid(),
              Eq(bookmarks::BookmarkNode::kBookmarkBarNodeGuid));
  EXPECT_THAT(updates[2].entity.specifics.bookmark().parent_guid(),
              Eq(kParentFolderGuid));
}

TEST(ParentGuidPreprocessingTest,
     ShouldNotOverridePreexistingParentGuidInSpecifics) {
  const std::string kBookmarkBarId = "bookmark_bar_id";
  const std::string kFolderId = "folder_id";

  const std::string kFolderGuid =
      base::GUID::GenerateRandomV4().AsLowercaseString();
  const std::string kParentGuidInSpecifics =
      base::GUID::GenerateRandomV4().AsLowercaseString();

  // Populate updates representing:
  // bookmark_bar
  //  |- folder 1
  //    |- folder 2
  syncer::UpdateResponseDataList updates;
  updates.emplace_back();
  updates.back().entity.id = kBookmarkBarId;
  updates.back().entity.server_defined_unique_tag = "bookmark_bar";
  updates.emplace_back();
  updates.back().entity.id = kFolderId;
  updates.back().entity.legacy_parent_id = kBookmarkBarId;
  updates.back().entity.specifics.mutable_bookmark()->set_guid(kFolderGuid);
  updates.back().entity.specifics.mutable_bookmark()->set_parent_guid(
      kParentGuidInSpecifics);

  // Although |parent_id| points to bookmarks bar, the |parent_guid| field
  // should prevail.
  ASSERT_THAT(GetGuidForSyncIdInUpdatesForTesting(updates, kBookmarkBarId),
              Eq(bookmarks::BookmarkNode::kBookmarkBarNodeGuid));

  PopulateParentGuidInSpecifics(/*tracker=*/nullptr, &updates);

  EXPECT_THAT(updates[1].entity.specifics.bookmark().parent_guid(),
              Eq(kParentGuidInSpecifics));
}

TEST(ParentGuidPreprocessingTest,
     ShouldPopulateParentGuidInIncrementalUpdates) {
  const std::string kSyncId = "id1";
  const std::string kBookmarkBarId = "bookmark_bar_id";

  std::unique_ptr<SyncedBookmarkTracker> tracker =
      SyncedBookmarkTracker::CreateEmpty(sync_pb::ModelTypeState());

  // Non-empty specifics are needed for SyncedBookmarkTracker::Add(), with
  // unique position populated.
  sync_pb::EntitySpecifics dummy_specifics;
  dummy_specifics.mutable_bookmark()->mutable_unique_position();

  // BookmarkModel is used here to pass DCHECKs that require that permanent
  // folders are tracked.
  std::unique_ptr<bookmarks::BookmarkModel> bookmark_model =
      bookmarks::TestBookmarkClient::CreateModel();
  tracker->Add(bookmark_model->bookmark_bar_node(), /*sync_id=*/kBookmarkBarId,
               /*server_version=*/0, /*creation_time=*/base::Time::Now(),
               /*specifics=*/dummy_specifics);
  tracker->Add(bookmark_model->other_node(), /*sync_id=*/"other_node_id",
               /*server_version=*/0, /*creation_time=*/base::Time::Now(),
               /*specifics=*/dummy_specifics);
  tracker->Add(bookmark_model->mobile_node(), /*sync_id=*/"mobile_node_id",
               /*server_version=*/0, /*creation_time=*/base::Time::Now(),
               /*specifics=*/dummy_specifics);

  // Add one regular (non-permanent) node.
  bookmarks::BookmarkNode tracked_node(/*id=*/1, base::GUID::GenerateRandomV4(),
                                       GURL());
  tracker->Add(&tracked_node, kSyncId,
               /*server_version=*/0, /*creation_time=*/base::Time::Now(),
               /*specifics=*/dummy_specifics);

  syncer::UpdateResponseDataList updates;
  updates.emplace_back();
  updates.back().entity.legacy_parent_id = kSyncId;
  updates.back().entity.specifics.mutable_bookmark()->set_guid("guid1");
  updates.emplace_back();
  updates.back().entity.legacy_parent_id = kBookmarkBarId;
  updates.back().entity.specifics.mutable_bookmark()->set_guid("guid2");
  PopulateParentGuidInSpecifics(tracker.get(), &updates);

  EXPECT_THAT(updates[0].entity.specifics.bookmark().parent_guid(),
              Eq(tracked_node.guid().AsLowercaseString()));
  EXPECT_THAT(updates[1].entity.specifics.bookmark().parent_guid(),
              Eq(bookmarks::BookmarkNode::kBookmarkBarNodeGuid));
}

TEST(ParentGuidPreprocessingTest,
     ShouldPopulateWithFakeGuidIfParentSetButUnknown) {
  // Fork of the private constant in the .cc file.
  const std::string kInvalidParentGuid = "220a410e-37b9-5bbc-8674-ea982459f940";

  const std::string kParentFolderId = "parent_folder_id";
  const std::string kParentFolderGuid =
      base::GUID::GenerateRandomV4().AsLowercaseString();

  // Populate updates representing:
  //  |- folder with unknown parent
  syncer::UpdateResponseDataList updates;
  updates.emplace_back();
  updates.back().entity.id = kParentFolderId;
  updates.back().entity.legacy_parent_id = "some_unknown_parent";
  updates.back().entity.specifics.mutable_bookmark()->set_guid(
      kParentFolderGuid);

  PopulateParentGuidInSpecifics(/*tracker=*/nullptr, &updates);

  EXPECT_THAT(updates[0].entity.specifics.bookmark().parent_guid(),
              Eq(kInvalidParentGuid));
}

}  // namespace

}  // namespace sync_bookmarks
