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
using moonbase::games::GameCommands;
using moonbase::games::GolfMove;

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
  // The same deal the hub made, as an engine value.
  castle::GameState MirrorDeal(const std::string& game_id, const std::vector<std::string>& ids) {
    cards::NoShuffleDealer dealer;
    auto state = castle::dealCastleGame(game_id, ids, dealer.DealNewUnshuffledDeck());
    EXPECT_TRUE(state.ok()) << state.status();
    return *state;
  }
  castle::GameState MirrorDeal(const Table& table) {
    return MirrorDeal(table.game_id, {table.alice.player_id, table.bob.player_id});
  }

  // Every seat readies as dealt and hears the opening turn; the mirror
  // follows. Each seat's stream is left at the turnChanged.
  void ReadyAll(std::vector<Seat*> seats, std::optional<castle::GameState>& mirror) {
    for (std::size_t i = 0; i < seats.size(); ++i) {
      ASSERT_TRUE(
          seats[i]->stream.Send(Castle(CastleMove::FromReady(moonbase::games::Ready{}))).ok());
      auto step = mirror->ready(static_cast<int>(i));
      ASSERT_TRUE(step.ok()) << step.status();
      mirror.emplace(*std::move(step));
    }
    ASSERT_EQ(mirror->getPhase(), castle::Phase::Playing);
    for (Seat* seat : seats) {
      auto turn = ReceiveCastle(seat->stream, "turnChanged");
      ASSERT_TRUE(turn.has_value()) << seat->player_id;
      EXPECT_EQ(turn->as_turnChanged_or_null()->playerId,
                mirror->getPlayer(mirror->getWhoseTurn()).getId());
    }
  }

  // The board, as one chair sees it, against the mirror: every public
  // fact, the last play, the hand faces only for the viewer's own seat,
  // and canPlay only where that seat is on turn with a legal play.
  void ExpectBoard(const moonbase::games::CastleView& seen, const std::string& viewer,
                   const castle::GameState& mirror) {
    EXPECT_EQ(seen.pileCount, static_cast<int>(mirror.getPile().size()));
    EXPECT_EQ(seen.drawPileCount, static_cast<int>(mirror.getDrawPile().size()));
    const std::vector<cards::Card>& pile = mirror.getPile();
    EXPECT_EQ(Faces(seen.run),
              Faces(std::vector<cards::Card>(pile.end() - mirror.runOnTop(), pile.end())));
    EXPECT_EQ(seen.finished, mirror.getFinished());
    ASSERT_EQ(seen.lastPlay.has_value(), mirror.getLastPlay().has_value());
    if (seen.lastPlay.has_value()) {
      EXPECT_EQ(seen.lastPlay->playerId, mirror.getLastPlay()->playerId);
      EXPECT_EQ(Faces(seen.lastPlay->cards), Faces(mirror.getLastPlay()->cards));
      EXPECT_EQ(seen.lastPlay->burned, mirror.getLastPlay()->burned);
      EXPECT_EQ(seen.lastPlay->pickedUp, mirror.getLastPlay()->pickedUp);
    }
    ASSERT_EQ(seen.players.size(), mirror.getPlayers().size());
    for (std::size_t i = 0; i < seen.players.size(); ++i) {
      const int seat = static_cast<int>(i);
      const castle::Player& want = mirror.getPlayer(seat);
      EXPECT_EQ(seen.players[i].playerId, want.getId());
      EXPECT_EQ(seen.players[i].handCount, static_cast<int>(want.getHand().size()));
      EXPECT_EQ(Faces(seen.players[i].faceUp), Faces(want.getFaceUp()));
      EXPECT_EQ(seen.players[i].faceDownCount, static_cast<int>(want.getFaceDown().size()));
      EXPECT_EQ(seen.players[i].ready, want.isReady());
      EXPECT_EQ(seen.players[i].out, want.isOut());
      EXPECT_EQ(Faces(seen.players[i].hand),
                want.getId() == viewer ? Faces(want.getHand()) : std::vector<std::string>{});
      EXPECT_EQ(seen.players[i].canPlay, want.getId() == viewer && mirror.getWhoseTurn() == seat &&
                                             mirror.hasLegalPlay(seat));
    }
  }

  // What a game reached on its way to the end.
  struct PlayedOut {
    int moves = 0;
    int blind_plays = 0;
    int pickups = 0;
    int burns = 0;
  };

  // Play to the end: each turn the mirror picks a legal play, the hub
  // gets the same command, and every chair's view must agree with the
  // mirror. Stops with the final views unread.
  void PlayToEnd(std::vector<Seat*> seats, std::optional<castle::GameState>& mirror,
                 PlayedOut& played) {
    auto advance = [&](absl::StatusOr<castle::GameState> next) {
      if (!next.ok()) {
        ADD_FAILURE() << next.status();
        return false;
      }
      mirror.emplace(*std::move(next));
      return true;
    };
    std::map<std::string, Seat*> by_id;
    for (Seat* seat : seats) by_id[seat->player_id] = seat;
    while (!mirror->isOver()) {
      ASSERT_LT(++played.moves, 400) << "the scripted game never ends";
      const int seat = mirror->getWhoseTurn();
      const castle::Player mover = mirror->getPlayer(seat);
      Seat& stream = *by_id.at(mover.getId());
      if (mover.source() == castle::Source::FaceDown) {
        // Blind: the engine plays the flip or hands the pile over itself.
        ++played.blind_plays;
        moonbase::games::PlayFaceDown blind;
        blind.index = 0;
        ASSERT_TRUE(stream.stream.Send(Castle(CastleMove::FromPlayfacedown(blind))).ok());
        ASSERT_TRUE(advance(mirror->playFaceDown(seat, 0)));
      } else if (!mirror->hasLegalPlay(seat)) {
        ++played.pickups;
        ASSERT_TRUE(
            stream.stream.Send(Castle(CastleMove::FromPickup(moonbase::games::PickUp{}))).ok());
        ASSERT_TRUE(advance(mirror->pickUp(seat)));
      } else {
        // Every card of the first rank in the row whose full count meets
        // the pile: the run on top sets the count a play must match.
        const std::vector<cards::Card>& row = mover.row(mover.source());
        std::vector<int> indexes;
        for (std::size_t i = 0; i < row.size() && indexes.empty(); ++i) {
          std::vector<int> of_rank;
          for (std::size_t j = 0; j < row.size(); ++j) {
            if (row[j].getRank() == row[i].getRank()) of_rank.push_back(static_cast<int>(j));
          }
          if (mirror->isPlayable(row[i].getRank(), static_cast<int>(of_rank.size()))) {
            indexes = of_rank;
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
      if (mirror->getLastPlay().has_value() && mirror->getLastPlay()->burned) ++played.burns;
      if (mirror->isOver()) break;
      for (Seat* hearer : seats) {
        auto view = ReceiveCastle(hearer->stream, "gameState");
        ASSERT_TRUE(view.has_value()) << "after move " << played.moves;
        ExpectBoard(view->as_gameState_or_null()->view, hearer->player_id, *mirror);
        EXPECT_EQ(view->as_gameState_or_null()->view.currentPlayerId.value_or(""),
                  mirror->getPlayer(mirror->getWhoseTurn()).getId());
      }
      if (mirror->getWhoseTurn() != seat) {
        for (Seat* hearer : seats) {
          auto turn = ReceiveCastle(hearer->stream, "turnChanged");
          ASSERT_TRUE(turn.has_value()) << "after move " << played.moves;
          EXPECT_EQ(turn->as_turnChanged_or_null()->playerId,
                    mirror->getPlayer(mirror->getWhoseTurn()).getId());
        }
      }
    }
  }

  // The end, from every chair: final views with every hand face up and
  // nobody to play, the finish order, then the room's stats crediting
  // the first out and nobody else.
  void ExpectEnding(std::vector<Seat*> seats, const castle::GameState& mirror) {
    ASSERT_EQ(mirror.getPhase(), castle::Phase::Over);
    ASSERT_EQ(mirror.getFinished().size(), 1u);
    const std::string winner = mirror.getFinished().front();
    for (Seat* seat : seats) {
      auto final_view = ReceiveCastle(seat->stream, "gameState");
      ASSERT_TRUE(final_view.has_value());
      const auto& view = final_view->as_gameState_or_null()->view;
      EXPECT_EQ(view.phase, "ended");
      EXPECT_FALSE(view.currentPlayerId.has_value());
      EXPECT_EQ(view.finished, std::vector<std::string>{winner});
      for (std::size_t i = 0; i < view.players.size(); ++i) {
        EXPECT_EQ(Faces(view.players[i].hand),
                  Faces(mirror.getPlayer(static_cast<int>(i)).getHand()));
        EXPECT_EQ(view.players[i].out, view.players[i].playerId == winner);
        EXPECT_FALSE(view.players[i].canPlay);
      }
      auto ended = ReceiveCastle(seat->stream, "gameEnded");
      ASSERT_TRUE(ended.has_value());
      EXPECT_EQ(ended->as_gameEnded_or_null()->finished, std::vector<std::string>{winner});
      EXPECT_EQ(ended->as_gameEnded_or_null()->loser, mirror.loser());
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
    EXPECT_TRUE(view.run.empty());
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
  auto mirror_swap = mirror->swapForSetup(0, 0, 0);
  ASSERT_TRUE(mirror_swap.ok());
  mirror.emplace(*std::move(mirror_swap));
  for (int seat : {0, 1}) {
    auto step = mirror->ready(seat);
    ASSERT_TRUE(step.ok());
    mirror.emplace(*std::move(step));
  }
  ASSERT_EQ(mirror->getWhoseTurn(), 1);

  PlayedOut played;
  ASSERT_NO_FATAL_FAILURE(PlayToEnd({&alice, &bob}, mirror, played));
  // The deal reached every kind of move, or this game proved less than
  // it claims: blind flips, forced pick-ups, a burn, and the counters
  // that say so.
  EXPECT_GT(played.blind_plays, 0) << "the deal never reached blind play";
  EXPECT_GT(played.pickups, 0) << "the deal never forced a pick-up";
  EXPECT_GT(played.burns, 0) << "the deal never burned the pile";
  EXPECT_EQ(metrics_->CounterTotal("castle_commands", {{"command", "playFaceDown"}}),
            played.blind_plays);
  EXPECT_EQ(metrics_->CounterTotal("castle_commands", {{"command", "pickUp"}}), played.pickups);
  EXPECT_EQ(metrics_->CounterTotal("castle_commands", {{"command", "ready"}}), 2);

  // Two seats: the first out wins and the other is the loser.
  ASSERT_TRUE(mirror->loser().has_value());
  EXPECT_NE(mirror->getFinished().front(), *mirror->loser());
  ASSERT_NO_FATAL_FAILURE(ExpectEnding({&alice, &bob}, *mirror));
  EXPECT_EQ(metrics_->CounterTotal("castle_events", {{"event", "gameEnded"}}), 2);
}

// Three seats: the game ends when the first seat sheds its last card,
// and with two still holding cards nobody is the loser.
TEST_F(CastleGameFixture, AThreeSeatGameEndsOnTheFirstOutAndNamesNoLoser) {
  auto table = MultiSeatCastleTable(3);
  ASSERT_TRUE(table.has_value());
  std::vector<Seat*> seats;
  for (Seat& seat : table->seats) seats.push_back(&seat);
  std::optional<castle::GameState> mirror(MirrorDeal(table->game_id, table->ids()));
  ASSERT_NO_FATAL_FAILURE(ReadyAll(seats, mirror));

  PlayedOut played;
  ASSERT_NO_FATAL_FAILURE(PlayToEnd(seats, mirror, played));
  EXPECT_EQ(metrics_->CounterTotal("castle_commands", {{"command", "playFaceDown"}}),
            played.blind_plays);
  EXPECT_EQ(metrics_->CounterTotal("castle_commands", {{"command", "pickUp"}}), played.pickups);
  ASSERT_EQ(mirror->getFinished().size(), 1u);
  EXPECT_FALSE(mirror->loser().has_value());
  ASSERT_NO_FATAL_FAILURE(ExpectEnding(seats, *mirror));
  EXPECT_EQ(metrics_->CounterTotal("castle_events", {{"event", "gameEnded"}}), 3);
}

TEST_F(CastleGameFixture, TheRoomListsTablesByGameAndAGolfMoveOnACastleTableIsRefused) {
  auto table = SeatedCastleTable();
  ASSERT_TRUE(table.has_value());
  ASSERT_TRUE(ReceiveCastle(table->alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveCastle(table->bob.stream, "gameState").has_value());

  ASSERT_TRUE(
      table->alice.stream.Send(GameCommands::FromGetroomstate(moonbase::games::GetRoomState{}))
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
      table->bob.stream.Send(GameCommands::FromGetroomstate(moonbase::games::GetRoomState{})).ok());
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
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromJoinroom(join_room)).ok());
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
      bob->stream.Send(GameCommands::FromGetroomstate(moonbase::games::GetRoomState{})).ok());
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
  std::optional<castle::GameState> mirror(MirrorDeal(*table));
  ASSERT_NO_FATAL_FAILURE(ReadyAll({&table->alice, &table->bob}, mirror));
  ASSERT_EQ(mirror->getPlayer(mirror->getWhoseTurn()).getId(), table->bob.player_id);

  // The seat on turn leaves. Two seats, one leaves: the engine abandons
  // rather than plays on. The final view is over — the leaver already
  // gone, nobody on turn, nobody able to play — before the ending names
  // no finish order.
  ASSERT_TRUE(
      table->bob.stream.Send(Castle(CastleMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveCastle(table->bob.stream, "gameLeft").has_value());
  auto final_view = ReceiveCastle(table->alice.stream, "gameState");
  ASSERT_TRUE(final_view.has_value());
  {
    const auto& view = final_view->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "ended");
    EXPECT_FALSE(view.currentPlayerId.has_value());
    ASSERT_EQ(view.players.size(), 1u);
    EXPECT_EQ(view.players[0].playerId, table->alice.player_id);
    EXPECT_FALSE(view.players[0].canPlay);
    EXPECT_EQ(view.players[0].hand.size(), 3u);
  }
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
  // Joining the golf table in castle's envelope is refused the same way.
  auto carol = OpenSeat();
  ASSERT_TRUE(carol.has_value());
  ASSERT_TRUE(ReceiveCase(carol->stream, "sessionReady").has_value());
  moonbase::games::JoinRoom join_room;
  join_room.roomId = golf_table->room_id;
  ASSERT_TRUE(carol->stream.Send(GameCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(carol->stream, "roomState").has_value());
  moonbase::games::JoinGame join_game;
  join_game.gameId = golf_table->game_id;
  ASSERT_TRUE(carol->stream.Send(Castle(CastleMove::FromJoingame(join_game))).ok());
  auto wrong_join = ReceiveCase(carol->stream, "commandRejected");
  ASSERT_TRUE(wrong_join.has_value());
  EXPECT_EQ(wrong_join->as_commandRejected_or_null()->reason, "that table plays golf");
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "state"}}), 2);

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
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "state"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("castle_commands", {{"command", "playFromHand"}}), 1);
  // Refused or not, a castle command counts on castle's series.
  EXPECT_EQ(metrics_->CounterTotal("castle_commands", {{"command", "joinGame"}}), 2);
}

// Four seats in setup. A leave while others are still setting up
// announces no turn: there is none yet. Once the rest are ready, the
// last unready seat leaving is what setup was waiting on, so the table
// opens without them and the others hear the turn.
TEST_F(CastleGameFixture, ALeaveDuringSetupOpensTheTableOnlyOnceTheRestAreReady) {
  auto table = MultiSeatCastleTable(4);
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->seats[0];
  Seat& bob = table->seats[1];
  Seat& carol = table->seats[2];
  Seat& dave = table->seats[3];
  std::optional<castle::GameState> mirror(MirrorDeal(table->game_id, table->ids()));

  ASSERT_TRUE(
      dave.stream.Send(Castle(CastleMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveCastle(dave.stream, "gameLeft").has_value());
  {
    auto step = mirror->removePlayer(3);
    ASSERT_TRUE(step.ok());
    mirror.emplace(*std::move(step));
  }
  ASSERT_EQ(mirror->getPhase(), castle::Phase::Setup);
  for (Seat* seat : {&alice, &bob, &carol}) {
    auto view = AwaitCastleView(
        seat->stream,
        [](const moonbase::games::CastleView& view) { return view.players.size() == 3; },
        seat->player_id + " view without dave");
    ASSERT_TRUE(view.has_value());
    ExpectBoard(*view, seat->player_id, *mirror);
    ASSERT_TRUE(ReceiveCase(seat->stream, "roomState").has_value());
    ExpectNoEvent(seat->stream);
  }

  for (Seat* seat : {&alice, &bob}) {
    ASSERT_TRUE(seat->stream.Send(Castle(CastleMove::FromReady(moonbase::games::Ready{}))).ok());
  }
  ASSERT_TRUE(
      carol.stream.Send(Castle(CastleMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveCastle(carol.stream, "gameLeft").has_value());
  for (int seat : {0, 1}) {
    auto step = mirror->ready(seat);
    ASSERT_TRUE(step.ok());
    mirror.emplace(*std::move(step));
  }
  {
    auto step = mirror->removePlayer(2);
    ASSERT_TRUE(step.ok());
    mirror.emplace(*std::move(step));
  }
  ASSERT_EQ(mirror->getPhase(), castle::Phase::Playing);
  const std::string opener = mirror->getPlayer(mirror->getWhoseTurn()).getId();
  for (Seat* seat : {&alice, &bob}) {
    auto view = AwaitCastleView(
        seat->stream,
        [](const moonbase::games::CastleView& view) { return view.players.size() == 2; },
        seat->player_id + " view without carol");
    ASSERT_TRUE(view.has_value());
    ExpectBoard(*view, seat->player_id, *mirror);
    auto turn = ReceiveCastle(seat->stream, "turnChanged");
    ASSERT_TRUE(turn.has_value()) << seat->player_id;
    EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, opener);
    ASSERT_TRUE(ReceiveCase(seat->stream, "roomState").has_value());
    ExpectNoEvent(seat->stream);
  }
}

// Four seats in play: a seat off turn leaving keeps the turn where it
// was, with no announcement; the seat on turn leaving hands it to the
// next, announced the way a move says it.
TEST_F(CastleGameFixture, ALeaveOffTurnIsSilentAndALeaveOnTurnMovesIt) {
  auto table = MultiSeatCastleTable(4);
  ASSERT_TRUE(table.has_value());
  std::vector<Seat*> seats;
  for (Seat& seat : table->seats) seats.push_back(&seat);
  std::optional<castle::GameState> mirror(MirrorDeal(table->game_id, table->ids()));
  ASSERT_NO_FATAL_FAILURE(ReadyAll(seats, mirror));

  auto seat_of = [&](const std::string& id) {
    for (std::size_t i = 0; i < mirror->getPlayers().size(); ++i) {
      if (mirror->getPlayer(static_cast<int>(i)).getId() == id) return static_cast<int>(i);
    }
    ADD_FAILURE() << id << " is not seated";
    return -1;
  };
  auto leave = [&](Seat& leaver) {
    ASSERT_TRUE(
        leaver.stream.Send(Castle(CastleMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
    ASSERT_TRUE(ReceiveCastle(leaver.stream, "gameLeft").has_value());
    auto step = mirror->removePlayer(seat_of(leaver.player_id));
    ASSERT_TRUE(step.ok()) << step.status();
    mirror.emplace(*std::move(step));
    seats.erase(std::find(seats.begin(), seats.end(), &leaver));
  };

  const std::string opener = mirror->getPlayer(mirror->getWhoseTurn()).getId();
  Seat* off_turn = nullptr;
  for (Seat* seat : seats) {
    if (seat->player_id != opener && off_turn == nullptr) off_turn = seat;
  }
  ASSERT_NE(off_turn, nullptr);
  ASSERT_NO_FATAL_FAILURE(leave(*off_turn));
  ASSERT_EQ(mirror->getPlayer(mirror->getWhoseTurn()).getId(), opener);
  for (Seat* seat : seats) {
    auto view = ReceiveCastle(seat->stream, "gameState");
    ASSERT_TRUE(view.has_value()) << seat->player_id;
    EXPECT_EQ(view->as_gameState_or_null()->view.players.size(), 3u);
    EXPECT_EQ(view->as_gameState_or_null()->view.currentPlayerId.value_or(""), opener);
    ExpectBoard(view->as_gameState_or_null()->view, seat->player_id, *mirror);
    ASSERT_TRUE(ReceiveCase(seat->stream, "roomState").has_value());
    ExpectNoEvent(seat->stream);
  }

  Seat* on_turn = nullptr;
  for (Seat* seat : seats) {
    if (seat->player_id == opener) on_turn = seat;
  }
  ASSERT_NE(on_turn, nullptr);
  ASSERT_NO_FATAL_FAILURE(leave(*on_turn));
  ASSERT_EQ(mirror->getPhase(), castle::Phase::Playing);
  const std::string next = mirror->getPlayer(mirror->getWhoseTurn()).getId();
  EXPECT_NE(next, opener);
  for (Seat* seat : seats) {
    auto view = ReceiveCastle(seat->stream, "gameState");
    ASSERT_TRUE(view.has_value()) << seat->player_id;
    EXPECT_EQ(view->as_gameState_or_null()->view.players.size(), 2u);
    EXPECT_EQ(view->as_gameState_or_null()->view.currentPlayerId.value_or(""), next);
    ExpectBoard(view->as_gameState_or_null()->view, seat->player_id, *mirror);
    auto turn = ReceiveCastle(seat->stream, "turnChanged");
    ASSERT_TRUE(turn.has_value()) << seat->player_id;
    EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, next);
    ASSERT_TRUE(ReceiveCase(seat->stream, "roomState").has_value());
    ExpectNoEvent(seat->stream);
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

// A browser close mid-game parks the seat: the table sees the
// disconnect, nothing ends, and the resume token reclaims the seat with
// the castle table intact, the turn still its own to play.
TEST_F(CastleGameFixture, AMidGameBrowserCloseParksTheSeatAndTheTableSurvives) {
  auto table = SeatedCastleTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  std::optional<castle::GameState> mirror(MirrorDeal(*table));
  ASSERT_NO_FATAL_FAILURE(ReadyAll({&alice, &table->bob}, mirror));
  ASSERT_EQ(mirror->getPlayer(mirror->getWhoseTurn()).getId(), table->bob.player_id);

  table->bob.stream.Close();
  bool bob_disconnected = false;
  for (int i = 0; i < 8 && !bob_disconnected; ++i) {
    auto event = NextEvent(alice.stream);
    ASSERT_TRUE(event.has_value());
    if (const auto* envelope = event->as_castle_or_null()) {
      EXPECT_EQ(envelope->update.as_gameEnded_or_null(), nullptr)
          << "a parked seat must not resolve the table";
      continue;
    }
    const auto* room = event->as_roomState_or_null();
    if (room == nullptr) continue;
    for (const auto& player : room->players) {
      if (player.playerId == table->bob.player_id) bob_disconnected = !player.connected;
    }
  }
  ASSERT_TRUE(bob_disconnected);
  ExpectNoEvent(alice.stream, std::chrono::milliseconds(300));

  auto resumed = OpenSeat(table->bob.resume_token);
  ASSERT_TRUE(resumed.has_value());
  ASSERT_TRUE(ReceiveCase(resumed->stream, "sessionReady").has_value());
  auto rejoined = ReceiveCastle(resumed->stream, "gameJoined");
  ASSERT_TRUE(rejoined.has_value());
  EXPECT_EQ(rejoined->as_gameJoined_or_null()->view.gameId, table->game_id);
  ExpectBoard(rejoined->as_gameJoined_or_null()->view, resumed->player_id, *mirror);
  EXPECT_TRUE(rejoined->as_gameJoined_or_null()->view.players[1].canPlay);
  // The reclaimed seat still plays: its jack lands and fans out.
  moonbase::games::PlayFromHand jack;
  jack.indexes = {0};
  ASSERT_TRUE(resumed->stream.Send(Castle(CastleMove::FromPlayfromhand(jack))).ok());
  auto step = mirror->playFromHand(1, {0});
  ASSERT_TRUE(step.ok());
  mirror.emplace(*std::move(step));
  for (Seat* seat : {&alice, &*resumed}) {
    auto view = ReceiveCastle(seat->stream, "gameState");
    ASSERT_TRUE(view.has_value());
    ExpectBoard(view->as_gameState_or_null()->view, seat->player_id, *mirror);
  }
}

// A deal that puts the four sevens in the hands: alice holds 7♠ 7♣ 9♥,
// bob 7♥ 7♦ 8♣. Everything else stays where the pristine deck has it.
class SevensDealer : public cards::Dealer {
 public:
  void ShuffleDeck(std::deque<cards::Card>& deck) override {
    using cards::Card;
    using cards::Rank;
    using cards::Suit;
    // Dealt from the back, nine a seat: face-down, face-up, then hand,
    // each row taking the back card first. So alice's hand is positions
    // 7-9 from the back and bob's 16-18, and a hand reads back to front.
    const std::vector<Card> alice = {Card(Suit::Hearts, Rank::Nine), Card(Suit::Clubs, Rank::Seven),
                                     Card(Suit::Spades, Rank::Seven)};
    const std::vector<Card> bob = {Card(Suit::Clubs, Rank::Eight),
                                   Card(Suit::Diamonds, Rank::Seven),
                                   Card(Suit::Hearts, Rank::Seven)};
    std::vector<Card> rest;
    for (const Card& card : deck) {
      const bool wanted = std::find(alice.begin(), alice.end(), card) != alice.end() ||
                          std::find(bob.begin(), bob.end(), card) != bob.end();
      if (!wanted) rest.push_back(card);
    }
    ASSERT_EQ(rest.size(), deck.size() - 6);
    // Cards are immutable values: the arranged deck is built by pushing,
    // never by shifting.
    std::deque<Card> arranged;
    auto push = [&](auto first, auto last) {
      for (auto it = first; it != last; ++it) arranged.push_back(*it);
    };
    push(rest.begin(), rest.end() - 12);
    push(bob.begin(), bob.end());
    push(rest.end() - 12, rest.end() - 6);
    push(alice.begin(), alice.end());
    push(rest.end() - 6, rest.end());
    ASSERT_EQ(arranged.size(), deck.size());
    deck.swap(arranged);
  }
};

class SevensCastleFixture : public CastleGameFixture {
 protected:
  std::shared_ptr<cards::Dealer> MakeDealer() override { return std::make_shared<SevensDealer>(); }
};

// Seven, then two sevens, then the fourth: four of a kind counts as a
// ten. The table sees the run build and clear, and hears no turn change
// after the clear because the seat that completed it plays again.
TEST_F(SevensCastleFixture, TheFourthSevenClearsThePileAndTheTurnStays) {
  auto table = SeatedCastleTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;
  auto dealt = ReceiveCastle(alice.stream, "gameState");
  ASSERT_TRUE(dealt.has_value());
  EXPECT_EQ(Faces(dealt->as_gameState_or_null()->view.players[0].hand),
            (std::vector<std::string>{"7♠", "7♣", "9♥"}));
  ASSERT_TRUE(ReceiveCastle(bob.stream, "gameState").has_value());
  for (Seat* seat : {&alice, &bob}) {
    ASSERT_TRUE(seat->stream.Send(Castle(CastleMove::FromReady(moonbase::games::Ready{}))).ok());
  }
  // Both hold a seven as their lowest ordinary card; the tie opens on alice.
  for (Seat* seat : {&alice, &bob}) {
    auto turn = ReceiveCastle(seat->stream, "turnChanged");
    ASSERT_TRUE(turn.has_value());
    EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, alice.player_id);
  }

  auto play = [&](Seat& seat, std::vector<int> indexes) {
    moonbase::games::PlayFromHand move;
    move.indexes = std::move(indexes);
    ASSERT_TRUE(seat.stream.Send(Castle(CastleMove::FromPlayfromhand(move))).ok());
  };
  auto view_after = [&](Seat& seat) {
    auto update = ReceiveCastle(seat.stream, "gameState");
    EXPECT_TRUE(update.has_value());
    return update.has_value() ? update->as_gameState_or_null()->view
                              : moonbase::games::CastleView{};
  };

  play(alice, {0});
  EXPECT_EQ(Faces(view_after(alice).run), (std::vector<std::string>{"7♠"}));
  EXPECT_EQ(view_after(bob).currentPlayerId.value_or(""), bob.player_id);
  for (Seat* seat : {&alice, &bob})
    ASSERT_TRUE(ReceiveCastle(seat->stream, "turnChanged").has_value());

  play(bob, {0, 1});
  EXPECT_EQ(Faces(view_after(bob).run), (std::vector<std::string>{"7♠", "7♥", "7♦"}));
  auto hers = view_after(alice);
  EXPECT_EQ(hers.currentPlayerId.value_or(""), alice.player_id);
  EXPECT_TRUE(hers.players[0].canPlay);
  for (Seat* seat : {&alice, &bob})
    ASSERT_TRUE(ReceiveCastle(seat->stream, "turnChanged").has_value());

  // The 7♣ drew back to index 0 after the first play; it completes the four.
  ASSERT_EQ(Face(hers.players[0].hand[0]), "7♣");
  play(alice, {0});
  for (Seat* seat : {&alice, &bob}) {
    auto cleared = view_after(*seat);
    EXPECT_TRUE(cleared.run.empty());
    EXPECT_EQ(cleared.pileCount, 0);
    ASSERT_TRUE(cleared.lastPlay.has_value());
    EXPECT_TRUE(cleared.lastPlay->burned);
    EXPECT_EQ(Faces(cleared.lastPlay->cards), (std::vector<std::string>{"7♣"}));
    EXPECT_EQ(cleared.currentPlayerId.value_or(""), alice.player_id);
    EXPECT_EQ(cleared.phase, "playing");
    ExpectNoEvent(seat->stream);
  }
}

class ShortGraceCastleFixture : public CastleGameFixture {
 protected:
  std::chrono::seconds GracePeriod() override { return std::chrono::seconds(1); }
};

// Grace expiry is the deliberate end of a disconnect: the absent seat
// leaves, which at two seats abandons the table — no finish order, no
// loser, and the survivor's tally counts a game played but not won.
TEST_F(ShortGraceCastleFixture, GraceExpiryAbandonsTheTableWithNoLoser) {
  auto table = SeatedCastleTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  ASSERT_TRUE(ReceiveCastle(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveCastle(table->bob.stream, "gameState").has_value());

  table->bob.stream.Close();
  auto ended = ReceiveCastle(alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_TRUE(ended->as_gameEnded_or_null()->finished.empty());
  EXPECT_FALSE(ended->as_gameEnded_or_null()->loser.has_value());
  // The table is torn down before the absent seat leaves the room, so
  // the first roomState still lists both; the departure follows.
  std::optional<EventOf<decltype(alice.stream)>> room;
  for (int i = 0; i < 3; ++i) {
    room = ReceiveCase(alice.stream, "roomState");
    ASSERT_TRUE(room.has_value());
    EXPECT_TRUE(room->as_roomState_or_null()->games.empty());
    if (room->as_roomState_or_null()->players.size() == 1) break;
  }
  ASSERT_EQ(room->as_roomState_or_null()->players.size(), 1u);
  EXPECT_EQ(room->as_roomState_or_null()->players[0].playerId, alice.player_id);
  EXPECT_EQ(room->as_roomState_or_null()->players[0].gamesPlayed, 1);
  EXPECT_EQ(room->as_roomState_or_null()->players[0].gamesWon, 0);
}

}  // namespace
}  // namespace games_hub
