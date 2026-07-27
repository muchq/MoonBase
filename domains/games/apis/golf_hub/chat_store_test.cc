#include "domains/games/apis/golf_hub/chat_store.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"

namespace golf_hub {
namespace {

// Stands in for the room state HubStore owns in production, so these
// tests can exercise the membership half of the contract without a
// database. PgChatStore reads the same answer out of room_members.
class Rooms {
 public:
  void Join(std::string room_id, std::string player_id) {
    const std::lock_guard<std::mutex> lock(mu_);
    members_.emplace(std::move(room_id), std::move(player_id));
  }
  void Leave(const std::string& room_id, const std::string& player_id) {
    const std::lock_guard<std::mutex> lock(mu_);
    members_.erase({room_id, player_id});
  }
  void Delete(const std::string& room_id) {
    const std::lock_guard<std::mutex> lock(mu_);
    std::erase_if(members_, [&](const auto& member) { return member.first == room_id; });
  }
  MemberGuard Guard() {
    return [this](const std::string& room_id, const std::string& player_id,
                  const MemberAction& action) {
      const std::lock_guard<std::mutex> lock(mu_);
      if (members_.count({room_id, player_id}) == 0) return false;
      {
        std::unique_lock<std::mutex> gate_lock(gate_mu_);
        if (pause_next_) {
          guard_entered_ = true;
          gate_cv_.notify_all();
          gate_cv_.wait(gate_lock, [this] { return release_guard_; });
          pause_next_ = false;
        }
      }
      action();
      return true;
    };
  }

  void PauseNextGuard() {
    const std::lock_guard<std::mutex> lock(gate_mu_);
    pause_next_ = true;
    guard_entered_ = false;
    release_guard_ = false;
  }
  bool WaitForGuard(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(gate_mu_);
    return gate_cv_.wait_for(lock, timeout, [this] { return guard_entered_; });
  }
  void ReleaseGuard() {
    {
      const std::lock_guard<std::mutex> lock(gate_mu_);
      release_guard_ = true;
    }
    gate_cv_.notify_all();
  }

 private:
  std::mutex mu_;
  std::set<std::pair<std::string, std::string>> members_;
  std::mutex gate_mu_;
  std::condition_variable gate_cv_;
  bool pause_next_ = false;
  bool guard_entered_ = false;
  bool release_guard_ = false;
};

class MemoryChatStoreTest : public ::testing::Test {
 protected:
  MemoryChatStoreTest() : store_(rooms_.Guard()) {
    rooms_.Join("R1", "alice");
    rooms_.Join("R1", "bob");
    rooms_.Join("R2", "bob");
  }

  Rooms rooms_;
  MemoryChatStore store_;
};

TEST_F(MemoryChatStoreTest, AppendsAndReadsRowsInMessageOrder) {
  auto first = store_.Append("R1", "alice", "one", "ignored");
  ASSERT_TRUE(first.ok());
  EXPECT_EQ(first->message_id, 1);
  EXPECT_EQ(first->room_id, "R1");
  EXPECT_EQ(first->player_id, "alice");
  EXPECT_EQ(first->text, "one");
  EXPECT_GT(first->sent_at_unix_millis, 0);

  auto second = store_.Append("R1", "bob", "two", "ignored");
  ASSERT_TRUE(second.ok());
  EXPECT_GT(second->message_id, first->message_id);

  auto recent = store_.LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok());
  ASSERT_EQ(recent->size(), 2u);
  EXPECT_EQ((*recent)[0].text, "one");
  EXPECT_EQ((*recent)[1].text, "two");

  auto after_first = store_.LoadAfter("R1", first->message_id, 100);
  ASSERT_TRUE(after_first.ok());
  ASSERT_EQ(after_first->size(), 1u);
  EXPECT_EQ(after_first->front().message_id, second->message_id);
}

TEST_F(MemoryChatStoreTest, RetainsLatestHundredAndDropsRoomHistory) {
  for (int i = 1; i <= 101; ++i) {
    auto appended = store_.Append("R1", "alice", "message-" + std::to_string(i), "ignored");
    ASSERT_TRUE(appended.ok());
  }

  auto recent = store_.LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok());
  ASSERT_EQ(recent->size(), 100u);
  EXPECT_EQ(recent->front().text, "message-2");
  EXPECT_EQ(recent->back().text, "message-101");

  store_.DropRoom("R1");
  recent = store_.LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok());
  EXPECT_TRUE(recent->empty());
}

TEST_F(MemoryChatStoreTest, LimitsRecentAndCursorReads) {
  for (int i = 1; i <= 5; ++i) {
    ASSERT_TRUE(store_.Append("R1", "alice", std::to_string(i), "ignored").ok());
  }

  auto recent = store_.LoadRecent("R1", 2);
  ASSERT_TRUE(recent.ok());
  ASSERT_EQ(recent->size(), 2u);
  EXPECT_EQ(recent->front().text, "4");
  EXPECT_EQ(recent->back().text, "5");

  auto after = store_.LoadAfter("R1", 1, 2);
  ASSERT_TRUE(after.ok());
  ASSERT_EQ(after->size(), 2u);
  EXPECT_EQ(after->front().text, "2");
  EXPECT_EQ(after->back().text, "3");

  const auto oversized = store_.LoadRecent("R1", 50);
  ASSERT_TRUE(oversized.ok());
  EXPECT_EQ(oversized->size(), 5u);

  const auto past_newest = store_.LoadAfter("R1", 5, 100);
  ASSERT_TRUE(past_newest.ok());
  EXPECT_TRUE(past_newest->empty());

  const auto no_recent = store_.LoadRecent("R1", 0);
  ASSERT_TRUE(no_recent.ok());
  EXPECT_TRUE(no_recent->empty());

  const auto no_after = store_.LoadAfter("R1", 0, 0);
  ASSERT_TRUE(no_after.ok());
  EXPECT_TRUE(no_after->empty());
}

TEST_F(MemoryChatStoreTest, KeepsRoomsIsolated) {
  const auto first = store_.Append("R1", "alice", "room one", "ignored");
  const auto second = store_.Append("R2", "bob", "room two", "ignored");
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  EXPECT_GT(second->message_id, first->message_id);

  const auto one = store_.LoadRecent("R1", 100);
  ASSERT_TRUE(one.ok());
  ASSERT_EQ(one->size(), 1u);
  EXPECT_EQ(one->front().text, "room one");

  // A room's cursor reads never see IDs the shared counter handed another room.
  const auto after_gap = store_.LoadAfter("R2", first->message_id, 100);
  ASSERT_TRUE(after_gap.ok());
  ASSERT_EQ(after_gap->size(), 1u);
  EXPECT_EQ(after_gap->front().text, "room two");

  store_.DropRoom("R1");
  const auto survivor = store_.LoadRecent("R2", 100);
  ASSERT_TRUE(survivor.ok());
  EXPECT_EQ(survivor->size(), 1u);

  store_.DropRoom("never-existed");
  const auto unknown = store_.LoadRecent("never-existed", 100);
  ASSERT_TRUE(unknown.ok());
  EXPECT_TRUE(unknown->empty());
}

TEST_F(MemoryChatStoreTest, RejectsAppendsFromOutsideTheRoom) {
  EXPECT_EQ(store_.Append("R1", "mallory", "let me in", "ignored").status().code(),
            absl::StatusCode::kFailedPrecondition);
  // A member of one room is not thereby a member of another.
  EXPECT_EQ(store_.Append("R2", "alice", "wrong room", "ignored").status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(store_.Append("no-such-room", "alice", "nowhere", "ignored").status().code(),
            absl::StatusCode::kFailedPrecondition);

  const auto recent = store_.LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok());
  EXPECT_TRUE(recent->empty());
}

TEST_F(MemoryChatStoreTest, LeavingKeepsOldMessagesAndStopsNewOnes) {
  ASSERT_TRUE(store_.Append("R1", "alice", "before leaving", "ignored").ok());
  rooms_.Leave("R1", "alice");

  EXPECT_EQ(store_.Append("R1", "alice", "after leaving", "ignored").status().code(),
            absl::StatusCode::kFailedPrecondition);

  // The room outlives the membership, so the message does too.
  const auto recent = store_.LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok());
  ASSERT_EQ(recent->size(), 1u);
  EXPECT_EQ(recent->front().text, "before leaving");

  // Deleting the room is what erases history; PgChatStore gets the same
  // effect from the room_chat_messages cascade.
  rooms_.Delete("R1");
  store_.DropRoom("R1");
  const auto erased = store_.LoadRecent("R1", 100);
  ASSERT_TRUE(erased.ok());
  EXPECT_TRUE(erased->empty());
}

TEST_F(MemoryChatStoreTest, AppendAndLeaveLinearizeThroughTheMembershipGuard) {
  rooms_.PauseNextGuard();
  absl::StatusOr<ChatRow> appended = absl::UnknownError("append did not run");
  std::thread appender([&] { appended = store_.Append("R1", "alice", "during leave", "ignored"); });
  const bool entered = rooms_.WaitForGuard(std::chrono::seconds(1));
  EXPECT_TRUE(entered);
  if (!entered) {
    rooms_.ReleaseGuard();
    appender.join();
    return;
  }

  std::promise<void> leave_started;
  std::promise<void> leave_finished;
  std::future<void> finished = leave_finished.get_future();
  std::thread leaver([&] {
    leave_started.set_value();
    rooms_.Leave("R1", "alice");
    leave_finished.set_value();
  });
  leave_started.get_future().wait();

  // The guard still owns the membership lock while it commits the chat
  // action, so leave cannot linearize between authorization and storage.
  EXPECT_EQ(finished.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
  rooms_.ReleaseGuard();
  appender.join();
  leaver.join();

  ASSERT_TRUE(appended.ok()) << appended.status();
  EXPECT_EQ(store_.Append("R1", "alice", "after leave", "ignored").status().code(),
            absl::StatusCode::kFailedPrecondition);
  const auto recent = store_.LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok());
  ASSERT_EQ(recent->size(), 1u);
  EXPECT_EQ(recent->front().text, "during leave");
}

TEST_F(MemoryChatStoreTest, RejectsTextARoomCannotStore) {
  const auto rejected = [this](const std::string& text) {
    return store_.Append("R1", "alice", text, "ignored").status().code();
  };
  EXPECT_EQ(rejected(""), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rejected(" \t\r\n"), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rejected(std::string("has a \0 in it", 13)), absl::StatusCode::kInvalidArgument);

  // Ill-formed UTF-8: a stray byte, a truncated sequence, a lone
  // continuation, an overlong '/', a surrogate half, and past U+10FFFF.
  EXPECT_EQ(rejected("\xFF"), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rejected("hi\xC3"), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rejected("\x80hi"), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rejected("\xC0\xAF"), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rejected("\xED\xA0\x80"), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rejected("\xF4\x90\x80\x80"), absl::StatusCode::kInvalidArgument);

  // Well-formed text is stored as-is, newlines and all.
  EXPECT_TRUE(store_.Append("R1", "alice", "line\nbreak", "ignored").ok());
  EXPECT_TRUE(store_.Append("R1", "alice", "caf\xC3\xA9 \xF0\x9F\x98\x80", "ignored").ok());
}

TEST_F(MemoryChatStoreTest, CountsBytesAndNeverSplitsACharacter) {
  const std::string grinning = "\xF0\x9F\x98\x80";  // U+1F600, four bytes.
  EXPECT_TRUE(store_.Append("R1", "alice", std::string(kChatTextByteLimit, 'x'), "ignored").ok());
  EXPECT_EQ(store_.Append("R1", "alice", std::string(kChatTextByteLimit + 1, 'x'), "ignored")
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);

  // Exactly at the limit with a multi-byte tail, then one character over
  // it: the character is refused whole rather than cut at 500 bytes.
  EXPECT_TRUE(
      store_.Append("R1", "alice", std::string(kChatTextByteLimit - 4, 'x') + grinning, "ignored")
          .ok());
  EXPECT_EQ(
      store_.Append("R1", "alice", std::string(kChatTextByteLimit - 3, 'x') + grinning, "ignored")
          .status()
          .code(),
      absl::StatusCode::kInvalidArgument);
}

TEST_F(MemoryChatStoreTest, ConcurrentAppendsGetUniqueAscendingIds) {
  constexpr int kThreads = 4;
  constexpr int kPerThread = 50;

  std::mutex mu;
  std::vector<int64_t> ids;
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([this, &mu, &ids, t] {
      const std::string room_id = t % 2 == 0 ? "R1" : "R2";
      for (int i = 0; i < kPerThread; ++i) {
        auto appended = store_.Append(room_id, "bob", std::to_string(i), "ignored");
        ASSERT_TRUE(appended.ok());
        const std::lock_guard<std::mutex> lock(mu);
        ids.push_back(appended->message_id);
      }
    });
  }
  for (std::thread& thread : threads) thread.join();

  ASSERT_EQ(ids.size(), static_cast<std::size_t>(kThreads * kPerThread));
  std::sort(ids.begin(), ids.end());
  EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());

  for (const char* room_id : {"R1", "R2"}) {
    const auto rows = store_.LoadRecent(room_id, kChatHistoryLimit);
    ASSERT_TRUE(rows.ok());
    EXPECT_EQ(rows->size(), kChatHistoryLimit);
    EXPECT_TRUE(std::is_sorted(
        rows->begin(), rows->end(),
        [](const ChatRow& lhs, const ChatRow& rhs) { return lhs.message_id < rhs.message_id; }));
  }
}

}  // namespace
}  // namespace golf_hub
