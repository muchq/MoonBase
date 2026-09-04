// The thoughts flows (#79), driven through the generated client over the
// in-memory pair: join, move, shape, leave, the close-is-a-leave rule, the
// world's bounds refused in-band, the ticket and seat contracts shared with
// golf, the stream budget, and the model pin for the thoughts_* series.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/apis/games_hub/thoughts_hub.h"

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

  // Bob has not joined, and still hears the world: a connected session
  // is watching.
  auto joined = NextEvent(bob->stream);
  ASSERT_TRUE(joined.has_value());
  ASSERT_NE(joined->as_playerJoined_or_null(), nullptr);
  const auto& player = joined->as_playerJoined_or_null()->player;
  EXPECT_EQ(player.playerId, alice->player_id);
  EXPECT_EQ(player.position, (std::vector<double>{10, 0, -5}));
  EXPECT_EQ(player.color, (std::vector<double>{0.8, 0.2, 0.6}));
  EXPECT_EQ(player.shape, 0);

  // Bob's join: his snapshot lists alice and not himself, and alice hears
  // him arrive. Neither hears their own join.
  ASSERT_TRUE(bob->stream.Send(Join({20, 0, 15}, {0.3, 0.9, 0.4}, 1)).ok());
  auto bobs_world = NextEvent(bob->stream);
  ASSERT_TRUE(bobs_world.has_value());
  ASSERT_NE(bobs_world->as_worldState_or_null(), nullptr);
  ASSERT_EQ(bobs_world->as_worldState_or_null()->players.size(), 1u);
  EXPECT_EQ(bobs_world->as_worldState_or_null()->players[0].playerId, alice->player_id);
  auto alice_hears = NextEvent(alice->stream);
  ASSERT_TRUE(alice_hears.has_value());
  ASSERT_NE(alice_hears->as_playerJoined_or_null(), nullptr);
  EXPECT_EQ(alice_hears->as_playerJoined_or_null()->player.playerId, bob->player_id);
  ExpectNoEvent(alice->stream);
  ExpectNoEvent(bob->stream);
}

TEST_F(GamesHubStreamFixture, MovesAndShapesReachTheOthersAndNeverEchoAndStick) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
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
  ASSERT_EQ(world->as_worldState_or_null()->players.size(), 1u);
  const auto& alice_now = world->as_worldState_or_null()->players[0];
  EXPECT_EQ(alice_now.position, (std::vector<double>{15, 0, -8}));
  EXPECT_EQ(alice_now.shape, 1);

  // thoughts_events counts deliveries, not commands: alice's join reached
  // one session, carol's reached two.
  EXPECT_EQ(metrics_->CounterTotal("thoughts_events", {{"event", "playerJoined"}}), 3);
  EXPECT_EQ(metrics_->CounterTotal("thoughts_events", {{"event", "playerMoved"}}), 1);
}

TEST_F(GamesHubStreamFixture, LeavingTellsTheOthersAndKeepsTheSessionForARejoin) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
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
}

TEST_F(GamesHubStreamFixture, AClosedSocketIsALeaveWithNothingToResume) {
  auto alice = OpenThoughtsSeat();
  auto bob = OpenThoughtsSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
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
  EXPECT_TRUE(world->as_worldState_or_null()->players.empty());
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
  };
  for (const auto& c : refused) {
    ASSERT_TRUE(alice->stream.Send(c.command).ok()) << c.name;
    EXPECT_EQ(NextRejection(alice->stream), c.reason) << c.name;
  }
  // Boundaries are inside: the edge itself is a legal place to stand.
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
  // Nine invalid joins, one invalid move, one invalid shape: all counted as
  // the client's malformed input, never as a state refusal.
  EXPECT_EQ(metrics_->CounterTotal("thoughts_rejections", {{"kind", "invalid"}}), 11);
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
  EXPECT_EQ(metrics_->CounterTotal("thoughts_rejections", {{"kind", "state"}}), 4);
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
  // The session survived: bob's join still reaches alice.
  ASSERT_TRUE(bob->stream.Send(FixtureJoin()).ok());
  ASSERT_TRUE(NextEvent(bob->stream).has_value());
  auto joined = NextEvent(alice->stream);
  ASSERT_TRUE(joined.has_value());
  EXPECT_NE(joined->as_playerJoined_or_null(), nullptr);
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
