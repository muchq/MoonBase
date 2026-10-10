// Round robins in a room (#1647), end to end through the generated
// client: creating one, the creator's forfeits and withdrawals, and what
// every member hears.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/random/random.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/libs/chess_play/game_state.h"

namespace games_hub {
namespace {

using moonbase::games::ChessMove;
using moonbase::games::ChessRoundRobin;
using moonbase::games::GameCommands;
using ::testing::ElementsAre;

class RoundRobinFixture : public GamesHubStreamFixture {
 protected:
  // A room of `count`, and the ids of its members in seat order.
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

  std::optional<ChessRoundRobin> Heard(Seat& seat) {
    auto update = ReceiveChess(seat.stream, "roundRobin");
    if (!update.has_value()) return std::nullopt;
    return *update->as_roundRobin_or_null();
  }

  // Receives roundRobin updates until one satisfies `pred`.
  template <typename Pred>
  std::optional<ChessRoundRobin> HeardWhere(Seat& seat, Pred pred) {
    for (int i = 0; i < 8; ++i) {
      auto heard = Heard(seat);
      if (!heard.has_value()) return std::nullopt;
      if (pred(*heard)) return heard;
    }
    return std::nullopt;
  }

  // The table seat `opener` opens for its pairing with `opponent`, once
  // every member has heard the pairing at it.
  std::optional<std::string> Opened(Members& members, int opener, int opponent,
                                    const std::string& round_robin_id) {
    Seat& seat = members.room.seats[opener];
    if (!seat.stream.Send(PlayRoundRobin(round_robin_id, members.ids[opponent])).ok()) {
      return std::nullopt;
    }
    auto joined = ReceiveChess(seat.stream, "gameJoined");
    if (!joined.has_value()) return std::nullopt;
    const std::string game_id = joined->as_gameJoined_or_null()->view.gameId;
    for (Seat& member : members.room.seats) {
      if (!HeardWhere(member, [&](const ChessRoundRobin& heard) {
             for (const auto& pairing : heard.pairings) {
               if (pairing.gameId == game_id) return true;
             }
             return false;
           }).has_value()) {
        return std::nullopt;
      }
    }
    return game_id;
  }

  std::optional<std::string> Refused(Seat& seat) {
    auto refused = ReceiveCase(seat.stream, "commandRejected");
    if (!refused.has_value()) return std::nullopt;
    return refused->as_commandRejected_or_null()->reason;
  }

  // Seat 0 creates a round robin of everyone seated; every member has
  // heard it.
  std::optional<ChessRoundRobin> Created(Members& members) {
    if (!members.room.seats[0].stream.Send(CreateRoundRobin(members.ids)).ok()) {
      return std::nullopt;
    }
    std::optional<ChessRoundRobin> created;
    for (Seat& seat : members.room.seats) {
      created = Heard(seat);
      if (!created.has_value()) return std::nullopt;
    }
    return created;
  }
};

std::vector<std::pair<std::string, std::string>> Pairs(const ChessRoundRobin& round_robin) {
  std::vector<std::pair<std::string, std::string>> pairs;
  for (const auto& pairing : round_robin.pairings) pairs.emplace_back(pairing.white, pairing.black);
  return pairs;
}

// Creating a round robin tells the whole room: its creator, entrants and
// terms, the pairings in Berger order, and everyone level before a game.
TEST_F(RoundRobinFixture, CreatingOneTellsTheRoomItsPairingsAndStandings) {
  auto members = Seated(4);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  moonbase::games::ChessStartGame terms;
  terms.initialSeconds = 300;
  terms.incrementSeconds = 3;
  ASSERT_TRUE(members->room.seats[1].stream.Send(CreateRoundRobin(ids, terms)).ok());
  for (Seat& seat : members->room.seats) {
    auto heard = Heard(seat);
    ASSERT_TRUE(heard.has_value()) << seat.player_id;
    EXPECT_FALSE(heard->roundRobinId.empty());
    EXPECT_EQ(heard->creator, ids[1]);
    EXPECT_EQ(heard->entrants, ids);
    EXPECT_EQ(heard->terms.setupId, "standard");
    EXPECT_EQ(heard->terms.initialSeconds, 300);
    EXPECT_EQ(heard->terms.incrementSeconds, 3);
    EXPECT_THAT(Pairs(*heard), ElementsAre(std::pair{ids[0], ids[3]}, std::pair{ids[1], ids[2]},
                                           std::pair{ids[3], ids[1]}, std::pair{ids[2], ids[0]},
                                           std::pair{ids[2], ids[3]}, std::pair{ids[0], ids[1]}));
    EXPECT_EQ(heard->pairings[0].round, 1);
    EXPECT_EQ(heard->pairings[5].round, 3);
    EXPECT_FALSE(heard->pairings[0].result.has_value());
    EXPECT_TRUE(heard->withdrawn.empty());
    ASSERT_EQ(heard->standings.size(), 4u);
    for (const auto& standing : heard->standings) {
      EXPECT_EQ(standing.place, 1);
      EXPECT_EQ(standing.points, 0);
    }
  }
}

TEST_F(RoundRobinFixture, CreatingRefusesAFieldThatIsNotTheRoomsOrNotItsSize) {
  auto members = Seated(4);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  Seat& creator = members->room.seats[0];
  ASSERT_TRUE(creator.stream.Send(CreateRoundRobin({ids[0], ids[1]})).ok());
  EXPECT_EQ(Refused(creator), "a round robin takes 3 to 8 entrants");
  ASSERT_TRUE(creator.stream.Send(CreateRoundRobin({ids[0], ids[1], ids[1]})).ok());
  EXPECT_EQ(Refused(creator), absl::StrCat(ids[1], " is entered twice"));
  ASSERT_TRUE(creator.stream.Send(CreateRoundRobin({ids[0], ids[1], "stranger"})).ok());
  EXPECT_EQ(Refused(creator), "stranger is not in the room");
  moonbase::games::ChessStartGame terms;
  terms.setupId = "no-such-setup";
  ASSERT_TRUE(creator.stream.Send(CreateRoundRobin(ids, terms)).ok());
  EXPECT_EQ(Refused(creator), "unknown chess setup: no-such-setup");
}

TEST_F(RoundRobinFixture, CreatingOutsideARoomIsRefused) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());
  ASSERT_TRUE(seat->stream.Send(CreateRoundRobin({"a", "b", "c"})).ok());
  EXPECT_EQ(Refused(*seat), "not in a room");
}

// A room runs round robins side by side, a new one while another has
// pairings to play, up to GolfHub::kMaxRoundRobinsPerRoom.
TEST_F(RoundRobinFixture, ARoomRunsSeveralUpToItsLimit) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  Seat& creator = members->room.seats[1];
  for (std::size_t i = 0; i < GolfHub::kMaxRoundRobinsPerRoom; ++i) {
    ASSERT_TRUE(creator.stream.Send(CreateRoundRobin(members->ids)).ok());
    ASSERT_TRUE(Heard(creator).has_value()) << i;
  }
  ASSERT_TRUE(creator.stream.Send(CreateRoundRobin(members->ids)).ok());
  EXPECT_EQ(Refused(creator), absl::StrCat("a room keeps at most ", GolfHub::kMaxRoundRobinsPerRoom,
                                           " round robins"));
  Seat& asker = members->room.seats[0];
  ASSERT_TRUE(asker.stream.Send(RoundRobins()).ok());
  // Behind the creates' roundRobin frames, more than one receive skips.
  std::optional<moonbase::games::ChessUpdate> answer;
  for (int i = 0; i < 3 && !answer.has_value(); ++i)
    answer = ReceiveChess(asker.stream, "roundRobins");
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(answer->as_roundRobins_or_null()->roundRobins.size(), GolfHub::kMaxRoundRobinsPerRoom);
}

// The creator records a forfeit on a pairing still to play: it scores as
// a win, marked forfeit, and every member hears the new standings. A
// decided pairing can't be forfeited again.
TEST_F(RoundRobinFixture, AForfeitScoresAWinForTheWholeRoomToSee) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  Seat& creator = members->room.seats[0];
  ASSERT_TRUE(creator.stream.Send(Forfeit(created->roundRobinId, ids[2], ids[1])).ok());
  for (Seat& seat : members->room.seats) {
    auto heard = Heard(seat);
    ASSERT_TRUE(heard.has_value()) << seat.player_id;
    int forfeits = 0;
    for (const auto& pairing : heard->pairings) {
      if (!pairing.forfeit) continue;
      ++forfeits;
      EXPECT_EQ(pairing.result, pairing.white == ids[2] ? "white" : "black");
      EXPECT_TRUE((pairing.white == ids[2] && pairing.black == ids[1]) ||
                  (pairing.white == ids[1] && pairing.black == ids[2]));
    }
    EXPECT_EQ(forfeits, 1);
    ASSERT_EQ(heard->standings.size(), 3u);
    EXPECT_EQ(heard->standings[0].playerId, ids[2]);
    EXPECT_EQ(heard->standings[0].points, 1);
    EXPECT_EQ(heard->standings[0].place, 1);
  }
  ASSERT_TRUE(creator.stream.Send(Forfeit(created->roundRobinId, ids[1], ids[2])).ok());
  EXPECT_EQ(Refused(creator),
            absl::StrCat(ids[1], " and ", ids[2], " have no pairing still to play"));
}

// Withdrawing an entrant voids their pairings still to play, and the
// player stays in the standings with what they scored.
TEST_F(RoundRobinFixture, AWithdrawalVoidsWhatIsLeftAndKeepsWhatWasScored) {
  auto members = Seated(4);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  Seat& creator = members->room.seats[0];
  ASSERT_TRUE(creator.stream.Send(Forfeit(created->roundRobinId, ids[3], ids[2])).ok());
  ASSERT_TRUE(Heard(creator).has_value());
  ASSERT_TRUE(creator.stream.Send(Withdraw(created->roundRobinId, ids[3])).ok());
  auto heard = Heard(members->room.seats[1]);
  ASSERT_TRUE(heard.has_value());
  heard = Heard(members->room.seats[1]);  // the forfeit's, then the withdrawal's
  ASSERT_TRUE(heard.has_value());
  EXPECT_THAT(heard->withdrawn, ElementsAre(ids[3]));
  for (const auto& pairing : heard->pairings) {
    const bool theirs = pairing.white == ids[3] || pairing.black == ids[3];
    EXPECT_EQ(pairing.voided, theirs && !pairing.result.has_value())
        << pairing.white << "-" << pairing.black;
  }
  bool found = false;
  for (const auto& standing : heard->standings) {
    if (standing.playerId != ids[3]) continue;
    found = true;
    EXPECT_TRUE(standing.withdrawn);
    EXPECT_EQ(standing.points, 1);
  }
  EXPECT_TRUE(found);

  ASSERT_TRUE(creator.stream.Send(Forfeit(created->roundRobinId, ids[3], ids[1])).ok());
  EXPECT_EQ(Refused(creator),
            absl::StrCat(ids[3], " and ", ids[1], " have no pairing still to play"));
  ASSERT_TRUE(creator.stream.Send(Withdraw(created->roundRobinId, ids[3])).ok());
  EXPECT_EQ(Refused(creator), absl::StrCat(ids[3], " has already withdrawn"));
  ASSERT_TRUE(creator.stream.Send(Withdraw(created->roundRobinId, "stranger")).ok());
  EXPECT_EQ(Refused(creator), "stranger is not entered");
}

TEST_F(RoundRobinFixture, OnlyTheCreatorModerates) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  Seat& other = members->room.seats[1];
  ASSERT_TRUE(other.stream.Send(Withdraw(created->roundRobinId, ids[2])).ok());
  EXPECT_EQ(Refused(other), "only the round robin's creator can do that");
  ASSERT_TRUE(other.stream.Send(Forfeit(created->roundRobinId, ids[1], ids[2])).ok());
  EXPECT_EQ(Refused(other), "only the round robin's creator can do that");
  ASSERT_TRUE(other.stream.Send(Forfeit("NOSUCH", ids[1], ids[2])).ok());
  EXPECT_EQ(Refused(other), "no such round robin in this room");
}

// Any member asks for the room's round robins: one that joined after the
// round robin was created hears it this way.
TEST_F(RoundRobinFixture, AMemberAsksForTheRoomsRoundRobins) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  Seat& asker = members->room.seats[2];
  ASSERT_TRUE(asker.stream.Send(RoundRobins()).ok());
  auto answer = ReceiveChess(asker.stream, "roundRobins");
  ASSERT_TRUE(answer.has_value());
  const auto& listed = answer->as_roundRobins_or_null()->roundRobins;
  ASSERT_EQ(listed.size(), 1u);
  EXPECT_EQ(listed[0].roundRobinId, created->roundRobinId);
  EXPECT_EQ(listed[0].entrants, members->ids);
  // Created with no terms: a challenge's defaults.
  EXPECT_EQ(listed[0].terms.setupId, "standard");
  EXPECT_EQ(listed[0].terms.initialSeconds, 180);
  EXPECT_EQ(listed[0].terms.incrementSeconds, 2);
}

// The creator's change lands on the version stored, not the one held: a
// newer one written elsewhere is adopted (and heard), and the change is
// made over it.
TEST_F(RoundRobinFixture, AChangeOverANewerStoredVersionAdoptsItFirst) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  auto stored = store_->LoadRoom(members->room.room_id);
  ASSERT_TRUE(stored.ok());
  ASSERT_EQ(stored->events.size(), 1u);
  HubStore::ChessEventRow elsewhere = stored->events[0];
  elsewhere.version = 2;
  elsewhere.withdrawn = {ids[2]};
  ASSERT_TRUE(*store_->CommitChessEvent(elsewhere, ""));

  Seat& creator = members->room.seats[0];
  ASSERT_TRUE(creator.stream.Send(Forfeit(created->roundRobinId, ids[0], ids[1])).ok());
  auto adopted = Heard(members->room.seats[1]);
  ASSERT_TRUE(adopted.has_value());
  EXPECT_THAT(adopted->withdrawn, ElementsAre(ids[2]));
  auto changed = Heard(members->room.seats[1]);
  ASSERT_TRUE(changed.has_value());
  EXPECT_THAT(changed->withdrawn, ElementsAre(ids[2]));
  EXPECT_EQ(changed->standings[0].playerId, ids[0]);
  EXPECT_EQ(changed->standings[0].points, 1);
  EXPECT_EQ(store_->LoadRoom(members->room.room_id)->events[0].version, 3);
}

const moonbase::games::ChessPairing* PairingOf(const ChessRoundRobin& round_robin,
                                               const std::string& a, const std::string& b) {
  for (const auto& pairing : round_robin.pairings) {
    if ((pairing.white == a && pairing.black == b) || (pairing.white == b && pairing.black == a)) {
      return &pairing;
    }
  }
  return nullptr;
}

GameCommands JoinTable(const std::string& game_id) {
  moonbase::games::JoinGame join;
  join.gameId = game_id;
  return Chess(moonbase::games::ChessMove::FromJoingame(join));
}

// A pairing is played at a table tagged with it: the round robin's
// terms, the pairing's colours, a start when the opponent sits, and the
// game's result is the pairing's for the whole room.
TEST_F(RoundRobinFixture, APairingPlayedAtItsTableScoresForTheRoom) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  const auto* pairing = PairingOf(*created, ids[0], ids[1]);
  ASSERT_NE(pairing, nullptr);
  const std::string white = pairing->white;
  const std::string black = pairing->black;
  // Black opens it, White fills it: the colours are the pairing's, not
  // the seats'.
  const int black_seat = black == ids[0] ? 0 : 1;
  const int white_seat = 1 - black_seat;

  auto game_id = Opened(*members, black_seat, white_seat, created->roundRobinId);
  ASSERT_TRUE(game_id.has_value());
  Seat& opponent = members->room.seats[white_seat];
  ASSERT_TRUE(opponent.stream.Send(JoinTable(*game_id)).ok());
  auto playing = AwaitChessView(
      opponent.stream, [](const auto& view) { return view.phase == "playing"; }, "the start");
  ASSERT_TRUE(playing.has_value());
  EXPECT_EQ(playing->roundRobinId, created->roundRobinId);
  EXPECT_FALSE(playing->terms.has_value());
  ASSERT_TRUE(playing->clock.has_value());
  EXPECT_EQ(playing->clock->initialMs, 180'000);
  EXPECT_EQ(playing->clock->incrementMs, 2'000);
  for (const auto& player : playing->players) {
    EXPECT_EQ(player.color, player.playerId == white ? "white" : "black") << player.playerId;
  }

  Seat& resigner = members->room.seats[black_seat];
  ASSERT_TRUE(resigner.stream.Send(Chess(ChessMove::FromResign({}))).ok());
  // Still the round robin's once over: its table plays no next game.
  auto ended = AwaitChessView(
      opponent.stream, [](const auto& view) { return view.phase == "ended"; }, "the end");
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->roundRobinId, created->roundRobinId);
  for (Seat& seat : members->room.seats) {
    auto decided = HeardWhere(seat, [&](const ChessRoundRobin& heard) {
      const auto* played = PairingOf(heard, ids[0], ids[1]);
      return played != nullptr && played->result.has_value();
    });
    ASSERT_TRUE(decided.has_value()) << seat.player_id;
    const auto* played = PairingOf(*decided, ids[0], ids[1]);
    EXPECT_EQ(played->result, "white");
    EXPECT_FALSE(played->forfeit);
    EXPECT_FALSE(played->gameId.has_value()) << "its table is done";
    EXPECT_EQ(decided->standings[0].playerId, white);
    EXPECT_EQ(decided->standings[0].points, 1);
  }
}

// Only the paired opponent fills a pairing's table, nothing changes its
// terms or seats a bot, and it plays the one game.
TEST_F(RoundRobinFixture, APairingsTableIsForItsPairAndOneGame) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  auto game_id = Opened(*members, 0, 1, created->roundRobinId);
  ASSERT_TRUE(game_id.has_value());
  Seat& opener = members->room.seats[0];
  Seat& stranger = members->room.seats[2];

  ASSERT_TRUE(stranger.stream.Send(JoinTable(*game_id)).ok());
  EXPECT_EQ(Refused(stranger), absl::StrCat("that table is for ", ids[1]));
  moonbase::games::ChessAddBot bot;
  bot.elo = 1500;
  ASSERT_TRUE(opener.stream.Send(Chess(ChessMove::FromAddbot(bot))).ok());
  EXPECT_EQ(Refused(opener), "a round robin's pairing is played by its pair");
  ASSERT_TRUE(opener.stream.Send(Chess(ChessMove::FromChallenge({}))).ok());
  EXPECT_EQ(Refused(opener), "a round robin's table plays on its terms");

  Seat& opponent = members->room.seats[1];
  ASSERT_TRUE(opponent.stream.Send(JoinTable(*game_id)).ok());
  ASSERT_TRUE(
      AwaitChessView(
          opponent.stream, [](const auto& view) { return view.phase == "playing"; }, "the start")
          .has_value());
  ASSERT_TRUE(opponent.stream.Send(Chess(ChessMove::FromResign({}))).ok());
  ASSERT_TRUE(AwaitChessView(
                  opener.stream, [](const auto& view) { return view.phase == "ended"; }, "the end")
                  .has_value());
  ASSERT_TRUE(opener.stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
  EXPECT_EQ(Refused(opener), "a round robin's table plays one game");
}

// A table whose start failed is started by hand on the round robin's
// terms, whatever the start names.
TEST_F(RoundRobinFixture, AHandStartedPairingPlaysOnTheRoundRobinsTerms) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  auto game_id = Opened(*members, 0, 1, created->roundRobinId);
  ASSERT_TRUE(game_id.has_value());
  std::atomic<int> opened{0};
  golf_->SetChessOpener(
      [&opened](std::string_view setup_id) -> absl::StatusOr<chess_play::ChessSetup> {
        if (opened++ == 0) return absl::UnavailableError("the opener is down");
        absl::BitGen gen;
        return chess_play::SelectChessSetup(setup_id, gen);
      });
  Seat& opener = members->room.seats[0];
  Seat& opponent = members->room.seats[1];
  ASSERT_TRUE(opponent.stream.Send(JoinTable(*game_id)).ok());
  EXPECT_EQ(Refused(opponent), "the opener is down");

  moonbase::games::ChessStartGame other;
  other.setupId = std::string(chess_play::kRandomKpkSetup);
  other.initialSeconds = 60;
  other.incrementSeconds = 0;
  ASSERT_TRUE(opener.stream.Send(Chess(ChessMove::FromStartgame(other))).ok());
  auto playing = AwaitChessView(
      opponent.stream, [](const auto& view) { return view.phase == "playing"; }, "the start");
  ASSERT_TRUE(playing.has_value());
  EXPECT_EQ(playing->setupId, "standard");
  ASSERT_TRUE(playing->clock.has_value());
  EXPECT_EQ(playing->clock->initialMs, 180'000);
  EXPECT_EQ(playing->clock->incrementMs, 2'000);
}

// A pairing's waiting table keeps the round robin's terms through a
// leave: its pair, back at it, start on them.
TEST_F(RoundRobinFixture, APairingsTableKeepsItsTermsThroughALeave) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  auto game_id = Opened(*members, 0, 1, created->roundRobinId);
  ASSERT_TRUE(game_id.has_value());
  std::atomic<int> opened{0};
  golf_->SetChessOpener(
      [&opened](std::string_view setup_id) -> absl::StatusOr<chess_play::ChessSetup> {
        if (opened++ == 0) return absl::UnavailableError("the opener is down");
        absl::BitGen gen;
        return chess_play::SelectChessSetup(setup_id, gen);
      });
  Seat& opener = members->room.seats[0];
  Seat& opponent = members->room.seats[1];
  ASSERT_TRUE(opponent.stream.Send(JoinTable(*game_id)).ok());
  EXPECT_EQ(Refused(opponent), "the opener is down");
  ASSERT_TRUE(opener.stream.Send(Chess(ChessMove::FromLeavegame({}))).ok());
  ASSERT_TRUE(ReceiveChess(opener.stream, "gameLeft").has_value());

  ASSERT_TRUE(opener.stream.Send(JoinTable(*game_id)).ok());
  auto playing = AwaitChessView(
      opener.stream, [](const auto& view) { return view.phase == "playing"; }, "the start");
  ASSERT_TRUE(playing.has_value());
  ASSERT_TRUE(playing->clock.has_value());
  EXPECT_EQ(playing->clock->initialMs, 180'000);
  EXPECT_EQ(playing->clock->incrementMs, 2'000);
}

// A pairing's table its opener leaves before anyone sits is gone, and
// the room hears the pairing at no table.
TEST_F(RoundRobinFixture, APairingsTableLeftEmptyLeavesThePairingOpen) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  ASSERT_TRUE(Opened(*members, 0, 1, created->roundRobinId).has_value());
  Seat& opener = members->room.seats[0];
  ASSERT_TRUE(opener.stream.Send(Chess(ChessMove::FromLeavegame({}))).ok());
  for (Seat& seat : members->room.seats) {
    auto heard = Heard(seat);
    ASSERT_TRUE(heard.has_value()) << seat.player_id;
    const auto* pairing = PairingOf(*heard, ids[0], ids[1]);
    ASSERT_NE(pairing, nullptr);
    EXPECT_FALSE(pairing->gameId.has_value());
    EXPECT_FALSE(pairing->result.has_value());
  }
}

// A pairing's waiting table shows the round robin's terms, not the
// default ones.
TEST_F(RoundRobinFixture, APairingsWaitingTableShowsTheRoundRobinsTerms) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  moonbase::games::ChessStartGame terms;
  terms.initialSeconds = 300;
  terms.incrementSeconds = 3;
  ASSERT_TRUE(members->room.seats[0].stream.Send(CreateRoundRobin(ids, terms)).ok());
  std::optional<ChessRoundRobin> created;
  for (Seat& seat : members->room.seats) ASSERT_TRUE((created = Heard(seat)).has_value());
  Seat& opener = members->room.seats[0];
  ASSERT_TRUE(opener.stream.Send(PlayRoundRobin(created->roundRobinId, ids[1])).ok());
  auto joined = ReceiveChess(opener.stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  const auto& view = joined->as_gameJoined_or_null()->view;
  ASSERT_TRUE(view.terms.has_value());
  EXPECT_EQ(view.terms->initialSeconds, 300);
  EXPECT_EQ(view.terms->incrementSeconds, 3);
  EXPECT_EQ(view.roundRobinId, created->roundRobinId);
}

// A pairing is opened from no table, and only while it counts: not by a
// seat at another table, nor once withdrawal voided it.
TEST_F(RoundRobinFixture, APairingIsOpenedFromNoTableWhileItCounts) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  Seat& creator = members->room.seats[0];
  Seat& entrant = members->room.seats[1];
  ASSERT_TRUE(creator.stream.Send(Withdraw(created->roundRobinId, ids[2])).ok());
  ASSERT_TRUE(Heard(entrant).has_value());
  ASSERT_TRUE(entrant.stream.Send(PlayRoundRobin(created->roundRobinId, ids[2])).ok());
  EXPECT_EQ(Refused(entrant),
            absl::StrCat(ids[1], " and ", ids[2], " have no pairing still to play"));

  ASSERT_TRUE(
      entrant.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveChess(entrant.stream, "gameJoined").has_value());
  ASSERT_TRUE(entrant.stream.Send(PlayRoundRobin(created->roundRobinId, ids[0])).ok());
  EXPECT_EQ(Refused(entrant), "leave your current game first");
}

// A pairing being played isn't forfeited: its table decides it.
TEST_F(RoundRobinFixture, APairingAtATableIsNotForfeited) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  auto game_id = Opened(*members, 1, 2, created->roundRobinId);
  ASSERT_TRUE(game_id.has_value());
  Seat& creator = members->room.seats[0];
  ASSERT_TRUE(creator.stream.Send(Forfeit(created->roundRobinId, ids[1], ids[2])).ok());
  EXPECT_EQ(Refused(creator), absl::StrCat("that pairing is being played at ", *game_id));
}

// A player who leaves their pairing's game mid-play loses it, as at any
// table, and it scores as played.
TEST_F(RoundRobinFixture, LeavingAPairingsGameLosesIt) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  auto game_id = Opened(*members, 0, 1, created->roundRobinId);
  ASSERT_TRUE(game_id.has_value());
  Seat& leaver = members->room.seats[1];
  ASSERT_TRUE(leaver.stream.Send(JoinTable(*game_id)).ok());
  ASSERT_TRUE(
      AwaitChessView(
          leaver.stream, [](const auto& view) { return view.phase == "playing"; }, "the start")
          .has_value());
  ASSERT_TRUE(leaver.stream.Send(Chess(ChessMove::FromLeavegame({}))).ok());
  Seat& bystander = members->room.seats[2];
  auto decided = HeardWhere(bystander, [&](const ChessRoundRobin& heard) {
    const auto* played = PairingOf(heard, ids[0], ids[1]);
    return played != nullptr && played->result.has_value();
  });
  ASSERT_TRUE(decided.has_value());
  const auto* played = PairingOf(*decided, ids[0], ids[1]);
  EXPECT_EQ(played->result, played->white == ids[0] ? "white" : "black");
  EXPECT_FALSE(played->forfeit);
  EXPECT_FALSE(played->gameId.has_value());
}

// Opening a pairing's table ends a watch, as opening any table does.
TEST_F(RoundRobinFixture, OpeningAPairingsTableStopsTheWatch) {
  auto members = Seated(4);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  Seat& creator = members->room.seats[0];
  ASSERT_TRUE(creator.stream.Send(CreateRoundRobin({ids[0], ids[1], ids[2]})).ok());
  std::optional<ChessRoundRobin> created;
  for (Seat& seat : members->room.seats) ASSERT_TRUE((created = Heard(seat)).has_value());
  Seat& host = members->room.seats[0];
  Seat& guest = members->room.seats[3];
  ASSERT_TRUE(
      host.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto joined = ReceiveChess(host.stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  const std::string plain = joined->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(guest.stream.Send(JoinTable(plain)).ok());
  ASSERT_TRUE(ReceiveChess(guest.stream, "gameJoined").has_value());
  ASSERT_TRUE(host.stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
  ASSERT_TRUE(
      AwaitChessView(
          guest.stream, [](const auto& view) { return view.phase == "playing"; }, "the start")
          .has_value());

  Seat& watcher = members->room.seats[1];
  moonbase::games::ChessWatch watch;
  watch.gameId = plain;
  ASSERT_TRUE(watcher.stream.Send(Chess(ChessMove::FromWatch(watch))).ok());
  ASSERT_TRUE(ReceiveChess(watcher.stream, "gameState").has_value());
  ASSERT_TRUE(watcher.stream.Send(PlayRoundRobin(created->roundRobinId, ids[2])).ok());
  auto mine = ReceiveChess(watcher.stream, "gameJoined");
  ASSERT_TRUE(mine.has_value());
  const std::string table = mine->as_gameJoined_or_null()->view.gameId;

  ASSERT_TRUE(guest.stream.Send(Chess(ChessMove::FromResign({}))).ok());
  ASSERT_TRUE(AwaitChessView(
                  host.stream, [](const auto& view) { return view.phase == "ended"; }, "the end")
                  .has_value());
  while (true) {
    auto received = watcher.stream.Receive(std::chrono::milliseconds(300));
    if (!received.ok() || !received->has_value()) break;
    const auto* chess = (*received)->as_chess_or_null();
    if (chess == nullptr) continue;
    if (const auto* state = chess->update.as_gameState_or_null()) {
      EXPECT_EQ(state->view.gameId, table) << "the watched table, still heard";
    }
    EXPECT_EQ(chess->update.as_gameEnded_or_null(), nullptr) << "the watched table's end";
  }
}

// A withdrawal never voids a pairing being played: not one whose table
// the entrant sits at, and a table left waiting on a withdrawn entrant
// is filled by no one.
TEST_F(RoundRobinFixture, AWithdrawalVoidsNoPairingInPlay) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  auto game_id = Opened(*members, 1, 2, created->roundRobinId);
  ASSERT_TRUE(game_id.has_value());
  Seat& creator = members->room.seats[0];
  ASSERT_TRUE(creator.stream.Send(Withdraw(created->roundRobinId, ids[1])).ok());
  EXPECT_EQ(Refused(creator), absl::StrCat(ids[1], " is playing a pairing at ", *game_id));

  ASSERT_TRUE(creator.stream.Send(Withdraw(created->roundRobinId, ids[2])).ok());
  Seat& withdrawn = members->room.seats[2];
  auto heard = Heard(withdrawn);
  ASSERT_TRUE(heard.has_value());
  EXPECT_THAT(heard->withdrawn, ElementsAre(ids[2]));
  ASSERT_TRUE(withdrawn.stream.Send(JoinTable(*game_id)).ok());
  EXPECT_EQ(Refused(withdrawn), "that table's pairing is void");
}

// A pairing at a table can't be opened again, nor one already decided;
// and a played pairing can't be forfeited.
TEST_F(RoundRobinFixture, APairingAtATableOrDecidedIsNotOpenedAgain) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  auto game_id = Opened(*members, 0, 1, created->roundRobinId);
  ASSERT_TRUE(game_id.has_value());
  Seat& opponent = members->room.seats[1];
  ASSERT_TRUE(opponent.stream.Send(PlayRoundRobin(created->roundRobinId, ids[0])).ok());
  EXPECT_EQ(Refused(opponent), absl::StrCat("that pairing is being played at ", *game_id));
  Seat& outsider = members->room.seats[2];
  ASSERT_TRUE(outsider.stream.Send(PlayRoundRobin(created->roundRobinId, "stranger")).ok());
  EXPECT_EQ(Refused(outsider), absl::StrCat(ids[2], " and stranger have no pairing still to play"));

  ASSERT_TRUE(opponent.stream.Send(JoinTable(*game_id)).ok());
  ASSERT_TRUE(
      AwaitChessView(
          opponent.stream, [](const auto& view) { return view.phase == "playing"; }, "the start")
          .has_value());
  ASSERT_TRUE(opponent.stream.Send(Chess(ChessMove::FromResign({}))).ok());
  Seat& creator = members->room.seats[0];
  ASSERT_TRUE(HeardWhere(creator, [&](const ChessRoundRobin& heard) {
                const auto* played = PairingOf(heard, ids[0], ids[1]);
                return played != nullptr && played->result.has_value();
              }).has_value());
  ASSERT_TRUE(creator.stream.Send(Chess(ChessMove::FromLeavegame({}))).ok());
  ASSERT_TRUE(ReceiveChess(creator.stream, "gameLeft").has_value());
  ASSERT_TRUE(creator.stream.Send(PlayRoundRobin(created->roundRobinId, ids[1])).ok());
  EXPECT_EQ(Refused(creator),
            absl::StrCat(ids[0], " and ", ids[1], " have no pairing still to play"));
  ASSERT_TRUE(creator.stream.Send(Forfeit(created->roundRobinId, ids[1], ids[0])).ok());
  EXPECT_EQ(Refused(creator),
            absl::StrCat(ids[1], " and ", ids[0], " have no pairing still to play"));
}

// While the creator is out of the room, any entrant in it moderates
// what isn't their own; a member who isn't entered never does.
TEST_F(RoundRobinFixture, WhileTheCreatorIsAwayAnEntrantModerates) {
  auto members = Seated(4);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  Seat& creator = members->room.seats[0];
  ASSERT_TRUE(creator.stream.Send(CreateRoundRobin({ids[0], ids[1], ids[2]})).ok());
  auto created = Heard(creator);
  ASSERT_TRUE(created.has_value());
  for (int i = 1; i < 4; ++i) ASSERT_TRUE(Heard(members->room.seats[i]).has_value());

  Seat& entrant = members->room.seats[1];
  ASSERT_TRUE(entrant.stream.Send(Forfeit(created->roundRobinId, ids[1], ids[2])).ok());
  EXPECT_EQ(Refused(entrant), "only the round robin's creator can do that");

  ASSERT_TRUE(creator.stream.Send(GameCommands::FromLeaveroom(moonbase::games::LeaveRoom{})).ok());
  ASSERT_TRUE(AwaitRoomState(
                  entrant.stream,
                  [](const moonbase::games::RoomState& room) { return room.players.size() == 3; },
                  "the creator gone")
                  .has_value());
  Seat& bystander = members->room.seats[3];
  ASSERT_TRUE(bystander.stream.Send(Forfeit(created->roundRobinId, ids[1], ids[2])).ok());
  EXPECT_EQ(Refused(bystander),
            "only the round robin's entrants can do that while its creator is away");
  constexpr char kNotThemselves[] = "while its creator is away, entrants can't moderate themselves";
  ASSERT_TRUE(entrant.stream.Send(Forfeit(created->roundRobinId, ids[1], ids[2])).ok());
  EXPECT_EQ(Refused(entrant), kNotThemselves);
  ASSERT_TRUE(entrant.stream.Send(Forfeit(created->roundRobinId, ids[2], ids[1])).ok());
  EXPECT_EQ(Refused(entrant), kNotThemselves);
  ASSERT_TRUE(entrant.stream.Send(Withdraw(created->roundRobinId, ids[1])).ok());
  EXPECT_EQ(Refused(entrant), kNotThemselves);

  ASSERT_TRUE(entrant.stream.Send(Forfeit(created->roundRobinId, ids[2], ids[0])).ok());
  auto forfeited = Heard(bystander);
  ASSERT_TRUE(forfeited.has_value());
  EXPECT_EQ(forfeited->standings[0].playerId, ids[2]);
  EXPECT_EQ(forfeited->standings[0].points, 1);
  ASSERT_TRUE(entrant.stream.Send(Withdraw(created->roundRobinId, ids[0])).ok());
  auto withdrawn = Heard(bystander);
  ASSERT_TRUE(withdrawn.has_value());
  EXPECT_THAT(withdrawn->withdrawn, ElementsAre(ids[0]));
}

// The ordinary 1v1 table is untouched by a round robin in the room: two
// entrants who are paired can still open a plain table, post a challenge,
// play, and play the next game, and none of it is their pairing's result.
TEST_F(RoundRobinFixture, APlainTableBetweenPairedEntrantsIsAnOrdinaryGame) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  Seat& host = members->room.seats[0];
  Seat& guest = members->room.seats[1];

  ASSERT_TRUE(
      host.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto joined = ReceiveChess(host.stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  const std::string game_id = joined->as_gameJoined_or_null()->view.gameId;
  EXPECT_FALSE(joined->as_gameJoined_or_null()->view.roundRobinId.has_value());
  moonbase::games::ChessStartGame terms;
  terms.initialSeconds = 60;
  terms.incrementSeconds = 0;
  ASSERT_TRUE(host.stream.Send(Chess(ChessMove::FromChallenge(terms))).ok());
  ASSERT_TRUE(AwaitChessView(
                  host.stream, [](const auto& view) { return view.terms.has_value(); },
                  "the posted challenge")
                  .has_value());

  ASSERT_TRUE(guest.stream.Send(JoinTable(game_id)).ok());
  auto playing = AwaitChessView(
      guest.stream, [](const auto& view) { return view.phase == "playing"; }, "the start");
  ASSERT_TRUE(playing.has_value());
  ASSERT_TRUE(playing->clock.has_value());
  EXPECT_EQ(playing->clock->initialMs, 60'000) << "the challenge's terms, not the round robin's";
  ASSERT_TRUE(guest.stream.Send(Chess(ChessMove::FromResign({}))).ok());
  ASSERT_TRUE(AwaitChessView(
                  host.stream, [](const auto& view) { return view.phase == "ended"; }, "the end")
                  .has_value());
  ASSERT_TRUE(host.stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
  ASSERT_TRUE(
      AwaitChessView(
          guest.stream,
          [](const auto& view) { return view.phase == "playing" && view.scoreSheet.size() == 1; },
          "the next game")
          .has_value());

  Seat& asker = members->room.seats[2];
  ASSERT_TRUE(asker.stream.Send(RoundRobins()).ok());
  auto listed = ReceiveChess(asker.stream, "roundRobins");
  ASSERT_TRUE(listed.has_value());
  const auto& round_robin = listed->as_roundRobins_or_null()->roundRobins.at(0);
  const auto* pairing = PairingOf(round_robin, ids[0], ids[1]);
  ASSERT_NE(pairing, nullptr);
  EXPECT_FALSE(pairing->result.has_value()) << "a plain game is not the pairing's";
  EXPECT_FALSE(pairing->gameId.has_value());
  for (const auto& standing : round_robin.standings) EXPECT_EQ(standing.points, 0);
}

// A pairing's table is watched like any chess table: the watcher sees it
// from waiting to its result, and the pairing still scores.
TEST_F(RoundRobinFixture, AWatcherFollowsAPairingsTableToItsResult) {
  auto members = Seated(3);
  ASSERT_TRUE(members.has_value());
  const auto& ids = members->ids;
  auto created = Created(*members);
  ASSERT_TRUE(created.has_value());
  auto game_id = Opened(*members, 0, 1, created->roundRobinId);
  ASSERT_TRUE(game_id.has_value());

  Seat& watcher = members->room.seats[2];
  moonbase::games::ChessWatch watch;
  watch.gameId = *game_id;
  ASSERT_TRUE(watcher.stream.Send(Chess(ChessMove::FromWatch(watch))).ok());
  auto waiting = ReceiveChess(watcher.stream, "gameState");
  ASSERT_TRUE(waiting.has_value());
  EXPECT_EQ(waiting->as_gameState_or_null()->view.phase, "waiting");
  ASSERT_TRUE(waiting->as_gameState_or_null()->view.terms.has_value());
  EXPECT_EQ(waiting->as_gameState_or_null()->view.terms->initialSeconds, 180);

  Seat& opponent = members->room.seats[1];
  ASSERT_TRUE(opponent.stream.Send(JoinTable(*game_id)).ok());
  ASSERT_TRUE(AwaitChessView(
                  watcher.stream, [](const auto& view) { return view.phase == "playing"; },
                  "the watcher seeing the start")
                  .has_value());
  ASSERT_TRUE(opponent.stream.Send(Chess(ChessMove::FromResign({}))).ok());
  auto ended = AwaitChessView(
      watcher.stream, [](const auto& view) { return view.result.has_value(); },
      "the watcher seeing the result");
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->result->winner, ids[0]);
  auto decided = HeardWhere(watcher, [&](const ChessRoundRobin& heard) {
    const auto* played = PairingOf(heard, ids[0], ids[1]);
    return played != nullptr && played->result.has_value();
  });
  ASSERT_TRUE(decided.has_value());
  EXPECT_EQ(decided->standings[0].playerId, ids[0]);
}

}  // namespace
}  // namespace games_hub
