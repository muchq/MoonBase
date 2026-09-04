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

std::vector<std::string> Faces(const std::vector<moonbase::games::Card>& cards) {
  std::vector<std::string> faces;
  for (const auto& card : cards) faces.push_back(Face(card));
  return faces;
}

// The engine's cards spelled the way the wire spells them, so a view can
// be compared to the mirror face by face.
std::vector<std::string> Faces(const std::vector<cards::Card>& cards) {
  static constexpr const char* kRanks[] = {"2", "3",  "4", "5", "6", "7", "8",
                                           "9", "10", "J", "Q", "K", "A"};
  static constexpr const char* kSuits[] = {"♣", "♦", "♥", "♠"};
  std::vector<std::string> faces;
  for (const auto& card : cards) {
    faces.push_back(std::string(kRanks[static_cast<int>(card.getRank())]) +
                    kSuits[static_cast<int>(card.getSuit())]);
  }
  return faces;
}

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
    if (!next.ok()) {
      ADD_FAILURE() << next.status();
      return false;
    }
    mirror.emplace(*std::move(next));
    return true;
  };
  ASSERT_TRUE(advance(mirror->swapForSetup(0, 0, 0)));
  ASSERT_TRUE(advance(mirror->ready(0)));
  ASSERT_TRUE(advance(mirror->ready(1)));
  ASSERT_EQ(mirror->getWhoseTurn(), 1);

  // The board, as one chair sees it, against the mirror: every public
  // fact, and the hand faces only for the viewer's own seat.
  auto expect_board = [&](const moonbase::games::CastleView& seen, const std::string& viewer) {
    EXPECT_EQ(seen.pileCount, static_cast<int>(mirror->getPile().size()));
    EXPECT_EQ(seen.drawPileCount, static_cast<int>(mirror->getDrawPile().size()));
    EXPECT_EQ(seen.pileTop.has_value() ? Face(*seen.pileTop) : "",
              mirror->pileTop().has_value() ? Faces({*mirror->pileTop()})[0] : "");
    EXPECT_EQ(seen.finished, mirror->getFinished());
    ASSERT_EQ(seen.players.size(), mirror->getPlayers().size());
    for (std::size_t i = 0; i < seen.players.size(); ++i) {
      const castle::Player& want = mirror->getPlayer(static_cast<int>(i));
      EXPECT_EQ(seen.players[i].playerId, want.getId());
      EXPECT_EQ(seen.players[i].handCount, static_cast<int>(want.getHand().size()));
      EXPECT_EQ(Faces(seen.players[i].faceUp), Faces(want.getFaceUp()));
      EXPECT_EQ(seen.players[i].faceDownCount, static_cast<int>(want.getFaceDown().size()));
      EXPECT_EQ(seen.players[i].ready, want.isReady());
      EXPECT_EQ(seen.players[i].out, want.isOut());
      EXPECT_EQ(Faces(seen.players[i].hand),
                want.getId() == viewer ? Faces(want.getHand()) : std::vector<std::string>{});
    }
  };

  // Play to the end: each turn the mirror picks a legal play, the hub
  // gets the same command, and the views must agree with the mirror.
  std::map<std::string, Seat*> seats = {{alice.player_id, &alice}, {bob.player_id, &bob}};
  int moves = 0;
  int blind_plays = 0;
  int pickups = 0;
  while (!mirror->isOver()) {
    ASSERT_LT(++moves, 400) << "the scripted game never ends";
    const int seat = mirror->getWhoseTurn();
    const castle::Player mover = mirror->getPlayer(seat);
    Seat& stream = *seats.at(mover.getId());
    if (mover.source() == castle::Source::FaceDown) {
      // Blind: the engine plays the flip or hands the pile over itself.
      ++blind_plays;
      moonbase::games::PlayFaceDown blind;
      blind.index = 0;
      ASSERT_TRUE(stream.stream.Send(Castle(CastleMove::FromPlayfacedown(blind))).ok());
      ASSERT_TRUE(advance(mirror->playFaceDown(seat, 0)));
    } else if (!mirror->hasLegalPlay(seat)) {
      ++pickups;
      ASSERT_TRUE(
          stream.stream.Send(Castle(CastleMove::FromPickup(moonbase::games::PickUp{}))).ok());
      ASSERT_TRUE(advance(mirror->pickUp(seat)));
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
        ASSERT_TRUE(advance(mirror->playFromHand(seat, indexes)));
      } else {
        moonbase::games::PlayFaceUp face_up;
        face_up.indexes = indexes;
        ASSERT_TRUE(stream.stream.Send(Castle(CastleMove::FromPlayfaceup(face_up))).ok());
        ASSERT_TRUE(advance(mirror->playFaceUp(seat, indexes)));
      }
    }
    if (mirror->isOver()) break;
    Seat& other = mover.getId() == alice.player_id ? bob : alice;
    auto view = ReceiveCastle(stream.stream, "gameState");
    ASSERT_TRUE(view.has_value()) << "after move " << moves;
    expect_board(view->as_gameState_or_null()->view, mover.getId());
    auto other_view = ReceiveCastle(other.stream, "gameState");
    ASSERT_TRUE(other_view.has_value()) << "after move " << moves;
    expect_board(other_view->as_gameState_or_null()->view, other.player_id);
    EXPECT_EQ(view->as_gameState_or_null()->view.currentPlayerId.value_or(""),
              mirror->getPlayer(mirror->getWhoseTurn()).getId());
    if (mirror->getWhoseTurn() != seat) {
      for (Seat* hearer : {&stream, &other}) {
        auto turn = ReceiveCastle(hearer->stream, "turnChanged");
        ASSERT_TRUE(turn.has_value()) << "after move " << moves;
        EXPECT_EQ(turn->as_turnChanged_or_null()->playerId,
                  mirror->getPlayer(mirror->getWhoseTurn()).getId());
      }
    }
  }
  // The deal reached every kind of move, or this game proved less than
  // it claims: blind flips, forced pick-ups, and the counters that say so.
  EXPECT_GT(blind_plays, 0) << "the deal never reached blind play";
  EXPECT_GT(pickups, 0) << "the deal never forced a pick-up";
  EXPECT_EQ(metrics_->CounterTotal("castle_commands", {{"command", "playFaceDown"}}), blind_plays);
  EXPECT_EQ(metrics_->CounterTotal("castle_commands", {{"command", "pickUp"}}), pickups);
  EXPECT_EQ(metrics_->CounterTotal("castle_commands", {{"command", "ready"}}), 2);

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
    // Every hand face up at the end, the loser's included.
    for (std::size_t i = 0; i < view.players.size(); ++i) {
      EXPECT_EQ(Faces(view.players[i].hand),
                Faces(mirror->getPlayer(static_cast<int>(i)).getHand()));
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
  EXPECT_EQ(metrics_->CounterTotal("castle_events", {{"event", "gameEnded"}}), 2);
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
  // The room hears every table in that table's envelope: bob, who only
  // ever speaks golf, gets a castle gameCreated to list in the lobby.
  auto announced = ReceiveCastle(bob->stream, "gameCreated");
  ASSERT_TRUE(announced.has_value());
  EXPECT_EQ(announced->as_gameCreated_or_null()->gameId, castle_id);
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

TEST_F(CastleGameFixture, CastleMovesOnAGolfTableAreRefusedAndEngineRefusalsCountAsRules) {
  auto golf_table = SeatedTable();
  ASSERT_TRUE(golf_table.has_value());
  ASSERT_TRUE(
      golf_table->alice.stream.Send(Castle(CastleMove::FromPickup(moonbase::games::PickUp{})))
          .ok());
  auto wrong_game = ReceiveCase(golf_table->alice.stream, "commandRejected");
  ASSERT_TRUE(wrong_game.has_value());
  EXPECT_EQ(wrong_game->as_commandRejected_or_null()->reason, "that table plays golf");
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "state"}}), 1);

  // The engine's refusal is a rules refusal on the dashboard, and the
  // castle move still counted on its own series.
  auto castle_table = SeatedCastleTable();
  ASSERT_TRUE(castle_table.has_value());
  moonbase::games::PlayFromHand play;
  play.indexes = {0};
  ASSERT_TRUE(castle_table->alice.stream.Send(Castle(CastleMove::FromPlayfromhand(play))).ok());
  auto early = ReceiveCase(castle_table->alice.stream, "commandRejected");
  ASSERT_TRUE(early.has_value());
  EXPECT_EQ(early->as_commandRejected_or_null()->reason, "still setting up");
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "rules"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "state"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("castle_commands", {{"command", "playFromHand"}}), 1);
}

// Four seats: one leaves during setup and the table opens without them,
// which the others hear as the turn; one leaves mid-play and the turn
// stays with its occupant while the seats compact under it.
TEST_F(CastleGameFixture, ALeaveThatOpensOrMovesTheTurnAnnouncesIt) {
  auto table = SeatedCastleTable();
  ASSERT_TRUE(table.has_value());
  ASSERT_TRUE(ReceiveCastle(table->alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveCastle(table->bob.stream, "gameState").has_value());
  // A started table admits nobody, so seat the extras before starting:
  // build a fresh four-seat table instead.
  auto& alice = table->alice;
  auto& bob = table->bob;
  ASSERT_TRUE(
      alice.stream.Send(Castle(CastleMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveCastle(alice.stream, "gameLeft").has_value());
  ASSERT_TRUE(ReceiveCastle(bob.stream, "gameEnded").has_value());

  auto carol = OpenSeat();
  auto dave = OpenSeat();
  ASSERT_TRUE(carol.has_value() && dave.has_value());
  ASSERT_TRUE(ReceiveCase(carol->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(dave->stream, "sessionReady").has_value());
  moonbase::games::JoinRoom join_room;
  join_room.roomId = table->room_id;
  for (Seat* seat : {&*carol, &*dave}) {
    ASSERT_TRUE(seat->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
    ASSERT_TRUE(ReceiveCase(seat->stream, "roomState").has_value());
  }
  ASSERT_TRUE(
      alice.stream.Send(Castle(CastleMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto created = ReceiveCastle(alice.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  moonbase::games::JoinGame join_game;
  join_game.gameId = created->as_gameJoined_or_null()->view.gameId;
  for (Seat* seat : {&bob, &*carol, &*dave}) {
    ASSERT_TRUE(seat->stream.Send(Castle(CastleMove::FromJoingame(join_game))).ok());
    ASSERT_TRUE(ReceiveCastle(seat->stream, "gameJoined").has_value());
  }
  ASSERT_TRUE(
      alice.stream.Send(Castle(CastleMove::FromStartgame(moonbase::games::StartGame{}))).ok());
  for (Seat* seat : {&alice, &bob, &*carol, &*dave}) {
    ASSERT_TRUE(ReceiveCastle(seat->stream, "gameStarted").has_value());
  }

  // Everyone but dave readies; dave leaves; the table opens on the
  // lowest ordinary hand card among the three who stayed.
  for (Seat* seat : {&alice, &bob, &*carol}) {
    ASSERT_TRUE(seat->stream.Send(Castle(CastleMove::FromReady(moonbase::games::Ready{}))).ok());
  }
  ASSERT_TRUE(
      dave->stream.Send(Castle(CastleMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveCastle(dave->stream, "gameLeft").has_value());
  cards::NoShuffleDealer dealer;
  auto mirror = castle::dealCastleGame(
      join_game.gameId, {alice.player_id, bob.player_id, carol->player_id, dave->player_id},
      dealer.DealNewUnshuffledDeck());
  ASSERT_TRUE(mirror.ok());
  std::optional<castle::GameState> opened;
  for (auto step : {mirror->ready(0)}) {
    ASSERT_TRUE(step.ok());
    opened.emplace(*std::move(step));
  }
  for (int seat : {1, 2}) {
    auto step = opened->ready(seat);
    ASSERT_TRUE(step.ok());
    opened.emplace(*std::move(step));
  }
  {
    auto step = opened->removePlayer(3);
    ASSERT_TRUE(step.ok());
    opened.emplace(*std::move(step));
  }
  ASSERT_EQ(opened->getPhase(), castle::Phase::Playing);
  const std::string opener = opened->getPlayer(opened->getWhoseTurn()).getId();
  for (Seat* seat : {&alice, &bob, &*carol}) {
    auto turn = ReceiveCastle(seat->stream, "turnChanged");
    ASSERT_TRUE(turn.has_value()) << seat->player_id;
    EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, opener);
  }

  // A seat that is not on turn leaves mid-play: no turnChanged, the
  // occupant on turn keeps it, and the views show two seats.
  Seat* leaver = nullptr;
  for (Seat* seat : {&alice, &bob, &*carol}) {
    if (seat->player_id != opener && leaver == nullptr) leaver = seat;
  }
  ASSERT_NE(leaver, nullptr);
  ASSERT_TRUE(
      leaver->stream.Send(Castle(CastleMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveCastle(leaver->stream, "gameLeft").has_value());
  for (Seat* seat : {&alice, &bob, &*carol}) {
    if (seat == leaver) continue;
    auto view = ReceiveCastle(seat->stream, "gameState");
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->as_gameState_or_null()->view.players.size(), 2u);
    EXPECT_EQ(view->as_gameState_or_null()->view.currentPlayerId.value_or(""), opener);
    auto next = ReceiveCase(seat->stream, "roomState");
    ASSERT_TRUE(next.has_value());
  }
  for (Seat* seat : {&alice, &bob, &*carol}) {
    if (seat != leaver) ExpectNoEvent(seat->stream);
  }
}

TEST_F(CastleGameFixture, AResumedCastleSeatGetsItsOwnViewBack) {
  auto table = SeatedCastleTable();
  ASSERT_TRUE(table.has_value());
  ASSERT_TRUE(ReceiveCastle(table->alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveCastle(table->bob.stream, "gameState").has_value());

  table->alice.stream.Close();
  ASSERT_TRUE(ReceiveCase(table->bob.stream, "roomState").has_value());
  auto resumed = OpenSeat(table->alice.resume_token);
  ASSERT_TRUE(resumed.has_value());
  EXPECT_EQ(resumed->player_id, table->alice.player_id);
  auto ready = ReceiveCase(resumed->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  // The seat's own view, in castle's envelope, redacted for its viewer.
  auto joined = ReceiveCastle(resumed->stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  const auto& view = joined->as_gameJoined_or_null()->view;
  EXPECT_EQ(view.phase, "setup");
  ASSERT_EQ(view.players.size(), 2u);
  EXPECT_EQ(view.players[0].hand.size(), 3u);
  EXPECT_EQ(Face(view.players[0].hand[0]), "K♦");
  EXPECT_TRUE(view.players[1].hand.empty());
  EXPECT_EQ(view.players[1].handCount, 3);
}

}  // namespace
}  // namespace games_hub
