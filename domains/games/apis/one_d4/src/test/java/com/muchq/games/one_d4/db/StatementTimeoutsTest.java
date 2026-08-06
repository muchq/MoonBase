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
    new IndexingRequestDao(testDb.jdbi()).reclaimStale(Duration.ofHours(1), Instant.now());

    assertThat(timeouts).as("reclaim ran no statements?").isNotEmpty();
    assertThat(timeouts)
        .as("every settle statement in the reclaim transaction carries the sweep bound")
        .allMatch(t -> t == 120);
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
