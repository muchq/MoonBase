// Castle on the room stream (#77): the second game on the hub, end to end
// through the generated client. The NoShuffleDealer deals the pristine
// deck from the back, so every card is known: alice (seat 0) holds the
// aces face down, A♣ K♠ K♥ face up and K♦ K♣ Q♠ in hand; bob the queens,
// jacks and J♣ 10♠ 10♥. A local engine mirror plays the same deal, which
// is what lets a whole game run to its end without a hand-written script
// of forty moves: every turn the mirror picks a legal play, the same
// command goes to the hub, and the hub's view must agree with the mirror.

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/castle/player.h"
#include "domains/games/libs/cards/dealer.h"

namespace games_hub {
namespace {

using moonbase::games::CastleMove;
using moonbase::games::CastleUpdate;
using moonbase::games::GolfCommands;
using moonbase::games::GolfMove;

GolfCommands Castle(CastleMove move) {
  moonbase::games::CastleCommand command;
  command.move = std::move(move);
  return GolfCommands::FromCastle(std::move(command));
}

// Tunnels into the castle envelope: the first CastleUpdate of the wanted
// case, skipping room noise and other updates in between.
std::optional<CastleUpdate> ReceiveCastle(moonbase::games::PlayClientStream& stream,
                                          const std::string& wanted,
                                          std::chrono::milliseconds budget = kReceiveBudget) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  for (int i = 0; i < 16; ++i) {
    auto received = ReceiveWithin(stream, deadline);
    if (!received.ok()) {
      ADD_FAILURE() << "gave up waiting for castle " << wanted << ": "
                    << received.error().message();
      return std::nullopt;
    }
    if (!received->has_value()) return std::nullopt;
    const auto* envelope = (*received)->as_castle_or_null();
    if (envelope == nullptr) continue;
    if (wanted == envelope->update.case_name()) return envelope->update;
  }
  return std::nullopt;
}

std::string Face(const moonbase::games::Card& card) { return card.rank + card.suit; }

class CastleGameFixture : public GamesHubStreamFixture {
 protected:
  // Two seats in one room at a castle table, started: both have heard
  // gameStarted and their setup view.
  std::optional<Table> SeatedCastleTable() {
    auto alice = OpenSeat();
    auto bob = OpenSeat();
    if (!alice.has_value() || !bob.has_value()) return std::nullopt;
    if (!ReceiveCase(alice->stream, "sessionReady").has_value()) return std::nullopt;
    if (!ReceiveCase(bob->stream, "sessionReady").has_value()) return std::nullopt;
    const std::string room_id = CreateRoomFor(*alice);
    if (room_id.empty()) return std::nullopt;
    moonbase::games::JoinRoom join_room;
    join_room.roomId = room_id;
    if (!bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok()) return std::nullopt;
    if (!ReceiveCase(bob->stream, "roomState").has_value()) return std::nullopt;

    if (!alice->stream.Send(Castle(CastleMove::FromCreategame(moonbase::games::CreateGame{})))
             .ok()) {
      return std::nullopt;
    }
    auto joined = ReceiveCastle(alice->stream, "gameJoined");
    if (!joined.has_value()) return std::nullopt;
    const std::string game_id = joined->as_gameJoined_or_null()->view.gameId;
    moonbase::games::JoinGame join_game;
    join_game.gameId = game_id;
    if (!bob->stream.Send(Castle(CastleMove::FromJoingame(join_game))).ok()) return std::nullopt;
    if (!ReceiveCastle(bob->stream, "gameJoined").has_value()) return std::nullopt;

    if (!alice->stream.Send(Castle(CastleMove::FromStartgame(moonbase::games::StartGame{}))).ok()) {
      return std::nullopt;
    }
    if (!ReceiveCastle(alice->stream, "gameStarted").has_value()) return std::nullopt;
    if (!ReceiveCastle(bob->stream, "gameStarted").has_value()) return std::nullopt;
    return Table{std::move(*alice), std::move(*bob), room_id, game_id};
  }

  // The same deal the hub made, as an engine value.
  castle::GameState MirrorDeal(const Table& table) {
    cards::NoShuffleDealer dealer;
    auto state = castle::dealCastleGame(table.game_id, {table.alice.player_id, table.bob.player_id},
                                        dealer.DealNewUnshuffledDeck());
    EXPECT_TRUE(state.ok()) << state.status();
    return *state;
  }
};

TEST_F(CastleGameFixture, SetupThenAWholeGameAgreesWithTheEngine) {
  auto table = SeatedCastleTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;

  // The deal, from each chair: own hand faces, the other hand a count,
  // both face-up rows, face-down rows as counts, nobody ready.
  auto opening = ReceiveCastle(alice.stream, "gameState");
  ASSERT_TRUE(opening.has_value());
  {
    const auto& view = opening->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "setup");
    EXPECT_FALSE(view.currentPlayerId.has_value());
    EXPECT_EQ(view.drawPileCount, 34);
    EXPECT_EQ(view.pileCount, 0);
    EXPECT_FALSE(view.pileTop.has_value());
    ASSERT_EQ(view.players.size(), 2u);
    const auto& me = view.players[0];
    EXPECT_EQ(me.playerId, alice.player_id);
    EXPECT_FALSE(me.ready);
    ASSERT_EQ(me.hand.size(), 3u);
    EXPECT_EQ(Face(me.hand[0]), "K♦");
    EXPECT_EQ(Face(me.hand[1]), "K♣");
    EXPECT_EQ(Face(me.hand[2]), "Q♠");
    ASSERT_EQ(me.faceUp.size(), 3u);
    EXPECT_EQ(Face(me.faceUp[0]), "A♣");
    EXPECT_EQ(me.faceDownCount, 3);
    const auto& them = view.players[1];
    EXPECT_EQ(them.playerId, bob.player_id);
    EXPECT_EQ(them.handCount, 3);
    EXPECT_TRUE(them.hand.empty());
    ASSERT_EQ(them.faceUp.size(), 3u);
    EXPECT_EQ(Face(them.faceUp[0]), "J♠");
    EXPECT_EQ(them.faceDownCount, 3);
  }
  ASSERT_TRUE(ReceiveCastle(bob.stream, "gameState").has_value());

  // A play before setup is done is the engine's refusal, in band.
  moonbase::games::PlayFromHand play;
  play.indexes = {0};
  ASSERT_TRUE(alice.stream.Send(Castle(CastleMove::FromPlayfromhand(play))).ok());
  auto early = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(early.has_value());
  EXPECT_EQ(early->as_commandRejected_or_null()->reason, "still setting up");

  // Alice swaps K♦ for her A♣; both chairs see the new face-up row, only
  // hers shows the ace in hand.
  moonbase::games::SwapForSetup swap;
  swap.handIndex = 0;
  swap.faceUpIndex = 0;
  ASSERT_TRUE(alice.stream.Send(Castle(CastleMove::FromSwapforsetup(swap))).ok());
  auto swapped = ReceiveCastle(alice.stream, "gameState");
  ASSERT_TRUE(swapped.has_value());
  EXPECT_EQ(Face(swapped->as_gameState_or_null()->view.players[0].hand[0]), "A♣");
  EXPECT_EQ(Face(swapped->as_gameState_or_null()->view.players[0].faceUp[0]), "K♦");
  auto bob_saw_swap = ReceiveCastle(bob.stream, "gameState");
  ASSERT_TRUE(bob_saw_swap.has_value());
  EXPECT_EQ(Face(bob_saw_swap->as_gameState_or_null()->view.players[0].faceUp[0]), "K♦");
  EXPECT_TRUE(bob_saw_swap->as_gameState_or_null()->view.players[0].hand.empty());

  // Ready, both: bob opens (his jack is the lowest ordinary hand card;
  // tens are special and do not count), and everyone hears the turn.
  ASSERT_TRUE(alice.stream.Send(Castle(CastleMove::FromReady(moonbase::games::Ready{}))).ok());
  ASSERT_TRUE(ReceiveCastle(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveCastle(bob.stream, "gameState").has_value());
  ASSERT_TRUE(bob.stream.Send(Castle(CastleMove::FromReady(moonbase::games::Ready{}))).ok());
  auto playing = ReceiveCastle(alice.stream, "gameState");
  ASSERT_TRUE(playing.has_value());
  EXPECT_EQ(playing->as_gameState_or_null()->view.phase, "playing");
  auto opener = ReceiveCastle(alice.stream, "turnChanged");
  ASSERT_TRUE(opener.has_value());
  EXPECT_EQ(opener->as_turnChanged_or_null()->playerId, bob.player_id);
  ASSERT_TRUE(ReceiveCastle(bob.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveCastle(bob.stream, "turnChanged").has_value());

  // The mirror, brought to the same point. Engine states are values
  // with const fields, so the mirror moves forward by replacement.
  std::optional<castle::GameState> mirror(MirrorDeal(*table));
  auto advance = [&](absl::StatusOr<castle::GameState> next) {
    ASSERT_TRUE(next.ok()) << next.status();
    mirror.emplace(*std::move(next));
  };
  advance(mirror->swapForSetup(0, 0, 0));
  advance(mirror->ready(0));
  advance(mirror->ready(1));
  ASSERT_EQ(mirror->getWhoseTurn(), 1);

  // Play to the end: each turn the mirror picks a legal play, the hub
  // gets the same command, and the views must agree with the mirror.
  std::map<std::string, Seat*> seats = {{alice.player_id, &alice}, {bob.player_id, &bob}};
  int moves = 0;
  while (!mirror->isOver()) {
    ASSERT_LT(++moves, 400) << "the scripted game never ends";
    const int seat = mirror->getWhoseTurn();
    const castle::Player mover = mirror->getPlayer(seat);
    Seat& stream = *seats.at(mover.getId());
    if (mover.source() == castle::Source::FaceDown) {
      // Blind: the engine plays the flip or hands the pile over itself.
      moonbase::games::PlayFaceDown blind;
      blind.index = 0;
      ASSERT_TRUE(stream.stream.Send(Castle(CastleMove::FromPlayfacedown(blind))).ok());
      advance(mirror->playFaceDown(seat, 0));
    } else if (!mirror->hasLegalPlay(seat)) {
      ASSERT_TRUE(
          stream.stream.Send(Castle(CastleMove::FromPickup(moonbase::games::PickUp{}))).ok());
      advance(mirror->pickUp(seat));
    } else {
      // Every card of the first playable rank in the row in play.
      const std::vector<cards::Card>& row = mover.row(mover.source());
      std::vector<int> indexes;
      for (std::size_t i = 0; i < row.size() && indexes.empty(); ++i) {
        if (!mirror->isPlayable(row[i].getRank())) continue;
        for (std::size_t j = 0; j < row.size(); ++j) {
          if (row[j].getRank() == row[i].getRank()) indexes.push_back(static_cast<int>(j));
        }
      }
      ASSERT_FALSE(indexes.empty());
      if (mover.source() == castle::Source::Hand) {
        moonbase::games::PlayFromHand from_hand;
        from_hand.indexes = indexes;
        ASSERT_TRUE(stream.stream.Send(Castle(CastleMove::FromPlayfromhand(from_hand))).ok());
        advance(mirror->playFromHand(seat, indexes));
      } else {
        moonbase::games::PlayFaceUp face_up;
        face_up.indexes = indexes;
        ASSERT_TRUE(stream.stream.Send(Castle(CastleMove::FromPlayfaceup(face_up))).ok());
        advance(mirror->playFaceUp(seat, indexes));
      }
    }
    if (mirror->isOver()) break;
    auto view = ReceiveCastle(stream.stream, "gameState");
    ASSERT_TRUE(view.has_value()) << "after move " << moves;
    const auto& seen = view->as_gameState_or_null()->view;
    EXPECT_EQ(seen.pileCount, static_cast<int>(mirror->getPile().size())) << "move " << moves;
    EXPECT_EQ(seen.drawPileCount, static_cast<int>(mirror->getDrawPile().size()));
    EXPECT_EQ(seen.currentPlayerId.value_or(""), mirror->getPlayer(mirror->getWhoseTurn()).getId());
    // The other chair hears the same state; turn changes reach both.
    Seat& other = mover.getId() == alice.player_id ? bob : alice;
    ASSERT_TRUE(ReceiveCastle(other.stream, "gameState").has_value());
    if (mirror->getWhoseTurn() != seat) {
      ASSERT_TRUE(ReceiveCastle(stream.stream, "turnChanged").has_value());
      ASSERT_TRUE(ReceiveCastle(other.stream, "turnChanged").has_value());
    }
  }

  // The end: final views with every hand face up, then the finish order
  // and the loser, then the room's stats credit the first out.
  ASSERT_EQ(mirror->getPhase(), castle::Phase::Over);
  ASSERT_EQ(mirror->getFinished().size(), 1u);
  const std::string winner = mirror->getFinished().front();
  const std::string loser = mirror->loser().value_or("");
  EXPECT_NE(winner, loser);
  for (Seat* seat : {&alice, &bob}) {
    auto final_view = ReceiveCastle(seat->stream, "gameState");
    ASSERT_TRUE(final_view.has_value());
    const auto& view = final_view->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "ended");
    EXPECT_FALSE(view.currentPlayerId.has_value());
    for (const auto& player : view.players) {
      EXPECT_EQ(player.hand.size(), static_cast<std::size_t>(player.handCount)) << player.playerId;
    }
    auto ended = ReceiveCastle(seat->stream, "gameEnded");
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->as_gameEnded_or_null()->finished, std::vector<std::string>{winner});
    EXPECT_EQ(ended->as_gameEnded_or_null()->loser.value_or(""), loser);
    auto room = ReceiveCase(seat->stream, "roomState");
    ASSERT_TRUE(room.has_value());
    EXPECT_TRUE(room->as_roomState_or_null()->games.empty());
    for (const auto& player : room->as_roomState_or_null()->players) {
      EXPECT_EQ(player.gamesPlayed, 1);
      EXPECT_EQ(player.gamesWon, player.playerId == winner ? 1 : 0);
      EXPECT_EQ(player.totalScore, 0);
    }
  }
}

TEST_F(CastleGameFixture, TheRoomListsTablesByGameAndAGolfMoveOnACastleTableIsRefused) {
  auto table = SeatedCastleTable();
  ASSERT_TRUE(table.has_value());
  ASSERT_TRUE(ReceiveCastle(table->alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveCastle(table->bob.stream, "gameState").has_value());

  ASSERT_TRUE(
      table->alice.stream.Send(GolfCommands::FromGetroomstate(moonbase::games::GetRoomState{}))
          .ok());
  auto room = ReceiveCase(table->alice.stream, "roomState");
  ASSERT_TRUE(room.has_value());
  ASSERT_EQ(room->as_roomState_or_null()->games.size(), 1u);
  EXPECT_EQ(room->as_roomState_or_null()->games[0].game, "castle");
  EXPECT_EQ(room->as_roomState_or_null()->games[0].status, "setup");
  EXPECT_EQ(room->as_roomState_or_null()->games[0].playerCount, 2);

  // Golf's vocabulary on a castle table: refused as state, and the golf
  // client never hears a castle event it cannot read.
  ASSERT_TRUE(
      table->alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  auto rejected = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "that table plays castle");
  // Nothing reached the table: bob's next event is his own room snapshot,
  // with no castle update in front of it.
  ASSERT_TRUE(
      table->bob.stream.Send(GolfCommands::FromGetroomstate(moonbase::games::GetRoomState{})).ok());
  for (int i = 0; i < 8; ++i) {
    auto event = NextEvent(table->bob.stream);
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->as_castle_or_null(), nullptr) << event->case_name();
    if (std::string(event->case_name()) == "roomState") break;
  }
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "state"}}), 1);
}

TEST_F(CastleGameFixture, ATableOfEachGameCanShareARoomAndJoinsAreByGame) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::games::JoinRoom join_room;
  join_room.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());

  ASSERT_TRUE(
      alice->stream.Send(Castle(CastleMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto castle_joined = ReceiveCastle(alice->stream, "gameJoined");
  ASSERT_TRUE(castle_joined.has_value());
  const std::string castle_id = castle_joined->as_gameJoined_or_null()->view.gameId;
  EXPECT_EQ(castle_joined->as_gameJoined_or_null()->view.phase, "waiting");
  ASSERT_TRUE(bob->stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto golf_joined = ReceiveGolf(bob->stream, "gameJoined");
  ASSERT_TRUE(golf_joined.has_value());
  const std::string golf_id = golf_joined->as_gameJoined_or_null()->view.gameId;

  // Both tables in the room's lobby, each named by its game.
  ASSERT_TRUE(
      bob->stream.Send(GolfCommands::FromGetroomstate(moonbase::games::GetRoomState{})).ok());
  auto room = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(room.has_value());
  std::map<std::string, std::string> games;
  for (const auto& summary : room->as_roomState_or_null()->games) {
    games[summary.gameId] = summary.game;
  }
  EXPECT_EQ(games, (std::map<std::string, std::string>{{castle_id, "castle"}, {golf_id, "golf"}}));

  // A golf join of the castle table is refused by name; the castle join
  // of a seat still at a golf table is the usual leave-first refusal.
  ASSERT_TRUE(bob->stream.Send(Move(GolfMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(bob->stream, "gameLeft").has_value());
  moonbase::games::JoinGame join_game;
  join_game.gameId = castle_id;
  ASSERT_TRUE(bob->stream.Send(Move(GolfMove::FromJoingame(join_game))).ok());
  auto wrong_game = ReceiveCase(bob->stream, "commandRejected");
  ASSERT_TRUE(wrong_game.has_value());
  EXPECT_EQ(wrong_game->as_commandRejected_or_null()->reason, "that table plays castle");
  ASSERT_TRUE(bob->stream.Send(Castle(CastleMove::FromJoingame(join_game))).ok());
  auto joined = ReceiveCastle(bob->stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  EXPECT_EQ(joined->as_gameJoined_or_null()->view.players.size(), 2u);
}

TEST_F(CastleGameFixture, LeavingMidGameAbandonsItWithNoLoser) {
  auto table = SeatedCastleTable();
  ASSERT_TRUE(table.has_value());
  ASSERT_TRUE(ReceiveCastle(table->alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveCastle(table->bob.stream, "gameState").has_value());

  ASSERT_TRUE(
      table->bob.stream.Send(Castle(CastleMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveCastle(table->bob.stream, "gameLeft").has_value());
  // Two seats, one leaves: the engine abandons rather than plays on.
  auto ended = ReceiveCastle(table->alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_TRUE(ended->as_gameEnded_or_null()->finished.empty());
  EXPECT_FALSE(ended->as_gameEnded_or_null()->loser.has_value());
  auto room = ReceiveCase(table->alice.stream, "roomState");
  ASSERT_TRUE(room.has_value());
  EXPECT_TRUE(room->as_roomState_or_null()->games.empty());
  for (const auto& player : room->as_roomState_or_null()->players) {
    // A leaver never names a loser, and only the seat that stayed played.
    EXPECT_EQ(player.gamesPlayed, player.playerId == table->alice.player_id ? 1 : 0);
    EXPECT_EQ(player.gamesWon, 0);
  }
}

}  // namespace
}  // namespace games_hub
