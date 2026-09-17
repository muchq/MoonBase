#include "domains/ai/libs/deja_cpp/production_client.h"

#include <gtest/gtest.h>

namespace {

TEST(ProductionClientTest, CanBeConstructedForDejaOnTheAppNetwork) {
  const auto client = deja::CreateProductionClient("http://deja:8093");

  EXPECT_TRUE(client.ok()) << client.error().message();
}

}  // namespace
