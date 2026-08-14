package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import java.io.Closeable;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.Statement;
import javax.sql.DataSource;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The retention delete's index on the deployment dialect (#1313 item 11). The hourly sweep runs
 * {@code DELETE FROM game_features WHERE indexed_at < ?} under a 120s statement bound; unindexed, a
 * sweep that hit the bound rolled back with no forward progress and retried the identical scan an
 * hour later. What this pins is that the delete's plan can reach {@code
 * idx_game_features_indexed_at} — same reachability question, and same {@code enable_seqscan = off}
 * technique, as {@code PostgresPlayerIndexTest}.
 *
 * <p>Runs against the real postgres CI provides via {@code PG_TEST_DB_URL}; skips when unset. Uses
 * a dedicated schema like the other PG-gated suites sharing that scratch database.
 */
public class PostgresRetentionIndexTest {

  private static final String DB_URL_ENV = "PG_TEST_DB_URL";
  private static final String SCHEMA = "one_d4_pg_retention_index_test";

  private DataSource dataSource;

  @BeforeEach
  public void setUp() throws Exception {
    String rawUrl = System.getenv(DB_URL_ENV);
    assumeTrue(
        rawUrl != null && !rawUrl.isBlank(),
        DB_URL_ENV + " is not set; skipping the real-postgres retention-index suite");

    try (Connection conn = DriverManager.getConnection(PgTestUrls.jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
      stmt.execute("CREATE SCHEMA " + SCHEMA);
    }

    dataSource = DataSourceFactory.create(PgTestUrls.jdbcUrl(rawUrl, SCHEMA));
    new Migration(dataSource, PostgresSqlDialect.INSTANCE).run();
  }

  @AfterEach
  public void tearDown() throws Exception {
    if (dataSource instanceof Closeable closeable) {
      closeable.close();
    }
    String rawUrl = System.getenv(DB_URL_ENV);
    if (rawUrl == null || rawUrl.isBlank()) {
      return;
    }
    try (Connection conn = DriverManager.getConnection(PgTestUrls.jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
    }
  }

  /**
   * The sweep's exact statement shape, a literal standing in for the bind: the plan must reach the
   * index rather than walk the table. {@code enable_seqscan = off} asks reachability, not the
   * planner's row-count judgement, for the same reasons {@code PostgresPlayerIndexTest} gives.
   */
  @Test
  public void theRetentionDeleteIsServedByTheIndexedAtIndexOnPostgres() throws Exception {
    String plan;
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      stmt.execute("SET enable_seqscan = off");
      StringBuilder sb = new StringBuilder();
      try (ResultSet rs =
          stmt.executeQuery(
              "EXPLAIN DELETE FROM game_features WHERE indexed_at < '2026-01-01T00:00:00'")) {
        while (rs.next()) {
          sb.append(rs.getString(1)).append('\n');
        }
      } finally {
        // Best-effort, same as PostgresPlayerIndexTest: the pool dies with the test, and a reset
        // failure must not replace the EXPLAIN failure that is the actual signal.
        try {
          stmt.execute("SET enable_seqscan = on");
        } catch (java.sql.SQLException ignored) {
          // The connection is already broken; the primary exception is propagating.
        }
      }
      plan = sb.toString();
    }

    assertThat(plan)
        .as("the sweep must walk straight to the expired rows")
        .contains("idx_game_features_indexed_at");
    assertThat(plan).as("and never fall back to the table walk").doesNotContain("Seq Scan");
  }
}
