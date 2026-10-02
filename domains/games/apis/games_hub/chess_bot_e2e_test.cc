// A bot at a chess table (#1618), end to end through the generated client:
// Stockfish's seat added by the table's one player, its moves played when
// it is on turn, and what becomes of it when the engine fails, the
// position moves on under it, or its player leaves. The engine here is a
// fake that answers what the test says; chess_engine's own tests cover
// Stockfish.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/libs/chess_play/game_state.h"

namespace games_hub {
namespace {

using moonbase::games::ChessMove;

// White Kg6 Pe7 against Kh8: e7e8q mates.
constexpr char kMate[] = "7k/4P3/6K1/8/8/8/8/8 w - - 0 1";
constexpr int64_t kT0 = 1'800'000'000'000;

moonbase::games::GameCommands AddBot(int elo) {
  moonbase::games::ChessAddBot add;
  add.elo = elo;
  return Chess(ChessMove::FromAddbot(add));
}

class ChessBotFixture : public GamesHubStreamFixture {
 protected:
  void SetUp() override {
    GamesHubStreamFixture::SetUp();
    golf_->SetClock([this] { return absl::FromUnixMillis(now_ms_.load()); });
    golf_->SetChessOpener([this] { return opening_; });
    golf_->SetChessBotEngine([this](const ChessBotAsk& ask) -> absl::StatusOr<std::string> {
      asks_.push_back(ask);
      if (during_ask_) during_ask_();
      return answer_;
    });
  }

  // alice alone at a new chess table; her first view read.
  std::optional<Seat> AliceAtATable() {
    auto room = SeatedRoom(1);
    if (!room.has_value()) return std::nullopt;
    Seat alice = std::move(room->seats[0]);
    if (!alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok()) {
      return std::nullopt;
    }
    if (!ReceiveChess(alice.stream, "gameJoined").has_value()) return std::nullopt;
    return alice;
  }

  // alice and a bot at `elo`, started; her view once it plays.
  std::optional<moonbase::games::ChessView> StartedAgainstBot(Seat& alice, int elo) {
    if (!alice.stream.Send(AddBot(elo)).ok()) return std::nullopt;
    if (!AwaitChessView(alice.stream, [](const auto& v) { return v.players.size() == 2; }, "the bot")) {
      return std::nullopt;
    }
    if (!alice.stream.Send(Chess(ChessMove::FromStartgame({}))).ok()) return std::nullopt;
    return AwaitChessView(alice.stream, [](const auto& v) { return v.phase == "playing"; }, "start");
  }

  std::atomic<int64_t> now_ms_{kT0};
  chess_play::Opening opening_{kMate, 0};
  std::vector<ChessBotAsk> asks_;
  absl::StatusOr<std::string> answer_ = std::string("e7e8q");
  std::function<void()> during_ask_;
};

TEST_F(ChessBotFixture, TheTablesOnePlayerSeatsABotAtTheirChosenStrength) {
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(alice->stream.Send(AddBot(1500)).ok());
  auto view = AwaitChessView(alice->stream, [](const auto& v) { return v.players.size() == 2; }, "the bot");
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->players[0].playerId, alice->player_id);
  EXPECT_FALSE(view->players[0].bot.value_or(false));
  EXPECT_EQ(view->players[1].playerId, "stockfish@1500");
  EXPECT_TRUE(view->players[1].bot.value_or(false));
  // The table is full: nobody else can sit, and a second bot cannot.
  ASSERT_TRUE(alice->stream.Send(AddBot(2000)).ok());
  auto refused = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "the table is full");
}

TEST_F(ChessBotFixture, ABotIsOnlyForAChessTableNotYetStarted) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  // Not at a table.
  ASSERT_TRUE(alice.stream.Send(AddBot(1500)).ok());
  auto lone = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(lone.has_value());
  EXPECT_EQ(lone->as_commandRejected_or_null()->reason, "not in a game");
  // Started.
  opening_.white_seat = 0;
  ASSERT_TRUE(alice.stream.Send(Chess(ChessMove::FromCreategame({}))).ok());
  auto created = ReceiveChess(alice.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  moonbase::games::JoinGame join;
  join.gameId = created->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(room->seats[1].stream.Send(Chess(ChessMove::FromJoingame(join))).ok());
  ASSERT_TRUE(AwaitChessView(alice.stream, [](const auto& v) { return v.players.size() == 2; }, "bob"));
  ASSERT_TRUE(alice.stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
  ASSERT_TRUE(AwaitChessView(alice.stream, [](const auto& v) { return v.phase == "playing"; }, "start"));
  ASSERT_TRUE(alice.stream.Send(AddBot(1500)).ok());
  auto started = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(started.has_value());
  EXPECT_EQ(started->as_commandRejected_or_null()->reason, "game already started");
}

TEST_F(ChessBotFixture, WithNoEngineTheHubSeatsNoBot) {
  golf_->SetChessBotEngine(nullptr);
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(alice->stream.Send(AddBot(1500)).ok());
  auto refused = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "no chess engine");
}

// On turn, the bot asks the engine with the game so far and plays its
// answer the way a player's move is played: here, the mate. Think time
// follows the table's clock (default 3+2 → ceiling).
TEST_F(ChessBotFixture, OnItsTurnTheBotPlaysTheEnginesMove) {
  opening_.white_seat = 1;  // the bot has White
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 1500).has_value());
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 1);
  ASSERT_EQ(asks_.size(), 1u);
  EXPECT_EQ(asks_[0].fen, kMate);
  EXPECT_TRUE(asks_[0].moves.empty());
  EXPECT_EQ(asks_[0].elo, 1500);
  EXPECT_EQ(asks_[0].movetime_ms, ChessBotMovetimeMs(180'000, 2'000));
  auto view = AwaitChessView(alice->stream, [](const auto& v) { return v.phase == "ended"; }, "the mate");
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->moves, std::vector<std::string>{"e7e8q"});
  EXPECT_EQ(view->result->winner, "stockfish@1500");
  // A finished game is nothing to play.
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 0);
  EXPECT_EQ(asks_.size(), 1u);
}

// A shorter clock asks for less think time: 30s + 0 → 500 ms.
TEST_F(ChessBotFixture, ThinkTimeFollowsTheTablesTimeControl) {
  opening_.white_seat = 1;
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(alice->stream.Send(AddBot(1500)).ok());
  ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return v.players.size() == 2; }, "the bot"));
  moonbase::games::ChessStartGame start;
  start.initialSeconds = 30;
  start.incrementSeconds = 0;
  ASSERT_TRUE(alice->stream.Send(Chess(ChessMove::FromStartgame(start))).ok());
  ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return v.phase == "playing"; }, "start"));
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 1);
  ASSERT_EQ(asks_.size(), 1u);
  EXPECT_EQ(asks_[0].movetime_ms, ChessBotMovetimeMs(30'000, 0));
  EXPECT_EQ(asks_[0].movetime_ms, 500);
}

// Remaining time caps the budget: after most of a short clock has run,
// the ask is sized to what's left, not the table's full TC slice.
TEST_F(ChessBotFixture, ThinkTimeIsCappedByTheSideToMovesRemaining) {
  opening_.white_seat = 1;
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(alice->stream.Send(AddBot(1500)).ok());
  ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return v.players.size() == 2; }, "the bot"));
  moonbase::games::ChessStartGame start;
  start.initialSeconds = 30;
  start.incrementSeconds = 0;
  ASSERT_TRUE(alice->stream.Send(Chess(ChessMove::FromStartgame(start))).ok());
  ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return v.phase == "playing"; }, "start"));
  // 30s clock, 29.6s already spent → 400 ms left → 350 ms ask after reserve.
  now_ms_ += 29'600;
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 1);
  ASSERT_EQ(asks_.size(), 1u);
  EXPECT_EQ(asks_[0].movetime_ms, ChessBotMovetimeMs(30'000, 0, 400));
  EXPECT_EQ(asks_[0].movetime_ms, 400 - kChessBotClockReserveMs);
}

TEST_F(ChessBotFixture, OffItsTurnTheBotWaits) {
  opening_.white_seat = 0;  // alice has White
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 1500).has_value());
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 0);
  EXPECT_TRUE(asks_.empty());
  // alice moves; now it is the bot's, with her move in the ask.
  moonbase::games::ChessPlay play;
  play.uci = "g6f6";
  ASSERT_TRUE(alice->stream.Send(Chess(ChessMove::FromPlay(play))).ok());
  ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return !v.moves.empty(); }, "her move"));
  answer_ = std::string("h8g8");
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 1);
  ASSERT_EQ(asks_.size(), 1u);
  EXPECT_EQ(asks_[0].moves, std::vector<std::string>{"g6f6"});
}

// An engine that cannot answer costs the bot its time, not the game: no
// move, its clock running, and no second ask until the retry.
TEST_F(ChessBotFixture, AnEngineThatFailsIsAskedAgainOnlyAfterTheRetry) {
  opening_.white_seat = 1;
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 1500).has_value());
  answer_ = absl::UnavailableError("engine down");
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 0);
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 0);
  EXPECT_EQ(asks_.size(), 1u);
  now_ms_ += kChessBotRetry.count();
  answer_ = std::string("e7e8q");
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 1);
  EXPECT_EQ(asks_.size(), 2u);
}

// The engine is asked without the hub's lock, so the position can move
// on while it thinks: an answer for a position that is gone is dropped.
TEST_F(ChessBotFixture, AnAnswerForAPositionThatMovedOnIsDropped) {
  opening_.white_seat = 1;
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 1500).has_value());
  during_ask_ = [&] {
    ASSERT_TRUE(alice->stream.Send(Chess(ChessMove::FromResign({}))).ok());
    ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return v.phase == "ended"; }, "resigned"));
  };
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 0);
  while (true) {
    auto received = alice->stream.Receive(std::chrono::milliseconds(300));
    if (!received.ok() || !received->has_value()) break;
    const auto* chess = (*received)->as_chess_or_null();
    const auto* state = chess == nullptr ? nullptr : chess->update.as_gameState_or_null();
    EXPECT_TRUE(state == nullptr || state->view.moves.empty()) << "the stale move was played";
  }
}

// The live case the check is for: with several instances, another plays
// the bot's move and its player answers while this one's engine thinks.
// The answer that comes back is for a position two moves gone; legal there
// still, it would be played as a move nobody asked for. Re-entering the
// bot loop from inside the engine stands in for the other instance.
TEST_F(ChessBotFixture, AnAnswerForAGameThatMovedOnInPlayIsDropped) {
  opening_.white_seat = 1;
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 1500).has_value());
  bool elsewhere = false;
  during_ask_ = [&] {
    if (elsewhere) return;
    elsewhere = true;
    answer_ = std::string("g6f6");
    EXPECT_EQ(golf_->PlayChessBotsOnce(), 1);
    moonbase::games::ChessPlay reply;
    reply.uci = "h8g8";
    ASSERT_TRUE(alice->stream.Send(Chess(ChessMove::FromPlay(reply))).ok());
    ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return v.moves.size() == 2; }, "her reply"));
    answer_ = std::string("e7e8q");  // legal after her reply too
  };
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 0);
  while (true) {
    auto received = alice->stream.Receive(std::chrono::milliseconds(300));
    if (!received.ok() || !received->has_value()) break;
    const auto* chess = (*received)->as_chess_or_null();
    const auto* state = chess == nullptr ? nullptr : chess->update.as_gameState_or_null();
    EXPECT_TRUE(state == nullptr || state->view.moves.size() == 2) << "the stale move was played";
  }
}

// A table keeps its id and, from a fixed opening, its start across games,
// and every other game the bot has White again with no moves made. An
// answer asked for in one game is not played in a later one that happens
// to look the same.
TEST_F(ChessBotFixture, AnAnswerFromAnEarlierGameIsNotPlayedInALaterOne) {
  opening_.white_seat = 1;
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 1500).has_value());
  during_ask_ = [&] {
    for (int game = 2; game <= 3; ++game) {
      ASSERT_TRUE(alice->stream.Send(Chess(ChessMove::FromResign({}))).ok());
      ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return v.phase == "ended"; }, "resigned"));
      ASSERT_TRUE(alice->stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
      ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return v.phase == "playing"; }, "next game"));
    }
  };
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 0) << "game one's move was played in game three";
}

// An engine failure holds back the game it failed in, not the next one
// at the same table.
TEST_F(ChessBotFixture, AnEngineFailureDoesNotHoldTheNextGame) {
  opening_.white_seat = 1;
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 1500).has_value());
  answer_ = absl::UnavailableError("engine down");
  ASSERT_EQ(golf_->PlayChessBotsOnce(), 0);
  ASSERT_TRUE(alice->stream.Send(Chess(ChessMove::FromResign({}))).ok());
  ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return v.phase == "ended"; }, "resigned"));
  ASSERT_TRUE(alice->stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
  ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return v.phase == "playing"; }, "next game"));
  moonbase::games::ChessPlay play;  // alice has White now
  play.uci = "g6f6";
  ASSERT_TRUE(alice->stream.Send(Chess(ChessMove::FromPlay(play))).ok());
  ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return !v.moves.empty(); }, "her move"));
  answer_ = std::string("h8g8");
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 1);
}

// An answer the position refuses is a fault like a failed ask: no move,
// and no second ask until the retry.
TEST_F(ChessBotFixture, AnIllegalAnswerIsAskedAgainOnlyAfterTheRetry) {
  opening_.white_seat = 1;
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 1500).has_value());
  answer_ = std::string("a1a2");
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 0);
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 0);
  EXPECT_EQ(asks_.size(), 1u);
  now_ms_ += kChessBotRetry.count();
  answer_ = std::string("e7e8q");
  EXPECT_EQ(golf_->PlayChessBotsOnce(), 1);
}

// A bot holds no table on its own: its player leaving a table not yet
// started takes the table, and the bot, away.
TEST_F(ChessBotFixture, ABotAloneHoldsNoTable) {
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(alice->stream.Send(AddBot(1500)).ok());
  ASSERT_TRUE(AwaitChessView(alice->stream, [](const auto& v) { return v.players.size() == 2; }, "the bot"));
  ASSERT_TRUE(alice->stream.Send(Chess(ChessMove::FromLeavegame({}))).ok());
  auto room = AwaitRoomState(alice->stream, [](const auto& state) { return state.games.empty(); }, "the table gone");
  ASSERT_TRUE(room.has_value());
}

// A game against a bot counts for the player, and the bot, holding no
// seat in the room, counts for nothing.
TEST_F(ChessBotFixture, AGameAgainstABotCountsForThePlayer) {
  opening_.white_seat = 1;
  auto alice = AliceAtATable();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(StartedAgainstBot(*alice, 1500).has_value());
  ASSERT_EQ(golf_->PlayChessBotsOnce(), 1);
  auto room = AwaitRoomState(alice->stream, [](const auto& state) {
    return std::any_of(state.players.begin(), state.players.end(), [](const auto& p) { return p.gamesPlayed == 1; });
  }, "alice's game");
  ASSERT_TRUE(room.has_value());
  ASSERT_EQ(room->players.size(), 1u);
  EXPECT_EQ(room->players[0].gamesWon, 0);
}

}  // namespace
}  // namespace games_hub
