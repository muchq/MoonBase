#include "domains/games/apis/golf_hub/chat_store.h"

#include <gtest/gtest.h>

#include <string>

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

  ASSERT_TRUE(store.LoadRecent("R1", 0)->empty());
  ASSERT_TRUE(store.LoadAfter("R1", 0, 0)->empty());
}

}  // namespace
}  // namespace golf_hub
