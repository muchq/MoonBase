package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.GameFeature;
import java.time.Instant;
import java.util.List;
import java.util.TimeZone;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * ChessQL's {@code date} / {@code month} fields compile to played_at comparisons against UTC
 * day/month boundaries. played_at is a TIMESTAMP column with no zone on both H2 and Postgres, so
 * any binding that routes through an instant converts via the JVM's default zone — and the whole
 * feature is then only correct if the write side and the query side agree on that zone. This suite
 * runs the end-to-end path under a deliberately extreme non-UTC default zone (Pacific/Kiritimati,
 * UTC+14) to prove the day boundaries stay UTC days rather than sliding with the JVM.
 *
 * <p>Agreeing on the JVM zone is not enough, which is why {@link
 * #storedWallClockIsUtcNotJvmLocal()} asserts the wall clock actually on disk rather than a round
 * trip: a symmetric write/read pair cancels the offset out within one process, so a query issued
 * from a JVM in a different zone than the one that indexed the row would still miss it. {@link
 * GameFeatureDao} binds played_at as a zone-free {@link java.time.LocalDateTime} on both sides —
 * the column's own type, so no conversion happens at all — which is what makes the stored value
 * zone-independent.
 *
 * <p>The zone comes from the Bazel target's {@code env = {"TZ": ...}} rather than {@code
 * TimeZone.setDefault} in a {@code @BeforeEach}: H2 caches the default zone globally the first time
 * it converts a value, so a zone changed mid-JVM would not reach the driver and the test would pass
 * vacuously. {@code GameFeatureDaoTest} covers the same path under the container's default zone.
 */
public class PlayedAtTimeZoneTest {

  /** UTC+14 with no DST — the largest offset in the tz database, so any zone leak shows up. */
  private static final String EXPECTED_ZONE = "Pacific/Kiritimati";

  private TestDb testDb;
  private GameFeatureDao dao;
  private UUID requestId;

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("playedattz");
    dao = new GameFeatureDao(testDb.jdbi(), H2SqlDialect.INSTANCE);
    requestId = UUID.randomUUID();
    try (var conn = testDb.dataSource().getConnection();
        var stmt =
            conn.prepareStatement(
                "INSERT INTO indexing_requests (id, player, platform, start_month, end_month,"
                    + " status) VALUES (?, 'p', 'CHESS_COM', '2026-06', '2026-07', 'COMPLETED')")) {
      stmt.setObject(1, requestId);
      stmt.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  /**
   * Guards the rest of the suite: if the target's TZ env stops reaching the JVM, every other
   * assertion here silently degrades into a duplicate of the UTC coverage.
   */
  @Test
  public void testRunsUnderTheIntendedNonUtcZone() {
    assertThat(TimeZone.getDefault().getID()).isEqualTo(EXPECTED_ZONE);
    assertThat(TimeZone.getDefault().getRawOffset()).isEqualTo(14 * 60 * 60 * 1000);
  }

  /**
   * Under UTC+14 these three instants have local calendar days one ahead of their UTC days, so a
   * date rewrite that leaked the JVM zone would put them in the wrong bucket.
   */
  @Test
  public void dateAndMonthBoundariesStayUtcUnderNonUtcJvmZone() {
    dao.insertBatch(
        List.of(
            // 2026-07-01T00:00 local in Kiritimati, but still June 30 in UTC
            gameAt("juneEnd", Instant.parse("2026-06-30T23:59:59.999Z")),
            // 2026-07-01T14:00 local — the first instant of UTC July
            gameAt("julyStart", Instant.parse("2026-07-01T00:00:00.000Z")),
            gameAt("julyMid", Instant.parse("2026-07-15T12:00:00.000Z"))));

    assertThat(urlsMatching("month = \"2026-06\"")).containsExactly("juneEnd");
    assertThat(urlsMatching("month = \"2026-07\"")).containsExactly("julyMid", "julyStart");
    assertThat(urlsMatching("date = \"2026-06-30\"")).containsExactly("juneEnd");
    assertThat(urlsMatching("date = \"2026-07-01\"")).containsExactly("julyStart");
    assertThat(urlsMatching("date <= \"2026-06-30\"")).containsExactly("juneEnd");
    assertThat(urlsMatching("date > \"2026-06-30\"")).containsExactly("julyMid", "julyStart");
    assertThat(urlsMatching("date < \"2026-07-01\"")).containsExactly("juneEnd");
    assertThat(urlsMatching("date >= \"2026-07-01\"")).containsExactly("julyMid", "julyStart");
    assertThat(urlsMatching("date != \"2026-07-01\""))
        .containsExactlyInAnyOrder("juneEnd", "julyMid");
  }

  /**
   * The storage-level assertion the round-trip tests cannot make. played_at holds a wall clock, so
   * the only zone-independent thing to store is the UTC one; reading it back as text goes straight
   * to the stored value with no driver-side Calendar conversion to cancel a zone error out.
   *
   * <p>Under UTC+14 a JVM-local bind would write {@code 2026-07-01 14:00:00} for these instants —
   * self-consistent for this process, and unreadable by a query from any other zone. The
   * millisecond-before instant is included because it is the one that decides which month the game
   * belongs to: stored as local, it reads back as July.
   */
  @Test
  public void storedWallClockIsUtcNotJvmLocal() {
    dao.insertBatch(
        List.of(
            gameAt("julyStart", Instant.parse("2026-07-01T00:00:00Z")),
            gameAt("juneEnd", Instant.parse("2026-06-30T23:59:59.999Z"))));

    assertThat(storedColumn("played_at", "julyStart")).isEqualTo("2026-07-01 00:00:00");
    assertThat(storedColumn("played_at", "juneEnd")).isEqualTo("2026-06-30 23:59:59.999");
  }

  /**
   * indexed_at is what retention compares against, and it used to be written by the database's own
   * {@code now()} while the threshold came from the JVM (#1268). On H2 that was self-consistent —
   * the engine runs in-process and shares the JVM's zone, so both sides shifted together and no
   * round-trip test could see it — but it made the stored value depend on whichever process wrote
   * it. Against a real Postgres in a different zone than the app, retention deleted early.
   *
   * <p>So the assertion has to be on the bytes on disk, not a round trip: under UTC+14 the old path
   * wrote {@code 2026-07-01 14:00:00} for this instant.
   */
  @Test
  public void storedIndexedAtIsUtcNotJvmLocal() {
    Instant indexedAt = Instant.parse("2026-07-01T00:00:00Z");
    dao.insertBatch(List.of(gameAt("indexed", Instant.parse("2026-07-15T12:00:00Z"), indexedAt)));

    assertThat(storedColumn("indexed_at", "indexed")).isEqualTo("2026-07-01 00:00:00");
  }

  /**
   * Retention's threshold rides the same convention as the column, so a row on the far side of the
   * boundary goes and one on the near side stays.
   *
   * <p>The surviving row sits 6 hours past the threshold — deliberately inside the zone's 14-hour
   * offset. Days either side of the boundary would prove nothing: the offset could not move them
   * across it, and a threshold bound through the JVM zone would pass. At 6 hours it does not: the
   * buggy bind shifts the boundary to 14:00 and sweeps a row that is not yet due.
   */
  @Test
  public void retentionComparesLikeWithLikeUnderNonUtcJvmZone() {
    Instant threshold = Instant.parse("2026-07-10T00:00:00Z");
    dao.insertBatch(
        List.of(
            gameAt("due", Instant.parse("2026-07-15T12:00:00Z"), threshold.minusSeconds(3600)),
            gameAt(
                "notDue", Instant.parse("2026-07-15T12:00:00Z"), threshold.plusSeconds(6 * 3600))));

    assertThat(dao.deleteOlderThan(threshold)).isEqualTo(1);
    assertThat(urlsMatching("num.moves >= 0")).containsExactly("notDue");
  }

  /**
   * The stored wall clock as text. {@code CAST(... AS VARCHAR)} renders the column server-side, so
   * neither the JVM's zone nor the driver's Calendar handling can touch the answer.
   */
  private String storedColumn(String column, String gameUrl) {
    try (var conn = testDb.dataSource().getConnection();
        var stmt =
            conn.prepareStatement(
                "SELECT CAST(" + column + " AS VARCHAR) FROM game_features WHERE game_url = ?")) {
      stmt.setString(1, gameUrl);
      try (var rs = stmt.executeQuery()) {
        assertThat(rs.next()).isTrue();
        return rs.getString(1);
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  /** played_at must survive the round trip as the same instant, not shifted by the zone offset. */
  @Test
  public void playedAtRoundTripsAsTheSameInstantUnderNonUtcJvmZone() {
    Instant played = Instant.parse("2026-07-01T00:00:00Z");
    dao.insertBatch(List.of(gameAt("julyStart", played)));

    List<GameFeature> rows =
        dao.query(new SqlCompiler().compile(Parser.parse("month = \"2026-07\"")), 10, 0);
    assertThat(rows).hasSize(1);
    assertThat(rows.get(0).playedAt()).isEqualTo(played);
  }

  /** The aggregate and totals paths carry the same timestamp binds, so they must agree too. */
  @Test
  public void aggregateAndTotalsAgreeOnDateScopeUnderNonUtcJvmZone() {
    dao.insertBatch(
        List.of(
            gameAt("juneEnd", Instant.parse("2026-06-30T23:59:59.999Z")),
            gameAt("julyStart", Instant.parse("2026-07-01T00:00:00.000Z")),
            gameAt("julyMid", Instant.parse("2026-07-15T12:00:00.000Z"))));

    SqlCompiler compiler = new SqlCompiler();
    var parsed = Parser.parse("month = \"2026-07\"");
    var groups =
        dao.aggregate(
            compiler.compileAggregate(parsed, List.of("time_class")), List.of("time_class"), 10);
    var totals =
        dao.aggregateTotals(compiler.compileAggregateTotals(parsed, List.of("time_class")));

    assertThat(groups).hasSize(1);
    assertThat(groups.get(0).count()).isEqualTo(2);
    assertThat(totals.totalGames()).isEqualTo(2);
    assertThat(totals.totalGroups()).isEqualTo(1);
  }

  private List<String> urlsMatching(String chessql) {
    return dao.query(new SqlCompiler().compile(Parser.parse(chessql)), 50, 0).stream()
        .map(GameFeature::gameUrl)
        .sorted()
        .toList();
  }

  private GameFeature gameAt(String url, Instant playedAt) {
    return gameAt(url, playedAt, Instant.now());
  }

  private GameFeature gameAt(String url, Instant playedAt, Instant indexedAt) {
    return new GameFeature(
        null,
        requestId,
        url,
        "CHESS_COM",
        "white",
        "black",
        1500,
        1500,
        null,
        null,
        "blitz",
        "A00",
        null,
        null,
        "1-0",
        playedAt,
        30,
        indexedAt,
        "1. e4 e5 *");
  }
}
