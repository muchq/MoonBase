// Pong on the room stream, end to end through the generated client. No
// tick thread runs here: each test ticks the hub itself with TickPongOnce,
// so every rally is the same rally every run, and the serve, the bounce
// and the miss land on known ticks.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/libs/pong/rally.h"
#include "domains/games/libs/pong/table.h"
#include "domains/games/libs/pong/table_serde.h"

namespace games_hub {
namespace {

using moonbase::games::GameCommands;
using moonbase::games::PongMove;

GameCommands PongStart(std::optional<std::string> variant = std::nullopt) {
  moonbase::games::PongStartGame start;
  start.variant = std::move(variant);
  return Pong(PongMove::FromStartgame(start));
}

GameCommands Paddle(double y, std::optional<double> x = std::nullopt) {
  moonbase::games::PongPaddle paddle;
  paddle.x = x;
  paddle.y = y;
  return Pong(PongMove::FromPaddle(paddle));
}

GameCommands AddBot(int level) {
  moonbase::games::PongAddBot add;
  add.level = level;
  return Pong(PongMove::FromAddbot(add));
}

GameCommands Watch(const std::string& game_id) {
  moonbase::games::PongWatch watch;
  watch.gameId = game_id;
  return Pong(PongMove::FromWatch(watch));
}

GameCommands LeaveTable() { return Pong(PongMove::FromLeavegame(moonbase::games::LeaveGame{})); }

GameCommands RoomStateAsk() {
  return GameCommands::FromGetroomstate(moonbase::games::GetRoomState{});
}

class PongFixture : public GamesHubStreamFixture {
 protected:
  // A two-seat pong table, started on `variant`: alice (seat 0, end 0)
  // created it, bob joined. Each seat has heard gameStarted and read its
  // first view. `idle` more members are in the room, at no table.
  struct Started {
    Table table;
    moonbase::games::PongView view;
    std::vector<Seat> idle;
  };
  std::optional<Started> StartedTable(std::optional<std::string> variant = std::nullopt,
                                      int idle = 0) {
    auto room = SeatedRoom(2 + idle);
    if (!room.has_value()) return std::nullopt;
    Table table{std::move(room->seats[0]), std::move(room->seats[1]), room->room_id, ""};
    std::vector<Seat> idle_seats;
    for (int i = 0; i < idle; ++i) idle_seats.push_back(std::move(room->seats[2 + i]));
    if (!table.alice.stream.Send(Pong(PongMove::FromCreategame(moonbase::games::CreateGame{})))
             .ok()) {
      return std::nullopt;
    }
    auto created = ReceivePong(table.alice.stream, "gameJoined");
    if (!created.has_value()) return std::nullopt;
    table.game_id = created->as_gameJoined_or_null()->view.gameId;
    moonbase::games::JoinGame join;
    join.gameId = table.game_id;
    if (!table.bob.stream.Send(Pong(PongMove::FromJoingame(join))).ok()) return std::nullopt;
    if (!ReceivePong(table.bob.stream, "gameJoined").has_value()) return std::nullopt;
    if (!table.alice.stream.Send(PongStart(std::move(variant))).ok()) return std::nullopt;
    std::optional<moonbase::games::PongView> view;
    for (Seat* seat : {&table.alice, &table.bob}) {
      if (!ReceivePong(seat->stream, "gameStarted").has_value()) return std::nullopt;
      auto state = ReceivePong(seat->stream, "gameState");
      if (!state.has_value()) return std::nullopt;
      view = state->as_gameState_or_null()->view;
    }
    return Started{std::move(table), *view, std::move(idle_seats)};
  }

  // Sends `command` and waits until the hub has handled it: commands on a
  // stream are handled in order, so the roomState a later ask brings back
  // follows it.
  void SendHandled(Seat& seat, const GameCommands& command) {
    ASSERT_TRUE(seat.stream.Send(command).ok());
    ASSERT_TRUE(seat.stream.Send(RoomStateAsk()).ok());
    ASSERT_TRUE(ReceiveCase(seat.stream, "roomState").has_value());
  }

  // Sends `command` and waits for the hub to have handled it, failing on
  // any refusal it drew.
  void SendUnrefused(Seat& seat, const GameCommands& command) {
    ASSERT_TRUE(seat.stream.Send(command).ok());
    ASSERT_TRUE(seat.stream.Send(RoomStateAsk()).ok());
    const auto deadline = std::chrono::steady_clock::now() + kReceiveBudget;
    while (true) {
      auto received = ReceiveWithin(seat.stream, deadline);
      ASSERT_TRUE(received.ok() && received->has_value()) << "no answer";
      if (const auto* refused = (*received)->as_commandRejected_or_null()) {
        ADD_FAILURE() << "refused: " << refused->reason;
      }
      if ((*received)->as_roomState_or_null() != nullptr) return;
    }
  }

  // The pong updates `seat` hears until a view `done` accepts, by case.
  std::vector<std::string> PongHeardUntil(
      Seat& seat, const std::function<bool(const moonbase::games::PongView&)>& done) {
    std::vector<std::string> heard;
    const auto deadline = std::chrono::steady_clock::now() + kReceiveBudget;
    while (true) {
      auto received = ReceiveWithin(seat.stream, deadline);
      if (!received.ok() || !received->has_value()) {
        ADD_FAILURE() << "no such view";
        return heard;
      }
      const auto* pong = (*received)->as_pong_or_null();
      if (pong == nullptr) continue;
      heard.emplace_back(pong->update.case_name());
      if (const auto* state = pong->update.as_gameState_or_null(); state && done(state->view)) {
        return heard;
      }
    }
  }

  // The first roomState `seat` hears that `done` accepts, past any number
  // of frames.
  std::optional<moonbase::games::RoomState> RoomStateWhere(
      Seat& seat, const std::function<bool(const moonbase::games::RoomState&)>& done) {
    const auto deadline = std::chrono::steady_clock::now() + kReceiveBudget;
    while (true) {
      auto received = ReceiveWithin(seat.stream, deadline);
      if (!received.ok() || !received->has_value()) {
        ADD_FAILURE() << "no such room state";
        return std::nullopt;
      }
      if (const auto* room = (*received)->as_roomState_or_null(); room && done(*room)) return *room;
    }
  }

  // The stored table, read straight from the store.
  pong::Table StoredTable(const Table& table) {
    auto stored = store_->LoadGame(table.room_id, table.game_id);
    EXPECT_TRUE(stored.ok() && stored->has_value());
    return std::get<pong::Table>(*(*stored)->state);
  }

  // Ticks until `seat` hears a view that `done` accepts, at most `ticks`.
  std::optional<moonbase::games::PongView> TickUntil(
      Seat& seat, const std::function<bool(const moonbase::games::PongView&)>& done,
      int ticks = 4000) {
    for (int i = 0; i < ticks; ++i) {
      hub_->TickPongOnce();
      while (true) {
        auto received = seat.stream.Receive(std::chrono::milliseconds(0));
        if (!received.ok() || !received->has_value()) break;
        const auto* pong = (*received)->as_pong_or_null();
        if (pong == nullptr) continue;
        if (const auto* state = pong->update.as_gameState_or_null(); state && done(state->view)) {
          return state->view;
        }
      }
    }
    ADD_FAILURE() << "no such view in " << ticks << " ticks";
    return std::nullopt;
  }

  // The newest frame `seat` holds, after draining what arrived.
  static std::optional<moonbase::games::PongFrame> LatestFrame(Seat& seat) {
    std::optional<moonbase::games::PongFrame> latest;
    while (true) {
      auto received = seat.stream.Receive(std::chrono::milliseconds(50));
      if (!received.ok() || !received->has_value()) break;
      if (const auto* pong = (*received)->as_pong_or_null()) {
        if (const auto* frame = pong->update.as_frame_or_null()) latest = *frame;
      }
    }
    return latest;
  }

  static int LivesOf(const moonbase::games::PongView& view, const std::string& player_id) {
    for (const auto& player : view.players) {
      if (player.playerId == player_id) return player.lives.value_or(-1);
    }
    return -1;
  }
};

TEST_F(PongFixture, AStartedTableShowsTheCourtAndEveryLife) {
  auto started = StartedTable("3d");
  ASSERT_TRUE(started.has_value());
  const auto& view = started->view;
  EXPECT_EQ(view.phase, "playing");
  EXPECT_EQ(view.variant, "3d");
  ASSERT_TRUE(view.court.has_value());
  const pong::Court court = pong::CourtOf(pong::Variant::kThreeD);
  EXPECT_EQ(view.court->width, court.width);
  EXPECT_EQ(view.court->depth, court.depth);
  EXPECT_EQ(view.court->ticksPerSecond, pong::kTicksPerSecond);
  ASSERT_EQ(view.players.size(), 2u);
  EXPECT_EQ(view.players[0].playerId, started->table.alice.player_id);
  EXPECT_EQ(view.players[0].lives, pong::kLives);
  EXPECT_EQ(view.players[1].lives, pong::kLives);
  EXPECT_TRUE(view.scoreSheet.empty());
}

TEST_F(PongFixture, TheVariantDefaultsToSmallcat) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  EXPECT_EQ(started->view.variant, "smallcat");
  EXPECT_EQ(started->view.court->width, 0);
}

TEST_F(PongFixture, AnUnknownVariantIsRefused) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  ASSERT_TRUE(alice.stream.Send(Pong(PongMove::FromCreategame({}))).ok());
  auto created = ReceivePong(alice.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  moonbase::games::JoinGame join;
  join.gameId = created->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(room->seats[1].stream.Send(Pong(PongMove::FromJoingame(join))).ok());
  ASSERT_TRUE(ReceivePong(room->seats[1].stream, "gameJoined").has_value());
  ASSERT_TRUE(alice.stream.Send(PongStart("squash")).ok());
  auto refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_THAT(refused->as_commandRejected_or_null()->reason, ::testing::HasSubstr("squash"));
}

TEST_F(PongFixture, TheRoomListsAPongTable) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  ASSERT_TRUE(started->table.alice.stream.Send(RoomStateAsk()).ok());
  auto room = ReceiveCase(started->table.alice.stream, "roomState");
  ASSERT_TRUE(room.has_value());
  const auto& games = room->as_roomState_or_null()->games;
  ASSERT_EQ(games.size(), 1u);
  EXPECT_EQ(games[0].game, "pong");
  EXPECT_EQ(games[0].status, "playing");
}

// The rally reaches both seats and a watcher as frames, every other tick,
// counting down to the serve.
TEST_F(PongFixture, FramesReachTheSeatsAndAWatcher) {
  auto started = StartedTable("smallcat", /*idle=*/1);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  auto watching = ReceivePong(carol.stream, "gameState");
  ASSERT_TRUE(watching.has_value());
  EXPECT_EQ(watching->as_gameState_or_null()->view.phase, "playing");

  EXPECT_EQ(hub_->TickPongOnce(), 1);
  EXPECT_FALSE(LatestFrame(started->table.alice).has_value()) << "every other tick";
  EXPECT_EQ(hub_->TickPongOnce(), 1);
  for (Seat* seat : {&started->table.alice, &started->table.bob, &carol}) {
    const auto frame = LatestFrame(*seat);
    ASSERT_TRUE(frame.has_value()) << seat->player_id;
    EXPECT_EQ(frame->gameId, started->table.game_id);
    EXPECT_EQ(frame->serveIn, pong::kServeWaitTicks - 2);
    ASSERT_EQ(frame->paddles.size(), 2u);
  }
}

// A phone that navigates away can leave its socket open on the hub's
// side; the player coming back with their resume token is seated again
// and hears the rally, not shown the court as a stranger.
TEST_F(PongFixture, AResumeWhileTheOldWireIsStillOpenReclaimsTheSeat) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;

  auto back = OpenSeat(table.alice.resume_token);
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->player_id, table.alice.player_id);
  auto ready = ReceiveCase(back->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  auto joined = ReceivePong(back->stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  EXPECT_EQ(joined->as_gameJoined_or_null()->view.gameId, table.game_id);

  hub_->TickPongOnce();
  hub_->TickPongOnce();
  const auto frame = LatestFrame(*back);
  ASSERT_TRUE(frame.has_value()) << "the rally does not reach the resumed seat";
  EXPECT_EQ(frame->gameId, table.game_id);
}

// Ticks count on from the clock, so a rally taken up mid-game, here or
// on another instance, never draws a frame a client would call stale.
TEST_F(PongFixture, FrameTicksCountOnFromTheClockAcrossATakeUp) {
  hub_->SetClock([] { return absl::FromUnixMillis(1'800'000'000'000); });
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  hub_->TickPongOnce();
  hub_->TickPongOnce();
  const auto before = LatestFrame(started->table.alice);
  ASSERT_TRUE(before.has_value());
  EXPECT_EQ(before->tick, 1'800'000'000'000 / 1000 * pong::kTicksPerSecond + 2);
  hub_->DropPongRalliesForTest();
  SendHandled(started->table.alice, Paddle(0));
  hub_->TickPongOnce();
  hub_->TickPongOnce();
  const auto after = LatestFrame(started->table.alice);
  ASSERT_TRUE(after.has_value());
  EXPECT_GE(after->tick, before->tick);
}

TEST_F(PongFixture, ASeatsPaddleChasesWhereItPoints) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  SendHandled(started->table.alice, Paddle(10'000));
  for (int i = 0; i < 100; ++i) hub_->TickPongOnce();
  const auto frame = LatestFrame(started->table.alice);
  ASSERT_TRUE(frame.has_value());
  const pong::Court court = pong::CourtOf(pong::Variant::kSmallcat);
  EXPECT_EQ(frame->paddles[0].y, court.height - court.paddle_height / 2);
  EXPECT_EQ(frame->paddles[1].y, court.height / 2) << "bob pointed nowhere";
}

TEST_F(PongFixture, APaddleFromNoSeatIsRefused) {
  auto started = StartedTable("smallcat", /*idle=*/1);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(ReceivePong(carol.stream, "gameState").has_value());
  ASSERT_TRUE(carol.stream.Send(Paddle(0)).ok());
  auto refused = ReceiveCase(carol.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not seated at a pong table");
}

// bob's paddle sits in the corner, and smallcat's first serve goes his
// way: the ball gets past him, and the point costs him a life.
TEST_F(PongFixture, AMissCostsALife) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  SendHandled(started->table.bob, Paddle(0));
  const std::string bob = started->table.bob.player_id;
  // Ticked one at a time until the point is stored: it is the table's.
  for (int tick = 0; tick < 4000 && StoredTable(started->table).points() == 0; ++tick) {
    hub_->TickPongOnce();
  }
  EXPECT_EQ(StoredTable(started->table).lives()[1], pong::kLives - 1);
  const auto view = AwaitPongView(
      started->table.alice.stream,
      [&](const auto& view) { return LivesOf(view, bob) < pong::kLives; }, "bob's miss");
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(LivesOf(*view, bob), pong::kLives - 1);
  EXPECT_EQ(LivesOf(*view, started->table.alice.player_id), pong::kLives);
  EXPECT_EQ(view->phase, "playing");
  // The next serve heads for the end that missed, bob's paddle where it was.
  for (int i = 0; i < pong::kServeWaitTicks + 4; ++i) hub_->TickPongOnce();
  const auto served = LatestFrame(started->table.alice);
  ASSERT_TRUE(served.has_value());
  EXPECT_GT(served->velocity.z, 0) << "toward bob";
  const pong::Court court = pong::CourtOf(pong::Variant::kSmallcat);
  EXPECT_EQ(served->paddles[1].y, court.paddle_height / 2);
}

TEST_F(PongFixture, TheLastLifeEndsTheGameAndCreditsTheRoom) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  SendHandled(started->table.bob, Paddle(0));
  const std::string alice = started->table.alice.player_id;
  const std::string bob = started->table.bob.player_id;
  const auto view =
      TickUntil(started->table.alice, [](const auto& view) { return view.phase == "ended"; });
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(LivesOf(*view, bob), 0);
  ASSERT_TRUE(view->result.has_value());
  EXPECT_EQ(view->result->winner, alice);
  EXPECT_EQ(view->result->ending, "points");
  ASSERT_EQ(view->scoreSheet.size(), 1u);
  auto ended = ReceivePong(started->table.alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->as_gameEnded_or_null()->result.winner, alice);
  auto room = AwaitRoomState(
      started->table.alice.stream,
      [&](const auto& state) {
        for (const auto& player : state.players) {
          if (player.playerId == alice && player.gamesWon == 1) return true;
        }
        return false;
      },
      "alice's win");
  ASSERT_TRUE(room.has_value());
  for (const auto& player : room->players) {
    EXPECT_EQ(player.gamesPlayed, 1) << player.playerId;
  }
  EXPECT_EQ(hub_->TickPongOnce(), 0) << "no rally between games";
}

TEST_F(PongFixture, EitherSeatStartsTheNextGame) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  SendHandled(started->table.bob, Paddle(0));
  ASSERT_TRUE(TickUntil(started->table.alice, [](const auto& view) {
                return view.phase == "ended";
              }).has_value());
  ASSERT_TRUE(started->table.bob.stream.Send(PongStart("3d")).ok());
  const auto next = AwaitPongView(
      started->table.bob.stream,
      [](const auto& view) { return view.phase == "playing" && view.variant == "3d"; },
      "the next game");
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next->variant, "3d");
  EXPECT_EQ(LivesOf(*next, started->table.bob.player_id), pong::kLives);
  EXPECT_EQ(next->scoreSheet.size(), 1u);
  EXPECT_EQ(hub_->TickPongOnce(), 1);
}

TEST_F(PongFixture, LeavingAGameInPlayAbandonsItToTheOtherSeat) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  ASSERT_TRUE(started->table.bob.stream.Send(LeaveTable()).ok());
  const auto closed = AwaitPongView(
      started->table.alice.stream, [](const auto& view) { return view.phase == "closed"; },
      "the close");
  ASSERT_TRUE(closed.has_value());
  ASSERT_TRUE(closed->result.has_value());
  EXPECT_EQ(closed->result->winner, started->table.alice.player_id);
  EXPECT_EQ(closed->result->ending, "abandoned");
  auto ended = ReceivePong(started->table.alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->as_gameEnded_or_null()->result.ending, "abandoned");
  EXPECT_EQ(hub_->TickPongOnce(), 0);
  // The abandoned game counts: a win for alice, a game for both.
  const std::string alice = started->table.alice.player_id;
  const auto room = AwaitRoomState(
      started->table.alice.stream,
      [&](const auto& state) {
        for (const auto& player : state.players) {
          if (player.playerId == alice && player.gamesWon == 1) return true;
        }
        return false;
      },
      "alice's win");
  ASSERT_TRUE(room.has_value());
  for (const auto& player : room->players) {
    EXPECT_EQ(player.gamesPlayed, 1) << player.playerId;
    if (player.playerId != alice) EXPECT_EQ(player.gamesWon, 0);
  }
}

// A paddle in mid-rally steers; it does not serve the ball again.
TEST_F(PongFixture, APaddleMidRallyDoesNotServeAgain) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  for (int i = 0; i < pong::kServeWaitTicks + 10; ++i) hub_->TickPongOnce();
  SendHandled(started->table.alice, Paddle(0));
  hub_->TickPongOnce();
  hub_->TickPongOnce();
  const auto frame = LatestFrame(started->table.alice);
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->serveIn, 0);
  EXPECT_NE(frame->velocity.z, 0);
  EXPECT_NE(frame->ball.z, pong::CourtOf(pong::Variant::kSmallcat).depth / 2);
}

// A pointer keeps moving at a table not in play: its paddles are dropped,
// not refused, and the hub keeps answering.
TEST_F(PongFixture, APaddleAtATableNotInPlayIsDropped) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  ASSERT_TRUE(alice.stream.Send(Pong(PongMove::FromCreategame({}))).ok());
  ASSERT_TRUE(ReceivePong(alice.stream, "gameJoined").has_value());
  SendUnrefused(alice, Paddle(5));
  EXPECT_EQ(hub_->PongRalliesHosted(), 0u);
}

TEST_F(PongFixture, APaddleBetweenGamesIsDropped) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  SendHandled(started->table.bob, Paddle(0));
  ASSERT_TRUE(TickUntil(started->table.alice, [](const auto& view) {
                return view.phase == "ended";
              }).has_value());
  SendUnrefused(started->table.alice, Paddle(5));
  EXPECT_EQ(hub_->PongRalliesHosted(), 0u);
}

// Each game's end is told once: not again when the next starts, nor when
// the table closes between games, which credits nothing more.
TEST_F(PongFixture, AGamesEndIsToldOnce) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  Seat& alice = started->table.alice;
  SendHandled(started->table.bob, Paddle(0));
  ASSERT_TRUE(TickUntil(alice, [](const auto& view) { return view.phase == "ended"; }).has_value());
  ASSERT_TRUE(ReceivePong(alice.stream, "gameEnded").has_value());
  ASSERT_TRUE(started->table.bob.stream.Send(PongStart()).ok());
  EXPECT_THAT(PongHeardUntil(alice, [](const auto& view) { return view.phase == "playing"; }),
              ::testing::Not(::testing::Contains("gameEnded")));
  // Nor right after the new game's view: its rally's first frame is next.
  hub_->TickPongOnce();
  hub_->TickPongOnce();
  while (true) {
    auto received = alice.stream.Receive(kReceiveBudget);
    ASSERT_TRUE(received.ok() && received->has_value());
    if (const auto* pong = (*received)->as_pong_or_null()) {
      EXPECT_EQ(std::string(pong->update.case_name()), "frame");
      break;
    }
  }
  // That game too, then a leave between games.
  SendHandled(started->table.bob, Paddle(0));
  ASSERT_TRUE(TickUntil(alice, [](const auto& view) {
                return view.phase == "ended" && view.scoreSheet.size() == 2;
              }).has_value());
  ASSERT_TRUE(ReceivePong(alice.stream, "gameEnded").has_value());
  const auto two_played = [](const moonbase::games::RoomState& state) {
    int won = 0;
    for (const auto& player : state.players) {
      if (player.gamesPlayed != 2) return false;
      won += player.gamesWon;
    }
    return won == 2;
  };
  ASSERT_TRUE(RoomStateWhere(alice, two_played).has_value());
  ASSERT_TRUE(started->table.bob.stream.Send(LeaveTable()).ok());
  EXPECT_THAT(PongHeardUntil(alice, [](const auto& view) { return view.phase == "closed"; }),
              ::testing::Not(::testing::Contains("gameEnded")));
  // The close between games credits nothing more.
  const auto room = RoomStateWhere(alice, [](const auto& state) { return state.games.empty(); });
  ASSERT_TRUE(room.has_value());
  EXPECT_TRUE(two_played(*room));
}

// A table erased under its rally, as a sweep or a sibling would: the next
// point finds it gone, and the rally goes with it.
TEST_F(PongFixture, ARallyWhoseTableIsGoneStops) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  SendHandled(started->table.bob, Paddle(0));
  store_->Enqueue({HubStore::DeleteGame{started->table.room_id, started->table.game_id}});
  store_->Flush();
  for (int tick = 0; tick < 4000 && hub_->PongRalliesHosted() > 0; ++tick) {
    hub_->TickPongOnce();
  }
  EXPECT_EQ(hub_->PongRalliesHosted(), 0u);
  EXPECT_TRUE(RoomStateWhere(started->table.alice, [](const auto& state) {
                return state.games.empty();
              }).has_value());
}

TEST_F(PongFixture, AWatcherGetsUpWithLeaveGame) {
  auto started = StartedTable("smallcat", /*idle=*/1);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(ReceivePong(carol.stream, "gameState").has_value());
  ASSERT_TRUE(carol.stream.Send(LeaveTable()).ok());
  auto left = ReceivePong(carol.stream, "gameLeft");
  ASSERT_TRUE(left.has_value());
  EXPECT_EQ(left->as_gameLeft_or_null()->gameId, started->table.game_id);
  hub_->TickPongOnce();
  hub_->TickPongOnce();
  EXPECT_FALSE(LatestFrame(carol).has_value()) << "no frames once up";
}

TEST_F(PongFixture, ABotTakesOnlyAnEmptySeatAtAPongTable) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  ASSERT_TRUE(alice.stream.Send(Chess(moonbase::games::ChessMove::FromCreategame({}))).ok());
  ASSERT_TRUE(ReceiveChess(alice.stream, "gameJoined").has_value());
  ASSERT_TRUE(alice.stream.Send(AddBot(1)).ok());
  auto refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "that table plays chess");
  ASSERT_TRUE(alice.stream.Send(Chess(moonbase::games::ChessMove::FromLeavegame({}))).ok());
  ASSERT_TRUE(ReceiveChess(alice.stream, "gameLeft").has_value());

  ASSERT_TRUE(alice.stream.Send(Pong(PongMove::FromCreategame({}))).ok());
  auto created = ReceivePong(alice.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  moonbase::games::JoinGame join;
  join.gameId = created->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(room->seats[1].stream.Send(Pong(PongMove::FromJoingame(join))).ok());
  ASSERT_TRUE(ReceivePong(room->seats[1].stream, "gameJoined").has_value());
  ASSERT_TRUE(alice.stream.Send(AddBot(1)).ok());
  refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "the table is full");
}

TEST_F(PongFixture, TheTablesOnePlayerSeatsABotWhoPlaysItsPaddle) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  ASSERT_TRUE(alice.stream.Send(Pong(PongMove::FromCreategame({}))).ok());
  ASSERT_TRUE(ReceivePong(alice.stream, "gameJoined").has_value());
  ASSERT_TRUE(alice.stream.Send(AddBot(2)).ok());
  const auto seated = AwaitPongView(
      alice.stream, [](const auto& view) { return view.players.size() == 2; }, "the bot");
  ASSERT_TRUE(seated.has_value());
  EXPECT_EQ(seated->players[1].playerId, "smallcat@2");
  EXPECT_EQ(seated->players[1].bot, true);
  EXPECT_FALSE(seated->players[0].bot.has_value());
  ASSERT_TRUE(alice.stream.Send(PongStart("smallcat")).ok());
  ASSERT_TRUE(AwaitPongView(
                  alice.stream, [](const auto& view) { return view.phase == "playing"; }, "start")
                  .has_value());
  // The first serve heads for the bot's end, off its line: it chases.
  const pong::Court court = pong::CourtOf(pong::Variant::kSmallcat);
  for (int i = 0; i < pong::kServeWaitTicks + 60; ++i) hub_->TickPongOnce();
  const auto frame = LatestFrame(alice);
  ASSERT_TRUE(frame.has_value());
  EXPECT_NE(frame->paddles[1].y, court.height / 2);
  EXPECT_EQ(frame->paddles[0].y, court.height / 2);
}

TEST_F(PongFixture, OnlyAWaitingTablesOnePlayerSeatsABot) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  ASSERT_TRUE(started->table.alice.stream.Send(AddBot(1)).ok());
  auto refused = ReceiveCase(started->table.alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "game already started");
}

// A bot holds no table alone: its player leaving a waiting table takes it.
TEST_F(PongFixture, TheBotLeavesWithItsPlayer) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  ASSERT_TRUE(alice.stream.Send(Pong(PongMove::FromCreategame({}))).ok());
  ASSERT_TRUE(ReceivePong(alice.stream, "gameJoined").has_value());
  ASSERT_TRUE(alice.stream.Send(AddBot(3)).ok());
  ASSERT_TRUE(AwaitPongView(
                  alice.stream, [](const auto& view) { return view.players.size() == 2; }, "bot")
                  .has_value());
  ASSERT_TRUE(alice.stream.Send(LeaveTable()).ok());
  const auto emptied = AwaitRoomState(
      alice.stream, [](const auto& state) { return state.games.empty(); }, "the table gone");
  EXPECT_TRUE(emptied.has_value());
}

// The instance that last committed the table runs its rally. A commit
// from elsewhere — another instance's point — hands the rally over: this
// one stops ticking it.
TEST_F(PongFixture, AnotherInstancesCommitTakesTheRally) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  EXPECT_EQ(hub_->PongRalliesHosted(), 1u);
  auto stored = store_->LoadGame(started->table.room_id, started->table.game_id);
  ASSERT_TRUE(stored.ok() && stored->has_value());
  HubStore::GameRow row = **stored;
  row.state.emplace(*std::get<pong::Table>(*row.state).point(0));
  ++row.version;
  ASSERT_TRUE(store_->CommitGameSave(row, "hub-elsewhere").ok());
  hub_->OnNotify("room_" + started->table.room_id, "hub-elsewhere");
  const auto moved = AwaitPongView(
      started->table.alice.stream,
      [&](const auto& view) {
        return LivesOf(view, started->table.alice.player_id) < pong::kLives;
      },
      "the other instance's point");
  ASSERT_TRUE(moved.has_value());
  EXPECT_EQ(hub_->PongRalliesHosted(), 0u);
  EXPECT_EQ(hub_->TickPongOnce(), 0);
  // A seat here steers nothing now: running a second rally beside the
  // other instance's would score the paddle that cannot move here.
  SendHandled(started->table.alice, Paddle(0));
  EXPECT_EQ(hub_->PongRalliesHosted(), 0u);
}

// A point whose commit finds another instance got there first hands the
// rally over too, and the table reads the other instance's score.
TEST_F(PongFixture, APointThatLosesItsCommitHandsTheRallyOn) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  SendHandled(started->table.bob, Paddle(0));
  auto stored = store_->LoadGame(started->table.room_id, started->table.game_id);
  ASSERT_TRUE(stored.ok() && stored->has_value());
  HubStore::GameRow row = **stored;
  row.state.emplace(*std::get<pong::Table>(*row.state).point(0));
  ++row.version;
  ASSERT_TRUE(store_->CommitGameSave(row, "hub-elsewhere").ok());
  const std::string alice = started->table.alice.player_id;
  const auto view = TickUntil(
      started->table.alice, [&](const auto& view) { return LivesOf(view, alice) < pong::kLives; });
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(LivesOf(*view, started->table.bob.player_id), pong::kLives) << "bob's miss lost";
  EXPECT_EQ(hub_->PongRalliesHosted(), 0u);
}

// After a restart nothing ticks the table until a seat moves its paddle,
// which takes the rally up here, from a serve.
TEST_F(PongFixture, ASeatsPaddleTakesUpATableNobodyIsTicking) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  hub_->DropPongRalliesForTest();
  EXPECT_EQ(hub_->TickPongOnce(), 0);
  SendHandled(started->table.alice, Paddle(0));
  EXPECT_EQ(hub_->PongRalliesHosted(), 1u);
  EXPECT_EQ(hub_->TickPongOnce(), 1);
}

// A store whose game commits can be taken down: a point the store could
// not take is played again, not lost and not kept only here.
class SaveOutageStore final : public HubStore {
 public:
  void Enqueue(std::vector<Op> ops) override { delegate_.Enqueue(std::move(ops)); }
  void Flush() override { delegate_.Flush(); }
  absl::StatusOr<Snapshot> LoadSnapshot() override { return delegate_.LoadSnapshot(); }
  absl::StatusOr<bool> CommitGameSave(const GameRow& row,
                                      const std::string& notify_payload) override {
    if (down) return absl::UnavailableError("hub store unreachable");
    return delegate_.CommitGameSave(row, notify_payload);
  }
  absl::StatusOr<bool> CommitGameFinish(const GameRow& row, const std::vector<StatsDelta>& deltas,
                                        const std::string& notify_payload) override {
    if (down) return absl::UnavailableError("hub store unreachable");
    return delegate_.CommitGameFinish(row, deltas, notify_payload);
  }
  absl::StatusOr<bool> CommitChessEvent(const ChessEventRow& row,
                                        const std::string& notify_payload) override {
    return delegate_.CommitChessEvent(row, notify_payload);
  }
  absl::StatusOr<std::optional<GameRow>> LoadGame(const std::string& room_id,
                                                  const std::string& game_id) override {
    return delegate_.LoadGame(room_id, game_id);
  }
  absl::StatusOr<ChessHistory> LoadChessHistory(const std::string& room_id, int limit) override {
    return delegate_.LoadChessHistory(room_id, limit);
  }
  absl::StatusOr<std::optional<ChessGameRow>> LoadChessGame(const std::string& room_id,
                                                            const ChessGameKey& key) override {
    return delegate_.LoadChessGame(room_id, key);
  }
  absl::StatusOr<std::vector<PublishedChessGame>> LoadPublishedChess(int64_t after_archive_id,
                                                                     int limit) override {
    return delegate_.LoadPublishedChess(after_archive_id, limit);
  }
  absl::StatusOr<RoomRows> LoadRoom(const std::string& room_id) override {
    return delegate_.LoadRoom(room_id);
  }

  std::atomic<bool> down{false};

 private:
  MemoryHubStore delegate_;
};

class PongOutageFixture : public PongFixture {
 protected:
  std::shared_ptr<HubStore> MakeStore() override {
    outage_ = std::make_shared<SaveOutageStore>();
    return outage_;
  }
  std::shared_ptr<SaveOutageStore> outage_;
};

TEST_F(PongOutageFixture, APointTheStoreCannotTakeIsPlayedAgain) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  SendHandled(started->table.bob, Paddle(0));
  outage_->down = true;
  // Long enough for bob to miss twice over.
  for (int i = 0; i < 600; ++i) hub_->TickPongOnce();
  EXPECT_EQ(hub_->PongRalliesHosted(), 1u) << "the rally plays on";
  EXPECT_EQ(StoredTable(started->table).lives()[1], pong::kLives);
  outage_->down = false;
  for (int tick = 0; tick < 4000 && StoredTable(started->table).points() == 0; ++tick) {
    hub_->TickPongOnce();
  }
  EXPECT_EQ(StoredTable(started->table).lives()[1], pong::kLives - 1) << "one point, once";
}

class PaddleBudgetFixture : public PongFixture {
 protected:
  RateLimits MakeRateLimits() override {
    RateLimits limits = UnlimitedRateLimits();
    limits.command_burst = 3;
    limits.command_refill_per_sec = 0.001;
    return limits;
  }
};

// A pointer sends many paddles; they come out of their own budget, and
// the table's other commands keep theirs.
TEST_F(PaddleBudgetFixture, PaddlesSpendTheirOwnBudget) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  for (int i = 0; i < 30; ++i) ASSERT_TRUE(alice.stream.Send(Paddle(i)).ok());
  ASSERT_TRUE(alice.stream.Send(Pong(PongMove::FromCreategame({}))).ok());
  while (true) {
    auto received = alice.stream.Receive(kReceiveBudget);
    ASSERT_TRUE(received.ok() && received->has_value());
    if (const auto* refused = (*received)->as_commandRejected_or_null()) {
      ASSERT_NE(refused->reason, "slow down");
      continue;
    }
    if (const auto* pong = (*received)->as_pong_or_null();
        pong != nullptr && pong->update.as_gameJoined_or_null() != nullptr) {
      break;
    }
  }
}


// The seat cap (#1621), pinned before it moves to the game's adapter.
TEST_F(PongFixture, AThirdSeatIsRefused) {
  ExpectSeatCap<PongMove>(2, Pong,
                      [](moonbase::games::PlayClientStream& stream, const std::string& wanted) {
                        return ReceivePong(stream, wanted);
                      });
}


// Pong's branches of the hub (#1621), pinned before they move to pong's
// adapter: the status the room lists a table under, the commands pong
// counts, and the record of a table's next game.
TEST_F(PongFixture, TheRoomListsTheTablePlayingEndedThenPlayingAgain) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ExpectListedStatus(table.alice, table.game_id, "playing");
  SendHandled(table.bob, Paddle(0));
  ASSERT_TRUE(
      TickUntil(table.alice, [](const auto& view) { return view.phase == "ended"; }).has_value());
  ExpectListedStatus(table.alice, table.game_id, "ended");
  ASSERT_TRUE(table.bob.stream.Send(PongStart("3d")).ok());
  ASSERT_TRUE(AwaitPongView(
                  table.bob.stream, [](const auto& view) { return view.phase == "playing"; },
                  "the next game")
                  .has_value());
  ExpectListedStatus(table.bob, table.game_id, "playing");
}

TEST_F(PongFixture, CommandsAreCountedOnPongsSeries) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  EXPECT_EQ(metrics_->CounterTotal("pong_commands", {{"command", "createGame"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("pong_commands", {{"command", "joinGame"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("pong_commands", {{"command", "startGame"}}), 1);
}

// A pong game in the stats archive (#1571), the first and the next.
class PongEventFixture : public PongFixture {
 protected:
  void SetUp() override {
    PongFixture::SetUp();
    hub_->SetEventWriter(
        [this](absl::Time, std::string_view line) { events_.emplace_back(line); });
  }
  // The writer goes first: the hub's teardown can end a game, and
  // `events_` dies before the base's hub does.
  void TearDown() override {
    if (hub_ != nullptr) hub_->SetEventWriter(nullptr);
    PongFixture::TearDown();
  }
  std::vector<std::string> GameLines() const {
    std::vector<std::string> games;
    for (const std::string& line : events_) {
      if (line.find(R"("event":"game_)") != std::string::npos) games.push_back(line);
    }
    return games;
  }
  std::vector<std::string> events_;
};

TEST_F(PongEventFixture, EachGameIsRecordedStartedAndFinishedAsPong) {
  auto started = StartedTable("smallcat");
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  SendHandled(table.bob, Paddle(0));
  ASSERT_TRUE(
      TickUntil(table.alice, [](const auto& view) { return view.phase == "ended"; }).has_value());
  ASSERT_TRUE(table.bob.stream.Send(PongStart("3d")).ok());
  ASSERT_TRUE(AwaitPongView(
                  table.bob.stream, [](const auto& view) { return view.phase == "playing"; },
                  "the next game")
                  .has_value());
  const auto games = GameLines();
  ASSERT_EQ(games.size(), 3u);
  EXPECT_THAT(games[0], ::testing::HasSubstr(R"("event":"game_started")"));
  EXPECT_THAT(games[1], ::testing::HasSubstr(R"("event":"game_finished")"));
  EXPECT_THAT(games[1], ::testing::HasSubstr(R"("outcome":"completed")"));
  EXPECT_THAT(games[2], ::testing::HasSubstr(R"("event":"game_started")"));
  for (const auto& line : games) {
    EXPECT_THAT(line, ::testing::HasSubstr(R"("variant":"pong")"));
    EXPECT_THAT(line, ::testing::HasSubstr(R"("players":2)"));
  }
}

}  // namespace
}  // namespace games_hub
