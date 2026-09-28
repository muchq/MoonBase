#include "domains/games/libs/mithril_cpp/production_client.h"

#include <gtest/gtest.h>

namespace {

TEST(ProductionClientTest, CanBeConstructedForMithrilOnTheAppNetwork) {
  const auto client = mithril::CreateProductionClient("http://mithril:8083");

  EXPECT_TRUE(client.ok()) << client.error().message();
}

}  // namespace
