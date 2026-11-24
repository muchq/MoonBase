// Wordchain on the room stream, end to end through the generated client.
//
// The fixture deals an unshuffled deck, so every race opens on the same
// puzzle; the tests solve it with the lexicon's own shortest ladder.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/apis/games_hub/wordchain_adapter.h"
#include "domains/games/libs/mithril_cpp/client.h"
#include "domains/games/libs/wordchain_race/lexicon.h"

namespace games_hub {
namespace {

using moonbase::games::GameCommands;
using moonbase::games::WordchainMove;
using moonbase::games::WordchainUpdate;
using moonbase::games::WordchainView;

constexpr int64_t kT0 = 1'700'000'000'000;

// mithril answering every ladder request with one ladder, counting them.
class CountingMithril final : public opal::http::HttpClient {
 public:
  explicit CountingMithril(std::atomic<int>& asked) : asked_(asked) {}
  opal::Outcome<opal::http::HttpResponse> Send(const opal::http::HttpRequest&) override {
    ++asked_;
    opal::http::HttpResponse response;
    response.status = 200;
    response.body = R"({"path":["cold","cord","card","ward","warm"]})";
    return response;
  }

 private:
  std::atomic<int>& asked_;
};

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

  // A room of `seats` whose first seat practises alone at a table the rest
  // can join; the first seat has heard gameStarted and read its first view.
  struct Practice {
    ManySeats table;
    WordchainView view;
  };
  std::optional<Practice> PracticeTable(int seats = 2) {
    auto room = SeatedRoom(seats);
    if (!room.has_value()) return std::nullopt;
    Practice practice{ManySeats{std::move(room->seats), room->room_id, ""}, {}};
    Seat& ann = practice.table.seats[0];
    if (!ann.stream.Send(Wordchain(WordchainMove::FromCreategame(moonbase::games::CreateGame{})))
             .ok()) {
      return std::nullopt;
    }
    auto created = ReceiveWordchain(ann.stream, "gameJoined");
    if (!created.has_value()) return std::nullopt;
    practice.table.game_id = created->as_gameJoined_or_null()->view.gameId;
    if (!ann.stream.Send(Start()).ok()) return std::nullopt;
    if (!ReceiveWordchain(ann.stream, "gameStarted").has_value()) return std::nullopt;
    auto state = ReceiveWordchain(ann.stream, "gameState");
    if (!state.has_value()) return std::nullopt;
    practice.view = state->as_gameState_or_null()->view;
    return practice;
  }

  GameCommands JoinOf(const std::string& game_id) {
    moonbase::games::JoinGame join;
    join.gameId = game_id;
    return Wordchain(WordchainMove::FromJoingame(join));
  }

  // The last roomState the stream holds once it goes quiet.
  static std::optional<moonbase::games::RoomState> LastRoomState(
      moonbase::games::PlayClientStream& stream) {
    std::optional<moonbase::games::RoomState> last;
    for (;;) {
      auto received = stream.Receive(std::chrono::milliseconds(300));
      if (!received.ok() || !received->has_value()) return last;
      if (const auto* state = (*received)->as_roomState_or_null()) last = *state;
    }
  }

  std::pair<int, int> PlayedAndWon(const std::string& room_id) {
    auto room = store_->LoadRoom(room_id);
    EXPECT_TRUE(room.ok());
    std::pair<int, int> sums{0, 0};
    if (!room.ok()) return sums;
    for (const auto& member : room->members) {
      sums.first += member.games_played;
      sums.second += member.games_won;
    }
    return sums;
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
  EXPECT_FALSE(first.practice);
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
    EXPECT_EQ(result.winners, std::vector<std::string>{ann.player_id});
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
    EXPECT_TRUE(result.winners.empty());
  }
  EXPECT_EQ(hub_->SweepClocksOnce(), 0);
}

// A shortest ladder would show the way: it waits for the race's end.
TEST_F(WordchainFixture, AShortestLadderShowsOnlyOnceTheRaceIsOver) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  for (const WordchainView& view : started->views) EXPECT_FALSE(view.solution.has_value());

  now_ms_ += 180'000;
  ASSERT_EQ(hub_->SweepClocksOnce(), 1);
  ASSERT_TRUE(ReadViews(*started));
  for (const WordchainView& view : started->views) {
    ASSERT_EQ(view.phase, "over");
    // The lexicon's own shortest ladder for the puzzle, word for word.
    EXPECT_EQ(view.solution, Solution(view)) << *view.start << " to " << *view.target;
  }
}

// A race stored before puzzles kept their ladder has none to show.
TEST(WordchainView, ARaceWithoutALadderShowsNoSolution) {
  using A = Adapter<wordchain_race::Table>;
  const std::string game_id = "W1";
  const std::vector<std::string> roster = {"ann"};
  const std::set<std::string> watchers;
  const std::optional<HostedTerms> terms;
  const std::optional<EventTag> event;
  const TableContext context{game_id, roster, watchers, terms, event, kT0};
  for (const bool kept : {false, true}) {
    wordchain_race::Puzzle puzzle{"cold", "warm", 5, "enable", {}};
    if (kept) puzzle.ladder = {"cold", "cord", "card", "ward", "warm"};
    auto table = wordchain_race::Table::open(roster, puzzle, kT0, 60'000);
    ASSERT_TRUE(table.ok()) << table.status();
    auto over = A::Flag(*table, kT0 + 60'000);
    ASSERT_TRUE(over.ok()) << over.status();
    const WordchainView view = A::ViewOf(context, &*over, "ann");
    EXPECT_EQ(view.solution.has_value(), kept);
  }
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
  // The hub's clock stood still, so both finished at the same moment on the
  // same words: a shared win.
  EXPECT_EQ(ended->as_gameEnded_or_null()->result.winners,
            (std::vector<std::string>{started->table.seats[0].player_id,
                                      started->table.seats[1].player_id}));

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
  EXPECT_EQ(won, 2);
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

// The chat bot would post the race's own shortest ladder to the room.
TEST_F(WordchainFixture, TheWordchainBotKeepsQuietWhileARaceRunsInTheRoom) {
  std::atomic<int> asked{0};
  opal::ClientConfig config = mithril::DefaultClientConfig("http://mithril:8083");
  config.http_client = std::make_shared<CountingMithril>(asked);
  auto client = mithril::Client::Create(std::move(config));
  ASSERT_TRUE(client.ok());
  hub_->StartWordchain(std::make_shared<mithril::Client>(std::move(*client)));

  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Seat& ann = started->table.seats[0];
  moonbase::games::Chat chat;
  chat.text = "/wordchain " + *started->views[0].start + " " + *started->views[0].target;
  ASSERT_TRUE(ann.stream.Send(GameCommands::FromChat(chat)).ok());
  auto own = ReceiveCase(ann.stream, "roomChat");
  ASSERT_TRUE(own.has_value());
  EXPECT_EQ(own->as_roomChat_or_null()->playerId, ann.player_id);
  const std::vector<std::string> after = CasesUntilQuiet(ann.stream);
  EXPECT_EQ(std::count(after.begin(), after.end(), "roomChat"), 0);
  EXPECT_EQ(asked.load(), 0);

  now_ms_ += 180'000;
  ASSERT_EQ(hub_->SweepClocksOnce(), 1);
  ASSERT_TRUE(ann.stream.Send(GameCommands::FromChat(chat)).ok());
  for (int i = 0; i < 2; ++i) {
    auto message = ReceiveCase(ann.stream, "roomChat");
    ASSERT_TRUE(message.has_value()) << i;
    if (i == 1) EXPECT_EQ(message->as_roomChat_or_null()->playerId, kWordchainPlayerId);
  }
  EXPECT_EQ(asked.load(), 1);
}

TEST_F(WordchainFixture, OneSeatRacesAlonePractisingAndNothingIsRecorded) {
  CaptureEvents();
  auto practice = PracticeTable();
  ASSERT_TRUE(practice.has_value());
  Seat& ann = practice->table.seats[0];
  EXPECT_TRUE(practice->view.practice);
  EXPECT_EQ(practice->view.phase, "racing");
  ASSERT_EQ(practice->view.runners.size(), 1u);

  const std::vector<std::string> ladder = Solution(practice->view);
  for (std::size_t i = 1; i < ladder.size(); ++i) {
    ASSERT_TRUE(ann.stream.Send(Play(ladder[i])).ok());
    ASSERT_TRUE(ReceiveWordchain(ann.stream, "gameState").has_value()) << ladder[i];
  }
  auto ended = ReceiveWordchain(ann.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->as_gameEnded_or_null()->result.ending, "finished");

  // Again, on the next puzzle: still practice.
  ASSERT_TRUE(ann.stream.Send(Start()).ok());
  auto next = ReceiveWordchain(ann.stream, "gameState");
  ASSERT_TRUE(next.has_value());
  const WordchainView& view = next->as_gameState_or_null()->view;
  EXPECT_TRUE(view.practice);
  EXPECT_EQ(view.phase, "racing");
  EXPECT_EQ(view.racesPlayed, 1);
  EXPECT_EQ(RunnerOf(view, ann.player_id).wins, 0);

  EXPECT_TRUE(GameLines().empty());
  EXPECT_EQ(PlayedAndWon(practice->table.room_id), std::make_pair(0, 0));
}

TEST_F(WordchainFixture, APractisingTableListsAsWaitingForASecondSeat) {
  auto practice = PracticeTable();
  ASSERT_TRUE(practice.has_value());
  auto lobby = LastRoomState(practice->table.seats[1].stream);
  ASSERT_TRUE(lobby.has_value());
  ASSERT_EQ(lobby->games.size(), 1u);
  EXPECT_EQ(lobby->games[0].status, "waiting");
  EXPECT_EQ(lobby->games[0].playerCount, 1);
}

TEST_F(WordchainFixture, AJoinEndsThePracticeRaceAndTheNextStartIsRecorded) {
  CaptureEvents();
  auto practice = PracticeTable();
  ASSERT_TRUE(practice.has_value());
  Seat& ann = practice->table.seats[0];
  Seat& bob = practice->table.seats[1];
  ASSERT_TRUE(ann.stream.Send(Play(Solution(practice->view)[1])).ok());
  ASSERT_TRUE(ReceiveWordchain(ann.stream, "gameState").has_value());

  ASSERT_TRUE(bob.stream.Send(JoinOf(practice->table.game_id)).ok());
  auto joined = ReceiveWordchain(bob.stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  auto anns = ReceiveWordchain(ann.stream, "gameState");
  ASSERT_TRUE(anns.has_value());
  for (const WordchainView* view :
       {&joined->as_gameJoined_or_null()->view, &anns->as_gameState_or_null()->view}) {
    EXPECT_EQ(view->phase, "waiting");
    EXPECT_FALSE(view->practice);
    EXPECT_EQ(view->runners.size(), 2u);
    EXPECT_FALSE(view->start.has_value());
  }

  ASSERT_TRUE(ann.stream.Send(Start()).ok());
  for (Seat* seat : {&ann, &bob}) {
    ASSERT_TRUE(ReceiveWordchain(seat->stream, "gameStarted").has_value());
    auto state = ReceiveWordchain(seat->stream, "gameState");
    ASSERT_TRUE(state.has_value());
    const WordchainView& view = state->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "racing");
    EXPECT_FALSE(view.practice);
    EXPECT_EQ(view.runners.size(), 2u);
    EXPECT_EQ(view.racesPlayed, 0);
  }
  const std::vector<std::string> lines = GameLines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_NE(lines[0].find(R"("event":"game_started")"), std::string::npos) << lines[0];
}

// The practice race's end was told; the real race's must be too.
TEST_F(WordchainFixture, ATableThatPractisedTellsItsFirstRealRaceEnding) {
  auto practice = PracticeTable();
  ASSERT_TRUE(practice.has_value());
  Seat& ann = practice->table.seats[0];
  Seat& bob = practice->table.seats[1];
  now_ms_ += 180'000;
  ASSERT_EQ(hub_->SweepClocksOnce(), 1);
  ASSERT_TRUE(ReceiveWordchain(ann.stream, "gameEnded").has_value());

  ASSERT_TRUE(bob.stream.Send(JoinOf(practice->table.game_id)).ok());
  ASSERT_TRUE(ReceiveWordchain(bob.stream, "gameJoined").has_value());
  ASSERT_TRUE(ann.stream.Send(Start()).ok());
  for (Seat* seat : {&ann, &bob}) {
    ASSERT_TRUE(ReceiveWordchain(seat->stream, "gameStarted").has_value());
  }
  now_ms_ += 180'000;
  ASSERT_EQ(hub_->SweepClocksOnce(), 1);
  for (Seat* seat : {&ann, &bob}) {
    auto ended = ReceiveWordchain(seat->stream, "gameEnded");
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->as_gameEnded_or_null()->result.ending, "timeout");
  }
  EXPECT_EQ(PlayedAndWon(practice->table.room_id), std::make_pair(2, 0));
}

TEST_F(WordchainFixture, LeavingAPracticeTableClosesItAndRecordsNothing) {
  CaptureEvents();
  auto practice = PracticeTable();
  ASSERT_TRUE(practice.has_value());
  Seat& ann = practice->table.seats[0];
  ASSERT_TRUE(ann.stream.Send(Leave()).ok());
  ASSERT_TRUE(ReceiveWordchain(ann.stream, "gameLeft").has_value());
  auto lobby = LastRoomState(practice->table.seats[1].stream);
  ASSERT_TRUE(lobby.has_value());
  EXPECT_TRUE(lobby->games.empty());
  EXPECT_TRUE(GameLines().empty());
  EXPECT_EQ(PlayedAndWon(practice->table.room_id), std::make_pair(0, 0));
}

// Another instance holding the practice race adopts the waiting table.
TEST_F(WordchainFixture, AnotherInstanceAdoptsTheTableAJoinReset) {
  auto practice = PracticeTable();
  ASSERT_TRUE(practice.has_value());
  auto instance = BuildSecondInstance(vault_, store_, chat_store_);
  ASSERT_NE(instance, nullptr);
  ASSERT_TRUE(practice->table.seats[1].stream.Send(JoinOf(practice->table.game_id)).ok());
  ASSERT_TRUE(ReceiveWordchain(practice->table.seats[1].stream, "gameJoined").has_value());
  instance->hub->OnNotify(RoomChannel(practice->table.room_id), "primary");

  auto ann = OpenSeatVia(*instance->client, practice->table.seats[0].resume_token);
  ASSERT_TRUE(ann.has_value());
  auto joined = ReceiveWordchain(ann->stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  const WordchainView& view = joined->as_gameJoined_or_null()->view;
  EXPECT_EQ(view.phase, "waiting");
  EXPECT_EQ(view.runners.size(), 2u);
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
