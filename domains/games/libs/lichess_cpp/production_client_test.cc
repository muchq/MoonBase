#include "domains/games/libs/lichess_cpp/production_client.h"

#include <gtest/gtest.h>

namespace {

TEST(ProductionClientTest, CanBeConstructedForLichessHttps) {
  const auto client = lichess::CreateProductionClient();

  EXPECT_TRUE(client.ok()) << client.error().message();
}

// A host that indexes only chess.com holds no Lichess token, and the worker
// builds this client either way — so an empty token has to produce a client
// rather than a failure, or one missing secret takes the whole worker down
// with it on startup.
TEST(ProductionClientTest, AnEmptyTokenStillBuildsAClient) {
  const auto client = lichess::CreateProductionClient("");

  EXPECT_TRUE(client.ok()) << client.error().message();
}

}  // namespace
