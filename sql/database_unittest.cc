// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sql/database.h"

#include <stddef.h>
#include <stdint.h>
#include <cstdint>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/test/gtest_util.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/trace_event/process_memory_dump.h"
#include "build/build_config.h"
#include "sql/database_memory_dump_provider.h"
#include "sql/meta_table.h"
#include "sql/sql_features.h"
#include "sql/statement.h"
#include "sql/test/database_test_peer.h"
#include "sql/test/error_callback_support.h"
#include "sql/test/scoped_error_expecter.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/sqlite/sqlite3.h"

namespace sql {

namespace {

using sql::test::ExecuteWithResult;

// Helper to return the count of items in sqlite_schema.  Return -1 in
// case of error.
int SqliteSchemaCount(Database* db) {
  const char* kSchemaCount = "SELECT COUNT(*) FROM sqlite_schema";
  Statement s(db->GetUniqueStatement(kSchemaCount));
  return s.Step() ? s.ColumnInt(0) : -1;
}

// Track the number of valid references which share the same pointer.
// This is used to allow testing an implicitly use-after-free case by
// explicitly having the ref count live longer than the object.
class RefCounter {
 public:
  explicit RefCounter(size_t* counter) : counter_(counter) { (*counter_)++; }
  RefCounter(const RefCounter& other) : counter_(other.counter_) {
    (*counter_)++;
  }
  RefCounter& operator=(const RefCounter&) = delete;
  ~RefCounter() { (*counter_)--; }

 private:
  raw_ptr<size_t> counter_;
};

// Empty callback for implementation of ErrorCallbackSetHelper().
void IgnoreErrorCallback(int error, Statement* stmt) {}

void ErrorCallbackSetHelper(Database* db,
                            size_t* counter,
                            const RefCounter& r,
                            int error,
                            Statement* stmt) {
  // The ref count should not go to zero when changing the callback.
  EXPECT_GT(*counter, 0u);
  db->set_error_callback(base::BindRepeating(&IgnoreErrorCallback));
  EXPECT_GT(*counter, 0u);
}

void ErrorCallbackResetHelper(Database* db,
                              size_t* counter,
                              const RefCounter& r,
                              int error,
                              Statement* stmt) {
  // The ref count should not go to zero when clearing the callback.
  EXPECT_GT(*counter, 0u);
  db->reset_error_callback();
  EXPECT_GT(*counter, 0u);
}

// Handle errors by blowing away the database.
void RazeErrorCallback(Database* db,
                       int expected_error,
                       int error,
                       Statement* stmt) {
  // Nothing here needs extended errors at this time.
  EXPECT_EQ(expected_error, expected_error & 0xff);
  EXPECT_EQ(expected_error, error & 0xff);
  db->RazeAndClose();
}

#if BUILDFLAG(IS_POSIX)
// Set a umask and restore the old mask on destruction.  Cribbed from
// shared_memory_unittest.cc.  Used by POSIX-only UserPermission test.
class ScopedUmaskSetter {
 public:
  explicit ScopedUmaskSetter(mode_t target_mask) {
    old_umask_ = umask(target_mask);
  }
  ~ScopedUmaskSetter() { umask(old_umask_); }

  ScopedUmaskSetter(const ScopedUmaskSetter&) = delete;
  ScopedUmaskSetter& operator=(const ScopedUmaskSetter&) = delete;

 private:
  mode_t old_umask_;
};
#endif  // BUILDFLAG(IS_POSIX)

}  // namespace

// We use the parameter to run all tests with WAL mode on and off.
class SQLDatabaseTest : public testing::Test,
                        public testing::WithParamInterface<bool> {
 public:
  enum class OverwriteType {
    kTruncate,
    kOverwrite,
  };

  ~SQLDatabaseTest() override = default;

  void SetUp() override {
    db_ = std::make_unique<Database>(GetDBOptions());
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    db_path_ = temp_dir_.GetPath().AppendASCII("database_test.sqlite");
    ASSERT_TRUE(db_->Open(db_path_));
  }

  DatabaseOptions GetDBOptions() {
    DatabaseOptions options;
    options.wal_mode = IsWALEnabled();
    // TODO(crbug.com/1120969): Remove after switching to exclusive mode on by
    // default.
    options.exclusive_locking = false;
#if BUILDFLAG(IS_FUCHSIA)  // Exclusive mode needs to be enabled to enter WAL
                           // mode on Fuchsia
    if (IsWALEnabled()) {
      options.exclusive_locking = true;
    }
#endif  // BUILDFLAG(IS_FUCHSIA)
    return options;
  }

  bool IsWALEnabled() { return GetParam(); }

  bool TruncateDatabase() {
    base::File file(db_path_,
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    return file.SetLength(0);
  }

  bool OverwriteDatabaseHeader(OverwriteType type) {
    base::File file(db_path_,
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    if (type == OverwriteType::kTruncate) {
      if (!file.SetLength(0))
        return false;
    }

    static constexpr char kText[] = "Now is the winter of our discontent.";
    constexpr int kTextBytes = sizeof(kText) - 1;
    return file.Write(0, kText, kTextBytes) == kTextBytes;
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath db_path_;
  std::unique_ptr<Database> db_;
};

TEST_P(SQLDatabaseTest, Execute_ValidStatement) {
  ASSERT_TRUE(db_->Execute("CREATE TABLE data(contents TEXT)"));
  EXPECT_EQ(SQLITE_OK, db_->GetErrorCode());
}

TEST_P(SQLDatabaseTest, Execute_InvalidStatement) {
  {
    sql::test::ScopedErrorExpecter error_expecter;
    error_expecter.ExpectError(SQLITE_ERROR);
    EXPECT_FALSE(db_->Execute("CREATE TABLE data("));
    EXPECT_TRUE(error_expecter.SawExpectedErrors());
  }
  EXPECT_EQ(SQLITE_ERROR, db_->GetErrorCode());
}

TEST_P(SQLDatabaseTest, ExecuteScriptForTesting_OneLineValid) {
  ASSERT_TRUE(db_->ExecuteScriptForTesting("CREATE TABLE data(contents TEXT)"));
  EXPECT_EQ(SQLITE_OK, db_->GetErrorCode());
}

TEST_P(SQLDatabaseTest, ExecuteScriptForTesting_OneLineInvalid) {
  ASSERT_FALSE(db_->ExecuteScriptForTesting("CREATE TABLE data("));
  EXPECT_EQ(SQLITE_ERROR, db_->GetErrorCode());
}

TEST_P(SQLDatabaseTest, ExecuteScriptForTesting_ExtraContents) {
  EXPECT_TRUE(db_->ExecuteScriptForTesting("CREATE TABLE data1(id)"))
      << "Minimal statement";
  EXPECT_TRUE(db_->ExecuteScriptForTesting("CREATE TABLE data2(id);"))
      << "Extra semicolon";
  EXPECT_TRUE(db_->ExecuteScriptForTesting("CREATE TABLE data3(id) -- Comment"))
      << "Trailing comment";

  EXPECT_TRUE(db_->ExecuteScriptForTesting(
      "CREATE TABLE data4(id);CREATE TABLE data5(id)"))
      << "Extra statement without whitespace";
  EXPECT_TRUE(db_->ExecuteScriptForTesting(
      "CREATE TABLE data6(id); CREATE TABLE data7(id)"))
      << "Extra statement separated by whitespace";

  EXPECT_TRUE(db_->ExecuteScriptForTesting("CREATE TABLE data8(id);-- Comment"))
      << "Comment without whitespace";
  EXPECT_TRUE(
      db_->ExecuteScriptForTesting("CREATE TABLE data9(id); -- Comment"))
      << "Comment sepatated by whitespace";
}

TEST_P(SQLDatabaseTest, ExecuteScriptForTesting_MultipleValidLines) {
  EXPECT_TRUE(db_->ExecuteScriptForTesting(R"(
      CREATE TABLE data1(contents TEXT);
      CREATE TABLE data2(contents TEXT);
      CREATE TABLE data3(contents TEXT);
  )"));
  EXPECT_EQ(SQLITE_OK, db_->GetErrorCode());

  // DoesColumnExist() is implemented directly on top of a SQLite call. The
  // other schema functions use sql::Statement infrastructure to query the
  // schema table.
  EXPECT_TRUE(db_->DoesColumnExist("data1", "contents"));
  EXPECT_TRUE(db_->DoesColumnExist("data2", "contents"));
  EXPECT_TRUE(db_->DoesColumnExist("data3", "contents"));
}

TEST_P(SQLDatabaseTest, ExecuteScriptForTesting_StopsOnCompileError) {
  EXPECT_FALSE(db_->ExecuteScriptForTesting(R"(
      CREATE TABLE data1(contents TEXT);
      CREATE TABLE data1();
      CREATE TABLE data3(contents TEXT);
  )"));
  EXPECT_EQ(SQLITE_ERROR, db_->GetErrorCode());

  EXPECT_TRUE(db_->DoesColumnExist("data1", "contents"));
  EXPECT_FALSE(db_->DoesColumnExist("data3", "contents"));
}

TEST_P(SQLDatabaseTest, ExecuteScriptForTesting_StopsOnStepError) {
  EXPECT_FALSE(db_->ExecuteScriptForTesting(R"(
      CREATE TABLE data1(contents TEXT UNIQUE);
      INSERT INTO data1(contents) VALUES('value1');
      INSERT INTO data1(contents) VALUES('value1');
      CREATE TABLE data3(contents TEXT);
  )"));
  EXPECT_EQ(SQLITE_CONSTRAINT_UNIQUE, db_->GetErrorCode());

  EXPECT_TRUE(db_->DoesColumnExist("data1", "contents"));
  EXPECT_FALSE(db_->DoesColumnExist("data3", "contents"));
}

TEST_P(SQLDatabaseTest, CachedStatement) {
  StatementID id1 = SQL_FROM_HERE;
  StatementID id2 = SQL_FROM_HERE;
  static const char kId1Sql[] = "SELECT a FROM foo";
  static const char kId2Sql[] = "SELECT b FROM foo";

  ASSERT_TRUE(db_->Execute("CREATE TABLE foo (a, b)"));
  ASSERT_TRUE(db_->Execute("INSERT INTO foo(a, b) VALUES (12, 13)"));

  sqlite3_stmt* raw_id1_statement;
  sqlite3_stmt* raw_id2_statement;
  {
    scoped_refptr<Database::StatementRef> ref_from_id1 =
        db_->GetCachedStatement(id1, kId1Sql);
    raw_id1_statement = ref_from_id1->stmt();

    Statement from_id1(std::move(ref_from_id1));
    ASSERT_TRUE(from_id1.is_valid());
    ASSERT_TRUE(from_id1.Step());
    EXPECT_EQ(12, from_id1.ColumnInt(0));

    scoped_refptr<Database::StatementRef> ref_from_id2 =
        db_->GetCachedStatement(id2, kId2Sql);
    raw_id2_statement = ref_from_id2->stmt();
    EXPECT_NE(raw_id1_statement, raw_id2_statement);

    Statement from_id2(std::move(ref_from_id2));
    ASSERT_TRUE(from_id2.is_valid());
    ASSERT_TRUE(from_id2.Step());
    EXPECT_EQ(13, from_id2.ColumnInt(0));
  }

  {
    scoped_refptr<Database::StatementRef> ref_from_id1 =
        db_->GetCachedStatement(id1, kId1Sql);
    EXPECT_EQ(raw_id1_statement, ref_from_id1->stmt())
        << "statement was not cached";

    Statement from_id1(std::move(ref_from_id1));
    ASSERT_TRUE(from_id1.is_valid());
    ASSERT_TRUE(from_id1.Step()) << "cached statement was not reset";
    EXPECT_EQ(12, from_id1.ColumnInt(0));

    scoped_refptr<Database::StatementRef> ref_from_id2 =
        db_->GetCachedStatement(id2, kId2Sql);
    EXPECT_EQ(raw_id2_statement, ref_from_id2->stmt())
        << "statement was not cached";

    Statement from_id2(std::move(ref_from_id2));
    ASSERT_TRUE(from_id2.is_valid());
    ASSERT_TRUE(from_id2.Step()) << "cached statement was not reset";
    EXPECT_EQ(13, from_id2.ColumnInt(0));
  }

  EXPECT_DCHECK_DEATH(db_->GetCachedStatement(id1, kId2Sql))
      << "Using a different SQL with the same statement ID should DCHECK";
  EXPECT_DCHECK_DEATH(db_->GetCachedStatement(id2, kId1Sql))
      << "Using a different SQL with the same statement ID should DCHECK";
}

TEST_P(SQLDatabaseTest, IsSQLValidTest) {
  ASSERT_TRUE(db_->Execute("CREATE TABLE foo (a, b)"));
  ASSERT_TRUE(db_->IsSQLValid("SELECT a FROM foo"));
  ASSERT_FALSE(db_->IsSQLValid("SELECT no_exist FROM foo"));
}

TEST_P(SQLDatabaseTest, DoesTableExist) {
  EXPECT_FALSE(db_->DoesTableExist("foo"));
  EXPECT_FALSE(db_->DoesTableExist("foo_index"));

  ASSERT_TRUE(db_->Execute("CREATE TABLE foo (a, b)"));
  ASSERT_TRUE(db_->Execute("CREATE INDEX foo_index ON foo (a)"));
  EXPECT_TRUE(db_->DoesTableExist("foo"));
  EXPECT_FALSE(db_->DoesTableExist("foo_index"));

  // DoesTableExist() is case-sensitive.
  EXPECT_FALSE(db_->DoesTableExist("Foo"));
  EXPECT_FALSE(db_->DoesTableExist("FOO"));
}

TEST_P(SQLDatabaseTest, DoesIndexExist) {
  ASSERT_TRUE(db_->Execute("CREATE TABLE foo (a, b)"));
  EXPECT_FALSE(db_->DoesIndexExist("foo"));
  EXPECT_FALSE(db_->DoesIndexExist("foo_ubdex"));

  ASSERT_TRUE(db_->Execute("CREATE INDEX foo_index ON foo (a)"));
  EXPECT_TRUE(db_->DoesIndexExist("foo_index"));
  EXPECT_FALSE(db_->DoesIndexExist("foo"));

  // DoesIndexExist() is case-sensitive.
  EXPECT_FALSE(db_->DoesIndexExist("Foo_index"));
  EXPECT_FALSE(db_->DoesIndexExist("Foo_Index"));
  EXPECT_FALSE(db_->DoesIndexExist("FOO_INDEX"));
}

TEST_P(SQLDatabaseTest, DoesViewExist) {
  EXPECT_FALSE(db_->DoesViewExist("voo"));
  ASSERT_TRUE(db_->Execute("CREATE VIEW voo (a) AS SELECT 1"));
  EXPECT_FALSE(db_->DoesIndexExist("voo"));
  EXPECT_FALSE(db_->DoesTableExist("voo"));
  EXPECT_TRUE(db_->DoesViewExist("voo"));

  // DoesTableExist() is case-sensitive.
  EXPECT_FALSE(db_->DoesViewExist("Voo"));
  EXPECT_FALSE(db_->DoesViewExist("VOO"));
}

TEST_P(SQLDatabaseTest, DoesColumnExist) {
  ASSERT_TRUE(db_->Execute("CREATE TABLE foo (a, b)"));

  EXPECT_FALSE(db_->DoesColumnExist("foo", "bar"));
  EXPECT_TRUE(db_->DoesColumnExist("foo", "a"));

  ASSERT_FALSE(db_->DoesTableExist("bar"));
  EXPECT_FALSE(db_->DoesColumnExist("bar", "b"));

  // SQLite resolves table/column names without case sensitivity.
  EXPECT_TRUE(db_->DoesColumnExist("FOO", "A"));
  EXPECT_TRUE(db_->DoesColumnExist("FOO", "a"));
  EXPECT_TRUE(db_->DoesColumnExist("foo", "A"));
}

TEST_P(SQLDatabaseTest, GetLastInsertRowId) {
  ASSERT_TRUE(db_->Execute("CREATE TABLE foo (id INTEGER PRIMARY KEY, value)"));

  ASSERT_TRUE(db_->Execute("INSERT INTO foo (value) VALUES (12)"));

  // Last insert row ID should be valid.
  int64_t row = db_->GetLastInsertRowId();
  EXPECT_LT(0, row);

  // It should be the primary key of the row we just inserted.
  Statement s(db_->GetUniqueStatement("SELECT value FROM foo WHERE id=?"));
  s.BindInt64(0, row);
  ASSERT_TRUE(s.Step());
  EXPECT_EQ(12, s.ColumnInt(0));
}

TEST_P(SQLDatabaseTest, Rollback) {
  ASSERT_TRUE(db_->BeginTransaction());
  ASSERT_TRUE(db_->BeginTransaction());
  EXPECT_EQ(2, db_->transaction_nesting());
  db_->RollbackTransaction();
  EXPECT_FALSE(db_->CommitTransaction());
  EXPECT_TRUE(db_->BeginTransaction());
}

// Test the scoped error expecter by attempting to insert a duplicate
// value into an index.
TEST_P(SQLDatabaseTest, ScopedErrorExpecter) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER UNIQUE)";
  ASSERT_TRUE(db_->Execute(kCreateSql));
  ASSERT_TRUE(db_->Execute("INSERT INTO foo (id) VALUES (12)"));

  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_CONSTRAINT);
    ASSERT_FALSE(db_->Execute("INSERT INTO foo (id) VALUES (12)"));
    ASSERT_TRUE(expecter.SawExpectedErrors());
  }
}

TEST_P(SQLDatabaseTest, SchemaIntrospectionUsesErrorExpecter) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER UNIQUE)";
  ASSERT_TRUE(db_->Execute(kCreateSql));
  ASSERT_FALSE(db_->DoesTableExist("bar"));
  ASSERT_TRUE(db_->DoesTableExist("foo"));
  ASSERT_TRUE(db_->DoesColumnExist("foo", "id"));
  db_->Close();

  // Corrupt the database so that nothing works, including PRAGMAs.
  ASSERT_TRUE(sql::test::CorruptSizeInHeader(db_path_));

  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_CORRUPT);
    ASSERT_TRUE(db_->Open(db_path_));
    ASSERT_FALSE(db_->DoesTableExist("bar"));
    ASSERT_FALSE(db_->DoesTableExist("foo"));
    ASSERT_FALSE(db_->DoesColumnExist("foo", "id"));
    ASSERT_TRUE(expecter.SawExpectedErrors());
  }
}

TEST_P(SQLDatabaseTest, ErrorCallback) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER UNIQUE)";
  ASSERT_TRUE(db_->Execute(kCreateSql));
  ASSERT_TRUE(db_->Execute("INSERT INTO foo (id) VALUES (12)"));

  int error = SQLITE_OK;
  {
    ScopedErrorCallback sec(db_.get(),
                            base::BindRepeating(&CaptureErrorCallback, &error));
    EXPECT_FALSE(db_->Execute("INSERT INTO foo (id) VALUES (12)"));

    // Later versions of SQLite throw SQLITE_CONSTRAINT_UNIQUE.  The specific
    // sub-error isn't really important.
    EXPECT_EQ(SQLITE_CONSTRAINT, (error & 0xff));
  }

  // Callback is no longer in force due to reset.
  {
    error = SQLITE_OK;
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_CONSTRAINT);
    ASSERT_FALSE(db_->Execute("INSERT INTO foo (id) VALUES (12)"));
    ASSERT_TRUE(expecter.SawExpectedErrors());
    EXPECT_EQ(SQLITE_OK, error);
  }

  // base::BindRepeating() can curry arguments to be passed by const reference
  // to the callback function.  If the callback function calls
  // re/set_error_callback(), the storage for those arguments can be
  // deleted while the callback function is still executing.
  //
  // RefCounter() counts how many objects are live using an external
  // count.  The same counter is passed to the callback, so that it
  // can check directly even if the RefCounter object is no longer
  // live.
  {
    size_t count = 0;
    ScopedErrorCallback sec(
        db_.get(), base::BindRepeating(&ErrorCallbackSetHelper, db_.get(),
                                       &count, RefCounter(&count)));

    EXPECT_FALSE(db_->Execute("INSERT INTO foo (id) VALUES (12)"));
  }

  // Same test, but reset_error_callback() case.
  {
    size_t count = 0;
    ScopedErrorCallback sec(
        db_.get(), base::BindRepeating(&ErrorCallbackResetHelper, db_.get(),
                                       &count, RefCounter(&count)));

    EXPECT_FALSE(db_->Execute("INSERT INTO foo (id) VALUES (12)"));
  }
}

TEST_P(SQLDatabaseTest, Execute_CompilationError) {
  bool error_callback_called = false;
  db_->set_error_callback(base::BindLambdaForTesting([&](int error,
                                                         sql::Statement*
                                                             statement) {
    EXPECT_EQ(SQLITE_ERROR, error);
    EXPECT_EQ(nullptr, statement);
    EXPECT_FALSE(error_callback_called)
        << "SQL compilation errors should call the error callback exactly once";
    error_callback_called = true;
  }));

  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_ERROR);
    EXPECT_FALSE(db_->Execute("SELECT missing_column FROM missing_table"));
    EXPECT_TRUE(expecter.SawExpectedErrors());
  }

  EXPECT_TRUE(error_callback_called)
      << "SQL compilation errors should call the error callback";
}

TEST_P(SQLDatabaseTest, GetUniqueStatement_CompilationError) {
  bool error_callback_called = false;
  db_->set_error_callback(base::BindLambdaForTesting([&](int error,
                                                         sql::Statement*
                                                             statement) {
    EXPECT_EQ(SQLITE_ERROR, error);
    EXPECT_EQ(nullptr, statement);
    EXPECT_FALSE(error_callback_called)
        << "SQL compilation errors should call the error callback exactly once";
    error_callback_called = true;
  }));

  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_ERROR);
    sql::Statement statement(
        db_->GetUniqueStatement("SELECT missing_column FROM missing_table"));
    EXPECT_FALSE(statement.is_valid());
    EXPECT_TRUE(expecter.SawExpectedErrors());
  }

  EXPECT_TRUE(error_callback_called)
      << "SQL compilation errors should call the error callback";
}

TEST_P(SQLDatabaseTest, GetCachedStatement_CompilationError) {
  bool error_callback_called = false;
  db_->set_error_callback(base::BindLambdaForTesting([&](int error,
                                                         sql::Statement*
                                                             statement) {
    EXPECT_EQ(SQLITE_ERROR, error);
    EXPECT_EQ(nullptr, statement);
    EXPECT_FALSE(error_callback_called)
        << "SQL compilation errors should call the error callback exactly once";
    error_callback_called = true;
  }));

  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_ERROR);
    sql::Statement statement(db_->GetCachedStatement(
        SQL_FROM_HERE, "SELECT missing_column FROM missing_table"));
    EXPECT_FALSE(statement.is_valid());
    EXPECT_TRUE(expecter.SawExpectedErrors());
  }

  EXPECT_TRUE(error_callback_called)
      << "SQL compilation errors should call the error callback";
}

TEST_P(SQLDatabaseTest, GetUniqueStatement_ExtraContents) {
  sql::Statement minimal(db_->GetUniqueStatement("SELECT 1"));
  sql::Statement extra_semicolon(db_->GetUniqueStatement("SELECT 1;"));

  // It would be nice to flag trailing comments too, as they cost binary size.
  // However, there's no easy way of doing that.
  sql::Statement trailing_comment(
      db_->GetUniqueStatement("SELECT 1 -- Comment"));

  EXPECT_DCHECK_DEATH(db_->GetUniqueStatement("SELECT 1;SELECT 2"))
      << "Extra statement without whitespace";
  EXPECT_DCHECK_DEATH(db_->GetUniqueStatement("SELECT 1; SELECT 2"))
      << "Extra statement separated by whitespace";
  EXPECT_DCHECK_DEATH(db_->GetUniqueStatement("SELECT 1;-- Comment"))
      << "Comment without whitespace";
  EXPECT_DCHECK_DEATH(db_->GetUniqueStatement("SELECT 1; -- Comment"))
      << "Comment separated by whitespace";
}

TEST_P(SQLDatabaseTest, GetCachedStatement_ExtraContents) {
  sql::Statement minimal(db_->GetCachedStatement(SQL_FROM_HERE, "SELECT 1"));
  sql::Statement extra_semicolon(
      db_->GetCachedStatement(SQL_FROM_HERE, "SELECT 1;"));

  // It would be nice to flag trailing comments too, as they cost binary size.
  // However, there's no easy way of doing that.
  sql::Statement trailing_comment(
      db_->GetCachedStatement(SQL_FROM_HERE, "SELECT 1 -- Comment"));

  EXPECT_DCHECK_DEATH(
      db_->GetCachedStatement(SQL_FROM_HERE, "SELECT 1;SELECT 2"))
      << "Extra statement without whitespace";
  EXPECT_DCHECK_DEATH(
      db_->GetCachedStatement(SQL_FROM_HERE, "SELECT 1; SELECT 2"))
      << "Extra statement separated by whitespace";
  EXPECT_DCHECK_DEATH(
      db_->GetCachedStatement(SQL_FROM_HERE, "SELECT 1;-- Comment"))
      << "Comment without whitespace";
  EXPECT_DCHECK_DEATH(
      db_->GetCachedStatement(SQL_FROM_HERE, "SELECT 1; -- Comment"))
      << "Comment separated by whitespace";
}

TEST_P(SQLDatabaseTest, IsSQLValid_ExtraContents) {
  EXPECT_TRUE(db_->IsSQLValid("SELECT 1"));
  EXPECT_TRUE(db_->IsSQLValid("SELECT 1;"))
      << "Trailing semicolons are currently tolerated";

  // It would be nice to flag trailing comments too, as they cost binary size.
  // However, there's no easy way of doing that.
  EXPECT_TRUE(db_->IsSQLValid("SELECT 1 -- Comment"))
      << "Trailing comments are currently tolerated";

  EXPECT_DCHECK_DEATH(db_->IsSQLValid("SELECT 1;SELECT 2"))
      << "Extra statement without whitespace";
  EXPECT_DCHECK_DEATH(db_->IsSQLValid("SELECT 1; SELECT 2"))
      << "Extra statement separated by whitespace";
  EXPECT_DCHECK_DEATH(db_->IsSQLValid("SELECT 1;-- Comment"))
      << "Comment without whitespace";
  EXPECT_DCHECK_DEATH(db_->IsSQLValid("SELECT 1; -- Comment"))
      << "Comment separated by whitespace";
}

TEST_P(SQLDatabaseTest, GetUniqueStatement_NoContents) {
  EXPECT_DCHECK_DEATH(db_->GetUniqueStatement("")) << "Empty string";
  EXPECT_DCHECK_DEATH(db_->GetUniqueStatement(" ")) << "Space";
  EXPECT_DCHECK_DEATH(db_->GetUniqueStatement("\n")) << "Newline";
  EXPECT_DCHECK_DEATH(db_->GetUniqueStatement("-- Comment")) << "Comment";
}

TEST_P(SQLDatabaseTest, GetCachedStatement_NoContents) {
  EXPECT_DCHECK_DEATH(db_->GetCachedStatement(SQL_FROM_HERE, ""))
      << "Empty string";
  EXPECT_DCHECK_DEATH(db_->GetCachedStatement(SQL_FROM_HERE, " ")) << "Space";
  EXPECT_DCHECK_DEATH(db_->GetCachedStatement(SQL_FROM_HERE, "\n"))
      << "Newline";
  EXPECT_DCHECK_DEATH(db_->GetCachedStatement(SQL_FROM_HERE, "-- Comment"))
      << "Comment";
}

TEST_P(SQLDatabaseTest, IsSQLValid_NoContents) {
  EXPECT_DCHECK_DEATH(db_->IsSQLValid("")) << "Empty string";
  EXPECT_DCHECK_DEATH(db_->IsSQLValid(" ")) << "Space";
  EXPECT_DCHECK_DEATH(db_->IsSQLValid("\n")) << "Newline";
  EXPECT_DCHECK_DEATH(db_->IsSQLValid("-- Comment")) << "Comment";
}

// Test that Database::Raze() results in a database without the
// tables from the original database.
TEST_P(SQLDatabaseTest, Raze) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER PRIMARY KEY, value)";
  ASSERT_TRUE(db_->Execute(kCreateSql));
  ASSERT_TRUE(db_->Execute("INSERT INTO foo (value) VALUES (12)"));

  int pragma_auto_vacuum = 0;
  {
    Statement s(db_->GetUniqueStatement("PRAGMA auto_vacuum"));
    ASSERT_TRUE(s.Step());
    pragma_auto_vacuum = s.ColumnInt(0);
    ASSERT_TRUE(pragma_auto_vacuum == 0 || pragma_auto_vacuum == 1);
  }

  // If auto_vacuum is set, there's an extra page to maintain a freelist.
  const int kExpectedPageCount = 2 + pragma_auto_vacuum;

  {
    Statement s(db_->GetUniqueStatement("PRAGMA page_count"));
    ASSERT_TRUE(s.Step());
    EXPECT_EQ(kExpectedPageCount, s.ColumnInt(0));
  }

  {
    Statement s(db_->GetUniqueStatement("SELECT * FROM sqlite_schema"));
    ASSERT_TRUE(s.Step());
    EXPECT_EQ("table", s.ColumnString(0));
    EXPECT_EQ("foo", s.ColumnString(1));
    EXPECT_EQ("foo", s.ColumnString(2));
    // Table "foo" is stored in the last page of the file.
    EXPECT_EQ(kExpectedPageCount, s.ColumnInt(3));
    EXPECT_EQ(kCreateSql, s.ColumnString(4));
  }

  ASSERT_TRUE(db_->Raze());

  {
    Statement s(db_->GetUniqueStatement("PRAGMA page_count"));
    ASSERT_TRUE(s.Step());
    EXPECT_EQ(1, s.ColumnInt(0));
  }

  ASSERT_EQ(0, SqliteSchemaCount(db_.get()));

  {
    Statement s(db_->GetUniqueStatement("PRAGMA auto_vacuum"));
    ASSERT_TRUE(s.Step());
    // The new database has the same auto_vacuum as a fresh database.
    EXPECT_EQ(pragma_auto_vacuum, s.ColumnInt(0));
  }
}

// Helper for SQLDatabaseTest.RazePageSize.  Creates a fresh db based on
// db_prefix, with the given initial page size, and verifies it against the
// expected size.  Then changes to the final page size and razes, verifying that
// the fresh database ends up with the expected final page size.
void TestPageSize(const base::FilePath& db_prefix,
                  int initial_page_size,
                  const std::string& expected_initial_page_size,
                  int final_page_size,
                  const std::string& expected_final_page_size) {
  static const char kCreateSql[] = "CREATE TABLE x (t TEXT)";
  static const char kInsertSql1[] = "INSERT INTO x VALUES ('This is a test')";
  static const char kInsertSql2[] = "INSERT INTO x VALUES ('That was a test')";

  const base::FilePath db_path = db_prefix.InsertBeforeExtensionASCII(
      base::NumberToString(initial_page_size));
  Database::Delete(db_path);
  Database db({.page_size = initial_page_size});
  ASSERT_TRUE(db.Open(db_path));
  ASSERT_TRUE(db.Execute(kCreateSql));
  ASSERT_TRUE(db.Execute(kInsertSql1));
  ASSERT_TRUE(db.Execute(kInsertSql2));
  ASSERT_EQ(expected_initial_page_size,
            ExecuteWithResult(&db, "PRAGMA page_size"));
  db.Close();

  // Re-open the database while setting a new |options.page_size| in the object.
  Database razed_db({.page_size = final_page_size});
  ASSERT_TRUE(razed_db.Open(db_path));
  // Raze will use the page size set in the connection object, which may not
  // match the file's page size.
  ASSERT_TRUE(razed_db.Raze());

  // SQLite 3.10.2 (at least) has a quirk with the sqlite3_backup() API (used by
  // Raze()) which causes the destination database to remember the previous
  // page_size, even if the overwriting database changed the page_size.  Access
  // the actual database to cause the cached value to be updated.
  EXPECT_EQ("0",
            ExecuteWithResult(&razed_db, "SELECT COUNT(*) FROM sqlite_schema"));

  EXPECT_EQ(expected_final_page_size,
            ExecuteWithResult(&razed_db, "PRAGMA page_size"));
  EXPECT_EQ("1", ExecuteWithResult(&razed_db, "PRAGMA page_count"));
}

// Verify that Recovery maintains the page size, and the virtual table
// works with page sizes other than SQLite's default.  Also verify the case
// where the default page size has changed.
TEST_P(SQLDatabaseTest, RazePageSize) {
  const std::string default_page_size =
      ExecuteWithResult(db_.get(), "PRAGMA page_size");

  // Sync uses 32k pages.
  EXPECT_NO_FATAL_FAILURE(
      TestPageSize(db_path_, 32768, "32768", 32768, "32768"));

  // Many clients use 4k pages.  This is the SQLite default after 3.12.0.
  EXPECT_NO_FATAL_FAILURE(TestPageSize(db_path_, 4096, "4096", 4096, "4096"));

  // 1k is the default page size before 3.12.0.
  EXPECT_NO_FATAL_FAILURE(TestPageSize(db_path_, 1024, "1024", 1024, "1024"));

  EXPECT_NO_FATAL_FAILURE(TestPageSize(db_path_, 2048, "2048", 4096, "4096"));

  // Databases with no page size specified should result in the default
  // page size.  2k has never been the default page size.
  ASSERT_NE("2048", default_page_size);
  EXPECT_NO_FATAL_FAILURE(TestPageSize(db_path_, 2048, "2048",
                                       DatabaseOptions::kDefaultPageSize,
                                       default_page_size));
}

// Test that Raze() results are seen in other connections.
TEST_P(SQLDatabaseTest, RazeMultiple) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER PRIMARY KEY, value)";
  ASSERT_TRUE(db_->Execute(kCreateSql));

  Database other_db(GetDBOptions());
  ASSERT_TRUE(other_db.Open(db_path_));

  // Check that the second connection sees the table.
  ASSERT_EQ(1, SqliteSchemaCount(&other_db));

  ASSERT_TRUE(db_->Raze());

  // The second connection sees the updated database.
  ASSERT_EQ(0, SqliteSchemaCount(&other_db));
}

TEST_P(SQLDatabaseTest, RazeLocked) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER PRIMARY KEY, value)";
  ASSERT_TRUE(db_->Execute(kCreateSql));

  // Open a transaction and write some data in a second connection.
  // This will acquire a PENDING or EXCLUSIVE transaction, which will
  // cause the raze to fail.
  Database other_db(GetDBOptions());
  ASSERT_TRUE(other_db.Open(db_path_));
  ASSERT_TRUE(other_db.BeginTransaction());
  const char* kInsertSql = "INSERT INTO foo VALUES (1, 'data')";
  ASSERT_TRUE(other_db.Execute(kInsertSql));

  ASSERT_FALSE(db_->Raze());

  // Works after COMMIT.
  ASSERT_TRUE(other_db.CommitTransaction());
  ASSERT_TRUE(db_->Raze());

  // Re-create the database.
  ASSERT_TRUE(db_->Execute(kCreateSql));
  ASSERT_TRUE(db_->Execute(kInsertSql));

  // An unfinished read transaction in the other connection also
  // blocks raze.
  // This doesn't happen in WAL mode because reads are no longer blocked by
  // write operations when using a WAL.
  if (!IsWALEnabled()) {
    const char* kQuery = "SELECT COUNT(*) FROM foo";
    Statement s(other_db.GetUniqueStatement(kQuery));
    ASSERT_TRUE(s.Step());
    ASSERT_FALSE(db_->Raze());

    // Completing the statement unlocks the database.
    ASSERT_FALSE(s.Step());
    ASSERT_TRUE(db_->Raze());
  }
}

// Verify that Raze() can handle an empty file.  SQLite should treat
// this as an empty database.
TEST_P(SQLDatabaseTest, RazeEmptyDB) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER PRIMARY KEY, value)";
  ASSERT_TRUE(db_->Execute(kCreateSql));
  db_->Close();

  ASSERT_TRUE(TruncateDatabase());

  ASSERT_TRUE(db_->Open(db_path_));
  ASSERT_TRUE(db_->Raze());
  EXPECT_EQ(0, SqliteSchemaCount(db_.get()));
}

// Verify that Raze() can handle a file of junk.
// Need exclusive mode off here as there are some subtleties (by design) around
// how the cache is used with it on which causes the test to fail.
TEST_P(SQLDatabaseTest, RazeNOTADB) {
  db_->Close();
  Database::Delete(db_path_);
  ASSERT_FALSE(base::PathExists(db_path_));

  ASSERT_TRUE(OverwriteDatabaseHeader(OverwriteType::kTruncate));
  ASSERT_TRUE(base::PathExists(db_path_));

  // SQLite will successfully open the handle, but fail when running PRAGMA
  // statements that access the database.
  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_NOTADB);

    EXPECT_TRUE(db_->Open(db_path_));
    ASSERT_TRUE(expecter.SawExpectedErrors());
  }
  EXPECT_TRUE(db_->Raze());
  db_->Close();

  // Now empty, the open should open an empty database.
  EXPECT_TRUE(db_->Open(db_path_));
  EXPECT_EQ(0, SqliteSchemaCount(db_.get()));
}

// Verify that Raze() can handle a database overwritten with garbage.
TEST_P(SQLDatabaseTest, RazeNOTADB2) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER PRIMARY KEY, value)";
  ASSERT_TRUE(db_->Execute(kCreateSql));
  ASSERT_EQ(1, SqliteSchemaCount(db_.get()));
  db_->Close();

  ASSERT_TRUE(OverwriteDatabaseHeader(OverwriteType::kOverwrite));

  // SQLite will successfully open the handle, but will fail with
  // SQLITE_NOTADB on pragma statemenets which attempt to read the
  // corrupted header.
  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_NOTADB);
    EXPECT_TRUE(db_->Open(db_path_));
    ASSERT_TRUE(expecter.SawExpectedErrors());
  }
  EXPECT_TRUE(db_->Raze());
  db_->Close();

  // Now empty, the open should succeed with an empty database.
  EXPECT_TRUE(db_->Open(db_path_));
  EXPECT_EQ(0, SqliteSchemaCount(db_.get()));
}

// Test that a callback from Open() can raze the database.  This is
// essential for cases where the Open() can fail entirely, so the
// Raze() cannot happen later.  Additionally test that when the
// callback does this during Open(), the open is retried and succeeds.
TEST_P(SQLDatabaseTest, RazeCallbackReopen) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER PRIMARY KEY, value)";
  ASSERT_TRUE(db_->Execute(kCreateSql));
  ASSERT_EQ(1, SqliteSchemaCount(db_.get()));
  db_->Close();

  // Corrupt the database so that nothing works, including PRAGMAs.
  ASSERT_TRUE(sql::test::CorruptSizeInHeader(db_path_));

  // Open() will succeed, even though the PRAGMA calls within will
  // fail with SQLITE_CORRUPT, as will this PRAGMA.
  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_CORRUPT);
    ASSERT_TRUE(db_->Open(db_path_));
    ASSERT_FALSE(db_->Execute("PRAGMA auto_vacuum"));
    db_->Close();
    ASSERT_TRUE(expecter.SawExpectedErrors());
  }

  db_->set_error_callback(
      base::BindRepeating(&RazeErrorCallback, db_.get(), SQLITE_CORRUPT));

  // When the PRAGMA calls in Open() raise SQLITE_CORRUPT, the error
  // callback will call RazeAndClose().  Open() will then fail and be
  // retried.  The second Open() on the empty database will succeed
  // cleanly.
  ASSERT_TRUE(db_->Open(db_path_));
  ASSERT_TRUE(db_->Execute("PRAGMA auto_vacuum"));
  EXPECT_EQ(0, SqliteSchemaCount(db_.get()));
}

// Basic test of RazeAndClose() operation.
TEST_P(SQLDatabaseTest, RazeAndClose) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER PRIMARY KEY, value)";
  const char* kPopulateSql = "INSERT INTO foo (value) VALUES (12)";

  // Test that RazeAndClose() closes the database, and that the
  // database is empty when re-opened.
  ASSERT_TRUE(db_->Execute(kCreateSql));
  ASSERT_TRUE(db_->Execute(kPopulateSql));
  ASSERT_TRUE(db_->RazeAndClose());
  ASSERT_FALSE(db_->is_open());
  db_->Close();
  ASSERT_TRUE(db_->Open(db_path_));
  ASSERT_EQ(0, SqliteSchemaCount(db_.get()));

  // Test that RazeAndClose() can break transactions.
  ASSERT_TRUE(db_->Execute(kCreateSql));
  ASSERT_TRUE(db_->Execute(kPopulateSql));
  ASSERT_TRUE(db_->BeginTransaction());
  ASSERT_TRUE(db_->RazeAndClose());
  ASSERT_FALSE(db_->is_open());
  ASSERT_FALSE(db_->CommitTransaction());
  db_->Close();
  ASSERT_TRUE(db_->Open(db_path_));
  ASSERT_EQ(0, SqliteSchemaCount(db_.get()));
}

// Test that various operations fail without crashing after
// RazeAndClose().
TEST_P(SQLDatabaseTest, RazeAndCloseDiagnostics) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER PRIMARY KEY, value)";
  const char* kPopulateSql = "INSERT INTO foo (value) VALUES (12)";
  const char* kSimpleSql = "SELECT 1";

  ASSERT_TRUE(db_->Execute(kCreateSql));
  ASSERT_TRUE(db_->Execute(kPopulateSql));

  // Test baseline expectations.
  db_->Preload();
  ASSERT_TRUE(db_->DoesTableExist("foo"));
  ASSERT_TRUE(db_->IsSQLValid(kSimpleSql));
  ASSERT_TRUE(db_->Execute(kSimpleSql));
  ASSERT_TRUE(db_->is_open());
  {
    Statement s(db_->GetUniqueStatement(kSimpleSql));
    ASSERT_TRUE(s.Step());
  }
  {
    Statement s(db_->GetCachedStatement(SQL_FROM_HERE, kSimpleSql));
    ASSERT_TRUE(s.Step());
  }
  ASSERT_TRUE(db_->BeginTransaction());
  ASSERT_TRUE(db_->CommitTransaction());
  ASSERT_TRUE(db_->BeginTransaction());
  db_->RollbackTransaction();

  ASSERT_TRUE(db_->RazeAndClose());

  // At this point, they should all fail, but not crash.
  db_->Preload();
  ASSERT_FALSE(db_->DoesTableExist("foo"));
  ASSERT_FALSE(db_->IsSQLValid(kSimpleSql));
  ASSERT_FALSE(db_->Execute(kSimpleSql));
  ASSERT_FALSE(db_->is_open());
  {
    Statement s(db_->GetUniqueStatement(kSimpleSql));
    ASSERT_FALSE(s.Step());
  }
  {
    Statement s(db_->GetCachedStatement(SQL_FROM_HERE, kSimpleSql));
    ASSERT_FALSE(s.Step());
  }
  ASSERT_FALSE(db_->BeginTransaction());
  ASSERT_FALSE(db_->CommitTransaction());
  ASSERT_FALSE(db_->BeginTransaction());
  db_->RollbackTransaction();

  // Close normally to reset the poisoned flag.
  db_->Close();

// DEATH tests not supported on Android, iOS, or Fuchsia.
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS) && !BUILDFLAG(IS_FUCHSIA)
  // Once the real Close() has been called, various calls enforce API
  // usage by becoming fatal in debug mode.  Since DEATH tests are
  // expensive, just test one of them.
  if (DLOG_IS_ON(FATAL)) {
    ASSERT_DEATH({ db_->IsSQLValid(kSimpleSql); },
                 "Illegal use of Database without a db");
  }
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS) &&
        // !BUILDFLAG(IS_FUCHSIA)
}

// TODO(shess): Spin up a background thread to hold other_db, to more
// closely match real life.  That would also allow testing
// RazeWithTimeout().

// On Windows, truncate silently fails against a memory-mapped file.  One goal
// of Raze() is to truncate the file to remove blocks which generate I/O errors.
// Test that Raze() turns off memory mapping so that the file is truncated.
// [This would not cover the case of multiple connections where one of the other
// connections is memory-mapped.  That is infrequent in Chromium.]
TEST_P(SQLDatabaseTest, RazeTruncate) {
  // The empty database has 0 or 1 pages.  Raze() should leave it with exactly 1
  // page.  Not checking directly because auto_vacuum on Android adds a freelist
  // page.
  ASSERT_TRUE(db_->Raze());
  int64_t expected_size;
  ASSERT_TRUE(base::GetFileSize(db_path_, &expected_size));
  ASSERT_GT(expected_size, 0);

  // Cause the database to take a few pages.
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER PRIMARY KEY, value)";
  ASSERT_TRUE(db_->Execute(kCreateSql));
  for (size_t i = 0; i < 24; ++i) {
    ASSERT_TRUE(
        db_->Execute("INSERT INTO foo (value) VALUES (randomblob(1024))"));
  }

  // In WAL mode, writes don't reach the database file until a checkpoint
  // happens.
  ASSERT_TRUE(db_->CheckpointDatabase());

  int64_t db_size;
  ASSERT_TRUE(base::GetFileSize(db_path_, &db_size));
  ASSERT_GT(db_size, expected_size);

  // Make a query covering most of the database file to make sure that the
  // blocks are actually mapped into memory.  Empirically, the truncate problem
  // doesn't seem to happen if no blocks are mapped.
  EXPECT_EQ("24576",
            ExecuteWithResult(db_.get(), "SELECT SUM(LENGTH(value)) FROM foo"));

  ASSERT_TRUE(db_->Raze());
  ASSERT_TRUE(base::GetFileSize(db_path_, &db_size));
  ASSERT_EQ(expected_size, db_size);
}

#if BUILDFLAG(IS_ANDROID)
TEST_P(SQLDatabaseTest, SetTempDirForSQL) {
  MetaTable meta_table;
  // Below call needs a temporary directory in sqlite3
  // On Android, it can pass only when the temporary directory is set.
  // Otherwise, sqlite3 doesn't find the correct directory to store
  // temporary files and will report the error 'unable to open
  // database file'.
  ASSERT_TRUE(meta_table.Init(db_.get(), 4, 4));
}
#endif  // BUILDFLAG(IS_ANDROID)

TEST_P(SQLDatabaseTest, Delete) {
  EXPECT_TRUE(db_->Execute("CREATE TABLE x (x)"));
  db_->Close();

  base::FilePath journal_path = Database::JournalPath(db_path_);
  base::FilePath wal_path = Database::WriteAheadLogPath(db_path_);

  // Should have both a main database file and a journal file if
  // journal_mode is TRUNCATE. There is no WAL file as it is deleted on Close.
  ASSERT_TRUE(base::PathExists(db_path_));
  if (!IsWALEnabled()) {  // TRUNCATE mode
    ASSERT_TRUE(base::PathExists(journal_path));
  }

  Database::Delete(db_path_);
  EXPECT_FALSE(base::PathExists(db_path_));
  EXPECT_FALSE(base::PathExists(journal_path));
  EXPECT_FALSE(base::PathExists(wal_path));
}

#if BUILDFLAG(IS_POSIX)  // This test operates on POSIX file permissions.
TEST_P(SQLDatabaseTest, PosixFilePermissions) {
  db_->Close();
  Database::Delete(db_path_);
  ASSERT_FALSE(base::PathExists(db_path_));

  // If the bots all had a restrictive umask setting such that databases are
  // always created with only the owner able to read them, then the code could
  // break without breaking the tests. Temporarily provide a more permissive
  // umask.
  ScopedUmaskSetter permissive_umask(S_IWGRP | S_IWOTH);

  ASSERT_TRUE(db_->Open(db_path_));

  // Cause the journal file to be created. If the default journal_mode is
  // changed back to DELETE, this test will need to be updated.
  EXPECT_TRUE(db_->Execute("CREATE TABLE x (x)"));

  int mode;
  ASSERT_TRUE(base::PathExists(db_path_));
  EXPECT_TRUE(base::GetPosixFilePermissions(db_path_, &mode));
  ASSERT_EQ(mode, 0600);

  if (IsWALEnabled()) {  // WAL mode
    // The WAL file is created lazily on first change.
    ASSERT_TRUE(db_->Execute("CREATE TABLE foo (a, b)"));

    base::FilePath wal_path = Database::WriteAheadLogPath(db_path_);
    ASSERT_TRUE(base::PathExists(wal_path));
    EXPECT_TRUE(base::GetPosixFilePermissions(wal_path, &mode));
    ASSERT_EQ(mode, 0600);

    // The shm file doesn't exist in exclusive locking mode.
    if (ExecuteWithResult(db_.get(), "PRAGMA locking_mode") == "normal") {
      base::FilePath shm_path = Database::SharedMemoryFilePath(db_path_);
      ASSERT_TRUE(base::PathExists(shm_path));
      EXPECT_TRUE(base::GetPosixFilePermissions(shm_path, &mode));
      ASSERT_EQ(mode, 0600);
    }
  } else {  // Truncate mode
    base::FilePath journal_path = Database::JournalPath(db_path_);
    DLOG(ERROR) << "journal_path: " << journal_path;
    ASSERT_TRUE(base::PathExists(journal_path));
    EXPECT_TRUE(base::GetPosixFilePermissions(journal_path, &mode));
    ASSERT_EQ(mode, 0600);
  }
}
#endif  // BUILDFLAG(IS_POSIX)

// Test that errors start happening once Poison() is called.
TEST_P(SQLDatabaseTest, Poison) {
  EXPECT_TRUE(db_->Execute("CREATE TABLE x (x)"));

  // Before the Poison() call, things generally work.
  EXPECT_TRUE(db_->IsSQLValid("INSERT INTO x VALUES ('x')"));
  EXPECT_TRUE(db_->Execute("INSERT INTO x VALUES ('x')"));
  {
    Statement s(db_->GetUniqueStatement("SELECT COUNT(*) FROM x"));
    ASSERT_TRUE(s.is_valid());
    ASSERT_TRUE(s.Step());
  }

  // Get a statement which is valid before and will exist across Poison().
  Statement valid_statement(
      db_->GetUniqueStatement("SELECT COUNT(*) FROM sqlite_schema"));
  ASSERT_TRUE(valid_statement.is_valid());
  ASSERT_TRUE(valid_statement.Step());
  valid_statement.Reset(true);

  db_->Poison();

  // After the Poison() call, things fail.
  EXPECT_FALSE(db_->IsSQLValid("INSERT INTO x VALUES ('x')"));
  EXPECT_FALSE(db_->Execute("INSERT INTO x VALUES ('x')"));
  {
    Statement s(db_->GetUniqueStatement("SELECT COUNT(*) FROM x"));
    ASSERT_FALSE(s.is_valid());
    ASSERT_FALSE(s.Step());
  }

  // The existing statement has become invalid.
  ASSERT_FALSE(valid_statement.is_valid());
  ASSERT_FALSE(valid_statement.Step());

  // Test that poisoning the database during a transaction works (with errors).
  // RazeErrorCallback() poisons the database, the extra COMMIT causes
  // CommitTransaction() to throw an error while committing.
  db_->set_error_callback(
      base::BindRepeating(&RazeErrorCallback, db_.get(), SQLITE_ERROR));
  db_->Close();
  ASSERT_TRUE(db_->Open(db_path_));
  EXPECT_TRUE(db_->BeginTransaction());
  EXPECT_TRUE(db_->Execute("INSERT INTO x VALUES ('x')"));
  EXPECT_TRUE(db_->Execute("COMMIT"));
  EXPECT_FALSE(db_->CommitTransaction());
}

TEST_P(SQLDatabaseTest, AttachDatabase) {
  EXPECT_TRUE(db_->Execute("CREATE TABLE foo (a, b)"));

  // Create a database to attach to.
  base::FilePath attach_path =
      db_path_.DirName().AppendASCII("SQLDatabaseAttach.db");
  static const char kAttachmentPoint[] = "other";
  {
    Database other_db;
    ASSERT_TRUE(other_db.Open(attach_path));
    EXPECT_TRUE(other_db.Execute("CREATE TABLE bar (a, b)"));
    EXPECT_TRUE(other_db.Execute("INSERT INTO bar VALUES ('hello', 'world')"));
  }

  // Cannot see the attached database, yet.
  EXPECT_FALSE(db_->IsSQLValid("SELECT count(*) from other.bar"));

  EXPECT_TRUE(DatabaseTestPeer::AttachDatabase(db_.get(), attach_path,
                                               kAttachmentPoint));
  EXPECT_TRUE(db_->IsSQLValid("SELECT count(*) from other.bar"));

  // Queries can touch both databases after the ATTACH.
  EXPECT_TRUE(db_->Execute("INSERT INTO foo SELECT a, b FROM other.bar"));
  {
    Statement s(db_->GetUniqueStatement("SELECT COUNT(*) FROM foo"));
    ASSERT_TRUE(s.Step());
    EXPECT_EQ(1, s.ColumnInt(0));
  }

  EXPECT_TRUE(DatabaseTestPeer::DetachDatabase(db_.get(), kAttachmentPoint));
  EXPECT_FALSE(db_->IsSQLValid("SELECT count(*) from other.bar"));
}

TEST_P(SQLDatabaseTest, AttachDatabaseWithOpenTransaction) {
  EXPECT_TRUE(db_->Execute("CREATE TABLE foo (a, b)"));

  // Create a database to attach to.
  base::FilePath attach_path =
      db_path_.DirName().AppendASCII("SQLDatabaseAttach.db");
  static const char kAttachmentPoint[] = "other";
  {
    Database other_db;
    ASSERT_TRUE(other_db.Open(attach_path));
    EXPECT_TRUE(other_db.Execute("CREATE TABLE bar (a, b)"));
    EXPECT_TRUE(other_db.Execute("INSERT INTO bar VALUES ('hello', 'world')"));
  }

  // Cannot see the attached database, yet.
  EXPECT_FALSE(db_->IsSQLValid("SELECT count(*) from other.bar"));

  // Attach succeeds in a transaction.
  EXPECT_TRUE(db_->BeginTransaction());
  EXPECT_TRUE(DatabaseTestPeer::AttachDatabase(db_.get(), attach_path,
                                               kAttachmentPoint));
  EXPECT_TRUE(db_->IsSQLValid("SELECT count(*) from other.bar"));

  // Queries can touch both databases after the ATTACH.
  EXPECT_TRUE(db_->Execute("INSERT INTO foo SELECT a, b FROM other.bar"));
  {
    Statement s(db_->GetUniqueStatement("SELECT COUNT(*) FROM foo"));
    ASSERT_TRUE(s.Step());
    EXPECT_EQ(1, s.ColumnInt(0));
  }

  // Detaching the same database fails, database is locked in the transaction.
  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_ERROR);
    EXPECT_FALSE(DatabaseTestPeer::DetachDatabase(db_.get(), kAttachmentPoint));
    EXPECT_TRUE(db_->IsSQLValid("SELECT count(*) from other.bar"));
    ASSERT_TRUE(expecter.SawExpectedErrors());
  }

  // Detach succeeds when the transaction is closed.
  db_->RollbackTransaction();
  EXPECT_TRUE(DatabaseTestPeer::DetachDatabase(db_.get(), kAttachmentPoint));
  EXPECT_FALSE(db_->IsSQLValid("SELECT count(*) from other.bar"));
}

TEST_P(SQLDatabaseTest, Basic_QuickIntegrityCheck) {
  const char* kCreateSql = "CREATE TABLE foo (id INTEGER PRIMARY KEY, value)";
  ASSERT_TRUE(db_->Execute(kCreateSql));
  EXPECT_TRUE(db_->QuickIntegrityCheck());
  db_->Close();

  ASSERT_TRUE(sql::test::CorruptSizeInHeader(db_path_));

  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_CORRUPT);
    ASSERT_TRUE(db_->Open(db_path_));
    EXPECT_FALSE(db_->QuickIntegrityCheck());
    ASSERT_TRUE(expecter.SawExpectedErrors());
  }
}

TEST_P(SQLDatabaseTest, Basic_FullIntegrityCheck) {
  const std::string kOk("ok");
  std::vector<std::string> messages;

  const char* kCreateSql = "CREATE TABLE foo (id INTEGER PRIMARY KEY, value)";
  ASSERT_TRUE(db_->Execute(kCreateSql));
  EXPECT_TRUE(db_->FullIntegrityCheck(&messages));
  EXPECT_EQ(1u, messages.size());
  EXPECT_EQ(kOk, messages[0]);
  db_->Close();

  ASSERT_TRUE(sql::test::CorruptSizeInHeader(db_path_));

  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_CORRUPT);
    ASSERT_TRUE(db_->Open(db_path_));
    EXPECT_TRUE(db_->FullIntegrityCheck(&messages));
    EXPECT_LT(1u, messages.size());
    EXPECT_NE(kOk, messages[0]);
    ASSERT_TRUE(expecter.SawExpectedErrors());
  }

  // TODO(shess): CorruptTableOrIndex could be used to produce a
  // file that would pass the quick check and fail the full check.
}

TEST_P(SQLDatabaseTest, OnMemoryDump) {
  base::trace_event::MemoryDumpArgs args = {
      base::trace_event::MemoryDumpLevelOfDetail::DETAILED};
  base::trace_event::ProcessMemoryDump pmd(args);
  ASSERT_TRUE(db_->memory_dump_provider_->OnMemoryDump(args, &pmd));
  EXPECT_GE(pmd.allocator_dumps().size(), 1u);
}

// Test that the functions to collect diagnostic data run to completion, without
// worrying too much about what they generate (since that will change).
TEST_P(SQLDatabaseTest, CollectDiagnosticInfo) {
  const std::string corruption_info = db_->CollectCorruptionInfo();
  EXPECT_NE(std::string::npos, corruption_info.find("SQLITE_CORRUPT"));
  EXPECT_NE(std::string::npos, corruption_info.find("integrity_check"));

  // A statement to see in the results.
  const char* kSimpleSql = "SELECT 'mountain'";
  Statement s(db_->GetCachedStatement(SQL_FROM_HERE, kSimpleSql));

  // Error includes the statement.
  const std::string readonly_info = db_->CollectErrorInfo(SQLITE_READONLY, &s);
  EXPECT_NE(std::string::npos, readonly_info.find(kSimpleSql));

  // Some other error doesn't include the statment.
  // TODO(shess): This is weak.
  const std::string full_info = db_->CollectErrorInfo(SQLITE_FULL, nullptr);
  EXPECT_EQ(std::string::npos, full_info.find(kSimpleSql));

  // A table to see in the SQLITE_ERROR results.
  EXPECT_TRUE(db_->Execute("CREATE TABLE volcano (x)"));

  // Version info to see in the SQLITE_ERROR results.
  MetaTable meta_table;
  ASSERT_TRUE(meta_table.Init(db_.get(), 4, 4));

  const std::string error_info = db_->CollectErrorInfo(SQLITE_ERROR, &s);
  EXPECT_NE(std::string::npos, error_info.find(kSimpleSql));
  EXPECT_NE(std::string::npos, error_info.find("volcano"));
  EXPECT_NE(std::string::npos, error_info.find("version: 4"));
}

// Test that a fresh database has mmap enabled by default, if mmap'ed I/O is
// enabled by SQLite.
TEST_P(SQLDatabaseTest, MmapInitiallyEnabled) {
  {
    Statement s(db_->GetUniqueStatement("PRAGMA mmap_size"));
    ASSERT_TRUE(s.Step())
        << "All supported SQLite versions should have mmap support";

    // If mmap I/O is not on, attempt to turn it on.  If that succeeds, then
    // Open() should have turned it on.  If mmap support is disabled, 0 is
    // returned.  If the VFS does not understand SQLITE_FCNTL_MMAP_SIZE (for
    // instance MojoVFS), -1 is returned.
    if (s.ColumnInt(0) <= 0) {
      ASSERT_TRUE(db_->Execute("PRAGMA mmap_size = 1048576"));
      s.Reset(true);
      ASSERT_TRUE(s.Step());
      EXPECT_LE(s.ColumnInt(0), 0);
    }
  }

  // Test that explicit disable prevents mmap'ed I/O.
  db_->Close();
  Database::Delete(db_path_);
  db_->set_mmap_disabled();
  ASSERT_TRUE(db_->Open(db_path_));
  EXPECT_EQ("0", ExecuteWithResult(db_.get(), "PRAGMA mmap_size"));
}

// Test whether a fresh database gets mmap enabled when using alternate status
// storage.
TEST_P(SQLDatabaseTest, MmapInitiallyEnabledAltStatus) {
  // Re-open fresh database with alt-status flag set.
  db_->Close();
  Database::Delete(db_path_);

  DatabaseOptions options = GetDBOptions();
  options.mmap_alt_status_discouraged = true;
  options.enable_views_discouraged = true;
  db_ = std::make_unique<Database>(options);
  ASSERT_TRUE(db_->Open(db_path_));

  {
    Statement s(db_->GetUniqueStatement("PRAGMA mmap_size"));
    ASSERT_TRUE(s.Step())
        << "All supported SQLite versions should have mmap support";

    // If mmap I/O is not on, attempt to turn it on.  If that succeeds, then
    // Open() should have turned it on.  If mmap support is disabled, 0 is
    // returned.  If the VFS does not understand SQLITE_FCNTL_MMAP_SIZE (for
    // instance MojoVFS), -1 is returned.
    if (s.ColumnInt(0) <= 0) {
      ASSERT_TRUE(db_->Execute("PRAGMA mmap_size = 1048576"));
      s.Reset(true);
      ASSERT_TRUE(s.Step());
      EXPECT_LE(s.ColumnInt(0), 0);
    }
  }

  // Test that explicit disable overrides set_mmap_alt_status().
  db_->Close();
  Database::Delete(db_path_);
  db_->set_mmap_disabled();
  ASSERT_TRUE(db_->Open(db_path_));
  EXPECT_EQ("0", ExecuteWithResult(db_.get(), "PRAGMA mmap_size"));
}

TEST_P(SQLDatabaseTest, GetAppropriateMmapSize) {
  const size_t kMmapAlot = 25 * 1024 * 1024;
  int64_t mmap_status = MetaTable::kMmapFailure;

  // If there is no meta table (as for a fresh database), assume that everything
  // should be mapped, and the status of the meta table is not affected.
  ASSERT_TRUE(!db_->DoesTableExist("meta"));
  ASSERT_GT(db_->GetAppropriateMmapSize(), kMmapAlot);
  ASSERT_TRUE(!db_->DoesTableExist("meta"));

  // When the meta table is first created, it sets up to map everything.
  MetaTable().Init(db_.get(), 1, 1);
  ASSERT_TRUE(db_->DoesTableExist("meta"));
  ASSERT_GT(db_->GetAppropriateMmapSize(), kMmapAlot);
  ASSERT_TRUE(MetaTable::GetMmapStatus(db_.get(), &mmap_status));
  ASSERT_EQ(MetaTable::kMmapSuccess, mmap_status);

  // Preload with partial progress of one page.  Should map everything.
  ASSERT_TRUE(db_->Execute("REPLACE INTO meta VALUES ('mmap_status', 1)"));
  ASSERT_GT(db_->GetAppropriateMmapSize(), kMmapAlot);
  ASSERT_TRUE(MetaTable::GetMmapStatus(db_.get(), &mmap_status));
  ASSERT_EQ(MetaTable::kMmapSuccess, mmap_status);

  // Failure status maps nothing.
  ASSERT_TRUE(db_->Execute("REPLACE INTO meta VALUES ('mmap_status', -2)"));
  ASSERT_EQ(0UL, db_->GetAppropriateMmapSize());

  // Re-initializing the meta table does not re-create the key if the table
  // already exists.
  ASSERT_TRUE(db_->Execute("DELETE FROM meta WHERE key = 'mmap_status'"));
  MetaTable().Init(db_.get(), 1, 1);
  ASSERT_EQ(MetaTable::kMmapSuccess, mmap_status);
  ASSERT_TRUE(MetaTable::GetMmapStatus(db_.get(), &mmap_status));
  ASSERT_EQ(0, mmap_status);

  // With no key, map everything and create the key.
  // TODO(shess): This really should be "maps everything after validating it",
  // but that is more complicated to structure.
  ASSERT_GT(db_->GetAppropriateMmapSize(), kMmapAlot);
  ASSERT_TRUE(MetaTable::GetMmapStatus(db_.get(), &mmap_status));
  ASSERT_EQ(MetaTable::kMmapSuccess, mmap_status);
}

TEST_P(SQLDatabaseTest, GetAppropriateMmapSizeAltStatus) {
  const size_t kMmapAlot = 25 * 1024 * 1024;

  // At this point, Database still expects a future [meta] table.
  ASSERT_FALSE(db_->DoesTableExist("meta"));
  ASSERT_FALSE(db_->DoesViewExist("MmapStatus"));
  ASSERT_GT(db_->GetAppropriateMmapSize(), kMmapAlot);
  ASSERT_FALSE(db_->DoesTableExist("meta"));
  ASSERT_FALSE(db_->DoesViewExist("MmapStatus"));

  // Using alt status, everything should be mapped, with state in the view.
  DatabaseOptions options = GetDBOptions();
  options.mmap_alt_status_discouraged = true;
  options.enable_views_discouraged = true;
  db_ = std::make_unique<Database>(options);
  ASSERT_TRUE(db_->Open(db_path_));

  ASSERT_GT(db_->GetAppropriateMmapSize(), kMmapAlot);
  ASSERT_FALSE(db_->DoesTableExist("meta"));
  ASSERT_TRUE(db_->DoesViewExist("MmapStatus"));
  EXPECT_EQ(base::NumberToString(MetaTable::kMmapSuccess),
            ExecuteWithResult(db_.get(), "SELECT * FROM MmapStatus"));

  // Also maps everything when kMmapSuccess is already in the view.
  ASSERT_GT(db_->GetAppropriateMmapSize(), kMmapAlot);

  // Preload with partial progress of one page.  Should map everything.
  ASSERT_TRUE(db_->Execute("DROP VIEW MmapStatus"));
  ASSERT_TRUE(db_->Execute("CREATE VIEW MmapStatus (value) AS SELECT 1"));
  ASSERT_GT(db_->GetAppropriateMmapSize(), kMmapAlot);
  EXPECT_EQ(base::NumberToString(MetaTable::kMmapSuccess),
            ExecuteWithResult(db_.get(), "SELECT * FROM MmapStatus"));

  // Failure status leads to nothing being mapped.
  ASSERT_TRUE(db_->Execute("DROP VIEW MmapStatus"));
  ASSERT_TRUE(db_->Execute("CREATE VIEW MmapStatus (value) AS SELECT -2"));
  ASSERT_EQ(0UL, db_->GetAppropriateMmapSize());
  EXPECT_EQ(base::NumberToString(MetaTable::kMmapFailure),
            ExecuteWithResult(db_.get(), "SELECT * FROM MmapStatus"));
}

TEST_P(SQLDatabaseTest, GetMemoryUsage) {
  // Databases with mmap enabled may not follow the assumptions below.
  db_->Close();
  db_->set_mmap_disabled();
  ASSERT_TRUE(db_->Open(db_path_));

  int initial_memory = db_->GetMemoryUsage();
  EXPECT_GT(initial_memory, 0)
      << "SQLite should always use some memory for a database";

  ASSERT_TRUE(db_->Execute("CREATE TABLE foo (a, b)"));
  ASSERT_TRUE(db_->Execute("INSERT INTO foo(a, b) VALUES (12, 13)"));

  int post_query_memory = db_->GetMemoryUsage();
  EXPECT_GT(post_query_memory, initial_memory)
      << "Page cache usage should go up after executing queries";

  db_->TrimMemory();
  int post_trim_memory = db_->GetMemoryUsage();
  EXPECT_GT(post_query_memory, post_trim_memory)
      << "Page cache usage should go down after calling TrimMemory()";
}

TEST_P(SQLDatabaseTest, DoubleQuotedStringLiteralsDisabledByDefault) {
  ASSERT_TRUE(db_->Execute("CREATE TABLE data(item TEXT NOT NULL);"));

  struct TestCase {
    const char* sql;
    bool is_valid;
  };
  std::vector<TestCase> test_cases = {
      // DML tests.
      {"SELECT item FROM data WHERE item >= 'string literal'", true},
      {"SELECT item FROM data WHERE item >= \"string literal\"", false},
      {"INSERT INTO data(item) VALUES('string literal')", true},
      {"INSERT INTO data(item) VALUES(\"string literal\")", false},
      {"UPDATE data SET item = 'string literal'", true},
      {"UPDATE data SET item = \"string literal\"", false},
      {"DELETE FROM data WHERE item >= 'string literal'", true},
      {"DELETE FROM data WHERE item >= \"string literal\"", false},

      // DDL tests.
      {"CREATE INDEX data_item ON data(item) WHERE item >= 'string literal'",
       true},
      {"CREATE INDEX data_item ON data(item) WHERE item >= \"string literal\"",
       false},
      {"CREATE TABLE data2(item TEXT DEFAULT 'string literal')", true},

      // This should be an invalid DDL statement, due to the double-quoted
      // string literal. However, SQLite currently parses it.
      {"CREATE TABLE data2(item TEXT DEFAULT \"string literal\")", true},
  };

  for (const TestCase& test_case : test_cases) {
    SCOPED_TRACE(test_case.sql);

    EXPECT_EQ(test_case.is_valid, db_->IsSQLValid(test_case.sql));
  }
}

TEST_P(SQLDatabaseTest, TriggersDisabledByDefault) {
  ASSERT_TRUE(db_->Execute("CREATE TABLE data(id INTEGER)"));

  // sqlite3_db_config() currently only disables running triggers. Schema
  // operations on triggers are still allowed.
  EXPECT_TRUE(
      db_->Execute("CREATE TRIGGER trigger AFTER INSERT ON data "
                   "BEGIN DELETE FROM data; END"));

  ASSERT_TRUE(db_->Execute("INSERT INTO data(id) VALUES(42)"));

  Statement select(db_->GetUniqueStatement("SELECT id FROM data"));
  EXPECT_TRUE(select.Step())
      << "If the trigger did not run, the table should not be empty.";
  EXPECT_EQ(42, select.ColumnInt64(0));

  // sqlite3_db_config() currently only disables running triggers. Schema
  // operations on triggers are still allowed.
  EXPECT_TRUE(db_->Execute("DROP TRIGGER IF EXISTS trigger"));
}

TEST_P(SQLDatabaseTest, ViewsDisabledByDefault) {
  EXPECT_FALSE(GetDBOptions().enable_views_discouraged);

  // sqlite3_db_config() currently only disables querying views. Schema
  // operations on views are still allowed.
  ASSERT_TRUE(db_->Execute("CREATE VIEW view(id) AS SELECT 1"));

  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_ERROR);
    Statement select_from_view(db_->GetUniqueStatement("SELECT id FROM view"));
    EXPECT_FALSE(select_from_view.is_valid());
    EXPECT_TRUE(expecter.SawExpectedErrors());
  }

  // sqlite3_db_config() currently only disables querying views. Schema
  // operations on views are still allowed.
  EXPECT_TRUE(db_->Execute("DROP VIEW IF EXISTS view"));
}

TEST_P(SQLDatabaseTest, ViewsEnabled) {
  DatabaseOptions options = GetDBOptions();
  options.enable_views_discouraged = true;
  db_ = std::make_unique<Database>(options);
  ASSERT_TRUE(db_->Open(db_path_));

  ASSERT_TRUE(db_->Execute("CREATE VIEW view(id) AS SELECT 1"));

  Statement select_from_view(db_->GetUniqueStatement("SELECT id FROM view"));
  ASSERT_TRUE(select_from_view.is_valid());
  EXPECT_TRUE(select_from_view.Step());
  EXPECT_EQ(1, select_from_view.ColumnInt64(0));

  EXPECT_TRUE(db_->Execute("DROP VIEW IF EXISTS view"));
}

TEST_P(SQLDatabaseTest, VirtualTablesDisabledByDefault) {
  EXPECT_FALSE(GetDBOptions().enable_virtual_tables_discouraged);

  // sqlite3_prepare_v3() currently only disables accessing virtual tables.
  // Schema operations on virtual tables are still allowed.
  ASSERT_TRUE(db_->Execute(
      "CREATE VIRTUAL TABLE fts_table USING fts3(data_table, content TEXT)"));

  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_ERROR);
    Statement select_from_vtable(db_->GetUniqueStatement(
        "SELECT content FROM fts_table WHERE content MATCH 'pattern'"));
    EXPECT_FALSE(select_from_vtable.is_valid());
    EXPECT_TRUE(expecter.SawExpectedErrors());
  }

  // sqlite3_prepare_v3() currently only disables accessing virtual tables.
  // Schema operations on virtual tables are still allowed.
  EXPECT_TRUE(db_->Execute("DROP TABLE IF EXISTS fts_table"));
}

TEST_P(SQLDatabaseTest, VirtualTablesEnabled) {
  DatabaseOptions options = GetDBOptions();
  options.enable_virtual_tables_discouraged = true;
  db_ = std::make_unique<Database>(options);
  ASSERT_TRUE(db_->Open(db_path_));

  ASSERT_TRUE(db_->Execute(
      "CREATE VIRTUAL TABLE fts_table USING fts3(data_table, content TEXT)"));

  Statement select_from_vtable(db_->GetUniqueStatement(
      "SELECT content FROM fts_table WHERE content MATCH 'pattern'"));
  ASSERT_TRUE(select_from_vtable.is_valid());
  EXPECT_FALSE(select_from_vtable.Step());

  EXPECT_TRUE(db_->Execute("DROP TABLE IF EXISTS fts_table"));
}

class SQLDatabaseTestExclusiveMode : public testing::Test,
                                     public testing::WithParamInterface<bool> {
 public:
  ~SQLDatabaseTestExclusiveMode() override = default;

  void SetUp() override {
    db_ = std::make_unique<Database>(GetDBOptions());
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    db_path_ = temp_dir_.GetPath().AppendASCII("recovery_test.sqlite");
    ASSERT_TRUE(db_->Open(db_path_));
  }

  DatabaseOptions GetDBOptions() {
    DatabaseOptions options;
    options.wal_mode = IsWALEnabled();
    options.exclusive_locking = true;
    return options;
  }

  bool IsWALEnabled() { return GetParam(); }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath db_path_;
  std::unique_ptr<Database> db_;
};

TEST_P(SQLDatabaseTestExclusiveMode, LockingModeExclusive) {
  EXPECT_EQ(ExecuteWithResult(db_.get(), "PRAGMA locking_mode"), "exclusive");
}

TEST_P(SQLDatabaseTest, LockingModeNormal) {
  EXPECT_EQ(ExecuteWithResult(db_.get(), "PRAGMA locking_mode"), "normal");
}

TEST_P(SQLDatabaseTest, OpenedInCorrectMode) {
  std::string expected_mode = IsWALEnabled() ? "wal" : "truncate";
  EXPECT_EQ(ExecuteWithResult(db_.get(), "PRAGMA journal_mode"), expected_mode);
}

TEST_P(SQLDatabaseTest, CheckpointDatabase) {
  if (!IsWALEnabled())
    return;

  base::FilePath wal_path = Database::WriteAheadLogPath(db_path_);

  int64_t wal_size = 0;
  // WAL file initially empty.
  EXPECT_TRUE(base::PathExists(wal_path));
  base::GetFileSize(wal_path, &wal_size);
  EXPECT_EQ(wal_size, 0);

  ASSERT_TRUE(
      db_->Execute("CREATE TABLE foo (id INTEGER UNIQUE, value INTEGER)"));
  ASSERT_TRUE(db_->Execute("INSERT INTO foo VALUES (1, 1)"));
  ASSERT_TRUE(db_->Execute("INSERT INTO foo VALUES (2, 2)"));

  // Writes reach WAL file but not db file.
  base::GetFileSize(wal_path, &wal_size);
  EXPECT_GT(wal_size, 0);

  int64_t db_size = 0;
  base::GetFileSize(db_path_, &db_size);
  EXPECT_EQ(db_size, db_->page_size());

  // Checkpoint database to immediately propagate writes to DB file.
  EXPECT_TRUE(db_->CheckpointDatabase());

  base::GetFileSize(db_path_, &db_size);
  EXPECT_GT(db_size, db_->page_size());
  EXPECT_EQ(ExecuteWithResult(db_.get(), "SELECT value FROM foo where id=1"),
            "1");
  EXPECT_EQ(ExecuteWithResult(db_.get(), "SELECT value FROM foo where id=2"),
            "2");
}

TEST_P(SQLDatabaseTest, CorruptSizeInHeaderTest) {
  ASSERT_TRUE(db_->Execute("CREATE TABLE foo (x)"));
  ASSERT_TRUE(db_->Execute("CREATE TABLE bar (x)"));
  db_->Close();

  ASSERT_TRUE(sql::test::CorruptSizeInHeader(db_path_));
  {
    sql::test::ScopedErrorExpecter expecter;
    expecter.ExpectError(SQLITE_CORRUPT);
    ASSERT_TRUE(db_->Open(db_path_));
    EXPECT_FALSE(db_->Execute("INSERT INTO foo values (1)"));
    EXPECT_FALSE(db_->DoesTableExist("foo"));
    EXPECT_FALSE(db_->DoesTableExist("bar"));
    EXPECT_FALSE(db_->Execute("SELECT * FROM foo"));
    EXPECT_TRUE(expecter.SawExpectedErrors());
  }
}

// WAL mode is currently not supported on Fuchsia.
#if !BUILDFLAG(IS_FUCHSIA)
INSTANTIATE_TEST_SUITE_P(JournalMode, SQLDatabaseTest, testing::Bool());
INSTANTIATE_TEST_SUITE_P(JournalMode,
                         SQLDatabaseTestExclusiveMode,
                         testing::Bool());
#else
INSTANTIATE_TEST_SUITE_P(JournalMode, SQLDatabaseTest, testing::Values(false));
INSTANTIATE_TEST_SUITE_P(JournalMode,
                         SQLDatabaseTestExclusiveMode,
                         testing::Values(false));
#endif

}  // namespace sql
