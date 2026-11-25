// Wordchain's quick race on the room stream, end to end: a seek finds one
// opponent in the hub's public pool, and the race starts by itself.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/apis/games_hub/wordchain_adapter.h"
#include "domains/games/libs/wordchain_race/lexicon.h"

namespace games_hub {
namespace {

using moonbase::games::GameCommands;
using moonbase::games::WordchainMove;
using moonbase::games::WordchainUpdate;
using moonbase::games::WordchainView;

constexpr int64_t kT0 = 1'700'000'000'000;

GameCommands Wordchain(WordchainMove move) {
  moonbase::games::WordchainCommand command;
  command.move = std::move(move);
  return GameCommands::FromWordchain(std::move(command));
}

GameCommands Seek(std::optional<int> letters = std::nullopt) {
  moonbase::games::WordchainSeek seek;
  seek.letters = letters;
  return Wordchain(WordchainMove::FromSeek(std::move(seek)));
}

GameCommands Play(const std::string& word) {
  moonbase::games::WordchainPlay play;
  play.word = word;
  return Wordchain(WordchainMove::FromPlay(std::move(play)));
}

GameCommands Start() {
  return Wordchain(WordchainMove::FromStartgame(moonbase::games::WordchainStartGame{}));
}

std::optional<WordchainUpdate> ReceiveWordchain(moonbase::games::PlayClientStream& stream,
                                                const std::string& wanted) {
  return ReceiveEnvelope(
      stream, wanted, "wordchain",
      [](const moonbase::games::GameEvents& event) { return event.as_wordchain_or_null(); },
      kReceiveBudget);
}

class QuickRaceFixture : public GamesHubStreamFixture {
 protected:
  void SetUp() override {
    GamesHubStreamFixture::SetUp();
    hub_->SetClock([this] { return absl::FromUnixMillis(now_ms_.load()); });
  }

  // A seat in no room, having heard sessionReady.
  std::optional<Seat> Ready() {
    auto seat = OpenSeat();
    if (!seat.has_value() || !ReceiveCase(seat->stream, "sessionReady").has_value()) {
      return std::nullopt;
    }
    return seat;
  }

  // The view a seek's sender joins with.
  static std::optional<WordchainView> Seeks(Seat& seat, std::optional<int> letters = std::nullopt) {
    if (!seat.stream.Send(Seek(letters)).ok()) return std::nullopt;
    auto joined = ReceiveWordchain(seat.stream, "gameJoined");
    if (!joined.has_value()) return std::nullopt;
    return joined->as_gameJoined_or_null()->view;
  }

  // The view a seat reads once its race has started.
  static std::optional<WordchainView> Started(Seat& seat) {
    if (!ReceiveWordchain(seat.stream, "gameStarted").has_value()) return std::nullopt;
    auto state = ReceiveWordchain(seat.stream, "gameState");
    if (!state.has_value()) return std::nullopt;
    return state->as_gameState_or_null()->view;
  }

  std::atomic<int64_t> now_ms_{kT0};
};

TEST_F(QuickRaceFixture, ALoneSeekWaitsAtAPublicTableOnTheHubsDeadline) {
  auto ann = Ready();
  ASSERT_TRUE(ann.has_value());
  auto view = Seeks(*ann);
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->phase, "waiting");
  ASSERT_EQ(view->runners.size(), 1u);
  ASSERT_TRUE(view->seek.has_value());
  EXPECT_FALSE(view->seek->letters.has_value());
  EXPECT_EQ(view->seek->deadlineMs, kT0 + kQuickSeekMs);
  EXPECT_FALSE(view->seek->expired);
  auto room = ReceiveCase(ann->stream, "roomState");
  ASSERT_TRUE(room.has_value());
  EXPECT_EQ(room->as_roomState_or_null()->roomId, kQuickRoomId);
}

TEST_F(QuickRaceFixture, TwoSeeksMeetAndTheRaceStartsByItselfThreeSecondsLater) {
  CaptureEvents();
  auto ann = Ready();
  auto bob = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value());
  auto anns = Seeks(*ann);
  ASSERT_TRUE(anns.has_value());
  auto bobs = Seeks(*bob);
  ASSERT_TRUE(bobs.has_value());
  EXPECT_EQ(bobs->gameId, anns->gameId);

  std::optional<WordchainView> race;
  for (Seat* seat : {&*ann, &*bob}) {
    auto view = Started(*seat);
    ASSERT_TRUE(view.has_value());
    race = view;
    EXPECT_EQ(view->phase, "racing");
    EXPECT_FALSE(view->practice);
    EXPECT_EQ(view->runners.size(), 2u);
    EXPECT_EQ(view->startedAtMs, kT0 + 3000);
    EXPECT_EQ(view->deadlineMs, kT0 + 3000 + 180'000);
    EXPECT_FALSE(view->seek.has_value());
  }

  // The countdown: no word before the start.
  ASSERT_TRUE(ann->stream.Send(Play("zzzz")).ok());
  auto early = ReceiveCase(ann->stream, "commandRejected");
  ASSERT_TRUE(early.has_value());
  EXPECT_EQ(early->as_commandRejected_or_null()->reason, "the race starts in a moment");

  // At the start, the ladder's first rung is taken.
  now_ms_ += 3000;
  const auto ladder =
      wordchain_race::EnableLexicon().words.shortestPath(*race->start, *race->target);
  ASSERT_TRUE(ladder.has_value());
  ASSERT_TRUE(ann->stream.Send(Play((*ladder)[1])).ok());
  auto played = ReceiveWordchain(ann->stream, "gameState");
  ASSERT_TRUE(played.has_value());
  const auto& mine = played->as_gameState_or_null()->view.runners[0];
  EXPECT_EQ(mine.playerId, ann->player_id);
  EXPECT_EQ(mine.ladder, std::vector<std::string>{(*ladder)[1]});

  // Recorded as any wordchain race is.
  const std::vector<std::string> lines = GameLines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_NE(lines[0].find(R"("event":"game_started")"), std::string::npos) << lines[0];
  EXPECT_NE(lines[0].find(R"("variant":"wordchain","players":2)"), std::string::npos) << lines[0];
}

TEST_F(QuickRaceFixture, AThirdSeekOpensATableOfItsOwn) {
  auto ann = Ready();
  auto bob = Ready();
  auto cat = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value() && cat.has_value());
  auto anns = Seeks(*ann);
  ASSERT_TRUE(anns.has_value());
  ASSERT_TRUE(Seeks(*bob).has_value());
  auto cats = Seeks(*cat);
  ASSERT_TRUE(cats.has_value());
  EXPECT_NE(cats->gameId, anns->gameId);
  EXPECT_EQ(cats->runners.size(), 1u);
}

TEST_F(QuickRaceFixture, LettersMatchWhenTheyAgreeOrEitherIsAny) {
  auto ann = Ready();
  auto bob = Ready();
  auto cat = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value() && cat.has_value());
  auto anns = Seeks(*ann, 5);
  ASSERT_TRUE(anns.has_value());
  EXPECT_EQ(anns->seek->letters, 5);
  auto bobs = Seeks(*bob, 4);
  ASSERT_TRUE(bobs.has_value());
  EXPECT_NE(bobs->gameId, anns->gameId);

  // Any takes the oldest seek, on its letters.
  auto cats = Seeks(*cat);
  ASSERT_TRUE(cats.has_value());
  EXPECT_EQ(cats->gameId, anns->gameId);
  auto view = Started(*cat);
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->start->size(), 5u);
}

TEST_F(QuickRaceFixture, AnySeekTakesTheJoinersLetters) {
  auto ann = Ready();
  auto bob = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value());
  ASSERT_TRUE(Seeks(*ann).has_value());
  ASSERT_TRUE(Seeks(*bob, 3).has_value());
  auto view = Started(*ann);
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->start->size(), 3u);
}

TEST_F(QuickRaceFixture, AnUnmatchedSeekExpiresKeepsItsSeatAndLeavesThePool) {
  auto ann = Ready();
  auto bob = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value());
  auto anns = Seeks(*ann);
  ASSERT_TRUE(anns.has_value());
  now_ms_ += kQuickSeekMs - 1;
  hub_->SweepClocksOnce();
  now_ms_ += 1;
  hub_->SweepClocksOnce();
  auto expired = ReceiveWordchain(ann->stream, "gameState");
  ASSERT_TRUE(expired.has_value());
  const WordchainView& view = expired->as_gameState_or_null()->view;
  EXPECT_EQ(view.gameId, anns->gameId);
  EXPECT_EQ(view.runners.size(), 1u);
  ASSERT_TRUE(view.seek.has_value());
  EXPECT_TRUE(view.seek->expired);

  auto bobs = Seeks(*bob);
  ASSERT_TRUE(bobs.has_value());
  EXPECT_NE(bobs->gameId, anns->gameId);
}

TEST_F(QuickRaceFixture, SeekingAgainFromAnExpiredSeekLooksAgainOnANewDeadline) {
  auto ann = Ready();
  auto bob = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value());
  auto anns = Seeks(*ann);
  ASSERT_TRUE(anns.has_value());
  now_ms_ += kQuickSeekMs;
  hub_->SweepClocksOnce();
  ASSERT_TRUE(ReceiveWordchain(ann->stream, "gameState").has_value());

  now_ms_ += 5000;
  ASSERT_TRUE(ann->stream.Send(Seek()).ok());
  auto again = ReceiveWordchain(ann->stream, "gameState");
  ASSERT_TRUE(again.has_value());
  const WordchainView& view = again->as_gameState_or_null()->view;
  EXPECT_EQ(view.gameId, anns->gameId);
  ASSERT_TRUE(view.seek.has_value());
  EXPECT_FALSE(view.seek->expired);
  EXPECT_EQ(view.seek->deadlineMs, kT0 + kQuickSeekMs + 5000 + kQuickSeekMs);

  auto bobs = Seeks(*bob);
  ASSERT_TRUE(bobs.has_value());
  EXPECT_EQ(bobs->gameId, anns->gameId);
}

TEST_F(QuickRaceFixture, LeavingTheRoomCancelsTheSeek) {
  auto ann = Ready();
  auto bob = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value());
  ASSERT_TRUE(Seeks(*ann).has_value());
  ASSERT_TRUE(
      ann->stream.Send(GameCommands::FromLeaveroom(moonbase::games::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(ann->stream, "roomLeft").has_value());
  auto bobs = Seeks(*bob);
  ASSERT_TRUE(bobs.has_value());
  EXPECT_EQ(bobs->runners.size(), 1u);
}

TEST_F(QuickRaceFixture, ASeekWaitsForAnOpponentAndDoesNotPractise) {
  auto ann = Ready();
  ASSERT_TRUE(ann.has_value());
  ASSERT_TRUE(Seeks(*ann).has_value());
  ASSERT_TRUE(ann->stream.Send(Start()).ok());
  auto refused = ReceiveCase(ann->stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "need at least 2 players to start");
}

TEST_F(QuickRaceFixture, APrivateRoomsMemberCannotSeek) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& ann = room->seats[0];
  ASSERT_TRUE(ann.stream.Send(Seek()).ok());
  auto refused = ReceiveCase(ann.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "leave your room to race someone new");
}

TEST_F(QuickRaceFixture, ThePoolHasNoCodeNoChatAndNoTablesOfOnesOwnChoosing) {
  auto ann = Ready();
  auto bob = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value());
  auto anns = Seeks(*ann);
  ASSERT_TRUE(anns.has_value());

  moonbase::games::JoinRoom join;
  join.roomId = std::string(kQuickRoomId);
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "commandRejected").has_value());

  moonbase::games::Chat chat;
  chat.text = "hi";
  ASSERT_TRUE(ann->stream.Send(GameCommands::FromChat(chat)).ok());
  auto quiet = ReceiveCase(ann->stream, "commandRejected");
  ASSERT_TRUE(quiet.has_value());
  EXPECT_EQ(quiet->as_commandRejected_or_null()->reason, "a quick race has no chat");

  ASSERT_TRUE(ann->stream.Send(Wordchain(WordchainMove::FromLeavegame(moonbase::games::LeaveGame{})))
                  .ok());
  ASSERT_TRUE(ReceiveWordchain(ann->stream, "gameLeft").has_value());
  for (GameCommands command :
       {Wordchain(WordchainMove::FromCreategame(moonbase::games::CreateGame{})),
        Wordchain(WordchainMove::FromJoingame([&] {
          moonbase::games::JoinGame join;
          join.gameId = anns->gameId;
          return join;
        }()))}) {
    ASSERT_TRUE(ann->stream.Send(command).ok());
    auto refused = ReceiveCase(ann->stream, "commandRejected");
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "a quick race is found by seeking");
  }
}

}  // namespace
}  // namespace games_hub
