#include "domains/games/apis/one_d4_worker/chess_com_archive.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "moonbase/chess_com/server.h"
#include "smithy/core/error.h"
#include "smithy/http/loopback.h"
#include "smithy/http/message.h"
#include "smithy/http/transport.h"

namespace one_d4_worker {
namespace {

using ::moonbase::chess_com::ArchiveNotFound;
using ::moonbase::chess_com::ChessComHandler;
using ::moonbase::chess_com::ChessComServer;
using ::moonbase::chess_com::FetchArchiveInput;
using ::moonbase::chess_com::FetchArchiveOutput;
using ::moonbase::chess_com::FetchPlayerInput;
using ::moonbase::chess_com::FetchPlayerOutput;
using ::moonbase::chess_com::FetchTitledInput;
using ::moonbase::chess_com::FetchTitledOutput;
using ::moonbase::chess_com::PlayedGame;
using ::moonbase::chess_com::PlayerNotFound;
using ::moonbase::chess_com::PlayerResult;
using ::moonbase::chess_com::TitleNotFound;

class ScriptedHandler final : public ChessComHandler {
 public:
  smithy::Outcome<FetchTitledOutput> FetchTitled(
      const FetchTitledInput& input, const smithy::server::RequestContext& /*ctx*/) override {
    const std::lock_guard<std::mutex> lock(mu_);
    titled_seen_ = input;
    if (title_not_found_) {
      smithy::Error error = smithy::Error::Modeled("TitleNotFound", "no such title");
      error.set_detail(TitleNotFound{.message = "no such title"});
      return error;
    }
    return titled_;
  }

  void set_titled(FetchTitledOutput titled) {
    const std::lock_guard<std::mutex> lock(mu_);
    titled_ = std::move(titled);
  }
  void set_title_not_found() {
    const std::lock_guard<std::mutex> lock(mu_);
    title_not_found_ = true;
  }
  FetchTitledInput titled_seen() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return titled_seen_;
  }

  smithy::Outcome<FetchPlayerOutput> FetchPlayer(
      const FetchPlayerInput& /*input*/, const smithy::server::RequestContext& /*ctx*/) override {
    const std::lock_guard<std::mutex> lock(mu_);
    if (player_not_found_) {
      smithy::Error error = smithy::Error::Modeled("PlayerNotFound", "player not found");
      error.set_detail(PlayerNotFound{.message = "player not found"});
      return error;
    }
    return player_;
  }

  void set_player(FetchPlayerOutput player) {
    const std::lock_guard<std::mutex> lock(mu_);
    player_ = std::move(player);
  }
  void set_player_not_found() {
    const std::lock_guard<std::mutex> lock(mu_);
    player_not_found_ = true;
  }

  smithy::Outcome<FetchArchiveOutput> FetchArchive(
      const FetchArchiveInput& input, const smithy::server::RequestContext& /*ctx*/) override {
    const std::lock_guard<std::mutex> lock(mu_);
    seen_ = input;
    if (not_found_) {
      smithy::Error error = smithy::Error::Modeled("ArchiveNotFound", "archive not found");
      error.set_detail(ArchiveNotFound{.message = "archive not found"});
      return error;
    }
    return output_;
  }

  void set_output(FetchArchiveOutput output) {
    const std::lock_guard<std::mutex> lock(mu_);
    output_ = std::move(output);
  }
  void set_not_found() {
    const std::lock_guard<std::mutex> lock(mu_);
    not_found_ = true;
  }
  FetchArchiveInput seen() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return seen_;
  }

 private:
  mutable std::mutex mu_;
  FetchArchiveInput seen_;
  FetchArchiveOutput output_;
  FetchPlayerOutput player_;
  FetchTitledInput titled_seen_;
  FetchTitledOutput titled_;
  bool title_not_found_ = false;
  bool not_found_ = false;
  bool player_not_found_ = false;
};

class ChessComArchiveTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = std::make_unique<ChessComServer>(handler_);
    loopback_ = std::make_shared<smithy::http::Loopback>();
    ASSERT_TRUE(loopback_->Start(server_->Handler()).ok());

    smithy::ClientConfig config = chess_com::DefaultClientConfig();
    config.http_client = loopback_;
    // One attempt: a test that waits out three backoffs on the error paths
    // is a test nobody runs.
    config.retry.max_attempts = 1;
    auto client = chess_com::Client::Create(std::move(config));
    ASSERT_TRUE(client.ok()) << client.error().message();
    client_ = std::make_unique<chess_com::Client>(std::move(*client));
    archive_ = std::make_unique<ChessComArchive>(*client_);
  }

  std::shared_ptr<ScriptedHandler> handler_ = std::make_shared<ScriptedHandler>();
  std::unique_ptr<ChessComServer> server_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
  std::unique_ptr<chess_com::Client> client_;
  std::unique_ptr<ChessComArchive> archive_;
};

TEST_F(ChessComArchiveTest, AsksForTheMonthItWasGiven) {
  ASSERT_TRUE(archive_->FetchMonth("HiKaRu", YearMonth{2026, 3}).ok());

  EXPECT_EQ(handler_->seen().username, "hikaru");
  EXPECT_EQ(handler_->seen().year, "2026");
  EXPECT_EQ(handler_->seen().month, "03");
}

TEST_F(ChessComArchiveTest, CarriesEveryFieldTheIndexerReads) {
  PlayedGame game;
  game.url = "https://www.chess.com/game/live/123";
  game.pgn = "[Event \"Live Chess\"]\n\n1. e4 e5";
  game.endTime = smithy::Timestamp::FromEpochSeconds(1'700'000'000);
  game.timeClass = "blitz";
  game.white = PlayerResult{.username = "Hikaru", .rating = 2800, .result = "win"};
  game.black = PlayerResult{.username = "Opponent", .rating = 2700, .result = "resigned"};
  game.eco = "https://www.chess.com/openings/Kings-Pawn-Opening";
  handler_->set_output(FetchArchiveOutput{.games = {game}});

  const auto games = archive_->FetchMonth("hikaru", YearMonth{2026, 3});

  ASSERT_TRUE(games.ok()) << games.status();
  ASSERT_EQ(games->size(), 1u);
  const ArchivedGame& got = games->front();
  EXPECT_EQ(got.url, "https://www.chess.com/game/live/123");
  EXPECT_EQ(got.pgn, "[Event \"Live Chess\"]\n\n1. e4 e5");
  EXPECT_EQ(got.time_class, "blitz");
  EXPECT_EQ(got.white_username, "Hikaru");
  EXPECT_EQ(got.black_username, "Opponent");
  EXPECT_EQ(got.white_rating, 2800);
  EXPECT_EQ(got.black_rating, 2700);
  EXPECT_EQ(got.white_result, "win");
  EXPECT_EQ(got.black_result, "resigned");
  EXPECT_EQ(got.eco_url, "https://www.chess.com/openings/Kings-Pawn-Opening");
  EXPECT_EQ(got.end_time, 1'700'000'000) << "seconds, as the queue and the column spell it";
}

TEST_F(ChessComArchiveTest, ReadsAGameThatSaysAlmostNothing) {
  // Every response member is optional on purpose: one incomplete game must
  // not cost the month. The run wants a row either way.
  handler_->set_output(FetchArchiveOutput{.games = {PlayedGame{}}});

  const auto games = archive_->FetchMonth("hikaru", YearMonth{2026, 3});

  ASSERT_TRUE(games.ok()) << games.status();
  ASSERT_EQ(games->size(), 1u);
  EXPECT_EQ(games->front().url, "");
  EXPECT_EQ(games->front().white_username, "");
  EXPECT_EQ(games->front().white_rating, 0);
  EXPECT_EQ(games->front().end_time, 0);
}

TEST_F(ChessComArchiveTest, AQuietMonthIsAnEmptyListAndNotAnError) {
  const auto games = archive_->FetchMonth("hikaru", YearMonth{2026, 3});

  ASSERT_TRUE(games.ok()) << games.status();
  EXPECT_TRUE(games->empty());
}

TEST_F(ChessComArchiveTest, AMissingArchiveIsNotFoundAndNothingElseIs) {
  // The run stops on either, but only NotFound says "there is no such
  // archive" rather than "the archive could not be read".
  handler_->set_not_found();

  const auto games = archive_->FetchMonth("hikaru", YearMonth{2026, 3});

  EXPECT_EQ(games.status().code(), absl::StatusCode::kNotFound);
  EXPECT_THAT(std::string(games.status().message()), ::testing::HasSubstr("hikaru"));
  EXPECT_THAT(std::string(games.status().message()), ::testing::HasSubstr("2026-03"));
}

TEST_F(ChessComArchiveTest, ReadsATitledRoster) {
  handler_->set_titled(FetchTitledOutput{.players = {"hikaru", "magnuscarlsen"}});

  const auto roster = archive_->FetchTitled("GM");

  ASSERT_TRUE(roster.ok()) << roster.status();
  EXPECT_THAT(*roster, ::testing::ElementsAre("hikaru", "magnuscarlsen"));
  EXPECT_EQ(handler_->titled_seen().title, "GM");
}

TEST_F(ChessComArchiveTest, ATitleNobodyHoldsIsAnEmptyRosterAndNotAFailure) {
  handler_->set_titled(FetchTitledOutput{});

  const auto roster = archive_->FetchTitled("WNM");

  ASSERT_TRUE(roster.ok()) << roster.status();
  EXPECT_THAT(*roster, ::testing::IsEmpty());
}

TEST_F(ChessComArchiveTest, ATitleWithNoRosterIsEmptyRatherThanAFailedRefresh) {
  // The caller stops at the first failed title and keeps the rosters it
  // already had, so flattening this 404 into a transport failure would
  // take the whole feature down over one retired abbreviation.
  handler_->set_title_not_found();

  const auto roster = archive_->FetchTitled("M");

  ASSERT_TRUE(roster.ok()) << roster.status();
  EXPECT_THAT(*roster, ::testing::IsEmpty());
}

TEST(ChessComArchiveTransportTest, ATransportFailureIsNotAMissingArchive) {
  // The failure mode this exists for: a 5xx or a dead connection read as
  // "no such archive" would complete a request having indexed nothing.
  class DeadTransport final : public smithy::http::HttpClient {
   public:
    smithy::Outcome<smithy::http::HttpResponse> Send(
        const smithy::http::HttpRequest& /*request*/) override {
      return smithy::Error::Unknown("connection refused");
    }
  };

  smithy::ClientConfig config = chess_com::DefaultClientConfig();
  config.http_client = std::make_shared<DeadTransport>();
  config.retry.max_attempts = 1;
  auto client = chess_com::Client::Create(std::move(config));
  ASSERT_TRUE(client.ok()) << client.error().message();
  ChessComArchive archive(*client);

  const auto games = archive.FetchMonth("hikaru", YearMonth{2026, 3});

  ASSERT_FALSE(games.ok());
  EXPECT_NE(games.status().code(), absl::StatusCode::kNotFound);

  // A roster that never reached chess.com is not an empty roster either —
  // taking that answer would leave every GM unlabelled.
  const auto roster = archive.FetchTitled("GM");
  EXPECT_FALSE(roster.ok());
}

}  // namespace
}  // namespace one_d4_worker
