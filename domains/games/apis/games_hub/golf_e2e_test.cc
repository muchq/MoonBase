// Golf's branches of the hub: the seat cap, the status the room lists a table under, the
// commands golf counts, the turn a leave hands on, and a whole game played to its knock.
//
// The NoShuffleDealer deals the pristine deck: the first seat holds the
// aces, the second the kings, and the first seat opens.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "domains/games/apis/games_hub/stream_test_fixture.h"

namespace games_hub {
namespace {

using moonbase::games::GolfMove;

class GolfFixture : public GamesHubStreamFixture {
 protected:
  std::optional<ManySeats> MultiSeatGolfTable(int count) {
    return MultiSeatTableOf<GolfMove>(
        count, Move, [](moonbase::games::PlayClientStream& stream, const std::string& wanted) {
          return ReceiveGolf(stream, wanted);
        });
  }

  static void Peek(Seat& seat, int index) {
    moonbase::games::PeekCard peek;
    peek.cardIndex = index;
    ASSERT_TRUE(seat.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  }
};

TEST_F(GolfFixture, AFifthSeatIsRefused) {
  ExpectSeatCap<GolfMove>(4, Move,
                          [](moonbase::games::PlayClientStream& stream, const std::string& wanted) {
                            return ReceiveGolf(stream, wanted);
                          });
}

// The room lists a golf table by its engine's phase: playing from the
// deal, peeking once every seat has peeked, playing again after the
// hide, knocked once a seat knocks.
TEST_F(GolfFixture, TheRoomListsEachPhaseOfTheTable) {
  auto table = SeatedGolfTable();
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->alice;
  Seat& bob = table->bob;
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameState").has_value());
  ExpectListedStatus(alice, table->game_id, "playing");

  for (Seat* seat : {&alice, &bob}) {
    for (int index : {0, 1}) {
      ASSERT_NO_FATAL_FAILURE(Peek(*seat, index));
      ASSERT_TRUE(ReceiveGolf(seat->stream, "gameState").has_value());
    }
  }
  ExpectListedStatus(alice, table->game_id, "peeking");

  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromHidecards(moonbase::games::HideCards{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());
  ExpectListedStatus(alice, table->game_id, "playing");

  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{}))).ok());
  ASSERT_TRUE(ReceiveGolf(bob.stream, "turnChanged").has_value());
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromKnock(moonbase::games::Knock{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "playerKnocked").has_value());
  ExpectListedStatus(alice, table->game_id, "knocked");
}

// Golf counts each command on its own series, by the move's name.
TEST_F(GolfFixture, CommandsAreCountedOnTheGolfSeries) {
  auto table = SeatedGolfTable();
  ASSERT_TRUE(table.has_value());
  ASSERT_TRUE(ReceiveGolf(table->alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameState").has_value());
  ASSERT_NO_FATAL_FAILURE(Peek(table->alice, 0));
  ASSERT_TRUE(ReceiveGolf(table->alice.stream, "gameState").has_value());
  ASSERT_NO_FATAL_FAILURE(Peek(table->bob, 0));
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameState").has_value());
  EXPECT_EQ(metrics_->CounterTotal("golf_commands", {{"command", "createGame"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("golf_commands", {{"command", "joinGame"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("golf_commands", {{"command", "startGame"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("golf_commands", {{"command", "peekCard"}}), 2);
}

// Three seats, the opener leaves on its turn: the table plays on, and the
// turn goes to the next seat.
TEST_F(GolfFixture, ALeaveOnTurnHandsTheTurnToTheNextSeat) {
  auto table = MultiSeatGolfTable(3);
  ASSERT_TRUE(table.has_value());
  Seat& opener = table->seats[0];
  Seat& next = table->seats[1];
  Seat& last = table->seats[2];
  for (Seat& seat : table->seats) ASSERT_TRUE(ReceiveGolf(seat.stream, "gameState").has_value());

  ASSERT_TRUE(opener.stream.Send(Move(GolfMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  for (Seat* seat : {&next, &last}) {
    auto turn = ReceiveGolf(seat->stream, "turnChanged");
    ASSERT_TRUE(turn.has_value());
    EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, next.player_id);
  }
}

TEST_F(GolfFixture, FullGameKnockerTieWinsAlone) {
  auto table = SeatedGolfTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;

  // The opening deal, from alice's chair: her cards face down even to her,
  // bob's hand nothing but nulls, the seeded discard face up.
  auto opening = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(opening.has_value());
  {
    const auto* update = opening->as_gameState_or_null();
    ASSERT_NE(update, nullptr);
    EXPECT_EQ(update->view.phase, "playing");
    ASSERT_TRUE(update->view.currentPlayerId.has_value());
    EXPECT_EQ(*update->view.currentPlayerId, alice.player_id);
    EXPECT_EQ(update->view.drawPileCount, 43);
    ASSERT_TRUE(update->view.discardTop.has_value());
    EXPECT_EQ(update->view.discardTop->rank, "Q");
    ASSERT_EQ(update->view.players.size(), 2u);
    for (const auto& player : update->view.players) {
      for (const auto& slot : player.cards) EXPECT_FALSE(slot.card.has_value());
    }
  }
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameState").has_value());

  // Opening peeks. Alice's first peek comes back to her alone, with the
  // ace face at the peeked index and nothing of bob's hand.
  moonbase::games::PeekCard peek;
  peek.cardIndex = 0;
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  auto peeked = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(peeked.has_value());
  {
    const auto& view = peeked->as_gameState_or_null()->view;
    ASSERT_TRUE(view.players[0].cards[0].card.has_value());
    EXPECT_EQ(view.players[0].cards[0].card->rank, "A");
    EXPECT_EQ(view.players[0].revealedIndexes, std::vector<int>{0});
    for (const auto& slot : view.players[1].cards) EXPECT_FALSE(slot.card.has_value());
  }
  peek.cardIndex = 1;
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());

  peek.cardIndex = 0;
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameState").has_value());
  peek.cardIndex = 1;
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());

  // The last peek starts the table-wide countdown: everyone hears it, and
  // bob's view shows his kings but still nothing of alice's aces.
  auto countdown = ReceiveGolf(bob.stream, "gameState");
  ASSERT_TRUE(countdown.has_value());
  {
    const auto& view = countdown->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "peeking");
    EXPECT_TRUE(view.allPlayersPeeked);
    ASSERT_TRUE(view.players[1].cards[0].card.has_value());
    EXPECT_EQ(view.players[1].cards[0].card->rank, "K");
    for (const auto& slot : view.players[0].cards) EXPECT_FALSE(slot.card.has_value());
  }
  auto alice_countdown = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(alice_countdown.has_value());
  EXPECT_EQ(alice_countdown->as_gameState_or_null()->view.phase, "peeking");

  // Turn moves wait for the hide.
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  auto gated = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(gated.has_value());

  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromHidecards(moonbase::games::HideCards{}))).ok());
  auto hidden = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(hidden.has_value());
  {
    const auto& view = hidden->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "playing");
    EXPECT_TRUE(view.players[0].revealedIndexes.empty());
  }
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameState").has_value());

  // Alice draws: she sees the face, bob sees only the count drop.
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  auto drawn = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(drawn.has_value());
  {
    const auto& view = drawn->as_gameState_or_null()->view;
    ASSERT_TRUE(view.drawnCard.has_value());
    EXPECT_EQ(view.drawnCard->rank, "Q");
    EXPECT_EQ(view.drawPileCount, 43);  // still on the pile until she commits
  }
  auto bob_saw_draw = ReceiveGolf(bob.stream, "gameState");
  ASSERT_TRUE(bob_saw_draw.has_value());
  EXPECT_FALSE(bob_saw_draw->as_gameState_or_null()->view.drawnCard.has_value());

  // She rejects it; the turn passes to bob.
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{}))).ok());
  auto turn = ReceiveGolf(alice.stream, "turnChanged");
  ASSERT_TRUE(turn.has_value());
  EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, bob.player_id);
  ASSERT_TRUE(ReceiveGolf(bob.stream, "turnChanged").has_value());

  // Bob knocks; alice takes the final turn; the game resolves. Both hands
  // cancel to zero, and the knocker takes the tie alone.
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromKnock(moonbase::games::Knock{}))).ok());
  auto knocked = ReceiveGolf(alice.stream, "playerKnocked");
  ASSERT_TRUE(knocked.has_value());
  EXPECT_EQ(knocked->as_playerKnocked_or_null()->playerId, bob.player_id);

  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{}))).ok());

  auto ended = ReceiveGolf(alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  {
    const auto* result = ended->as_gameEnded_or_null();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->winner, bob.player_id);
    ASSERT_EQ(result->winners.size(), 1u);
    EXPECT_EQ(result->winners[0], bob.player_id);
    ASSERT_EQ(result->finalScores.size(), 2u);
    for (const auto& score : result->finalScores) EXPECT_EQ(score.score, 0);
  }
  auto bob_ended = ReceiveGolf(bob.stream, "gameEnded");
  ASSERT_TRUE(bob_ended.has_value());

  // The final board is face up for everyone, and the room's running stats
  // credit the knocker's solo win.
  auto final_alice = ReceiveCase(alice.stream, "roomState");
  ASSERT_TRUE(final_alice.has_value());
  {
    const auto* room = final_alice->as_roomState_or_null();
    ASSERT_NE(room, nullptr);
    EXPECT_TRUE(room->games.empty());
    for (const auto& player : room->players) {
      EXPECT_EQ(player.gamesPlayed, 1);
      EXPECT_EQ(player.gamesWon, player.playerId == bob.player_id ? 1 : 0);
      EXPECT_EQ(player.totalScore, 0);
    }
  }
}

}  // namespace
}  // namespace games_hub
