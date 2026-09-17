// Beyoncé Rule wire-contract test (consumer tier), the way golf_wire_test
// pins the hub's stream: the exact bytes deja's `recent` hands back,
// parsed by the real generated client, so a rename on the Rust side fails
// here — in CI, with the field named — instead of in a glasshouse that
// quietly stops showing anything.
//
// kPinnedBody is deja's own `recent_is_pinned_on_the_wire` fixture,
// character for character (domains/ai/apis/deja/src/api.rs). The two are
// meant to be edited together; that is the whole contract.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "domains/ai/libs/deja_cpp/client.h"
#include "opal/core/error.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace {

using deja::Client;

constexpr char kPinnedBody[] =
    R"({"events":[{"seq":2,"ts":1789500001.5,"lane":0,"step":0,"context":["api.muchq.com GET )"
    R"(/iili/v1/r/* 302 browser"],"actual":"api.muchq.com GET /iili/v1/r/* 302 browser",)"
    R"("predictions":{"bigram":[],"net":[{"token":"api.muchq.com GET /iili/v1/r/* 302 browser",)"
    R"("p":0.3377925157546997},{"token":"<unk>","p":0.33110377192497253}]},"surprise":)"
    R"({"bigram":1.0986122886681098,"net":1.085323452949524},"threshold":null,"verdict":"warmup",)"
    R"("ewma_loss":{"bigram":0.0,"net":0.0},"vocab_size":3}]})";

// A warmed-up event: the threshold is a number, the bigram has an opinion,
// and the verdict is one of the judged ones.
constexpr char kAnomalyBody[] =
    R"({"events":[{"seq":1204,"ts":1789500061.25,"lane":7,"step":1000,)"
    R"("context":["muchq.com GET / 200 browser","api.muchq.com POST /games/v2/session 200 browser"],)"
    R"("actual":"muchq.com GET /wp-login.php 404 bot probe","predictions":)"
    R"({"bigram":[{"token":"muchq.com GET / 200 browser","p":0.75}],)"
    R"("net":[{"token":"muchq.com GET / 200 browser","p":0.61}]},)"
    R"("surprise":{"bigram":9.5,"net":8.25},"threshold":6.5,"verdict":"anomaly",)"
    R"("ewma_loss":{"bigram":1.75,"net":1.5},"vocab_size":41}]})";

class OneBody final : public opal::http::HttpClient {
 public:
  explicit OneBody(std::string body) : body_(std::move(body)) {}

  opal::Outcome<opal::http::HttpResponse> Send(
      const opal::http::HttpRequest& /*request*/) override {
    opal::http::HttpResponse response;
    response.status = 200;
    response.body = body_;
    return response;
  }

 private:
  std::string body_;
};

opal::Outcome<moonbase::deja::FetchRecentOutput> Parse(std::string body) {
  opal::ClientConfig config = deja::DefaultClientConfig("http://deja:8093");
  config.http_client = std::make_shared<OneBody>(std::move(body));
  auto client = Client::Create(std::move(config));
  EXPECT_TRUE(client.ok()) << client.error().message();
  return client->Recent(1);
}

TEST(RecentWire, DejasPinnedBodyParsesFieldForField) {
  const auto recent = Parse(kPinnedBody);

  ASSERT_TRUE(recent.ok()) << recent.error().message();
  ASSERT_EQ(recent->events.size(), 1u);
  const auto& event = recent->events[0];
  EXPECT_EQ(event.seq, 2);
  EXPECT_DOUBLE_EQ(event.ts, 1789500001.5);
  EXPECT_EQ(event.lane, 0);
  EXPECT_EQ(event.step, 0);
  ASSERT_EQ(event.context.size(), 1u);
  EXPECT_EQ(event.context[0], "api.muchq.com GET /iili/v1/r/* 302 browser");
  EXPECT_EQ(event.actual, "api.muchq.com GET /iili/v1/r/* 302 browser");
  EXPECT_TRUE(event.predictions.bigram.empty()) << "the bigram has no opinion on a first sighting";
  ASSERT_EQ(event.predictions.net.size(), 2u);
  EXPECT_EQ(event.predictions.net[0].token, "api.muchq.com GET /iili/v1/r/* 302 browser");
  EXPECT_DOUBLE_EQ(event.predictions.net[0].p, 0.3377925157546997);
  EXPECT_EQ(event.predictions.net[1].token, "<unk>");
  EXPECT_DOUBLE_EQ(event.surprise.bigram, 1.0986122886681098);
  EXPECT_DOUBLE_EQ(event.surprise.net, 1.085323452949524);
  // Warming up: the threshold is a literal null, and reads as absent
  // rather than as a zero the hub would compare against.
  EXPECT_FALSE(event.threshold.has_value());
  EXPECT_EQ(event.verdict, "warmup");
  EXPECT_DOUBLE_EQ(event.ewmaLoss.bigram, 0.0);
  EXPECT_DOUBLE_EQ(event.ewmaLoss.net, 0.0);
  EXPECT_EQ(event.vocabSize, 3);
}

TEST(RecentWire, AJudgedEventCarriesItsThresholdAndBothPredictorsGuesses) {
  const auto recent = Parse(kAnomalyBody);

  ASSERT_TRUE(recent.ok()) << recent.error().message();
  ASSERT_EQ(recent->events.size(), 1u);
  const auto& event = recent->events[0];
  EXPECT_EQ(event.seq, 1204);
  EXPECT_EQ(event.lane, 7);
  EXPECT_EQ(event.step, 1000);
  EXPECT_EQ(event.context.size(), 2u);
  EXPECT_EQ(event.actual, "muchq.com GET /wp-login.php 404 bot probe");
  ASSERT_EQ(event.predictions.bigram.size(), 1u);
  EXPECT_EQ(event.predictions.bigram[0].token, "muchq.com GET / 200 browser");
  EXPECT_DOUBLE_EQ(event.predictions.bigram[0].p, 0.75);
  ASSERT_EQ(event.predictions.net.size(), 1u);
  ASSERT_TRUE(event.threshold.has_value());
  EXPECT_DOUBLE_EQ(*event.threshold, 6.5);
  EXPECT_EQ(event.verdict, "anomaly");
  EXPECT_EQ(event.vocabSize, 41);
}

// The negative half, and the reason this file exists: the pin only earns
// its keep if a renamed or dropped field is fatal here. Each case is one
// plausible edit to deja's Event struct.
TEST(RecentWire, ARenameOnTheRustSideFailsTheParse) {
  struct Case {
    const char* name;
    const char* from;
    const char* to;
  };
  const Case renames[] = {
      {"snake_case dropped", R"("ewma_loss":)", R"("ewmaLoss":)"},
      {"vocab_size renamed", R"("vocab_size":)", R"("vocabSize":)"},
      {"seq renamed", R"({"seq":2,)", R"({"sequence":2,)"},
      {"actual renamed", R"("actual":)", R"("token":)"},
      {"verdict dropped", R"("verdict":"warmup",)", ""},
      {"predictions reshaped", R"("predictions":{"bigram":)", R"("predictions":{"ngram":)"},
      {"surprise reshaped", R"("surprise":{"bigram")", R"("surprise":{"control")"},
      {"guesses reshaped", R"("p":0.3377925157546997)", R"("prob":0.3377925157546997)"},
      {"the envelope renamed", R"({"events":)", R"({"tape":)"},
  };
  const std::string pinned = kPinnedBody;
  for (const Case& rename : renames) {
    std::string body = pinned;
    const auto at = body.find(rename.from);
    ASSERT_NE(at, std::string::npos) << rename.name << ": the fixture no longer contains it";
    body.replace(at, std::string(rename.from).size(), rename.to);
    EXPECT_FALSE(Parse(body).ok()) << rename.name << " parsed anyway";
  }
  // The control: the untouched fixture parses, so the failures above are
  // the renames and not a broken harness.
  EXPECT_TRUE(Parse(pinned).ok());
}

// deja is free to grow. A member the hub does not model is ignored, so a
// new field on the Rust side does not take the wall down.
TEST(RecentWire, AFieldDejaAddsIsIgnored) {
  std::string body = kPinnedBody;
  const auto at = body.find(R"("vocab_size":3)");
  ASSERT_NE(at, std::string::npos);
  body.insert(at, R"("entropy":2.5,"lane_age_secs":91.0,)");

  const auto recent = Parse(body);

  ASSERT_TRUE(recent.ok()) << recent.error().message();
  ASSERT_EQ(recent->events.size(), 1u);
  EXPECT_EQ(recent->events[0].vocabSize, 3);
}

}  // namespace
