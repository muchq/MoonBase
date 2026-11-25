// Display names on the room stream: a player's chosen name rides beside
// their id in roomState; the id stays their identity.

#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "domains/games/apis/games_hub/stream_test_fixture.h"

namespace games_hub {
namespace {

using moonbase::games::GameCommands;
using moonbase::games::RoomState;

GameCommands SetName(const std::string& name) {
  moonbase::games::SetName set;
  set.name = name;
  return GameCommands::FromSetname(std::move(set));
}

// `player_id`'s name as `state` lists it; nullopt when absent, and a
// failure when the player is not listed at all.
std::optional<std::string> NameIn(const RoomState& state, const std::string& player_id) {
  for (const auto& player : state.players) {
    if (player.playerId == player_id) return player.name;
  }
  ADD_FAILURE() << player_id << " is not in the room";
  return std::nullopt;
}

// The first roomState listing `player_id` under `name` (nullopt for none):
// a seat may still hold the roomStates of earlier joins.
std::optional<RoomState> AwaitNamed(moonbase::games::PlayClientStream& stream,
                                    const std::string& player_id,
                                    const std::optional<std::string>& name) {
  return AwaitRoomState(
      stream,
      [&](const RoomState& state) {
        for (const auto& player : state.players) {
          if (player.playerId == player_id) return player.name == name;
        }
        return false;
      },
      player_id + " named " + name.value_or("<none>"));
}

class DisplayNameFixture : public GamesHubStreamFixture {};

TEST_F(DisplayNameFixture, ANameSetInARoomShowsToEveryMemberBesideTheId) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& ann = room->seats[0];
  Seat& bob = room->seats[1];
  ASSERT_TRUE(ann.stream.Send(SetName("  Ann  ")).ok());
  for (Seat* seat : {&ann, &bob}) {
    auto state = AwaitNamed(seat->stream, ann.player_id, "Ann");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(NameIn(*state, bob.player_id), std::nullopt);
  }
}

TEST_F(DisplayNameFixture, ANameSetBeforeJoiningCarriesIntoTheRoom) {
  auto ann = OpenSeat();
  ASSERT_TRUE(ann.has_value());
  ASSERT_TRUE(ReceiveCase(ann->stream, "sessionReady").has_value());
  ASSERT_TRUE(ann->stream.Send(SetName("Ann")).ok());
  ASSERT_TRUE(ann->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto state = ReceiveCase(ann->stream, "roomState");
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(NameIn(*state->as_roomState_or_null(), ann->player_id), "Ann");
}

TEST_F(DisplayNameFixture, AnEmptyNameClearsIt) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& ann = room->seats[0];
  ASSERT_TRUE(ann.stream.Send(SetName("Ann")).ok());
  ASSERT_TRUE(AwaitNamed(ann.stream, ann.player_id, "Ann").has_value());
  ASSERT_TRUE(ann.stream.Send(SetName("")).ok());
  EXPECT_TRUE(AwaitNamed(ann.stream, ann.player_id, std::nullopt).has_value());
}

TEST_F(DisplayNameFixture, TwoMembersMayShareAName) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  for (Seat& seat : room->seats) ASSERT_TRUE(seat.stream.Send(SetName("Sam")).ok());
  auto state = AwaitNamed(room->seats[0].stream, room->seats[1].player_id, "Sam");
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(NameIn(*state, room->seats[0].player_id), "Sam");
}

TEST_F(DisplayNameFixture, ANameTheRulesRefuseIsRejectedAndChangesNothing) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& ann = room->seats[0];
  ASSERT_TRUE(ann.stream.Send(SetName("Ann")).ok());
  ASSERT_TRUE(AwaitNamed(ann.stream, ann.player_id, "Ann").has_value());
  const std::vector<std::pair<std::string, std::string>> refusals = {
      {std::string(25, 'a'), "a name is at most 24 characters"},
      {"microgpt", "that name is a bot's"},
  };
  for (const auto& [refused, reason] : refusals) {
    ASSERT_TRUE(ann.stream.Send(SetName(refused)).ok());
    auto rejected = ReceiveCase(ann.stream, "commandRejected");
    ASSERT_TRUE(rejected.has_value()) << refused;
    EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, reason) << refused;
  }
  ASSERT_TRUE(ann.stream.Send(GameCommands::FromGetroomstate({})).ok());
  auto state = ReceiveCase(ann.stream, "roomState");
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(NameIn(*state->as_roomState_or_null(), ann.player_id), "Ann");
}

// The name is the room's record of the member, so another instance (or a
// restart) lists it too.
TEST_F(DisplayNameFixture, AnotherInstanceListsTheStoredName) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& ann = room->seats[0];
  ASSERT_TRUE(ann.stream.Send(SetName("Ann")).ok());
  ASSERT_TRUE(AwaitNamed(ann.stream, ann.player_id, "Ann").has_value());
  store_->Flush();

  auto rows = store_->LoadRoom(room->room_id);
  ASSERT_TRUE(rows.ok());
  ASSERT_EQ(rows->members.size(), 1u);
  EXPECT_EQ(rows->members[0].display_name, "Ann");

  auto instance = BuildSecondInstance(vault_, store_, chat_store_);
  ASSERT_NE(instance, nullptr);
  auto resumed = OpenSeatVia(*instance->client, ann.resume_token);
  ASSERT_TRUE(resumed.has_value());
  auto state = ReceiveCase(resumed->stream, "roomState");
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(NameIn(*state->as_roomState_or_null(), ann.player_id), "Ann");
}

// A rename on one instance reaches a member seated on another, even on
// the catch-up after a reconnect, which re-projects only rows that moved.
TEST_F(DisplayNameFixture, ARenameReachesAMemberOnAnotherInstance) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& ann = room->seats[0];
  auto instance = BuildSecondInstance(vault_, store_, chat_store_);
  ASSERT_NE(instance, nullptr);
  auto bob = OpenSeatVia(*instance->client, room->seats[1].resume_token);
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(AwaitNamed(bob->stream, ann.player_id, std::nullopt).has_value());

  ASSERT_TRUE(ann.stream.Send(SetName("Ann")).ok());
  ASSERT_TRUE(AwaitNamed(ann.stream, ann.player_id, "Ann").has_value());
  store_->Flush();
  // A reconnect's catch-up projects only rows that moved: a rename must
  // count as moving.
  instance->hub->OnChannelActive(RoomChannel(room->room_id));
  EXPECT_TRUE(AwaitNamed(bob->stream, ann.player_id, "Ann").has_value());
}

}  // namespace
}  // namespace games_hub
