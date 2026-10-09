#include "domains/games/apis/one_d4_worker/games_hub_archive.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "domains/games/apis/games_hub/chess_archive.h"
#include "domains/games/apis/one_d4_worker/result.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "opal/core/error.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

class ScriptedHttpClient final : public opal::http::HttpClient {
 public:
  explicit ScriptedHttpClient(std::vector<opal::http::HttpResponse> responses)
      : responses_(std::move(responses)) {}

  opal::Outcome<opal::http::HttpResponse> Send(const opal::http::HttpRequest& request) override {
    requests_.push_back(request);
    if (next_ == responses_.size()) return opal::Error::Unknown("no scripted response");
    return responses_[next_++];
  }

  const std::vector<opal::http::HttpRequest>& requests() const { return requests_; }

 private:
  std::vector<opal::http::HttpResponse> responses_;
  std::vector<opal::http::HttpRequest> requests_;
  std::size_t next_ = 0;
};

opal::http::HttpResponse Page(std::vector<std::string> games) {
  opal::http::HttpResponse response;
  response.status = 200;
  response.headers.Set("content-type", "application/x-chess-pgn");
  for (const std::string& game : games) {
    if (!response.body.empty()) response.body += "\n";
    response.body += game;
  }
  return response;
}

opal::http::HttpResponse Status(int status) {
  opal::http::HttpResponse response;
  response.status = status;
  return response;
}

constexpr char kStandard[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
// 2026-01-15T12:00:00Z and 2026-02-01T00:00:00Z.
constexpr int64_t kMidJanuaryMs = 1'768'478'400'000;
constexpr int64_t kFebruaryMs = 1'769'904'000'000;

// A game as the hub's feed writes it: the hub's own PGN, so a change to
// [Site] or the tags it writes breaks this test rather than production.
std::string HubGame(int64_t archive_id, std::string white, std::string black,
                    int64_t ended_at_ms = kMidJanuaryMs,
                    chess_play::TimeControl clock = {180'000, 2'000}) {
  auto game = chess_play::GameState::start({white, black}, "standard",
                                           chess_play::Opening{kStandard, 0}, clock, 0, "standard");
  EXPECT_TRUE(game.ok()) << game.status();
  for (const char* uci : {"e2e4", "e7e5", "f1c4", "b8c6", "d1h5", "g8f6", "h5f7"}) {
    auto next = game->move(game->whoseTurn(), uci, 0);
    EXPECT_TRUE(next.ok()) << uci << ": " << next.status();
    game = std::move(next);
  }
  return games_hub::ChessPgnOf(archive_id, *game, ended_at_ms);
}

struct Fixture {
  std::shared_ptr<ScriptedHttpClient> transport;
  std::shared_ptr<absl::Time> now =
      std::make_shared<absl::Time>(absl::FromUnixSeconds(1'800'000'000));
  std::unique_ptr<GamesHubArchive> archive;
};

Fixture ArchiveOver(std::vector<opal::http::HttpResponse> responses) {
  Fixture fixture;
  fixture.transport = std::make_shared<ScriptedHttpClient>(std::move(responses));
  opal::ClientConfig config = GamesHubArchive::DefaultClientConfig("http://games_hub:8089");
  config.http_client = fixture.transport;
  config.retry.max_attempts = 1;
  auto archive = GamesHubArchive::Create(std::move(config), [now = fixture.now] { return *now; });
  EXPECT_TRUE(archive.ok()) << archive.status();
  fixture.archive = std::move(*archive);
  return fixture;
}

YearMonth January() { return YearMonth{.year = 2026, .month = 1}; }
YearMonth February() { return YearMonth{.year = 2026, .month = 2}; }

opal::http::HttpResponse Body(std::string body) {
  opal::http::HttpResponse response = Page({});
  response.body = std::move(body);
  return response;
}

std::vector<std::string> Urls(const std::vector<ArchivedGame>& games) {
  std::vector<std::string> urls;
  for (const ArchivedGame& game : games) urls.push_back(game.url);
  return urls;
}

// ---- which games ----

// The feed is every published game, not one player's month, so the
// archive is what narrows it: either side, that month, nobody else's.
TEST(GamesHubArchive, KeepsThePlayersGamesFromThatMonthOnly) {
  Fixture fixture = ArchiveOver({
      Page({HubGame(1, "alice", "bob"), HubGame(2, "carol", "alice"), HubGame(3, "carol", "dave"),
            HubGame(4, "alice", "bob", kFebruaryMs), HubGame(5, "alice", "bob", kFebruaryMs - 1)}),
      Page({}),
  });

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_THAT(Urls(*games),
              ElementsAre("https://muchq.com/games/chess/1", "https://muchq.com/games/chess/2",
                          "https://muchq.com/games/chess/5"));
}

// The month is half-open: its first instant is in it, the next month's is
// not.
TEST(GamesHubArchive, AGameEndingAtTheMonthsFirstInstantIsInIt) {
  constexpr int64_t kJanuaryMs = 1'767'225'600'000;  // 2026-01-01T00:00:00Z
  Fixture fixture = ArchiveOver({Page({HubGame(1, "alice", "bob", kJanuaryMs)}), Page({})});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ(games->size(), 1u);
}

// one_d4 lower-cases the player it stores; a hub id is matched whatever
// its case.
TEST(GamesHubArchive, MatchesThePlayerWhateverItsCase) {
  Fixture fixture = ArchiveOver({Page({HubGame(1, "Bouncy-Coral-Quokka", "bob")}), Page({})});

  const auto games = fixture.archive->FetchMonth("bouncy-coral-quokka", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ(games->size(), 1u);
}

// ---- reading the feed ----

// Pages run on from the last game's archive id, which only [Site] states,
// until a page comes back empty.
TEST(GamesHubArchive, ReadsOnFromTheLastArchiveIdUntilAPageIsEmpty) {
  Fixture fixture = ArchiveOver({
      Page({HubGame(3, "alice", "bob"), HubGame(7, "alice", "bob")}),
      Page({HubGame(9, "alice", "bob")}),
      Page({}),
  });

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ(games->size(), 3u);
  const auto& requests = fixture.transport->requests();
  ASSERT_EQ(requests.size(), 3u);
  EXPECT_THAT(requests[0].target, HasSubstr("/games/v2/chess.pgn?after=0"));
  EXPECT_THAT(requests[1].target, HasSubstr("after=7"));
  EXPECT_THAT(requests[2].target, HasSubstr("after=9"));
}

// ---- one read, shared ----

// The hub allows 20 requests a minute per client, and a request is up to
// twelve months: reading the whole feed for each would run out of them.
TEST(GamesHubArchive, EveryMonthAndRequestShareOneReadForAMinute) {
  Fixture fixture = ArchiveOver(
      {Page({HubGame(1, "alice", "bob"), HubGame(2, "alice", "carol", kFebruaryMs)}), Page({})});

  const auto january = fixture.archive->FetchMonth("alice", January());
  *fixture.now += absl::Seconds(59);
  const auto february = fixture.archive->FetchMonth("ALICE", February());
  const auto carols = fixture.archive->FetchMonth("carol", February());

  ASSERT_TRUE(january.ok()) << january.status();
  ASSERT_TRUE(february.ok()) << february.status();
  ASSERT_TRUE(carols.ok()) << carols.status();
  EXPECT_THAT(Urls(*january), ElementsAre("https://muchq.com/games/chess/1"));
  EXPECT_THAT(Urls(*february), ElementsAre("https://muchq.com/games/chess/2"));
  EXPECT_THAT(Urls(*carols), ElementsAre("https://muchq.com/games/chess/2"));
  EXPECT_EQ(fixture.transport->requests().size(), 2u);
}

// The current month is read again on every request, so a game that ends
// after the read reaches the next one.
TEST(GamesHubArchive, ReadsTheFeedAgainOnceTheReadIsAMinuteOld) {
  Fixture fixture =
      ArchiveOver({Page({HubGame(1, "alice", "bob")}), Page({}),
                   Page({HubGame(1, "alice", "bob"), HubGame(2, "alice", "bob")}), Page({})});

  ASSERT_TRUE(fixture.archive->FetchMonth("alice", January()).ok());
  *fixture.now += absl::Seconds(60);
  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ(games->size(), 2u);
  EXPECT_EQ(fixture.transport->requests().size(), 4u);
}

TEST(GamesHubArchive, AFailedReadIsNotKept) {
  Fixture fixture = ArchiveOver({Status(503), Page({HubGame(1, "alice", "bob")}), Page({})});

  EXPECT_FALSE(fixture.archive->FetchMonth("alice", January()).ok());
  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ(games->size(), 1u);
}

// ---- a feed it cannot read ----

// A 200 that is not PGN — a proxy's page, say — is not the end of the
// feed. Read as one, every month would be recorded complete and empty.
TEST(GamesHubArchive, ABodyThatIsNotPgnIsDataLossNotAnEmptyFeed) {
  Fixture fixture = ArchiveOver({Body("<!doctype html><title>muchq</title>")});

  const auto games = fixture.archive->FetchMonth("alice", January());

  EXPECT_EQ(games.status().code(), absl::StatusCode::kDataLoss) << games.status();
}

// The feed is read whole, so failing on one bad game would fail every
// month for every player until it aged out. It is skipped — nobody can
// say whose it is — and the cursor still moves past it.
TEST(GamesHubArchive, AGameThatWillNotParseIsSkippedAndReadPast) {
  Fixture fixture = ArchiveOver({
      Page({HubGame(1, "alice", "bob"),
            "[Event \"unterminated\n[Site \"https://muchq.com/games/chess/2\"]\n"
            "[White \"alice\"]\n\n1. e4 *\n",
            HubGame(3, "alice", "bob")}),
      Page({}),
  });

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_THAT(Urls(*games),
              ElementsAre("https://muchq.com/games/chess/1", "https://muchq.com/games/chess/3"));
  EXPECT_THAT(fixture.transport->requests()[1].target, HasSubstr("after=3"));
}

TEST(GamesHubArchive, ASiteOffTheHubDoesNotMoveTheCursor) {
  Fixture fixture = ArchiveOver({Page({"[Event \"x\"]\n[Site \"7\"]\n\n1. e4 *\n"})});

  const auto games = fixture.archive->FetchMonth("alice", January());

  EXPECT_EQ(games.status().code(), absl::StatusCode::kDataLoss) << games.status();
}

// A feed that would not move the cursor would be read forever.
TEST(GamesHubArchive, APageWithNoReadableSiteFailsRatherThanLooping) {
  Fixture fixture = ArchiveOver({Page({"[Event \"x\"]\n[Site \"elsewhere\"]\n\n1. e4 *\n"})});

  const auto games = fixture.archive->FetchMonth("alice", January());

  EXPECT_EQ(games.status().code(), absl::StatusCode::kDataLoss) << games.status();
}

TEST(GamesHubArchive, APageThatGoesBackwardsFailsRatherThanLooping) {
  Fixture fixture = ArchiveOver({Page({HubGame(7, "alice", "bob")}), Page({HubGame(7, "a", "b")})});

  const auto games = fixture.archive->FetchMonth("alice", January());

  EXPECT_EQ(games.status().code(), absl::StatusCode::kDataLoss) << games.status();
}

// A month nobody could read must not be recorded as a quiet one (#1360).
TEST(GamesHubArchive, AnUnreachableHubIsUnavailableNotAnEmptyMonth) {
  Fixture fixture = ArchiveOver({Status(500)});

  const auto games = fixture.archive->FetchMonth("alice", January());

  EXPECT_EQ(games.status().code(), absl::StatusCode::kUnavailable) << games.status();
}

TEST(GamesHubArchive, AFailureOnALaterPageStillFailsTheMonth) {
  Fixture fixture = ArchiveOver({Page({HubGame(1, "alice", "bob")}), Status(503)});

  const auto games = fixture.archive->FetchMonth("alice", January());

  EXPECT_EQ(games.status().code(), absl::StatusCode::kUnavailable) << games.status();
}

// ---- the row ----

TEST(GamesHubArchive, ReadsTheTagsIntoTheRow) {
  Fixture fixture = ArchiveOver({Page({HubGame(1, "alice", "bob")}), Page({})});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  ASSERT_EQ(games->size(), 1u);
  const ArchivedGame& game = (*games)[0];
  EXPECT_EQ(game.white_username, "alice");
  EXPECT_EQ(game.black_username, "bob");
  EXPECT_EQ(ResultOf(game.white_result, game.black_result), "1-0");
  EXPECT_EQ(game.end_time, kMidJanuaryMs / 1000);
  EXPECT_EQ(game.time_class, "blitz");
  EXPECT_THAT(game.pgn, HasSubstr("4. Qxf7# 1-0"));
}

// Sized to ride out a games_hub restart: a request claimed during a deploy
// would otherwise fail outright on connection refused.
TEST(GamesHubArchive, TheDefaultClientWaitsOutARestart) {
  const opal::ClientConfig config = GamesHubArchive::DefaultClientConfig("http://hub");
  EXPECT_EQ(config.endpoint, "http://hub");
  EXPECT_GE(config.retry.max_attempts, 5);
  EXPECT_GE(config.retry.initial_backoff, std::chrono::seconds(1));
  EXPECT_EQ(config.user_agent, "one_d4_worker/1.0") << "aura counts the caller by it";
}

// ---- the speed ----

// The hub's Event names no speed, so it comes from the clock, by Lichess's
// rule: initial plus forty increments.
TEST(TimeClassOfControl, UsesInitialPlusFortyIncrements) {
  EXPECT_EQ(TimeClassOfControl("29+0"), "ultrabullet");
  EXPECT_EQ(TimeClassOfControl("30+0"), "bullet");
  EXPECT_EQ(TimeClassOfControl("179+0"), "bullet");
  EXPECT_EQ(TimeClassOfControl("479+0"), "blitz");
  EXPECT_EQ(TimeClassOfControl("480+0"), "rapid");
  EXPECT_EQ(TimeClassOfControl("0+4"), "bullet");
  EXPECT_EQ(TimeClassOfControl("0+5"), "blitz");
  EXPECT_EQ(TimeClassOfControl("1499+0"), "rapid");
  EXPECT_EQ(TimeClassOfControl("15+0"), "ultrabullet");
  EXPECT_EQ(TimeClassOfControl("60+0"), "bullet");
  EXPECT_EQ(TimeClassOfControl("120+1"), "bullet");
  EXPECT_EQ(TimeClassOfControl("180+0"), "blitz");
  EXPECT_EQ(TimeClassOfControl("180+2"), "blitz");
  EXPECT_EQ(TimeClassOfControl("300+5"), "rapid");
  EXPECT_EQ(TimeClassOfControl("600+0"), "rapid");
  EXPECT_EQ(TimeClassOfControl("1500+0"), "classical");
}

TEST(TimeClassOfControl, AnUnreadableClockHasNoSpeed) {
  EXPECT_EQ(TimeClassOfControl(""), "");
  EXPECT_EQ(TimeClassOfControl("-"), "");
  EXPECT_EQ(TimeClassOfControl("180"), "");
  EXPECT_EQ(TimeClassOfControl("x+2"), "");
  EXPECT_EQ(TimeClassOfControl("60+0+0"), "");
}

}  // namespace
}  // namespace one_d4_worker
