#include "domains/games/apis/one_d4_worker/db_options.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {
namespace {

using ::testing::HasSubstr;

TEST(DbOptions, BoundsBothWaysAConnectionCanHang) {
  const std::string bounded = WithExecutionBounds("postgresql://host/db");

  EXPECT_THAT(bounded, HasSubstr("statement_timeout%3D120000"));
  EXPECT_THAT(bounded, HasSubstr("tcp_user_timeout=150000"));
  EXPECT_THAT(bounded, HasSubstr("connect_timeout=10"))
      << "libpq waits forever to connect by default, and reconnects are lazy";
  EXPECT_THAT(bounded, HasSubstr("keepalives=1"));
}

TEST(DbOptions, KeepsAQueryStringTheUrlAlreadyHad) {
  const std::string bounded = WithExecutionBounds("postgresql://host/db?sslmode=require");

  EXPECT_THAT(bounded, HasSubstr("sslmode=require"));
  EXPECT_THAT(bounded, HasSubstr("&options="));
  EXPECT_THAT(bounded, ::testing::Not(HasSubstr("?options=")));
}

// The bound is only worth anything if the server honours it. Skips
// without PG_TEST_DB_URL, like the other Postgres suites.
TEST(DbOptions, TheServerActuallyCancelsAStatementThatOverruns) {
  const char* url = std::getenv("PG_TEST_DB_URL");
  if (url == nullptr || *url == '\0') GTEST_SKIP() << "PG_TEST_DB_URL unset";

  // A tenth of a second, so the test does not wait out the real bound.
  const std::string quick =
      absl::StrCat(url, std::string_view(url).find('?') == std::string_view::npos ? "?" : "&",
                   "options=-c%20statement_timeout%3D100");
  pg::Client client(quick);

  // The control. Without it this passes whenever the server is simply
  // unreachable, which is the one condition under which it proves
  // nothing at all — every Exec fails and the bound is never exercised.
  const auto reachable = client.Exec("SELECT 1");
  ASSERT_TRUE(reachable.ok()) << "no server to time anything out: " << reachable.status();

  const auto slept = client.Exec("SELECT pg_sleep(5)");

  EXPECT_FALSE(slept.ok()) << "a statement ran five seconds under a tenth-of-a-second bound";
}

TEST(DbOptions, AStatementInsideTheBoundIsLeftAlone) {
  const char* url = std::getenv("PG_TEST_DB_URL");
  if (url == nullptr || *url == '\0') GTEST_SKIP() << "PG_TEST_DB_URL unset";

  pg::Client client(WithExecutionBounds(url));

  const auto slept = client.Exec("SELECT pg_sleep(0.2)");

  EXPECT_TRUE(slept.ok()) << slept.status();
}

}  // namespace
}  // namespace one_d4_worker
