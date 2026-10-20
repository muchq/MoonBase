#include "domains/games/libs/go_engine_cpp/production_client.h"

#include <gtest/gtest.h>

namespace {

TEST(ProductionClientTest, CanBeConstructedForGoEngineOnTheAppNetwork) {
  const auto client = go_engine::CreateProductionClient("http://go_engine:8095");
  EXPECT_TRUE(client.ok()) << client.error().message();
}

}  // namespace
