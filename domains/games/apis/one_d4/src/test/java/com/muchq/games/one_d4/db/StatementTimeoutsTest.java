package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.sql.SQLException;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import org.jdbi.v3.core.statement.SqlLogger;
import org.jdbi.v3.core.statement.StatementContext;
import org.jdbi.v3.core.statement.UnableToExecuteStatementException;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The timeout mechanism itself, plus the wiring of the unattended loops and status reads onto it —
 * observed on the real JDBC statements via Jdbi's logger. Wiring assertions reference the constants
 * (0 from a path that lost its bound never equals them); the constants' values are pinned exactly
 * once, in {@link #theBoundsAreThePolicyValues}, so changing a bound costs one edit plus whatever
 * pinned arithmetic genuinely breaks.
 */
public class StatementTimeoutsTest {

  private TestDb testDb;
  private final List<Integer> timeouts = new ArrayList<>();
  private final List<Boolean> autoCommits = new ArrayList<>();

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("statementtimeouts");
    testDb
        .jdbi()
        .setSqlLogger(
            new SqlLogger() {
              @Override
              public void logBeforeExecution(StatementContext ctx) {
                try {
                  // Batch statements log with a null statement; only plain statements are probed.
                  var statement = ctx.getStatement();
                  if (statement != null) {
                    timeouts.add(statement.getQueryTimeout());
                    autoCommits.add(statement.getConnection().getAutoCommit());
                  }
                } catch (SQLException e) {
                  throw new RuntimeException(e);
                }
              }
            });
  }

  /**
   * The one home of each bound's value, with its rationale — every wiring assertion elsewhere
   * references the constant. Mutating a constant fails exactly here; losing a bound at a call site
   * fails the wiring probe for that site.
   */
  @Test
  public void theBoundsAreThePolicyValues() {
    assertThat(StatementTimeouts.SERVING_READ_SECONDS)
        .as(
            "far above any LIMIT-bounded read or 8-row poll; FirstPageWarmerTest pins its"
                + " relationship to the warmer tick budget")
        .isEqualTo(10);
    assertThat(StatementTimeouts.RETENTION_SWEEP_SECONDS)
        .as(
            "sized for a sweep, not a page; DataSourceFactoryTest pins that the Postgres socket"
                + " timeout exceeds it")
        .isEqualTo(120);
  }

  @Test
  public void claimNextsCandidateScanIsBounded() {
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi());
    dao.createOrAdopt(
        "poller",
        "CHESS_COM",
        "2026-01",
        "2026-01",
        false,
        false,
        Duration.ofMinutes(5),
        Instant.now());

    timeouts.clear();
    var claimed = dao.claimNext("owner-1", Duration.ofMinutes(5), Instant.now());

    // The claim must succeed so the probe list demonstrably covers a full poll, not a no-op.
    assertThat(claimed).as("the seeded request must be claimable").isPresent();
    assertThat(timeouts).as("claimNext ran no statements?").isNotEmpty();
    assertThat(timeouts.get(0))
        .as("the candidate scan — the poller's wedge point — must carry the serving-read bound")
        .isEqualTo(StatementTimeouts.SERVING_READ_SECONDS);
  }

  @Test
  public void statusPathServingReadsAreBounded() {
    timeouts.clear();
    new IndexingRequestDao(testDb.jdbi()).listRecent(50);
    assertThat(timeouts)
        .as("listRecent — GET /v1/index's list read")
        .containsExactly(StatementTimeouts.SERVING_READ_SECONDS);

    timeouts.clear();
    new IndexedPeriodDao(testDb.jdbi(), true).findPeriodsForPlayers(List.of("hikaru"));
    assertThat(timeouts)
        .as("findPeriodsForPlayers — GET /v1/index's data-availability read")
        .containsExactly(StatementTimeouts.SERVING_READ_SECONDS);
  }

  @Test
  public void reclaimStaleCarriesTheSweepBoundInsideItsTransaction() {
    timeouts.clear();
    autoCommits.clear();
    new IndexingRequestDao(testDb.jdbi()).reclaimStale(Duration.ofHours(1), Instant.now());

    assertThat(timeouts).as("reclaim ran no statements?").isNotEmpty();
    assertThat(timeouts)
        .as("every settle statement in the reclaim transaction carries the sweep bound")
        .allMatch(t -> t == StatementTimeouts.RETENTION_SWEEP_SECONDS);
    // The "inside its transaction" half: reclaim's three settles have order-dependent predicates
    // (releasing stamps updated_at, which hides the staleness the retire arm looks for), so they
    // must stay one unit. The non-transactional variant compiles in its place, which is exactly
    // why this is a probe rather than prose.
    assertThat(autoCommits)
        .as("reclaim's statements run inside one transaction")
        .allMatch(ac -> !ac);
  }

  @Test
  public void allThreeRetentionDeletesCarryTheSweepBound() {
    Instant threshold = Instant.parse("2026-01-01T00:00:00Z");

    timeouts.clear();
    new GameFeatureDao(testDb.jdbi(), true).deleteOlderThan(threshold);
    assertThat(timeouts)
        .as("game_features delete")
        .contains(StatementTimeouts.RETENTION_SWEEP_SECONDS);

    timeouts.clear();
    new IndexingRequestDao(testDb.jdbi()).deleteOlderThan(threshold);
    assertThat(timeouts)
        .as("indexing_requests delete")
        .contains(StatementTimeouts.RETENTION_SWEEP_SECONDS);

    timeouts.clear();
    new IndexedPeriodDao(testDb.jdbi(), true).deleteOlderThan(threshold);
    assertThat(timeouts)
        .as("indexed_periods delete")
        .contains(StatementTimeouts.RETENTION_SWEEP_SECONDS);
  }

  @Test
  public void aBoundedSweepLeavesNoTimeoutOnTheSession() throws Exception {
    new GameFeatureDao(testDb.jdbi(), true).deleteOlderThan(Instant.parse("2026-01-01T00:00:00Z"));

    try (var conn = testDb.dataSource().getConnection();
        var probe = conn.createStatement()) {
      assertThat(probe.getQueryTimeout())
          .as("the sweep bound must not leak to later statements on the pooled connection")
          .isEqualTo(0);
    }
  }

  // ---------------------------------------------------------------------------------------------
  // The mechanism itself, tested directly rather than through a DAO.
  // ---------------------------------------------------------------------------------------------

  /** H2 alias target for the slow-query tests: sleeps per evaluated row. Must be public static. */
  public static int nap(int millis) throws InterruptedException {
    Thread.sleep(millis);
    return 0;
  }

  /**
   * The bound must cancel a statement that is actually running, not merely be set. 400 rows
   * sleeping 20ms each is a ~8s query; a 1s bound has to kill it partway. H2 enforces the timeout
   * cooperatively and only checks every so many processed rows, which is why the fixture needs
   * hundreds of rows — with too few the check never runs and the query completes as if unbounded.
   * The positive twin shares the fixture (same alias, same query shape), so a broken alias cannot
   * masquerade as the bound firing. Postgres-side cancellation is covered by
   * PostgresReadTimeoutTest.
   */
  @Test
  public void slowExecutionIsCancelledAtTheBound() throws Exception {
    try (var conn = testDb.dataSource().getConnection();
        var stmt = conn.createStatement()) {
      stmt.execute(
          "CREATE ALIAS IF NOT EXISTS NAP FOR"
              + " \"com.muchq.games.one_d4.db.StatementTimeoutsTest.nap\"");
      for (int i = 0; i < 400; i++) {
        stmt.execute(
            "INSERT INTO indexed_periods (player, platform, year_month, fetched_at, is_complete,"
                + " games_count) VALUES ('slow-"
                + i
                + "', 'CHESS_COM', '2026-01', now(), true, 0)");
      }
    }

    String slowSql = "SELECT player FROM indexed_periods WHERE NAP(20) = 0";
    long start = System.nanoTime();
    assertThatThrownBy(
            () ->
                StatementTimeouts.withStatementTimeout(
                    testDb.jdbi(), 1, h -> h.createQuery(slowSql).mapTo(String.class).list()))
        .as("a statement outrunning the bound must be cancelled, not awaited")
        .isInstanceOf(UnableToExecuteStatementException.class);
    long elapsedMillis = (System.nanoTime() - start) / 1_000_000;
    assertThat(elapsedMillis)
        .as("cancellation must arrive well before the ~8s the full query needs")
        .isLessThan(5_000);

    // The error path must clear the H2 session too: this is the leak that matters in production
    // — a timed-out statement returning its pooled connection with the bound still set. Without
    // the reset running in a finally (rather than at the end of the try), this probe reads 1.
    try (var conn = testDb.dataSource().getConnection();
        var probe = conn.createStatement()) {
      assertThat(probe.getQueryTimeout())
          .as("a cancelled statement must not leave its bound on the pooled connection")
          .isEqualTo(0);
    }

    // Positive twin: the same query shape under a negligible per-row sleep completes normally,
    // proving the alias and the query work and the failure above is the bound.
    List<String> rows =
        StatementTimeouts.withStatementTimeout(
            testDb.jdbi(),
            1,
            h ->
                h.createQuery("SELECT player FROM indexed_periods WHERE NAP(0) = 0")
                    .mapTo(String.class)
                    .list());
    assertThat(rows).hasSize(400);
  }

  @Test
  public void withStatementTimeout_appliesTheGivenBoundAndReturnsTheBodysValue() {
    timeouts.clear();
    int result =
        StatementTimeouts.withStatementTimeout(
            testDb.jdbi(), 7, h -> h.createQuery("SELECT 42").mapTo(Integer.class).one());

    assertThat(result).isEqualTo(42);
    // 7, not one of the production constants: the entry point must carry whatever it is given.
    assertThat(timeouts).containsExactly(7);
  }

  @Test
  public void withStatementTimeout_propagatesTheBodysExceptionAndStillClearsTheSession()
      throws Exception {
    assertThatThrownBy(
            () ->
                StatementTimeouts.withStatementTimeout(
                    testDb.jdbi(),
                    7,
                    h -> {
                      throw new IllegalStateException("body failure");
                    }))
        .as("the body's own exception must come through untouched")
        .isInstanceOf(IllegalStateException.class)
        .hasMessage("body failure");

    try (var conn = testDb.dataSource().getConnection();
        var probe = conn.createStatement()) {
      assertThat(probe.getQueryTimeout())
          .as("a failing body must still not leak its bound onto the pooled connection")
          .isEqualTo(0);
    }
  }

  /**
   * The log-and-swallow contract of the session cleanup, under an injected cleanup failure: a
   * proxied connection whose plain {@code createStatement()} — the cleanup's only entry — starts
   * failing after the body has run. The body's statements go through {@code prepareStatement}, so
   * only the cleanup is hit. This is the panel finding (a finally-throw replacing the real outcome)
   * pinned by a test rather than fixed on trust.
   */
  @Test
  public void aFailingCleanupNeverReplacesTheBodysOutcome() {
    java.util.concurrent.atomic.AtomicBoolean failCreateStatement =
        new java.util.concurrent.atomic.AtomicBoolean(false);
    org.jdbi.v3.core.Jdbi faultyJdbi =
        org.jdbi.v3.core.Jdbi.create(
            () -> {
              java.sql.Connection real = testDb.dataSource().getConnection();
              return (java.sql.Connection)
                  java.lang.reflect.Proxy.newProxyInstance(
                      getClass().getClassLoader(),
                      new Class<?>[] {java.sql.Connection.class},
                      (proxy, method, args) -> {
                        if (method.getName().equals("createStatement")
                            && (args == null || args.length == 0)
                            && failCreateStatement.get()) {
                          throw new SQLException("injected cleanup failure");
                        }
                        try {
                          return method.invoke(real, args);
                        } catch (java.lang.reflect.InvocationTargetException e) {
                          throw e.getCause();
                        }
                      });
            });

    // Success path: the body's result survives a failed cleanup.
    failCreateStatement.set(false);
    int result =
        StatementTimeouts.withStatementTimeout(
            faultyJdbi,
            7,
            h -> {
              int r = h.createQuery("SELECT 42").mapTo(Integer.class).one();
              failCreateStatement.set(true);
              return r;
            });
    assertThat(result).as("a successful body must not be failed by its cleanup").isEqualTo(42);

    // Error path: the body's exception survives a failed cleanup — not the cleanup's.
    failCreateStatement.set(false);
    assertThatThrownBy(
            () ->
                StatementTimeouts.withStatementTimeout(
                    faultyJdbi,
                    7,
                    h -> {
                      failCreateStatement.set(true);
                      throw new IllegalStateException("the real failure");
                    }))
        .isInstanceOf(IllegalStateException.class)
        .hasMessage("the real failure");
  }

  @Test
  public void inTransactionWithTimeout_isBoundedAndActuallyTransactional() {
    timeouts.clear();
    assertThatThrownBy(
            () ->
                StatementTimeouts.inTransactionWithTimeout(
                    testDb.jdbi(),
                    7,
                    h -> {
                      assertThat(h.isInTransaction()).isTrue();
                      h.createUpdate(
                              "INSERT INTO indexing_requests (id, player, platform, start_month,"
                                  + " end_month) VALUES (:id, 'rollback', 'CHESS_COM', '2026-01',"
                                  + " '2026-01')")
                          .bind("id", java.util.UUID.randomUUID())
                          .execute();
                      throw new IllegalStateException("roll me back");
                    }))
        .hasMessage("roll me back");

    assertThat(timeouts).contains(7);
    Integer survivors =
        testDb
            .jdbi()
            .withHandle(
                h ->
                    h.createQuery(
                            "SELECT COUNT(*) FROM indexing_requests WHERE player = 'rollback'")
                        .mapTo(Integer.class)
                        .one());
    assertThat(survivors)
        .as("a failing body must roll its writes back — the transactional half of the contract")
        .isEqualTo(0);
  }

  @Test
  public void fetchForReanalysisStaysDeliberatelyUnbounded() {
    timeouts.clear();
    new GameFeatureDao(testDb.jdbi(), true).fetchForReanalysis(10, 0);

    assertThat(timeouts).as("fetchForReanalysis ran no statements?").isNotEmpty();
    // The exclusion GameFeatureDao's javadoc states, pinned: the admin batch read pages the whole
    // table to feed a write path and must not silently gain the serving bound.
    assertThat(timeouts).allMatch(t -> t == 0);
  }
}
