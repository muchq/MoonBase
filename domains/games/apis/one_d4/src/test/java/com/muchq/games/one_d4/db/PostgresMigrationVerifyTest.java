package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

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
 * What one_d4 runs at boot instead of migrating (#1426). Needs the deployment engine: the check
 * builds the expected schema inside a transaction it rolls back, which only holds where DDL is
 * transactional.
 */
public class PostgresMigrationVerifyTest {

  private static final String SCHEMA = "migration_verify_test";

  private String rawUrl;
  private String jdbcUrl;
  private DataSource dataSource;

  @BeforeEach
  public void setUp() throws Exception {
    rawUrl = PgTestUrls.requireRawUrl();
    jdbcUrl = PgTestUrls.jdbcUrl(rawUrl, SCHEMA);

    exec("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE", null);
    exec("CREATE SCHEMA " + SCHEMA, null);
  }

  @AfterEach
  public void tearDown() throws Exception {
    if (dataSource instanceof Closeable closeable) {
      closeable.close();
      dataSource = null;
    }
    if (rawUrl == null || rawUrl.isBlank()) {
      return;
    }
    exec("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE", null);
  }

  @Test
  public void passesOnceTheMigrateStepHasRun() {
    assertThat(MigrationRunner.run(jdbcUrl, null, null)).isZero();

    assertThatCode(() -> verifier().verify()).doesNotThrowAnyException();
  }

  @Test
  public void failsOnADatabaseNobodyMigrated() {
    assertThatThrownBy(() -> verifier().verify())
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("one_d4_migrate")
        .hasMessageContaining("table indexing_requests");
  }

  @Test
  public void namesTheColumnAMissingStepWouldHaveAdded() throws Exception {
    assertThat(MigrationRunner.run(jdbcUrl, null, null)).isZero();
    exec("ALTER TABLE indexing_requests DROP COLUMN skip_cache CASCADE", SCHEMA);

    assertThatThrownBy(() -> verifier().verify())
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("column indexing_requests.skip_cache");
  }

  @Test
  public void namesTheIndexAMissingStepWouldHaveBuilt() throws Exception {
    assertThat(MigrationRunner.run(jdbcUrl, null, null)).isZero();
    exec("DROP INDEX idx_game_features_white_username", SCHEMA);

    assertThatThrownBy(() -> verifier().verify())
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("index idx_game_features_white_username");
  }

  /**
   * The point of the demotion: boot stops being a writer. A verify that repaired what it found, or
   * that left its scratch schema behind, would be a second writer wearing a different name.
   */
  @Test
  public void writesNothing() throws Exception {
    assertThat(MigrationRunner.run(jdbcUrl, null, null)).isZero();
    exec("ALTER TABLE indexing_requests DROP COLUMN skip_cache CASCADE", SCHEMA);
    exec(
        "INSERT INTO indexing_requests (player, platform, start_month, end_month)"
            + " VALUES ('alice', 'chess.com', '2026-01', '2026-01')",
        SCHEMA);

    assertThatThrownBy(() -> verifier().verify()).isInstanceOf(IllegalStateException.class);
    assertThatThrownBy(() -> verifier().verify()).isInstanceOf(IllegalStateException.class);

    assertThat(
            scalar(
                "SELECT count(*) FROM information_schema.columns WHERE table_schema = '"
                    + SCHEMA
                    + "' AND table_name = 'indexing_requests'"
                    + " AND column_name = 'skip_cache'"))
        .as("the failed verify put the column back")
        .isEqualTo("0");
    assertThat(
            scalar(
                "SELECT count(*) FROM information_schema.schemata"
                    + " WHERE schema_name LIKE 'one_d4_verify_%'"))
        .as("a scratch schema outlived the transaction that built it")
        .isEqualTo("0");
    assertThat(scalar("SELECT count(*) FROM " + SCHEMA + ".indexing_requests"))
        .as("the rows the verify ran over")
        .isEqualTo("1");
  }

  /** One pool per test, closed in {@link #tearDown} — the scratch database has finite slots. */
  private Migration verifier() {
    if (dataSource == null) {
      dataSource = DataSourceFactory.create(jdbcUrl, null, null);
    }
    return new Migration(dataSource);
  }

  private void exec(String sql, String schema) throws Exception {
    try (Connection conn = DriverManager.getConnection(PgTestUrls.jdbcUrl(rawUrl, schema));
        Statement stmt = conn.createStatement()) {
      stmt.execute(sql);
    }
  }

  private String scalar(String sql) throws Exception {
    try (Connection conn = DriverManager.getConnection(PgTestUrls.jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement();
        ResultSet rs = stmt.executeQuery(sql)) {
      rs.next();
      return rs.getString(1);
    }
  }
}
