#include "domains/games/apis/games_hub/ticket_vault.h"

#include <gtest/gtest.h>

#include <chrono>

namespace {

TEST(InMemoryTicketVaultTest, TicketSpendsExactlyOnce) {
  games_hub::InMemoryTicketVault vault(std::chrono::seconds(60), std::chrono::seconds(60));
  const auto ticket = vault.IssueTicket("p-1");
  ASSERT_TRUE(ticket.ok());
  EXPECT_TRUE(vault.PeekTicket(*ticket));
  const auto first = vault.SpendTicket(*ticket);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(*first, "p-1");
  EXPECT_FALSE(vault.PeekTicket(*ticket));
  EXPECT_FALSE(vault.SpendTicket(*ticket).has_value());
}

TEST(InMemoryTicketVaultTest, ExpiredTicketNeverSpends) {
  games_hub::InMemoryTicketVault vault(std::chrono::seconds(0), std::chrono::seconds(60));
  const auto ticket = vault.IssueTicket("p-1");
  ASSERT_TRUE(ticket.ok());
  EXPECT_FALSE(vault.PeekTicket(*ticket));
  EXPECT_FALSE(vault.SpendTicket(*ticket).has_value());
}

TEST(InMemoryTicketVaultTest, ResumeTokenIsMultiUseUntilExpiry) {
  games_hub::InMemoryTicketVault vault(std::chrono::seconds(60), std::chrono::seconds(60));
  const auto token = vault.IssueResumeToken("p-2");
  ASSERT_TRUE(token.ok());
  EXPECT_EQ(vault.ResolveResumeToken(*token).value_or(""), "p-2");
  EXPECT_EQ(vault.ResolveResumeToken(*token).value_or(""), "p-2");
  EXPECT_FALSE(vault.ResolveResumeToken("rt-nope").has_value());

  games_hub::InMemoryTicketVault expired(std::chrono::seconds(60), std::chrono::seconds(0));
  const auto dead = expired.IssueResumeToken("p-3");
  ASSERT_TRUE(dead.ok());
  EXPECT_FALSE(expired.ResolveResumeToken(*dead).has_value());
}

TEST(InMemoryTicketVaultTest, RandomIdsCarryPrefixAndDiffer) {
  const std::string a = games_hub::RandomId("p");
  const std::string b = games_hub::RandomId("p");
  EXPECT_EQ(a.rfind("p-", 0), 0u);
  EXPECT_EQ(a.size(), 14u);
  EXPECT_NE(a, b);
}

}  // namespace
