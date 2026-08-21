package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.Statement;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The deploy step end to end on the deployment engine: {@code MigrationRunner.run} against a real
 * Postgres is exactly what the {@code one_d4_migrate} container executes, so this is the success
 * half of the exit contract ({@code MigrationRunnerTest} has the failure half). Run twice because
 * the container runs on every deploy — the second pass over an already-migrated schema is its
 * steady state, not an edge case.
 */
public class PostgresMigrationRunnerTest {

  private static final String SCHEMA = "migration_runner_test";

  private String rawUrl;

  @BeforeEach
  public void setUp() throws Exception {
    rawUrl = System.getenv("PG_TEST_DB_URL");
    assumeTrue(
        rawUrl != null && !rawUrl.isBlank(),
        "PG_TEST_DB_URL is not set; skipping the real-postgres runner suite");

    try (Connection conn = DriverManager.getConnection(PgTestUrls.jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
      stmt.execute("CREATE SCHEMA " + SCHEMA);
    }
  }

  @AfterEach
  public void tearDown() throws Exception {
    if (rawUrl == null || rawUrl.isBlank()) {
      return;
    }
    try (Connection conn = DriverManager.getConnection(PgTestUrls.jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
    }
  }

  @Test
  public void runMigratesAFreshDatabaseAndAgainOverTheResult_returningZeroBothTimes()
      throws Exception {
    String jdbcUrl = PgTestUrls.jdbcUrl(rawUrl, SCHEMA);

    assertThat(MigrationRunner.run(jdbcUrl, null, null)).as("first pass, empty schema").isZero();
    assertThat(MigrationRunner.run(jdbcUrl, null, null))
        .as("second pass, every-deploy re-run")
        .isZero();

    // The gate's promise to the worker: the tables it polls exist once run() returned 0.
    try (Connection conn = DriverManager.getConnection(jdbcUrl);
        Statement stmt = conn.createStatement();
        ResultSet rs =
            stmt.executeQuery(
                "SELECT count(*) FROM indexing_requests"
                    + " UNION ALL SELECT count(*) FROM reanalysis_requests")) {
      assertThat(rs.next()).isTrue();
    }
  }
}
