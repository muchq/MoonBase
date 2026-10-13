// TURN's REST credentials (#1590), pinned against vectors computed outside
// this code (Python's hmac): username "<expiry unix seconds>:<playerId>",
// credential base64(HMAC-SHA1(secret, username)) — what coturn's
// use-auth-secret checks. The expiry is the first hour boundary past
// now + ttl, so a player's username holds for the hour and coturn's
// per-user quota with it.

#include "domains/games/apis/games_hub/turn_credentials.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/time/time.h"

namespace games_hub {
namespace {

TEST(TurnCredentialsTest, MintsTheRestApiUsernameAndCredential) {
  TurnConfig config;
  config.urls = {"turn:turn.example:3478?transport=udp", "turn:turn.example:3478?transport=tcp"};
  config.secret = "s3cret";
  const auto server = TurnServerFor(config, "alice", absl::FromUnixSeconds(1700000000));
  EXPECT_EQ(server.urls, config.urls);
  EXPECT_EQ(server.username, "1700089200:alice");
  EXPECT_EQ(server.credential, "P8oP/4GDeid/8CsP3PMQYOO7aSg=");
}

TEST(TurnCredentialsTest, TheSecretAndTheLifetimeBothReachTheCredential) {
  TurnConfig config;
  config.urls = {"turn:turn.example:3478"};
  config.secret = "another secret";
  config.ttl = absl::Minutes(1);
  const auto server = TurnServerFor(config, "bob", absl::FromUnixSeconds(1700000000));
  EXPECT_EQ(server.username, "1700002800:bob");
  EXPECT_EQ(server.credential, "fdsdNb6qT/NMghx0FHG4BDDauDA=");
}

TEST(TurnCredentialsTest, ARejoinWithinTheHourGetsTheSameUsername) {
  TurnConfig config;
  config.urls = {"turn:turn.example:3478"};
  config.secret = "s3cret";
  // 1699999200 and 1700002800 are hour boundaries.
  const auto first = TurnServerFor(config, "alice", absl::FromUnixSeconds(1699999200));
  const auto last = TurnServerFor(config, "alice", absl::FromUnixSeconds(1700002799));
  const auto next = TurnServerFor(config, "alice", absl::FromUnixSeconds(1700002800));
  EXPECT_EQ(first.username, last.username);
  EXPECT_NE(last.username, next.username);
}

TEST(TurnCredentialsTest, CredentialsOutliveADayOfVoice) {
  TurnConfig config;
  config.urls = {"turn:turn.example:3478"};
  config.secret = "s3cret";
  const absl::Time now = absl::FromUnixSeconds(1700002799);
  const std::string username = TurnServerFor(config, "alice", now).username.value();
  const int64_t expiry = std::stoll(username.substr(0, username.find(':')));
  EXPECT_GT(absl::FromUnixSeconds(expiry), now + absl::Hours(24));
}

TEST(TurnCredentialsTest, ConfiguredOnlyWithBothUrlsAndASecret) {
  EXPECT_FALSE(TurnConfigFrom("", "s3cret").has_value());
  EXPECT_FALSE(TurnConfigFrom("turn:turn.example:3478", "").has_value());
  EXPECT_FALSE(TurnConfigFrom(" , ", "s3cret").has_value());
  EXPECT_FALSE(TurnConfigFrom("turn:turn.example:3478", " \t").has_value());
  const auto config =
      TurnConfigFrom(" turn:a.example:3478?transport=udp, turn:a.example:3478 ", "s3cret");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->urls,
            (std::vector<std::string>{"turn:a.example:3478?transport=udp", "turn:a.example:3478"}));
  EXPECT_EQ(config->secret, "s3cret");
  EXPECT_EQ(config->ttl, absl::Hours(24));
}

}  // namespace
}  // namespace games_hub
