#include "domains/games/apis/golf_hub/pg_chat_store.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/apis/golf_hub/chat_store.h"
#include "domains/games/apis/golf_hub/migrations.h"
#include "domains/platform/libs/pg/listener.h"
#include "domains/platform/libs/pg/pg.h"
#include "gtest/gtest.h"

namespace {

using golf_hub::ChatRow;
using golf_hub::PgChatStore;

// Payload sink for the notify assertions, matching pg_hub_store_test:
// LISTEN lands asynchronously, so tests probe with a marker payload
// until the subscription is live; postgres keeps send order after that.
struct Received {
  std::mutex mu;
  std::condition_variable cv;
  std::vector<std::string> payloads;

  void Add(const std::string& payload) {
    const std::lock_guard<std::mutex> lock(mu);
    payloads.push_back(payload);
    cv.notify_all();
  }
  bool Saw(const std::string& want, std::chrono::seconds timeout = std::chrono::seconds(5)) {
    std::unique_lock<std::mutex> lock(mu);
    return cv.wait_for(lock, timeout, [&] {
      return std::find(payloads.begin(), payloads.end(), want) != payloads.end();
    });
  }
};

std::vector<std::string> Texts(const std::vector<ChatRow>& rows) {
  std::vector<std::string> texts;
  texts.reserve(rows.size());
  for (const ChatRow& row : rows) texts.push_back(row.text);
  return texts;
}

// The #1226 slice of the persistence integration suite: chat's SQL is
// the risky code, so there is no in-memory double here. Gated on
// GOLF_HUB_TEST_DB_URL like the rest of the suite.
class PgChatStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    url_ = std::getenv("GOLF_HUB_TEST_DB_URL");
    if (url_ == nullptr || *url_ == '\0') {
      GTEST_SKIP() << "GOLF_HUB_TEST_DB_URL unset";
    }
    db_ = std::make_shared<pg::Client>(url_);
    ASSERT_TRUE(golf_hub::RunMigrations(*db_).ok());
    ASSERT_TRUE(db_->Exec("TRUNCATE rooms CASCADE").ok());
    store_ = std::make_unique<PgChatStore>(db_);
    Join("R1", "alice");
    Join("R1", "bob");
    Join("R2", "bob");
  }

  void Join(const std::string& room_id, const std::string& player_id) {
    ASSERT_TRUE(
        db_->Exec("INSERT INTO rooms (room_id) VALUES ($1) ON CONFLICT DO NOTHING", {room_id})
            .ok());
    ASSERT_TRUE(db_->Exec("INSERT INTO room_members"
                          " (room_id, player_id, connected, games_played, games_won, total_score)"
                          " VALUES ($1, $2, true, 0, 0, 0) ON CONFLICT DO NOTHING",
                          {room_id, player_id})
                    .ok());
  }

  int CountRows(const std::string& room_id) {
    auto result = db_->Exec("SELECT 1 FROM room_chat_messages WHERE room_id = $1", {room_id});
    EXPECT_TRUE(result.ok()) << result.status();
    return result.ok() ? result->rows() : -1;
  }

  void ConfirmSubscribed(const std::string& channel, Received& received) {
    bool live = false;
    for (int i = 0; i < 50 && !live; ++i) {
      ASSERT_TRUE(db_->Exec("SELECT pg_notify($1, 'probe')", {channel}).ok());
      live = received.Saw("probe", std::chrono::seconds(1));
    }
    ASSERT_TRUE(live) << "LISTEN on " << channel << " never became live";
  }

  const char* url_ = nullptr;
  std::shared_ptr<pg::Client> db_;
  std::unique_ptr<PgChatStore> store_;
};

TEST_F(PgChatStoreTest, MigrationIsIdempotentAndDefinesTheContract) {
  // SetUp already migrated; a second pass must be a no-op, not an error.
  ASSERT_TRUE(golf_hub::RunMigrations(*db_).ok());

  auto cascade = db_->Exec(
      "SELECT confdeltype FROM pg_constraint"
      " WHERE conrelid = 'room_chat_messages'::regclass AND contype = 'f'");
  ASSERT_TRUE(cascade.ok()) << cascade.status();
  ASSERT_EQ(cascade->rows(), 1) << "chat must have exactly one foreign key, to rooms";
  EXPECT_EQ(cascade->Get(0, 0).value_or(""), "c") << "the rooms reference must ON DELETE CASCADE";

  auto checks = db_->Exec(
      "SELECT count(*) FROM pg_constraint"
      " WHERE conrelid = 'room_chat_messages'::regclass AND contype = 'c'");
  ASSERT_TRUE(checks.ok()) << checks.status();
  EXPECT_EQ(checks->Get(0, 0).value_or("0"), "1") << "the body length check must survive";

  auto index = db_->Exec(
      "SELECT count(*) FROM pg_indexes WHERE tablename = 'room_chat_messages'"
      " AND indexdef LIKE '%(room_id, message_id)'");
  ASSERT_TRUE(index.ok()) << index.status();
  EXPECT_EQ(index->Get(0, 0).value_or("0"), "1")
      << "cursor reads need the (room_id, message_id) index";
}

TEST_F(PgChatStoreTest, AppendCommitsARowAndNotifiesItsChatChannel) {
  Received received;
  pg::Listener listener(
      url_, [&](const std::string&, const std::string& payload) { received.Add(payload); });
  listener.Listen(golf_hub::ChatChannel("R1"));
  ConfirmSubscribed(golf_hub::ChatChannel("R1"), received);

  const auto appended = store_->Append("R1", "alice", "hello room", "instance-a");
  ASSERT_TRUE(appended.ok()) << appended.status();
  EXPECT_GT(appended->message_id, 0);
  EXPECT_GT(appended->sent_at_unix_millis, 0);
  EXPECT_EQ(appended->room_id, "R1");
  EXPECT_EQ(appended->player_id, "alice");
  EXPECT_EQ(appended->text, "hello room");
  EXPECT_TRUE(received.Saw("instance-a")) << "the append must notify its chat channel";

  // The returned row is the row a reader observes, id and clock included.
  const auto recent = store_->LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok()) << recent.status();
  ASSERT_EQ(recent->size(), 1u);
  EXPECT_EQ(recent->front().message_id, appended->message_id);
  EXPECT_EQ(recent->front().sent_at_unix_millis, appended->sent_at_unix_millis);
  EXPECT_EQ(recent->front().player_id, "alice");
  EXPECT_EQ(recent->front().text, "hello room");
}

TEST_F(PgChatStoreTest, RejectedAppendWritesNoRowAndNotifiesNobody) {
  Received received;
  pg::Listener listener(
      url_, [&](const std::string&, const std::string& payload) { received.Add(payload); });
  listener.Listen(golf_hub::ChatChannel("R1"));
  ConfirmSubscribed(golf_hub::ChatChannel("R1"), received);

  EXPECT_EQ(store_->Append("R1", "mallory", "let me in", "rejected").status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(store_->Append("R2", "alice", "wrong room", "rejected").status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(store_->Append("no-such-room", "alice", "nowhere", "rejected").status().code(),
            absl::StatusCode::kFailedPrecondition);
  // Invalid text never reaches the server at all.
  EXPECT_EQ(store_->Append("R1", "alice", "   ", "rejected").status().code(),
            absl::StatusCode::kInvalidArgument);

  EXPECT_EQ(CountRows("R1"), 0);
  EXPECT_EQ(CountRows("R2"), 0);

  // A later good append proves the channel was working the whole time,
  // so the absence of "rejected" is real rather than a missed listener.
  ASSERT_TRUE(store_->Append("R1", "alice", "for real", "accepted").ok());
  ASSERT_TRUE(received.Saw("accepted"));
  const std::lock_guard<std::mutex> lock(received.mu);
  EXPECT_EQ(std::find(received.payloads.begin(), received.payloads.end(), "rejected"),
            received.payloads.end());
}

TEST_F(PgChatStoreTest, PruningKeepsExactlyTheNewestHundred) {
  std::vector<int64_t> ids;
  for (int i = 1; i <= 105; ++i) {
    auto appended = store_->Append("R1", "alice", "message-" + std::to_string(i), "p");
    ASSERT_TRUE(appended.ok()) << appended.status();
    ids.push_back(appended->message_id);
  }

  EXPECT_EQ(CountRows("R1"), static_cast<int>(golf_hub::kChatHistoryLimit));
  const auto recent = store_->LoadRecent("R1", golf_hub::kChatHistoryLimit);
  ASSERT_TRUE(recent.ok()) << recent.status();
  ASSERT_EQ(recent->size(), golf_hub::kChatHistoryLimit);
  EXPECT_EQ(recent->front().text, "message-6");
  EXPECT_EQ(recent->back().text, "message-105");
  EXPECT_EQ(recent->front().message_id, ids[5]);
  EXPECT_EQ(recent->back().message_id, ids.back());
}

TEST_F(PgChatStoreTest, RejectedAppendDoesNotPruneExistingHistory) {
  for (int i = 1; i <= 105; ++i) {
    ASSERT_TRUE(store_->Append("R1", "alice", "message-" + std::to_string(i), "p").ok());
  }
  ASSERT_EQ(CountRows("R1"), static_cast<int>(golf_hub::kChatHistoryLimit));

  // The prune is a data-modifying CTE, and postgres runs those whether
  // or not anything reads them. Guard it wrong and a stranger's rejected
  // message silently evicts a real one, which no other test would show:
  // the row count only goes wrong when the room is already full.
  EXPECT_EQ(store_->Append("R1", "mallory", "not mine to send", "p").status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(CountRows("R1"), static_cast<int>(golf_hub::kChatHistoryLimit));

  const auto recent = store_->LoadRecent("R1", golf_hub::kChatHistoryLimit);
  ASSERT_TRUE(recent.ok()) << recent.status();
  EXPECT_EQ(recent->front().text, "message-6");
  EXPECT_EQ(recent->back().text, "message-105");
}

TEST_F(PgChatStoreTest, LeavingKeepsOldMessagesAndStopsNewOnes) {
  ASSERT_TRUE(store_->Append("R1", "alice", "before leaving", "p").ok());
  ASSERT_TRUE(
      db_->Exec("DELETE FROM room_members WHERE room_id = 'R1' AND player_id = 'alice'").ok());

  EXPECT_EQ(store_->Append("R1", "alice", "after leaving", "p").status().code(),
            absl::StatusCode::kFailedPrecondition);

  // There is deliberately no foreign key from chat to room_members: the
  // room outlives the membership, so the message does too.
  const auto recent = store_->LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok()) << recent.status();
  ASSERT_EQ(recent->size(), 1u);
  EXPECT_EQ(recent->front().text, "before leaving");
}

TEST_F(PgChatStoreTest, DeletingTheRoomCascadesItsChat) {
  ASSERT_TRUE(store_->Append("R1", "alice", "doomed", "p").ok());
  ASSERT_TRUE(store_->Append("R2", "bob", "survivor", "p").ok());

  ASSERT_TRUE(db_->Exec("DELETE FROM rooms WHERE room_id = 'R1'").ok());

  EXPECT_EQ(CountRows("R1"), 0);
  EXPECT_EQ(CountRows("R2"), 1);
  // DropRoom is a no-op here precisely because the cascade already ran.
  store_->DropRoom("R1");
  EXPECT_EQ(CountRows("R2"), 1);
}

TEST_F(PgChatStoreTest, ReadsAreAscendingAndCursorsPage) {
  std::vector<int64_t> ids;
  for (int i = 1; i <= 5; ++i) {
    auto appended = store_->Append("R1", "alice", std::to_string(i), "p");
    ASSERT_TRUE(appended.ok()) << appended.status();
    ids.push_back(appended->message_id);
  }
  ASSERT_TRUE(store_->Append("R2", "bob", "other room", "p").ok());

  const auto all = store_->LoadRecent("R1", 100);
  ASSERT_TRUE(all.ok()) << all.status();
  EXPECT_EQ(Texts(*all), (std::vector<std::string>{"1", "2", "3", "4", "5"}));

  // Bounded recent reads take the newest, still ascending.
  const auto newest_two = store_->LoadRecent("R1", 2);
  ASSERT_TRUE(newest_two.ok()) << newest_two.status();
  EXPECT_EQ(Texts(*newest_two), (std::vector<std::string>{"4", "5"}));

  // Cursor at zero, in the middle, at the newest, and past it.
  EXPECT_EQ(Texts(*store_->LoadAfter("R1", 0, 100)),
            (std::vector<std::string>{"1", "2", "3", "4", "5"}));
  EXPECT_EQ(Texts(*store_->LoadAfter("R1", ids[1], 100)),
            (std::vector<std::string>{"3", "4", "5"}));
  EXPECT_TRUE(store_->LoadAfter("R1", ids.back(), 100)->empty());
  EXPECT_TRUE(store_->LoadAfter("R1", ids.back() + 1000, 100)->empty());

  // Paging drains in order without gaps or repeats.
  std::vector<std::string> paged;
  int64_t cursor = 0;
  for (int page = 0; page < 10; ++page) {
    auto rows = store_->LoadAfter("R1", cursor, 2);
    ASSERT_TRUE(rows.ok()) << rows.status();
    if (rows->empty()) break;
    for (const ChatRow& row : *rows) paged.push_back(row.text);
    cursor = rows->back().message_id;
  }
  EXPECT_EQ(paged, (std::vector<std::string>{"1", "2", "3", "4", "5"}));

  // Unknown rooms and zero limits read empty, never an error.
  EXPECT_TRUE(store_->LoadRecent("no-such-room", 100)->empty());
  EXPECT_TRUE(store_->LoadAfter("no-such-room", 0, 100)->empty());
  EXPECT_TRUE(store_->LoadRecent("R1", 0)->empty());
  EXPECT_TRUE(store_->LoadAfter("R1", 0, 0)->empty());
}

TEST_F(PgChatStoreTest, StoresMetacharactersAndUnicodeAsData) {
  const std::vector<std::string> texts = {
      "'; DROP TABLE room_chat_messages; --",
      "100% \\backslash\\ and _underscore_",
      "caf\xC3\xA9 \xF0\x9F\x98\x80 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",
      "line\nbreak\ttab",
  };
  for (const std::string& text : texts) {
    ASSERT_TRUE(store_->Append("R1", "alice", text, "p").ok()) << text;
  }

  const auto recent = store_->LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok()) << recent.status();
  EXPECT_EQ(Texts(*recent), texts);
  // The table is still there, which is the point of the first one.
  EXPECT_EQ(CountRows("R1"), static_cast<int>(texts.size()));
}

TEST_F(PgChatStoreTest, ConcurrentWritersOnSeparateConnectionsRetainTheNewestHundred) {
  constexpr int kWriters = 3;
  constexpr int kPerWriter = 50;

  // Separate pg::Client instances mean separate connections, so these
  // appends contend for the room lock in the server, not in a mutex.
  std::vector<std::shared_ptr<pg::Client>> clients;
  std::vector<std::unique_ptr<PgChatStore>> stores;
  for (int i = 0; i < kWriters; ++i) {
    clients.push_back(std::make_shared<pg::Client>(url_));
    stores.push_back(std::make_unique<PgChatStore>(clients.back()));
  }

  std::mutex mu;
  std::vector<int64_t> committed;
  std::vector<std::thread> threads;
  threads.reserve(kWriters);
  for (int w = 0; w < kWriters; ++w) {
    threads.emplace_back([&, w] {
      for (int i = 0; i < kPerWriter; ++i) {
        auto appended = stores[w]->Append("R1", "bob", "w" + std::to_string(w), "p");
        ASSERT_TRUE(appended.ok()) << appended.status();
        const std::lock_guard<std::mutex> lock(mu);
        committed.push_back(appended->message_id);
      }
    });
  }
  for (std::thread& thread : threads) thread.join();

  ASSERT_EQ(committed.size(), static_cast<std::size_t>(kWriters * kPerWriter));
  EXPECT_EQ(CountRows("R1"), static_cast<int>(golf_hub::kChatHistoryLimit));

  // Retention is not best-effort: the survivors are exactly the newest
  // hundred ids the writers were handed, in order.
  std::sort(committed.begin(), committed.end());
  EXPECT_EQ(std::adjacent_find(committed.begin(), committed.end()), committed.end())
      << "identity must not hand two appends the same id";
  const std::vector<int64_t> newest(
      committed.end() - static_cast<std::ptrdiff_t>(golf_hub::kChatHistoryLimit), committed.end());

  const auto retained = store_->LoadRecent("R1", golf_hub::kChatHistoryLimit);
  ASSERT_TRUE(retained.ok()) << retained.status();
  std::vector<int64_t> retained_ids;
  for (const ChatRow& row : *retained) retained_ids.push_back(row.message_id);
  EXPECT_EQ(retained_ids, newest);
}

TEST_F(PgChatStoreTest, AppendRacingRoomDeletionRejectsWithoutOrphaning) {
  ASSERT_TRUE(store_->Append("R1", "alice", "before", "p").ok());

  // A dedicated Client can drive an explicit transaction because nothing
  // else shares its connection — the very assumption that makes this
  // unsafe to compose in production, and fine to rely on in a test.
  pg::Client blocker(url_);
  ASSERT_TRUE(blocker.Exec("BEGIN").ok());
  ASSERT_TRUE(blocker.Exec("DELETE FROM rooms WHERE room_id = 'R1'").ok());

  auto appender = std::make_shared<pg::Client>(url_);
  // Bounds the wait so a lost lock fails the test instead of hanging it.
  ASSERT_TRUE(appender->Exec("SET statement_timeout = '30s'").ok());
  PgChatStore racing(appender);

  absl::StatusOr<ChatRow> raced = absl::UnknownError("never ran");
  std::thread thread([&] { raced = racing.Append("R1", "alice", "during", "p"); });
  ASSERT_TRUE(blocker.Exec("COMMIT").ok());
  thread.join();

  // The room won, so the append must be a clean rejection rather than a
  // row pointing at a room that no longer exists.
  EXPECT_EQ(raced.status().code(), absl::StatusCode::kFailedPrecondition) << raced.status();
  auto orphans = db_->Exec(
      "SELECT 1 FROM room_chat_messages c"
      " WHERE NOT EXISTS (SELECT 1 FROM rooms r WHERE r.room_id = c.room_id)");
  ASSERT_TRUE(orphans.ok()) << orphans.status();
  EXPECT_EQ(orphans->rows(), 0);
  EXPECT_EQ(CountRows("R1"), 0) << "the cascade took the earlier message too";
}

}  // namespace
