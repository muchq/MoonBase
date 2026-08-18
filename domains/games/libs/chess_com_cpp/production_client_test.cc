#include "domains/games/libs/chess_com_cpp/production_client.h"

#include <gtest/gtest.h>

namespace {

TEST(ProductionClientTest, CanBeConstructedForChessComHttps) {
  const auto client = chess_com::CreateProductionClient();

  EXPECT_TRUE(client.ok()) << client.error().message();
}

}  // namespace
