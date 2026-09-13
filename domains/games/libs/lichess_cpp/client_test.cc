#include "domains/games/libs/lichess_cpp/client.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "moonbase/lichess/client.h"
#include "opal/core/error.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace {

using lichess::Client;
using moonbase::lichess::ExportGamesErrors;

/// Answers from a script and keeps what it was asked, so a test can assert on
/// the request rather than only on what came back.
class ScriptedHttpClient final : public opal::http::HttpClient {
 public:
  explicit ScriptedHttpClient(std::vector<opal::http::HttpResponse> responses)
      : responses_(std::move(responses)) {}

  opal::Outcome<opal::http::HttpResponse> Send(const opal::http::HttpRequest& request) override {
    requests_.push_back(request);
    if (next_response_ == responses_.size()) {
      return opal::Error::Unknown("no scripted response");
    }
    return responses_[next_response_++];
  }

  const std::vector<opal::http::HttpRequest>& requests() const { return requests_; }

 private:
  std::vector<opal::http::HttpResponse> responses_;
  std::vector<opal::http::HttpRequest> requests_;
  std::size_t next_response_ = 0;
};

constexpr char kTwoGames[] =
    "[Event \"Rated blitz game\"]\n[Site \"https://lichess.org/abcd1234\"]\n\n1. e4 e5 1-0\n\n"
    "[Event \"Rated bullet game\"]\n[Site \"https://lichess.org/efgh5678\"]\n\n1. d4 d5 0-1\n";

opal::http::HttpResponse PgnResponse(std::string body) {
  opal::http::HttpResponse response;
  response.status = 200;
  response.headers.Set("content-type", "application/x-chess-pgn");
  response.body = std::move(body);
  return response;
}

/// Builds a client over a script, handing back the transport so the test can
/// read the request that was made.
std::pair<Client, std::shared_ptr<ScriptedHttpClient>> ClientOver(
    std::vector<opal::http::HttpResponse> responses, std::string_view token = "") {
  auto transport = std::make_shared<ScriptedHttpClient>(std::move(responses));
  opal::ClientConfig config = lichess::WithBearerToken(lichess::DefaultClientConfig(), token);
  config.http_client = transport;
  auto client = Client::Create(std::move(config));
  EXPECT_TRUE(client.ok()) << client.error().message();
  return {std::move(*client), transport};
}

// ---- the budget the config encodes ----

// The timeout is not a round number picked by habit: Lichess streams the
// export at about 20 games/second anonymously, and opal applies this per
// attempt, so the timeout *is* the largest month this client can read. A
// retry restarts the response rather than resuming it, so a month that
// cannot finish in one attempt cannot finish at all.
//
// Pinned as arithmetic rather than as a constant so the reason survives:
// lower it and the test says which months stop being indexable.
TEST(LichessClientConfig, TheTimeoutCoversAMonthAnActiveBulletPlayerCouldPlay) {
  constexpr int kGamesPerSecondAnonymous = 20;
  constexpr int kBusyMonthGames = 10'000;
  constexpr int kNeededMs = (kBusyMonthGames / kGamesPerSecondAnonymous) * 1000;

  EXPECT_GE(lichess::DefaultClientConfig().request_timeout_ms, kNeededMs)
      << "a month of " << kBusyMonthGames << " games streams for " << kNeededMs / 1000
      << "s and would be cut off, and the retry would restart it rather than resume";
}

// Lichess's 429 cooldown for concurrent requests was observed outlasting a
// 75-second backoff. A ceiling below that retries into the same refusal and
// spends the attempts discovering it.
TEST(LichessClientConfig, TheBackoffCeilingOutlastsTheObservedCooldown) {
  EXPECT_GE(lichess::DefaultClientConfig().retry.max_backoff, std::chrono::seconds(75));
}

// PGN is the format the whole slice turns on: it carries the ECO, Opening and
// *Title headers the indexer already parses. Nothing in opal sets Accept, so
// if this is not sent the server picks, and its pick is not a contract.
TEST(LichessClient, AsksForPgn) {
  auto [client, transport] = ClientOver({PgnResponse(kTwoGames)});

  ASSERT_TRUE(client.ExportGames("hikaru", 100, 200).ok());

  ASSERT_EQ(transport->requests().size(), 1u);
  EXPECT_EQ(transport->requests()[0].headers.Get("accept"), "application/x-chess-pgn");
}

// Milliseconds, and half-open. chess.com's end_time is seconds, so whoever
// maps a month onto a range is converting; sending the wrong unit asks for a
// window about 24 days wide starting in 1970.
TEST(LichessClient, SendsTheRangeAsQueryParameters) {
  auto [client, transport] = ClientOver({PgnResponse(kTwoGames)});

  ASSERT_TRUE(client.ExportGames("hikaru", 1767225600000, 1769904000000).ok());

  const std::string& target = transport->requests()[0].target;
  EXPECT_NE(target.find("since=1767225600000"), std::string::npos) << target;
  EXPECT_NE(target.find("until=1769904000000"), std::string::npos) << target;
  EXPECT_NE(target.find("hikaru"), std::string::npos) << target;
}

// The export answers anonymous callers 404 even for accounts that exist, so a
// configured token is the difference between working and not.
TEST(LichessClient, CarriesTheBearerTokenWhenThereIsOne) {
  auto [client, transport] = ClientOver({PgnResponse(kTwoGames)}, "lip_secret");

  ASSERT_TRUE(client.ExportGames("hikaru", 100, 200).ok());

  EXPECT_EQ(transport->requests()[0].headers.Get("authorization"), "Bearer lip_secret");
}

// No token means no header, not an empty one. An empty credential is a
// different request from no credential, and only the second is what we mean.
TEST(LichessClient, SendsNoAuthorizationHeaderWithoutAToken) {
  auto [client, transport] = ClientOver({PgnResponse(kTwoGames)});

  ASSERT_TRUE(client.ExportGames("hikaru", 100, 200).ok());

  EXPECT_FALSE(transport->requests()[0].headers.Get("authorization").has_value());
}

// The payload is bytes, not a JSON document: it must arrive exactly as sent,
// because a PGN parser is the next thing to read it.
TEST(LichessClient, ReturnsThePgnBytesVerbatim) {
  auto [client, transport] = ClientOver({PgnResponse(kTwoGames)});

  const auto result = client.ExportGames("hikaru", 100, 200);

  ASSERT_TRUE(result.ok()) << result.error().message();
  EXPECT_EQ(result->games.ToString(), kTwoGames);
}

// A player with nothing in the window is an empty body, and that is not an
// error — it is the same shape chess.com's quiet month has. Failing here
// would fail a run over a month the player simply did not play.
TEST(LichessClient, AnEmptyWindowIsNotAnError) {
  auto [client, transport] = ClientOver({PgnResponse("")});

  const auto result = client.ExportGames("hikaru", 100, 200);

  ASSERT_TRUE(result.ok()) << result.error().message();
  EXPECT_TRUE(result->games.empty());
}

// With Accept: application/x-chess-pgn the 404 body is a 13 KB HTML page, so
// the error has to be recognisable from its status alone. A client that tried
// to read a modeled body out of that would report a deserialization failure
// and lose the one fact that matters.
TEST(LichessClient, A404IsTheModeledNotFoundDespiteAnHtmlBody) {
  opal::http::HttpResponse not_found;
  not_found.status = 404;
  not_found.headers.Set("content-type", "text/html; charset=utf-8");
  not_found.body = "<!DOCTYPE html><html><head><title>Page not found</title></head></html>";
  auto [client, transport] = ClientOver({not_found});

  const auto result = client.ExportGames("hikaru", 100, 200);

  ASSERT_FALSE(result.ok());
  EXPECT_TRUE(ExportGamesErrors::FromError(result.error()).is_games_not_found())
      << result.error().message();
}

}  // namespace
