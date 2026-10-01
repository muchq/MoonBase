#include "domains/games/libs/chess_engine_cpp/production_client.h"

#include <gtest/gtest.h>

namespace {

TEST(ProductionClientTest, CanBeConstructedForChessEngineOnTheAppNetwork) {
  const auto client = chess_engine::CreateProductionClient("http://chess_engine:8094");
  EXPECT_TRUE(client.ok()) << client.error().message();
}

}  // namespace
