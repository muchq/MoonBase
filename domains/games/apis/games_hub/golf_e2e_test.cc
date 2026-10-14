// Golf's branches of the hub (#1621), pinned before they move to golf's
// adapter: the seat cap, the status the room lists a table under, the
// commands golf counts, and the turn a leave hands on.
//
// The NoShuffleDealer deals the pristine deck: the first seat holds the
// aces, the second the kings, and the first seat opens.

#include <gtest/gtest.h>

#include <optional>
#include <string>

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
  auto table = SeatedTable();
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
TEST_F(GolfFixture, CommandsAreCountedOnGolfsSeries) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
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

}  // namespace
}  // namespace games_hub
