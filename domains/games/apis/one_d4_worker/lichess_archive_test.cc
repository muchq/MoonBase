#include "domains/games/apis/one_d4_worker/lichess_archive.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "domains/games/apis/one_d4_worker/result.h"
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
    const int concurrent = ++inside_;
    int seen = peak_.load();
    while (concurrent > seen && !peak_.compare_exchange_weak(seen, concurrent)) {
    }
    // Long enough that a second caller would be inside this one if nothing
    // kept it out.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    requests_.push_back(request);
    const std::size_t index = next_ < responses_.size() ? next_++ : responses_.size();
    --inside_;
    if (index == responses_.size()) return opal::Error::Unknown("no scripted response");
    return responses_[index];
  }

  int peak_concurrency() const { return peak_.load(); }

  const std::vector<opal::http::HttpRequest>& requests() const { return requests_; }

 private:
  std::vector<opal::http::HttpResponse> responses_;
  std::vector<opal::http::HttpRequest> requests_;
  std::size_t next_ = 0;
  std::atomic<int> inside_{0};
  std::atomic<int> peak_{0};
};

/// One Lichess game, spelled as Lichess spells them.
std::string AGame(std::string event, std::string site, std::string white, std::string black,
                  std::string result, std::string extra = "") {
  return absl::StrCat("[Event \"", event, "\"]\n[Site \"", site, "\"]\n[White \"", white,
                      "\"]\n[Black \"", black, "\"]\n[Result \"", result,
                      "\"]\n[UTCDate \"2026.01.15\"]\n[UTCTime \"12:34:56\"]\n[WhiteElo "
                      "\"2100\"]\n[BlackElo \"2050\"]\n[ECO \"B00\"]\n[Opening \"Goldsmith "
                      "Defense\"]\n",
                      extra, "\n1. e4 e5 2. Nf3 Nc6 ", result, "\n\n");
}

opal::http::HttpResponse Pgn(std::string body) {
  opal::http::HttpResponse response;
  response.status = 200;
  response.headers.Set("content-type", "application/x-chess-pgn");
  response.body = std::move(body);
  return response;
}

struct Fixture {
  std::shared_ptr<ScriptedHttpClient> transport;
  std::unique_ptr<lichess::Client> client;
  std::unique_ptr<LichessArchive> archive;
};

Fixture ArchiveOver(std::vector<opal::http::HttpResponse> responses,
                    std::string_view token = "lip_secret") {
  Fixture fixture;
  fixture.transport = std::make_shared<ScriptedHttpClient>(std::move(responses));
  opal::ClientConfig config = lichess::WithBearerToken(lichess::DefaultClientConfig(), token);
  config.http_client = fixture.transport;
  auto client = lichess::Client::Create(std::move(config));
  EXPECT_TRUE(client.ok()) << client.error().message();
  fixture.client = std::make_unique<lichess::Client>(std::move(*client));
  fixture.archive = std::make_unique<LichessArchive>(*fixture.client);
  return fixture;
}

YearMonth January() { return YearMonth{.year = 2026, .month = 1}; }

// ---- the range ----

// Lichess has no monthly endpoint. January is [Jan 1 00:00, Feb 1 00:00) in
// milliseconds — the half-open shape matters at the boundary, where a closed
// range would index the first game of February into January as well.
TEST(LichessArchive, MapsAMonthOntoAHalfOpenMillisecondRange) {
  Fixture fixture = ArchiveOver({Pgn("")});

  ASSERT_TRUE(fixture.archive->FetchMonth("alice", January()).ok());

  const std::string& target = fixture.transport->requests()[0].target;
  EXPECT_THAT(target, HasSubstr("since=1767225600000"));
  EXPECT_THAT(target, HasSubstr("until=1769904000000"));
}

// ---- the row ----

TEST(LichessArchive, ReadsTheTagsIntoTheRow) {
  Fixture fixture = ArchiveOver(
      {Pgn(AGame("Rated Blitz game", "https://lichess.org/abcd1234", "alice", "bob", "1-0"))});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  ASSERT_EQ(games->size(), 1u);
  EXPECT_EQ((*games)[0].url, "https://lichess.org/abcd1234");
  EXPECT_EQ((*games)[0].white_username, "alice");
  EXPECT_EQ((*games)[0].black_username, "bob");
  EXPECT_EQ((*games)[0].white_rating, 2100);
  EXPECT_EQ((*games)[0].black_rating, 2050);
}

// The row stores the PGN, and the parser hands back tags and moves rather
// than source — so the bytes have to be kept on the way past.
TEST(LichessArchive, KeepsEachGamesPgnVerbatim) {
  const std::string first =
      AGame("Rated Blitz game", "https://lichess.org/aaaa1111", "alice", "bob", "1-0");
  const std::string second =
      AGame("Rated Bullet game", "https://lichess.org/bbbb2222", "carol", "dave", "0-1");
  Fixture fixture = ArchiveOver({Pgn(absl::StrCat(first, second))});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  ASSERT_EQ(games->size(), 2u);
  EXPECT_THAT((*games)[0].pgn, HasSubstr("https://lichess.org/aaaa1111"));
  EXPECT_THAT((*games)[0].pgn, HasSubstr("1. e4 e5 2. Nf3 Nc6 1-0"));
  EXPECT_THAT((*games)[0].pgn, ::testing::Not(HasSubstr("bbbb2222")))
      << "one game's text ran into the next";
  EXPECT_THAT((*games)[1].pgn, HasSubstr("https://lichess.org/bbbb2222"));
}

// Lichess states the result once, chess.com states it per side, and the row
// stores standard notation derived from the chess.com words. The words this
// picks therefore have to round-trip back to what Lichess said.
TEST(LichessArchive, ResultSurvivesTheRoundTripThroughChessComsVocabulary) {
  for (const std::string& expected :
       {std::string("1-0"), std::string("0-1"), std::string("1/2-1/2")}) {
    Fixture fixture = ArchiveOver(
        {Pgn(AGame("Rated Blitz game", "https://lichess.org/a", "alice", "bob", expected))});

    const auto games = fixture.archive->FetchMonth("alice", January());

    ASSERT_TRUE(games.ok()) << games.status();
    ASSERT_EQ(games->size(), 1u);
    EXPECT_EQ(ResultOf((*games)[0].white_result, (*games)[0].black_result), expected);
  }
}

TEST(LichessArchive, AnUnfinishedGameHasNoResultRatherThanAWrongOne) {
  Fixture fixture =
      ArchiveOver({Pgn(AGame("Rated Blitz game", "https://lichess.org/a", "alice", "bob", "*"))});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ(ResultOf((*games)[0].white_result, (*games)[0].black_result), "unknown");
}

// ---- titles ----

// Lichess states a title on the game itself, which chess.com never does —
// against 493 games of hikaru's August 2026 archive, zero carry any *Title
// tag (#1527). So for a Lichess game the title is free: no roster, no
// per-player lookup, no extra request.
TEST(LichessArchive, TitlesComeFromTheGamesOwnTags) {
  Fixture fixture = ArchiveOver(
      {Pgn(AGame("Rated Blitz game", "https://lichess.org/a", "Sultai", "Zhigalko_Sergei", "0-1",
                 "[WhiteTitle \"CM\"]\n[BlackTitle \"GM\"]\n"))});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ((*games)[0].white_title, "CM");
  EXPECT_EQ((*games)[0].black_title, "GM");
}

// An untitled player carries no tag at all, which is the same absence a
// chess.com game shows for everyone. Empty is the only honest reading: it
// says nothing, and nothing is what gets stored.
TEST(LichessArchive, AnAbsentTitleTagIsEmptyRatherThanInvented) {
  Fixture fixture =
      ArchiveOver({Pgn(AGame("Rated Blitz game", "https://lichess.org/a", "a", "b", "1-0"))});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ((*games)[0].white_title, "");
  EXPECT_EQ((*games)[0].black_title, "");
}

// One side titled and the other not is the common case, and the sides must
// not borrow from each other.
TEST(LichessArchive, OneSidedTitlesStayOnTheirOwnSide) {
  Fixture fixture = ArchiveOver({Pgn(AGame("Rated Blitz game", "https://lichess.org/a", "gm",
                                           "nobody", "1-0", "[WhiteTitle \"GM\"]\n"))});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ((*games)[0].white_title, "GM");
  EXPECT_EQ((*games)[0].black_title, "");
}

// ---- speeds ----

TEST(LichessArchive, TimeClassComesFromTheEventTag) {
  const std::string pgn =
      absl::StrCat(AGame("Rated Blitz game", "https://lichess.org/a", "a", "b", "1-0"),
                   AGame("Casual Bullet game", "https://lichess.org/b", "a", "b", "1-0"),
                   AGame("Rated Rapid game", "https://lichess.org/c", "a", "b", "1-0"),
                   AGame("Rated Classical game", "https://lichess.org/d", "a", "b", "1-0"),
                   AGame("Rated Correspondence game", "https://lichess.org/e", "a", "b", "1-0"));
  Fixture fixture = ArchiveOver({Pgn(pgn)});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  std::vector<std::string> classes;
  for (const ArchivedGame& game : *games) classes.push_back(game.time_class);
  // Correspondence becomes chess.com's name for the same thing; the rest
  // keep theirs.
  EXPECT_THAT(classes, ElementsAre("blitz", "bullet", "rapid", "classical", "daily"));
}

// UltraBullet is a speed chess.com does not have, and exclude_bullet tests
// time_class == "bullet" — so a request that excludes bullet keeps these.
// Pinned because it is surprising, not because it is settled: #1527 asks
// whether exclude_bullet means "bullet" or "bullet and faster", and this is
// the answer today.
TEST(LichessArchive, UltraBulletIsItsOwnSpeedAndSoSurvivesExcludeBullet) {
  Fixture fixture =
      ArchiveOver({Pgn(AGame("Rated UltraBullet game", "https://lichess.org/a", "a", "b", "1-0"))});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ((*games)[0].time_class, "ultrabullet");
  EXPECT_NE((*games)[0].time_class, "bullet");
}

// A tournament game's Event carries the tournament name and URL after the
// speed, so matching has to survive the trailing text.
TEST(LichessArchive, ReadsTheSpeedOutOfATournamentEvent) {
  Fixture fixture =
      ArchiveOver({Pgn(AGame("Rated Blitz tournament https://lichess.org/tournament/xyz",
                             "https://lichess.org/a", "a", "b", "1-0"))});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ((*games)[0].time_class, "blitz");
}

// ---- time ----

// Lichess counts the range in milliseconds; the row stores seconds, as
// chess.com's end_time does. Storing milliseconds here would date every
// Lichess game to 1970 in one direction or 55000 AD in the other.
TEST(LichessArchive, EndTimeIsSecondsFromTheUtcTags) {
  Fixture fixture =
      ArchiveOver({Pgn(AGame("Rated Blitz game", "https://lichess.org/a", "a", "b", "1-0"))});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ((*games)[0].end_time, 1768480496);
}

TEST(LichessArchive, AGameWithNoUtcTagsHasNoEndTimeRatherThanAWrongOne) {
  Fixture fixture = ArchiveOver(
      {Pgn("[Event \"Rated Blitz game\"]\n[Site \"https://lichess.org/a\"]\n[White \"a\"]\n[Black "
           "\"b\"]\n[Result \"1-0\"]\n\n1. e4 e5 1-0\n")});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  ASSERT_EQ(games->size(), 1u);
  EXPECT_EQ((*games)[0].end_time, 0);
}

// ---- openings ----

// Lichess states the opening; chess.com states neither name nor code, which
// is why the run scrapes its ECOUrl slug. Leaving this unmapped gave every
// Lichess row a blank opening name while the PGN carried one.
TEST(LichessArchive, ReadsTheOpeningNameFromTheTag) {
  Fixture fixture =
      ArchiveOver({Pgn(AGame("Rated Blitz game", "https://lichess.org/a", "alice", "bob", "1-0"))});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ((*games)[0].opening_name, "Goldsmith Defense");
  EXPECT_TRUE((*games)[0].eco_url.empty()) << "eco_url is chess.com's slug and has no analogue";
}

// ---- games that will not parse ----

// IndexRun writes a row for a PGN it cannot parse rather than dropping it
// (RecordsAGameWhosePgnWillNotEvenParse). Skipping here would pre-empt that
// policy from a layer that does not own it.
TEST(LichessArchive, KeepsAGameWhosePgnWillNotParse) {
  // An unterminated tag value: the reader rejects the whole block rather
  // than reading a game out of it.
  Fixture fixture = ArchiveOver({Pgn("[Event \"unterminated\n1. e4 e5 1-0\n")});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  ASSERT_EQ(games->size(), 1u);
  EXPECT_THAT((*games)[0].pgn, HasSubstr("unterminated"));
}

// The failure this exists to prevent: if every block were dropped, the month
// would come back an empty success, and a quiet month is recorded complete —
// so a parser that drifted would cache "indexed, no games" over a month full
// of them, and nothing would fetch it again (#1360).
TEST(LichessArchive, AMonthOfUnparseableGamesIsNotAQuietMonth) {
  Fixture fixture = ArchiveOver(
      {Pgn("[Event \"Rated Blitz game\"]\nnot a pgn\n\n[Event \"Rated Blitz game\"]\nalso not\n")});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_EQ(games->size(), 2u) << "the month read as empty and would be cached complete";
}

// ---- the port's contract ----

// A month the player was quiet in is an empty vector, not an error. Failing
// here would fail a run over a month somebody simply did not play.
TEST(LichessArchive, AQuietMonthIsAnEmptyVectorNotAnError) {
  Fixture fixture = ArchiveOver({Pgn("")});

  const auto games = fixture.archive->FetchMonth("alice", January());

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_TRUE(games->empty());
}

// NotFound means the archive is not there at all, and must fail the run
// (#1360) — completing on it would record "indexed, no games" for a month
// nobody read. With Accept: PGN the 404 body is an HTML page, so this has to
// survive there being nothing in it to parse.
TEST(LichessArchive, A404IsNotFoundSoTheRunFails) {
  opal::http::HttpResponse not_found;
  not_found.status = 404;
  not_found.headers.Set("content-type", "text/html; charset=utf-8");
  not_found.body = "<!DOCTYPE html><html><head><title>Page not found</title></head></html>";
  Fixture fixture = ArchiveOver({not_found});

  const auto games = fixture.archive->FetchMonth("alice", January());

  EXPECT_TRUE(absl::IsNotFound(games.status())) << games.status();
}

// A worker with no token cannot read any month of any player: the export
// answers anonymous callers 404 even for accounts that exist. Same status
// code as a handle that does not exist, and the two want opposite answers
// — "check the spelling" against "configure the server" — so which one it
// is comes from whether this worker has a token, the only thing here that
// can tell them apart.
TEST(LichessArchive, A404WithoutATokenIsUnauthenticatedRatherThanAMissingPlayer) {
  opal::http::HttpResponse not_found;
  not_found.status = 404;
  not_found.headers.Set("content-type", "text/html; charset=utf-8");
  not_found.body = "<!DOCTYPE html><html><head><title>Page not found</title></head></html>";
  Fixture fixture = ArchiveOver({not_found}, /*token=*/"");

  const auto games = fixture.archive->FetchMonth("alice", January());

  EXPECT_TRUE(absl::IsUnauthenticated(games.status())) << games.status();
}

// Only the 404 is about the token. A tokenless worker that gets a 429 has
// still been refused for the ordinary reason, and calling that a
// configuration problem would send the operator after the wrong thing.
TEST(LichessArchive, WithoutATokenARateLimitIsStillUnavailable) {
  opal::http::HttpResponse limited;
  limited.status = 429;
  limited.body = "{\"error\":\"Please only run 1 request(s) at a time\"}";
  Fixture fixture = ArchiveOver({limited, limited, limited}, /*token=*/"");

  const auto games = fixture.archive->FetchMonth("alice", January());

  EXPECT_TRUE(absl::IsUnavailable(games.status())) << games.status();
}

// Anything that is not the modeled 404 is a failure to read the month rather
// than an empty one, for the same reason.
TEST(LichessArchive, ARateLimitIsUnavailableNotAnEmptyMonth) {
  opal::http::HttpResponse limited;
  limited.status = 429;
  limited.body = "{\"error\":\"Please only run 1 request(s) at a time\"}";
  Fixture fixture = ArchiveOver({limited, limited, limited});

  const auto games = fixture.archive->FetchMonth("alice", January());

  EXPECT_TRUE(absl::IsUnavailable(games.status())) << games.status();
}

// ---- the one-at-a-time rule ----

// Lichess refuses concurrent exports with 429 "Please only run 1 request(s)
// at a time", and the cooldown outlasts the backoff. The worker runs
// ONE_D4_INDEX_SLOTS requests at once — four in the deployed compose —
// against one archive, so without the gate four LICHESS claims would race
// into that refusal and spend their attempts finding it out.
TEST(LichessArchive, RunsOneExportAtATime) {
  Fixture fixture = ArchiveOver({Pgn(""), Pgn(""), Pgn(""), Pgn("")});

  std::vector<std::thread> threads;
  threads.reserve(4);
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back(
        [&fixture] { EXPECT_TRUE(fixture.archive->FetchMonth("alice", January()).ok()); });
  }
  for (std::thread& thread : threads) thread.join();

  EXPECT_EQ(fixture.transport->peak_concurrency(), 1)
      << "concurrent exports are the 429 this gate exists to avoid";
}

// ---- splitting ----

TEST(SplitPgnGames, FindsNothingInAnEmptyBody) { EXPECT_TRUE(SplitPgnGames("").empty()); }

// "[Event " inside movetext or a tag value is text, not a boundary. Cutting
// there would split one game into two, and the second half would parse as a
// game with no tags.
TEST(SplitPgnGames, OnlyCutsAtTheStartOfALine) {
  const std::string pgn =
      "[Event \"Rated Blitz game\"]\n[Site \"https://lichess.org/a\"]\n\n1. e4 {[Event \"not a "
      "game\"]} e5 1-0\n";

  EXPECT_EQ(SplitPgnGames(pgn).size(), 1u);
}

}  // namespace
}  // namespace one_d4_worker
