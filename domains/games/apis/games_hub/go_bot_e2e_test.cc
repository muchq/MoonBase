// A bot at a go table, end to end through the generated client: GNU Go's
// seat added by the table's one player, its moves asked of the engine and
// played when it is on turn, and its acceptance of the marks in the
// scoring. The engine here is a fake that answers what the test says;
// go_engine's own tests cover GNU Go.

#include <gtest/gtest.h>

#include <atomic>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"

namespace games_hub {
namespace {

using moonbase::games::GameCommands;
using moonbase::games::GoMove;
using moonbase::games::GoView;

constexpr int64_t kT0 = 1'800'000'000'000;

GameCommands AddBot(int level) {
  moonbase::games::GoAddBot add;
  add.level = level;
  return Go(GoMove::FromAddbot(add));
}

GameCommands Play(int x, int y) {
  moonbase::games::GoPlay play;
  play.x = x;
  play.y = y;
  return Go(GoMove::FromPlay(play));
}

GameCommands Pass() { return Go(GoMove::FromPass(moonbase::games::GoPass{})); }

GameCommands Accept() { return Go(GoMove::FromAccept(moonbase::games::GoAccept{})); }

GameCommands NineByNine() {
  moonbase::games::GoStartGame start;
  start.size = 9;
  start.komi = 5.5;
  start.initialSeconds = 60;
  start.incrementSeconds = 0;
  return Go(GoMove::FromStartgame(std::move(start)));
}

class GoBotFixture : public GamesHubStreamFixture {
 protected:
  void SetUp() override {
    GamesHubStreamFixture::SetUp();
    hub_->SetClock([this] { return absl::FromUnixMillis(now_ms_.load()); });
    hub_->SetGoBotEngine([this](const GoBotAsk& ask) -> absl::StatusOr<std::string> {
      asks_.push_back(ask);
      return answer_;
    });
  }

  std::optional<GoView> ViewWhere(Seat& seat, const std::function<bool(const GoView&)>& ready) {
    for (int i = 0; i < 16; ++i) {
      auto update = ReceiveGo(seat.stream, "gameState");
      if (!update.has_value()) return std::nullopt;
      const GoView& view = update->as_gameState_or_null()->view;
      if (ready(view)) return view;
    }
    ADD_FAILURE() << "no such view for " << seat.player_id;
    return std::nullopt;
  }

  // alice alone at a new go table; her first view read.
  std::optional<Seat> AliceAtATable() {
    auto room = SeatedRoom(1);
    if (!room.has_value()) return std::nullopt;
    Seat alice = std::move(room->seats[0]);
    if (!alice.stream.Send(Go(GoMove::FromCreategame(moonbase::games::CreateGame{}))).ok()) {
      return std::nullopt;
    }
    if (!ReceiveGo(alice.stream, "gameJoined").has_value()) return std::nullopt;
    return alice;
  }

  // alice, Black, against a bot at `level` on 9×9; her view once it plays.
  std::optional<GoView> StartedAgainstBot(Seat& alice, int level) {
    if (!alice.stream.Send(AddBot(level)).ok()) return std::nullopt;
    if (!ViewWhere(alice, [](const GoView& v) { return v.players.size() == 2; })) {
      return std::nullopt;
    }
    if (!alice.stream.Send(NineByNine()).ok()) return std::nullopt;
    return ViewWhere(alice, [](const GoView& v) { return v.phase == "playing"; });
  }

  std::atomic<int64_t> now_ms_{kT0};
  std::vector<GoBotAsk> asks_;
  absl::StatusOr<std::string> answer_ = std::string("C3");
};

TEST_F(GoBotFixture, TheTablesOnePlayerSeatsABotAtTheirChosenLevel) {
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(alice->stream.Send(AddBot(5)).ok());
  auto view = ViewWhere(*alice, [](const GoView& v) { return v.players.size() == 2; });
  ASSERT_TRUE(view.has_value());
  EXPECT_FALSE(view->players[0].bot.value_or(false));
  EXPECT_EQ(view->players[1].playerId, "gnugo@5");
  EXPECT_TRUE(view->players[1].bot.value_or(false));
}

TEST_F(GoBotFixture, WithNoEngineTheHubSeatsNoBot) {
  hub_->SetGoBotEngine(nullptr);
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(alice->stream.Send(AddBot(5)).ok());
  auto refused = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "no go engine");
}

// Off turn the bot asks nothing; on turn it asks with the game so far, in
// GTP's vertices (columns skip I, rows count up from the bottom), and its
// answer is played as a player's move is.
TEST_F(GoBotFixture, OnItsTurnTheBotPlaysTheEnginesVertex) {
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 7).has_value());
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 0);
  EXPECT_TRUE(asks_.empty());
  ASSERT_TRUE(alice->stream.Send(Play(8, 0)).ok());
  ASSERT_TRUE(ViewWhere(*alice, [](const GoView& v) { return v.moveCount == 1; }));
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 1);
  ASSERT_EQ(asks_.size(), 1u);
  EXPECT_EQ(asks_[0].size, 9);
  EXPECT_EQ(asks_[0].komi, 5.5);
  EXPECT_EQ(asks_[0].level, 7);
  EXPECT_EQ(asks_[0].moves, std::vector<std::string>{"J9"});
  auto view = ViewWhere(*alice, [](const GoView& v) { return v.moveCount == 2; });
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->board[6], "..w......");
  EXPECT_EQ(view->board[0], "........b");
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 0);
}

TEST_F(GoBotFixture, APassIsAskedAsOneAndAnsweredWithOne) {
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 1).has_value());
  answer_ = std::string("PASS");
  ASSERT_TRUE(alice->stream.Send(Play(4, 4)).ok());
  ASSERT_TRUE(ViewWhere(*alice, [](const GoView& v) { return v.moveCount == 1; }));
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 1);
  ASSERT_TRUE(ViewWhere(*alice, [](const GoView& v) { return v.moveCount == 2; }));
  ASSERT_TRUE(alice->stream.Send(Play(2, 2)).ok());
  ASSERT_TRUE(ViewWhere(*alice, [](const GoView& v) { return v.moveCount == 3; }));
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 1);
  ASSERT_EQ(asks_.size(), 2u);
  EXPECT_EQ(asks_[1].moves, (std::vector<std::string>{"E5", "pass", "C7"}));
}

TEST_F(GoBotFixture, AResignationEndsTheGame) {
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 3).has_value());
  answer_ = std::string("resign");
  ASSERT_TRUE(alice->stream.Send(Play(4, 4)).ok());
  ASSERT_TRUE(ViewWhere(*alice, [](const GoView& v) { return v.moveCount == 1; }));
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 1);
  auto ended = ViewWhere(*alice, [](const GoView& v) { return v.phase == "ended"; });
  ASSERT_TRUE(ended.has_value());
  ASSERT_TRUE(ended->result.has_value());
  EXPECT_EQ(ended->result->ending, "resignation");
  EXPECT_EQ(ended->result->winner, alice->player_id);
}

// In the scoring the bot accepts whatever is marked, asking no engine: the
// player marks the dead, and a mark withdraws both acceptances, so the bot
// accepts again.
TEST_F(GoBotFixture, InTheScoringTheBotAcceptsTheMarksWithoutAsking) {
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 3).has_value());
  answer_ = std::string("pass");
  ASSERT_TRUE(alice->stream.Send(Play(4, 4)).ok());
  ASSERT_TRUE(ViewWhere(*alice, [](const GoView& v) { return v.moveCount == 1; }));
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 1);
  ASSERT_TRUE(ViewWhere(*alice, [](const GoView& v) { return v.moveCount == 2; }));
  ASSERT_TRUE(alice->stream.Send(Pass()).ok());
  ASSERT_TRUE(ViewWhere(*alice, [](const GoView& v) { return v.phase == "scoring"; }));
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 1);
  EXPECT_EQ(asks_.size(), 1u);
  ASSERT_TRUE(ViewWhere(*alice, [](const GoView& v) {
    return v.scoring.has_value() && v.scoring->acceptedBy.size() == 1;
  }));
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 0);
  ASSERT_TRUE(alice->stream.Send(Accept()).ok());
  auto ended = ViewWhere(*alice, [](const GoView& v) { return v.phase == "ended"; });
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->result->ending, "score");
  EXPECT_EQ(ended->result->winner, alice->player_id);
}

// An answer the game refuses is the engine's fault: it is not played, and
// the game is not asked about again until the retry is due, the bot's
// clock running meanwhile.
TEST_F(GoBotFixture, AnAnswerTheGameRefusesWaitsOutTheRetry) {
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 3).has_value());
  answer_ = std::string("E5");
  ASSERT_TRUE(alice->stream.Send(Play(4, 4)).ok());
  ASSERT_TRUE(ViewWhere(*alice, [](const GoView& v) { return v.moveCount == 1; }));
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 0);
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 0);
  EXPECT_EQ(asks_.size(), 1u);
  answer_ = std::string("D4");
  now_ms_ += kEngineBotRetry.count();
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 1);
  EXPECT_EQ(asks_.size(), 2u);
}

TEST_F(GoBotFixture, AnEngineThatFailsWaitsOutTheRetry) {
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 3).has_value());
  answer_ = absl::UnavailableError("gnugo is down");
  ASSERT_TRUE(alice->stream.Send(Play(4, 4)).ok());
  ASSERT_TRUE(ViewWhere(*alice, [](const GoView& v) { return v.moveCount == 1; }));
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 0);
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 0);
  EXPECT_EQ(asks_.size(), 1u);
  answer_ = std::string("D4");
  now_ms_ += kEngineBotRetry.count();
  EXPECT_EQ(hub_->PlayEngineBotsOnce(), 1);
}

}  // namespace
}  // namespace games_hub
