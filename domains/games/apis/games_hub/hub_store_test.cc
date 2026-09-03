#include "domains/games/apis/games_hub/hub_store.h"

#include <gtest/gtest.h>

#include <deque>
#include <optional>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/game_state.h"

namespace games_hub {
namespace {

golf::GameState DealtState() {
  std::deque<cards::Card> deck;
  for (int i = 0; i < 52; ++i) deck.emplace_back(i);
  auto dealt = golf::dealGolfGame("G1", {"alice", "bob"}, std::move(deck));
  EXPECT_TRUE(dealt.ok());
  return *std::move(dealt);
}

TEST(MemoryHubStoreTest, OpsRoundTripAndRoomDeleteCascades) {
  MemoryHubStore store;
  store.Enqueue(
      {HubStore::UpsertRoom{"R1"}, HubStore::UpsertMember{{"R1", "alice", true, 2, 1, 9}}});
  store.Flush();
  ASSERT_TRUE(*store.CommitGameSave({"R1", "G1", {"alice"}, std::nullopt, 1}, "ignored"));

  auto snapshot = store.LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  ASSERT_EQ(snapshot->rooms, (std::vector<std::string>{"R1"}));
  ASSERT_EQ(snapshot->members.size(), 1u);
  EXPECT_EQ(snapshot->members[0].player_id, "alice");
  ASSERT_EQ(snapshot->games.size(), 1u);
  EXPECT_EQ(snapshot->games[0].version, 1);

  store.Enqueue({HubStore::DeleteRoom{"R1"}});
  store.Flush();
  snapshot = store.LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  EXPECT_TRUE(snapshot->rooms.empty());
  EXPECT_TRUE(snapshot->members.empty());
  EXPECT_TRUE(snapshot->games.empty());

  // Match PostgreSQL's foreign keys: stale child writes cannot recreate
  // rows after their parent room has disappeared.
  store.Enqueue({HubStore::UpsertMember{{"R1", "late", true, 0, 0, 0}}});
  auto landed = store.CommitGameSave({"R1", "late-game", {"late"}, std::nullopt, 1}, "ignored");
  ASSERT_TRUE(landed.ok());
  EXPECT_FALSE(*landed);
  snapshot = store.LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  EXPECT_TRUE(snapshot->members.empty());
  EXPECT_TRUE(snapshot->games.empty());
}

TEST(MemoryHubStoreTest, FinishCommitIsConditionalAtomicAndRetained) {
  MemoryHubStore store;
  store.Enqueue({HubStore::UpsertRoom{"R1"},
                 HubStore::UpsertMember{{"R1", "alice", true, 3, 1, 10}},
                 HubStore::UpsertMember{{"R1", "bob", true, 3, 0, 12}}});
  store.Flush();
  ASSERT_TRUE(*store.CommitGameSave({"R1", "G1", {"alice", "bob"}, std::nullopt, 1}, "start"));

  const golf::GameState state = DealtState();
  const std::vector<HubStore::StatsDelta> deltas = {
      {"alice", 1, 1, 4},
      {"bob", 1, 0, 9},
  };
  auto landed = store.CommitGameFinish({"R1", "G1", {"alice", "bob"}, state, 2}, deltas, "finish");
  ASSERT_TRUE(landed.ok());
  EXPECT_TRUE(*landed);

  auto room = store.LoadRoom("R1");
  ASSERT_TRUE(room.ok());
  ASSERT_EQ(room->games.size(), 1u);
  EXPECT_EQ(room->games[0].version, 2);
  ASSERT_TRUE(room->games[0].state.has_value());
  for (const auto& member : room->members) {
    if (member.player_id == "alice") {
      EXPECT_EQ(member.games_played, 4);
      EXPECT_EQ(member.games_won, 2);
      EXPECT_EQ(member.total_score, 14);
    } else {
      EXPECT_EQ(member.player_id, "bob");
      EXPECT_EQ(member.games_played, 4);
      EXPECT_EQ(member.games_won, 0);
      EXPECT_EQ(member.total_score, 21);
    }
  }

  landed = store.CommitGameFinish({"R1", "G1", {"alice", "bob"}, state, 2}, deltas, "replay");
  ASSERT_TRUE(landed.ok());
  EXPECT_FALSE(*landed);
  room = store.LoadRoom("R1");
  ASSERT_TRUE(room.ok());
  for (const auto& member : room->members) {
    EXPECT_EQ(member.games_played, 4);
  }
}

}  // namespace
}  // namespace games_hub
