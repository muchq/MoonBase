#include "domains/games/apis/golf_hub/chat_store.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"

namespace golf_hub {
namespace {

TEST(MemoryChatStoreTest, AppendsAndReadsRowsInMessageOrder) {
  MemoryChatStore store;

  auto first = store.Append("R1", "alice", "one", "ignored");
  ASSERT_TRUE(first.ok());
  EXPECT_EQ(first->message_id, 1);
  EXPECT_EQ(first->room_id, "R1");
  EXPECT_EQ(first->player_id, "alice");
  EXPECT_EQ(first->text, "one");
  EXPECT_GT(first->sent_at_unix_millis, 0);

  auto second = store.Append("R1", "bob", "two", "ignored");
  ASSERT_TRUE(second.ok());
  EXPECT_GT(second->message_id, first->message_id);

  auto recent = store.LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok());
  ASSERT_EQ(recent->size(), 2u);
  EXPECT_EQ((*recent)[0].text, "one");
  EXPECT_EQ((*recent)[1].text, "two");

  auto after_first = store.LoadAfter("R1", first->message_id, 100);
  ASSERT_TRUE(after_first.ok());
  ASSERT_EQ(after_first->size(), 1u);
  EXPECT_EQ(after_first->front().message_id, second->message_id);
}

TEST(MemoryChatStoreTest, RetainsLatestHundredAndDropsRoomHistory) {
  MemoryChatStore store;
  for (int i = 1; i <= 101; ++i) {
    auto appended = store.Append("R1", "alice", "message-" + std::to_string(i), "ignored");
    ASSERT_TRUE(appended.ok());
  }

  auto recent = store.LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok());
  ASSERT_EQ(recent->size(), 100u);
  EXPECT_EQ(recent->front().text, "message-2");
  EXPECT_EQ(recent->back().text, "message-101");

  store.DropRoom("R1");
  recent = store.LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok());
  EXPECT_TRUE(recent->empty());
}

TEST(MemoryChatStoreTest, LimitsRecentAndCursorReads) {
  MemoryChatStore store;
  for (int i = 1; i <= 5; ++i) {
    ASSERT_TRUE(store.Append("R1", "alice", std::to_string(i), "ignored").ok());
  }

  auto recent = store.LoadRecent("R1", 2);
  ASSERT_TRUE(recent.ok());
  ASSERT_EQ(recent->size(), 2u);
  EXPECT_EQ(recent->front().text, "4");
  EXPECT_EQ(recent->back().text, "5");

  auto after = store.LoadAfter("R1", 1, 2);
  ASSERT_TRUE(after.ok());
  ASSERT_EQ(after->size(), 2u);
  EXPECT_EQ(after->front().text, "2");
  EXPECT_EQ(after->back().text, "3");

  const auto oversized = store.LoadRecent("R1", 50);
  ASSERT_TRUE(oversized.ok());
  EXPECT_EQ(oversized->size(), 5u);

  const auto past_newest = store.LoadAfter("R1", 5, 100);
  ASSERT_TRUE(past_newest.ok());
  EXPECT_TRUE(past_newest->empty());

  const auto no_recent = store.LoadRecent("R1", 0);
  ASSERT_TRUE(no_recent.ok());
  EXPECT_TRUE(no_recent->empty());

  const auto no_after = store.LoadAfter("R1", 0, 0);
  ASSERT_TRUE(no_after.ok());
  EXPECT_TRUE(no_after->empty());
}

TEST(MemoryChatStoreTest, KeepsRoomsIsolated) {
  MemoryChatStore store;
  const auto first = store.Append("R1", "alice", "room one", "ignored");
  const auto second = store.Append("R2", "bob", "room two", "ignored");
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  EXPECT_GT(second->message_id, first->message_id);

  const auto one = store.LoadRecent("R1", 100);
  ASSERT_TRUE(one.ok());
  ASSERT_EQ(one->size(), 1u);
  EXPECT_EQ(one->front().text, "room one");

  // A room's cursor reads never see IDs the shared counter handed another room.
  const auto after_gap = store.LoadAfter("R2", first->message_id, 100);
  ASSERT_TRUE(after_gap.ok());
  ASSERT_EQ(after_gap->size(), 1u);
  EXPECT_EQ(after_gap->front().text, "room two");

  store.DropRoom("R1");
  const auto survivor = store.LoadRecent("R2", 100);
  ASSERT_TRUE(survivor.ok());
  EXPECT_EQ(survivor->size(), 1u);

  store.DropRoom("never-existed");
  const auto unknown = store.LoadRecent("never-existed", 100);
  ASSERT_TRUE(unknown.ok());
  EXPECT_TRUE(unknown->empty());
}

TEST(MemoryChatStoreTest, RejectsEmptyAndOversizedText) {
  MemoryChatStore store;
  EXPECT_EQ(store.Append("R1", "alice", "", "ignored").status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(store.Append("R1", "alice", " \t\n", "ignored").status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(store.Append("R1", "alice", std::string(kChatTextByteLimit + 1, 'x'), "ignored")
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(store.Append("R1", "alice", std::string(kChatTextByteLimit, 'x'), "ignored").ok());

  const auto recent = store.LoadRecent("R1", 100);
  ASSERT_TRUE(recent.ok());
  EXPECT_EQ(recent->size(), 1u);
}

TEST(MemoryChatStoreTest, ConcurrentAppendsGetUniqueAscendingIds) {
  constexpr int kThreads = 4;
  constexpr int kPerThread = 50;
  MemoryChatStore store;

  std::mutex mu;
  std::vector<int64_t> ids;
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&store, &mu, &ids, t] {
      const std::string room_id = t % 2 == 0 ? "R1" : "R2";
      for (int i = 0; i < kPerThread; ++i) {
        auto appended = store.Append(room_id, "alice", std::to_string(i), "ignored");
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
    const auto rows = store.LoadRecent(room_id, kChatHistoryLimit);
    ASSERT_TRUE(rows.ok());
    EXPECT_EQ(rows->size(), kChatHistoryLimit);
    EXPECT_TRUE(std::is_sorted(
        rows->begin(), rows->end(),
        [](const ChatRow& lhs, const ChatRow& rhs) { return lhs.message_id < rhs.message_id; }));
  }
}

}  // namespace
}  // namespace golf_hub
