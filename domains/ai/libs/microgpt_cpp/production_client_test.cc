#include "domains/ai/libs/microgpt_cpp/production_client.h"

#include <gtest/gtest.h>

namespace {

TEST(ProductionClientTest, CanBeConstructedForMicrogptOnTheAppNetwork) {
  const auto client = microgpt::CreateProductionClient("http://microgpt-serve:8087");

  EXPECT_TRUE(client.ok()) << client.error().message();
}

}  // namespace
