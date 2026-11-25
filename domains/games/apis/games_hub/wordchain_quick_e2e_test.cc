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
using moonbase::games::WordchainRunner;
using moonbase::games::WordchainSeekState;
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

GameCommands JoinOf(const std::string& game_id) {
  moonbase::games::JoinGame join;
  join.gameId = game_id;
  return Wordchain(WordchainMove::FromJoingame(std::move(join)));
}

std::optional<WordchainUpdate> ReceiveWordchain(moonbase::games::PlayClientStream& stream,
                                                const std::string& wanted) {
  return ReceiveEnvelope(
      stream, wanted, "wordchain",
      [](const moonbase::games::GameEvents& event) { return event.as_wordchain_or_null(); },
      kReceiveBudget);
}

std::string ReasonOf(moonbase::games::PlayClientStream& stream) {
  auto refused = ReceiveCase(stream, "commandRejected");
  return refused.has_value() ? refused->as_commandRejected_or_null()->reason : "(none)";
}

// A seat with no rungs yet; `viewer` sees its own (empty) ladder.
WordchainRunner Runner(const std::string& player_id, bool viewer) {
  WordchainRunner runner;
  runner.playerId = player_id;
  if (viewer) runner.ladder = std::vector<std::string>{};
  return runner;
}

// A seek's table as its seeker sees it.
WordchainView Seeking(const std::string& game_id, const std::string& seeker,
                      std::optional<int> letters, int64_t deadline_ms, bool expired = false) {
  WordchainView view;
  view.gameId = game_id;
  view.phase = "waiting";
  view.runners = {Runner(seeker, false)};
  WordchainSeekState seek;
  seek.letters = letters;
  seek.deadlineMs = deadline_ms;
  seek.expired = expired;
  view.seek = seek;
  return view;
}

// The race `actual` shows, as `viewer` should see it the moment it opens on
// a countdown at `opened_ms`: the puzzle is the hub's shuffle, the rest is
// the rule.
WordchainView Opened(const WordchainView& actual, const std::vector<std::string>& seats,
                     const std::string& viewer, int64_t opened_ms) {
  WordchainView view;
  view.gameId = actual.gameId;
  view.phase = "racing";
  view.start = actual.start;
  view.target = actual.target;
  view.par = actual.par;
  view.lexicon = "enable";
  view.startedAtMs = opened_ms + kQuickCountdownMs;
  view.deadlineMs = opened_ms + kQuickCountdownMs + 180'000;
  for (const std::string& seat : seats) view.runners.push_back(Runner(seat, seat == viewer));
  return view;
}

class QuickRaceFixture : public GamesHubStreamFixture {
 protected:
  void SetUp() override {
    GamesHubStreamFixture::SetUp();
    hub_->SetClock([this] { return absl::FromUnixMillis(now_ms_.load()); });
  }

  // A seat in no room, having heard sessionReady.
  static std::optional<Seat> Ready(std::optional<Seat> seat) {
    if (!seat.has_value() || !ReceiveCase(seat->stream, "sessionReady").has_value()) {
      return std::nullopt;
    }
    return seat;
  }
  std::optional<Seat> Ready() { return Ready(OpenSeat()); }

  // The view a seek's sender joins with.
  static std::optional<WordchainView> Seeks(Seat& seat, std::optional<int> letters = std::nullopt) {
    if (!seat.stream.Send(Seek(letters)).ok()) return std::nullopt;
    auto joined = ReceiveWordchain(seat.stream, "gameJoined");
    if (!joined.has_value()) return std::nullopt;
    return joined->as_gameJoined_or_null()->view;
  }

  // The next view a seat reads.
  static std::optional<WordchainView> NextView(Seat& seat) {
    auto state = ReceiveWordchain(seat.stream, "gameState");
    if (!state.has_value()) return std::nullopt;
    return state->as_gameState_or_null()->view;
  }

  // The view a seat reads once its race has started.
  static std::optional<WordchainView> Started(Seat& seat) {
    if (!ReceiveWordchain(seat.stream, "gameStarted").has_value()) return std::nullopt;
    return NextView(seat);
  }

  std::atomic<int64_t> now_ms_{kT0};
};

// MARK: A seek and its match

TEST_F(QuickRaceFixture, ALoneSeekWaitsAtAPublicTableOnTheHubsDeadline) {
  auto ann = Ready();
  ASSERT_TRUE(ann.has_value());
  auto view = Seeks(*ann);
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(*view, Seeking(view->gameId, ann->player_id, std::nullopt, kT0 + kQuickSeekMs));
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

  const std::vector<std::string> seats = {ann->player_id, bob->player_id};
  std::optional<WordchainView> race;
  for (Seat* seat : {&*ann, &*bob}) {
    race = Started(*seat);
    ASSERT_TRUE(race.has_value());
    EXPECT_EQ(*race, Opened(*race, seats, seat->player_id, kT0));
  }

  // The countdown: no word before the start.
  ASSERT_TRUE(ann->stream.Send(Play("zzzz")).ok());
  EXPECT_EQ(ReasonOf(ann->stream), "the race starts in a moment");

  // At the start, the ladder's first rung is taken.
  now_ms_ += kQuickCountdownMs;
  const auto ladder =
      wordchain_race::EnableLexicon().words.shortestPath(*race->start, *race->target);
  ASSERT_TRUE(ladder.has_value());
  ASSERT_TRUE(ann->stream.Send(Play((*ladder)[1])).ok());
  auto played = NextView(*ann);
  ASSERT_TRUE(played.has_value());
  WordchainView expected = Opened(*race, seats, ann->player_id, kT0);
  expected.runners[0].words = 1;
  expected.runners[0].ladder = std::vector<std::string>{(*ladder)[1]};
  EXPECT_EQ(*played, expected);

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
  EXPECT_EQ(*cats, Seeking(cats->gameId, cat->player_id, std::nullopt, kT0 + kQuickSeekMs));
}

// A seek on one instance is matched from another: the seek is the table's
// stored terms, not anything one instance holds.
TEST_F(QuickRaceFixture, ASeekIsMatchedFromAnotherInstance) {
  auto ann = Ready();
  ASSERT_TRUE(ann.has_value());
  auto anns = Seeks(*ann);
  ASSERT_TRUE(anns.has_value());

  auto instance = BuildSecondInstance(vault_, store_, chat_store_);
  ASSERT_NE(instance, nullptr);
  instance->hub->SetClock([this] { return absl::FromUnixMillis(now_ms_.load()); });
  auto bob = Ready(OpenSeatVia(*instance->client));
  ASSERT_TRUE(bob.has_value());
  auto bobs = Seeks(*bob);
  ASSERT_TRUE(bobs.has_value());
  EXPECT_EQ(bobs->gameId, anns->gameId);
  auto race = Started(*bob);
  ASSERT_TRUE(race.has_value());
  const std::vector<std::string> seats = {ann->player_id, bob->player_id};
  EXPECT_EQ(*race, Opened(*race, seats, bob->player_id, kT0));

  // Woken, the seeker's instance shows them the same race.
  hub_->OnNotify(RoomChannel(std::string(kQuickRoomId)), "second");
  auto seen = NextView(*ann);
  ASSERT_TRUE(seen.has_value());
  EXPECT_EQ(*seen, Opened(*race, seats, ann->player_id, kT0));
}

// MARK: Letters

TEST_F(QuickRaceFixture, LettersMatchWhenTheyAgreeOrEitherIsAny) {
  auto ann = Ready();
  auto bob = Ready();
  auto cat = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value() && cat.has_value());
  auto anns = Seeks(*ann, 5);
  ASSERT_TRUE(anns.has_value());
  EXPECT_EQ(*anns, Seeking(anns->gameId, ann->player_id, 5, kT0 + kQuickSeekMs));
  auto bobs = Seeks(*bob, 4);
  ASSERT_TRUE(bobs.has_value());
  EXPECT_NE(bobs->gameId, anns->gameId);

  // Any takes the oldest seek, on its letters.
  auto cats = Seeks(*cat);
  ASSERT_TRUE(cats.has_value());
  EXPECT_EQ(cats->gameId, anns->gameId);
  auto view = Started(*cat);
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->start->size(), 5u) << *view->start;
}

TEST_F(QuickRaceFixture, AnySeekTakesTheJoinersLetters) {
  auto ann = Ready();
  auto bob = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value());
  ASSERT_TRUE(Seeks(*ann).has_value());
  ASSERT_TRUE(Seeks(*bob, 3).has_value());
  auto view = Started(*ann);
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->start->size(), 3u) << *view->start;
}

// MARK: The deadline

TEST_F(QuickRaceFixture, AnUnmatchedSeekExpiresKeepsItsSeatAndLeavesThePool) {
  auto ann = Ready();
  auto bob = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value());
  auto anns = Seeks(*ann);
  ASSERT_TRUE(anns.has_value());
  now_ms_ += kQuickSeekMs - 1;
  EXPECT_EQ(hub_->SweepSeeksOnce(), 0);
  now_ms_ += 1;
  EXPECT_EQ(hub_->SweepSeeksOnce(), 1);
  auto expired = NextView(*ann);
  ASSERT_TRUE(expired.has_value());
  EXPECT_EQ(*expired, Seeking(anns->gameId, ann->player_id, std::nullopt, kT0 + kQuickSeekMs,
                              /*expired=*/true));

  auto bobs = Seeks(*bob);
  ASSERT_TRUE(bobs.has_value());
  EXPECT_NE(bobs->gameId, anns->gameId);
}

// The clock sweep is what runs the deadline.
TEST_F(QuickRaceFixture, TheClockSweepExpiresSeeks) {
  auto ann = Ready();
  ASSERT_TRUE(ann.has_value());
  auto anns = Seeks(*ann);
  ASSERT_TRUE(anns.has_value());
  now_ms_ += kQuickSeekMs;
  hub_->SweepClocksOnce();
  auto expired = NextView(*ann);
  ASSERT_TRUE(expired.has_value());
  EXPECT_EQ(*expired, Seeking(anns->gameId, ann->player_id, std::nullopt, kT0 + kQuickSeekMs,
                              /*expired=*/true));
}

TEST_F(QuickRaceFixture, SeekingAgainFromAnExpiredSeekLooksAgainOnANewDeadline) {
  auto ann = Ready();
  auto bob = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value());
  auto anns = Seeks(*ann);
  ASSERT_TRUE(anns.has_value());
  now_ms_ += kQuickSeekMs;
  ASSERT_EQ(hub_->SweepSeeksOnce(), 1);
  ASSERT_TRUE(NextView(*ann).has_value());

  now_ms_ += 5000;
  ASSERT_TRUE(ann->stream.Send(Seek()).ok());
  auto again = NextView(*ann);
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(*again, Seeking(anns->gameId, ann->player_id, std::nullopt,
                            kT0 + kQuickSeekMs + 5000 + kQuickSeekMs));

  auto bobs = Seeks(*bob);
  ASSERT_TRUE(bobs.has_value());
  EXPECT_EQ(bobs->gameId, anns->gameId);
}

// MARK: Leaving and refusals

TEST_F(QuickRaceFixture, LeavingTheRoomCancelsTheSeek) {
  auto ann = Ready();
  auto bob = Ready();
  ASSERT_TRUE(ann.has_value() && bob.has_value());
  ASSERT_TRUE(Seeks(*ann).has_value());
  ASSERT_TRUE(ann->stream.Send(GameCommands::FromLeaveroom(moonbase::games::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(ann->stream, "roomLeft").has_value());
  auto bobs = Seeks(*bob);
  ASSERT_TRUE(bobs.has_value());
  EXPECT_EQ(*bobs, Seeking(bobs->gameId, bob->player_id, std::nullopt, kT0 + kQuickSeekMs));
}

TEST_F(QuickRaceFixture, ASeekWaitsForAnOpponentAndDoesNotPractise) {
  auto ann = Ready();
  ASSERT_TRUE(ann.has_value());
  ASSERT_TRUE(Seeks(*ann).has_value());
  ASSERT_TRUE(ann->stream.Send(Start()).ok());
  EXPECT_EQ(ReasonOf(ann->stream), "need at least 2 players to start");
}

TEST_F(QuickRaceFixture, APrivateRoomsMemberCannotSeek) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& ann = room->seats[0];
  ASSERT_TRUE(ann.stream.Send(Seek()).ok());
  EXPECT_EQ(ReasonOf(ann.stream), "leave your room to race someone new");
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
  EXPECT_EQ(ReasonOf(bob->stream), "a quick race is found by seeking");

  moonbase::games::Chat chat;
  chat.text = "hi";
  ASSERT_TRUE(ann->stream.Send(GameCommands::FromChat(chat)).ok());
  EXPECT_EQ(ReasonOf(ann->stream), "a quick race has no chat");

  ASSERT_TRUE(
      ann->stream.Send(Wordchain(WordchainMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveWordchain(ann->stream, "gameLeft").has_value());
  ASSERT_TRUE(
      ann->stream.Send(Wordchain(WordchainMove::FromCreategame(moonbase::games::CreateGame{})))
          .ok());
  EXPECT_EQ(ReasonOf(ann->stream), "a quick race is found by seeking");
  ASSERT_TRUE(ann->stream.Send(JoinOf(anns->gameId)).ok());
  EXPECT_EQ(ReasonOf(ann->stream), "a quick race is found by seeking");
}

// MARK: Must not change

// A room's table keeps four seats and its starter's say: no seek, no
// countdown.
TEST_F(QuickRaceFixture, ARoomTableSeatsFourAndStartsWhenAskedWithNoCountdown) {
  auto room = SeatedRoom(4);
  ASSERT_TRUE(room.has_value());
  auto& seats = room->seats;
  ASSERT_TRUE(
      seats[0].stream.Send(Wordchain(WordchainMove::FromCreategame(moonbase::games::CreateGame{})))
          .ok());
  auto created = ReceiveWordchain(seats[0].stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  const WordchainView open = created->as_gameJoined_or_null()->view;
  EXPECT_FALSE(open.seek.has_value());
  for (std::size_t i = 1; i < seats.size(); ++i) {
    ASSERT_TRUE(seats[i].stream.Send(JoinOf(open.gameId)).ok());
    ASSERT_TRUE(ReceiveWordchain(seats[i].stream, "gameJoined").has_value()) << i;
  }
  ASSERT_TRUE(seats[0].stream.Send(Start()).ok());
  auto race = Started(seats[0]);
  ASSERT_TRUE(race.has_value());
  EXPECT_EQ(race->startedAtMs, kT0);
  EXPECT_EQ(race->deadlineMs, kT0 + 180'000);
  EXPECT_EQ(race->runners.size(), 4u);
}

}  // namespace
}  // namespace games_hub
