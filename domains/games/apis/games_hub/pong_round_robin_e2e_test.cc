// Pong's round robins, end to end through the generated client: chess's
// rules on pong's tables. A pairing's table plays its one game on the
// round robin's variant, ticked here by hand, and that game is the
// pairing's result.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/libs/pong/table.h"

namespace games_hub {
namespace {

using moonbase::games::GameCommands;
using moonbase::games::PongMove;
using moonbase::games::PongRoundRobin;

GameCommands CreatePongRoundRobin(std::vector<std::string> entrants,
                                  std::optional<std::string> variant = std::nullopt) {
  moonbase::games::PongCreateRoundRobin create;
  create.entrants = std::move(entrants);
  create.variant = std::move(variant);
  return Pong(PongMove::FromCreateroundrobin(create));
}

GameCommands PlayPongPairing(const std::string& round_robin_id, const std::string& opponent) {
  moonbase::games::ChessPlayRoundRobin play;
  play.roundRobinId = round_robin_id;
  play.opponent = opponent;
  return Pong(PongMove::FromPlayroundrobin(play));
}

GameCommands PongForfeit(const std::string& round_robin_id, const std::string& winner,
                         const std::string& loser) {
  moonbase::games::ChessForfeit forfeit;
  forfeit.roundRobinId = round_robin_id;
  forfeit.winner = winner;
  forfeit.loser = loser;
  return Pong(PongMove::FromForfeit(forfeit));
}

GameCommands PongRoundRobins() {
  return Pong(PongMove::FromRoundrobins(moonbase::games::ChessRoundRobinsRequest{}));
}

GameCommands PongPaddle(double y) {
  moonbase::games::PongPaddle paddle;
  paddle.y = y;
  return Pong(PongMove::FromPaddle(paddle));
}

class PongRoundRobinFixture : public GamesHubStreamFixture {
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
  std::optional<PongRoundRobin> HeardWhere(Seat& seat, Pred pred) {
    for (int i = 0; i < 8; ++i) {
      auto update = ReceivePong(seat.stream, "roundRobin");
      if (!update.has_value()) return std::nullopt;
      if (pred(*update->as_roundRobin_or_null())) return *update->as_roundRobin_or_null();
    }
    return std::nullopt;
  }

  // Seat 0 creates a pong round robin of everyone seated; every member has
  // heard it.
  std::optional<PongRoundRobin> Created(Members& members,
                                        std::optional<std::string> variant = std::nullopt) {
    if (!members.room.seats[0].stream.Send(CreatePongRoundRobin(members.ids, variant)).ok()) {
      return std::nullopt;
    }
    std::optional<PongRoundRobin> created;
    for (Seat& seat : members.room.seats) {
      created = HeardWhere(seat, [](const auto&) { return true; });
      if (!created.has_value()) return std::nullopt;
    }
    return created;
  }

  // The next refusal `seat` hears, past however many frames a game left
  // queued ahead of it.
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
};

TEST_F(PongRoundRobinFixture, CreatingOneTellsTheRoomItsVariantPairingsAndStandings) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto created = Created(*members, "3d");
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->creator, members->ids[0]);
  EXPECT_EQ(created->variant, "3d");
  EXPECT_EQ(created->entrants, members->ids);
  EXPECT_EQ(created->pairings.size(), 3u);
  for (const auto& pairing : created->pairings) {
    EXPECT_FALSE(pairing.winner.has_value());
    EXPECT_FALSE(pairing.forfeit);
  }
  ASSERT_EQ(created->standings.size(), 3u);
  for (const auto& standing : created->standings) EXPECT_EQ(standing.points, 0);
}

TEST_F(PongRoundRobinFixture, AnUnknownVariantIsRefused) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  ASSERT_TRUE(members->room.seats[0].stream.Send(CreatePongRoundRobin(members->ids, "chess")).ok());
  const auto reason = Refused(members->room.seats[0]);
  ASSERT_TRUE(reason.has_value());
  EXPECT_THAT(*reason, ::testing::HasSubstr("unknown pong variant"));
}

// The pairing's table waits for the opponent, starts on the round robin's
// variant when they sit, plays its one game, and scores the pairing.
TEST_F(PongRoundRobinFixture, APairingsTablePlaysOneGameAndScoresIt) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto created = Created(*members, "3d");
  ASSERT_TRUE(created.has_value());
  Seat& alice = members->room.seats[0];
  Seat& bob = members->room.seats[1];
  ASSERT_TRUE(alice.stream.Send(PlayPongPairing(created->roundRobinId, members->ids[1])).ok());
  auto joined = ReceivePong(alice.stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  const auto& waiting = joined->as_gameJoined_or_null()->view;
  EXPECT_EQ(waiting.roundRobinId, created->roundRobinId);
  EXPECT_EQ(waiting.phase, "waiting");

  moonbase::games::JoinGame join;
  join.gameId = waiting.gameId;
  ASSERT_TRUE(bob.stream.Send(Pong(PongMove::FromJoingame(join))).ok());
  const auto playing = AwaitPongView(
      bob.stream, [](const auto& view) { return view.phase == "playing"; }, "the pairing's game");
  ASSERT_TRUE(playing.has_value());
  EXPECT_EQ(playing->variant, "3d") << "the round robin's, not the join's";
  EXPECT_EQ(playing->roundRobinId, created->roundRobinId);

  // bob's paddle in the corner: he misses every serve.
  ASSERT_TRUE(bob.stream.Send(PongPaddle(0)).ok());
  ASSERT_TRUE(bob.stream.Send(GameCommands::FromGetroomstate({})).ok());
  ASSERT_TRUE(ReceiveCase(bob.stream, "roomState").has_value());
  for (int tick = 0; tick < 4000 && golf_->PongRalliesHosted() > 0; ++tick) {
    golf_->TickPongOnce();
  }
  EXPECT_EQ(golf_->PongRalliesHosted(), 0u);
  const auto scored = HeardWhere(members->room.seats[2], [](const PongRoundRobin& heard) {
    for (const auto& pairing : heard.pairings) {
      if (pairing.winner.has_value()) return true;
    }
    return false;
  });
  ASSERT_TRUE(scored.has_value());
  int decided = 0;
  for (const auto& pairing : scored->pairings) {
    if (!pairing.winner.has_value()) continue;
    ++decided;
    EXPECT_EQ(*pairing.winner, members->ids[0]);
    EXPECT_FALSE(pairing.forfeit);
  }
  EXPECT_EQ(decided, 1);
  EXPECT_EQ(scored->standings[0].playerId, members->ids[0]);
  EXPECT_EQ(scored->standings[0].points, 1);

  // One game: no next one at a pairing's table.
  ASSERT_TRUE(alice.stream.Send(Pong(PongMove::FromStartgame({}))).ok());
  const auto reason = Refused(alice);
  ASSERT_TRUE(reason.has_value());
  EXPECT_EQ(*reason, "a round robin's table plays one game");
}

TEST_F(PongRoundRobinFixture, APairingsTableTakesNoBot) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  Seat& alice = members->room.seats[0];
  ASSERT_TRUE(alice.stream.Send(PlayPongPairing(created->roundRobinId, members->ids[1])).ok());
  ASSERT_TRUE(ReceivePong(alice.stream, "gameJoined").has_value());
  moonbase::games::PongAddBot add;
  add.level = 2;
  ASSERT_TRUE(alice.stream.Send(Pong(PongMove::FromAddbot(add))).ok());
  const auto reason = Refused(alice);
  ASSERT_TRUE(reason.has_value());
  EXPECT_EQ(*reason, "a round robin's pairing is played by its pair");
}

TEST_F(PongRoundRobinFixture, TheCreatorRecordsAForfeit) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  ASSERT_TRUE(members->room.seats[0]
                  .stream.Send(PongForfeit(created->roundRobinId, members->ids[1], members->ids[2]))
                  .ok());
  const auto forfeited = HeardWhere(members->room.seats[1], [](const PongRoundRobin& heard) {
    for (const auto& pairing : heard.pairings) {
      if (pairing.forfeit) return true;
    }
    return false;
  });
  ASSERT_TRUE(forfeited.has_value());
  for (const auto& pairing : forfeited->pairings) {
    if (!pairing.forfeit) continue;
    EXPECT_EQ(pairing.winner, members->ids[1]);
  }
}

TEST_F(PongRoundRobinFixture, TheCreatorWithdrawsAnEntrant) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  moonbase::games::ChessWithdraw withdraw;
  withdraw.roundRobinId = created->roundRobinId;
  withdraw.playerId = members->ids[2];
  ASSERT_TRUE(members->room.seats[0].stream.Send(Pong(PongMove::FromWithdraw(withdraw))).ok());
  const auto withdrawn = HeardWhere(
      members->room.seats[1], [](const PongRoundRobin& heard) { return !heard.withdrawn.empty(); });
  ASSERT_TRUE(withdrawn.has_value());
  EXPECT_THAT(withdrawn->withdrawn, ::testing::ElementsAre(members->ids[2]));
  int voided = 0;
  for (const auto& pairing : withdrawn->pairings) voided += pairing.voided ? 1 : 0;
  EXPECT_EQ(voided, 2);
}

// Each game's envelope serves its own round robins: pong's roundRobins
// lists pong's alone, and pong's moves refuse a chess round robin.
TEST_F(PongRoundRobinFixture, EachEnvelopeServesItsOwnGamesRoundRobins) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  Seat& alice = members->room.seats[0];
  ASSERT_TRUE(alice.stream.Send(CreateRoundRobin(members->ids)).ok());
  auto chess = ReceiveChess(alice.stream, "roundRobin");
  ASSERT_TRUE(chess.has_value());
  const std::string chess_id = chess->as_roundRobin_or_null()->roundRobinId;
  const auto pong = Created(*members, "3d");
  ASSERT_TRUE(pong.has_value());

  ASSERT_TRUE(alice.stream.Send(PongRoundRobins()).ok());
  auto listed = ReceivePong(alice.stream, "roundRobins");
  ASSERT_TRUE(listed.has_value());
  const auto& round_robins = listed->as_roundRobins_or_null()->roundRobins;
  ASSERT_EQ(round_robins.size(), 1u);
  EXPECT_EQ(round_robins[0].roundRobinId, pong->roundRobinId);

  ASSERT_TRUE(alice.stream.Send(PongForfeit(chess_id, members->ids[1], members->ids[2])).ok());
  const auto reason = Refused(alice);
  ASSERT_TRUE(reason.has_value());
  EXPECT_EQ(*reason, "that round robin plays chess");
  ASSERT_TRUE(alice.stream.Send(PlayPongPairing(chess_id, members->ids[1])).ok());
  const auto play_reason = Refused(alice);
  ASSERT_TRUE(play_reason.has_value());
  EXPECT_EQ(*play_reason, "that round robin plays chess");
}

}  // namespace
}  // namespace games_hub
