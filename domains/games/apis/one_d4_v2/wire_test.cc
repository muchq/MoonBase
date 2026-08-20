#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "domains/games/apis/one_d4_v2/smithy_handler.h"
#include "moonbase/one_d4/client.h"
#include "moonbase/one_d4/server.h"
#include "smithy/http/loopback.h"
#include "smithy/http/transport.h"

// The bytes on the wire, through the generated server. The route, the field
// names, and the error shape are the v2 contract; the MCP tool and any v1
// caller moving over parse exactly this.

namespace one_d4_v2 {
namespace {

using ::testing::HasSubstr;

constexpr char kScholarsMateJson[] =
    R"({"pgn": "[Event \"Live Chess\"]\n[White \"alice\"]\n[Black \"bob\"]\n\n1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n"})";

class WireTest : public testing::Test {
 protected:
  void SetUp() override {
    server_ =
        std::make_unique<moonbase::one_d4::OneD4V2Server>(std::make_shared<SmithyAnalyzeHandler>());
    loopback_ = std::make_shared<smithy::http::Loopback>();
    const auto started = loopback_->Start(server_->Handler());
    ASSERT_TRUE(started.ok()) << started.error().message();
  }

  smithy::http::HttpResponse Post(const std::string& body) {
    smithy::http::HttpRequest request;
    request.method = "POST";
    request.target = "/1d4/v2/analyze";
    request.headers.Set("content-type", "application/json");
    request.body = body;
    auto response = loopback_->Send(std::move(request));
    EXPECT_TRUE(response.ok());
    return response.ok() ? *response : smithy::http::HttpResponse{};
  }

  std::unique_ptr<moonbase::one_d4::OneD4V2Server> server_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
};

TEST_F(WireTest, AnalyzesAGameAtTheV2Route) {
  const auto response = Post(kScholarsMateJson);
  EXPECT_EQ(response.status, 200);
  EXPECT_THAT(response.body, HasSubstr("\"numMoves\":4"));
  EXPECT_THAT(response.body, HasSubstr("\"motifs\":"));
  EXPECT_THAT(response.body, HasSubstr("\"checkmate\""));
  EXPECT_THAT(response.body, HasSubstr("\"occurrences\":"));
  EXPECT_THAT(response.body, HasSubstr("\"moveNumber\""));
  EXPECT_THAT(response.body, HasSubstr("\"isMate\":true"));
}

TEST_F(WireTest, ABadPgnIsA400WithTheModeledError) {
  const auto response = Post(R"({"pgn": "this is not a pgn"})");
  EXPECT_EQ(response.status, 400);
  EXPECT_THAT(response.body, HasSubstr("did not parse"));

  // The typed half, read the way a caller reads it: the generated client
  // surfaces the modeled code and detail.
  smithy::ClientConfig config;
  config.http_client = loopback_;
  auto client = moonbase::one_d4::OneD4V2Client::Create(std::move(config));
  ASSERT_TRUE(client.ok());
  moonbase::one_d4::AnalyzeInput input;
  input.pgn = "this is not a pgn";
  auto denied = client->Analyze(input);
  ASSERT_FALSE(denied.ok());
  EXPECT_EQ(denied.error().code(), "InvalidPgnError");
  ASSERT_NE(denied.error().detail<moonbase::one_d4::InvalidPgnError>(), nullptr);
  EXPECT_THAT(denied.error().detail<moonbase::one_d4::InvalidPgnError>()->message,
              HasSubstr("did not parse"));
}

TEST_F(WireTest, AMissingPgnIsA400) { EXPECT_EQ(Post(R"({"pgn": ""})").status, 400); }

// The generated client, end to end — what the day-two C++ caller uses, and
// a second reader of the same wire the substring assertions pin.
TEST_F(WireTest, TheGeneratedClientRoundTrips) {
  smithy::ClientConfig config;
  config.http_client = loopback_;
  auto client = moonbase::one_d4::OneD4V2Client::Create(std::move(config));
  ASSERT_TRUE(client.ok()) << client.error().message();

  moonbase::one_d4::AnalyzeInput input;
  input.pgn =
      "[Event \"Live Chess\"]\n[White \"alice\"]\n[Black \"bob\"]\n\n"
      "1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n";
  auto output = client->Analyze(input);
  ASSERT_TRUE(output.ok()) << output.error().message();
  EXPECT_EQ(output->numMoves, 4);
  EXPECT_FALSE(output->motifs.empty());
  ASSERT_TRUE(output->occurrences.count("checkmate"));
  EXPECT_TRUE(output->occurrences.at("checkmate").front().isMate);
}

// motifs is exactly the key set of occurrences — carried separately for the
// caller that only wants "what happened", and wrong the moment they drift.
TEST_F(WireTest, MotifsIsExactlyTheOccurrenceKeySet) {
  smithy::ClientConfig config;
  config.http_client = loopback_;
  auto client = moonbase::one_d4::OneD4V2Client::Create(std::move(config));
  ASSERT_TRUE(client.ok());

  moonbase::one_d4::AnalyzeInput input;
  input.pgn =
      "[Event \"Live Chess\"]\n[White \"alice\"]\n[Black \"bob\"]\n\n"
      "1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n";
  auto output = client->Analyze(input);
  ASSERT_TRUE(output.ok());

  std::vector<std::string> keys;
  for (const auto& [motif, occurrences] : output->occurrences) keys.push_back(motif);
  EXPECT_EQ(output->motifs, keys);
}

}  // namespace
}  // namespace one_d4_v2
