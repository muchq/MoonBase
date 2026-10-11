#include "domains/games/libs/one_d4_cpp/production_client.h"

#include <gtest/gtest.h>

namespace {

TEST(ProductionClientTest, CanBeConstructedForOneD4OnTheAppNetwork) {
  const auto client = one_d4::CreateProductionClient("http://one_d4:8080");
  EXPECT_TRUE(client.ok()) << client.error().message();
}

}  // namespace
