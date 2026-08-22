#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/time/time.h"
#include "domains/iili/apis/iili/smithy_handler.h"
#include "domains/iili/apis/iili/test_support.h"
#include "moonbase/iili/client.h"
#include "moonbase/iili/server.h"
#include "smithy/http/loopback.h"
#include "smithy/http/transport.h"

// The bytes on the wire, through the generated server: the v2 contract.

namespace iili {
namespace {

using ::testing::HasSubstr;

// Expiry literals below: 1755003600000 = kNow+1h; 1754000000000 < kNow;
// 1758000000000 > kNow+30d.

class WireTest : public testing::Test {
 protected:
  WireTest()
      : store_(std::make_shared<FakeUrlStore>()),
        shortener_(MakeShortener(store_, [] { return kNow; })) {
    server_ = std::make_unique<moonbase::iili::IiliServer>(
        std::make_shared<SmithyShortenerHandler>(shortener_));
    loopback_ = std::make_shared<smithy::http::Loopback>();
    const auto started = loopback_->Start(WithHeadAsGet(server_->Handler()));
    EXPECT_TRUE(started.ok());
  }

  smithy::http::HttpResponse Send(const std::string& method, const std::string& target,
                                  const std::string& body) {
    smithy::http::HttpRequest request;
    request.method = method;
    request.target = target;
    if (!body.empty()) {
      request.headers.Set("content-type", "application/json");
      request.body = body;
    }
    auto response = loopback_->Send(std::move(request));
    EXPECT_TRUE(response.ok());
    return response.ok() ? *response : smithy::http::HttpResponse{};
  }

  std::shared_ptr<FakeUrlStore> store_;
  std::shared_ptr<Shortener> shortener_;
  std::unique_ptr<moonbase::iili::IiliServer> server_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
};

TEST_F(WireTest, ShortenReturns201WithTheSlugAloneInTheBody) {
  const auto response = Send("POST", "/iili/v1/shorten",
                             R"({"longUrl":"https://www.example.com","expiresAt":1755003600000})");
  EXPECT_EQ(response.status, 201);
  // One key, nothing else; the spelling is the contract.
  EXPECT_EQ(response.body, R"({"slug":"AQA"})");
}

TEST_F(WireTest, AnOmittedExpiryIsA400) {
  const auto response =
      Send("POST", "/iili/v1/shorten", R"({"longUrl":"https://www.example.com"})");
  EXPECT_EQ(response.status, 400);
  EXPECT_THAT(response.body, HasSubstr("expiresAt"));
}

// Trait validation runs before the handler; one case per rule.
TEST_F(WireTest, TheGeneratedServerEnforcesTheLongUrlTraits) {
  // Whole body once: the fieldList shape is the contract.
  const auto tooShort =
      Send("POST", "/iili/v1/shorten", R"({"longUrl":"http://g.c","expiresAt":1755003600000})");
  EXPECT_EQ(tooShort.status, 400);
  EXPECT_EQ(tooShort.body,
            "{\"fieldList\":[{\"message\":\"Value with length 10 at '/longUrl' failed to satisfy"
            " constraint: Member must have length between 11 and 1000, inclusive\","
            "\"path\":\"/longUrl\"}],\"message\":\"1 validation error detected. Value with length"
            " 10 at '/longUrl' failed to satisfy constraint: Member must have length between 11"
            " and 1000, inclusive\"}");

  const auto noProtocol = Send("POST", "/iili/v1/shorten",
                               R"({"longUrl":"www.example.com/path","expiresAt":1755003600000})");
  EXPECT_EQ(noProtocol.status, 400);
  EXPECT_THAT(noProtocol.body, HasSubstr("/longUrl"));

  const auto tooLong = Send("POST", "/iili/v1/shorten",
                            std::string(R"({"longUrl":"https://example.com/)") +
                                std::string(1000, 'a') + R"(","expiresAt":1755003600000})");
  EXPECT_EQ(tooLong.status, 400);

  const auto missing = Send("POST", "/iili/v1/shorten", R"({})");
  EXPECT_EQ(missing.status, 400);
}

// The accepting side of both @length bounds: exactly 11 ("http://g.co" is
// the reason the bound is 11) and exactly 1000.
TEST_F(WireTest, TheLengthBoundsAcceptTheirOwnEdges) {
  const auto shortest =
      Send("POST", "/iili/v1/shorten", R"({"longUrl":"http://g.co","expiresAt":1755003600000})");
  EXPECT_EQ(shortest.status, 201);

  const std::string longest = "https://example.com/" + std::string(980, 'a');
  ASSERT_EQ(longest.size(), 1000u);
  const auto atMax = Send("POST", "/iili/v1/shorten",
                          R"({"longUrl":")" + longest + R"(","expiresAt":1755003600000})");
  EXPECT_EQ(atMax.status, 201);
}

// A store outage is a 500 whose body carries none of the store's text — not
// the 404 v1 collapsed it into. The modeled-400 tests above are the positive
// twin proving error messages do reach bodies when meant to.
TEST_F(WireTest, AStoreFailureIsANonLeaking500NotA404) {
  store_->fail_lookups = true;
  const auto response = Send("GET", "/iili/v1/r/DAA", "");
  EXPECT_GE(response.status, 500);
  EXPECT_THAT(response.body, ::testing::Not(HasSubstr("password")));
  EXPECT_THAT(response.body, ::testing::Not(HasSubstr("shared_postgres")));
}

// The clock rules cannot be traits; they answer as the modeled error, and
// the message names the rule.
TEST_F(WireTest, TheExpiryRulesAnswerAsTheModeledError) {
  const auto past = Send("POST", "/iili/v1/shorten",
                         R"({"longUrl":"https://www.example.com","expiresAt":1754000000000})");
  EXPECT_EQ(past.status, 400);
  EXPECT_THAT(past.body, HasSubstr("expiresAt is in the past"));

  const auto tooFar = Send("POST", "/iili/v1/shorten",
                           R"({"longUrl":"https://www.example.com","expiresAt":1758000000000})");
  EXPECT_EQ(tooFar.status, 400);
  EXPECT_THAT(tooFar.body, HasSubstr("30 days"));
}

// The product. The `{}` body is alloy-conformance-pinned; pin ours too.
TEST_F(WireTest, RedirectIsA302WithLocationAndTheConformancePinnedBody) {
  store_->targets["DAA"] = Target{"https://www.example.com/target", kNow + absl::Hours(1)};

  const auto response = Send("GET", "/iili/v1/r/DAA", "");
  EXPECT_EQ(response.status, 302);
  EXPECT_EQ(response.headers.Get("Location").value_or(""), "https://www.example.com/target");
  EXPECT_EQ(response.body, "{}");
}

// #1433: unfurlers and link checkers lead with HEAD, which the router would
// 405 on its own.
TEST_F(WireTest, HeadGetsTheSameRedirectAsGetWithNoBody) {
  store_->targets["DAA"] = Target{"https://www.example.com/target", kNow + absl::Hours(1)};

  const auto response = Send("HEAD", "/iili/v1/r/DAA", "");
  EXPECT_EQ(response.status, 302);
  EXPECT_EQ(response.headers.Get("Location").value_or(""), "https://www.example.com/target");
  EXPECT_EQ(response.body, "");
}

TEST_F(WireTest, HeadOnAnUnknownSlugIsTheSame404StatusAsGet) {
  const auto response = Send("HEAD", "/iili/v1/r/zzz", "");
  EXPECT_EQ(response.status, 404);
  EXPECT_EQ(response.body, "");
}

// Unknown and expired collapse into one exact 404 body: the reader can't
// probe which it was.
TEST_F(WireTest, UnknownAndExpiredSlugsShareTheOneModeledJson404) {
  const auto unknown = Send("GET", "/iili/v1/r/zzz", "");
  EXPECT_EQ(unknown.status, 404);
  EXPECT_EQ(unknown.body, R"({"message":"no such link"})");

  store_->targets["DAA"] = Target{"https://www.example.com", kNow - absl::Seconds(1)};
  const auto expired = Send("GET", "/iili/v1/r/DAA", "");
  EXPECT_EQ(expired.status, 404);
  EXPECT_EQ(expired.body, unknown.body);
}

// The redirect arrives as a typed success carrying Location — what the
// smithy-cpp pin bump (#187) exists for.
TEST_F(WireTest, TheGeneratedClientRoundTripsBothOperations) {
  smithy::ClientConfig config;
  config.http_client = loopback_;
  auto client = moonbase::iili::IiliClient::Create(std::move(config));
  ASSERT_TRUE(client.ok()) << client.error().message();

  moonbase::iili::ShortenInput shorten;
  shorten.longUrl = "https://www.example.com/page";
  shorten.expiresAt = 1755003600000;
  auto minted = client->Shorten(shorten);
  ASSERT_TRUE(minted.ok()) << minted.error().message();
  EXPECT_EQ(minted->slug, "AQA");

  moonbase::iili::RedirectInput redirect;
  redirect.slug = "AQA";
  auto followed = client->Redirect(redirect);
  ASSERT_TRUE(followed.ok()) << followed.error().message();
  EXPECT_EQ(followed->location, "https://www.example.com/page");
}

TEST_F(WireTest, TheClientReadsTheTypedErrors) {
  smithy::ClientConfig config;
  config.http_client = loopback_;
  auto client = moonbase::iili::IiliClient::Create(std::move(config));
  ASSERT_TRUE(client.ok());

  moonbase::iili::RedirectInput unknown;
  unknown.slug = "zzz";
  auto missed = client->Redirect(unknown);
  ASSERT_FALSE(missed.ok());
  EXPECT_EQ(missed.error().code(), "NotFoundError");

  moonbase::iili::ShortenInput past;
  past.longUrl = "https://www.example.com";
  past.expiresAt = 1754000000000;
  auto refused = client->Shorten(past);
  ASSERT_FALSE(refused.ok());
  EXPECT_EQ(refused.error().code(), "InvalidRequestError");
  ASSERT_NE(refused.error().detail<moonbase::iili::InvalidRequestError>(), nullptr);
  EXPECT_THAT(refused.error().detail<moonbase::iili::InvalidRequestError>()->message,
              HasSubstr("in the past"));
}

}  // namespace
}  // namespace iili
