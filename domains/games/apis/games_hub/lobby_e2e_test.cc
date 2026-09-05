// The lobby member of the room stream (#1490 phase 3), driven through the
// generated client over the in-memory pair: the world is the session's
// room's, or the plaza's while unroomed; a room change and a closed
// socket leave it; a roomId on this stream can only agree with that.
// The world's own rules (bounds, join-before-move, leave-first, fan-out)
// are world_test's, on the World itself.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/hub_store.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/apis/games_hub/world.h"

namespace games_hub {
namespace {

using moonbase::games::GameCommands;
using moonbase::games::LobbyAction;

GameCommands JoinWorld(std::optional<std::string> room_id = std::nullopt) {
  moonbase::games::JoinWorld join;
  join.roomId = std::move(room_id);
  join.position = {10, 0, -5};
  join.color = {0.8, 0.2, 0.6};
  join.shape = 0;
  return Lobby(LobbyAction::FromJoin(std::move(join)));
}

GameCommands MoveTo(std::vector<double> position) {
  moonbase::games::MoveTo move;
  move.position = std::move(position);
  return Lobby(LobbyAction::FromMove(std::move(move)));
}

GameCommands LeaveWorld() { return Lobby(LobbyAction::FromLeave(moonbase::games::LeaveWorld{})); }

// On the seam fixture so the race test below can arm the hub's hooks;
// unarmed, the seams are no-ops.
class LobbyFixture : public SeamFixture {
 protected:
  // A seat past its sessionReady, in no room and no world.
  std::optional<Seat> Arrive() {
    auto seat = OpenSeat();
    if (!seat.has_value()) return std::nullopt;
    if (!ReceiveCase(seat->stream, "sessionReady").has_value()) return std::nullopt;
    return seat;
  }
};

std::string NextRejection(moonbase::games::PlayClientStream& stream) {
  auto event = ReceiveCase(stream, "commandRejected");
  if (!event.has_value()) return "<no event>";
  return event->as_commandRejected_or_null()->reason;
}

// The ids a worldState lists.
std::vector<std::string> Listed(const std::optional<moonbase::games::LobbyUpdate>& update) {
  std::vector<std::string> ids;
  if (!update.has_value() || update->as_worldState_or_null() == nullptr) return {"<no worldState>"};
  for (const auto& player : update->as_worldState_or_null()->players)
    ids.push_back(player.playerId);
  return ids;
}

TEST_F(LobbyFixture, AnUnroomedSessionStandsInThePlazaAndARoomedOneInItsRoom) {
  auto alice = Arrive();
  auto bob = Arrive();
  auto carol = Arrive();
  ASSERT_TRUE(alice.has_value() && bob.has_value() && carol.has_value());

  // Unroomed joins share the plaza.
  ASSERT_TRUE(alice->stream.Send(JoinWorld()).ok());
  EXPECT_EQ(Listed(ReceiveLobby(alice->stream, "worldState")), std::vector<std::string>{});
  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  EXPECT_EQ(Listed(ReceiveLobby(bob->stream, "worldState")),
            std::vector<std::string>{alice->player_id});
  auto joined = ReceiveLobby(alice->stream, "playerJoined");
  ASSERT_TRUE(joined.has_value());
  EXPECT_EQ(joined->as_playerJoined_or_null()->player.playerId, bob->player_id);

  // A roomed join stands in the room's world: carol sees nobody, and the
  // plaza hears nothing of her.
  ASSERT_TRUE(carol->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(carol->stream, "roomState").has_value());
  ASSERT_TRUE(carol->stream.Send(JoinWorld()).ok());
  EXPECT_EQ(Listed(ReceiveLobby(carol->stream, "worldState")), std::vector<std::string>{});
  ASSERT_TRUE(carol->stream.Send(MoveTo({1, 0, 1})).ok());
  ExpectNoEvent(alice->stream);
  ExpectNoEvent(bob->stream);

  // And a plaza move stays in the plaza, counted on the lobby's own
  // series.
  ASSERT_TRUE(alice->stream.Send(MoveTo({15, 0, -8})).ok());
  auto moved = ReceiveLobby(bob->stream, "playerMoved");
  ASSERT_TRUE(moved.has_value());
  EXPECT_EQ(moved->as_playerMoved_or_null()->playerId, alice->player_id);
  ExpectNoEvent(carol->stream);
  EXPECT_EQ(metrics_->CounterTotal("lobby_commands", {{"command", "move"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("lobby_events", {{"event", "playerMoved"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("lobby_events", {{"event", "worldState"}}), 3);
}

// The world is the session's room's: a roomId can only agree with it.
// The world's own refusals (move before join, leave when out) reach the
// room stream as commandRejected like any other.
TEST_F(LobbyFixture, ARoomIdOnTheRoomStreamCanOnlyNameTheSessionsWorld) {
  auto alice = Arrive();
  auto bob = Arrive();
  ASSERT_TRUE(alice.has_value() && bob.has_value());

  ASSERT_TRUE(alice->stream.Send(MoveTo({1, 0, 1})).ok());
  EXPECT_EQ(NextRejection(alice->stream), "join the world first");
  ASSERT_TRUE(alice->stream.Send(LeaveWorld()).ok());
  EXPECT_EQ(NextRejection(alice->stream), "not in the world");
  ASSERT_TRUE(alice->stream.Send(JoinWorld("ABC123")).ok());
  EXPECT_EQ(NextRejection(alice->stream), "the world is your room's; join the room first");
  ASSERT_TRUE(alice->stream.Send(JoinWorld(std::string(World::kPlaza))).ok());
  EXPECT_EQ(Listed(ReceiveLobby(alice->stream, "worldState")), std::vector<std::string>{});

  ASSERT_TRUE(bob->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;
  ASSERT_TRUE(bob->stream.Send(JoinWorld(std::string(World::kPlaza))).ok());
  EXPECT_EQ(NextRejection(bob->stream), "the world is your room's; join the room first");
  ASSERT_TRUE(bob->stream.Send(JoinWorld(room_id)).ok());
  EXPECT_EQ(Listed(ReceiveLobby(bob->stream, "worldState")), std::vector<std::string>{});
  EXPECT_EQ(metrics_->CounterTotal("hub_rejections", {{"kind", "state"}}), 4);
}

// A room change leaves the world behind: the plaza hears the leaver go,
// the new room's world starts empty for them, and leaving the room puts
// them back in the plaza on their next join.
TEST_F(LobbyFixture, ChangingRoomsLeavesTheWorldBehind) {
  auto alice = Arrive();
  auto bob = Arrive();
  auto carol = Arrive();
  ASSERT_TRUE(alice.has_value() && bob.has_value() && carol.has_value());
  ASSERT_TRUE(alice->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "worldState").has_value());
  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(bob->stream, "worldState").has_value());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "playerJoined").has_value());
  ASSERT_TRUE(carol->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(carol->stream, "worldState").has_value());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "playerJoined").has_value());
  ASSERT_TRUE(ReceiveLobby(bob->stream, "playerJoined").has_value());

  // createRoom: bob is out of the plaza before his roomState.
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;
  auto left = ReceiveLobby(alice->stream, "playerLeft");
  ASSERT_TRUE(left.has_value());
  EXPECT_EQ(left->as_playerLeft_or_null()->playerId, bob->player_id);
  ASSERT_TRUE(ReceiveLobby(carol->stream, "playerLeft").has_value());
  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  EXPECT_EQ(Listed(ReceiveLobby(bob->stream, "worldState")), std::vector<std::string>{});

  // joinRoom: carol follows, out of the plaza and into bob's world.
  moonbase::games::JoinRoom join_room;
  join_room.roomId = room_id;
  ASSERT_TRUE(carol->stream.Send(GameCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(carol->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(carol->stream, "roomChatHistory").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  left = ReceiveLobby(alice->stream, "playerLeft");
  ASSERT_TRUE(left.has_value());
  EXPECT_EQ(left->as_playerLeft_or_null()->playerId, carol->player_id);
  ASSERT_TRUE(carol->stream.Send(JoinWorld()).ok());
  EXPECT_EQ(Listed(ReceiveLobby(carol->stream, "worldState")),
            std::vector<std::string>{bob->player_id});
  ASSERT_TRUE(ReceiveLobby(bob->stream, "playerJoined").has_value());

  // leaveRoom: bob is out of the room's world (carol hears it) and, on
  // his next join, back in the plaza with alice.
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromLeaveroom(moonbase::games::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomLeft").has_value());
  // Carol hears the world before the room: playerLeft, then roomState,
  // the order they changed.
  auto first = NextEvent(carol->stream);
  ASSERT_TRUE(first.has_value());
  ASSERT_NE(first->as_lobby_or_null(), nullptr);
  ASSERT_NE(first->as_lobby_or_null()->update.as_playerLeft_or_null(), nullptr);
  EXPECT_EQ(first->as_lobby_or_null()->update.as_playerLeft_or_null()->playerId, bob->player_id);
  auto second = NextEvent(carol->stream);
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(second->as_roomState_or_null(), nullptr);
  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  EXPECT_EQ(Listed(ReceiveLobby(bob->stream, "worldState")),
            std::vector<std::string>{alice->player_id});
  ASSERT_TRUE(ReceiveLobby(alice->stream, "playerJoined").has_value());
  ExpectNoEvent(carol->stream);
}

// Presence is the socket: a close leaves the world at once while the
// seat parks for grace, and the resumed session joins afresh — the world
// hears an arrival, not a return.
TEST_F(LobbyFixture, AClosedSocketLeavesTheWorldWhileTheSeatParks) {
  auto alice = Arrive();
  auto bob = Arrive();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(alice->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "worldState").has_value());
  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(bob->stream, "worldState").has_value());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "playerJoined").has_value());

  alice->stream.Close();
  auto left = ReceiveLobby(bob->stream, "playerLeft");
  ASSERT_TRUE(left.has_value());
  EXPECT_EQ(left->as_playerLeft_or_null()->playerId, alice->player_id);

  auto back = OpenSeat(alice->resume_token);
  ASSERT_TRUE(back.has_value());
  auto ready = ReceiveCase(back->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  ASSERT_TRUE(back->stream.Send(MoveTo({1, 0, 1})).ok());
  EXPECT_EQ(NextRejection(back->stream), "join the world first");
  ASSERT_TRUE(back->stream.Send(JoinWorld()).ok());
  EXPECT_EQ(Listed(ReceiveLobby(back->stream, "worldState")),
            std::vector<std::string>{bob->player_id});
  auto rejoined = ReceiveLobby(bob->stream, "playerJoined");
  ASSERT_TRUE(rejoined.has_value());
  EXPECT_EQ(rejoined->as_playerJoined_or_null()->player.playerId, alice->player_id);
  ExpectNoEvent(bob->stream);
}

// A sibling instance's write can drop a member from a room without the
// member's own leave ever passing through here — a reap on the sibling's
// boot cohort (#1295's residue) is one. The wake that mirrors the drop
// takes the member's world standing with it, as it does for a deleted
// room: peers hear playerLeft, and the member's next join — the plaza's
// now — is a fresh arrival, not "already in the world".
TEST_F(LobbyFixture, ASiblingsMemberDropLeavesTheWorldToo) {
  auto alice = Arrive();
  auto bob = Arrive();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;
  moonbase::games::JoinRoom join_room;
  join_room.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());
  ASSERT_TRUE(alice->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "worldState").has_value());
  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(bob->stream, "worldState").has_value());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "playerJoined").has_value());

  store_->Enqueue({HubStore::DeleteMember{room_id, bob->player_id}});
  golf_->OnNotify(RoomChannel(room_id), "sibling");
  auto left = ReceiveLobby(alice->stream, "playerLeft");
  ASSERT_TRUE(left.has_value());
  EXPECT_EQ(left->as_playerLeft_or_null()->playerId, bob->player_id);

  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  EXPECT_EQ(Listed(ReceiveLobby(bob->stream, "worldState")), std::vector<std::string>{});
}

// A game join whose id is not local refreshes the room first, and that
// refresh can mirror a sibling's member drop. The join's own answer —
// here a refusal, the game never existed — does not decide whether the
// leave the refresh staged reaches the peers.
TEST_F(LobbyFixture, AWorldLeaveStagedByARefreshOutlivesTheJoinsRefusal) {
  auto alice = Arrive();
  auto bob = Arrive();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;
  moonbase::games::JoinRoom join_room;
  join_room.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());
  ASSERT_TRUE(alice->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "worldState").has_value());
  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(bob->stream, "worldState").has_value());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "playerJoined").has_value());

  // The sibling's drop, unannounced: alice's join to a game nobody made
  // is what reads it.
  store_->Enqueue({HubStore::DeleteMember{room_id, bob->player_id}});
  moonbase::games::JoinGame join_game;
  join_game.gameId = "NOSUCH";
  ASSERT_TRUE(alice->stream.Send(Move(moonbase::games::GolfMove::FromJoingame(join_game))).ok());
  // The leave first — it is the room's truth, ahead of the join's answer.
  auto left = ReceiveLobby(alice->stream, "playerLeft");
  ASSERT_TRUE(left.has_value());
  EXPECT_EQ(left->as_playerLeft_or_null()->playerId, bob->player_id);
  EXPECT_EQ(NextRejection(alice->stream), "game not found");
}

// A room a sibling deleted takes its members' world standing with it:
// the members leave one by one, so whoever is still in when the other
// goes hears one playerLeft, and both stand free for the plaza.
TEST_F(LobbyFixture, ASiblingsRoomDeleteLeavesTheWorldToo) {
  auto alice = Arrive();
  auto bob = Arrive();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;
  moonbase::games::JoinRoom join_room;
  join_room.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());
  ASSERT_TRUE(alice->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "worldState").has_value());
  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(bob->stream, "worldState").has_value());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "playerJoined").has_value());

  store_->Enqueue({HubStore::DeleteRoom{room_id}});
  golf_->OnNotify(RoomChannel(room_id), "sibling");
  // Which of the two hears it is the members' order; that one of them
  // does is the fan-out.
  EXPECT_EQ(metrics_->CounterTotal("lobby_events", {{"event", "playerLeft"}}), 1);
  ASSERT_TRUE(alice->stream.Send(JoinWorld()).ok());
  EXPECT_EQ(Listed(ReceiveLobby(alice->stream, "worldState")), std::vector<std::string>{});
  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  EXPECT_EQ(Listed(ReceiveLobby(bob->stream, "worldState")),
            std::vector<std::string>{alice->player_id});
}

// The close path at its seam: the world entry goes and playerLeft fans
// out while the seat is still held, and only then does the seat park.
class LobbyRaceFixture : public LobbyFixture {
 protected:
  GolfTestHooks MakeGolfHooks() override {
    GolfTestHooks hooks;
    hooks.before_seat_release = [this](const std::string&) { Park("seat"); };
    return hooks;
  }
};

// A resume admitted the instant the seat frees must find the world
// already clean: were the seat released first, the resume's join could
// be refused as "already in the world", or its fresh entry erased by the
// old frame's leave.
TEST_F(LobbyRaceFixture, AClosedSocketLeavesTheWorldBeforeTheSeatParks) {
  auto alice = Arrive();
  auto bob = Arrive();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(alice->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "worldState").has_value());
  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(bob->stream, "worldState").has_value());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "playerJoined").has_value());

  Arm();
  Trigger closer(*this, [&] { alice->stream.Close(); });
  ASSERT_TRUE(WaitParked("seat"));

  // Inside the seam: the world has already told bob, and the seat is
  // still alice's and live — a resume now is the modeled SeatConflict.
  auto left = ReceiveLobby(bob->stream, "playerLeft");
  ASSERT_TRUE(left.has_value());
  EXPECT_EQ(left->as_playerLeft_or_null()->playerId, alice->player_id);
  auto conflicted = OpenSeat(alice->resume_token);
  ASSERT_TRUE(conflicted.has_value());
  auto refused = conflicted->stream.Receive();
  ASSERT_FALSE(refused.ok()) << "the seat was released before the world was cleaned";
  EXPECT_EQ(refused.error().code(), "SeatConflict");
  closer.Join();

  // Past the seam the seat parks, and the resume's join is a fresh
  // arrival: bob hears one playerJoined, nothing is refused.
  auto back = OpenSeat(alice->resume_token);
  ASSERT_TRUE(back.has_value());
  auto ready = ReceiveCase(back->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed) << "the seat was reaped, not parked";
  ASSERT_TRUE(back->stream.Send(JoinWorld()).ok());
  EXPECT_EQ(Listed(ReceiveLobby(back->stream, "worldState")),
            std::vector<std::string>{bob->player_id});
  auto rejoined = ReceiveLobby(bob->stream, "playerJoined");
  ASSERT_TRUE(rejoined.has_value());
  EXPECT_EQ(rejoined->as_playerJoined_or_null()->player.playerId, alice->player_id);
  ExpectNoEvent(bob->stream);
}

// Grace expiry after a close finds the world already left: whoever
// remains heard one playerLeft at the close and nothing at the reap.
class ShortGraceLobbyFixture : public LobbyFixture {
 protected:
  std::chrono::seconds GracePeriod() override { return std::chrono::seconds(1); }
};

TEST_F(ShortGraceLobbyFixture, GraceExpiryAfterACloseLeavesNothingMoreToLeave) {
  auto alice = Arrive();
  auto bob = Arrive();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(alice->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "worldState").has_value());
  ASSERT_TRUE(bob->stream.Send(JoinWorld()).ok());
  ASSERT_TRUE(ReceiveLobby(bob->stream, "worldState").has_value());
  ASSERT_TRUE(ReceiveLobby(alice->stream, "playerJoined").has_value());

  alice->stream.Close();
  ASSERT_TRUE(ReceiveLobby(bob->stream, "playerLeft").has_value());
  // A budget past the grace: the seat expires while bob listens, and he
  // hears nothing more.
  ExpectNoEvent(bob->stream, std::chrono::seconds(2));
  EXPECT_EQ(metrics_->CounterTotal("hub_seats_expired", {}), 1);
  EXPECT_EQ(metrics_->CounterTotal("lobby_events", {{"event", "playerLeft"}}), 1);
}

}  // namespace
}  // namespace games_hub
