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
    // The exclusion is as deliberate as the bound: the claim UPDATEs that follow the scan stay
    // unbounded, so wrapping all of claimNext in the serving bound must fail here. Non-empty
    // because a successful claim necessarily ran at least the claiming UPDATE.
    assertThat(timeouts.subList(1, timeouts.size()))
        .as("the claim UPDATEs after the scan stay deliberately unbounded")
        .isNotEmpty()
        .allMatch(t -> t == 0);
  }

  @Test
  public void statusPathServingReadsAreBounded() {
    timeouts.clear();
    new IndexingRequestDao(testDb.jdbi()).listRecent(50);
    assertThat(timeouts)
        .as("listRecent — GET /v1/index's list read")
        .containsExactly(StatementTimeouts.SERVING_READ_SECONDS);

    timeouts.clear();
    new IndexedPeriodDao(testDb.jdbi()).findPeriodsForPlayers(List.of("hikaru"));
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
    new GameFeatureDao(testDb.jdbi()).deleteOlderThan(threshold);
    assertThat(timeouts)
        .as("game_features delete")
        .contains(StatementTimeouts.RETENTION_SWEEP_SECONDS);

    timeouts.clear();
    new IndexingRequestDao(testDb.jdbi()).deleteOlderThan(threshold);
    assertThat(timeouts)
        .as("indexing_requests delete")
        .contains(StatementTimeouts.RETENTION_SWEEP_SECONDS);

    timeouts.clear();
    new IndexedPeriodDao(testDb.jdbi()).deleteOlderThan(threshold);
    assertThat(timeouts)
        .as("indexed_periods delete")
        .contains(StatementTimeouts.RETENTION_SWEEP_SECONDS);
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
  public void withStatementTimeout_propagatesTheBodysException() {
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
}
