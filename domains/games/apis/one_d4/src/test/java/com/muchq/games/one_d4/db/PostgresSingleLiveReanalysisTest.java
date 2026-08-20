package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import java.io.Closeable;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * At most one live reanalysis pass, enforced by the database rather than by whoever remembers to
 * check. A partial unique index is a Postgres feature H2 cannot express, so — the same argument
 * {@link PostgresConcurrentWriteTest} makes — asserting it on H2 would pass whether or not the
 * schema is right. Runs against the real postgres CI provides via {@code PG_TEST_DB_URL}; skips
 * when that is unset.
 */
public class PostgresSingleLiveReanalysisTest {

  private static final String DB_URL_ENV = "PG_TEST_DB_URL";
  private static final String SCHEMA = "one_d4_pg_single_live_test";

  private DataSource dataSource;

  @BeforeEach
  public void setUp() throws Exception {
    String rawUrl = System.getenv(DB_URL_ENV);
    assumeTrue(
        rawUrl != null && !rawUrl.isBlank(),
        DB_URL_ENV + " is not set; skipping the real-postgres schema suite");

    try (Connection conn = DriverManager.getConnection(PgTestUrls.jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
      stmt.execute("CREATE SCHEMA " + SCHEMA);
    }
    dataSource = DataSourceFactory.create(PgTestUrls.jdbcUrl(rawUrl, SCHEMA));
    new Migration(dataSource, new PostgresSqlDialect()).run();
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

  private void insert(String status) throws SQLException {
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      stmt.execute("INSERT INTO reanalysis_requests (status) VALUES ('" + status + "')");
    }
  }

  /**
   * The dao's race backstop, against the index it leans on. Check-then-insert means two racers can
   * both see no live row; the index lets one insert land and the dao maps the other's violation
   * back to the winner's row. H2's stand-in index is not unique, so only here does that branch
   * execute.
   */
  @Test
  public void racingEnqueuesConvergeOnOnePass() throws Exception {
    ReanalysisRequestDao dao = new ReanalysisRequestDao(Jdbi.create(dataSource));

    int racers = 8;
    ExecutorService pool = Executors.newFixedThreadPool(racers);
    CountDownLatch start = new CountDownLatch(1);
    Set<UUID> ids = ConcurrentHashMap.newKeySet();
    try {
      for (int i = 0; i < racers; i++) {
        pool.submit(
            () -> {
              start.await();
              ids.add(dao.enqueue().request().id());
              return null;
            });
      }
      start.countDown();
      pool.shutdown();
      assertThat(pool.awaitTermination(30, TimeUnit.SECONDS)).isTrue();
    } finally {
      pool.shutdownNow();
    }

    assertThat(ids).hasSize(1);
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement();
        var rs = stmt.executeQuery("SELECT count(*) FROM reanalysis_requests")) {
      assertThat(rs.next()).isTrue();
      assertThat(rs.getInt(1)).as("one row, not one per racer").isEqualTo(1);
    }
  }

  @Test
  public void aSecondLivePassIsRefusedAtInsert() throws Exception {
    insert("PENDING");
    assertThatThrownBy(() -> insert("PENDING"))
        .as("two PENDING rows are two claimable passes walking one corpus")
        .hasMessageContaining("idx_reanalysis_requests_single_live");
  }

  @Test
  public void aRunningPassBlocksANewOneToo() throws Exception {
    insert("PROCESSING");
    assertThatThrownBy(() -> insert("PENDING"))
        .as("a pass being mid-corpus is exactly when a duplicate costs the most")
        .hasMessageContaining("idx_reanalysis_requests_single_live");
  }

  @Test
  public void aFinishedPassFreesTheSlot() throws Exception {
    insert("PENDING");
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      stmt.execute("UPDATE reanalysis_requests SET status = 'COMPLETED'");
    }
    insert("PENDING");
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      var rs = stmt.executeQuery("SELECT count(*) FROM reanalysis_requests");
      assertThat(rs.next()).isTrue();
      assertThat(rs.getInt(1)).as("history stays; only liveness is unique").isEqualTo(2);
    }
  }
}
