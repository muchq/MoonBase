#include "domains/games/apis/golf_hub/ticket_vault.h"

#include <gtest/gtest.h>

#include <chrono>

namespace {

TEST(TicketVaultTest, TicketSpendsExactlyOnce) {
  golf_hub::TicketVault vault(std::chrono::seconds(60), std::chrono::seconds(60));
  const std::string ticket = vault.IssueTicket("p-1");
  EXPECT_TRUE(vault.PeekTicket(ticket));
  const auto first = vault.SpendTicket(ticket);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(*first, "p-1");
  EXPECT_FALSE(vault.PeekTicket(ticket));
  EXPECT_FALSE(vault.SpendTicket(ticket).has_value());
}

TEST(TicketVaultTest, ExpiredTicketNeverSpends) {
  golf_hub::TicketVault vault(std::chrono::seconds(0), std::chrono::seconds(60));
  const std::string ticket = vault.IssueTicket("p-1");
  EXPECT_FALSE(vault.PeekTicket(ticket));
  EXPECT_FALSE(vault.SpendTicket(ticket).has_value());
}

TEST(TicketVaultTest, ResumeTokenIsMultiUseUntilExpiry) {
  golf_hub::TicketVault vault(std::chrono::seconds(60), std::chrono::seconds(60));
  const std::string token = vault.IssueResumeToken("p-2");
  EXPECT_EQ(vault.ResolveResumeToken(token).value_or(""), "p-2");
  EXPECT_EQ(vault.ResolveResumeToken(token).value_or(""), "p-2");
  EXPECT_FALSE(vault.ResolveResumeToken("rt-nope").has_value());

  golf_hub::TicketVault expired(std::chrono::seconds(60), std::chrono::seconds(0));
  const std::string dead = expired.IssueResumeToken("p-3");
  EXPECT_FALSE(expired.ResolveResumeToken(dead).has_value());
}

TEST(TicketVaultTest, RandomIdsCarryPrefixAndDiffer) {
  const std::string a = golf_hub::RandomId("p");
  const std::string b = golf_hub::RandomId("p");
  EXPECT_EQ(a.rfind("p-", 0), 0u);
  EXPECT_EQ(a.size(), 14u);
  EXPECT_NE(a, b);
}

}  // namespace
