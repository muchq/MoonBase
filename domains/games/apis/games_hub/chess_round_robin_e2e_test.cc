// Round robins in a room (#1647), end to end through the generated
// client: creating one, the creator's forfeits and withdrawals, and what
// every member hears.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"

namespace games_hub {
namespace {

using moonbase::games::ChessRoundRobin;
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

}  // namespace
}  // namespace games_hub
