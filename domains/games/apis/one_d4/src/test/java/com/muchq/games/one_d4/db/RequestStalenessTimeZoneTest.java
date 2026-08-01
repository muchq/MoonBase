package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.TimeZone;
import java.util.UUID;
import javax.sql.DataSource;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * {@code indexing_requests.created_at} and {@code updated_at} are TIMESTAMP columns with no zone on
 * both H2 and Postgres, and {@link RetentionPolicy#STALE_REQUEST} measures against them across a
 * window of one hour. That window is short enough that a zone leak does not shift a boundary — it
 * inverts the predicate, retiring every healthy request or ignoring every stranded one.
 *
 * <p>This is the same hazard {@code game_features.indexed_at} had (#1268), so this suite is the
 * counterpart to {@link PlayedAtTimeZoneTest} and exists for the same reason its javadoc gives:
 * <b>a symmetric write/read pair cancels the offset out within one process.</b> Every test in
 * {@code IndexingRequestDaoTest} writes its fixture with the same JVM conversion the DAO reads
 * with, so all of them would pass against a DAO that stored local time. The assertions here look at
 * the wall clock actually on disk, and at a threshold computed independently of the DAO.
 *
 * <p>This is not hypothetical for this table in particular. {@code createOrAdopt}'s own javadoc
 * describes REST and MCP as two JVMs against one Postgres; if they disagreed about the stored
 * convention, one would systematically mis-measure the other's requests.
 *
 * <p>The zone comes from the Bazel target's {@code env = {"TZ": ...}} rather than {@code
 * TimeZone.setDefault} in a {@code @BeforeEach}: H2 caches the default zone globally the first time
 * it converts a value, so a zone changed mid-JVM would not reach the driver and this would pass
 * vacuously.
 */
public class RequestStalenessTimeZoneTest {

  /** UTC+14 with no DST — the largest offset in the tz database, so any zone leak shows up. */
  private static final String EXPECTED_ZONE = "Pacific/Kiritimati";

  private static final Instant NOW = Instant.parse("2026-07-01T12:00:00Z");
  private static final Duration STALE_AFTER = Duration.ofHours(1);

  private IndexingRequestDao dao;
  private DataSource dataSource;

  @BeforeEach
  public void setUp() {
    TestDb testDb = TestDb.create("request_staleness_tz");
    dataSource = testDb.dataSource();
    dao = new IndexingRequestDao(testDb.jdbi(), Clock.fixed(NOW, ZoneOffset.UTC));
  }

  @Test
  public void sanityTheTargetActuallyRunsUnderTheNonUtcZone() {
    assertThat(TimeZone.getDefault().getID())
        .as("the Bazel target must set TZ, or every assertion here is vacuous")
        .isEqualTo(EXPECTED_ZONE);
  }

  /**
   * The load-bearing assertion: the value on disk is the UTC wall clock, not the JVM-local one. A
   * DAO that bound through the default zone would store 2026-07-02T02:00 here (UTC+14).
   */
  @Test
  public void storedTimestampsAreUtcWallClocksNotJvmLocal() {
    IndexingRequestStore.Claim claim =
        dao.createOrAdopt("tz", "CHESS_COM", "2026-06", "2026-06", false, STALE_AFTER, NOW);

    LocalDateTime expected = LocalDateTime.of(2026, 7, 1, 12, 0, 0);
    assertThat(rawTimestamp(claim.request().id(), "created_at")).isEqualTo(expected);
    assertThat(rawTimestamp(claim.request().id(), "updated_at")).isEqualTo(expected);
  }

  @Test
  public void updateStatusAlsoStampsAUtcWallClock() {
    IndexingRequestStore.Claim claim =
        dao.createOrAdopt("tz2", "CHESS_COM", "2026-06", "2026-06", false, STALE_AFTER, NOW);
    dao.updateStatus(claim.request().id(), "PROCESSING", null, 1);

    assertThat(rawTimestamp(claim.request().id(), "updated_at"))
        .as("updated_at must not come from a different clock than created_at")
        .isEqualTo(LocalDateTime.of(2026, 7, 1, 12, 0, 0));
  }

  /**
   * Staleness measured against a row whose timestamp this test wrote itself, in UTC, without going
   * through the DAO — so the DAO cannot cancel its own error out.
   */
  @Test
  public void stalenessIsMeasuredInUtcRegardlessOfTheJvmZone() {
    IndexingRequestStore.Claim fresh =
        dao.createOrAdopt("tz3", "CHESS_COM", "2026-06", "2026-06", false, STALE_AFTER, NOW);
    IndexingRequestStore.Claim stale =
        dao.createOrAdopt("tz4", "CHESS_COM", "2026-06", "2026-06", false, STALE_AFTER, NOW);

    // 30 minutes old and 3 hours old, written as UTC wall clocks by hand.
    writeUtcWallClock(fresh.request().id(), LocalDateTime.of(2026, 7, 1, 11, 30));
    writeUtcWallClock(stale.request().id(), LocalDateTime.of(2026, 7, 1, 9, 0));

    assertThat(dao.reclaimStale(STALE_AFTER, NOW)).isEqualTo(1);

    assertThat(dao.findById(stale.request().id()).orElseThrow().status()).isEqualTo("FAILED");
    assertThat(dao.findById(fresh.request().id()).orElseThrow().status()).isEqualTo("PENDING");
  }

  /**
   * The same boundary through the dedupe read. Under a UTC+14 leak the 30-minute-old row would look
   * 14 hours stale and dedupe would stop seeing it, which is what silently disables #1249's guard.
   */
  @Test
  public void findExistingRequestStillSeesAFreshRowUnderANonUtcZone() {
    IndexingRequestStore.Claim claim =
        dao.createOrAdopt("tz5", "CHESS_COM", "2026-06", "2026-06", false, STALE_AFTER, NOW);
    writeUtcWallClock(claim.request().id(), LocalDateTime.of(2026, 7, 1, 11, 30));

    assertThat(
            dao.findExistingRequest(
                "tz5", "CHESS_COM", "2026-06", "2026-06", false, STALE_AFTER, NOW))
        .isPresent();
  }

  private LocalDateTime rawTimestamp(UUID id, String column) {
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement("SELECT " + column + " FROM indexing_requests WHERE id = ?")) {
      ps.setObject(1, id);
      try (var rs = ps.executeQuery()) {
        assertThat(rs.next()).isTrue();
        // getObject(LocalDateTime.class) reads the stored wall clock with no zone conversion —
        // getTimestamp would reintroduce the JVM zone and hide exactly what this is checking.
        return rs.getObject(column, LocalDateTime.class);
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private void writeUtcWallClock(UUID id, LocalDateTime utcWallClock) {
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement("UPDATE indexing_requests SET updated_at = ? WHERE id = ?")) {
      ps.setObject(1, utcWallClock);
      ps.setObject(2, id);
      ps.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
