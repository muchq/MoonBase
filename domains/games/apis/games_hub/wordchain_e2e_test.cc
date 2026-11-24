// Wordchain on the room stream, end to end through the generated client.
//
// The fixture deals an unshuffled deck, so every race opens on the same
// puzzle; the tests solve it with the lexicon's own shortest ladder.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <initializer_list>
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

GameCommands Play(const std::string& word) {
  moonbase::games::WordchainPlay play;
  play.word = word;
  return Wordchain(WordchainMove::FromPlay(std::move(play)));
}

GameCommands Start(std::optional<int> letters = std::nullopt) {
  moonbase::games::WordchainStartGame start;
  start.letters = letters;
  return Wordchain(WordchainMove::FromStartgame(std::move(start)));
}

GameCommands Leave() {
  return Wordchain(WordchainMove::FromLeavegame(moonbase::games::LeaveGame{}));
}

std::optional<WordchainUpdate> ReceiveWordchain(moonbase::games::PlayClientStream& stream,
                                                const std::string& wanted,
                                                std::chrono::milliseconds budget = kReceiveBudget) {
  return ReceiveEnvelope(
      stream, wanted, "wordchain",
      [](const moonbase::games::GameEvents& event) { return event.as_wordchain_or_null(); },
      budget);
}

const moonbase::games::WordchainRunner& RunnerOf(const WordchainView& view,
                                                 const std::string& player_id) {
  for (const auto& runner : view.runners) {
    if (runner.playerId == player_id) return runner;
  }
  ADD_FAILURE() << "no runner " << player_id;
  return view.runners.front();
}

class WordchainFixture : public GamesHubStreamFixture {
 protected:
  void SetUp() override {
    GamesHubStreamFixture::SetUp();
    hub_->SetClock([this] { return absl::FromUnixMillis(now_ms_.load()); });
  }

  // A started table of `seats`, the first its creator; each seat has heard
  // gameStarted and read its first view, `views` in seat order.
  struct Started {
    ManySeats table;
    std::vector<WordchainView> views;
  };
  std::optional<Started> StartedTable(int seats = 2) {
    auto room = SeatedRoom(seats);
    if (!room.has_value()) return std::nullopt;
    ManySeats table{std::move(room->seats), room->room_id, ""};
    if (!table.seats[0]
             .stream.Send(Wordchain(WordchainMove::FromCreategame(moonbase::games::CreateGame{})))
             .ok()) {
      return std::nullopt;
    }
    auto created = ReceiveWordchain(table.seats[0].stream, "gameJoined");
    if (!created.has_value()) return std::nullopt;
    table.game_id = created->as_gameJoined_or_null()->view.gameId;
    moonbase::games::JoinGame join;
    join.gameId = table.game_id;
    for (std::size_t i = 1; i < table.seats.size(); ++i) {
      if (!table.seats[i].stream.Send(Wordchain(WordchainMove::FromJoingame(join))).ok()) {
        return std::nullopt;
      }
      if (!ReceiveWordchain(table.seats[i].stream, "gameJoined").has_value()) return std::nullopt;
    }
    if (!table.seats[0].stream.Send(Start()).ok()) return std::nullopt;
    Started started{std::move(table), std::vector<WordchainView>(seats)};
    for (Seat& seat : started.table.seats) {
      if (!ReceiveWordchain(seat.stream, "gameStarted").has_value()) return std::nullopt;
    }
    if (!ReadViews(started)) return std::nullopt;
    return started;
  }

  // Each seat's next view, into `views`.
  static bool ReadViews(Started& started) {
    for (std::size_t i = 0; i < started.views.size(); ++i) {
      auto state = ReceiveWordchain(started.table.seats[i].stream, "gameState");
      if (!state.has_value()) return false;
      started.views[i] = state->as_gameState_or_null()->view;
    }
    return true;
  }

  // The lexicon's shortest ladder for the race the views show, start and
  // target included.
  static std::vector<std::string> Solution(const WordchainView& view) {
    auto path = wordchain_race::EnableLexicon().words.shortestPath(*view.start, *view.target);
    if (!path.has_value()) {
      ADD_FAILURE() << "no ladder from " << *view.start << " to " << *view.target;
      return {};
    }
    return *path;
  }

  // A ladder one word longer than par: its first step leaves the climb no
  // shorter, so finishing on it ends nothing by itself.
  static std::vector<std::string> OffParLadder(const WordchainView& view) {
    const auto& words = wordchain_race::EnableLexicon().words;
    const std::size_t par = static_cast<std::size_t>(*view.par);
    for (const std::string& first : words.wordsOfLength(static_cast<int>(view.start->size()))) {
      if (!wordchain_race::OneLetterApart(*view.start, first)) continue;
      auto rest = words.shortestPath(first, *view.target);
      if (rest.has_value() && rest->size() == par) return *rest;
    }
    ADD_FAILURE() << "no ladder one past par from " << *view.start;
    return {};
  }

  // Seats `seats` each climb `ladder`, every seat reading each view.
  static void Climb(Started& started, std::initializer_list<int> seats,
                    const std::vector<std::string>& ladder) {
    for (int seat : seats) {
      for (const std::string& word : ladder) {
        ASSERT_TRUE(started.table.seats[seat].stream.Send(Play(word)).ok());
        ASSERT_TRUE(ReadViews(started)) << word;
      }
    }
  }

  std::atomic<int64_t> now_ms_{kT0};
};

TEST_F(WordchainFixture, EverySeatRacesThePuzzleOnOneClock) {
  auto started = StartedTable(3);
  ASSERT_TRUE(started.has_value());
  const WordchainView& first = started->views[0];
  EXPECT_EQ(first.phase, "racing");
  ASSERT_TRUE(first.start.has_value());
  ASSERT_TRUE(first.target.has_value());
  EXPECT_EQ(first.lexicon, "enable");
  EXPECT_EQ(first.startedAtMs, kT0);
  EXPECT_EQ(first.deadlineMs, kT0 + 180'000);
  EXPECT_EQ(static_cast<int>(Solution(first).size()), *first.par);
  for (const WordchainView& view : started->views) {
    EXPECT_EQ(view.start, first.start);
    EXPECT_EQ(view.target, first.target);
    EXPECT_EQ(view.runners.size(), 3u);
  }
}

TEST_F(WordchainFixture, ARunnerSeesOnlyTheirOwnLadderUntilTheRaceIsOver) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Seat& ann = started->table.seats[0];
  const std::vector<std::string> ladder = Solution(started->views[0]);
  ASSERT_GE(ladder.size(), 3u);

  ASSERT_TRUE(ann.stream.Send(Play(ladder[1])).ok());
  ASSERT_TRUE(ReadViews(*started));
  EXPECT_EQ(RunnerOf(started->views[0], ann.player_id).ladder,
            std::vector<std::string>{ladder[1]});
  const auto& seen_by_bob = RunnerOf(started->views[1], ann.player_id);
  EXPECT_EQ(seen_by_bob.words, 1);
  EXPECT_FALSE(seen_by_bob.ladder.has_value());
}

TEST_F(WordchainFixture, AFinishOnParWinsTheRaceAndShowsEveryLadder) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Seat& ann = started->table.seats[0];
  Seat& bob = started->table.seats[1];
  const std::vector<std::string> ladder = Solution(started->views[0]);
  for (std::size_t i = 1; i < ladder.size(); ++i) {
    ASSERT_TRUE(ann.stream.Send(Play(ladder[i])).ok());
    ASSERT_TRUE(ReadViews(*started)) << "after " << ladder[i];
  }
  for (Seat* seat : {&ann, &bob}) {
    auto ended = ReceiveWordchain(seat->stream, "gameEnded");
    ASSERT_TRUE(ended.has_value());
    const auto& result = ended->as_gameEnded_or_null()->result;
    EXPECT_EQ(result.ending, "finished");
    EXPECT_EQ(result.winner, ann.player_id);
  }
  const WordchainView& bobs = started->views[1];
  EXPECT_EQ(bobs.phase, "over");
  EXPECT_EQ(bobs.racesPlayed, 1);
  EXPECT_EQ(RunnerOf(bobs, ann.player_id).ladder,
            std::vector<std::string>(ladder.begin() + 1, ladder.end()));
  EXPECT_EQ(RunnerOf(bobs, ann.player_id).wins, 1);
}

TEST_F(WordchainFixture, AWordOffTheLadderIsRefusedInBand) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Seat& ann = started->table.seats[0];
  ASSERT_TRUE(ann.stream.Send(Play("qzqz")).ok());
  auto refused = ReceiveCase(ann.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "\"qzqz\" is not a word here");
}

TEST_F(WordchainFixture, TheClockEndsARaceNobodyIsToMoveIn) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  now_ms_ += 179'999;
  EXPECT_EQ(hub_->SweepClocksOnce(), 0);
  now_ms_ += 1;
  EXPECT_EQ(hub_->SweepClocksOnce(), 1);
  for (Seat& seat : started->table.seats) {
    auto ended = ReceiveWordchain(seat.stream, "gameEnded");
    ASSERT_TRUE(ended.has_value());
    const auto& result = ended->as_gameEnded_or_null()->result;
    EXPECT_EQ(result.ending, "timeout");
    EXPECT_FALSE(result.winner.has_value());
  }
  EXPECT_EQ(hub_->SweepClocksOnce(), 0);
}

TEST_F(WordchainFixture, StartingAgainAfterARaceRunsTheNextOne) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  now_ms_ += 180'000;
  ASSERT_EQ(hub_->SweepClocksOnce(), 1);
  for (Seat& seat : started->table.seats) {
    ASSERT_TRUE(ReceiveWordchain(seat.stream, "gameEnded").has_value());
  }
  ASSERT_TRUE(started->table.seats[0].stream.Send(Start(5)).ok());
  for (Seat& seat : started->table.seats) {
    auto state = ReceiveWordchain(seat.stream, "gameState");
    ASSERT_TRUE(state.has_value());
    const WordchainView& view = state->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "racing");
    EXPECT_EQ(view.racesPlayed, 1);
    EXPECT_EQ(view.start->size(), 5u);
  }
}

TEST_F(WordchainFixture, ALeaveDownToOneClosesTheTable) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Seat& ann = started->table.seats[0];
  ASSERT_TRUE(started->table.seats[1].stream.Send(Leave()).ok());
  auto ended = ReceiveWordchain(ann.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->as_gameEnded_or_null()->result.ending, "leave");
}

TEST_F(WordchainFixture, ALeaveThatEndsARaceOnAnOpenTableCreditsIt) {
  CaptureEvents();
  auto started = StartedTable(3);
  ASSERT_TRUE(started.has_value());
  Climb(*started, {0, 1}, OffParLadder(started->views[0]));
  ASSERT_EQ(started->views[0].phase, "racing");

  ASSERT_TRUE(started->table.seats[2].stream.Send(Leave()).ok());
  auto ended = ReceiveWordchain(started->table.seats[0].stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->as_gameEnded_or_null()->result.ending, "finished");
  EXPECT_EQ(ended->as_gameEnded_or_null()->result.winner, started->table.seats[0].player_id);

  const std::vector<std::string> lines = GameLines();
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_NE(lines[1].find(R"("event":"game_finished")"), std::string::npos) << lines[1];
  auto room = store_->LoadRoom(started->table.room_id);
  ASSERT_TRUE(room.ok());
  int played = 0;
  int won = 0;
  for (const auto& member : room->members) {
    played += member.games_played;
    won += member.games_won;
  }
  EXPECT_EQ(played, 3);
  EXPECT_EQ(won, 1);
}

TEST_F(WordchainFixture, ALeaveAfterATimeoutLeavesTheTableReadable) {
  auto started = StartedTable(3);
  ASSERT_TRUE(started.has_value());
  Climb(*started, {0, 1}, OffParLadder(started->views[0]));
  now_ms_ += 180'000;
  ASSERT_EQ(hub_->SweepClocksOnce(), 1);
  // The sweep's views and gameEnded first, then the view after the leave.
  ASSERT_TRUE(ReceiveWordchain(started->table.seats[0].stream, "gameEnded").has_value());
  ASSERT_TRUE(started->table.seats[2].stream.Send(Leave()).ok());
  auto after = ReceiveWordchain(started->table.seats[0].stream, "gameState");
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->as_gameState_or_null()->view.runners.size(), 3u);

  auto row = store_->LoadGame(started->table.room_id, started->table.game_id);
  ASSERT_TRUE(row.ok()) << row.status();
  ASSERT_TRUE(row->has_value() && (*row)->state.has_value());
  const auto& table = std::get<wordchain_race::Table>(*(*row)->state);
  EXPECT_EQ(table.players().size(), 2u);
  EXPECT_TRUE(Adapter<wordchain_race::Table>::Deserialize(
                  Adapter<wordchain_race::Table>::Serialize(table))
                  .ok());
}

TEST_F(WordchainFixture, ATableSeatsFour) {
  ExpectSeatCap<WordchainMove>(
      4, [](WordchainMove move) { return Wordchain(std::move(move)); },
      [](moonbase::games::PlayClientStream& stream, const std::string& wanted) {
        return ReceiveWordchain(stream, wanted);
      });
}

}  // namespace
}  // namespace games_hub
