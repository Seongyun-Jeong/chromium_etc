// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_FEED_CORE_V2_CONFIG_H_
#define COMPONENTS_FEED_CORE_V2_CONFIG_H_

#include "base/containers/flat_set.h"
#include "base/time/time.h"
#include "components/feed/core/proto/v2/wire/capability.pb.h"

namespace feed {
class StreamType;

// The Feed configuration. Default values appear below. Always use
// |GetFeedConfig()| to get the current configuration.
struct Config {
  // Maximum number of requests per day for FeedQuery, NextPage, and
  // ActionUpload.
  int max_feed_query_requests_per_day = 20;
  int max_next_page_requests_per_day = 20;
  int max_action_upload_requests_per_day = 20;
  int max_list_recommended_web_feeds_requests_per_day = 20;
  int max_list_web_feeds_requests_per_day = 20;
  // We'll always attempt to refresh content older than this.
  base::TimeDelta stale_content_threshold = base::Hours(4);
  // Content older than this threshold will not be shown to the user.
  base::TimeDelta content_expiration_threshold = base::Hours(48);
  // How long the window is for background refresh tasks. If the task cannot be
  // scheduled in the window, the background refresh is aborted.
  base::TimeDelta background_refresh_window_length = base::Hours(24);
  // The time between background refresh attempts. Ignored if a server-defined
  // fetch schedule has been assigned.
  base::TimeDelta default_background_refresh_interval = base::Hours(24);
  // Maximum number of times to attempt to upload a pending action before
  // deleting it.
  int max_action_upload_attempts = 3;
  // Maximum age for a pending action. Actions older than this are deleted.
  base::TimeDelta max_action_age = base::Hours(24);
  // Maximum payload size for one action upload batch.
  size_t max_action_upload_bytes = 20000;
  // If no surfaces are attached, the stream model is unloaded after this
  // timeout.
  base::TimeDelta model_unload_timeout = base::Seconds(1);
  // How far ahead in number of items from last visible item to final item
  // before attempting to load more content.
  int load_more_trigger_lookahead = 5;
  // How far does the user have to scroll the feed before the feed begins
  // to consider loading more data. The scrolling threshold is a proxy
  // measure for deciding whether the user has engaged with the feed.
  int load_more_trigger_scroll_distance_dp = 100;
  // Whether to attempt uploading actions when Chrome is hidden.
  bool upload_actions_on_enter_background = true;
  // Whether to send (pseudonymous) logs for signed-out sessions.
  bool send_signed_out_session_logs = false;
  // The max age of a signed-out session token.
  base::TimeDelta session_id_max_age = base::Days(30);
  // Maximum number of images prefetched per refresh.
  int max_prefetch_image_requests_per_refresh = 50;
  // The minimum interval from the last time the notice is viewed in order for
  // it to be considered viewed again.
  base::TimeDelta minimum_notice_view_interval = base::Minutes(5);

  // Configuration for Web Feeds.

  // How long before Web Feed content is considered stale.
  base::TimeDelta web_feed_stale_content_threshold = base::Hours(1);
  // TimeDelta after startup to fetch recommended and subscribed Web Feeds if
  // they are stale. If zero, no fetching is done.
  base::TimeDelta fetch_web_feed_info_delay = base::Seconds(40);
  // How long before cached recommended feed data on the device is considered
  // stale and refetched.
  base::TimeDelta recommended_feeds_staleness_threshold = base::Days(28);
  // How long before cached subscribed feed data on the device is considered
  // stale and refetched.
  base::TimeDelta subscribed_feeds_staleness_threshold = base::Days(7);
  // Number of days of history to query when determining whether to show the
  // follow accelerator.
  int webfeed_accelerator_recent_visit_history_days = 14;

  // Configuration for `PersistentKeyValueStore`.

  // Maximum total database size before items are evicted.
  int64_t persistent_kv_store_maximum_size_before_eviction = 1000000;
  // Eviction task is performed after this many bytes are written.
  int persistent_kv_store_cleanup_interval_in_written_bytes = 1000000;

  // Until we get the new list contents API working, keep using FeedQuery.
  // TODO(crbug/1152592): remove this when new endpoint is tested enough.
  bool use_feed_query_requests_for_web_feeds = false;

  // Set of optional capabilities included in requests. See
  // CreateFeedQueryRequest() for required capabilities.
  base::flat_set<feedwire::Capability> experimental_capabilities = {
      feedwire::Capability::DISMISS_COMMAND,
      feedwire::Capability::INFINITE_FEED,
      feedwire::Capability::MATERIAL_NEXT_BASELINE,
      feedwire::Capability::PREFETCH_METADATA,
      feedwire::Capability::REQUEST_SCHEDULE,
      feedwire::Capability::UI_THEME_V2,
      feedwire::Capability::UNDO_FOR_DISMISS_COMMAND,
      feedwire::Capability::CONTENT_LIFETIME,
  };

  Config();
  Config(const Config& other);
  ~Config();

  base::TimeDelta GetStalenessThreshold(const StreamType& stream_type) const;
};

// Gets the current configuration.
const Config& GetFeedConfig();

// Sets whether the legacy feed endpoint should be used for Web Feed content
// fetches.
void SetUseFeedQueryRequestsForWebFeeds(const bool use_legacy);

void SetFeedConfigForTesting(const Config& config);
void OverrideConfigWithFinchForTesting();

}  // namespace feed

#endif  // COMPONENTS_FEED_CORE_V2_CONFIG_H_
