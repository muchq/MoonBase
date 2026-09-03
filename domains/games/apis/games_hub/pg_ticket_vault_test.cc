#include "domains/games/apis/games_hub/pg_ticket_vault.h"

#include <chrono>
#include <cstdlib>
#include <memory>

#include "absl/status/status.h"
#include "domains/games/apis/games_hub/migrations.h"
#include "domains/platform/libs/pg/pg.h"
#include "gtest/gtest.h"

namespace {

// Real-postgres suite (#1194's honest test story: the risky code is the
// SQL, so no in-memory double). Runs when GAMES_HUB_TEST_DB_URL points at
// a scratch database — e.g.
//   GAMES_HUB_TEST_DB_URL=postgresql://user:pass@localhost:5432/games_hub_test
// and skips otherwise (CI has no postgres in the loop yet).
class PgTicketVaultTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* url = std::getenv("GAMES_HUB_TEST_DB_URL");
    if (url == nullptr || *url == '\0') {
      GTEST_SKIP() << "GAMES_HUB_TEST_DB_URL unset";
    }
    db_ = std::make_shared<pg::Client>(url);
    // Every test re-runs migrations: idempotence is part of the contract.
    ASSERT_TRUE(games_hub::RunMigrations(*db_).ok());
    ASSERT_TRUE(db_->Exec("TRUNCATE tickets, resume_tokens").ok());
  }

  games_hub::PgTicketVault MakeVault(std::chrono::seconds ticket_ttl = std::chrono::seconds(60),
                                     std::chrono::seconds resume_ttl = std::chrono::seconds(60)) {
    return games_hub::PgTicketVault(db_, ticket_ttl, resume_ttl);
  }

  int CountRows(const std::string& table) {
    auto result = db_->Exec("SELECT count(*) FROM " + table);
    return result.ok() ? std::atoi(result->Get(0, 0).value_or("-1").c_str()) : -1;
  }

  std::shared_ptr<pg::Client> db_;
};

TEST_F(PgTicketVaultTest, TicketSpendsExactlyOnce) {
  auto vault = MakeVault();
  const auto ticket = vault.IssueTicket("p-1");
  ASSERT_TRUE(ticket.ok());
  EXPECT_TRUE(vault.PeekTicket(*ticket));
  const auto first = vault.SpendTicket(*ticket);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(*first, "p-1");
  EXPECT_FALSE(vault.PeekTicket(*ticket));
  EXPECT_FALSE(vault.SpendTicket(*ticket).has_value());
}

TEST_F(PgTicketVaultTest, ExpiredTicketNeverSpends) {
  auto vault = MakeVault(/*ticket_ttl=*/std::chrono::seconds(0));
  const auto ticket = vault.IssueTicket("p-1");
  ASSERT_TRUE(ticket.ok());
  EXPECT_FALSE(vault.PeekTicket(*ticket));
  EXPECT_FALSE(vault.SpendTicket(*ticket).has_value());
}

TEST_F(PgTicketVaultTest, ResumeTokenIsMultiUseUntilExpiry) {
  auto vault = MakeVault();
  const auto token = vault.IssueResumeToken("p-2");
  ASSERT_TRUE(token.ok());
  EXPECT_EQ(vault.ResolveResumeToken(*token).value_or(""), "p-2");
  EXPECT_EQ(vault.ResolveResumeToken(*token).value_or(""), "p-2");
  EXPECT_FALSE(vault.ResolveResumeToken("rt-nope").has_value());

  auto expired = MakeVault(std::chrono::seconds(60), /*resume_ttl=*/std::chrono::seconds(0));
  const auto dead = expired.IssueResumeToken("p-3");
  ASSERT_TRUE(dead.ok());
  EXPECT_FALSE(expired.ResolveResumeToken(*dead).has_value());
}

// The point of #1194 step 1: a credential minted by one instance is
// honored by another.
TEST_F(PgTicketVaultTest, CredentialsAreVisibleAcrossVaultInstances) {
  auto minter = MakeVault();
  auto other_instance =
      games_hub::PgTicketVault(std::make_shared<pg::Client>(std::getenv("GAMES_HUB_TEST_DB_URL")),
                               std::chrono::seconds(60), std::chrono::seconds(60));

  const auto ticket = minter.IssueTicket("p-1");
  ASSERT_TRUE(ticket.ok());
  EXPECT_TRUE(other_instance.PeekTicket(*ticket));
  EXPECT_EQ(other_instance.SpendTicket(*ticket).value_or(""), "p-1");
  EXPECT_FALSE(minter.PeekTicket(*ticket));

  const auto token = minter.IssueResumeToken("p-1");
  ASSERT_TRUE(token.ok());
  EXPECT_EQ(other_instance.ResolveResumeToken(*token).value_or(""), "p-1");
}

// An at-rest dump must leak no live credential: rows hold sha256 hashes,
// never the tokens the clients carry.
TEST_F(PgTicketVaultTest, StoresOnlyHashesAtRest) {
  auto vault = MakeVault();
  const auto ticket = vault.IssueTicket("p-1");
  const auto token = vault.IssueResumeToken("p-1");
  ASSERT_TRUE(ticket.ok());
  ASSERT_TRUE(token.ok());

  auto leaked_tickets = db_->Exec("SELECT count(*) FROM tickets WHERE ticket_hash = $1", {*ticket});
  ASSERT_TRUE(leaked_tickets.ok());
  EXPECT_EQ(leaked_tickets->Get(0, 0).value_or(""), "0");
  auto hashed = db_->Exec(
      "SELECT count(*) FROM tickets"
      " WHERE ticket_hash = encode(sha256(convert_to($1, 'UTF8')), 'hex')",
      {*ticket});
  ASSERT_TRUE(hashed.ok());
  EXPECT_EQ(hashed->Get(0, 0).value_or(""), "1");

  auto leaked_tokens =
      db_->Exec("SELECT count(*) FROM resume_tokens WHERE token_hash = $1", {*token});
  ASSERT_TRUE(leaked_tokens.ok());
  EXPECT_EQ(leaked_tokens->Get(0, 0).value_or(""), "0");
  auto hashed_token = db_->Exec(
      "SELECT count(*) FROM resume_tokens"
      " WHERE token_hash = encode(sha256(convert_to($1, 'UTF8')), 'hex')",
      {*token});
  ASSERT_TRUE(hashed_token.ok());
  EXPECT_EQ(hashed_token->Get(0, 0).value_or(""), "1");
}

// The property future schema steps depend on: re-running migrations must
// never disturb existing rows.
TEST_F(PgTicketVaultTest, MigrationsPreserveExistingRows) {
  auto vault = MakeVault();
  const auto ticket = vault.IssueTicket("p-1");
  ASSERT_TRUE(ticket.ok());
  ASSERT_TRUE(games_hub::RunMigrations(*db_).ok());
  EXPECT_EQ(CountRows("tickets"), 1);
  EXPECT_TRUE(vault.PeekTicket(*ticket));
}

TEST_F(PgTicketVaultTest, MintPurgesExpiredRows) {
  auto expiring = MakeVault(/*ticket_ttl=*/std::chrono::seconds(0),
                            /*resume_ttl=*/std::chrono::seconds(0));
  ASSERT_TRUE(expiring.IssueTicket("p-1").ok());
  ASSERT_TRUE(expiring.IssueResumeToken("p-1").ok());
  EXPECT_EQ(CountRows("tickets"), 1);
  EXPECT_EQ(CountRows("resume_tokens"), 1);

  auto vault = MakeVault();
  ASSERT_TRUE(vault.IssueTicket("p-2").ok());
  ASSERT_TRUE(vault.IssueResumeToken("p-2").ok());
  EXPECT_EQ(CountRows("tickets"), 1);
  EXPECT_EQ(CountRows("resume_tokens"), 1);
}

// Outside the fixture on purpose: fail-closed needs no database (the URL
// points at a refusing port), so this one runs even where the gated
// suite skips — CI included.
TEST(PgTicketVaultOutageTest, UnavailableDatabaseFailsClosed) {
  auto dead_db = std::make_shared<pg::Client>("postgresql://127.0.0.1:1/nope?connect_timeout=2");
  games_hub::PgTicketVault vault(dead_db, std::chrono::seconds(60), std::chrono::seconds(60));
  EXPECT_EQ(vault.IssueTicket("p-1").status().code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(vault.IssueResumeToken("p-1").status().code(), absl::StatusCode::kUnavailable);
  EXPECT_FALSE(vault.PeekTicket("t-x"));
  EXPECT_FALSE(vault.SpendTicket("t-x").has_value());
  EXPECT_FALSE(vault.ResolveResumeToken("rt-x").has_value());
}

}  // namespace
