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

  // A started table of `count` seats; each seat has heard gameStarted
  // and still has its setup view to read.
  struct BigTable {
    std::vector<Seat> seats;
    std::string room_id;
    std::string game_id;
    std::vector<std::string> ids() const {
      std::vector<std::string> ids;
      for (const Seat& seat : seats) ids.push_back(seat.player_id);
      return ids;
    }
  };
  std::optional<BigTable> MultiSeatTable(int count) {
    BigTable table;
    for (int i = 0; i < count; ++i) {
      auto seat = OpenSeat();
      if (!seat.has_value()) return std::nullopt;
      if (!ReceiveCase(seat->stream, "sessionReady").has_value()) return std::nullopt;
      table.seats.push_back(std::move(*seat));
    }
    Seat& host = table.seats.front();
    table.room_id = CreateRoomFor(host);
    if (table.room_id.empty()) return std::nullopt;
    moonbase::games::JoinRoom join_room;
    join_room.roomId = table.room_id;
    for (std::size_t i = 1; i < table.seats.size(); ++i) {
      if (!table.seats[i].stream.Send(GolfCommands::FromJoinroom(join_room)).ok()) {
        return std::nullopt;
      }
      if (!ReceiveCase(table.seats[i].stream, "roomState").has_value()) return std::nullopt;
    }
    if (!host.stream.Send(Castle(CastleMove::FromCreategame(moonbase::games::CreateGame{}))).ok()) {
      return std::nullopt;
    }
    auto created = ReceiveCastle(host.stream, "gameJoined");
    if (!created.has_value()) return std::nullopt;
    table.game_id = created->as_gameJoined_or_null()->view.gameId;
    moonbase::games::JoinGame join_game;
    join_game.gameId = table.game_id;
    for (std::size_t i = 1; i < table.seats.size(); ++i) {
      if (!table.seats[i].stream.Send(Castle(CastleMove::FromJoingame(join_game))).ok()) {
        return std::nullopt;
      }
      if (!ReceiveCastle(table.seats[i].stream, "gameJoined").has_value()) return std::nullopt;
    }
    if (!host.stream.Send(Castle(CastleMove::FromStartgame(moonbase::games::StartGame{}))).ok()) {
      return std::nullopt;
    }
    for (Seat& seat : table.seats) {
      if (!ReceiveCastle(seat.stream, "gameStarted").has_value()) return std::nullopt;
    }
    return table;
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
    EXPECT_EQ(seen.pileTop.has_value() ? Face(*seen.pileTop) : "",
              mirror.pileTop().has_value() ? Faces({*mirror.pileTop()})[0] : "");
    EXPECT_EQ(seen.finished, mirror.getFinished());
    ASSERT_EQ(seen.lastPlay.has_value(), mirror.getLastPlay().has_value());
    if (seen.lastPlay.has_value()) {
      EXPECT_EQ(seen.lastPlay->playerId, mirror.getLastPlay()->playerId);
      EXPECT_EQ(Faces(seen.lastPlay->cards), Faces(mirror.getLastPlay()->cards));
      EXPECT_EQ(seen.lastPlay->burned, mirror.getLastPlay()->burned);
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
      EXPECT_EQ(seen.players[i].canPlay, !mirror.isOver() && want.getId() == viewer &&
                                             mirror.getWhoseTurn() == seat &&
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
      for (std::size_t i = 0; i < view.players.size(); ++i) {
        EXPECT_EQ(Faces(view.players[i].hand),
                  Faces(mirror.getPlayer(static_cast<int>(i)).getHand()));
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
  for (auto step : {mirror->swapForSetup(0, 0, 0)}) {
    ASSERT_TRUE(step.ok());
    mirror.emplace(*std::move(step));
  }
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
  auto table = MultiSeatTable(3);
  ASSERT_TRUE(table.has_value());
  std::vector<Seat*> seats;
  for (Seat& seat : table->seats) seats.push_back(&seat);
  std::optional<castle::GameState> mirror(MirrorDeal(table->game_id, table->ids()));
  ASSERT_NO_FATAL_FAILURE(ReadyAll(seats, mirror));

  PlayedOut played;
  ASSERT_NO_FATAL_FAILURE(PlayToEnd(seats, mirror, played));
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
  // Two seats, one leaves: the engine abandons rather than plays on. The
  // final view is over — the leaver already gone, nobody on turn, nobody
  // able to play — before the ending names no finish order.
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
  ASSERT_TRUE(carol->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
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

// Three seats, two ready: the third leaving is what setup was waiting
// on, so the table opens without them and the others hear the turn.
TEST_F(CastleGameFixture, ALeaveDuringSetupOpensTheTableAndTheOthersHearTheTurn) {
  auto table = MultiSeatTable(3);
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->seats[0];
  Seat& bob = table->seats[1];
  Seat& carol = table->seats[2];
  for (Seat* seat : {&alice, &bob}) {
    ASSERT_TRUE(seat->stream.Send(Castle(CastleMove::FromReady(moonbase::games::Ready{}))).ok());
  }
  ASSERT_TRUE(
      carol.stream.Send(Castle(CastleMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveCastle(carol.stream, "gameLeft").has_value());

  std::optional<castle::GameState> mirror(MirrorDeal(table->game_id, table->ids()));
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
    // Past the deal and the two ready fan-outs to the leave's view.
    std::optional<CastleUpdate> view;
    for (int i = 0; i < 4; ++i) {
      view = ReceiveCastle(seat->stream, "gameState");
      ASSERT_TRUE(view.has_value()) << seat->player_id;
      if (view->as_gameState_or_null()->view.players.size() == 2) break;
    }
    ExpectBoard(view->as_gameState_or_null()->view, seat->player_id, *mirror);
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
  auto table = MultiSeatTable(4);
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

// A browser close mid-setup parks the seat: the table sees the
// disconnect, nothing ends, and the resume token reclaims the seat with
// the castle table intact and still playable.
TEST_F(CastleGameFixture, AMidGameBrowserCloseParksTheSeatAndTheTableSurvives) {
  auto table = SeatedCastleTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  ASSERT_TRUE(ReceiveCastle(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveCastle(table->bob.stream, "gameState").has_value());

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
  EXPECT_FALSE(alice.stream.Receive(std::chrono::milliseconds(300)).ok());

  auto resumed = OpenSeat(table->bob.resume_token);
  ASSERT_TRUE(resumed.has_value());
  ASSERT_TRUE(ReceiveCase(resumed->stream, "sessionReady").has_value());
  auto rejoined = ReceiveCastle(resumed->stream, "gameJoined");
  ASSERT_TRUE(rejoined.has_value());
  EXPECT_EQ(rejoined->as_gameJoined_or_null()->view.gameId, table->game_id);
  EXPECT_EQ(rejoined->as_gameJoined_or_null()->view.phase, "setup");
  // The reclaimed seat still plays: its ready lands and fans out.
  ASSERT_TRUE(resumed->stream.Send(Castle(CastleMove::FromReady(moonbase::games::Ready{}))).ok());
  auto readied = ReceiveCastle(resumed->stream, "gameState");
  ASSERT_TRUE(readied.has_value());
  EXPECT_TRUE(readied->as_gameState_or_null()->view.players[1].ready);
  ASSERT_TRUE(ReceiveCastle(alice.stream, "gameState").has_value());
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
