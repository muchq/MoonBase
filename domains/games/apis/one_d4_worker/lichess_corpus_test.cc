// A real Lichess export, end to end: the bytes Lichess actually returned,
// through the archive that parses them and the extractor that reads the
// games out.
//
// testdata/lichess_alireza_corpus.pgn is seven rated bullet games from
// alireza2003's August 2026 archive, fetched from
// /api/games/user/{username} with no extra query parameters. That last
// clause is the point of keeping it: the hand-written fixtures in
// lichess_archive_test.cc were written from the API docs, and they carry
// [ECO] and [Opening] tags that a default export does not send. A fixture
// nobody fetched cannot tell you that.
//
// What the file is an oracle for, that a fixture is not:
//
//   - which tags a default export carries, and which it does not
//   - that real bullet games replay, so the motif extractor sees moves
//   - that titles arrive on the game, which is the whole premise of
//     reading them from PGN headers rather than a roster (#1527)

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/one_d4_worker/archive.h"
#include "domains/games/apis/one_d4_worker/lichess_archive.h"
#include "domains/games/libs/chess_cpp/parsed_game.h"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/one_d4_motifs/extract.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace one_d4_worker {
namespace {

using ::testing::Each;
using ::testing::IsEmpty;
using ::testing::Not;

constexpr char kCorpus[] = "domains/games/apis/one_d4_worker/testdata/lichess_alireza_corpus.pgn";

/// What the file turned out to hold. Counted rather than assumed: a
/// re-fetched bank that quietly changed shape would otherwise still pass.
constexpr int kGames = 7;

/// Moves across the bank, as counted rather than estimated. Frozen like
/// chess_cpp's corpus counts: a tokenizer that drops or invents a move moves
/// this number, and a bound picked to be comfortably true would not notice.
constexpr int kMoves = 233;

class OneResponse final : public opal::http::HttpClient {
 public:
  explicit OneResponse(std::string body) : body_(std::move(body)) {}

  opal::Outcome<opal::http::HttpResponse> Send(const opal::http::HttpRequest&) override {
    opal::http::HttpResponse response;
    response.status = 200;
    response.headers.Set("content-type", "application/x-chess-pgn");
    response.body = body_;
    return response;
  }

 private:
  std::string body_;
};

std::string ReadCorpus() {
  std::ifstream file(kCorpus);
  EXPECT_TRUE(file.is_open()) << "cannot open " << kCorpus;
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

std::vector<ArchivedGame> FetchCorpus() {
  opal::ClientConfig config = lichess::DefaultClientConfig();
  config.http_client = std::make_shared<OneResponse>(ReadCorpus());
  auto client = lichess::Client::Create(std::move(config));
  EXPECT_TRUE(client.ok()) << client.error().message();
  auto owned = std::make_unique<lichess::Client>(std::move(*client));
  LichessArchive archive(*owned);
  auto games = archive.FetchMonth("alireza2003", YearMonth{.year = 2026, .month = 8});
  EXPECT_TRUE(games.ok()) << games.status();
  return games.ok() ? *games : std::vector<ArchivedGame>{};
}

TEST(LichessCorpus, SplitsEveryGameInARealExport) {
  const std::vector<ArchivedGame> games = FetchCorpus();

  ASSERT_EQ(games.size(), static_cast<std::size_t>(kGames));
  for (const ArchivedGame& game : games) {
    EXPECT_THAT(game.url, Not(IsEmpty()));
    EXPECT_THAT(game.pgn, Not(IsEmpty()));
    EXPECT_THAT(game.white_username, Not(IsEmpty()));
    EXPECT_THAT(game.black_username, Not(IsEmpty()));
    EXPECT_GT(game.white_rating, 0);
    EXPECT_GT(game.black_rating, 0);
    EXPECT_GT(game.end_time, 0) << "UTCDate and UTCTime should have parsed";
    EXPECT_EQ(game.time_class, "bullet") << game.url;
  }
  EXPECT_EQ(games.front().url, "https://lichess.org/fL59y06E");
}

// The premise of #1527 slice 5, against data rather than a fixture: Lichess
// states titles on the game, so the indexer needs no roster for it.
TEST(LichessCorpus, EveryGameStatesBothPlayersTitles) {
  const std::vector<ArchivedGame> games = FetchCorpus();

  ASSERT_EQ(games.size(), static_cast<std::size_t>(kGames));
  for (const ArchivedGame& game : games) {
    EXPECT_THAT(game.white_title, Not(IsEmpty())) << game.url;
    EXPECT_THAT(game.black_title, Not(IsEmpty())) << game.url;
  }
  // alireza2003 is a GM and plays titled opposition here; the pair is one of
  // the two orderings rather than a fixed side.
  EXPECT_EQ(games.front().white_title, "IM");
  EXPECT_EQ(games.front().black_title, "GM");
}

// The finding this file exists for. A default export carries no [ECO] and no
// [Opening], so eco_url is empty (it is chess.com's slug) and opening_name is
// empty too — the hand-written fixtures claimed otherwise. Opening data needs
// opening=true on the request; until that is asked for, these stay blank and
// the run has nothing to fall back to.
TEST(LichessCorpus, ADefaultExportCarriesNoOpeningTags) {
  const std::vector<ArchivedGame> games = FetchCorpus();

  ASSERT_EQ(games.size(), static_cast<std::size_t>(kGames));
  for (const ArchivedGame& game : games) {
    EXPECT_THAT(game.eco_url, IsEmpty()) << "eco_url is chess.com's slug and has no analogue";
    EXPECT_THAT(game.opening_name, IsEmpty()) << game.url;
  }
}

// The reason a PGN is worth storing at all: every one of these replays, so
// the extractor sees real moves rather than an empty game.
TEST(LichessCorpus, EveryGameReplaysAndExtracts) {
  const std::vector<ArchivedGame> games = FetchCorpus();

  ASSERT_EQ(games.size(), static_cast<std::size_t>(kGames));
  int total_moves = 0;
  int games_with_motifs = 0;
  for (const ArchivedGame& game : games) {
    const auto parsed = chess_cpp::ParseGame(game.pgn);
    ASSERT_TRUE(parsed.ok()) << game.url << ": " << parsed.status();

    const auto features = one_d4::Extract(*parsed);
    ASSERT_TRUE(features.ok()) << game.url << ": " << features.status();
    EXPECT_GT(features->num_moves, 0) << game.url;
    total_moves += features->num_moves;
    if (!features->occurrences.empty()) ++games_with_motifs;
  }
  EXPECT_EQ(total_moves, kMoves);
  EXPECT_GT(games_with_motifs, 0) << "a whole bank of real games fired no motif at all";
}

}  // namespace
}  // namespace one_d4_worker
