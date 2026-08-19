#include "domains/games/libs/chess_com_cpp/client.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "moonbase/chess_com/server.h"
#include "smithy/core/error.h"
#include "smithy/http/loopback.h"
#include "smithy/http/message.h"
#include "smithy/http/transport.h"

namespace {

using chess_com::Client;
using moonbase::chess_com::ArchiveNotFound;
using moonbase::chess_com::ChessComHandler;
using moonbase::chess_com::ChessComServer;
using moonbase::chess_com::FetchArchiveInput;
using moonbase::chess_com::FetchArchiveOutput;
using moonbase::chess_com::FetchPlayerInput;
using moonbase::chess_com::FetchPlayerOutput;
using moonbase::chess_com::FetchTitledInput;
using moonbase::chess_com::FetchTitledOutput;
using moonbase::chess_com::PlayedGame;
using moonbase::chess_com::PlayerNotFound;
using moonbase::chess_com::PlayerResult;
using moonbase::chess_com::TitleNotFound;

class ScriptedHttpClient final : public smithy::http::HttpClient {
 public:
  explicit ScriptedHttpClient(std::vector<smithy::http::HttpResponse> responses)
      : responses_(std::move(responses)) {}

  smithy::Outcome<smithy::http::HttpResponse> Send(
      const smithy::http::HttpRequest& request) override {
    requests_.push_back(request);
    if (next_response_ == responses_.size()) {
      return smithy::Error::Unknown("no scripted response");
    }
    return responses_[next_response_++];
  }

  const std::vector<smithy::http::HttpRequest>& requests() const { return requests_; }

 private:
  std::vector<smithy::http::HttpResponse> responses_;
  std::vector<smithy::http::HttpRequest> requests_;
  std::size_t next_response_ = 0;
};

class RecordingHandler final : public ChessComHandler {
 public:
  smithy::Outcome<FetchPlayerOutput> FetchPlayer(
      const FetchPlayerInput& input, const smithy::server::RequestContext& /*context*/) override {
    const std::lock_guard<std::mutex> lock(mu_);
    player_input_ = input;
    if (player_not_found_) {
      smithy::Error error = smithy::Error::Modeled("PlayerNotFound", "player not found");
      error.set_detail(PlayerNotFound{.message = "player not found"});
      return error;
    }
    return FetchPlayerOutput{.title = "GM"};
  }

  smithy::Outcome<FetchTitledOutput> FetchTitled(
      const FetchTitledInput& input, const smithy::server::RequestContext& /*context*/) override {
    const std::lock_guard<std::mutex> lock(mu_);
    titled_input_ = input;
    ++titled_calls_;
    if (title_not_found_) {
      smithy::Error error = smithy::Error::Modeled("TitleNotFound", "title not found");
      error.set_detail(TitleNotFound{.message = "title not found"});
      return error;
    }
    return titled_output_;
  }

  FetchTitledInput titled_input() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return titled_input_;
  }

  int titled_calls() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return titled_calls_;
  }

  void set_titled_output(FetchTitledOutput output) {
    const std::lock_guard<std::mutex> lock(mu_);
    titled_output_ = std::move(output);
  }

  void set_title_not_found() {
    const std::lock_guard<std::mutex> lock(mu_);
    title_not_found_ = true;
  }

  smithy::Outcome<FetchArchiveOutput> FetchArchive(
      const FetchArchiveInput& input, const smithy::server::RequestContext& /*context*/) override {
    const std::lock_guard<std::mutex> lock(mu_);
    archive_input_ = input;
    ++archive_calls_;
    if (archive_not_found_) {
      smithy::Error error = smithy::Error::Modeled("ArchiveNotFound", "archive not found");
      error.set_detail(ArchiveNotFound{.message = "archive not found"});
      return error;
    }
    return archive_output_;
  }

  FetchPlayerInput player_input() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return player_input_;
  }

  FetchArchiveInput archive_input() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return archive_input_;
  }

  int archive_calls() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return archive_calls_;
  }

  void set_archive_output(FetchArchiveOutput output) {
    const std::lock_guard<std::mutex> lock(mu_);
    archive_output_ = std::move(output);
  }

  void set_player_not_found() {
    const std::lock_guard<std::mutex> lock(mu_);
    player_not_found_ = true;
  }

  void set_archive_not_found() {
    const std::lock_guard<std::mutex> lock(mu_);
    archive_not_found_ = true;
  }

 private:
  mutable std::mutex mu_;
  FetchPlayerInput player_input_;
  FetchArchiveInput archive_input_;
  FetchArchiveOutput archive_output_;
  int archive_calls_ = 0;
  bool player_not_found_ = false;
  bool archive_not_found_ = false;
  FetchTitledInput titled_input_;
  FetchTitledOutput titled_output_;
  int titled_calls_ = 0;
  bool title_not_found_ = false;
};

class ClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = std::make_unique<ChessComServer>(handler_);
    loopback_ = std::make_shared<smithy::http::Loopback>();
    ASSERT_TRUE(loopback_->Start(server_->Handler()).ok());

    smithy::ClientConfig config = chess_com::DefaultClientConfig();
    config.http_client = loopback_;
    auto client = Client::Create(std::move(config));
    ASSERT_TRUE(client.ok()) << client.error().message();
    client_ = std::make_unique<Client>(std::move(*client));
  }

  std::shared_ptr<RecordingHandler> handler_ = std::make_shared<RecordingHandler>();
  std::unique_ptr<ChessComServer> server_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
  std::unique_ptr<Client> client_;
};

TEST(DefaultClientConfigTest, UsesChessComProductionDefaults) {
  const smithy::ClientConfig config = chess_com::DefaultClientConfig();

  EXPECT_EQ(config.endpoint, "https://api.chess.com");
  EXPECT_EQ(config.user_agent, "MoonBase indexer/1.0");
  EXPECT_EQ(config.request_timeout_ms, 60'000);
  EXPECT_EQ(config.retry.max_attempts, 3);
  EXPECT_EQ(config.retry.initial_backoff, std::chrono::seconds(1));
  EXPECT_EQ(config.retry.max_backoff, std::chrono::seconds(20));
}

TEST_F(ClientTest, FetchPlayerLowercasesUsername) {
  const auto player = client_->FetchPlayer("HiKaRu");

  ASSERT_TRUE(player.ok()) << player.error().message();
  EXPECT_EQ(player->title, "GM");
  EXPECT_EQ(handler_->player_input().username, "hikaru");
}

TEST_F(ClientTest, FetchArchiveFormatsLabelsAndReturnsIndexerFields) {
  PlayedGame game;
  game.url = "https://www.chess.com/game/live/123";
  game.pgn = "[Event \"Live Chess\"]\n\n1. e4 e5";
  game.endTime = smithy::Timestamp::FromEpochSeconds(1'700'000'000);
  game.timeClass = "blitz";
  game.white = PlayerResult{.username = "Hikaru", .rating = 2800, .result = "win"};
  game.black = PlayerResult{.username = "Opponent", .rating = 2700, .result = "resigned"};
  game.eco = "https://www.chess.com/openings/Kings-Pawn-Opening";
  handler_->set_archive_output(FetchArchiveOutput{.games = {game}});

  const auto archive = client_->FetchArchive("HiKaRu", 2026, 3);

  ASSERT_TRUE(archive.ok()) << archive.error().message();
  ASSERT_EQ(archive->games.size(), 1u);
  EXPECT_EQ(archive->games[0], game);
  const FetchArchiveInput input = handler_->archive_input();
  EXPECT_EQ(input.username, "hikaru");
  EXPECT_EQ(input.year, "2026");
  EXPECT_EQ(input.month, "03");
}

TEST_F(ClientTest, FetchArchiveDoesNotPadDoubleDigitMonths) {
  const auto archive = client_->FetchArchive("hikaru", 2026, 10);

  ASSERT_TRUE(archive.ok()) << archive.error().message();
  EXPECT_EQ(handler_->archive_input().month, "10");
}

TEST_F(ClientTest, EmptyArchiveIsAValidSuccess) {
  const auto archive = client_->FetchArchive("hikaru", 2026, 3);

  ASSERT_TRUE(archive.ok()) << archive.error().message();
  EXPECT_TRUE(archive->games.empty());
}

TEST_F(ClientTest, InvalidMonthIsRejectedBeforeSending) {
  const auto zero = client_->FetchArchive("hikaru", 2026, 0);
  const auto thirteen = client_->FetchArchive("hikaru", 2026, 13);

  EXPECT_FALSE(zero.ok());
  EXPECT_FALSE(thirteen.ok());
  EXPECT_EQ(handler_->archive_calls(), 0);
}

TEST_F(ClientTest, InvalidYearIsRejectedBeforeSending) {
  const auto too_short = client_->FetchArchive("hikaru", 999, 1);
  const auto too_long = client_->FetchArchive("hikaru", 10'000, 1);

  EXPECT_FALSE(too_short.ok());
  EXPECT_FALSE(too_long.ok());
  EXPECT_EQ(handler_->archive_calls(), 0);
}

TEST_F(ClientTest, PlayerAndArchiveNotFoundRemainDistinct) {
  handler_->set_player_not_found();
  const auto player = client_->FetchPlayer("missing");
  ASSERT_FALSE(player.ok());
  EXPECT_EQ(player.error().code(), "PlayerNotFound");
  EXPECT_NE(player.error().detail<PlayerNotFound>(), nullptr);

  handler_->set_archive_not_found();
  const auto archive = client_->FetchArchive("missing", 2026, 3);
  ASSERT_FALSE(archive.ok());
  EXPECT_EQ(archive.error().code(), "ArchiveNotFound");
  EXPECT_NE(archive.error().detail<ArchiveNotFound>(), nullptr);
}

TEST_F(ClientTest, FetchTitledReadsARoster) {
  handler_->set_titled_output(FetchTitledOutput{.players = {"hikaru", "magnuscarlsen"}});

  const auto titled = client_->FetchTitled("GM");

  ASSERT_TRUE(titled.ok()) << titled.error().message();
  ASSERT_EQ(titled->players.size(), 2u);
  EXPECT_EQ(titled->players[0], "hikaru");
  EXPECT_EQ(titled->players[1], "magnuscarlsen");
  EXPECT_EQ(handler_->titled_input().title, "GM");
}

TEST_F(ClientTest, FetchTitledUppercasesTheTitle) {
  // The path spells titles in upper case; chess.com 404s on "gm".
  ASSERT_TRUE(client_->FetchTitled("wim").ok());

  EXPECT_EQ(handler_->titled_input().title, "WIM");
}

TEST_F(ClientTest, AnEmptyTitleNeverReachesTheWire) {
  // The generated client refuses an empty path label, which is why this
  // one adds no check of its own — an empty title would otherwise be a
  // request for /pub/titled/, and that is a different resource.
  const auto empty = client_->FetchTitled("");

  EXPECT_FALSE(empty.ok());
  EXPECT_EQ(handler_->titled_calls(), 0);
}

TEST_F(ClientTest, AnUnknownTitleStaysDistinctFromTheOtherNotFounds) {
  handler_->set_title_not_found();

  const auto titled = client_->FetchTitled("XX");

  ASSERT_FALSE(titled.ok());
  EXPECT_EQ(titled.error().code(), "TitleNotFound");
  EXPECT_NE(titled.error().detail<TitleNotFound>(), nullptr);
}

TEST(ClientContractTest, UnknownResponseMembersDoNotBreakArchiveDecoding) {
  smithy::http::HttpResponse response;
  response.status = 200;
  response.body = R"({
    "games": [{
      "url": "https://www.chess.com/game/live/123",
      "pgn": "1. e4 e5",
      "end_time": 1700000000,
      "time_class": "blitz",
      "white": {"username": "Hikaru", "rating": 2800, "result": "win", "uuid": "ignored"},
      "black": {"username": "Opponent", "rating": 2700, "result": "resigned"},
      "rated": true
    }],
    "future_field": "ignored"
  })";
  auto transport =
      std::make_shared<ScriptedHttpClient>(std::vector<smithy::http::HttpResponse>{response});
  smithy::ClientConfig config = chess_com::DefaultClientConfig();
  config.http_client = transport;
  auto client = Client::Create(std::move(config));
  ASSERT_TRUE(client.ok()) << client.error().message();

  const auto archive = client->FetchArchive("hikaru", 2026, 3);

  ASSERT_TRUE(archive.ok()) << archive.error().message();
  ASSERT_EQ(archive->games.size(), 1u);
  ASSERT_TRUE(archive->games[0].white.has_value());
  ASSERT_TRUE(archive->games[0].white->username.has_value());
  EXPECT_EQ(*archive->games[0].white->username, "Hikaru");
  ASSERT_TRUE(archive->games[0].endTime.has_value());
  EXPECT_EQ(archive->games[0].endTime->epoch_milliseconds(), 1'700'000'000'000);
}

TEST(ClientContractTest, MissingPlayerSideIsAccepted) {
  smithy::http::HttpResponse response;
  response.status = 200;
  response.body = R"({
    "games": [{
      "url": "https://www.chess.com/game/live/123",
      "pgn": "1. e4 e5",
      "end_time": 1700000000,
      "time_class": "blitz",
      "white": {"username": "Hikaru", "rating": 2800, "result": "win"}
    }]
  })";
  auto transport =
      std::make_shared<ScriptedHttpClient>(std::vector<smithy::http::HttpResponse>{response});
  smithy::ClientConfig config = chess_com::DefaultClientConfig();
  config.http_client = transport;
  auto client = Client::Create(std::move(config));
  ASSERT_TRUE(client.ok()) << client.error().message();

  const auto archive = client->FetchArchive("hikaru", 2026, 3);

  ASSERT_TRUE(archive.ok()) << archive.error().message();
  ASSERT_EQ(archive->games.size(), 1u);
  EXPECT_TRUE(archive->games[0].white.has_value());
  EXPECT_FALSE(archive->games[0].black.has_value());
}

TEST(ClientContractTest, MissingGameAndPlayerFieldsDoNotDiscardTheArchive) {
  smithy::http::HttpResponse response;
  response.status = 200;
  response.body = R"({
    "games": [
      {"white": {}},
      {
        "url": "https://www.chess.com/game/live/123",
        "pgn": "1. e4 e5",
        "end_time": 1700000000,
        "time_class": "blitz",
        "white": {"username": "Hikaru", "rating": 2800, "result": "win"},
        "black": {"username": "Opponent", "rating": 2700, "result": "resigned"}
      }
    ]
  })";
  auto transport =
      std::make_shared<ScriptedHttpClient>(std::vector<smithy::http::HttpResponse>{response});
  smithy::ClientConfig config = chess_com::DefaultClientConfig();
  config.http_client = transport;
  auto client = Client::Create(std::move(config));
  ASSERT_TRUE(client.ok()) << client.error().message();

  const auto archive = client->FetchArchive("hikaru", 2026, 3);

  ASSERT_TRUE(archive.ok()) << archive.error().message();
  ASSERT_EQ(archive->games.size(), 2u);
  EXPECT_FALSE(archive->games[0].url.has_value());
  EXPECT_FALSE(archive->games[0].pgn.has_value());
  EXPECT_FALSE(archive->games[0].endTime.has_value());
  EXPECT_FALSE(archive->games[0].timeClass.has_value());
  ASSERT_TRUE(archive->games[0].white.has_value());
  EXPECT_FALSE(archive->games[0].white->username.has_value());
  EXPECT_FALSE(archive->games[0].white->rating.has_value());
  EXPECT_FALSE(archive->games[0].white->result.has_value());
  ASSERT_TRUE(archive->games[1].url.has_value());
  EXPECT_EQ(*archive->games[1].url, "https://www.chess.com/game/live/123");
}

TEST(ClientContractTest, RateLimitIsRetriedWithConfiguredPolicy) {
  smithy::http::HttpResponse rate_limited;
  rate_limited.status = 429;
  rate_limited.body = R"({"code":0,"message":"try again"})";
  smithy::http::HttpResponse success;
  success.status = 200;
  success.body = R"({"games":[]})";
  auto transport = std::make_shared<ScriptedHttpClient>(
      std::vector<smithy::http::HttpResponse>{rate_limited, success});
  smithy::ClientConfig config = chess_com::DefaultClientConfig();
  config.http_client = transport;
  std::vector<std::chrono::milliseconds> delays;
  config.retry.sleep = [&](std::chrono::milliseconds delay) { delays.push_back(delay); };
  config.retry.jitter = [] { return 1.0; };
  auto client = Client::Create(std::move(config));
  ASSERT_TRUE(client.ok()) << client.error().message();

  const auto archive = client->FetchArchive("hikaru", 2026, 3);

  ASSERT_TRUE(archive.ok()) << archive.error().message();
  EXPECT_EQ(transport->requests().size(), 2u);
  EXPECT_EQ(delays, (std::vector<std::chrono::milliseconds>{std::chrono::seconds(1)}));
  EXPECT_EQ(transport->requests()[0].method, "GET");
  EXPECT_EQ(transport->requests()[0].target, "/pub/player/hikaru/games/2026/03");
  EXPECT_EQ(transport->requests()[0].headers.Get("user-agent").value_or(""),
            "MoonBase indexer/1.0");
}

TEST(ClientContractTest, PersistentRateLimitStopsAfterThreeAttempts) {
  smithy::http::HttpResponse rate_limited;
  rate_limited.status = 429;
  rate_limited.body = R"({"code":0,"message":"try again"})";
  auto transport = std::make_shared<ScriptedHttpClient>(
      std::vector<smithy::http::HttpResponse>{rate_limited, rate_limited, rate_limited});
  smithy::ClientConfig config = chess_com::DefaultClientConfig();
  config.http_client = transport;
  std::vector<std::chrono::milliseconds> delays;
  config.retry.sleep = [&](std::chrono::milliseconds delay) { delays.push_back(delay); };
  config.retry.jitter = [] { return 1.0; };
  auto client = Client::Create(std::move(config));
  ASSERT_TRUE(client.ok()) << client.error().message();

  const auto archive = client->FetchArchive("hikaru", 2026, 3);

  EXPECT_FALSE(archive.ok());
  EXPECT_EQ(transport->requests().size(), 3u);
  EXPECT_EQ(delays, (std::vector<std::chrono::milliseconds>{std::chrono::seconds(1),
                                                            std::chrono::seconds(2)}));
}

TEST(ClientContractTest, Bare404UsesTheOperationSpecificError) {
  smithy::http::HttpResponse not_found;
  not_found.status = 404;
  not_found.body = R"({"code":0,"message":"not found"})";
  auto transport = std::make_shared<ScriptedHttpClient>(
      std::vector<smithy::http::HttpResponse>{not_found, not_found});
  smithy::ClientConfig config = chess_com::DefaultClientConfig();
  config.http_client = transport;
  auto client = Client::Create(std::move(config));
  ASSERT_TRUE(client.ok()) << client.error().message();

  const auto player = client->FetchPlayer("missing");
  ASSERT_FALSE(player.ok());
  EXPECT_NE(player.error().detail<PlayerNotFound>(), nullptr);
  ASSERT_EQ(transport->requests().size(), 1u);
  EXPECT_EQ(transport->requests()[0].target, "/pub/player/missing");

  const auto archive = client->FetchArchive("missing", 2026, 3);
  ASSERT_FALSE(archive.ok());
  EXPECT_NE(archive.error().detail<ArchiveNotFound>(), nullptr);
  ASSERT_EQ(transport->requests().size(), 2u);
  EXPECT_EQ(transport->requests()[1].target, "/pub/player/missing/games/2026/03");
}

}  // namespace
