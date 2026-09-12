package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import org.junit.jupiter.api.Test;
import org.opentest4j.TestAbortedException;

/**
 * The gate every one_d4 suite goes through, and the reason dropping H2 does not quietly drop the
 * schema's coverage with it (#1532): a missing database is a skip for a developer and a failure for
 * a CI job. Driven directly, because a suite that gets this wrong reports a pass either way — which
 * is exactly the failure mode being guarded against.
 */
public class PgTestUrlsTest {

  @Test
  public void aConfiguredUrlIsReturned() {
    assertThat(PgTestUrls.requireRawUrl("postgresql://h/db", "1")).isEqualTo("postgresql://h/db");
  }

  @Test
  public void noUrlAndNoCiSkips() {
    assertThatThrownBy(() -> PgTestUrls.requireRawUrl(null, null))
        .as("a developer without a database gets a skip, not a red suite")
        .isInstanceOf(TestAbortedException.class);
    assertThatThrownBy(() -> PgTestUrls.requireRawUrl("  ", ""))
        .isInstanceOf(TestAbortedException.class);
  }

  @Test
  public void noUrlUnderCiFails() {
    assertThatThrownBy(() -> PgTestUrls.requireRawUrl(null, "true"))
        .as("a CI job must not pass by skipping the only suites that run the SQL")
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining(PgTestUrls.DB_URL_ENV)
        .hasMessageContaining(PgTestUrls.REQUIRE);
  }

  @Test
  public void jdbcUrlMovesCredentialsIntoQueryParamsAndScopesTheSchema() {
    assertThat(PgTestUrls.jdbcUrl("postgresql://u:p%40ss@h:5433/db", "s"))
        .isEqualTo("jdbc:postgresql://h:5433/db?user=u&password=p%40ss&currentSchema=s");
    assertThat(PgTestUrls.jdbcUrl("postgresql://h/db", null))
        .as("no credentials, no schema, and the default port")
        .isEqualTo("jdbc:postgresql://h:5432/db");
  }
}
