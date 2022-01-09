// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/history_clusters/core/url_deduper_cluster_finalizer.h"

#include "base/test/task_environment.h"
#include "components/history_clusters/core/clustering_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace history_clusters {
namespace {

using ::testing::ElementsAre;
using ::testing::UnorderedElementsAre;

class UrlDeduperClusterFinalizerTest : public ::testing::Test {
 public:
  void SetUp() override {
    cluster_finalizer_ = std::make_unique<UrlDeduperClusterFinalizer>();
  }

  void TearDown() override { cluster_finalizer_.reset(); }

  void FinalizeCluster(history::Cluster& cluster) {
    cluster_finalizer_->FinalizeCluster(cluster);
  }

 private:
  std::unique_ptr<UrlDeduperClusterFinalizer> cluster_finalizer_;
  base::test::TaskEnvironment task_environment_;
};

TEST_F(UrlDeduperClusterFinalizerTest, DedupeExactURL) {
  // canonical_visit has the same URL as Visit1.
  history::ClusterVisit visit = testing::CreateClusterVisit(
      testing::CreateDefaultAnnotatedVisit(1, GURL("https://google.com/")));
  visit.annotated_visit.context_annotations.total_foreground_duration =
      base::Seconds(20);

  history::ClusterVisit canonical_visit = testing::CreateClusterVisit(
      testing::CreateDefaultAnnotatedVisit(2, GURL("https://google.com/")));

  history::Cluster cluster;
  cluster.visits = {visit, canonical_visit};
  FinalizeCluster(cluster);
  EXPECT_THAT(testing::ToVisitResults({cluster}),
              ElementsAre(ElementsAre(testing::VisitResult(1, 1.0),
                                      testing::VisitResult(2, 1.0, {1}))));
  const auto& actual_canonical_visit = cluster.visits.at(1);
  // Make sure total foreground duration is updated correctly even if some don't
  // have the field populated.
  EXPECT_EQ(actual_canonical_visit.annotated_visit.context_annotations
                .total_foreground_duration,
            base::Seconds(20));
}

TEST_F(UrlDeduperClusterFinalizerTest, DedupeRespectsDifferentURLs) {
  history::ClusterVisit visit = testing::CreateClusterVisit(
      testing::CreateDefaultAnnotatedVisit(1, GURL("https://google.com/")));

  history::ClusterVisit canonical_visit = testing::CreateClusterVisit(
      testing::CreateDefaultAnnotatedVisit(2, GURL("https://foo.com/")));

  history::Cluster cluster;
  cluster.visits = {visit, canonical_visit};
  FinalizeCluster(cluster);
  EXPECT_THAT(testing::ToVisitResults({cluster}),
              ElementsAre(ElementsAre(testing::VisitResult(1, 1.0),
                                      testing::VisitResult(2, 1.0))));
}

TEST_F(UrlDeduperClusterFinalizerTest, DedupeNormalizedUrl) {
  // canonical_visit has the same normalized URL as Visit1.
  history::ClusterVisit visit = testing::CreateClusterVisit(
      testing::CreateDefaultAnnotatedVisit(
          1, GURL("https://example.com/normalized?q=whatever")),
      GURL("https://example.com/normalized"));

  history::ClusterVisit canonical_visit =
      testing::CreateClusterVisit(testing::CreateDefaultAnnotatedVisit(
          2, GURL("https://example.com/normalized")));

  history::Cluster cluster;
  cluster.visits = {visit, canonical_visit};
  FinalizeCluster(cluster);
  EXPECT_THAT(testing::ToVisitResults({cluster}),
              ElementsAre(ElementsAre(testing::VisitResult(1, 1.0),
                                      testing::VisitResult(2, 1.0, {1}))));
  const auto& actual_canonical_visit = cluster.visits.at(1);
  // Make sure total foreground duration not updated if none of the visits have
  // it populated.
  EXPECT_EQ(actual_canonical_visit.annotated_visit.context_annotations
                .total_foreground_duration,
            base::Seconds(-1));
}

TEST_F(UrlDeduperClusterFinalizerTest, MergesAnnotations) {
  // canonical_visit has the same normalized URL as duplicated_visit.
  history::ClusterVisit duplicate_visit = testing::CreateClusterVisit(
      testing::CreateDefaultAnnotatedVisit(
          1, GURL("https://example.com/normalized?q=whatever")),
      GURL("https://example.com/normalized"));
  duplicate_visit.annotated_visit.content_annotations.related_searches = {
      "xyz"};
  duplicate_visit.annotated_visit.context_annotations.omnibox_url_copied = true;
  duplicate_visit.annotated_visit.context_annotations.is_existing_bookmark =
      true;
  duplicate_visit.annotated_visit.context_annotations
      .is_existing_part_of_tab_group = true;
  duplicate_visit.annotated_visit.context_annotations.is_new_bookmark = true;
  duplicate_visit.annotated_visit.context_annotations.is_placed_in_tab_group =
      true;
  duplicate_visit.annotated_visit.context_annotations.is_ntp_custom_link = true;
  duplicate_visit.annotated_visit.context_annotations
      .total_foreground_duration = base::Seconds(20);

  history::ClusterVisit canonical_visit =
      testing::CreateClusterVisit(testing::CreateDefaultAnnotatedVisit(
          2, GURL("https://example.com/normalized")));
  canonical_visit.annotated_visit.content_annotations.related_searches = {
      "abc", "xyz"};
  canonical_visit.annotated_visit.context_annotations.omnibox_url_copied =
      false;
  canonical_visit.annotated_visit.context_annotations.is_existing_bookmark =
      false;
  canonical_visit.annotated_visit.context_annotations
      .is_existing_part_of_tab_group = false;
  canonical_visit.annotated_visit.context_annotations.is_new_bookmark = false;
  canonical_visit.annotated_visit.context_annotations.is_placed_in_tab_group =
      false;
  canonical_visit.annotated_visit.context_annotations.is_ntp_custom_link =
      false;
  canonical_visit.annotated_visit.context_annotations
      .total_foreground_duration = base::Seconds(20);

  history::Cluster cluster;
  cluster.visits = {duplicate_visit, canonical_visit};
  FinalizeCluster(cluster);
  EXPECT_THAT(testing::ToVisitResults({cluster}),
              ElementsAre(ElementsAre(testing::VisitResult(1, 1.0),
                                      testing::VisitResult(2, 1.0, {1}))));
  const auto& actual_canonical_visit = cluster.visits.at(1);
  EXPECT_TRUE(actual_canonical_visit.annotated_visit.context_annotations
                  .omnibox_url_copied);
  EXPECT_TRUE(actual_canonical_visit.annotated_visit.context_annotations
                  .is_existing_bookmark);
  EXPECT_TRUE(actual_canonical_visit.annotated_visit.context_annotations
                  .is_existing_part_of_tab_group);
  EXPECT_TRUE(actual_canonical_visit.annotated_visit.context_annotations
                  .is_new_bookmark);
  EXPECT_TRUE(actual_canonical_visit.annotated_visit.context_annotations
                  .is_placed_in_tab_group);
  EXPECT_TRUE(actual_canonical_visit.annotated_visit.context_annotations
                  .is_ntp_custom_link);
  EXPECT_THAT(actual_canonical_visit.annotated_visit.content_annotations
                  .related_searches,
              UnorderedElementsAre("abc", "xyz"));
  EXPECT_EQ(actual_canonical_visit.annotated_visit.visit_row.visit_duration,
            base::Seconds(10 * 2));
  EXPECT_EQ(actual_canonical_visit.annotated_visit.context_annotations
                .total_foreground_duration,
            base::Seconds(20 * 2));
}

}  // namespace
}  // namespace history_clusters
