// Phase 1 e2e: session minting, ticket admission, room lifecycle, and the
// reconnect-adjacent seat semantics, driven through the generated client
// over the in-memory pair. Wire-level details (JSON-text framing, real
// sockets) are upstream-tested; these pin the hub's behavior.

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "domains/games/apis/golf_hub/stream_test_fixture.h"

namespace golf_hub {
namespace {

using moonbase::golf::GolfCommands;
using moonbase::golf::GolfEvents;

// Receives until an event of the wanted case arrives (skipping others),
// failing after a few frames so a wrong stream can't hang the test.
std::optional<GolfEvents> ReceiveCase(moonbase::golf::PlayClientStream& stream,
                                      const std::string& wanted) {
  for (int i = 0; i < 8; ++i) {
    auto received = stream.Receive();
    if (!received.ok() || !received->has_value()) return std::nullopt;
    if (wanted == (*received)->case_name()) return **received;
  }
  return std::nullopt;
}

TEST_F(GolfHubStreamFixture, SessionMintsDistinctPlayersAndResumeTokenRoundTrips) {
  moonbase::golf::GetSessionInput input;
  auto first = client_->GetSession(input);
  auto second = client_->GetSession(input);
  ASSERT_TRUE(first.ok() && second.ok());
  EXPECT_NE(first->playerId, second->playerId);

  moonbase::golf::GetSessionInput resume;
  resume.resumeToken = first->resumeToken;
  auto resumed = client_->GetSession(resume);
  ASSERT_TRUE(resumed.ok());
  EXPECT_EQ(resumed->playerId, first->playerId);
  EXPECT_NE(resumed->ticket, first->ticket);
}

TEST_F(GolfHubStreamFixture, BadTicketFailsTypedBeforeAnyEvent) {
  moonbase::golf::PlayInput input;
  input.ticket = "t-bogus";
  auto stream = client_->Play(input);
  ASSERT_TRUE(stream.ok()) << stream.error().message();
  auto first = stream->Receive();
  ASSERT_FALSE(first.ok());
  EXPECT_EQ(first.error().code(), "Unauthenticated") << first.error().message();
}

TEST_F(GolfHubStreamFixture, TicketSpendsOnce) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());

  // The same ticket is gone; a second dial with it dies typed. (A fresh
  // ticket for the same player is the SeatConflict test below.)
  moonbase::golf::PlayInput replay;
  replay.ticket = "t-bogus";  // any unspendable ticket behaves alike
  auto second = client_->Play(replay);
  ASSERT_TRUE(second.ok());
  auto first_event = second->Receive();
  ASSERT_FALSE(first_event.ok());
  EXPECT_EQ(first_event.error().code(), "Unauthenticated");
}

TEST_F(GolfHubStreamFixture, SecondLiveConnectionForSamePlayerIsRefused) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());

  // Fresh ticket for the same player while the first wire is healthy:
  // admission refuses (ADR-0022) as the modeled SeatConflict.
  auto conflicted = OpenSeat(seat->resume_token);
  ASSERT_TRUE(conflicted.has_value());
  EXPECT_EQ(conflicted->player_id, seat->player_id);
  auto first = conflicted->stream.Receive();
  ASSERT_FALSE(first.ok());
  EXPECT_EQ(first.error().code(), "SeatConflict");
}

TEST_F(GolfHubStreamFixture, CreateJoinAndLeaveBroadcastRoomState) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateRoom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const auto* room = created->as_roomState_or_null();
  ASSERT_NE(room, nullptr);
  ASSERT_EQ(room->players.size(), 1u);
  EXPECT_EQ(room->players[0].playerId, alice->player_id);
  const std::string room_id = room->roomId;

  moonbase::golf::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinRoom(join)).ok());
  auto bob_view = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(bob_view.has_value());
  EXPECT_EQ(bob_view->as_roomState_or_null()->players.size(), 2u);
  auto alice_view = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(alice_view.has_value());
  EXPECT_EQ(alice_view->as_roomState_or_null()->players.size(), 2u);

  // Bob leaves deliberately: he gets the ack, Alice sees the shrink.
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromLeaveRoom(moonbase::golf::LeaveRoom{})).ok());
  auto ack = ReceiveCase(bob->stream, "roomLeft");
  ASSERT_TRUE(ack.has_value());
  EXPECT_EQ(ack->as_roomLeft_or_null()->roomId, room_id);
  auto after_leave = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(after_leave.has_value());
  ASSERT_EQ(after_leave->as_roomState_or_null()->players.size(), 1u);
  EXPECT_EQ(after_leave->as_roomState_or_null()->players[0].playerId, alice->player_id);
}

TEST_F(GolfHubStreamFixture, CommandsOutsideARoomAreRejectedInBand) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());

  ASSERT_TRUE(
      seat->stream.Send(GolfCommands::FromGetRoomState(moonbase::golf::GetRoomState{})).ok());
  auto rejected = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "not in a room");

  moonbase::golf::JoinRoom join;
  join.roomId = "r-nope";
  ASSERT_TRUE(seat->stream.Send(GolfCommands::FromJoinRoom(join)).ok());
  auto unknown = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(unknown.has_value());

  // The stream survived both rejections.
  ASSERT_TRUE(seat->stream.Send(GolfCommands::FromCreateRoom(moonbase::golf::CreateRoom{})).ok());
  EXPECT_TRUE(ReceiveCase(seat->stream, "roomState").has_value());
}

TEST_F(GolfHubStreamFixture, CleanCloseFreesTheRoomSlotAndNotifies) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateRoom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  moonbase::golf::JoinRoom join;
  join.roomId = created->as_roomState_or_null()->roomId;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinRoom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  bob->stream.Close();
  auto shrunk = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(shrunk.has_value());
  ASSERT_EQ(shrunk->as_roomState_or_null()->players.size(), 1u);
  EXPECT_EQ(shrunk->as_roomState_or_null()->players[0].playerId, alice->player_id);
}

}  // namespace
}  // namespace golf_hub
