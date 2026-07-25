#include <gtest/gtest.h>

#include <cstdlib>
#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/golf/game_state_serde.h"
#include "domains/games/libs/cards/golf/player.h"
#include "domains/platform/libs/pg/pg.h"

namespace {

using cards::Card;

// Phase-0 slice of the persistence integration suite (#1194): serialized
// engine states must survive the storage target step 2 will write them
// to, and jsonb is not a byte-preserving container — it re-orders keys
// (length-then-bytes, not alphabetical), re-spaces the text, and refuses
// escaped NULs outright. Real postgres via GOLF_HUB_TEST_DB_URL (the
// vault suite's pattern); skips otherwise.
class GameStateJsonbTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* url = std::getenv("GOLF_HUB_TEST_DB_URL");
    if (url == nullptr || *url == '\0') {
      GTEST_SKIP() << "GOLF_HUB_TEST_DB_URL unset";
    }
    db_ = std::make_unique<pg::Client>(url);
    // TEMP table: session-scoped on the client's one connection, gone
    // when the test's connection closes — no migration entanglement.
    ASSERT_TRUE(db_->Exec("CREATE TEMP TABLE IF NOT EXISTS game_state_probe (state jsonb)").ok());
    ASSERT_TRUE(db_->Exec("TRUNCATE game_state_probe").ok());
  }

  // INSERT the serialized state into the jsonb column and return
  // postgres's own text rendering of what it stored.
  std::optional<std::string> StoreAndFetch(const std::string& serialized) {
    auto inserted =
        db_->Exec("INSERT INTO game_state_probe (state) VALUES ($1::jsonb)", {serialized});
    if (!inserted.ok()) {
      ADD_FAILURE() << "insert failed: " << inserted.status();
      return std::nullopt;
    }
    auto fetched = db_->Exec("SELECT state::text FROM game_state_probe");
    if (!fetched.ok() || fetched->rows() != 1) {
      ADD_FAILURE() << "fetch failed: " << fetched.status();
      return std::nullopt;
    }
    return fetched->Get(0, 0);
  }

  std::unique_ptr<pg::Client> db_;
};

std::deque<Card> pristineDeck() {
  std::deque<Card> deck;
  for (int i = 0; i < 52; ++i) deck.emplace_back(i);
  return deck;
}

// A mid-game state with every kind of content populated: peek history,
// hidden reveal, a swapped hand, a grown discard pile.
golf::GameState playedState() {
  std::optional<golf::GameState> state;
  auto dealt = golf::dealGolfGame("g", {"alice", "bob"}, pristineDeck());
  EXPECT_TRUE(dealt.ok()) << dealt.status();
  state.emplace(*std::move(dealt));
  for (const int player : {0, 1}) {
    for (const golf::Position position : {golf::Position::TopLeft, golf::Position::BottomRight}) {
      auto next = state->peekOwnCard(player, position);
      EXPECT_TRUE(next.ok()) << next.status();
      state.emplace(*std::move(next));
    }
  }
  auto hidden = state->hideCards(0);
  EXPECT_TRUE(hidden.ok()) << hidden.status();
  state.emplace(*std::move(hidden));
  auto drawn = state->peekAtDrawPile(0);
  EXPECT_TRUE(drawn.ok()) << drawn.status();
  state.emplace(*std::move(drawn));
  auto swapped = state->swapForDrawPile(0, golf::Position::TopLeft);
  EXPECT_TRUE(swapped.ok()) << swapped.status();
  return *std::move(swapped);
}

TEST_F(GameStateJsonbTest, StoredStateSurvivesJsonbNormalization) {
  const golf::GameState state = playedState();
  const std::string serialized = golf::serializeGameState(state);

  const auto stored = StoreAndFetch(serialized);
  ASSERT_TRUE(stored.has_value());
  // jsonb re-renders the text: byte identity is an app-side property
  // only, and nothing downstream may compare stored states by bytes.
  EXPECT_NE(*stored, serialized);

  const auto restored = golf::deserializeGameState(*stored);
  ASSERT_TRUE(restored.ok()) << restored.status();
  // Canonical re-serialization is state equality (ids are storage-owned
  // and excluded by schema).
  EXPECT_EQ(golf::serializeGameState(*restored), serialized);
}

TEST_F(GameStateJsonbTest, NameWithNulByteStillInserts) {
  // Without serialize-side replacement this INSERT fails with
  // "unsupported Unicode escape sequence" — the reason the policy
  // exists. The sanitized name must then survive the round trip.
  const golf::Player mangled{std::string("a\0b", 3), Card(0), Card(1), Card(2), Card(3)};
  const golf::GameState state{{Card(4)}, {Card(5)}, {mangled, mangled}, false, 0, -1, false,
                              "g",       "v"};
  const std::string serialized = golf::serializeGameState(state);

  const auto stored = StoreAndFetch(serialized);
  ASSERT_TRUE(stored.has_value());
  const auto restored = golf::deserializeGameState(*stored);
  ASSERT_TRUE(restored.ok()) << restored.status();
  EXPECT_EQ(golf::serializeGameState(*restored), serialized);
}

TEST_F(GameStateJsonbTest, StoredStateIsQueryable) {
  // The reason step 2 wants jsonb rather than text: operators reach into
  // the stored state without deserializing it.
  ASSERT_TRUE(StoreAndFetch(golf::serializeGameState(playedState())).has_value());
  auto version = db_->Exec("SELECT state->>'v' FROM game_state_probe");
  ASSERT_TRUE(version.ok()) << version.status();
  EXPECT_EQ(version->Get(0, 0).value_or(""), "1");
  auto seats = db_->Exec("SELECT jsonb_array_length(state->'players') FROM game_state_probe");
  ASSERT_TRUE(seats.ok()) << seats.status();
  EXPECT_EQ(seats->Get(0, 0).value_or(""), "2");
}

}  // namespace
