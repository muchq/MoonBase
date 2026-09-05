// The thoughts flows (#79), driven through the generated client over the
// in-memory pair: join, move, shape, leave, the close-is-a-leave rule, a
// world per room with the plaza as the default (#1490), the world's bounds
// refused in-band, the ticket and seat contracts shared with golf, the
// stream budget, and the model pin for the thoughts_* series.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/apis/games_hub/thoughts_hub.h"
#include "domains/games/apis/games_hub/world.h"

namespace games_hub {
namespace {

using moonbase::games::ThoughtsCommands;
using moonbase::games::ThoughtsEvents;

ThoughtsCommands Join(std::vector<double> position, std::vector<double> color, std::int32_t shape) {
  moonbase::games::JoinWorld join;
  join.position = std::move(position);
  join.color = std::move(color);
  join.shape = shape;
  return ThoughtsCommands::FromJoin(std::move(join));
}

// The same join, into a named room's world.
ThoughtsCommands JoinIn(std::string room_id, std::vector<double> position,
                        std::vector<double> color, std::int32_t shape) {
  moonbase::games::JoinWorld join;
  join.roomId = std::move(room_id);
  join.position = std::move(position);
  join.color = std::move(color);
  join.shape = shape;
  return ThoughtsCommands::FromJoin(std::move(join));
}

ThoughtsCommands MoveTo(std::vector<double> position) {
  moonbase::games::MoveTo move;
  move.position = std::move(position);
  return ThoughtsCommands::FromMove(std::move(move));
}

ThoughtsCommands Shape(std::int32_t shape) {
  moonbase::games::ChangeShape change;
  change.shape = shape;
  return ThoughtsCommands::FromShape(std::move(change));
}

ThoughtsCommands Leave() { return ThoughtsCommands::FromLeave(moonbase::games::LeaveWorld{}); }

// The Go server's fixture player: (10, 0, -5), magenta-ish, a sphere.
ThoughtsCommands FixtureJoin() { return Join({10, 0, -5}, {0.8, 0.2, 0.6}, 0); }

// Puts a seat in the plaza and consumes its worldState, so the seat hears
// what follows; fails the test if the join is not answered with one.
void EnterPlaza(moonbase::games::ThinkClientStream& stream) {
  ASSERT_TRUE(stream.Send(Join({20, 0, 15}, {0.3, 0.9, 0.4}, 1)).ok());
  auto world = NextEvent(stream);
  ASSERT_TRUE(world.has_value());
  ASSERT_NE(world->as_worldState_or_null(), nullptr);
}

// The reason on the next event, which must be a commandRejected.
std::string NextRejection(moonbase::games::ThinkClientStream& stream) {
  auto event = NextEvent(stream);
  if (!event.has_value()) return "<no event>";
  const auto* rejected = event->as_commandRejected_or_null();
  if (rejected == nullptr) return std::string("<") + event->case_name() + ">";
  return rejected->reason;
}

TEST_F(GamesHubStreamFixture, JoinShowsTheJoinerTheWorldAndTellsEveryoneElse) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());

  // The first joiner sees an empty world — sent even when empty, so the
  // client knows it is synced rather than inferring it from silence.
  ASSERT_TRUE(alice->stream.Send(FixtureJoin()).ok());
  auto world = NextEvent(alice->stream);
  ASSERT_TRUE(world.has_value());
  ASSERT_NE(world->as_worldState_or_null(), nullptr);
  EXPECT_TRUE(world->as_worldState_or_null()->players.empty());

  // Bob has not joined, so he is in no world and hears nothing of alice.
  ExpectNoEvent(bob->stream);

  // Bob's join: his snapshot lists alice — as she stands, in full — and
  // not himself, and alice hears him arrive. Neither hears their own join.
  ASSERT_TRUE(bob->stream.Send(Join({20, 0, 15}, {0.3, 0.9, 0.4}, 1)).ok());
  auto bobs_world = NextEvent(bob->stream);
  ASSERT_TRUE(bobs_world.has_value());
  ASSERT_NE(bobs_world->as_worldState_or_null(), nullptr);
  ASSERT_EQ(bobs_world->as_worldState_or_null()->players.size(), 1u);
  const auto& player = bobs_world->as_worldState_or_null()->players[0];
  EXPECT_EQ(player.playerId, alice->player_id);
  EXPECT_EQ(player.position, (std::vector<double>{10, 0, -5}));
  EXPECT_EQ(player.color, (std::vector<double>{0.8, 0.2, 0.6}));
  EXPECT_EQ(player.shape, 0);
  auto alice_hears = NextEvent(alice->stream);
  ASSERT_TRUE(alice_hears.has_value());
  ASSERT_NE(alice_hears->as_playerJoined_or_null(), nullptr);
  const auto& arrived = alice_hears->as_playerJoined_or_null()->player;
  EXPECT_EQ(arrived.playerId, bob->player_id);
  EXPECT_EQ(arrived.position, (std::vector<double>{20, 0, 15}));
  EXPECT_EQ(arrived.color, (std::vector<double>{0.3, 0.9, 0.4}));
  EXPECT_EQ(arrived.shape, 1);
  ExpectNoEvent(alice->stream);
  ExpectNoEvent(bob->stream);
}

// A world per room: a join naming a room lands in that room's world, and
// every fan-out — join, move, shape, leave, a closed socket — stops at
// its edge. The room layer is not consulted: any id names a world.
TEST_F(GamesHubStreamFixture, AJoinNamingARoomLandsInThatRoomsWorldAndFansOutOnlyThere) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  auto carol = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value() && carol.has_value());

  ASSERT_TRUE(alice->stream.Send(JoinIn("ABC123", {10, 0, -5}, {0.8, 0.2, 0.6}, 0)).ok());
  ASSERT_NE(NextEvent(alice->stream).value().as_worldState_or_null(), nullptr);
  ASSERT_TRUE(bob->stream.Send(JoinIn("XYZ789", {1, 0, 1}, {0, 0, 1}, 1)).ok());
  auto bobs_world = NextEvent(bob->stream);
  ASSERT_TRUE(bobs_world.has_value());
  ASSERT_NE(bobs_world->as_worldState_or_null(), nullptr);
  EXPECT_TRUE(bobs_world->as_worldState_or_null()->players.empty())
      << "bob's world must not list alice, who is in another room";
  ExpectNoEvent(alice->stream);

  // Carol joins alice's room: her snapshot is alice alone, alice hears
  // her, bob hears nothing.
  ASSERT_TRUE(carol->stream.Send(JoinIn("ABC123", {2, 0, 2}, {0, 1, 0}, 2)).ok());
  auto carols_world = NextEvent(carol->stream);
  ASSERT_TRUE(carols_world.has_value());
  ASSERT_NE(carols_world->as_worldState_or_null(), nullptr);
  ASSERT_EQ(carols_world->as_worldState_or_null()->players.size(), 1u);
  EXPECT_EQ(carols_world->as_worldState_or_null()->players[0].playerId, alice->player_id);
  auto alice_hears = NextEvent(alice->stream);
  ASSERT_TRUE(alice_hears.has_value());
  ASSERT_NE(alice_hears->as_playerJoined_or_null(), nullptr);
  EXPECT_EQ(alice_hears->as_playerJoined_or_null()->player.playerId, carol->player_id);
  ExpectNoEvent(bob->stream);

  // Moves and shapes stay in the room.
  ASSERT_TRUE(alice->stream.Send(MoveTo({15, 0, -8})).ok());
  ASSERT_NE(NextEvent(carol->stream).value().as_playerMoved_or_null(), nullptr);
  ASSERT_TRUE(alice->stream.Send(Shape(1)).ok());
  ASSERT_NE(NextEvent(carol->stream).value().as_shapeChanged_or_null(), nullptr);
  ExpectNoEvent(bob->stream);

  // So do departures, deliberate or by a closed socket.
  ASSERT_TRUE(alice->stream.Send(Leave()).ok());
  auto left = NextEvent(carol->stream);
  ASSERT_TRUE(left.has_value());
  ASSERT_NE(left->as_playerLeft_or_null(), nullptr);
  EXPECT_EQ(left->as_playerLeft_or_null()->playerId, alice->player_id);
  ExpectNoEvent(bob->stream);
  carol->stream.Close();
  ExpectNoEvent(bob->stream);
  // Deliveries counted per room: alice's and bob's joins reached nobody,
  // carol's reached alice.
  EXPECT_EQ(metrics_->CounterTotal("thoughts_events", {{"event", "playerJoined"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("thoughts_events", {{"event", "playerLeft"}}), 1);
}

// The plaza is a well-known room: an unroomed join and one naming "plaza"
// share a world, so today's muchq.com/thoughts client, which names no
// room, keeps meeting everyone who lands there.
TEST_F(GamesHubStreamFixture, AnUnroomedJoinLandsInThePlaza) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(alice->stream.Send(FixtureJoin()).ok());
  ASSERT_NE(NextEvent(alice->stream).value().as_worldState_or_null(), nullptr);

  ASSERT_TRUE(bob->stream.Send(JoinIn("plaza", {1, 0, 1}, {0, 0, 1}, 1)).ok());
  auto world = NextEvent(bob->stream);
  ASSERT_TRUE(world.has_value());
  ASSERT_NE(world->as_worldState_or_null(), nullptr);
  ASSERT_EQ(world->as_worldState_or_null()->players.size(), 1u);
  EXPECT_EQ(world->as_worldState_or_null()->players[0].playerId, alice->player_id);
  auto joined = NextEvent(alice->stream);
  ASSERT_TRUE(joined.has_value());
  ASSERT_NE(joined->as_playerJoined_or_null(), nullptr);
  EXPECT_EQ(joined->as_playerJoined_or_null()->player.playerId, bob->player_id);
}

TEST_F(GamesHubStreamFixture, MovesAndShapesReachTheOthersAndNeverEchoAndStick) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  EnterPlaza(bob->stream);
  ASSERT_TRUE(alice->stream.Send(FixtureJoin()).ok());
  ASSERT_TRUE(NextEvent(alice->stream).has_value());  // worldState
  ASSERT_TRUE(NextEvent(bob->stream).has_value());    // playerJoined

  ASSERT_TRUE(alice->stream.Send(MoveTo({15, 0, -8})).ok());
  auto moved = NextEvent(bob->stream);
  ASSERT_TRUE(moved.has_value());
  ASSERT_NE(moved->as_playerMoved_or_null(), nullptr);
  EXPECT_EQ(moved->as_playerMoved_or_null()->playerId, alice->player_id);
  EXPECT_EQ(moved->as_playerMoved_or_null()->position, (std::vector<double>{15, 0, -8}));

  ASSERT_TRUE(alice->stream.Send(Shape(1)).ok());
  auto shaped = NextEvent(bob->stream);
  ASSERT_TRUE(shaped.has_value());
  ASSERT_NE(shaped->as_shapeChanged_or_null(), nullptr);
  EXPECT_EQ(shaped->as_shapeChanged_or_null()->playerId, alice->player_id);
  EXPECT_EQ(shaped->as_shapeChanged_or_null()->shape, 1);
  ExpectNoEvent(alice->stream);

  // The world remembers: a later joiner sees alice where she moved, as
  // what she became.
  auto carol = OpenThoughtsSeat();
  ASSERT_TRUE(carol.has_value());
  ASSERT_TRUE(carol->stream.Send(FixtureJoin()).ok());
  auto world = NextEvent(carol->stream);
  ASSERT_TRUE(world.has_value());
  ASSERT_NE(world->as_worldState_or_null(), nullptr);
  const auto& players = world->as_worldState_or_null()->players;
  ASSERT_EQ(players.size(), 2u);
  const auto alice_now = std::find_if(players.begin(), players.end(), [&](const auto& p) {
    return p.playerId == alice->player_id;
  });
  ASSERT_NE(alice_now, players.end());
  EXPECT_EQ(alice_now->position, (std::vector<double>{15, 0, -8}));
  EXPECT_EQ(alice_now->shape, 1);

  // thoughts_events counts deliveries, not commands: alice's join reached
  // one player, carol's reached two.
  EXPECT_EQ(metrics_->CounterTotal("thoughts_events", {{"event", "playerJoined"}}), 3);
  EXPECT_EQ(metrics_->CounterTotal("thoughts_events", {{"event", "playerMoved"}}), 1);
}

TEST_F(GamesHubStreamFixture, LeavingTellsTheOthersAndKeepsTheSessionForARejoin) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  EnterPlaza(bob->stream);
  ASSERT_TRUE(alice->stream.Send(FixtureJoin()).ok());
  ASSERT_TRUE(NextEvent(alice->stream).has_value());
  ASSERT_TRUE(NextEvent(bob->stream).has_value());

  ASSERT_TRUE(alice->stream.Send(Leave()).ok());
  auto left = NextEvent(bob->stream);
  ASSERT_TRUE(left.has_value());
  ASSERT_NE(left->as_playerLeft_or_null(), nullptr);
  EXPECT_EQ(left->as_playerLeft_or_null()->playerId, alice->player_id);
  // No ack to the leaver; the stream is simply open for a rejoin, which is
  // also how a color changes.
  ExpectNoEvent(alice->stream);
  ASSERT_TRUE(alice->stream.Send(Join({0, 0, 0}, {1, 1, 1}, 2)).ok());
  auto world = NextEvent(alice->stream);
  ASSERT_TRUE(world.has_value());
  ASSERT_NE(world->as_worldState_or_null(), nullptr);
  auto rejoined = NextEvent(bob->stream);
  ASSERT_TRUE(rejoined.has_value());
  ASSERT_NE(rejoined->as_playerJoined_or_null(), nullptr);
  EXPECT_EQ(rejoined->as_playerJoined_or_null()->player.color, (std::vector<double>{1, 1, 1}));

  // And how a room changes: leave, then join elsewhere. Bob, in the
  // plaza, hears her go and nothing after.
  ASSERT_TRUE(alice->stream.Send(Leave()).ok());
  ASSERT_NE(NextEvent(bob->stream).value().as_playerLeft_or_null(), nullptr);
  ASSERT_TRUE(alice->stream.Send(JoinIn("ABC123", {0, 0, 0}, {1, 1, 1}, 2)).ok());
  ASSERT_NE(NextEvent(alice->stream).value().as_worldState_or_null(), nullptr);
  ExpectNoEvent(bob->stream);
}

TEST_F(GamesHubStreamFixture, AClosedSocketIsALeaveWithNothingToResume) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  EnterPlaza(bob->stream);
  ASSERT_TRUE(alice->stream.Send(FixtureJoin()).ok());
  ASSERT_TRUE(NextEvent(alice->stream).has_value());
  ASSERT_TRUE(NextEvent(bob->stream).has_value());

  // A closed tab: no grace, so the others hear playerLeft at once, and
  // the seat is gone rather than parked.
  alice->stream.Close();
  auto left = NextEvent(bob->stream);
  ASSERT_TRUE(left.has_value());
  ASSERT_NE(left->as_playerLeft_or_null(), nullptr);
  EXPECT_EQ(left->as_playerLeft_or_null()->playerId, alice->player_id);
  EXPECT_EQ(metrics_->CounterTotal("thoughts_disconnects", {{"kind", "clean"}}), 1);

  // The resume token mints a fresh seat for the same player, admitted as
  // new (resumed false — the fixture asserts it) into a world that has
  // forgotten them: bob hears a join, not a return.
  auto back = OpenThoughtsSeat(alice->resume_token);
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->player_id, alice->player_id);
  ASSERT_TRUE(back->stream.Send(FixtureJoin()).ok());
  auto world = NextEvent(back->stream);
  ASSERT_TRUE(world.has_value());
  ASSERT_NE(world->as_worldState_or_null(), nullptr);
  ASSERT_EQ(world->as_worldState_or_null()->players.size(), 1u);
  EXPECT_EQ(world->as_worldState_or_null()->players[0].playerId, bob->player_id);
  auto rejoined = NextEvent(bob->stream);
  ASSERT_TRUE(rejoined.has_value());
  EXPECT_NE(rejoined->as_playerJoined_or_null(), nullptr);
}

// The world's rules, each refused in-band with a reason — and, the negative
// half, nothing changes: a later joiner sees only what was admitted.
TEST_F(GamesHubStreamFixture, TheWorldsBoundsAreRefusedInBandAndChangeNothing) {
  auto alice = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value());

  struct Case {
    const char* name;
    ThoughtsCommands command;
    const char* reason;
  };
  const Case refused[] = {
      {"x beyond the edge", Join({100, 0, -5}, {0.8, 0.2, 0.6}, 0), "position out of bounds (±50)"},
      {"z beyond the edge", Join({10, 0, -51}, {0.8, 0.2, 0.6}, 0), "position out of bounds (±50)"},
      {"off the ground plane", Join({10, 1, -5}, {0.8, 0.2, 0.6}, 0), "y must be 0"},
      {"two coordinates", Join({10, 0}, {0.8, 0.2, 0.6}, 0), "position must be [x, y, z]"},
      {"color above 1", Join({10, 0, -5}, {1.5, 0.2, 0.6}, 0),
       "color components must be within 0..1"},
      {"color below 0", Join({10, 0, -5}, {0.8, -0.1, 0.6}, 0),
       "color components must be within 0..1"},
      {"four color components", Join({10, 0, -5}, {0.8, 0.2, 0.6, 1.0}, 0),
       "color must be [r, g, b]"},
      {"negative shape", Join({10, 0, -5}, {0.8, 0.2, 0.6}, -1),
       "shape must be 0 (sphere), 1 (cube) or 2 (pyramid)"},
      {"fourth shape", Join({10, 0, -5}, {0.8, 0.2, 0.6}, 3),
       "shape must be 0 (sphere), 1 (cube) or 2 (pyramid)"},
      // A room id, when named, is held to golf's NUL rule, and — since any
      // id here creates a world — refused empty or over the length bound.
      {"empty room id", JoinIn("", {10, 0, -5}, {0.8, 0.2, 0.6}, 0), "invalid room id"},
      {"room id with a NUL", JoinIn(std::string("AB\0C", 4), {10, 0, -5}, {0.8, 0.2, 0.6}, 0),
       "invalid room id"},
      {"room id over the bound",
       JoinIn(std::string(World::kMaxRoomIdLength + 1, 'A'), {10, 0, -5}, {0.8, 0.2, 0.6}, 0),
       "invalid room id"},
  };
  for (const auto& c : refused) {
    ASSERT_TRUE(alice->stream.Send(c.command).ok()) << c.name;
    EXPECT_EQ(NextRejection(alice->stream), c.reason) << c.name;
  }
  // Boundaries are inside: the edge itself is a legal place to stand, and
  // a room id at the bound names a room.
  ASSERT_TRUE(
      alice->stream
          .Send(JoinIn(std::string(World::kMaxRoomIdLength, 'A'), {50, 0, -50}, {0, 1, 1}, 2))
          .ok());
  ASSERT_NE(NextEvent(alice->stream).value().as_worldState_or_null(), nullptr);
  ASSERT_TRUE(alice->stream.Send(Leave()).ok());
  ASSERT_TRUE(alice->stream.Send(Join({50, 0, -50}, {0, 1, 1}, 2)).ok());
  ASSERT_NE(NextEvent(alice->stream).value().as_worldState_or_null(), nullptr);

  // Refused updates leave the admitted state alone.
  ASSERT_TRUE(alice->stream.Send(MoveTo({60, 0, 0})).ok());
  EXPECT_EQ(NextRejection(alice->stream), "position out of bounds (±50)");
  ASSERT_TRUE(alice->stream.Send(Shape(5)).ok());
  EXPECT_EQ(NextRejection(alice->stream), "shape must be 0 (sphere), 1 (cube) or 2 (pyramid)");

  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(bob->stream.Send(FixtureJoin()).ok());
  auto world = NextEvent(bob->stream);
  ASSERT_TRUE(world.has_value());
  ASSERT_NE(world->as_worldState_or_null(), nullptr);
  ASSERT_EQ(world->as_worldState_or_null()->players.size(), 1u);
  const auto& only = world->as_worldState_or_null()->players[0];
  EXPECT_EQ(only.playerId, alice->player_id);
  EXPECT_EQ(only.position, (std::vector<double>{50, 0, -50}));
  EXPECT_EQ(only.shape, 2);
  // Twelve invalid joins, one invalid move, one invalid shape: all counted
  // as the client's malformed input, never as a state refusal.
  EXPECT_EQ(metrics_->CounterTotal("thoughts_rejections", {{"kind", "invalid"}}), 14);
}

TEST_F(GamesHubStreamFixture, MoveShapeAndLeaveNeedAJoinAndAJoinNeedsALeave) {
  auto alice = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value());

  ASSERT_TRUE(alice->stream.Send(MoveTo({1, 0, 1})).ok());
  EXPECT_EQ(NextRejection(alice->stream), "join the world first");
  ASSERT_TRUE(alice->stream.Send(Shape(1)).ok());
  EXPECT_EQ(NextRejection(alice->stream), "join the world first");
  ASSERT_TRUE(alice->stream.Send(Leave()).ok());
  EXPECT_EQ(NextRejection(alice->stream), "not in the world");

  ASSERT_TRUE(alice->stream.Send(FixtureJoin()).ok());
  ASSERT_NE(NextEvent(alice->stream).value().as_worldState_or_null(), nullptr);
  ASSERT_TRUE(alice->stream.Send(FixtureJoin()).ok());
  EXPECT_EQ(NextRejection(alice->stream), "already in the world; leave first");
  // Nor is a join elsewhere a way to change rooms: one world at a time.
  ASSERT_TRUE(alice->stream.Send(JoinIn("ABC123", {0, 0, 0}, {1, 1, 1}, 2)).ok());
  EXPECT_EQ(NextRejection(alice->stream), "already in the world; leave first");
  EXPECT_EQ(metrics_->CounterTotal("thoughts_rejections", {{"kind", "state"}}), 5);
}

TEST_F(GamesHubStreamFixture, AThoughtsStreamNeedsAFreshTicketAndRefusesASecondSeat) {
  // An unspendable ticket and a NUL-bearing one (the protocol boundary
  // golf's ProtocolBoundaryFixture pins) are refused alike, before any
  // event.
  for (const std::string& ticket : {std::string("t-bogus"), std::string("t-\0bogus", 8)}) {
    moonbase::games::ThinkInput bogus;
    bogus.ticket = ticket;
    auto refused = client_->Think(bogus);
    ASSERT_TRUE(refused.ok()) << refused.error().message();
    auto first = refused->Receive();
    ASSERT_FALSE(first.ok());
    EXPECT_EQ(first.error().code(), "Unauthenticated") << first.error().message();
  }

  auto alice = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value());
  // A fresh ticket for the same player while the first wire is healthy:
  // refused as SeatConflict, the same shape golf answers with.
  moonbase::games::GetSessionInput resume;
  resume.resumeToken = alice->resume_token;
  auto session = client_->GetSession(resume);
  ASSERT_TRUE(session.ok());
  moonbase::games::ThinkInput again;
  again.ticket = session->ticket;
  auto conflicted = client_->Think(again);
  ASSERT_TRUE(conflicted.ok());
  auto event = conflicted->Receive();
  ASSERT_FALSE(event.ok());
  EXPECT_EQ(event.error().code(), "SeatConflict");
  EXPECT_EQ(metrics_->CounterTotal("thoughts_admissions_refused", {{"reason", "bad_ticket"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("thoughts_admissions_refused", {{"reason", "seat_conflict"}}),
            1);
}

// One session identity, two hubs: a ticket spends on whichever stream it
// opens and is gone for the other, while the golf and thoughts seats for a
// player are independent — holding one does not conflict with the other.
TEST_F(GamesHubStreamFixture, OneTicketOpensOneStreamAndAPlayerMayHoldASeatOnEachHub) {
  auto alice = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value());

  // The ticket the thoughts stream spent cannot open golf: same vault,
  // single use.
  auto spent = OpenSeat(alice->resume_token);
  ASSERT_TRUE(spent.has_value());
  EXPECT_EQ(spent->player_id, alice->player_id);
  // OpenSeat minted a fresh ticket via the resume token, so this golf
  // stream is admitted — a different registry, no SeatConflict — and the
  // thoughts stream stays live alongside it.
  ASSERT_TRUE(ReceiveCase(spent->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(FixtureJoin()).ok());
  ASSERT_NE(NextEvent(alice->stream).value().as_worldState_or_null(), nullptr);

  // And the literal replay: the thoughts ticket itself, spent, on golf.
  moonbase::games::GetSessionInput fresh;
  auto session = client_->GetSession(fresh);
  ASSERT_TRUE(session.ok());
  moonbase::games::ThinkInput think;
  think.ticket = session->ticket;
  auto thoughts = client_->Think(think);
  ASSERT_TRUE(thoughts.ok());
  ASSERT_TRUE(NextEvent(*thoughts).has_value());  // sessionReady: the spend
  moonbase::games::PlayInput play;
  play.ticket = session->ticket;
  auto golf = client_->Play(play);
  ASSERT_TRUE(golf.ok());
  auto first = golf->Receive();
  ASSERT_FALSE(first.ok());
  EXPECT_EQ(first.error().code(), "Unauthenticated");
}

// Two tokens and no refill: the join and one move spend them, the next
// move is refused in-band without reaching the world, and the others
// hear exactly the one that landed.
class ThoughtsRateLimitedFixture : public GamesHubStreamFixture {
 protected:
  ThoughtsLimits MakeThoughtsLimits() override {
    ThoughtsLimits limits;
    limits.command_burst = 2;
    limits.command_refill_per_sec = 0;
    return limits;
  }
};

TEST_F(ThoughtsRateLimitedFixture, AMoveFloodIsRefusedAfterTheBurst) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  EnterPlaza(bob->stream);
  ASSERT_TRUE(alice->stream.Send(FixtureJoin()).ok());
  ASSERT_TRUE(NextEvent(alice->stream).has_value());
  ASSERT_TRUE(NextEvent(bob->stream).has_value());

  ASSERT_TRUE(alice->stream.Send(MoveTo({1, 0, 1})).ok());
  ASSERT_TRUE(alice->stream.Send(MoveTo({2, 0, 2})).ok());
  EXPECT_EQ(NextRejection(alice->stream), "slow down");
  auto moved = NextEvent(bob->stream);
  ASSERT_TRUE(moved.has_value());
  ASSERT_NE(moved->as_playerMoved_or_null(), nullptr);
  EXPECT_EQ(moved->as_playerMoved_or_null()->position, (std::vector<double>{1, 0, 1}));
  ExpectNoEvent(bob->stream);
  EXPECT_EQ(metrics_->CounterTotal("thoughts_rate_limited", {}), 1);
  EXPECT_EQ(metrics_->CounterTotal("thoughts_rejections", {{"kind", "rate_limited"}}), 1);
  // The session survived: bob's move still reaches alice.
  ASSERT_TRUE(bob->stream.Send(MoveTo({3, 0, 3})).ok());
  auto bob_moved = NextEvent(alice->stream);
  ASSERT_TRUE(bob_moved.has_value());
  EXPECT_NE(bob_moved->as_playerMoved_or_null(), nullptr);
}

// The two orderings the world lock is for, each driven at the hub's
// scheduling seam so the interleaving is the test's, not the scheduler's.
//
// The in-memory wire completes a session's parked Receive inline on the
// thread that sent the frame (or closed the socket), so the acting session's
// command — and the hook it parks in — runs on whichever thread triggered
// it. Each trigger therefore gets its own thread, and the test thread stays
// free to observe and release.
class ThoughtsRaceFixture : public GamesHubStreamFixture {
 protected:
  ThoughtsTestHooks MakeThoughtsHooks() override {
    ThoughtsTestHooks hooks;
    hooks.after_snapshot_queued = [this] { Park("snapshot"); };
    hooks.before_seat_release = [this](const std::string&) { Park("seat"); };
    return hooks;
  }

  // A hook that never released would hang the fixture's own closes.
  void TearDown() override {
    Disarm();
    Release();
    GamesHubStreamFixture::TearDown();
  }

  void Arm() {
    const std::lock_guard<std::mutex> lock(park_mu_);
    armed_ = true;
    released_ = false;
  }
  void Disarm() {
    const std::lock_guard<std::mutex> lock(park_mu_);
    armed_ = false;
  }
  // Waits until a hook named `stage` is parked (the hub's thread is inside
  // the seam), or fails after the budget.
  bool WaitParked(const std::string& stage) {
    std::unique_lock<std::mutex> lock(park_mu_);
    return park_cv_.wait_for(lock, kReceiveBudget, [&] { return parked_ == stage; });
  }
  void Release() {
    {
      const std::lock_guard<std::mutex> lock(park_mu_);
      parked_.clear();
      released_ = true;
    }
    park_cv_.notify_all();
  }

 private:
  void Park(const std::string& stage) {
    std::unique_lock<std::mutex> lock(park_mu_);
    if (!armed_) return;  // the seam only parks while a test has armed it
    parked_ = stage;
    park_cv_.notify_all();
    park_cv_.wait(lock, [&] { return released_; });
  }

  std::mutex park_mu_;
  std::condition_variable park_cv_;
  std::string parked_;
  bool armed_ = false;
  bool released_ = false;
};

// A leave that races a join lands behind the joiner's snapshot, never ahead
// of it: with the snapshot queued under the world lock, the leaver's erase
// waits, so the joiner sees the leaver in worldState and then hears
// playerLeft — the order a client can apply. Queued outside the lock, the
// playerLeft could arrive first, for a player the snapshot then resurrects.
TEST_F(ThoughtsRaceFixture, ALeaveDuringAJoinLandsBehindTheJoinersSnapshot) {
  auto alice = OpenThoughtsSeat();
  auto carol = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && carol.has_value() && bob.has_value());
  ASSERT_TRUE(alice->stream.Send(FixtureJoin()).ok());
  ASSERT_TRUE(NextEvent(alice->stream).has_value());  // worldState
  ASSERT_TRUE(carol->stream.Send(FixtureJoin()).ok());
  ASSERT_TRUE(NextEvent(carol->stream).has_value());  // worldState
  ASSERT_TRUE(NextEvent(alice->stream).has_value());  // playerJoined carol

  // Bob joins and parks inside the seam with the lock held and his
  // snapshot (listing carol) already queued.
  Arm();
  bool join_sent = false;
  std::thread joiner([&] { join_sent = bob->stream.Send(FixtureJoin()).ok(); });
  ASSERT_TRUE(WaitParked("snapshot"));

  // Carol leaves now. Her erase needs the lock bob holds, so nothing about
  // her leave can reach anyone yet — alice, in the world and not in the
  // seam, hears silence.
  bool leave_sent = false;
  std::thread leaver([&] { leave_sent = carol->stream.Send(Leave()).ok(); });
  ExpectNoEvent(alice->stream);
  Disarm();
  Release();
  joiner.join();
  leaver.join();
  EXPECT_TRUE(join_sent);
  EXPECT_TRUE(leave_sent);

  // Bob: the snapshot with carol in it, then her departure — in that order.
  auto world = NextEvent(bob->stream);
  ASSERT_TRUE(world.has_value());
  ASSERT_NE(world->as_worldState_or_null(), nullptr);
  std::set<std::string> listed;
  for (const auto& p : world->as_worldState_or_null()->players) listed.insert(p.playerId);
  EXPECT_EQ(listed, (std::set<std::string>{alice->player_id, carol->player_id}));
  auto left = NextEvent(bob->stream);
  ASSERT_TRUE(left.has_value());
  ASSERT_NE(left->as_playerLeft_or_null(), nullptr);
  EXPECT_EQ(left->as_playerLeft_or_null()->playerId, carol->player_id);
  // Alice hears both — bob's arrival and carol's departure — whose relative
  // order is the scheduler's, not the lock's.
  std::set<std::string> heard;
  for (int i = 0; i < 2; ++i) {
    auto event = NextEvent(alice->stream);
    ASSERT_TRUE(event.has_value());
    heard.insert(event->case_name());
  }
  EXPECT_EQ(heard, (std::set<std::string>{"playerJoined", "playerLeft"}));
}

// A closed socket erases its world entry and fans out playerLeft while the
// seat is still held, and only then releases it: a reconnect admitted the
// instant the seat frees finds the world already clean, instead of being
// refused as "already in the world" or having its own entry erased by the
// old frame's leave.
TEST_F(ThoughtsRaceFixture, AClosedSocketErasesTheWorldBeforeReleasingTheSeat) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  EnterPlaza(bob->stream);
  ASSERT_TRUE(alice->stream.Send(FixtureJoin()).ok());
  ASSERT_TRUE(NextEvent(alice->stream).has_value());
  ASSERT_TRUE(NextEvent(bob->stream).has_value());

  Arm();
  std::thread closer([&] { alice->stream.Close(); });
  ASSERT_TRUE(WaitParked("seat"));

  // Inside the seam: the world has already told bob, and the seat is still
  // alice's.
  auto left = NextEvent(bob->stream);
  ASSERT_TRUE(left.has_value());
  ASSERT_NE(left->as_playerLeft_or_null(), nullptr);
  EXPECT_EQ(left->as_playerLeft_or_null()->playerId, alice->player_id);
  const auto seats = thoughts_->registry().Ids();
  EXPECT_NE(std::find(seats.begin(), seats.end(), alice->player_id), seats.end())
      << "the seat was released before the world was cleaned";
  Disarm();
  Release();
  closer.join();

  // Past the seam the seat frees, and the reconnect's join is a fresh
  // arrival: bob hears one playerJoined, nothing is refused.
  auto back = OpenThoughtsSeat(alice->resume_token);
  ASSERT_TRUE(back.has_value());
  ASSERT_TRUE(back->stream.Send(FixtureJoin()).ok());
  auto world = NextEvent(back->stream);
  ASSERT_TRUE(world.has_value());
  EXPECT_NE(world->as_worldState_or_null(), nullptr);
  auto rejoined = NextEvent(bob->stream);
  ASSERT_TRUE(rejoined.has_value());
  EXPECT_NE(rejoined->as_playerJoined_or_null(), nullptr);
  ExpectNoEvent(bob->stream);
}

// A respawn that races a join hears the join before its own new snapshot,
// never after: the joiner queues the world's playerJoined in the same
// hold as its snapshot, so bob's leave-and-rejoin elsewhere waits behind
// it, and bob's queue reads playerJoined(alice), then his attic snapshot
// — which replaces the plaza, alice included. Queued after the unlock,
// the playerJoined could land behind bob's attic snapshot: a plaza
// player drawn in the attic, whom no playerLeft ever reaches.
TEST_F(ThoughtsRaceFixture, ARespawnDuringAJoinHearsTheJoinBeforeItsOwnSnapshot) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  EnterPlaza(bob->stream);

  Arm();
  bool join_sent = false;
  std::thread joiner([&] { join_sent = alice->stream.Send(FixtureJoin()).ok(); });
  ASSERT_TRUE(WaitParked("snapshot"));
  bool respawn_sent = false;
  std::thread respawner([&] {
    respawn_sent = bob->stream.Send(Leave()).ok() &&
                   bob->stream.Send(JoinIn("attic", {0, 0, 0}, {1, 1, 1}, 2)).ok();
  });
  ExpectNoEvent(bob->stream);
  Disarm();
  Release();
  joiner.join();
  respawner.join();
  EXPECT_TRUE(join_sent);
  EXPECT_TRUE(respawn_sent);

  auto joined = NextEvent(bob->stream);
  ASSERT_TRUE(joined.has_value());
  ASSERT_NE(joined->as_playerJoined_or_null(), nullptr);
  EXPECT_EQ(joined->as_playerJoined_or_null()->player.playerId, alice->player_id);
  auto attic = NextEvent(bob->stream);
  ASSERT_TRUE(attic.has_value());
  ASSERT_NE(attic->as_worldState_or_null(), nullptr);
  EXPECT_TRUE(attic->as_worldState_or_null()->players.empty());
  // Alice, still in the plaza: her snapshot listed bob, then she heard him
  // go; her later leave stays there.
  auto world = NextEvent(alice->stream);
  ASSERT_TRUE(world.has_value());
  ASSERT_NE(world->as_worldState_or_null(), nullptr);
  ASSERT_EQ(world->as_worldState_or_null()->players.size(), 1u);
  EXPECT_EQ(world->as_worldState_or_null()->players[0].playerId, bob->player_id);
  auto left = NextEvent(alice->stream);
  ASSERT_TRUE(left.has_value());
  ASSERT_NE(left->as_playerLeft_or_null(), nullptr);
  ASSERT_TRUE(alice->stream.Send(Leave()).ok());
  ExpectNoEvent(bob->stream);
}

// The thoughts_commands/thoughts_events declarations are a hand copy of the
// model's union cases; this reads thoughts.smithy and fails on drift in
// either direction, the way StreamSeriesMatchTheModelUnions does for golf.
TEST(ThoughtsSeriesModelPin, ThoughtsSeriesMatchTheModelUnions) {
  const std::string model = ReadModel("domains/games/apis/games_hub/model/thoughts.smithy");
  ASSERT_FALSE(model.empty());

  const auto commands = ModelUnionCases(model, "ThoughtsCommands");
  const auto events = ModelUnionCases(model, "ThoughtsEvents");
  // Controls: a parser that quietly matched nothing must fail here, not
  // produce two empty sets that agree.
  ASSERT_NE(std::find(commands.begin(), commands.end(), "join"), commands.end());
  ASSERT_NE(std::find(events.begin(), events.end(), "playerLeft"), events.end());

  EXPECT_EQ(DeclaredLabelValues("thoughts_commands", "command"),
            std::set<std::string>(commands.begin(), commands.end()));
  EXPECT_EQ(DeclaredLabelValues("thoughts_events", "event"),
            std::set<std::string>(events.begin(), events.end()));
}

}  // namespace
}  // namespace games_hub
