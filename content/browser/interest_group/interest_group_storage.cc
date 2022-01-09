// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/interest_group/interest_group_storage.h"

#include <memory>
#include <vector>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_string_value_serializer.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/notreached.h"
#include "base/sequence_checker.h"
#include "base/strings/string_piece.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/time/time.h"
#include "content/services/auction_worklet/public/mojom/bidder_worklet.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "net/base/escape.h"
#include "sql/database.h"
#include "sql/error_delegate_util.h"
#include "sql/meta_table.h"
#include "sql/recovery.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/interest_group/interest_group.h"
#include "third_party/sqlite/sqlite3.h"
#include "url/origin.h"

namespace content {

namespace {

using auction_worklet::mojom::BiddingBrowserSignalsPtr;
using auction_worklet::mojom::BiddingInterestGroupPtr;
using auction_worklet::mojom::PreviousWinPtr;

const base::FilePath::CharType kDatabasePath[] =
    FILE_PATH_LITERAL("InterestGroups");

// Version number of the database.
//
// Version 1 - 2021/03 - crrev.com/c/2757425
// Version 2 - 2021/08 - crrev.com/c/3097715
// Version 3 - 2021/09 - crrev.com/c/3165576
// Version 4 - 2021/10 - crrev.com/c/3172863
// Version 5 - 2021/10 - crrev.com/c/3067804
// Version 6 - 2021/12 - crrev.com/c/3330516
//
// Version 1 adds a table for interest groups.
// Version 2 adds a column for rate limiting interest group updates.
// Version 3 adds a field for ad components.
// Version 4 adds joining origin and url.
// Version 5 adds k-anonymity tables and fields.
// Version 6 adds WebAssembly helper url.
const int kCurrentVersionNumber = 6;

// Earliest version of the code which can use a |kCurrentVersionNumber|
// database without failing.
const int kCompatibleVersionNumber = 6;

// Latest version of the database that cannot be upgraded to
// |kCurrentVersionNumber| without razing the database.
const int kDeprecatedVersionNumber = 5;

enum class KAnonType {
  kOwnerAndName = 1,
  kUpdateURL = 2,
  kAdURL = 3,  // represents ads and ad components
};

GURL KAnonKeyFor(url::Origin interest_group_owner,
                 std::string interest_group_name) {
  return interest_group_owner.GetURL().Resolve(
      net::EscapePath(interest_group_name));
}

std::string Serialize(const base::Value& value) {
  std::string json_output;
  JSONStringValueSerializer serializer(&json_output);
  serializer.Serialize(value);
  return json_output;
}
std::unique_ptr<base::Value> DeserializeValue(
    const std::string& serialized_value) {
  if (serialized_value.empty())
    return {};
  JSONStringValueDeserializer deserializer(serialized_value);
  return deserializer.Deserialize(/*error_code=*/nullptr,
                                  /*error_message=*/nullptr);
}

std::string Serialize(const url::Origin& origin) {
  return origin.Serialize();
}
url::Origin DeserializeOrigin(const std::string& serialized_origin) {
  return url::Origin::Create(GURL(serialized_origin));
}

std::string Serialize(const absl::optional<GURL>& url) {
  if (!url)
    return std::string();
  return url->spec();
}
absl::optional<GURL> DeserializeURL(const std::string& serialized_url) {
  GURL result(serialized_url);
  if (result.is_empty())
    return absl::nullopt;
  return result;
}

base::Value ToValue(const blink::InterestGroup::Ad& ad) {
  base::Value dict(base::Value::Type::DICTIONARY);
  dict.SetStringKey("url", ad.render_url.spec());
  if (ad.metadata)
    dict.SetStringKey("metadata", ad.metadata.value());
  return dict;
}
blink::InterestGroup::Ad FromInterestGroupAdValue(const base::Value* value) {
  blink::InterestGroup::Ad result;
  const std::string* maybe_url = value->FindStringKey("url");
  if (maybe_url)
    result.render_url = GURL(*maybe_url);
  const std::string* maybe_metadata = value->FindStringKey("metadata");
  if (maybe_metadata)
    result.metadata = *maybe_metadata;
  return result;
}

std::string Serialize(
    const absl::optional<std::vector<blink::InterestGroup::Ad>>& ads) {
  if (!ads)
    return std::string();
  base::Value list(base::Value::Type::LIST);
  for (const auto& ad : ads.value()) {
    list.Append(ToValue(ad));
  }
  return Serialize(list);
}
absl::optional<std::vector<blink::InterestGroup::Ad>>
DeserializeInterestGroupAdVector(const std::string& serialized_ads) {
  std::unique_ptr<base::Value> ads_value = DeserializeValue(serialized_ads);
  if (!ads_value || !ads_value->is_list())
    return absl::nullopt;
  std::vector<blink::InterestGroup::Ad> result;
  for (const auto& ad_value : ads_value->GetList()) {
    result.emplace_back(FromInterestGroupAdValue(&ad_value));
  }
  return result;
}

std::string Serialize(const absl::optional<std::vector<std::string>>& strings) {
  if (!strings)
    return std::string();
  base::Value list(base::Value::Type::LIST);
  for (const auto& s : strings.value())
    list.Append(s);
  return Serialize(list);
}
absl::optional<std::vector<std::string>> DeserializeStringVector(
    const std::string& serialized_vector) {
  std::unique_ptr<base::Value> list = DeserializeValue(serialized_vector);
  if (!list || !list->is_list())
    return absl::nullopt;
  std::vector<std::string> result;
  for (const auto& value : list->GetList())
    result.push_back(value.GetString());
  return result;
}

// Initializes the tables, returning true on success.
// The tables cannot exist when calling this function.
bool CreateV6Schema(sql::Database& db) {
  DCHECK(!db.DoesTableExist("interest_groups"));
  static const char kInterestGroupTableSql[] =
      // clang-format off
      "CREATE TABLE interest_groups("
        "expiration INTEGER NOT NULL,"
        "last_updated INTEGER NOT NULL,"
        "next_update_after INTEGER NOT NULL,"
        "owner TEXT NOT NULL,"
        "joining_origin TEXT NOT NULL,"
        "name TEXT NOT NULL,"
        "joining_url TEXT NOT NULL,"
        "bidding_url TEXT NOT NULL,"
        "bidding_wasm_helper_url TEXT NOT NULL,"
        "update_url TEXT NOT NULL,"
        "trusted_bidding_signals_url TEXT NOT NULL,"
        "trusted_bidding_signals_keys TEXT NOT NULL,"
        "user_bidding_signals TEXT,"
        "ads TEXT NOT NULL,"
        "ad_components TEXT NOT NULL,"
      "PRIMARY KEY(owner,name))";
  // clang-format on
  if (!db.Execute(kInterestGroupTableSql))
    return false;

  // Index on group expiration. Owner and Name are only here to speed up
  // queries that don't need the full group.
  DCHECK(!db.DoesIndexExist("interest_group_expiration"));
  static const char kInterestGroupExpirationIndexSql[] =
      // clang-format off
      "CREATE INDEX interest_group_expiration"
      " ON interest_groups(expiration DESC, owner, name)";
  // clang-format on
  if (!db.Execute(kInterestGroupExpirationIndexSql))
    return false;

  // Index on group expiration by owner.
  DCHECK(!db.DoesIndexExist("interest_group_owner"));
  static const char kInterestGroupOwnerIndexSql[] =
      // clang-format off
      "CREATE INDEX interest_group_owner"
      " ON interest_groups(owner, expiration DESC)";
  // clang-format on
  if (!db.Execute(kInterestGroupOwnerIndexSql))
    return false;

  // Index on group expiration by joining origin. Owner and Name are only here
  // to speed up queries that don't need the full group.
  DCHECK(!db.DoesIndexExist("interest_group_joining_origin"));
  static const char kInterestGroupJoiningOriginIndexSql[] =
      // clang-format off
      "CREATE INDEX interest_group_joining_origin"
      " ON interest_groups(joining_origin, expiration DESC, owner, name)";
  // clang-format on
  if (!db.Execute(kInterestGroupJoiningOriginIndexSql))
    return false;

  static const char kInterestGroupKAnonTableSql[] =
      // clang-format off
      "CREATE TABLE kanon("
        "last_referenced_time INTEGER NOT NULL,"
        "type INTEGER NOT NULL,"
        "key TEXT NOT NULL,"
        "k_anon_count INTEGER NOT NULL,"
        "last_k_anon_updated_time INTEGER NOT NULL,"
        "last_reported_to_anon_server_time INTEGER NOT NULL,"
        "PRIMARY KEY(type,key))";
  // clang-format on
  if (!db.Execute(kInterestGroupKAnonTableSql))
    return false;

  // Index on kanon last_referenced_time.
  DCHECK(!db.DoesIndexExist("kanon_last_referenced_time"));
  static const char kInterestGroupKAnonLastRefIndexSql[] =
      // clang-format off
      "CREATE INDEX kanon_last_referenced_time"
      " ON kanon(last_referenced_time DESC)";
  // clang-format on
  if (!db.Execute(kInterestGroupKAnonLastRefIndexSql))
    return false;

  // We can't use the interest group and join time as primary keys since
  // different pages may try to join the same interest group at the same time.
  DCHECK(!db.DoesTableExist("join_history"));
  static const char kJoinHistoryTableSql[] =
      // clang-format off
      "CREATE TABLE join_history("
        "owner TEXT NOT NULL,"
        "name TEXT NOT NULL,"
        "join_time INTEGER NOT NULL,"
      "FOREIGN KEY(owner,name) REFERENCES interest_groups)";
  // clang-format on
  if (!db.Execute(kJoinHistoryTableSql))
    return false;

  DCHECK(!db.DoesIndexExist("join_history_index"));
  static const char kJoinHistoryIndexSql[] =
      // clang-format off
      "CREATE INDEX join_history_index "
      "ON join_history(owner,name,join_time)";
  // clang-format on
  if (!db.Execute(kJoinHistoryIndexSql))
    return false;

  // We can't use the interest group and bid time as primary keys since
  // auctions on separate pages may occur at the same time.
  DCHECK(!db.DoesTableExist("bid_history"));
  static const char kBidHistoryTableSql[] =
      // clang-format off
      "CREATE TABLE bid_history("
        "owner TEXT NOT NULL,"
        "name TEXT NOT NULL,"
        "bid_time INTEGER NOT NULL,"
      "FOREIGN KEY(owner,name) REFERENCES interest_groups)";
  // clang-format on
  if (!db.Execute(kBidHistoryTableSql))
    return false;

  DCHECK(!db.DoesIndexExist("bid_history_index"));
  static const char kBidHistoryIndexSql[] =
      // clang-format off
      "CREATE INDEX bid_history_index "
      "ON bid_history(owner,name,bid_time)";
  // clang-format on
  if (!db.Execute(kBidHistoryIndexSql))
    return false;

  // We can't use the interest group and win time as primary keys since
  // auctions on separate pages may occur at the same time.
  DCHECK(!db.DoesTableExist("win_history"));
  static const char kWinHistoryTableSQL[] =
      // clang-format off
      "CREATE TABLE win_history("
        "owner TEXT NOT NULL,"
        "name TEXT NOT NULL,"
        "win_time INTEGER NOT NULL,"
        "ad TEXT NOT NULL,"
      "FOREIGN KEY(owner,name) REFERENCES interest_groups)";
  // clang-format on
  if (!db.Execute(kWinHistoryTableSQL))
    return false;

  DCHECK(!db.DoesIndexExist("win_history_index"));
  static const char kWinHistoryIndexSQL[] =
      // clang-format off
      "CREATE INDEX win_history_index "
      "ON win_history(owner,name,win_time DESC)";
  // clang-format on
  if (!db.Execute(kWinHistoryIndexSQL))
    return false;

  return true;
}

bool DoCreateOrMarkKAnonReferenced(sql::Database& db,
                                   KAnonType type,
                                   const GURL& key,
                                   const base::Time& now) {
  base::Time distant_past = base::Time::Min();
  base::Time cutoff = now - InterestGroupStorage::kHistoryLength;

  // This flow basically emulates SQLite's UPSERT feature which is disabled in
  // Chrome. Although there are two statements executed, we don't need to
  // enclose them in a transaction since only one will actually modify the
  // database.

  // clang-format off
  sql::Statement maybe_insert_kanon(
      db.GetCachedStatement(SQL_FROM_HERE,
          "INSERT OR IGNORE INTO kanon("
              "last_referenced_time,"
              "type,"
              "key,"
              "k_anon_count,"
              "last_k_anon_updated_time,"
              "last_reported_to_anon_server_time) "
            "VALUES(?,?,?,0,?,?)"
      ));

  // clang-format on
  if (!maybe_insert_kanon.is_valid())
    return false;

  maybe_insert_kanon.Reset(true);
  maybe_insert_kanon.BindTime(0, now);
  maybe_insert_kanon.BindInt(1, static_cast<int>(type));
  maybe_insert_kanon.BindString(2, Serialize(key));
  maybe_insert_kanon.BindTime(3, distant_past);
  maybe_insert_kanon.BindTime(4, distant_past);

  if (!maybe_insert_kanon.Run())
    return false;

  // If the insert changed the database return early.
  if (db.GetLastChangeCount() > 0)
    return true;

  // Update last referenced time, clearing previous k-anon data if their values
  // have expired.
  // clang-format off
  sql::Statement update_kanon(
      db.GetCachedStatement(SQL_FROM_HERE,
          "UPDATE kanon "
          "SET last_referenced_time=?1,"
              "k_anon_count=IIF(last_referenced_time>?2,k_anon_count,0),"
              "last_k_anon_updated_time="
                "IIF(last_referenced_time>?2,last_k_anon_updated_time,?3),"
              "last_reported_to_anon_server_time=IIF(last_referenced_time>?2,"
                "last_reported_to_anon_server_time,?3) "
          "WHERE type=?4 AND key=?5"));
  // clang-format on
  if (!update_kanon.is_valid())
    return false;

  update_kanon.Reset(true);
  update_kanon.BindTime(0, now);
  update_kanon.BindTime(1, cutoff);
  update_kanon.BindTime(2, distant_past);
  update_kanon.BindInt(3, static_cast<int>(type));
  update_kanon.BindString(4, Serialize(key));

  return update_kanon.Run();
}

bool DoCreateOrMarkInterestGroupNameReferenced(sql::Database& db,
                                               url::Origin owner,
                                               const std::string& name,
                                               const base::Time& now) {
  return DoCreateOrMarkKAnonReferenced(db, KAnonType::kOwnerAndName,
                                       KAnonKeyFor(owner, name), now);
}

bool DoCreateOrMarkInterestGroupUpdateURLReferenced(sql::Database& db,
                                                    const GURL& update_url,
                                                    const base::Time& now) {
  return DoCreateOrMarkKAnonReferenced(db, KAnonType::kUpdateURL, update_url,
                                       now);
}

bool DoCreateOrMarkAdReferenced(sql::Database& db,
                                const blink::InterestGroup::Ad& ad,
                                const base::Time& now) {
  return DoCreateOrMarkKAnonReferenced(db, KAnonType::kAdURL, ad.render_url,
                                       now);
}

bool DoCreateOrMarkInterestGroupAndAdsReferenced(
    sql::Database& db,
    const blink::InterestGroup& data,
    const base::Time& now) {
  if (data.ads) {
    // Mark these ads as being 'in use'.
    for (const blink::InterestGroup::Ad& ad : data.ads.value()) {
      if (!DoCreateOrMarkAdReferenced(db, ad, now))
        return false;
    }
  }

  if (data.ad_components) {
    // Mark these ads as being 'in use'.
    for (const blink::InterestGroup::Ad& ad : data.ad_components.value()) {
      if (!DoCreateOrMarkAdReferenced(db, ad, now))
        return false;
    }
  }

  if (!DoCreateOrMarkInterestGroupNameReferenced(db, data.owner, data.name,
                                                 now)) {
    return false;
  }

  if (data.update_url) {
    if (!DoCreateOrMarkInterestGroupUpdateURLReferenced(
            db, data.update_url.value(), now)) {
      return false;
    }
  }
  return true;
}

bool DoJoinInterestGroup(sql::Database& db,
                         const blink::InterestGroup& data,
                         const GURL& joining_url,
                         base::Time last_updated,
                         base::Time next_update_after) {
  sql::Transaction transaction(&db);
  if (!transaction.Begin())
    return false;

  url::Origin joining_origin = url::Origin::Create(joining_url);

  // clang-format off
  sql::Statement join_group(
      db.GetCachedStatement(SQL_FROM_HERE,
          "INSERT OR REPLACE INTO interest_groups("
            "expiration,"
            "last_updated,"
            "next_update_after,"
            "owner,"
            "joining_origin,"
            "name,"
            "joining_url,"
            "bidding_url,"
            "bidding_wasm_helper_url,"
            "update_url,"
            "trusted_bidding_signals_url,"
            "trusted_bidding_signals_keys,"
            "user_bidding_signals,"  // opaque data
            "ads,"
            "ad_components) "
          "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));

  // clang-format on
  if (!join_group.is_valid())
    return false;

  join_group.Reset(true);
  join_group.BindTime(0, data.expiry);
  join_group.BindTime(1, last_updated);
  join_group.BindTime(2, next_update_after);
  join_group.BindString(3, Serialize(data.owner));
  join_group.BindString(4, Serialize(joining_origin));
  join_group.BindString(5, data.name);
  join_group.BindString(6, Serialize(joining_url));
  join_group.BindString(7, Serialize(data.bidding_url));
  join_group.BindString(8, Serialize(data.bidding_wasm_helper_url));
  join_group.BindString(9, Serialize(data.update_url));
  join_group.BindString(10, Serialize(data.trusted_bidding_signals_url));
  join_group.BindString(11, Serialize(data.trusted_bidding_signals_keys));
  if (data.user_bidding_signals) {
    join_group.BindString(12, data.user_bidding_signals.value());
  } else {
    join_group.BindNull(12);
  }
  join_group.BindString(13, Serialize(data.ads));
  join_group.BindString(14, Serialize(data.ad_components));

  if (!join_group.Run())
    return false;

  // Record the join. It should be unique since a site should only join once
  // per a page load. If it is not unique we should collapse the entries to
  // minimize the damage done by a misbehaving site.
  sql::Statement join_hist(
      db.GetCachedStatement(SQL_FROM_HERE,
                            "INSERT INTO join_history(owner,name,join_time) "
                            "VALUES(?,?,?)"));
  if (!join_hist.is_valid())
    return false;

  join_hist.Reset(true);
  join_hist.BindString(0, Serialize(data.owner));
  join_hist.BindString(1, data.name);
  join_hist.BindTime(2, last_updated);

  if (!join_hist.Run())
    return false;

  if (!DoCreateOrMarkInterestGroupAndAdsReferenced(db, data, last_updated))
    return false;

  return transaction.Commit();
}

bool DoLoadInterestGroup(sql::Database& db,
                         const url::Origin& owner,
                         const std::string& name,
                         blink::InterestGroup& group) {
  // clang-format off
  sql::Statement load(
      db.GetCachedStatement(SQL_FROM_HERE,
        "SELECT expiration,"
          "bidding_url,"
          "bidding_wasm_helper_url,"
          "update_url,"
          "trusted_bidding_signals_url,"
          "trusted_bidding_signals_keys,"
          "user_bidding_signals,"  // opaque data
          "ads,"
          "ad_components "
        "FROM interest_groups "
        "WHERE owner = ? AND name = ? "));
  // clang-format on

  if (!load.is_valid())
    return false;

  load.Reset(true);
  load.BindString(0, Serialize(owner));
  load.BindString(1, name);

  if (!load.Step() || !load.Succeeded())
    return false;

  group.expiry = load.ColumnTime(0);
  group.owner = owner;
  group.name = name;
  group.bidding_url = DeserializeURL(load.ColumnString(1));
  group.bidding_wasm_helper_url = DeserializeURL(load.ColumnString(2));
  group.update_url = DeserializeURL(load.ColumnString(3));
  group.trusted_bidding_signals_url = DeserializeURL(load.ColumnString(4));
  group.trusted_bidding_signals_keys =
      DeserializeStringVector(load.ColumnString(5));
  if (load.GetColumnType(6) != sql::ColumnType::kNull)
    group.user_bidding_signals = load.ColumnString(6);
  group.ads = DeserializeInterestGroupAdVector(load.ColumnString(7));
  group.ad_components = DeserializeInterestGroupAdVector(load.ColumnString(8));

  return true;
}

bool DoStoreInterestGroupUpdate(sql::Database& db,
                                const blink::InterestGroup& group,
                                base::Time last_updated) {
  // clang-format off
  sql::Statement store_group(
      db.GetCachedStatement(SQL_FROM_HERE,
          "UPDATE interest_groups "
          "SET last_updated=?,"
            "bidding_url=?,"
            "bidding_wasm_helper_url=?,"
            "update_url=?,"
            "trusted_bidding_signals_url=?,"
            "trusted_bidding_signals_keys=?,"
            "ads=?,"
            "ad_components=? "
          "WHERE owner=? AND name=?"));

  // clang-format on
  if (!store_group.is_valid())
    return false;

  store_group.Reset(true);
  store_group.BindTime(0, last_updated);
  store_group.BindString(1, Serialize(group.bidding_url));
  store_group.BindString(2, Serialize(group.bidding_wasm_helper_url));
  store_group.BindString(3, Serialize(group.update_url));
  store_group.BindString(4, Serialize(group.trusted_bidding_signals_url));
  store_group.BindString(5, Serialize(group.trusted_bidding_signals_keys));
  store_group.BindString(6, Serialize(group.ads));
  store_group.BindString(7, Serialize(group.ad_components));
  store_group.BindString(8, Serialize(group.owner));
  store_group.BindString(9, group.name);

  return store_group.Run();
}

bool DoUpdateInterestGroup(sql::Database& db,
                           const blink::InterestGroup& update,
                           base::Time now) {
  sql::Transaction transaction(&db);
  if (!transaction.Begin())
    return false;

  // Unlike Join() operations, for Update() operations, values that aren't
  // specified in the JSON returned by servers (Serialize()'d below as empty
  // strings) aren't modified in the database -- in this sense, new data is
  // merged with old data.
  //
  // Since we need to verify this results in a valid interest group, we have to
  // first read the interest group from the DB, apply the changes and then
  // verify the interest group is valid before writing it to the database.

  blink::InterestGroup stored_group;
  if (!DoLoadInterestGroup(db, update.owner, update.name, stored_group))
    return false;

  // (Optimization) Don't do anything for expired interest groups.
  if (stored_group.expiry < now)
    return false;

  if (update.bidding_url)
    stored_group.bidding_url = update.bidding_url;
  if (update.bidding_wasm_helper_url)
    stored_group.bidding_wasm_helper_url = update.bidding_wasm_helper_url;
  if (update.trusted_bidding_signals_url)
    stored_group.trusted_bidding_signals_url =
        update.trusted_bidding_signals_url;
  if (update.trusted_bidding_signals_keys)
    stored_group.trusted_bidding_signals_keys =
        update.trusted_bidding_signals_keys;
  if (update.ads)
    stored_group.ads = update.ads;
  if (update.ad_components)
    stored_group.ad_components = update.ad_components;

  if (!stored_group.IsValid()) {
    // TODO(behamilton): Report errors to devtools.
    return false;
  }

  if (!DoStoreInterestGroupUpdate(db, stored_group, now))
    return false;

  // Updates do not change the expiration time so we do not need to refresh the
  // referenced field for fields that didn't change.
  if (!DoCreateOrMarkInterestGroupAndAdsReferenced(db, update, now))
    return false;
  return transaction.Commit();
}

bool DoReportUpdateFailed(sql::Database& db,
                          const url::Origin& owner,
                          const std::string& name,
                          bool net_disconnected,
                          base::Time now) {
  sql::Statement update_group(db.GetCachedStatement(SQL_FROM_HERE, R"(
UPDATE interest_groups SET
  next_update_after=?
WHERE owner=? AND name=?)"));

  if (!update_group.is_valid())
    return false;

  update_group.Reset(true);
  if (net_disconnected) {
    update_group.BindTime(0, now);
  } else {
    update_group.BindTime(
        0, now + InterestGroupStorage::kUpdateFailedBackoffPeriod);
  }
  update_group.BindString(1, Serialize(owner));
  update_group.BindString(2, name);

  return update_group.Run();
}

bool RemoveJoinHistory(sql::Database& db,
                       const url::Origin& owner,
                       const std::string& name) {
  sql::Statement remove_join_history(
      db.GetCachedStatement(SQL_FROM_HERE,
                            "DELETE FROM join_history "
                            "WHERE owner=? AND name=?"));
  if (!remove_join_history.is_valid())
    return false;

  remove_join_history.Reset(true);
  remove_join_history.BindString(0, Serialize(owner));
  remove_join_history.BindString(1, name);
  return remove_join_history.Run();
}

bool RemoveBidHistory(sql::Database& db,
                      const url::Origin& owner,
                      const std::string& name) {
  sql::Statement remove_bid_history(
      db.GetCachedStatement(SQL_FROM_HERE,
                            "DELETE FROM bid_history "
                            "WHERE owner=? AND name=?"));
  if (!remove_bid_history.is_valid())
    return false;

  remove_bid_history.Reset(true);
  remove_bid_history.BindString(0, Serialize(owner));
  remove_bid_history.BindString(1, name);
  return remove_bid_history.Run();
}

bool RemoveWinHistory(sql::Database& db,
                      const url::Origin& owner,
                      const std::string& name) {
  sql::Statement remove_win_history(
      db.GetCachedStatement(SQL_FROM_HERE,
                            "DELETE FROM win_history "
                            "WHERE owner=? AND name=?"));
  if (!remove_win_history.is_valid())
    return false;

  remove_win_history.Reset(true);
  remove_win_history.BindString(0, Serialize(owner));
  remove_win_history.BindString(1, name);
  return remove_win_history.Run();
}

bool DoRemoveInterestGroup(sql::Database& db,
                           const url::Origin& owner,
                           const std::string& name) {
  sql::Transaction transaction(&db);
  if (!transaction.Begin())
    return false;

  // These tables have foreign keys that reference the interest group table.
  if (!RemoveJoinHistory(db, owner, name))
    return false;
  if (!RemoveBidHistory(db, owner, name))
    return false;
  if (!RemoveWinHistory(db, owner, name))
    return false;

  sql::Statement remove_group(
      db.GetCachedStatement(SQL_FROM_HERE,
                            "DELETE FROM interest_groups "
                            "WHERE owner=? AND name=?"));
  if (!remove_group.is_valid())
    return false;

  remove_group.Reset(true);
  remove_group.BindString(0, Serialize(owner));
  remove_group.BindString(1, name);
  return remove_group.Run() && transaction.Commit();
}

bool DoRecordInterestGroupBid(sql::Database& db,
                              const url::Origin& owner,
                              const std::string& name,
                              base::Time bid_time) {
  // Record the bid. It should be unique since auctions should be serialized.
  // If it is not unique we should just keep the first one.
  // clang-format off
  sql::Statement bid_hist(
      db.GetCachedStatement(SQL_FROM_HERE,
      "INSERT INTO bid_history(owner,name,bid_time) "
      "VALUES(?,?,?)"));
  // clang-format on
  if (!bid_hist.is_valid())
    return false;

  bid_hist.Reset(true);
  bid_hist.BindString(0, Serialize(owner));
  bid_hist.BindString(1, name);
  bid_hist.BindTime(2, bid_time);
  return bid_hist.Run();
}

bool DoRecordInterestGroupWin(sql::Database& db,
                              const url::Origin& owner,
                              const std::string& name,
                              const std::string& ad_json,
                              base::Time win_time) {
  // Record the win. It should be unique since auctions should be serialized.
  // If it is not unique we should just keep the first one.
  // clang-format off
  sql::Statement win_hist(
      db.GetCachedStatement(SQL_FROM_HERE,
      "INSERT INTO win_history(owner,name,win_time,ad) "
      "VALUES(?,?,?,?)"));
  // clang-format on
  if (!win_hist.is_valid())
    return false;

  win_hist.Reset(true);
  win_hist.BindString(0, Serialize(owner));
  win_hist.BindString(1, name);
  win_hist.BindTime(2, win_time);
  win_hist.BindString(3, ad_json);
  return win_hist.Run();
}

bool DoUpdateKAnonymity(sql::Database& db,
                        KAnonType type,
                        const StorageInterestGroup::KAnonymityData& data,
                        const absl::optional<base::Time>& update_sent_time) {
  // clang-format off
  sql::Statement update(
      db.GetCachedStatement(SQL_FROM_HERE,
      "UPDATE kanon "
      "SET k_anon_count=?, last_k_anon_updated_time=?,"
          "last_reported_to_anon_server_time="
            "IFNULL(?,last_reported_to_anon_server_time) "
      "WHERE type=? AND key=?"));
  // clang-format on
  if (!update.is_valid())
    return false;

  update.Reset(true);
  update.BindInt(0, data.k);
  update.BindTime(1, data.last_updated);
  if (update_sent_time)
    update.BindTime(2, update_sent_time.value());
  else
    update.BindNull(2);
  update.BindInt(3, static_cast<int>(type));
  update.BindString(4, Serialize(data.key));
  return update.Run();
}

bool DoUpdateInterestGroupNameKAnonymity(
    sql::Database& db,
    const StorageInterestGroup::KAnonymityData& data,
    const absl::optional<base::Time>& update_sent_time) {
  return DoUpdateKAnonymity(db, KAnonType::kOwnerAndName, data,
                            update_sent_time);
}

bool DoUpdateInterestGroupUpdateURLKAnonymity(
    sql::Database& db,
    const StorageInterestGroup::KAnonymityData& data,
    const absl::optional<base::Time>& update_sent_time) {
  return DoUpdateKAnonymity(db, KAnonType::kUpdateURL, data, update_sent_time);
}

bool DoUpdateAdKAnonymity(sql::Database& db,
                          const StorageInterestGroup::KAnonymityData& data,
                          const absl::optional<base::Time>& update_sent_time) {
  return DoUpdateKAnonymity(db, KAnonType::kAdURL, data, update_sent_time);
}

absl::optional<std::vector<url::Origin>> DoGetAllInterestGroupOwners(
    sql::Database& db,
    base::Time expiring_after) {
  std::vector<url::Origin> result;
  sql::Statement load(db.GetCachedStatement(SQL_FROM_HERE,
                                            "SELECT DISTINCT owner "
                                            "FROM interest_groups "
                                            "WHERE expiration>=? "
                                            "ORDER BY expiration DESC"));
  if (!load.is_valid()) {
    DLOG(ERROR) << "LoadAllInterestGroups SQL statement did not compile: "
                << db.GetErrorMessage();
    return absl::nullopt;
  }
  load.Reset(true);
  load.BindTime(0, expiring_after);
  while (load.Step()) {
    result.push_back(DeserializeOrigin(load.ColumnString(0)));
  }
  if (!load.Succeeded())
    return absl::nullopt;
  return result;
}

absl::optional<std::vector<url::Origin>> DoGetAllInterestGroupJoiningOrigins(
    sql::Database& db,
    base::Time expiring_after) {
  std::vector<url::Origin> result;
  sql::Statement load(db.GetCachedStatement(SQL_FROM_HERE,
                                            "SELECT DISTINCT joining_origin "
                                            "FROM interest_groups "
                                            "WHERE expiration>=?"));
  if (!load.is_valid()) {
    DLOG(ERROR)
        << "LoadAllInterestGroupJoiningOrigins SQL statement did not compile: "
        << db.GetErrorMessage();
    return absl::nullopt;
  }
  load.Reset(true);
  load.BindTime(0, expiring_after);
  while (load.Step()) {
    result.push_back(DeserializeOrigin(load.ColumnString(0)));
  }
  if (!load.Succeeded())
    return absl::nullopt;
  return result;
}

bool DoGetInterestGroupKAnonymity(
    sql::Database& db,
    KAnonType type,
    const GURL& key,
    absl::optional<StorageInterestGroup::KAnonymityData>& output) {
  // clang-format off
  sql::Statement interest_group_kanon(
    db.GetCachedStatement(SQL_FROM_HERE,
      "SELECT k_anon_count, last_k_anon_updated_time "
      "FROM kanon "
      "WHERE type=? AND key=?"
    ));
  // clang-format on
  if (!interest_group_kanon.is_valid()) {
    DLOG(ERROR)
        << "GetInterestGroupsForOwner interest_group_kanon SQL statement did "
           "not compile: "
        << db.GetErrorMessage();
    return false;
  }

  interest_group_kanon.Reset(true);
  interest_group_kanon.BindInt(0, static_cast<int>(type));
  interest_group_kanon.BindString(1, Serialize(key));

  if (!interest_group_kanon.Step()) {
    return false;
  }

  output = {key, interest_group_kanon.ColumnInt(0),
            interest_group_kanon.ColumnTime(1)};
  return interest_group_kanon.Succeeded();
}

bool DoGetInterestGroupNameKAnonymity(
    sql::Database& db,
    const url::Origin& owner,
    const std::string& name,
    absl::optional<StorageInterestGroup::KAnonymityData>& output) {
  return DoGetInterestGroupKAnonymity(db, KAnonType::kOwnerAndName,
                                      KAnonKeyFor(owner, name), output);
}

bool DoGetInterestGroupUpdateURLKAnonymity(
    sql::Database& db,
    const GURL& update_url,
    absl::optional<StorageInterestGroup::KAnonymityData>& output) {
  return DoGetInterestGroupKAnonymity(db, KAnonType::kUpdateURL, update_url,
                                      output);
}

bool DoGetAdsKAnonymity(
    sql::Database& db,
    const GURL& ad_url,
    absl::optional<StorageInterestGroup::KAnonymityData>& output) {
  return DoGetInterestGroupKAnonymity(db, KAnonType::kAdURL, ad_url, output);
}

bool GetPreviousWins(sql::Database& db,
                     const url::Origin& owner,
                     const std::string& name,
                     base::Time win_time_after,
                     BiddingInterestGroupPtr& output) {
  // clang-format off
  sql::Statement prev_wins(
      db.GetCachedStatement(SQL_FROM_HERE,
                            "SELECT win_time, ad "
                            "FROM win_history "
                            "WHERE owner = ? AND name = ? AND win_time >= ? "
                            "ORDER BY win_time DESC"));
  // clang-format on
  if (!prev_wins.is_valid()) {
    DLOG(ERROR) << "GetInterestGroupsForOwner win_history SQL statement did "
                   "not compile: "
                << db.GetErrorMessage();
    return false;
  }
  prev_wins.Reset(true);
  prev_wins.BindString(0, Serialize(owner));
  prev_wins.BindString(1, name);
  prev_wins.BindTime(2, win_time_after);
  while (prev_wins.Step()) {
    PreviousWinPtr prev_win = auction_worklet::mojom::PreviousWin::New();
    prev_win->time = prev_wins.ColumnTime(0);
    prev_win->ad_json = prev_wins.ColumnString(1);
    output->signals->prev_wins.push_back(std::move(prev_win));
  }
  return prev_wins.Succeeded();
}

bool GetJoinCount(sql::Database& db,
                  const url::Origin& owner,
                  const std::string& name,
                  base::Time joined_after,
                  BiddingInterestGroupPtr& output) {
  // clang-format off
  sql::Statement join_count(
      db.GetCachedStatement(SQL_FROM_HERE,
    "SELECT COUNT(1) "
    "FROM join_history "
    "WHERE owner = ? AND name = ? AND join_time >=?"));
  // clang-format on
  if (!join_count.is_valid()) {
    DLOG(ERROR) << "GetJoinCount SQL statement did not compile: "
                << db.GetErrorMessage();
    return false;
  }
  join_count.Reset(true);
  join_count.BindString(0, Serialize(owner));
  join_count.BindString(1, name);
  join_count.BindTime(2, joined_after);
  while (join_count.Step()) {
    output->signals->join_count = join_count.ColumnInt64(0);
  }
  return join_count.Succeeded();
}

bool GetBidCount(sql::Database& db,
                 const url::Origin& owner,
                 const std::string& name,
                 base::Time now,
                 BiddingInterestGroupPtr& output) {
  // clang-format off
  sql::Statement bid_count(
      db.GetCachedStatement(SQL_FROM_HERE,
    "SELECT COUNT(1) "
    "FROM bid_history "
    "WHERE owner = ? AND name = ? AND bid_time >= ?"));
  // clang-format on
  if (!bid_count.is_valid()) {
    DLOG(ERROR) << "GetBidCount SQL statement did not compile: "
                << db.GetErrorMessage();
    return false;
  }
  bid_count.Reset(true);
  bid_count.BindString(0, Serialize(owner));
  bid_count.BindString(1, name);
  bid_count.BindTime(2, now - InterestGroupStorage::kHistoryLength);
  while (bid_count.Step()) {
    output->signals->bid_count = bid_count.ColumnInt64(0);
  }
  return bid_count.Succeeded();
}

absl::optional<std::vector<std::string>> DoGetInterestGroupNamesForOwner(
    sql::Database& db,
    const url::Origin& owner,
    base::Time now,
    base::Time next_update_after) {
  // clang-format off
  sql::Statement get_names(
    db.GetCachedStatement(SQL_FROM_HERE,
    "SELECT name "
    "FROM interest_groups "
    "WHERE owner=? AND expiration>=? AND ?>=next_update_after "
    "ORDER BY expiration DESC"));
  // clang-format on

  if (!get_names.is_valid())
    return absl::nullopt;

  get_names.Reset(true);
  get_names.BindString(0, Serialize(owner));
  get_names.BindTime(1, now);
  get_names.BindTime(2, next_update_after);

  std::vector<std::string> result;
  while (get_names.Step()) {
    result.push_back(get_names.ColumnString(0));
  }
  if (!get_names.Succeeded())
    return absl::nullopt;

  return result;
}

absl::optional<StorageInterestGroup> DoGetStoredInterestGroup(
    sql::Database& db,
    const url::Origin owner,
    std::string name,
    base::Time now) {
  StorageInterestGroup db_interest_group;
  db_interest_group.bidding_group =
      auction_worklet::mojom::BiddingInterestGroup::New();
  if (!DoLoadInterestGroup(db, owner, name,
                           db_interest_group.bidding_group->group))
    return absl::nullopt;

  if (!DoGetInterestGroupNameKAnonymity(
          db, owner, db_interest_group.bidding_group->group.name,
          db_interest_group.name_kanon)) {
    return absl::nullopt;
  }
  if (db_interest_group.bidding_group->group.update_url &&
      !DoGetInterestGroupUpdateURLKAnonymity(
          db, db_interest_group.bidding_group->group.update_url.value(),
          db_interest_group.update_url_kanon)) {
    return absl::nullopt;
  }
  if (db_interest_group.bidding_group->group.ads) {
    for (auto& ad : db_interest_group.bidding_group->group.ads.value()) {
      absl::optional<StorageInterestGroup::KAnonymityData> ad_kanon;
      if (!DoGetAdsKAnonymity(db, ad.render_url, ad_kanon)) {
        return absl::nullopt;
      }
      if (!ad_kanon)
        continue;
      db_interest_group.ads_kanon.push_back(std::move(ad_kanon).value());
    }
  }
  if (db_interest_group.bidding_group->group.ad_components) {
    for (auto& ad :
         db_interest_group.bidding_group->group.ad_components.value()) {
      absl::optional<StorageInterestGroup::KAnonymityData> ad_kanon;
      if (!DoGetAdsKAnonymity(db, ad.render_url, ad_kanon)) {
        return absl::nullopt;
      }
      if (!ad_kanon)
        continue;
      db_interest_group.ads_kanon.push_back(std::move(ad_kanon).value());
    }
  }

  db_interest_group.bidding_group->signals =
      auction_worklet::mojom::BiddingBrowserSignals::New();
  if (!GetJoinCount(db, owner, name, now - InterestGroupStorage::kHistoryLength,
                    db_interest_group.bidding_group)) {
    return absl::nullopt;
  }
  if (!GetBidCount(db, owner, name, now - InterestGroupStorage::kHistoryLength,
                   db_interest_group.bidding_group)) {
    return absl::nullopt;
  }
  if (!GetPreviousWins(db, owner, name,
                       now - InterestGroupStorage::kHistoryLength,
                       db_interest_group.bidding_group)) {
    return absl::nullopt;
  }
  return db_interest_group;
}

absl::optional<std::vector<StorageInterestGroup>> DoGetInterestGroupsForOwner(
    sql::Database& db,
    const url::Origin& owner,
    base::Time now,
    bool claim_groups_for_update = false) {
  sql::Transaction transaction(&db);

  if (!transaction.Begin())
    return absl::nullopt;

  base::Time next_update_after =
      (claim_groups_for_update ? now : base::Time::Max());
  absl::optional<std::vector<std::string>> group_names =
      DoGetInterestGroupNamesForOwner(db, owner, now, next_update_after);

  if (!group_names)
    return absl::nullopt;

  if (claim_groups_for_update) {
    // clang-format off
    sql::Statement update_group(db.GetCachedStatement(SQL_FROM_HERE,
      "UPDATE interest_groups SET next_update_after=? "
      "WHERE owner = ? AND expiration >=? AND ?>= next_update_after"));
    // clang-format on
    if (!update_group.is_valid())
      return absl::nullopt;

    update_group.Reset(true);
    update_group.BindTime(
        0, now + InterestGroupStorage::kUpdateSucceededBackoffPeriod);
    update_group.BindString(1, Serialize(owner));
    update_group.BindTime(2, now);
    update_group.BindTime(3, now);

    if (!update_group.Run())
      return absl::nullopt;
  }

  std::vector<StorageInterestGroup> result;
  for (const std::string& name : *group_names) {
    absl::optional<StorageInterestGroup> db_interest_group =
        DoGetStoredInterestGroup(db, owner, name, now);
    if (!db_interest_group)
      return absl::nullopt;
    result.push_back(std::move(db_interest_group).value());
  }

  if (!transaction.Commit())
    return absl::nullopt;

  return result;
}

absl::optional<std::vector<std::pair<url::Origin, std::string>>>
DoGetInterestGroupNamesForJoiningOrigin(sql::Database& db,
                                        const url::Origin& joining_origin,
                                        base::Time now) {
  std::vector<std::pair<url::Origin, std::string>> result;

  // clang-format off
  sql::Statement load(
      db.GetCachedStatement(SQL_FROM_HERE,
        "SELECT owner,name "
        "FROM interest_groups "
        "WHERE joining_origin = ? AND expiration >=?"));
  // clang-format on

  if (!load.is_valid()) {
    DLOG(ERROR) << "GetInterestGroupNamesForJoiningOrigin SQL statement did "
                   "not compile: "
                << db.GetErrorMessage();
    return absl::nullopt;
  }

  load.Reset(true);
  load.BindString(0, Serialize(joining_origin));
  load.BindTime(1, now);

  while (load.Step()) {
    result.emplace_back(DeserializeOrigin(load.ColumnString(0)),
                        load.ColumnString(1));
  }
  if (!load.Succeeded())
    return absl::nullopt;
  return result;
}

bool DoDeleteInterestGroupData(
    sql::Database& db,
    const base::RepeatingCallback<bool(const url::Origin&)>& origin_matcher) {
  const base::Time distant_past = base::Time::Min();
  const base::Time distant_future = base::Time::Max();
  sql::Transaction transaction(&db);

  if (!transaction.Begin())
    return false;

  std::vector<url::Origin> affected_origins;
  absl::optional<std::vector<url::Origin>> maybe_all_origins =
      DoGetAllInterestGroupOwners(db, distant_past);

  if (!maybe_all_origins)
    return false;
  for (const url::Origin& origin : maybe_all_origins.value()) {
    if (origin_matcher.is_null() || origin_matcher.Run(origin)) {
      affected_origins.push_back(origin);
    }
  }

  for (const auto& affected_origin : affected_origins) {
    absl::optional<std::vector<std::string>> maybe_group_names =
        DoGetInterestGroupNamesForOwner(db, affected_origin, distant_past,
                                        distant_future);
    if (!maybe_group_names)
      return false;
    for (const auto& group_name : maybe_group_names.value()) {
      if (!DoRemoveInterestGroup(db, affected_origin, group_name))
        return false;
    }
  }

  affected_origins.clear();
  maybe_all_origins = DoGetAllInterestGroupJoiningOrigins(db, distant_past);
  if (!maybe_all_origins)
    return false;
  for (const url::Origin& origin : maybe_all_origins.value()) {
    if (origin_matcher.is_null() || origin_matcher.Run(origin)) {
      affected_origins.push_back(origin);
    }
  }
  for (const auto& affected_origin : affected_origins) {
    absl::optional<std::vector<std::pair<url::Origin, std::string>>>
        maybe_group_names = DoGetInterestGroupNamesForJoiningOrigin(
            db, affected_origin, distant_past);
    if (!maybe_group_names)
      return false;
    for (const auto& interest_group_key : maybe_group_names.value()) {
      if (!DoRemoveInterestGroup(db, interest_group_key.first,
                                 interest_group_key.second)) {
        return false;
      }
    }
  }

  return transaction.Commit();
}

bool DeleteOldJoins(sql::Database& db, base::Time cutoff) {
  sql::Statement del_join_history(db.GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM join_history WHERE join_time <= ?"));
  if (!del_join_history.is_valid()) {
    DLOG(ERROR) << "DeleteOldJoins SQL statement did not compile.";
    return false;
  }
  del_join_history.Reset(true);
  del_join_history.BindTime(0, cutoff);
  if (!del_join_history.Run()) {
    DLOG(ERROR) << "Could not delete old join_history.";
    return false;
  }
  return true;
}

bool DeleteOldBids(sql::Database& db, base::Time cutoff) {
  sql::Statement del_bid_history(db.GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM bid_history WHERE bid_time <= ?"));
  if (!del_bid_history.is_valid()) {
    DLOG(ERROR) << "DeleteOldBids SQL statement did not compile.";
    return false;
  }
  del_bid_history.Reset(true);
  del_bid_history.BindTime(0, cutoff);
  if (!del_bid_history.Run()) {
    DLOG(ERROR) << "Could not delete old bid_history.";
    return false;
  }
  return true;
}

bool DeleteOldWins(sql::Database& db, base::Time cutoff) {
  sql::Statement del_win_history(db.GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM win_history WHERE win_time <= ?"));
  if (!del_win_history.is_valid()) {
    DLOG(ERROR) << "DeleteOldWins SQL statement did not compile.";
    return false;
  }
  del_win_history.Reset(true);
  del_win_history.BindTime(0, cutoff);
  if (!del_win_history.Run()) {
    DLOG(ERROR) << "Could not delete old win_history.";
    return false;
  }
  return true;
}

bool ClearExcessInterestGroups(sql::Database& db,
                               size_t max_owners,
                               size_t max_owner_interest_groups) {
  const base::Time distant_past = base::Time::Min();
  const absl::optional<std::vector<url::Origin>> maybe_all_origins =
      DoGetAllInterestGroupOwners(db, distant_past);
  if (!maybe_all_origins)
    return false;
  for (size_t owner_idx = 0; owner_idx < maybe_all_origins.value().size();
       owner_idx++) {
    const url::Origin& affected_origin = maybe_all_origins.value()[owner_idx];
    const absl::optional<std::vector<StorageInterestGroup>>
        maybe_interest_groups =
            DoGetInterestGroupsForOwner(db, affected_origin, distant_past);
    if (!maybe_interest_groups)
      return false;
    size_t first_idx = max_owner_interest_groups;
    if (owner_idx >= max_owners)
      first_idx = 0;
    for (size_t group_idx = first_idx;
         group_idx < maybe_interest_groups.value().size(); group_idx++) {
      if (!DoRemoveInterestGroup(db, affected_origin,
                                 maybe_interest_groups.value()[group_idx]
                                     .bidding_group->group.name))
        return false;
    }
  }
  return true;
}

bool ClearExpiredInterestGroups(sql::Database& db,
                                base::Time expiration_before) {
  sql::Transaction transaction(&db);
  if (!transaction.Begin())
    return false;

  sql::Statement expired_interest_group(
      db.GetCachedStatement(SQL_FROM_HERE,
                            "SELECT owner, name "
                            "FROM interest_groups "
                            "WHERE expiration <= ?"));
  if (!expired_interest_group.is_valid()) {
    DLOG(ERROR) << "ClearExpiredInterestGroups SQL statement did not compile.";
    return false;
  }

  expired_interest_group.Reset(true);
  expired_interest_group.BindTime(0, expiration_before);
  std::vector<std::pair<url::Origin, std::string>> expired_groups;
  while (expired_interest_group.Step()) {
    expired_groups.emplace_back(
        DeserializeOrigin(expired_interest_group.ColumnString(0)),
        expired_interest_group.ColumnString(1));
  }
  if (!expired_interest_group.Succeeded()) {
    DLOG(ERROR) << "ClearExpiredInterestGroups could not get expired groups.";
    // Keep going so we can clear any groups that we did get.
  }
  for (const auto& interest_group : expired_groups) {
    if (!DoRemoveInterestGroup(db, interest_group.first, interest_group.second))
      return false;
  }
  return transaction.Commit();
}

bool ClearExpiredKAnon(sql::Database& db, base::Time cutoff) {
  sql::Statement expired_kanon(
      db.GetCachedStatement(SQL_FROM_HERE,
                            "DELETE FROM kanon "
                            "WHERE last_referenced_time <= ?"));
  if (!expired_kanon.is_valid()) {
    DLOG(ERROR) << "ClearExpiredKAnon SQL statement did not compile.";
    return false;
  }

  expired_kanon.Reset(true);
  expired_kanon.BindTime(0, cutoff);
  return expired_kanon.Run();
}

bool DoPerformDatabaseMaintenance(sql::Database& db,
                                  base::Time now,
                                  size_t max_owners,
                                  size_t max_owner_interest_groups) {
  SCOPED_UMA_HISTOGRAM_TIMER_MICROS("Storage.InterestGroup.DBMaintenanceTime");
  sql::Transaction transaction(&db);
  if (!transaction.Begin())
    return false;
  if (!ClearExcessInterestGroups(db, max_owners, max_owner_interest_groups))
    return false;
  if (!ClearExpiredInterestGroups(db, now))
    return false;
  if (!DeleteOldJoins(db, now - InterestGroupStorage::kHistoryLength))
    return false;
  if (!DeleteOldBids(db, now - InterestGroupStorage::kHistoryLength))
    return false;
  if (!DeleteOldWins(db, now - InterestGroupStorage::kHistoryLength))
    return false;
  if (!ClearExpiredKAnon(db, now - InterestGroupStorage::kHistoryLength))
    return false;
  return transaction.Commit();
}

base::FilePath DBPath(const base::FilePath& base) {
  if (base.empty())
    return base;
  return base.Append(kDatabasePath);
}

}  // namespace

constexpr base::TimeDelta InterestGroupStorage::kHistoryLength;
constexpr base::TimeDelta InterestGroupStorage::kMaintenanceInterval;
constexpr base::TimeDelta InterestGroupStorage::kIdlePeriod;
constexpr base::TimeDelta InterestGroupStorage::kUpdateSucceededBackoffPeriod;
constexpr base::TimeDelta InterestGroupStorage::kUpdateFailedBackoffPeriod;

InterestGroupStorage::InterestGroupStorage(const base::FilePath& path)
    : path_to_database_(DBPath(path)),
      max_owners_(blink::features::kInterestGroupStorageMaxOwners.Get()),
      max_owner_interest_groups_(
          blink::features::kInterestGroupStorageMaxGroupsPerOwner.Get()),
      max_ops_before_maintenance_(
          blink::features::kInterestGroupStorageMaxOpsBeforeMaintenance.Get()),
      db_(std::make_unique<sql::Database>(sql::DatabaseOptions{})),
      db_maintenance_timer_(FROM_HERE,
                            kIdlePeriod,
                            this,
                            &InterestGroupStorage::PerformDBMaintenance) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

InterestGroupStorage::~InterestGroupStorage() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool InterestGroupStorage::EnsureDBInitialized() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::Time now = base::Time::Now();
  if (now > last_maintenance_time_ + kMaintenanceInterval) {
    // Schedule maintenance for next idle period. If maintenance already
    // scheduled this delays it further (we're not idle).
    db_maintenance_timer_.Reset();
  }
  // Force maintenance even if we're busy if the database may have changed too
  // much.
  if (ops_since_last_maintenance_++ > max_ops_before_maintenance_)
    PerformDBMaintenance();

  last_access_time_ = now;
  if (db_ && db_->is_open())
    return true;
  return InitializeDB();
}

bool InterestGroupStorage::InitializeDB() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  db_ = std::make_unique<sql::Database>(sql::DatabaseOptions{});
  db_->set_error_callback(base::BindRepeating(
      &InterestGroupStorage::DatabaseErrorCallback, base::Unretained(this)));
  db_->set_histogram_tag("InterestGroups");

  if (path_to_database_.empty()) {
    if (!db_->OpenInMemory()) {
      DLOG(ERROR) << "Failed to create in-memory interest group database: "
                  << db_->GetErrorMessage();
      return false;
    }
  } else {
    const base::FilePath dir = path_to_database_.DirName();

    if (!base::DirectoryExists(dir) && !base::CreateDirectory(dir)) {
      DLOG(ERROR) << "Failed to create directory for interest group database";
      return false;
    }
    if (db_->Open(path_to_database_) == false) {
      DLOG(ERROR) << "Failed to open interest group database: "
                  << db_->GetErrorMessage();
      return false;
    }
  }

  if (!InitializeSchema()) {
    db_->Close();
    return false;
  }

  DCHECK(sql::MetaTable::DoesTableExist(db_.get()));
  DCHECK(db_->DoesTableExist("interest_groups"));
  DCHECK(db_->DoesTableExist("join_history"));
  DCHECK(db_->DoesTableExist("bid_history"));
  DCHECK(db_->DoesTableExist("win_history"));
  DCHECK(db_->DoesTableExist("kanon"));
  return true;
}

bool InterestGroupStorage::InitializeSchema() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!db_)
    return false;

  sql::MetaTable::RazeIfIncompatible(
      db_.get(), /*lowest_supported_version=*/kDeprecatedVersionNumber + 1,
      kCurrentVersionNumber);

  sql::MetaTable meta_table;
  bool has_metatable = meta_table.DoesTableExist(db_.get());
  if (!has_metatable && db_->DoesTableExist("interest_groups")) {
    // Existing DB with no meta table. We have no idea what version the schema
    // is so we should remove it and start fresh.
    db_->Raze();
  }
  const bool new_db = !has_metatable;
  if (!meta_table.Init(db_.get(), kCurrentVersionNumber,
                       kCompatibleVersionNumber))
    return false;

  if (new_db)
    return CreateV6Schema(*db_);

  const int db_version = meta_table.GetVersionNumber();

  if (db_version >= kCurrentVersionNumber) {
    // Getting past RazeIfIncompatible implies that
    // kCurrentVersionNumber >= meta_table.GetCompatibleVersionNumber
    // So DB is either the current database version or a future version that is
    // back-compatible with this version of Chrome.
    return true;
  }

  // Older version - should be migrated.
  // db_version < kCurrentVersionNumber
  // db_version > kDeprecatedVersionNumber
  // TODO(behamilton): handle migration.
  NOTREACHED();  // There are currently no DB versions that can be migrated.
  return false;
}

void InterestGroupStorage::JoinInterestGroup(
    const blink::InterestGroup& group,
    const GURL& main_frame_joining_url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return;
  if (!DoJoinInterestGroup(*db_, group, main_frame_joining_url,
                           base::Time::Now(),
                           /*next_update_after=*/base::Time::Min()))
    DLOG(ERROR) << "Could not join interest group: " << db_->GetErrorMessage();
}

void InterestGroupStorage::LeaveInterestGroup(const url::Origin& owner,
                                              const std::string& name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return;
  if (!DoRemoveInterestGroup(*db_, owner, name))
    DLOG(ERROR) << "Could not leave interest group: " << db_->GetErrorMessage();
}

void InterestGroupStorage::UpdateInterestGroup(
    const blink::InterestGroup group) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return;

  if (!DoUpdateInterestGroup(*db_, group, base::Time::Now())) {
    DLOG(ERROR) << "Could not update interest group: "
                << db_->GetErrorMessage();
  }
}

void InterestGroupStorage::ReportUpdateFetchFailed(const url::Origin& owner,
                                                   const std::string& name,
                                                   bool net_disconnected) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized()) {
    NOTREACHED();  // We already fetched interest groups to update...
    return;
  }

  if (!DoReportUpdateFailed(*db_, owner, name, net_disconnected,
                            base::Time::Now())) {
    DLOG(ERROR) << "Couldn't update next_update_after: "
                << db_->GetErrorMessage();
  }
}

void InterestGroupStorage::RecordInterestGroupBid(const url::Origin& owner,
                                                  const std::string& name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return;

  if (!DoRecordInterestGroupBid(*db_, owner, name, base::Time::Now())) {
    DLOG(ERROR) << "Could not record win for interest group: "
                << db_->GetErrorMessage();
  }
}

void InterestGroupStorage::RecordInterestGroupWin(const url::Origin& owner,
                                                  const std::string& name,
                                                  const std::string& ad_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return;

  if (!DoRecordInterestGroupWin(*db_, owner, name, ad_json,
                                base::Time::Now())) {
    DLOG(ERROR) << "Could not record bid for interest group: "
                << db_->GetErrorMessage();
  }
}

void InterestGroupStorage::UpdateInterestGroupNameKAnonymity(
    const StorageInterestGroup::KAnonymityData& data,
    const absl::optional<base::Time>& update_sent_time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return;

  if (!DoUpdateInterestGroupNameKAnonymity(*db_, data, update_sent_time)) {
    DLOG(ERROR) << "Could not update k-anonymity for ad: "
                << db_->GetErrorMessage();
  }
}

void InterestGroupStorage::UpdateInterestGroupUpdateURLKAnonymity(
    const StorageInterestGroup::KAnonymityData& data,
    const absl::optional<base::Time>& update_sent_time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return;

  if (!DoUpdateInterestGroupUpdateURLKAnonymity(*db_, data, update_sent_time)) {
    DLOG(ERROR) << "Could not update k-anonymity for ad: "
                << db_->GetErrorMessage();
  }
}

void InterestGroupStorage::UpdateAdKAnonymity(
    const StorageInterestGroup::KAnonymityData& data,
    const absl::optional<base::Time>& update_sent_time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return;

  if (!DoUpdateAdKAnonymity(*db_, data, update_sent_time)) {
    DLOG(ERROR) << "Could not update k-anonymity for ad: "
                << db_->GetErrorMessage();
  }
}

std::vector<url::Origin> InterestGroupStorage::GetAllInterestGroupOwners() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return {};

  absl::optional<std::vector<url::Origin>> maybe_result =
      DoGetAllInterestGroupOwners(*db_, base::Time::Now());
  if (!maybe_result)
    return {};
  return std::move(maybe_result.value());
}

std::vector<StorageInterestGroup>
InterestGroupStorage::GetInterestGroupsForOwner(const url::Origin& owner) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return {};

  absl::optional<std::vector<StorageInterestGroup>> maybe_result =
      DoGetInterestGroupsForOwner(*db_, owner, base::Time::Now());
  if (!maybe_result)
    return {};
  base::UmaHistogramCounts1000("Storage.InterestGroup.PerSiteCount",
                               maybe_result->size());
  return std::move(maybe_result.value());
}

std::vector<StorageInterestGroup>
InterestGroupStorage::ClaimInterestGroupsForUpdate(const url::Origin& owner) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return {};

  absl::optional<std::vector<StorageInterestGroup>> maybe_result =
      DoGetInterestGroupsForOwner(*db_, owner, base::Time::Now(),
                                  /*claim_groups_for_update=*/true);
  if (!maybe_result)
    return {};
  return std::move(maybe_result.value());
}

void InterestGroupStorage::DeleteInterestGroupData(
    const base::RepeatingCallback<bool(const url::Origin&)>& origin_matcher) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!EnsureDBInitialized())
    return;

  if (!DoDeleteInterestGroupData(*db_, origin_matcher)) {
    DLOG(ERROR) << "Could not delete interest group data: "
                << db_->GetErrorMessage();
  }
}

void InterestGroupStorage::PerformDBMaintenance() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  last_maintenance_time_ = base::Time::Now();
  ops_since_last_maintenance_ = 0;
  int64_t db_size;
  if (base::GetFileSize(path_to_database_, &db_size)) {
    UMA_HISTOGRAM_MEMORY_KB("Storage.InterestGroup.DBSize", db_size / 1024);
  }
  if (EnsureDBInitialized()) {
    DoPerformDatabaseMaintenance(
        *db_, last_maintenance_time_, /*max_owners=*/max_owners_,
        /*max_owner_interest_groups=*/max_owner_interest_groups_);
  }
}

std::vector<StorageInterestGroup>
InterestGroupStorage::GetAllInterestGroupsUnfilteredForTesting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::Time distant_past = base::Time::Min();
  std::vector<StorageInterestGroup> result;
  absl::optional<std::vector<url::Origin>> maybe_owners =
      DoGetAllInterestGroupOwners(*db_, distant_past);
  if (!maybe_owners)
    return {};
  for (const auto& owner : *maybe_owners) {
    absl::optional<std::vector<StorageInterestGroup>> maybe_owner_results =
        DoGetInterestGroupsForOwner(*db_, owner, distant_past);
    DCHECK(maybe_owner_results);
    std::move(maybe_owner_results->begin(), maybe_owner_results->end(),
              std::back_inserter(result));
  }
  return result;
}

base::Time InterestGroupStorage::GetLastMaintenanceTimeForTesting() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return last_maintenance_time_;
}

void InterestGroupStorage::DatabaseErrorCallback(int extended_error,
                                                 sql::Statement* stmt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Only save the basic error code (not extended) to UMA.
  base::UmaHistogramExactLinear("Storage.InterestGroup.DBErrors",
                                extended_error & 0xFF,
                                /*sqlite error max+1*/ SQLITE_WARNING + 1);

  if (sql::IsErrorCatastrophic(extended_error)) {
    // Normally this will poison the database, causing any subsequent operations
    // to silently fail without any side effects. However, if RazeAndClose() is
    // called from the error callback in response to an error raised from within
    // sql::Database::Open, opening the now-razed database will be retried.
    db_->RazeAndClose();
    return;
  }

  // The default handling is to assert on debug and to ignore on release.
  if (!sql::Database::IsExpectedSqliteError(extended_error))
    DLOG(FATAL) << db_->GetErrorMessage();
}

}  // namespace content
