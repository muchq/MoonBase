package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import java.sql.SQLException;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import org.jdbi.v3.core.statement.SqlLogger;
import org.jdbi.v3.core.statement.StatementContext;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The bounds on the other unattended loops — the dispatch poller's candidate scan and the hourly
 * retention sweep's deletes — observed on the real JDBC statements via Jdbi's logger, the same way
 * GameFeatureDaoTest pins the serving reads. Asserted against literals rather than the constants,
 * so mutating a constant cannot mutate the expectation with it.
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

  @Test
  public void claimNextsCandidateScanIsBoundedAndItsClaimUpdatesAreNot() {
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

    // The claim must succeed, or the "statements after the scan" half below is vacuously true.
    assertThat(claimed).as("the seeded request must be claimable").isPresent();
    assertThat(timeouts.size())
        .as("a successful claim runs the scan plus at least the claim UPDATE")
        .isGreaterThan(1);
    assertThat(timeouts.get(0))
        .as("the candidate scan — the poller's wedge point — must carry the serving-read bound")
        .isEqualTo(10);
    assertThat(timeouts.subList(1, timeouts.size()))
        .as("the claim UPDATE and status reads that follow stay unbounded")
        .allMatch(t -> t == 0);
  }

  @Test
  public void reclaimStaleCarriesTheSweepBoundInsideItsTransaction() {
    timeouts.clear();
    autoCommits.clear();
    new IndexingRequestDao(testDb.jdbi()).reclaimStale(Duration.ofHours(1), Instant.now());

    assertThat(timeouts).as("reclaim ran no statements?").isNotEmpty();
    assertThat(timeouts)
        .as("every settle statement in the reclaim transaction carries the sweep bound")
        .allMatch(t -> t == 120);
    // The "inside its transaction" half: reclaim must use the transactional variant, or the three
    // settle statements stop being a unit. The non-transactional variant compiles in its place,
    // which is exactly why this is a probe rather than prose.
    assertThat(autoCommits)
        .as("reclaim's statements run inside one transaction")
        .allMatch(ac -> !ac);
  }

  @Test
  public void theSweepDeletesAreSingleStatementsOutsideAnyTransaction() {
    timeouts.clear();
    autoCommits.clear();
    new GameFeatureDao(testDb.jdbi(), true).deleteOlderThan(Instant.parse("2026-01-01T00:00:00Z"));

    // Each delete is one self-committing statement — unlike reclaim there is no multi-statement
    // unit to keep atomic, and holding a transaction open across the cascade would only lengthen
    // lock hold times. Pinned so the two shapes stay deliberate rather than accidental.
    assertThat(autoCommits).as("the delete ran no statements?").isNotEmpty();
    assertThat(autoCommits).allMatch(ac -> ac);
  }

  // ---------------------------------------------------------------------------------------------
  // The mechanism itself, tested directly rather than through a DAO.
  // ---------------------------------------------------------------------------------------------

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
    org.assertj.core.api.Assertions.assertThatThrownBy(
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
    org.assertj.core.api.Assertions.assertThatThrownBy(
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
    org.assertj.core.api.Assertions.assertThatThrownBy(
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
                            "SELECT COUNT(*) FROM indexing_requests WHERE player =" + " 'rollback'")
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

  @Test
  public void allThreeRetentionDeletesCarryTheSweepBound() {
    Instant threshold = Instant.parse("2026-01-01T00:00:00Z");

    timeouts.clear();
    new GameFeatureDao(testDb.jdbi(), true).deleteOlderThan(threshold);
    assertThat(timeouts).as("game_features delete").contains(120);

    timeouts.clear();
    new IndexingRequestDao(testDb.jdbi()).deleteOlderThan(threshold);
    assertThat(timeouts).as("indexing_requests delete").contains(120);

    timeouts.clear();
    new IndexedPeriodDao(testDb.jdbi(), true).deleteOlderThan(threshold);
    assertThat(timeouts).as("indexed_periods delete").contains(120);
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
}
