// Go's round robins, end to end through the generated client: chess's
// rules on go's tables. A pairing's table waits on the round robin's
// terms, starts when the opponent sits with the pairing's Black to move,
// plays its one game, and that game is the pairing's result.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/stream_test_fixture.h"

namespace games_hub {
namespace {

using moonbase::games::GameCommands;
using moonbase::games::GoMove;
using moonbase::games::GoRoundRobin;
using moonbase::games::GoView;

moonbase::games::GoStartGame NineByNine(double komi = 5.5) {
  moonbase::games::GoStartGame terms;
  terms.size = 9;
  terms.komi = komi;
  terms.initialSeconds = 60;
  terms.incrementSeconds = 0;
  return terms;
}

GameCommands CreateGoRoundRobin(std::vector<std::string> entrants,
                                std::optional<moonbase::games::GoStartGame> terms = NineByNine()) {
  moonbase::games::GoCreateRoundRobin create;
  create.entrants = std::move(entrants);
  create.terms = std::move(terms);
  return Go(GoMove::FromCreateroundrobin(create));
}

GameCommands PlayGoPairing(const std::string& round_robin_id, const std::string& opponent) {
  moonbase::games::ChessPlayRoundRobin play;
  play.roundRobinId = round_robin_id;
  play.opponent = opponent;
  return Go(GoMove::FromPlayroundrobin(play));
}

GameCommands GoRoundRobins() {
  return Go(GoMove::FromRoundrobins(moonbase::games::ChessRoundRobinsRequest{}));
}

class GoRoundRobinFixture : public GamesHubStreamFixture {
 protected:
  struct Members {
    Room room;
    std::vector<std::string> ids;
  };
  std::optional<Members> Seated(int count) {
    auto room = SeatedRoom(count);
    if (!room.has_value()) return std::nullopt;
    std::vector<std::string> ids;
    for (const Seat& seat : room->seats) ids.push_back(seat.player_id);
    return Members{std::move(*room), std::move(ids)};
  }

  template <typename Pred>
  std::optional<GoRoundRobin> HeardWhere(Seat& seat, Pred pred) {
    for (int i = 0; i < 8; ++i) {
      auto update = ReceiveGo(seat.stream, "roundRobin");
      if (!update.has_value()) return std::nullopt;
      if (pred(*update->as_roundRobin_or_null())) return *update->as_roundRobin_or_null();
    }
    return std::nullopt;
  }

  // Seat 0 creates a go round robin of everyone seated; every member has
  // heard it.
  std::optional<GoRoundRobin> Created(Members& members,
                                      moonbase::games::GoStartGame terms = NineByNine()) {
    if (!members.room.seats[0].stream.Send(CreateGoRoundRobin(members.ids, terms)).ok()) {
      return std::nullopt;
    }
    std::optional<GoRoundRobin> created;
    for (Seat& seat : members.room.seats) {
      created = HeardWhere(seat, [](const auto&) { return true; });
      if (!created.has_value()) return std::nullopt;
    }
    return created;
  }

  // The next refusal `seat` hears, past however many views and round
  // robins a game left queued ahead of it.
  std::optional<std::string> Refused(Seat& seat) {
    const auto deadline = std::chrono::steady_clock::now() + kReceiveBudget;
    while (true) {
      auto received = ReceiveWithin(seat.stream, deadline);
      if (!received.ok() || !received->has_value()) {
        ADD_FAILURE() << "no refusal";
        return std::nullopt;
      }
      if (const auto* refused = (*received)->as_commandRejected_or_null()) return refused->reason;
    }
  }

  // The next view `seat` hears that `ready` holds of.
  static std::optional<GoView> ViewWhere(Seat& seat,
                                         const std::function<bool(const GoView&)>& ready) {
    for (int i = 0; i < 16; ++i) {
      auto update = ReceiveGo(seat.stream, "gameState");
      if (!update.has_value()) return std::nullopt;
      const GoView& view = update->as_gameState_or_null()->view;
      if (ready(view)) return view;
    }
    ADD_FAILURE() << "no such view for " << seat.player_id;
    return std::nullopt;
  }

  // The pairing between seats 0 and 1, by its index, and who is Black.
  struct Paired {
    int index = -1;
    std::string black;
    std::string white;
  };
  static Paired PairingOf(const GoRoundRobin& round_robin, const std::string& a,
                          const std::string& b) {
    for (std::size_t i = 0; i < round_robin.pairings.size(); ++i) {
      const auto& pairing = round_robin.pairings[i];
      if ((pairing.black == a && pairing.white == b) || (pairing.black == b && pairing.white == a)) {
        return {static_cast<int>(i), pairing.black, pairing.white};
      }
    }
    return {};
  }

  // Seat 0 opens its pairing with seat 1 and seat 1 sits: both have heard
  // the game start on the round robin's terms.
  std::optional<GoView> PairingStarted(Members& members, const GoRoundRobin& round_robin) {
    Seat& alice = members.room.seats[0];
    Seat& bob = members.room.seats[1];
    if (!alice.stream.Send(PlayGoPairing(round_robin.roundRobinId, members.ids[1])).ok()) {
      return std::nullopt;
    }
    auto joined = ReceiveGo(alice.stream, "gameJoined");
    if (!joined.has_value()) return std::nullopt;
    moonbase::games::JoinGame join;
    join.gameId = joined->as_gameJoined_or_null()->view.gameId;
    if (!bob.stream.Send(Go(GoMove::FromJoingame(join))).ok()) return std::nullopt;
    std::optional<GoView> playing;
    for (Seat* seat : {&alice, &bob}) {
      playing = ViewWhere(*seat, [](const GoView& v) { return v.phase == "playing"; });
      if (!playing.has_value()) return std::nullopt;
    }
    return playing;
  }

  Seat& SeatOf(Members& members, const std::string& id) {
    return members.room.seats[id == members.ids[0] ? 0 : 1];
  }
};

TEST_F(GoRoundRobinFixture, CreatingOneTellsTheRoomItsTermsPairingsAndStandings) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->creator, members->ids[0]);
  EXPECT_EQ(created->entrants, members->ids);
  EXPECT_EQ(created->terms.size, 9);
  EXPECT_EQ(created->terms.komi, 5.5);
  EXPECT_EQ(created->terms.initialSeconds, 60);
  EXPECT_EQ(created->terms.incrementSeconds, 0);
  EXPECT_EQ(created->pairings.size(), 3u);
  for (const auto& pairing : created->pairings) EXPECT_FALSE(pairing.result.has_value());
  ASSERT_EQ(created->standings.size(), 3u);
  for (const auto& standing : created->standings) EXPECT_EQ(standing.points, 0);
}

TEST_F(GoRoundRobinFixture, TermsNoGameCouldOpenOnAreRefused) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  moonbase::games::GoStartGame terms = NineByNine();
  terms.size = 10;
  ASSERT_TRUE(members->room.seats[0].stream.Send(CreateGoRoundRobin(members->ids, terms)).ok());
  EXPECT_EQ(Refused(members->room.seats[0]), "a board is 9, 13 or 19, not 10");
}

// The pairing's table waits on the round robin's terms, starts on them
// when the opponent sits, with the pairing's Black on move, and its one
// game decides the pairing.
TEST_F(GoRoundRobinFixture, APairingsTableStartsOnItsTermsWithItsBlackAndScoresIt) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  const Paired paired = PairingOf(*created, members->ids[0], members->ids[1]);
  ASSERT_GE(paired.index, 0);

  Seat& alice = members->room.seats[0];
  ASSERT_TRUE(alice.stream.Send(PlayGoPairing(created->roundRobinId, members->ids[1])).ok());
  auto joined = ReceiveGo(alice.stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  const GoView& waiting = joined->as_gameJoined_or_null()->view;
  EXPECT_EQ(waiting.roundRobinId, created->roundRobinId);
  EXPECT_EQ(waiting.phase, "waiting");
  ASSERT_TRUE(waiting.terms.has_value());
  EXPECT_EQ(waiting.terms->size, 9);

  moonbase::games::JoinGame join;
  join.gameId = waiting.gameId;
  ASSERT_TRUE(members->room.seats[1].stream.Send(Go(GoMove::FromJoingame(join))).ok());
  const auto playing = ViewWhere(members->room.seats[1],
                                 [](const GoView& v) { return v.phase == "playing"; });
  ASSERT_TRUE(playing.has_value());
  EXPECT_EQ(playing->size, 9);
  EXPECT_EQ(playing->komi, 5.5);
  EXPECT_EQ(playing->roundRobinId, created->roundRobinId);
  ASSERT_EQ(playing->players.size(), 2u);
  EXPECT_EQ(playing->players[0].playerId, paired.black);
  EXPECT_EQ(playing->players[0].color, "black");
  EXPECT_EQ(playing->currentPlayerId, paired.black);

  // White resigns: Black takes the pairing.
  ASSERT_TRUE(SeatOf(*members, paired.white)
                  .stream.Send(Go(GoMove::FromResign(moonbase::games::GoResign{})))
                  .ok());
  const auto scored = HeardWhere(members->room.seats[2], [&](const GoRoundRobin& heard) {
    return heard.pairings[paired.index].result.has_value();
  });
  ASSERT_TRUE(scored.has_value());
  EXPECT_EQ(scored->pairings[paired.index].result, "black");
  EXPECT_FALSE(scored->pairings[paired.index].forfeit);
  EXPECT_EQ(scored->standings[0].playerId, paired.black);
  EXPECT_EQ(scored->standings[0].points, 1);

  // One game: no next one at a pairing's table.
  ASSERT_TRUE(alice.stream.Send(Go(GoMove::FromStartgame({}))).ok());
  EXPECT_EQ(Refused(alice), "a round robin's table plays one game");
}

// An even count is a draw, half a point each.
TEST_F(GoRoundRobinFixture, AnEvenCountDrawsThePairing) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto created = Created(*members, NineByNine(/*komi=*/0));
  ASSERT_TRUE(created.has_value());
  const Paired paired = PairingOf(*created, members->ids[0], members->ids[1]);
  ASSERT_GE(paired.index, 0);
  ASSERT_TRUE(PairingStarted(*members, *created).has_value());

  Seat& black = SeatOf(*members, paired.black);
  Seat& white = SeatOf(*members, paired.white);
  ASSERT_TRUE(black.stream.Send(Go(GoMove::FromPass(moonbase::games::GoPass{}))).ok());
  ASSERT_TRUE(ViewWhere(white, [](const GoView& v) { return v.moveCount == 1; }));
  ASSERT_TRUE(white.stream.Send(Go(GoMove::FromPass(moonbase::games::GoPass{}))).ok());
  ASSERT_TRUE(ViewWhere(black, [](const GoView& v) { return v.phase == "scoring"; }));
  for (Seat* seat : {&black, &white}) {
    ASSERT_TRUE(seat->stream.Send(Go(GoMove::FromAccept(moonbase::games::GoAccept{}))).ok());
  }
  const auto scored = HeardWhere(members->room.seats[2], [&](const GoRoundRobin& heard) {
    return heard.pairings[paired.index].result.has_value();
  });
  ASSERT_TRUE(scored.has_value());
  EXPECT_EQ(scored->pairings[paired.index].result, "draw");
  for (const auto& standing : scored->standings) {
    EXPECT_EQ(standing.points, standing.playerId == members->ids[2] ? 0 : 0.5) << standing.playerId;
  }
}

// Each game's envelope serves its own round robins: go's roundRobins
// lists go's alone, and go's moves refuse a chess round robin.
TEST_F(GoRoundRobinFixture, EachEnvelopeServesItsOwnGamesRoundRobins) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  Seat& alice = members->room.seats[0];
  ASSERT_TRUE(alice.stream.Send(CreateRoundRobin(members->ids)).ok());
  auto chess = ReceiveChess(alice.stream, "roundRobin");
  ASSERT_TRUE(chess.has_value());
  const std::string chess_id = chess->as_roundRobin_or_null()->roundRobinId;
  const auto go = Created(*members);
  ASSERT_TRUE(go.has_value());

  ASSERT_TRUE(alice.stream.Send(GoRoundRobins()).ok());
  auto listed = ReceiveGo(alice.stream, "roundRobins");
  ASSERT_TRUE(listed.has_value());
  const auto& round_robins = listed->as_roundRobins_or_null()->roundRobins;
  ASSERT_EQ(round_robins.size(), 1u);
  EXPECT_EQ(round_robins[0].roundRobinId, go->roundRobinId);

  ASSERT_TRUE(alice.stream.Send(PlayGoPairing(chess_id, members->ids[1])).ok());
  EXPECT_EQ(Refused(alice), "that round robin plays chess");
}

// Leaving a pairing's game in play loses it, as chess's does: the table's
// sheet scores the abandonment and the pairing takes it.
TEST_F(GoRoundRobinFixture, LeavingAPairingsGameLosesIt) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  const Paired paired = PairingOf(*created, members->ids[0], members->ids[1]);
  ASSERT_GE(paired.index, 0);
  ASSERT_TRUE(PairingStarted(*members, *created).has_value());
  ASSERT_TRUE(SeatOf(*members, paired.black)
                  .stream.Send(Go(GoMove::FromLeavegame(moonbase::games::LeaveGame{})))
                  .ok());
  const auto scored = HeardWhere(members->room.seats[2], [&](const GoRoundRobin& heard) {
    return heard.pairings[paired.index].result.has_value();
  });
  ASSERT_TRUE(scored.has_value());
  EXPECT_EQ(scored->pairings[paired.index].result, "white");
  EXPECT_FALSE(scored->pairings[paired.index].forfeit);
  EXPECT_FALSE(scored->pairings[paired.index].gameId.has_value());
}

TEST_F(GoRoundRobinFixture, TheCreatorRecordsAForfeitAndWithdrawsAnEntrant) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  Seat& creator = members->room.seats[0];
  moonbase::games::ChessForfeit forfeit;
  forfeit.roundRobinId = created->roundRobinId;
  forfeit.winner = members->ids[0];
  forfeit.loser = members->ids[1];
  ASSERT_TRUE(creator.stream.Send(Go(GoMove::FromForfeit(forfeit))).ok());
  const auto forfeited = HeardWhere(members->room.seats[1], [](const GoRoundRobin& heard) {
    for (const auto& pairing : heard.pairings) {
      if (pairing.forfeit) return true;
    }
    return false;
  });
  ASSERT_TRUE(forfeited.has_value());
  for (const auto& pairing : forfeited->pairings) {
    if (!pairing.forfeit) continue;
    EXPECT_EQ(pairing.result, pairing.white == members->ids[0] ? "white" : "black");
  }

  moonbase::games::ChessWithdraw withdraw;
  withdraw.roundRobinId = created->roundRobinId;
  withdraw.playerId = members->ids[2];
  ASSERT_TRUE(creator.stream.Send(Go(GoMove::FromWithdraw(withdraw))).ok());
  const auto withdrawn = HeardWhere(
      members->room.seats[1], [](const GoRoundRobin& heard) { return !heard.withdrawn.empty(); });
  ASSERT_TRUE(withdrawn.has_value());
  EXPECT_THAT(withdrawn->withdrawn, ::testing::ElementsAre(members->ids[2]));
  int voided = 0;
  for (const auto& pairing : withdrawn->pairings) voided += pairing.voided ? 1 : 0;
  EXPECT_EQ(voided, 2);
}

}  // namespace
}  // namespace games_hub
