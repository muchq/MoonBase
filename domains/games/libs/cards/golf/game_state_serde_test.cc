#include "domains/games/libs/cards/golf/game_state_serde.h"

#include <gtest/gtest.h>

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/golf/player.h"

using namespace cards;
using namespace golf;

namespace {

// Card(i) inverts intValue(), so 0..51 is the full deck in a fixed order —
// the same determinism trick the hub's NoShuffleDealer relies on.
std::deque<Card> pristineDeck() {
  std::deque<Card> deck;
  for (int i = 0; i < 52; ++i) deck.emplace_back(i);
  return deck;
}

void expectStatesEqual(const GameState& a, const GameState& b) {
  EXPECT_EQ(a.getDrawPile(), b.getDrawPile());
  EXPECT_EQ(a.getDiscardPile(), b.getDiscardPile());
  EXPECT_EQ(a.getPlayers(), b.getPlayers());
  EXPECT_EQ(a.getPeekedAtDrawPile(), b.getPeekedAtDrawPile());
  EXPECT_EQ(a.getWhoseTurn(), b.getWhoseTurn());
  EXPECT_EQ(a.getWhoKnocked(), b.getWhoKnocked());
  EXPECT_EQ(a.getPeeksHidden(), b.getPeeksHidden());
  EXPECT_EQ(a.getGameId(), b.getGameId());
  EXPECT_EQ(a.getVersionId(), b.getVersionId());
}

// Serialize -> deserialize -> compare, and serialize again: the second
// pass must be byte-identical, so stored states re-serialize stably.
void expectRoundTrips(const GameState& state) {
  const std::string serialized = serializeGameState(state);
  const auto restored = deserializeGameState(serialized);
  ASSERT_TRUE(restored.ok()) << restored.status();
  expectStatesEqual(state, *restored);
  EXPECT_EQ(serializeGameState(*restored), serialized);
}

TEST(GameStateSerde, RoundTripsAFreshDeal) {
  const auto dealt = dealGolfGame("g-fresh", {"alice", "bob", "cara"}, pristineDeck());
  ASSERT_TRUE(dealt.ok()) << dealt.status();
  expectRoundTrips(*dealt);
}

TEST(GameStateSerde, RoundTripsEveryStateOfADeterministicGame) {
  // GameState is immutable (const members, no assignment), so the walk
  // re-emplaces into an optional at every transition.
  std::optional<GameState> state;
  auto dealt = dealGolfGame("g-full", {"alice", "bob"}, pristineDeck());
  ASSERT_TRUE(dealt.ok()) << dealt.status();
  state.emplace(*std::move(dealt));
  expectRoundTrips(*state);

  const auto advance = [&state](absl::StatusOr<GameState> next) {
    ASSERT_TRUE(next.ok()) << next.status();
    state.emplace(*std::move(next));
    expectRoundTrips(*state);
  };

  // The opening reveal: both players peek two of their own cards, then
  // one hide ends the countdown for the table.
  for (const int player : {0, 1}) {
    for (const Position position : {Position::TopLeft, Position::BottomRight}) {
      advance(state->peekOwnCard(player, position));
    }
  }
  advance(state->hideCards(0));

  // Turn play, every mechanic once. Draw-pile moves are draw-then-decide:
  // peekAtDrawPile is the draw, then the swap either keeps or declines it.
  advance(state->peekAtDrawPile(0));
  advance(state->swapForDrawPile(0, Position::TopLeft));
  advance(state->swapForDiscardPile(1, Position::BottomLeft));
  advance(state->peekAtDrawPile(0));
  advance(state->swapDrawForDiscardPile(0));
  advance(state->knock(1));
  advance(state->peekAtDrawPile(0));
  advance(state->swapForDrawPile(0, Position::TopRight));
  ASSERT_TRUE(state->isOver());
}

TEST(GameStateSerde, RoundTripsAnAbandonedSeatAndEmptyPiles) {
  // A nameless seat is a real engine state (abandoned mid-game), and an
  // empty draw pile is the game-over-by-exhaustion shape.
  const Player abandoned{Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Three),
                         Card(Suit::Hearts, Rank::Four), Card(Suit::Spades, Rank::Five)};
  const Player named{"dana",
                     Card(Suit::Clubs, Rank::Six),
                     Card(Suit::Diamonds, Rank::Seven),
                     Card(Suit::Hearts, Rank::Eight),
                     Card(Suit::Spades, Rank::Nine),
                     {Position::TopLeft, Position::BottomRight},
                     true};
  const GameState state{{}, {}, {abandoned, named}, false, 1, 0, true, "g-ragged", "v-9"};
  expectRoundTrips(state);
}

TEST(GameStateSerde, RejectsUnknownSchemaVersion) {
  const auto dealt = dealGolfGame("g", {"a", "b"}, pristineDeck());
  ASSERT_TRUE(dealt.ok());
  std::string serialized = serializeGameState(*dealt);
  const auto bumped = serialized.replace(serialized.find("\"v\":1"), 5, "\"v\":2");
  const auto restored = deserializeGameState(bumped);
  ASSERT_FALSE(restored.ok());
  EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(GameStateSerde, RejectsMalformedInput) {
  // None of these may crash, and all must read as invalid-argument: the
  // bytes come from a database row, not from code we trust.
  const std::vector<std::string> bad = {
      "",                 // not JSON
      "not json at all",  // not JSON
      "[]",               // wrong top-level shape
      R"({"v":1})",       // missing everything else
      R"({"v":"1"})",     // version of the wrong type
  };
  for (const std::string& input : bad) {
    const auto restored = deserializeGameState(input);
    ASSERT_FALSE(restored.ok()) << "accepted: " << input;
    EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument) << input;
  }
}

TEST(GameStateSerde, RejectsOutOfRangeValues) {
  const auto dealt = dealGolfGame("g", {"a", "b"}, pristineDeck());
  ASSERT_TRUE(dealt.ok());
  const std::string serialized = serializeGameState(*dealt);

  struct Break {
    std::string find;
    std::string replace;
  };
  // Each corruption targets a range the engine indexes by: card codes
  // must stay 0..51, peek positions 0..3, seats within the roster.
  const std::vector<Break> breaks = {
      {"\"drawPile\":[", "\"drawPile\":[52,"},
      {"\"drawPile\":[", "\"drawPile\":[-1,"},
      {"\"whoseTurn\":0", "\"whoseTurn\":7"},
      {"\"whoKnocked\":-1", "\"whoKnocked\":-3"},
  };
  for (const auto& corruption : breaks) {
    std::string corrupted = serialized;
    const auto at = corrupted.find(corruption.find);
    ASSERT_NE(at, std::string::npos) << corruption.find;
    corrupted.replace(at, corruption.find.size(), corruption.replace);
    const auto restored = deserializeGameState(corrupted);
    ASSERT_FALSE(restored.ok()) << "accepted: " << corruption.replace;
    EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument);
  }

  // A peek position outside 0..3 in a hand-built payload.
  std::string bad_peek = serialized;
  const auto players_at = bad_peek.find("\"peeked\":[]");
  ASSERT_NE(players_at, std::string::npos);
  bad_peek.replace(players_at, 11, "\"peeked\":[4]");
  const auto restored = deserializeGameState(bad_peek);
  ASSERT_FALSE(restored.ok());
  EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(GameStateSerde, RejectsWrongHandSize) {
  const auto dealt = dealGolfGame("g", {"a", "b"}, pristineDeck());
  ASSERT_TRUE(dealt.ok());
  std::string serialized = serializeGameState(*dealt);
  // A hand is exactly four cards; drop one.
  const auto at = serialized.find("\"cards\":[");
  ASSERT_NE(at, std::string::npos);
  const auto comma = serialized.find(',', at + 9);
  serialized.erase(at + 9, comma - (at + 9) + 1);
  const auto restored = deserializeGameState(serialized);
  ASSERT_FALSE(restored.ok());
  EXPECT_EQ(restored.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
